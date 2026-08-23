// w10compositor —— Win10DE 合成器入口（M0）
//
// 用法示例：
//   ./w10compositor --width 1920 --height 1080 --frames 5 \
//       --screenshot /tmp/w10de-m0.png
//
// 后端默认 headless；可用 WLR_BACKEND=wayland 嵌套运行（WSLg/weston）。

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>  // strtol
#include <cstring>
#include <string>

#include "compositor/server.h"
#include "ipc/config.h"

namespace {

void printUsage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  --width <px>        headless 输出宽度（默认 1920）\n"
        "  --height <px>       headless 输出高度（默认 1080）\n"
        "  --frames <n>        渲染 n 帧后退出，0 表示无限运行（默认 0；headless 冒烟用）\n"
        "  --screenshot <path> 退出前将输出保存为 PNG 并验证中心像素（默认不保存）\n"
        "  --socket <name>     固定 Wayland socket 名（会话启动用；默认自动生成）\n"
        "  --config <path>     配置文件路径（默认 ~/.config/w10de/config.ini）\n"
        "  --verbose           输出调试日志\n"
        "  --help              显示本帮助\n"
        "环境变量：\n"
        "  WLR_BACKEND         headless | wayland | drm（默认 headless）\n",
        prog);
}

}  // namespace

int main(int argc, char* argv[]) {
    w10de::CompositorOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        // 数字参数解析（lambda 定义在循环内以访问 i）：拒绝非数字与溢出
        //（atoi 会静默返回 0，strtol 需检查 endptr 与 ERANGE）。
        auto parseInt = [&](const char* name) -> int {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "option %s requires an argument\n", name);
                std::exit(1);
            }
            const char* s = argv[++i];
            char* end = nullptr;
            errno = 0;
            const long v = std::strtol(s, &end, 10);
            if (end == s || *end != '\0' || errno == ERANGE || v > INT_MAX || v < INT_MIN) {
                std::fprintf(stderr, "option %s requires a valid number, got '%s'\n", name, s);
                std::exit(1);
            }
            return static_cast<int>(v);
        };
        if (arg == "--width") {
            opts.width = parseInt("--width");
        } else if (arg == "--height") {
            opts.height = parseInt("--height");
        } else if (arg == "--frames") {
            opts.frames = parseInt("--frames");
        } else if (arg == "--screenshot") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "option --screenshot requires an argument\n");
                return 1;
            }
            opts.screenshotPath = argv[++i];
        } else if (arg == "--socket") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "option --socket requires an argument\n");
                return 1;
            }
            opts.socketName = argv[++i];
        } else if (arg == "--config") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "option --config requires an argument\n");
                return 1;
            }
            opts.configPath = argv[++i];
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    // 配置覆盖（在参数校验前应用）：--config 指定，否则默认
    // ~/.config/w10de/config.ini；配置值优先于命令行默认，但命令行
    // 显式参数仍可被配置覆盖——MVP 语义：配置文件为准。
    if (opts.configPath.empty()) {
        if (const char* home = std::getenv("HOME"); home != nullptr) {
            opts.configPath = std::string(home) + "/.config/w10de/config.ini";
        }
    }
    if (!opts.configPath.empty()) {
        const w10de::Config config = w10de::Config::load(opts.configPath);
        opts.width = config.getInt("output", "width", opts.width);
        opts.height = config.getInt("output", "height", opts.height);
    }

    if (opts.width <= 0 || opts.height <= 0) {
        std::fprintf(stderr, "invalid size: %dx%d (must be positive)\n",
                     opts.width, opts.height);
        return 1;
    }
    if (opts.frames < 0) {
        std::fprintf(stderr, "invalid --frames: %d (must be >= 0)\n", opts.frames);
        return 1;
    }
    if (!opts.screenshotPath.empty() && opts.frames == 0) {
        std::fprintf(stderr,
            "--screenshot requires --frames > 0 (截图在渲染指定帧数后执行)\n");
        return 1;
    }

    w10de::Compositor compositor(std::move(opts));
    if (!compositor.init()) {
        std::fprintf(stderr, "compositor init failed\n");
        return 1;
    }
    return compositor.run();
}

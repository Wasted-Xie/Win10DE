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
#include "ipc/shortcuts.h"

namespace {

void printUsage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  --width <px>        headless 输出宽度（默认 1920）\n"
        "  --height <px>       headless 输出高度（默认 1080）\n"
        "  --outputs <n>       headless 输出数量 1-8（默认 1；多显示器排列验证）\n"
        "  --frames <n>        渲染 n 帧后退出，0 表示无限运行（默认 0；headless 冒烟用）\n"
        "  --screenshot <path> 退出前将输出保存为 PNG 并验证中心像素（默认不保存）\n"
        "  --socket <name>     固定 Wayland socket 名（会话启动用；默认自动生成）\n"
        "  --config <path>     配置文件路径（默认 ~/.config/w10de/config.ini）\n"
        "  --workspace <n>     启动工作区 0-3（默认 0；M7 多工作区）\n"
        "  --switch-ws <f>:<n> 渲染到第 f 帧时切换到工作区 n（可重复；headless 验证用）\n"
        "  --snap-test         每个窗口 map 后自动贴左半屏（M8 Aero Snap 验证）\n"
        "  --alttab-test <f>   渲染到第 f 帧时显示 Alt+Tab 切换器（headless 验证）\n"
        "  --snaplayout-test <f> 渲染到第 f 帧时显示 Snap 布局选择器（headless 验证）\n"
        "  --clipboard-test <f> 渲染到第 f 帧时触发剪贴板历史面板（Win+V，headless 验证）\n"
        "  --shortcuts-dump    打印 [shortcuts] 配置生效的快捷键绑定后退出（验证用）\n"
        "  --windowrules-dump  打印 [window_rules] 解析结果后退出（验证用）\n"
        "  --nightlight-test <t> 打印 <t>K 色温 gamma 表采样后退出（验证用）\n"
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
        } else if (arg == "--outputs") {
            opts.outputs = parseInt("--outputs");
            if (opts.outputs < 1 || opts.outputs > 8) {
                std::fprintf(stderr, "invalid --outputs: %d (must be 1-8)\n",
                             opts.outputs);
                return 1;
            }
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
        } else if (arg == "--workspace") {
            opts.initialWorkspace = parseInt("--workspace");
        } else if (arg == "--switch-ws") {
            // 格式 frame:workspace（如 600:1）；帧与工作区均需为合法整数。
            if (i + 1 >= argc) {
                std::fprintf(stderr, "option --switch-ws requires an argument\n");
                return 1;
            }
            const std::string spec = argv[++i];
            const size_t colon = spec.find(':');
            if (colon == std::string::npos) {
                std::fprintf(stderr,
                    "option --switch-ws requires 'frame:workspace', got '%s'\n",
                    spec.c_str());
                return 1;
            }
            char* end = nullptr;
            errno = 0;
            const long frame = std::strtol(spec.c_str(), &end, 10);
            if (end != spec.c_str() + static_cast<std::ptrdiff_t>(colon) ||
                    errno == ERANGE || frame < 1 || frame > INT_MAX) {
                std::fprintf(stderr,
                    "invalid --switch-ws frame in '%s' (must be >= 1; "
                    "帧从 1 起计数)\n", spec.c_str());
                return 1;
            }
            errno = 0;
            const long ws = std::strtol(spec.c_str() + colon + 1, &end, 10);
            if (end == spec.c_str() + colon + 1 || *end != '\0' ||
                    errno == ERANGE || ws < 0 ||
                    ws >= w10de::Compositor::kWorkspaceCount) {
                std::fprintf(stderr,
                    "invalid --switch-ws workspace in '%s' (must be 0..%d)\n",
                    spec.c_str(), w10de::Compositor::kWorkspaceCount - 1);
                return 1;
            }
            opts.workspaceSwitches.emplace_back(static_cast<int>(frame),
                                                static_cast<int>(ws));
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "--snap-test") {
            opts.snapTest = true;
        } else if (arg == "--alttab-test") {
            opts.alttabTestFrame = parseInt("--alttab-test");
        } else if (arg == "--clipboard-test") {
            opts.clipboardTestFrame = parseInt("--clipboard-test");
        } else if (arg == "--snaplayout-test") {
            opts.snaplayoutTestFrame = parseInt("--snaplayout-test");
        } else if (arg == "--shortcuts-dump") {
            opts.shortcutsDump = true;
        } else if (arg == "--windowrules-dump") {
            opts.windowRulesDump = true;
        } else if (arg == "--nightlight-test") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "option --nightlight-test requires an argument\n");
                return 1;
            }
            opts.nightlightTestTemp = parseInt("--nightlight-test");
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
    if (opts.initialWorkspace < 0 ||
            opts.initialWorkspace >= w10de::Compositor::kWorkspaceCount) {
        std::fprintf(stderr, "invalid --workspace: %d (must be 0..%d)\n",
                     opts.initialWorkspace,
                     w10de::Compositor::kWorkspaceCount - 1);
        return 1;
    }
    // --switch-ws 的帧需在 --frames 之前（截图前完成切换才可见）。
    for (const auto& [frame, ws] : opts.workspaceSwitches) {
        (void)ws;
        if (opts.frames > 0 && frame >= opts.frames) {
            std::fprintf(stderr,
                "invalid --switch-ws frame %d: must be < --frames %d\n",
                frame, opts.frames);
            return 1;
        }
    }

    // --shortcuts-dump：打印配置生效的快捷键绑定后退出（headless 验证：
    // 不需要启动后端，仅解析配置）。
    if (opts.shortcutsDump) {
        const w10de::Config config = w10de::Config::load(opts.configPath);
        const auto bindings = w10de::loadShortcuts(config);
        for (int a = 0; a < static_cast<int>(w10de::ShortcutAction::Count); ++a) {
            const auto& b = bindings[static_cast<size_t>(a)];
            std::printf("%-12s mods=0x%02x sym=0x%04x%s\n",
                        w10de::shortcutActionName(static_cast<w10de::ShortcutAction>(a)),
                        b.mods, b.sym, b.valid() ? "" : " (invalid)");
        }
        return 0;
    }

    // --windowrules-dump：打印 [window_rules] 段解析结果后退出（KDE-GAP
    // 中优先 #6：headless 验证规则解析，不启动后端）。
    if (opts.windowRulesDump) {
        const auto rules = w10de::ipc::loadWindowRules(opts.configPath);
        for (const auto& r : rules) {
            std::printf("match(app_id='%s' title='%s') -> "
                        "ontop=%d borderless=%d ws=%d geom=%s\n",
                        r.matchAppId.c_str(), r.matchTitle.c_str(),
                        r.alwaysOnTop ? 1 : 0, r.borderless ? 1 : 0,
                        r.workspace,
                        r.hasGeometry
                            ? (std::to_string(r.geomX) + "," +
                               std::to_string(r.geomY) + "," +
                               std::to_string(r.geomW) + "," +
                               std::to_string(r.geomH)).c_str()
                            : "-");
        }
        return 0;
    }

    // --nightlight-test <temp>：打印指定色温的 gamma 表采样（headless 无
    // gamma 硬件，用算法单测替代像素验证；低优先 Night Light）。
    if (opts.nightlightTestTemp > 0) {
        constexpr size_t kSize = 256;
        std::vector<uint16_t> r(kSize), g(kSize), b(kSize);
        if (!w10de::ipc::buildGammaRamps(opts.nightlightTestTemp, kSize,
                                         r.data(), g.data(), b.data())) {
            std::fprintf(stderr, "invalid temperature\n");
            return 1;
        }
        std::printf("gamma %dK: r[0,128,255]=%u,%u,%u "
                    "g=%u,%u,%u b=%u,%u,%u\n",
                    opts.nightlightTestTemp,
                    r[0], r[128], r[255],
                    g[0], g[128], g[255],
                    b[0], b[128], b[255]);
        return 0;
    }

    w10de::Compositor compositor(std::move(opts));
    if (!compositor.init()) {
        std::fprintf(stderr, "compositor init failed\n");
        return 1;
    }
    return compositor.run();
}

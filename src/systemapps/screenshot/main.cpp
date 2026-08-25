// w10screenshot —— 截图工具（第三批：wlr-screencopy 协议客户端）。
//
// CLI：w10screenshot [--output NAME] [--out PATH]
//   --output：输出名（缺省用第一个 wl_output；HEADLESS-1 等）
//   --out：PNG 保存路径（缺省 /tmp/w10screenshot.png）
// 流程：绑定 zwlr_screencopy_manager_v1(v3) → create_capture_output →
// buffer 事件（尺寸/stride/格式）→ 创建 wl_shm buffer → buffer_done
// 事件后 copy(buffer) → ready → 写 PNG 退出。
//
// 审查（子代理 b8347ac0）修复记录：
//   S1 失败/断线路径守卫：非 ready 状态不再转换/写图（原实现空指针崩溃/假成功）
//   M1 --output 选择：枚举所有 wl_output（v4），按名字匹配（原只绑第一个、参数静默失效）
//   L1 manager bind v3 + copy 移到 buffer_done（协议规范时序）
//   L2 ftruncate/mmap 失败路径 close fd
//   L3 ARGB8888 保留真实 alpha（XRGB 才强制 0xFF）
//   L4 capture_output 第三参数注释更正（overlay_cursor）
//   L5 事件循环 poll 超时兜底（5s）

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-screencopy-unstable-v1-client-protocol.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

struct ScreenshotClient {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_shm* shm = nullptr;
    zwlr_screencopy_manager_v1* manager = nullptr;
    wl_output* output = nullptr;

    // 枚举到的全部输出（M1：bind 所有，按 --output 名字选择）。
    std::vector<wl_output*> allOutputs;
    std::vector<std::string> outputNames;

    // capture 状态
    zwlr_screencopy_frame_v1* frame = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
    uint32_t frameFormat = 0;  // 记录 frame 事件格式（L3：alpha 处理）
    bool yInvert = false;
    int bufFd = -1;
    void* mapped = nullptr;
    size_t mappedSize = 0;
    wl_buffer* buffer = nullptr;
    bool bufferReady = false;
    bool failed = false;

    // 输出选择
    std::string outputName;
    bool gotFrame = false;
};

// 清理所有 Wayland 资源（统一退出路径；各指针可空）。
void cleanupClient(ScreenshotClient* c) {
    if (c->buffer != nullptr) {
        wl_buffer_destroy(c->buffer);
        c->buffer = nullptr;
    }
    if (c->mapped != nullptr) {
        munmap(c->mapped, c->mappedSize);
        c->mapped = nullptr;
    }
    if (c->frame != nullptr) {
        zwlr_screencopy_frame_v1_destroy(c->frame);
        c->frame = nullptr;
    }
    for (wl_output* o : c->allOutputs) {
        if (o != nullptr) {
            wl_output_destroy(o);
        }
    }
    c->allOutputs.clear();
    c->output = nullptr;
    if (c->manager != nullptr) {
        zwlr_screencopy_manager_v1_destroy(c->manager);
        c->manager = nullptr;
    }
    if (c->registry != nullptr) {
        wl_registry_destroy(c->registry);
        c->registry = nullptr;
    }
    if (c->display != nullptr) {
        wl_display_disconnect(c->display);
        c->display = nullptr;
    }
}

void handleFrameBuffer(void* data, zwlr_screencopy_frame_v1* /*frame*/,
                       uint32_t format, uint32_t width, uint32_t height,
                       uint32_t stride) {
    auto* c = static_cast<ScreenshotClient*>(data);
    if (format != WL_SHM_FORMAT_ARGB8888 &&
            format != WL_SHM_FORMAT_XRGB8888) {
        std::fprintf(stderr, "w10screenshot: unsupported format 0x%x\n", format);
        c->failed = true;
        return;
    }
    c->width = static_cast<int>(width);
    c->height = static_cast<int>(height);
    c->stride = static_cast<int>(stride);
    c->frameFormat = format;
    const size_t frameSize = static_cast<size_t>(stride) * height;
    c->mappedSize = frameSize;
    char name[64];
    std::snprintf(name, sizeof(name), "/w10shot-%d", getpid());
    shm_unlink(name);
    c->bufFd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (c->bufFd < 0) {
        std::fprintf(stderr, "w10screenshot: shm_open failed\n");
        c->failed = true;
        return;
    }
    shm_unlink(name);
    if (ftruncate(c->bufFd, static_cast<off_t>(c->mappedSize)) != 0) {
        std::fprintf(stderr, "w10screenshot: ftruncate failed\n");
        close(c->bufFd);  // L2：失败路径释放 fd
        c->bufFd = -1;
        c->failed = true;
        return;
    }
    c->mapped = mmap(nullptr, c->mappedSize, PROT_READ | PROT_WRITE,
                     MAP_SHARED, c->bufFd, 0);
    if (c->mapped == MAP_FAILED) {
        std::fprintf(stderr, "w10screenshot: mmap failed\n");
        close(c->bufFd);  // L2
        c->bufFd = -1;
        c->mapped = nullptr;
        c->failed = true;
        return;
    }
    wl_shm_pool* pool = wl_shm_create_pool(c->shm, c->bufFd,
                                           static_cast<int32_t>(c->mappedSize));
    // buffer 格式必须与 frame 事件一致（否则 compositor 报 invalid buffer
    // format 协议错误——实测）。
    c->buffer = wl_shm_pool_create_buffer(pool, 0, c->width, c->height,
                                          c->stride, format);
    wl_shm_pool_destroy(pool);
    close(c->bufFd);
    c->bufFd = -1;
    if (c->buffer == nullptr) {
        std::fprintf(stderr, "w10screenshot: buffer creation failed\n");
        c->failed = true;
        return;
    }
    // copy 不在本 handler 内：按协议时序等 buffer_done（L1）。
}

void handleFrameFlags(void* data, zwlr_screencopy_frame_v1* /*frame*/,
                      uint32_t flags) {
    auto* c = static_cast<ScreenshotClient*>(data);
    c->yInvert = (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

void handleFrameReady(void* data, zwlr_screencopy_frame_v1* /*frame*/,
                      uint32_t /*tv_sec_hi*/, uint32_t /*tv_sec_lo*/,
                      uint32_t /*tv_nsec*/) {
    auto* c = static_cast<ScreenshotClient*>(data);
    c->bufferReady = true;
    c->gotFrame = true;
}

void handleFrameFailed(void* data, zwlr_screencopy_frame_v1* /*frame*/) {
    auto* c = static_cast<ScreenshotClient*>(data);
    std::fprintf(stderr, "w10screenshot: capture failed\n");
    c->failed = true;
    c->gotFrame = true;
}

void handleFrameBufferDone(void* data, zwlr_screencopy_frame_v1* frame) {
    // v3 buffer_done：server 已处理 buffer 描述（buffer 事件之后、copy 之前
    // 的标准时序）。此时 shm buffer 已创建，发送 copy（L1 合规时序）。
    auto* c = static_cast<ScreenshotClient*>(data);
    if (c->buffer == nullptr || c->frame == nullptr) {
        std::fprintf(stderr, "w10screenshot: buffer_done before buffer\n");
        c->failed = true;
        return;
    }
    zwlr_screencopy_frame_v1_copy(frame, c->buffer);
}

void handleOutputGeometry(void* /*data*/, wl_output* /*output*/, int32_t /*x*/,
                          int32_t /*y*/, int32_t /*physW*/, int32_t /*physH*/,
                          int32_t /*subpixel*/, const char* /*make*/,
                          const char* /*model*/, int32_t /*transform*/) {}
void handleOutputMode(void* /*data*/, wl_output* /*output*/, uint32_t /*flags*/,
                      int32_t /*width*/, int32_t /*height*/, int32_t /*refresh*/) {}
void handleOutputDone(void* /*data*/, wl_output* /*output*/) {}
void handleOutputScale(void* /*data*/, wl_output* /*output*/, int32_t /*factor*/) {}
void handleOutputName(void* data, wl_output* output, const char* name) {
    // M1：记录每个输出的名字（v4 事件），供 --output 匹配。
    auto* c = static_cast<ScreenshotClient*>(data);
    const size_t idx = std::find(c->allOutputs.begin(), c->allOutputs.end(),
                                 output) -
                       c->allOutputs.begin();
    if (idx < c->allOutputs.size() && idx >= c->outputNames.size()) {
        c->outputNames.resize(idx + 1);
    }
    if (idx < c->outputNames.size()) {
        c->outputNames[idx] = name ? name : "";
    }
}
void handleOutputDescription(void* /*data*/, wl_output* /*output*/,
                             const char* /*description*/) {}

void handleRegistryGlobal(void* data, wl_registry* registry, uint32_t name,
                          const char* interface, uint32_t /*version*/) {
    auto* c = static_cast<ScreenshotClient*>(data);
    if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        c->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name,
                                                       &wl_shm_interface, 1));
    } else if (std::strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        // L1：bind v3（buffer_done/linux_dmabuf 事件；wlroots 0.19 支持）。
        c->manager = static_cast<zwlr_screencopy_manager_v1*>(
            wl_registry_bind(registry, name,
                             &zwlr_screencopy_manager_v1_interface, 3));
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        // M1：bind 所有输出（v4 收 name 事件），roundtrip 后统一选择。
        static const wl_output_listener kOutputListener = {
            .geometry = handleOutputGeometry,
            .mode = handleOutputMode,
            .done = handleOutputDone,
            .scale = handleOutputScale,
            .name = handleOutputName,
            .description = handleOutputDescription,
        };
        wl_output* o = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, 4));
        wl_output_add_listener(o, &kOutputListener, c);
        c->allOutputs.push_back(o);
    }
}

void handleRegistryGlobalRemove(void* /*data*/, wl_registry* /*registry*/,
                                uint32_t /*name*/) {}

const wl_registry_listener kRegistryListener = {
    .global = handleRegistryGlobal,
    .global_remove = handleRegistryGlobalRemove,
};

const zwlr_screencopy_frame_v1_listener kFrameListener = {
    .buffer = handleFrameBuffer,
    .flags = handleFrameFlags,
    .ready = handleFrameReady,
    .failed = handleFrameFailed,
    .damage = nullptr,
    .linux_dmabuf = nullptr,
    .buffer_done = handleFrameBufferDone,
};

// ARGB8888/XRGB8888（内存小端 [B,G,R,A]）→ RGBA（stb 期望）。
// 注意：dst 按 x*4 索引（每行逐像素推进），早期版本漏掉递增导致截图只有
// 第一列有内容（黑屏误判，已修复）。
// L3：ARGB8888 保留真实 alpha；XRGB8888 强制 0xFF。
void convertToRgba(const uint8_t* src, int width, int height, int stride,
                   uint32_t format, bool yInvert, std::vector<uint8_t>* rgba) {
    rgba->resize(static_cast<size_t>(width) * height * 4);
    const bool hasAlpha = (format == WL_SHM_FORMAT_ARGB8888);
    for (int y = 0; y < height; ++y) {
        const int srcY = yInvert ? (height - 1 - y) : y;
        const uint8_t* row = src + static_cast<size_t>(srcY) * stride;
        uint8_t* dst = rgba->data() + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            dst[x * 4 + 0] = row[x * 4 + 2];  // R
            dst[x * 4 + 1] = row[x * 4 + 1];  // G
            dst[x * 4 + 2] = row[x * 4 + 0];  // B
            dst[x * 4 + 3] = hasAlpha ? row[x * 4 + 3] : 0xFF;
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    ScreenshotClient c;
    std::string outPath = "/tmp/w10screenshot.png";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            c.outputName = argv[++i];
        } else if (arg == "--help") {
            std::printf("Usage: %s [--output NAME] [--out PATH]\n", argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return 1;
        }
    }

    c.display = wl_display_connect(nullptr);
    if (c.display == nullptr) {
        std::fprintf(stderr, "w10screenshot: cannot connect to Wayland\n");
        return 1;
    }
    c.registry = wl_display_get_registry(c.display);
    wl_registry_add_listener(c.registry, &kRegistryListener, &c);
    wl_display_roundtrip(c.display);
    if (c.shm == nullptr || c.manager == nullptr) {
        std::fprintf(stderr, "w10screenshot: missing wl_shm or screencopy manager\n");
        cleanupClient(&c);
        return 1;
    }
    if (c.allOutputs.empty()) {
        std::fprintf(stderr, "w10screenshot: no output available\n");
        cleanupClient(&c);
        return 1;
    }

    // M1：按 --output 名字选择输出；未指定用第一个；名字不匹配报错。
    c.output = c.allOutputs[0];
    if (!c.outputName.empty()) {
        c.output = nullptr;
        for (size_t i = 0; i < c.allOutputs.size(); ++i) {
            if (i < c.outputNames.size() && c.outputNames[i] == c.outputName) {
                c.output = c.allOutputs[i];
                break;
            }
        }
        if (c.output == nullptr) {
            std::fprintf(stderr, "w10screenshot: no output named '%s' (available:",
                         c.outputName.c_str());
            for (const auto& n : c.outputNames) {
                std::fprintf(stderr, " %s", n.c_str());
            }
            std::fprintf(stderr, ")\n");
            cleanupClient(&c);
            return 1;
        }
    }

    // capture_output(manager, frame_counter, output)：frame_counter=0（不跟踪
    // damage，单次截图）；第三参数为 overlay_cursor（0 = 不合成光标），由
    // capture_output 请求自身携带（L4 注释更正）。
    c.frame = zwlr_screencopy_manager_v1_capture_output(c.manager, 0, c.output);
    zwlr_screencopy_frame_v1_add_listener(c.frame, &kFrameListener, &c);
    wl_display_roundtrip(c.display);  // 触发 buffer/flags/buffer_done → copy
    wl_display_roundtrip(c.display);  // 触发 ready/failed

    // 事件循环直到 ready/failed/断线；poll 超时 5s 兜底（L5：
    // 输出暂停渲染/无 commit 时避免永久阻塞）。
    const int kTimeoutMs = 5000;
    int waitedMs = 0;
    while (!c.gotFrame && !c.failed) {
        if (wl_display_flush(c.display) == -1) {
            break;  // 连接断开
        }
        pollfd pfd{wl_display_get_fd(c.display), POLLIN, 0};
        const int pr = poll(&pfd, 1, 250);
        if (pr == 0) {
            waitedMs += 250;
            if (waitedMs >= kTimeoutMs) {
                std::fprintf(stderr, "w10screenshot: timeout waiting for frame\n");
                c.failed = true;
                break;
            }
            continue;
        }
        if (pr < 0) {
            break;
        }
        if (wl_display_dispatch(c.display) == -1) {
            break;  // 连接断开/协议错误
        }
    }

    // S1 守卫：只有 ready（bufferReady）且映射有效才允许转换/写图；
    // 其余（failed/断线/超时/映射缺失）统一按失败退出，杜绝空指针崩溃与假成功。
    if (!c.bufferReady || c.failed || c.mapped == nullptr || c.buffer == nullptr ||
            c.width <= 0 || c.height <= 0) {
        std::fprintf(stderr, "w10screenshot: capture failed\n");
        cleanupClient(&c);
        return 1;
    }

    std::vector<uint8_t> rgba;
    convertToRgba(static_cast<const uint8_t*>(c.mapped), c.width, c.height,
                  c.stride, c.frameFormat, c.yInvert, &rgba);
    if (stbi_write_png(outPath.c_str(), c.width, c.height, 4, rgba.data(),
                       c.width * 4) == 0) {
        std::fprintf(stderr, "w10screenshot: failed to write %s\n", outPath.c_str());
        cleanupClient(&c);
        return 1;
    }
    std::printf("w10screenshot: saved %dx%d to %s\n", c.width, c.height,
                outPath.c_str());

    cleanupClient(&c);
    return 0;
}

// w10screenshot 捕获核心实现（从原 main.cpp 提取，协议逻辑不变）。
#include "systemapps/screenshot/capture.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-screencopy-unstable-v1-client-protocol.h"

namespace w10shot {

namespace {

struct ScreenshotClient {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_shm* shm = nullptr;
    zwlr_screencopy_manager_v1* manager = nullptr;
    wl_output* output = nullptr;

    std::vector<wl_output*> allOutputs;
    std::vector<std::string> outputNames;

    zwlr_screencopy_frame_v1* frame = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
    uint32_t frameFormat = 0;
    bool yInvert = false;
    int bufFd = -1;
    void* mapped = nullptr;
    size_t mappedSize = 0;
    wl_buffer* buffer = nullptr;
    bool bufferReady = false;
    bool failed = false;

    std::string outputName;
    bool gotFrame = false;
};

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
        close(c->bufFd);
        c->bufFd = -1;
        c->failed = true;
        return;
    }
    c->mapped = mmap(nullptr, c->mappedSize, PROT_READ | PROT_WRITE,
                     MAP_SHARED, c->bufFd, 0);
    if (c->mapped == MAP_FAILED) {
        std::fprintf(stderr, "w10screenshot: mmap failed\n");
        close(c->bufFd);
        c->bufFd = -1;
        c->mapped = nullptr;
        c->failed = true;
        return;
    }
    wl_shm_pool* pool = wl_shm_create_pool(c->shm, c->bufFd,
                                           static_cast<int32_t>(c->mappedSize));
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
    } else if (std::strcmp(interface,
                           zwlr_screencopy_manager_v1_interface.name) == 0) {
        c->manager = static_cast<zwlr_screencopy_manager_v1*>(
            wl_registry_bind(registry, name,
                             &zwlr_screencopy_manager_v1_interface, 3));
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
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
void convertToRgba(const uint8_t* src, int width, int height, int stride,
                   uint32_t format, bool yInvert, std::vector<uint8_t>* rgba) {
    rgba->resize(static_cast<size_t>(width) * height * 4);
    const bool hasAlpha = (format == WL_SHM_FORMAT_ARGB8888);
    for (int y = 0; y < height; ++y) {
        const int srcY = yInvert ? (height - 1 - y) : y;
        const uint8_t* row = src + static_cast<size_t>(srcY) * stride;
        uint8_t* dst = rgba->data() + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            dst[x * 4 + 0] = row[x * 4 + 2];
            dst[x * 4 + 1] = row[x * 4 + 1];
            dst[x * 4 + 2] = row[x * 4 + 0];
            dst[x * 4 + 3] = hasAlpha ? row[x * 4 + 3] : 0xFF;
        }
    }
}

}  // namespace

bool captureOutput(const CaptureOptions& opts, std::vector<uint8_t>* rgba,
                   int* width, int* height, std::string* err) {
    ScreenshotClient c;
    c.outputName = opts.outputName;
    c.display = wl_display_connect(nullptr);
    if (c.display == nullptr) {
        if (err != nullptr) {
            *err = "cannot connect to Wayland";
        }
        return false;
    }
    c.registry = wl_display_get_registry(c.display);
    wl_registry_add_listener(c.registry, &kRegistryListener, &c);
    // 两次 roundtrip：第一次枚举 registry（回调内 bind 各 global，bind
    // 请求在**读取循环中** marshal，roundtrip 结束前未 flush）；第二次
    // flush bind 请求并处理初始事件（wl_output name/geometry/done 等）——
    // 缺第二次会导致 outputNames 为空、--output 匹配静默失效（G2 实测）。
    wl_display_roundtrip(c.display);
    wl_display_roundtrip(c.display);
    if (c.shm == nullptr || c.manager == nullptr) {
        if (err != nullptr) {
            *err = "missing wl_shm or screencopy manager";
        }
        cleanupClient(&c);
        return false;
    }
    if (c.allOutputs.empty()) {
        if (err != nullptr) {
            *err = "no output available";
        }
        cleanupClient(&c);
        return false;
    }

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
            if (err != nullptr) {
                *err = "no output named '" + c.outputName + "'";
            }
            cleanupClient(&c);
            return false;
        }
    }

    c.frame = zwlr_screencopy_manager_v1_capture_output(c.manager, 0, c.output);
    zwlr_screencopy_frame_v1_add_listener(c.frame, &kFrameListener, &c);
    wl_display_roundtrip(c.display);
    wl_display_roundtrip(c.display);

    const int kTimeoutMs = 5000;
    int waitedMs = 0;
    while (!c.gotFrame && !c.failed) {
        if (wl_display_flush(c.display) == -1) {
            break;
        }
        pollfd pfd{wl_display_get_fd(c.display), POLLIN, 0};
        const int pr = poll(&pfd, 1, 250);
        if (pr == 0) {
            waitedMs += 250;
            if (waitedMs >= kTimeoutMs) {
                if (err != nullptr) {
                    *err = "timeout waiting for frame";
                }
                c.failed = true;
                break;
            }
            continue;
        }
        if (pr < 0) {
            break;
        }
        if (wl_display_dispatch(c.display) == -1) {
            break;
        }
    }

    if (!c.bufferReady || c.failed || c.mapped == nullptr || c.buffer == nullptr ||
            c.width <= 0 || c.height <= 0) {
        if (err != nullptr && err->empty()) {
            *err = "capture failed";
        }
        cleanupClient(&c);
        return false;
    }

    std::vector<uint8_t> full;
    convertToRgba(static_cast<const uint8_t*>(c.mapped), c.width, c.height,
                  c.stride, c.frameFormat, c.yInvert, &full);
    cleanupClient(&c);

    // 区域裁剪（钳制到输出内）。
    if (opts.hasRegion) {
        int cw = 0, ch = 0;
        cropRgba(full, c.width, c.height, opts.regionX, opts.regionY,
                 opts.regionW, opts.regionH, rgba, &cw, &ch);
        if (cw <= 0 || ch <= 0) {
            if (err != nullptr) {
                *err = "region outside output bounds";
            }
            return false;
        }
        *width = cw;
        *height = ch;
    } else {
        *rgba = std::move(full);
        *width = c.width;
        *height = c.height;
    }
    return true;
}

void cropRgba(const std::vector<uint8_t>& rgba, int width, int height,
              int x, int y, int w, int h, std::vector<uint8_t>* cropped,
              int* outW, int* outH) {
    cropped->clear();
    *outW = 0;
    *outH = 0;
    if (width <= 0 || height <= 0 || w <= 0 || h <= 0) {
        return;
    }
    // 审查 M1（G2）：x+w / y+h 用 64 位计算避免 int 溢出（用户 --region
    // 输入可达 INT_MAX）。
    const long long xEnd = static_cast<long long>(x) + w;
    const long long yEnd = static_cast<long long>(y) + h;
    // 钳制到 buffer 边界。
    const long long x0ll = std::max(0LL, static_cast<long long>(x));
    const long long y0ll = std::max(0LL, static_cast<long long>(y));
    const long long x1ll = std::min(static_cast<long long>(width), xEnd);
    const long long y1ll = std::min(static_cast<long long>(height), yEnd);
    const int x0 = static_cast<int>(x0ll);
    const int y0 = static_cast<int>(y0ll);
    const int cw = static_cast<int>(x1ll - x0ll);
    const int ch = static_cast<int>(y1ll - y0ll);
    if (cw <= 0 || ch <= 0) {
        return;
    }
    cropped->resize(static_cast<size_t>(cw) * ch * 4);
    for (int row = 0; row < ch; ++row) {
        const size_t srcOff = static_cast<size_t>(y0 + row) * width * 4
            + static_cast<size_t>(x0) * 4;
        const size_t dstOff = static_cast<size_t>(row) * cw * 4;
        std::memcpy(cropped->data() + dstOff, rgba.data() + srcOff,
                    static_cast<size_t>(cw) * 4);
    }
    *outW = cw;
    *outH = ch;
}

}  // namespace w10shot

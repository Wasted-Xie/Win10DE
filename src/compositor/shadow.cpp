#include "compositor/shadow.h"
#include "compositor/util.h"

#include <cstring>
#include <new>  // std::nothrow
#include <drm_fourcc.h>

namespace w10de {

namespace {

void shadowBufferDestroy(struct wlr_buffer* base) {
    ShadowBuffer* b = W10DE_CONTAINER_OF(base, ShadowBuffer, base);
    wlr_buffer_finish(base);
    delete[] b->pixels;
    delete b;
}

bool shadowBeginDataPtrAccess(struct wlr_buffer* base, uint32_t /*flags*/,
                              void** data, uint32_t* format, size_t* stride) {
    ShadowBuffer* b = W10DE_CONTAINER_OF(base, ShadowBuffer, base);
    *data = b->pixels;
    *format = DRM_FORMAT_ARGB8888;
    *stride = b->stride;
    return true;
}

void shadowEndDataPtrAccess(struct wlr_buffer* /*base*/) {}

const struct wlr_buffer_impl kShadowBufferImpl = {
    .destroy = shadowBufferDestroy,
    .begin_data_ptr_access = shadowBeginDataPtrAccess,
    .end_data_ptr_access = shadowEndDataPtrAccess,
};

// 阴影透明度曲线：内缘（紧贴窗口）最浓，向外线性衰减到 0。
// alpha(d/s) = kMaxAlpha * (1 - d/s)，d 为像素到窗口区域的距离。
constexpr uint8_t kMaxShadowAlpha = 92;  // ≈0.36（Win10 阴影观感）

}  // namespace

ShadowBuffer* renderShadow(int contentW, int contentH, int shadowSize) {
    if (contentW <= 0 || contentH <= 0 || shadowSize <= 0) {
        return nullptr;
    }
    const int bufW = contentW + 2 * shadowSize;
    const int bufH = contentH + 2 * shadowSize;
    // 审查：pixels 用 nothrow 分配，失败时释放 buf 返回 nullptr（与现有
    // 错误路径一致，避免 bad_alloc 穿透到 updateShadow 调用点）。
    auto* buf = new ShadowBuffer();
    buf->stride = static_cast<size_t>(bufW) * 4;
    buf->pixels = new (std::nothrow) uint8_t[
        buf->stride * static_cast<size_t>(bufH)];
    if (buf->pixels == nullptr) {
        delete buf;
        return nullptr;
    }

    // 窗口区域（全透明）在内部矩形 [s, s+w) x [s, s+h)。
    // 阴影环：像素到内部矩形的切比雪夫距离 d（方形圆角，视觉接近 Win10
    // 直角窗口阴影）；d >= s 时完全透明。
    const int s = shadowSize;
    const int innerRight = s + contentW;
    const int innerBottom = s + contentH;
    const float invS = 1.0f / static_cast<float>(s);

    for (int y = 0; y < bufH; ++y) {
        const int dy = y < s ? s - y : (y >= innerBottom ? y - (innerBottom - 1) : 0);
        uint8_t* row = buf->pixels + static_cast<size_t>(y) * buf->stride;
        for (int x = 0; x < bufW; ++x) {
            const int dx = x < s ? s - x : (x >= innerRight ? x - (innerRight - 1) : 0);
            // 窗口内部区域（dx==0 && dy==0）：完全透明（alpha=0）。
            if (dx == 0 && dy == 0) {
                row[x * 4 + 0] = 0;
                row[x * 4 + 1] = 0;
                row[x * 4 + 2] = 0;
                row[x * 4 + 3] = 0;
                continue;
            }
            const int d = dx > dy ? dx : dy;
            if (d >= s) {
                // 阴影环外：完全透明。
                row[x * 4 + 0] = 0;
                row[x * 4 + 1] = 0;
                row[x * 4 + 2] = 0;
                row[x * 4 + 3] = 0;
                continue;
            }
            // ARGB8888 内存序 [B,G,R,A]；黑色预乘（RGB=0），alpha 线性衰减。
            const float t = static_cast<float>(d) * invS;
            uint8_t a = static_cast<uint8_t>(static_cast<float>(kMaxShadowAlpha) * (1.0f - t));
            row[x * 4 + 0] = 0;
            row[x * 4 + 1] = 0;
            row[x * 4 + 2] = 0;
            row[x * 4 + 3] = a;
        }
    }

    wlr_buffer_init(&buf->base, &kShadowBufferImpl, bufW, bufH);
    return buf;
}

}  // namespace w10de

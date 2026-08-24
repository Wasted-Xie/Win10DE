// 窗口阴影纹理（M8：Win10 风格窗口阴影）。
//
// 用内存直接填充 ARGB8888 预乘像素的自实现 wlr_buffer（与 titletext 同
// 模式）：内部（窗口区域）全透明，四周 shadowSize 宽的半透明黑渐变环。
// 由 wlr_scene_buffer 渲染上传，挂载在窗口装饰树最底层（z 序在窗口之下、
// 但随窗口整体移动）。
#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
}  // extern "C"

namespace w10de {

struct ShadowBuffer {
    wlr_buffer base;
    uint8_t* pixels = nullptr;  // ARGB8888 预乘，owned
    size_t stride = 0;
};

// 渲染窗口阴影：内容区 contentW×contentH（含标题栏），四周 shadowSize 宽
// 的渐变环。返回新分配的 ShadowBuffer（调用方负责 wlr_buffer_drop）；
// 参数非法时返回 nullptr。
ShadowBuffer* renderShadow(int contentW, int contentH, int shadowSize);

// 默认阴影宽度（逻辑像素）。
constexpr int kShadowSize = 8;

}  // namespace w10de

// M2b 标题栏文字：cairo/pango 渲染 + 自实现 wlr_buffer（scene 显示）。
//
// wlroots 0.19 无 wlr_buffer_from_texture，且 wlr_scene_buffer 渲染时会自动
// 上传 buffer 内容为 texture——因此用内存像素 buffer（DATA_PTR）承载
// cairo/pango 渲染的标题文字，直接交给 wlr_scene_buffer 显示。
#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
}

namespace w10de {

// 内存像素 wlr_buffer（ARGB8888 预乘，与 cairo ARGB32 字节序一致）。
struct TitleTextBuffer {
    struct wlr_buffer base;
    uint8_t* pixels = nullptr;
    size_t stride = 0;
};

// 用 cairo/pango 渲染标题文字到内存 buffer（透明背景、白字、单行省略）。
// text 为空/尺寸非法返回 nullptr。返回的 buffer 已被 init 引用，
// 调用方用 wlr_buffer_drop() 释放（连同像素内存一起销毁）。
TitleTextBuffer* renderTitleText(const char* text, int width, int height,
                                 const float textColor[3]);

}  // namespace w10de

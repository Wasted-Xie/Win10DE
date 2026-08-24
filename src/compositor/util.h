// compositor 工具宏（C++ 版 container_of）
//
// wlroots 的 wl_container_of 用 __typeof__(sample) 推导类型，与 C++ 的
// `auto* x = wl_container_of(listener, x, member)` 写法冲突——auto 推导
// 时 x 尚未声明，报 "use of 'x' before deduction of 'auto'"（真实编译
// 发现，Arch/gcc16 首次编译验证）。本宏以显式类型实例替代 sample，
// 使 __typeof__ 可解析，用法：
//   auto* self = W10DE_CONTAINER_OF(listener, MyType, memberListener_);
#pragma once

#include <wayland-server-core.h>  // wl_container_of

#include "ipc/theme.h"

#define W10DE_CONTAINER_OF(ptr, type, member) \
    wl_container_of((ptr), static_cast<type*>(nullptr), member)

// 主题色 → 预乘浮点颜色（wlr_scene_rect 用；alpha 恒 1.0）。
inline void themeColorToFloat(const w10de::ThemeColor& c, float out[4]) {
    out[0] = c.r / 255.0f;
    out[1] = c.g / 255.0f;
    out[2] = c.b / 255.0f;
    out[3] = 1.0f;
}

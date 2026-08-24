// Win10DE 主题定义（compositor/shell 共用，无 Qt 依赖）。
//
// 主题来源：配置文件 `~/.config/w10de/config.ini` 的 `[theme]` 段。
//   mode = dark | light          # 预设基底（默认 dark = Win10 深色）
//   <color_key> = #RRGGBB        # 任意颜色键覆盖预设（自定义主题通道）
//
// 语义：mode 决定颜色基底（light 取浅色预设，其余取值均回退深色基底），
// 个别键可覆盖基底；未提供的键取对应预设值。两进程读取同一配置，视觉一致。
#pragma once

#include <cstdint>
#include <string>

#include "ipc/config.h"

namespace w10de {

// 8bit RGB 颜色。
struct ThemeColor {
    uint8_t r = 0, g = 0, b = 0;

    bool operator==(const ThemeColor& o) const {
        return r == o.r && g == o.g && b == o.b;
    }
    bool operator!=(const ThemeColor& o) const { return !(*this == o); }
};

// 全部主题颜色键（[theme] 段）。
struct Theme {
    // 任务栏背景。
    ThemeColor taskbarBg;
    // 开始菜单背景。
    ThemeColor menuBg;
    // 开始菜单左侧栏（比主区略深）。
    ThemeColor menuSidebar;
    // 窗口标题栏背景。
    ThemeColor titlebarBg;
    // 窗口按钮背景（最小化/最大化）。
    ThemeColor buttonBg;
    // 窗口按钮 hover 背景。
    ThemeColor buttonHover;
    // 关闭按钮背景。
    ThemeColor closeBg;
    // 关闭按钮 hover 背景。
    ThemeColor closeHover;
    // 主文字。
    ThemeColor textPrimary;
    // 次级文字（时钟等）。
    ThemeColor textSecondary;
    // UI 悬停背景（开始菜单/任务栏项）。
    ThemeColor hoverBg;
    // UI 按下背景。
    ThemeColor pressedBg;
    // 强调色（Win10 蓝）。
    ThemeColor accent;
    // 强调底色上的文字色（激活高亮等；深浅模式均为白，保证对比度）。
    ThemeColor accentText;
    // 桌面背景（compositor 背景矩形）。
    ThemeColor desktopBg;
    // 当前模式（仅记录，加载时已据此取基底）。
    std::string mode = "dark";
};

// 深色预设（Win10 深色：任务栏/标题栏深灰、白字）。
Theme darkTheme();
// 浅色预设（Win10 浅色：浅灰任务栏/标题栏、深字）。
Theme lightTheme();

// 从配置加载主题：mode 选基底 + [theme] 段颜色键覆盖。
// config 为空时返回深色预设。
Theme loadTheme(const Config& config);

// 解析 "#RRGGBB"（允许无 # 前缀）→ ThemeColor；非法返回 false。
bool parseColor(const std::string& text, ThemeColor* out);

}  // namespace w10de

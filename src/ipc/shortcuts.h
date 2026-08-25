// 快捷键配置（第二批：[shortcuts] 配置段驱动 Seat 快捷键，对标 KDE
// System Settings → Shortcuts 的 config 通道）。
//
// 配置格式（~/.config/w10de/config.ini）：
//   [shortcuts]
//   close = win+q
//   lock  = win+l
//   move_left = win+left
// 修饰键：win（LOGO）/ctrl/shift/alt，可组合（"ctrl+shift+a"）；
// 键名：字母/数字/left/right/up/down/tab/escape/space 等。
// 未配置的动作保持默认绑定（与原硬编码一致）。
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace w10de {

class Config;

// 动作清单（Win10/KDE 常见快捷键；Alt+Tab 语义特殊，保持固定不配置化）。
enum class ShortcutAction {
    Close,          // 关闭聚焦窗口
    Maximize,       // 最大化/还原
    Minimize,       // 最小化/还原
    SnapLeft,       // 左半屏
    SnapRight,      // 右半屏
    SnapUp,         // 最大化（Win+↑ 语义）
    SnapDown,       // 还原
    Lock,           // 锁屏
    Quit,           // 退出合成器
    Clipboard,      // 剪贴板历史（Win+V）
    Workspace1,     // 切换工作区 1-4
    Workspace2,
    Workspace3,
    Workspace4,
    Count,
};

// 动作名 → 枚举（配置键名）。
const char* shortcutActionName(ShortcutAction action);

// 按键绑定（解析后的修饰键组合 + xkb keysym）。
struct ShortcutBinding {
    uint32_t mods = 0;    // wlr 修饰键位（WLR_MODIFIER_LOGO/CTRL/SHIFT/ALT）
    uint32_t sym = 0;     // xkb keysym（XKB_KEY_q 等；0 = 未配置）
    bool valid() const { return sym != 0; }
};

// 从配置加载快捷键映射（缺省项用默认绑定）。
// 返回每个动作的绑定（始终填满 Count 项）。
std::vector<ShortcutBinding> loadShortcuts(const Config& config);

// 解析 "win+q" / "ctrl+shift+a" 等文本 → 绑定；非法返回无效绑定。
ShortcutBinding parseShortcut(const std::string& text);

}  // namespace w10de

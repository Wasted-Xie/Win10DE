#include "ipc/shortcuts.h"
#include "ipc/config.h"

#include <cctype>
#include <cstdio>   // fprintf（绑定冲突告警）
#include <cstring>

// xkb keysym 头（wlroots 依赖 xkbcommon；compositor 已链接）。
#include <xkbcommon/xkbcommon-keysyms.h>

namespace w10de {

namespace {

// 修饰键/键名解析表。
struct ModEntry {
    const char* name;
    uint32_t flag;
};
const ModEntry kMods[] = {
    {"win", 0x40 /* WLR_MODIFIER_LOGO */},
    {"logo", 0x40},
    {"super", 0x40},
    {"ctrl", 0x04 /* WLR_MODIFIER_CTRL */},
    {"control", 0x04},
    {"shift", 0x01 /* WLR_MODIFIER_SHIFT */},
    {"alt", 0x08 /* WLR_MODIFIER_ALT */},
};

// 键名 → keysym（小写字母/数字直接映射；特殊键查表）。
uint32_t keySymForName(const std::string& name) {
    if (name.size() == 1) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
        if (c >= 'a' && c <= 'z') {
            return XKB_KEY_a + (c - 'a');
        }
        if (c >= '0' && c <= '9') {
            return XKB_KEY_0 + (c - '0');
        }
        return 0;
    }
    struct KeyEntry {
        const char* name;
        uint32_t sym;
    };
    static const KeyEntry kKeys[] = {
        {"left", XKB_KEY_Left}, {"right", XKB_KEY_Right},
        {"up", XKB_KEY_Up}, {"down", XKB_KEY_Down},
        {"tab", XKB_KEY_Tab}, {"escape", XKB_KEY_Escape}, {"esc", XKB_KEY_Escape},
        {"space", XKB_KEY_space}, {"return", XKB_KEY_Return},
        {"enter", XKB_KEY_Return}, {"backspace", XKB_KEY_BackSpace},
        {"delete", XKB_KEY_Delete}, {"home", XKB_KEY_Home}, {"end", XKB_KEY_End},
        {"f1", XKB_KEY_F1}, {"f2", XKB_KEY_F2}, {"f3", XKB_KEY_F3},
        {"f4", XKB_KEY_F4}, {"f5", XKB_KEY_F5}, {"f6", XKB_KEY_F6},
        {"f7", XKB_KEY_F7}, {"f8", XKB_KEY_F8}, {"f9", XKB_KEY_F9},
        {"f10", XKB_KEY_F10}, {"f11", XKB_KEY_F11}, {"f12", XKB_KEY_F12},
        {"insert", XKB_KEY_Insert}, {"pgup", XKB_KEY_Page_Up},
        {"pageup", XKB_KEY_Page_Up}, {"pgdn", XKB_KEY_Page_Down},
        {"pagedown", XKB_KEY_Page_Down}, {"capslock", XKB_KEY_Caps_Lock},
        {"minus", XKB_KEY_minus}, {"equal", XKB_KEY_equal},
        {"comma", XKB_KEY_comma}, {"period", XKB_KEY_period},
        {"slash", XKB_KEY_slash}, {"semicolon", XKB_KEY_semicolon},
        {"apostrophe", XKB_KEY_apostrophe}, {"backslash", XKB_KEY_backslash},
        {"bracketleft", XKB_KEY_bracketleft}, {"bracketright", XKB_KEY_bracketright},
        {"grave", XKB_KEY_grave},
    };
    for (const auto& k : kKeys) {
        if (name == k.name) {
            return k.sym;
        }
    }
    return 0;
}

// 默认绑定（与原 seat.cpp 硬编码一致——配置化后默认值不变）。
ShortcutBinding defaultBinding(ShortcutAction action) {
    ShortcutBinding b;
    b.mods = 0x40;  // WLR_MODIFIER_LOGO
    switch (action) {
    case ShortcutAction::Close: b.sym = XKB_KEY_q; break;
    case ShortcutAction::Maximize: b.sym = XKB_KEY_f; break;
    case ShortcutAction::Minimize: b.sym = XKB_KEY_m; break;
    case ShortcutAction::SnapLeft: b.sym = XKB_KEY_Left; break;
    case ShortcutAction::SnapRight: b.sym = XKB_KEY_Right; break;
    case ShortcutAction::SnapUp: b.sym = XKB_KEY_Up; break;
    case ShortcutAction::SnapDown: b.sym = XKB_KEY_Down; break;
    case ShortcutAction::SnapLayout: b.sym = XKB_KEY_z; break;
    case ShortcutAction::Lock: b.sym = XKB_KEY_l; break;
    case ShortcutAction::Quit: b.sym = XKB_KEY_Escape; break;
    case ShortcutAction::Clipboard: b.sym = XKB_KEY_v; break;
    case ShortcutAction::Workspace1: b.sym = XKB_KEY_1; break;
    case ShortcutAction::Workspace2: b.sym = XKB_KEY_2; break;
    case ShortcutAction::Workspace3: b.sym = XKB_KEY_3; break;
    case ShortcutAction::Workspace4: b.sym = XKB_KEY_4; break;
    default: break;
    }
    return b;
}

}  // namespace

const char* shortcutActionName(ShortcutAction action) {
    switch (action) {
    case ShortcutAction::Close: return "close";
    case ShortcutAction::Maximize: return "maximize";
    case ShortcutAction::Minimize: return "minimize";
    case ShortcutAction::SnapLeft: return "move_left";
    case ShortcutAction::SnapRight: return "move_right";
    case ShortcutAction::SnapUp: return "move_up";
    case ShortcutAction::SnapDown: return "move_down";
    case ShortcutAction::SnapLayout: return "snap_layout";
    case ShortcutAction::Lock: return "lock";
    case ShortcutAction::Quit: return "quit";
    case ShortcutAction::Clipboard: return "clipboard";
    case ShortcutAction::Workspace1: return "workspace_1";
    case ShortcutAction::Workspace2: return "workspace_2";
    case ShortcutAction::Workspace3: return "workspace_3";
    case ShortcutAction::Workspace4: return "workspace_4";
    default: return "";
    }
}

ShortcutBinding parseShortcut(const std::string& text) {
    ShortcutBinding b;
    // 按 '+' 拆分；trim 每个 token 的首尾空白（审查 L6）。
    std::vector<std::string> parts;
    std::string cur;
    const auto flush = [&] {
        size_t start = 0;
        while (start < cur.size() &&
               std::isspace(static_cast<unsigned char>(cur[start]))) {
            ++start;
        }
        size_t end = cur.size();
        while (end > start &&
               std::isspace(static_cast<unsigned char>(cur[end - 1]))) {
            --end;
        }
        parts.push_back(cur.substr(start, end - start));
        cur.clear();
    };
    for (char c : text) {
        if (c == '+') {
            flush();
        } else {
            cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    flush();
    if (parts.size() < 2) {
        return b;  // 无效（至少一个修饰键 + 一个键名）
    }
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        bool found = false;
        for (const auto& m : kMods) {
            if (parts[i] == m.name) {
                // 重复修饰键（如 "win+win+q"）：拒绝（审查 L4）。
                if (b.mods & m.flag) {
                    return ShortcutBinding{};
                }
                b.mods |= m.flag;
                found = true;
                break;
            }
        }
        if (!found) {
            return ShortcutBinding{};  // 未知修饰键
        }
    }
    b.sym = keySymForName(parts.back());
    if (b.sym == 0 || b.mods == 0) {
        return ShortcutBinding{};  // 未知键名或缺少修饰键
    }
    return b;
}

std::vector<ShortcutBinding> loadShortcuts(const Config& config) {
    std::vector<ShortcutBinding> bindings(static_cast<size_t>(ShortcutAction::Count));
    for (int a = 0; a < static_cast<int>(ShortcutAction::Count); ++a) {
        const auto action = static_cast<ShortcutAction>(a);
        const std::string key = shortcutActionName(action);
        bindings[static_cast<size_t>(a)] = defaultBinding(action);
        if (key.empty()) {
            continue;
        }
        const std::string value = config.get("shortcuts", key);
        if (value.empty()) {
            continue;  // 未配置：默认
        }
        const ShortcutBinding parsed = parseShortcut(value);
        if (parsed.valid()) {
            bindings[static_cast<size_t>(a)] = parsed;
        }
        // 非法配置：保留默认（与 [theme] 颜色非法值忽略一致）。
    }
    // 冲突检测（审查 M2）：两个动作绑同一 (mods, sym)——按动作枚举序
    // 先者生效，后者告警（用户配置抢占默认键时避免静默失效）。
    for (int a = 0; a < static_cast<int>(ShortcutAction::Count); ++a) {
        const ShortcutBinding& ba = bindings[static_cast<size_t>(a)];
        if (!ba.valid()) {
            continue;
        }
        for (int c = a + 1; c < static_cast<int>(ShortcutAction::Count); ++c) {
            const ShortcutBinding& bc = bindings[static_cast<size_t>(c)];
            if (bc.valid() && bc.mods == ba.mods && bc.sym == ba.sym) {
                std::fprintf(stderr,
                    "shortcuts: binding conflict '%s' and '%s' share "
                    "(mods=0x%02x sym=0x%04x); first wins\n",
                    shortcutActionName(static_cast<ShortcutAction>(a)),
                    shortcutActionName(static_cast<ShortcutAction>(c)),
                    ba.mods, ba.sym);
            }
        }
    }
    return bindings;
}

}  // namespace w10de

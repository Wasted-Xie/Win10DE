#include "ipc/theme.h"

#include <cctype>
#include <cstdlib>

namespace w10de {

Theme darkTheme() {
    Theme t;
    t.taskbarBg = {0x2D, 0x2D, 0x2D};
    t.menuBg = {0x1E, 0x1E, 0x1E};
    t.menuSidebar = {0x17, 0x17, 0x17};
    t.titlebarBg = {0x2D, 0x2D, 0x2D};
    t.buttonBg = {0x3C, 0x3C, 0x3C};
    t.buttonHover = {0x5C, 0x5C, 0x5C};
    t.closeBg = {0xE8, 0x11, 0x23};
    t.closeHover = {0xF1, 0x70, 0x7A};
    t.textPrimary = {0xFF, 0xFF, 0xFF};
    t.textSecondary = {0xC0, 0xC0, 0xC0};
    t.hoverBg = {0x3C, 0x3C, 0x3C};
    t.pressedBg = {0x46, 0x46, 0x46};
    t.accent = {0x00, 0x78, 0xD7};
    // 强调底（#0078D7）上白字对比度 ≈4.5:1；深色模式沿用白。
    t.accentText = {0xFF, 0xFF, 0xFF};
    t.desktopBg = {0x00, 0x78, 0xD7};
    t.mode = "dark";
    return t;
}

Theme lightTheme() {
    // Win10 浅色：浅灰任务栏/标题栏 + 深色文字；强调色保持 Win10 蓝，
    // 关闭钮调整为浅色背景更沉稳的深红（#C42B1C，hover #E81123）。
    Theme t;
    t.taskbarBg = {0xF3, 0xF3, 0xF3};
    // 菜单 #F0F0F0 与磁贴常态 #E5E5E5 保持可辨（原 #E6E6E6 与磁贴几乎
    // 同色，浅色下无边界——审查 t2）。
    t.menuBg = {0xF0, 0xF0, 0xF0};
    t.menuSidebar = {0xE8, 0xE8, 0xE8};
    t.titlebarBg = {0xF3, 0xF3, 0xF3};
    t.buttonBg = {0xE5, 0xE5, 0xE5};
    t.buttonHover = {0xCC, 0xCC, 0xCC};
    t.closeBg = {0xC4, 0x2B, 0x1C};
    t.closeHover = {0xE8, 0x11, 0x23};
    t.textPrimary = {0x1A, 0x1A, 0x1A};
    t.textSecondary = {0x6E, 0x6E, 0x6E};
    t.hoverBg = {0xE5, 0xE5, 0xE5};
    t.pressedBg = {0xCC, 0xCC, 0xCC};
    t.accent = {0x00, 0x78, 0xD7};
    // 浅色下激活高亮文字仍固定白（黑字 on #0078D7 仅 3.87:1 <4.5:1）。
    t.accentText = {0xFF, 0xFF, 0xFF};
    t.desktopBg = {0xBC, 0xD8, 0xF0};
    t.mode = "light";
    return t;
}

bool parseColor(const std::string& text, ThemeColor* out) {
    if (out == nullptr) {
        return false;
    }
    std::string s = text;
    // 允许 "#RRGGBB" 与 "RRGGBB"。
    if (!s.empty() && s[0] == '#') {
        s.erase(0, 1);
    }
    if (s.size() != 6) {
        return false;
    }
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    auto hex2 = [&](size_t off) -> uint8_t {
        return static_cast<uint8_t>(std::strtoul(s.substr(off, 2).c_str(), nullptr, 16));
    };
    out->r = hex2(0);
    out->g = hex2(2);
    out->b = hex2(4);
    return true;
}

Theme loadTheme(const Config& config) {
    // mode 选基底；未配置/非法回退深色。
    const std::string mode = config.get("theme", "mode", "dark");
    Theme t = (mode == "light") ? lightTheme() : darkTheme();
    t.mode = mode;

    // 颜色键覆盖（自定义主题通道）：合法值才覆盖，非法忽略（保持预设）。
    struct KeyColor {
        const char* key;
        ThemeColor Theme::*field;
    };
    static const KeyColor kKeys[] = {
        {"taskbar_bg", &Theme::taskbarBg},
        {"menu_bg", &Theme::menuBg},
        {"menu_sidebar", &Theme::menuSidebar},
        {"titlebar_bg", &Theme::titlebarBg},
        {"button_bg", &Theme::buttonBg},
        {"button_hover", &Theme::buttonHover},
        {"close_bg", &Theme::closeBg},
        {"close_hover", &Theme::closeHover},
        {"text_primary", &Theme::textPrimary},
        {"text_secondary", &Theme::textSecondary},
        {"hover_bg", &Theme::hoverBg},
        {"pressed_bg", &Theme::pressedBg},
        {"accent", &Theme::accent},
        {"accent_text", &Theme::accentText},
        {"desktop_bg", &Theme::desktopBg},
    };
    for (const auto& k : kKeys) {
        const std::string v = config.get("theme", k.key);
        if (v.empty()) {
            continue;
        }
        ThemeColor c;
        if (parseColor(v, &c)) {
            t.*(k.field) = c;
        }
    }
    return t;
}

}  // namespace w10de

// Shell 主题实现：从共享 [theme] 配置段加载（见 ipc/theme.h）。
#include "theme/colors.h"

#include "ipc/config.h"

namespace w10de::theme {

namespace {

// 当前主题（全局单例；main 启动时加载）。
::w10de::Theme g_theme = ::w10de::darkTheme();

QColor toQColor(const ::w10de::ThemeColor& c) {
    return QColor(c.r, c.g, c.b);
}

}  // namespace

void applyTheme(const ::w10de::Theme& theme) {
    g_theme = theme;
}

void loadFromConfig(const std::string& configPath) {
    if (configPath.empty()) {
        g_theme = ::w10de::darkTheme();
        return;
    }
    g_theme = ::w10de::loadTheme(::w10de::Config::load(configPath));
}

QColor kTaskbarBackground() { return toQColor(g_theme.taskbarBg); }
QColor kStartMenuBackground() { return toQColor(g_theme.menuBg); }
QColor kMenuSidebar() { return toQColor(g_theme.menuSidebar); }
QColor kTextPrimary() { return toQColor(g_theme.textPrimary); }
QColor kTextSecondary() { return toQColor(g_theme.textSecondary); }
QColor kHoverBackground() { return toQColor(g_theme.hoverBg); }
QColor kPressedBackground() { return toQColor(g_theme.pressedBg); }
QColor kAccentBlue() { return toQColor(g_theme.accent); }
QColor kAccentText() { return toQColor(g_theme.accentText); }
QColor kTitlebarBackground() { return toQColor(g_theme.titlebarBg); }
QColor kButtonBackground() { return toQColor(g_theme.buttonBg); }
QColor kButtonHover() { return toQColor(g_theme.buttonHover); }
QColor kCloseBackground() { return toQColor(g_theme.closeBg); }
QColor kCloseHover() { return toQColor(g_theme.closeHover); }
QColor kDesktopBackground() { return toQColor(g_theme.desktopBg); }

}  // namespace w10de::theme

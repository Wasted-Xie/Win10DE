// Win10DE 主题颜色（Shell 客户端视觉）。
//
// 值来自配置文件 `~/.config/w10de/config.ini` 的 `[theme]` 段（与 compositor
// 共用同一主题定义，见 src/ipc/theme.h）：mode 预设（dark/light）+ 颜色键
// 覆盖（自定义主题通道）。main.cpp 启动时调用 theme::loadFromConfig() 加载。
// 各 UI 通过 `theme::kXxx()` 访问当前主题色。
#pragma once

#include <QColor>
#include <string>

#include "ipc/theme.h"

namespace w10de::theme {

// 从配置加载并设为当前主题（compositor 未配置时同深色预设）。
void loadFromConfig(const std::string& configPath);

// 从已解析的共享 Theme 结构应用（单元测试/默认路径用）。
void applyTheme(const ::w10de::Theme& theme);

// 当前主题色访问器（与旧常量同名同义，改为函数调用形式）。
QColor kTaskbarBackground();
QColor kStartMenuBackground();
QColor kMenuSidebar();
QColor kTextPrimary();
QColor kTextSecondary();
QColor kHoverBackground();
QColor kPressedBackground();
QColor kAccentBlue();
// 强调底上的文字色（任务栏激活高亮等；深浅模式均白）。
QColor kAccentText();
// 窗口装饰色（compositor 同款，供 shell 若需预览/一致用）。
QColor kTitlebarBackground();
QColor kButtonBackground();
QColor kButtonHover();
QColor kCloseBackground();
QColor kCloseHover();
QColor kDesktopBackground();

// 任务栏高度（逻辑像素，非主题项）。
inline constexpr int kTaskbarHeight = 48;

}  // namespace w10de::theme

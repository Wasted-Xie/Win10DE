// Win10 主题颜色常量（Shell 客户端视觉）。
#pragma once

#include <QColor>

namespace w10de::theme {

// 任务栏 / 标题栏深色背景。
inline const QColor kTaskbarBackground(0x2D, 0x2D, 0x2D);
// 开始菜单深色背景（比任务栏略深）。
inline const QColor kStartMenuBackground(0x1E, 0x1E, 0x1E);
// 主文字。
inline const QColor kTextPrimary(0xFF, 0xFF, 0xFF);
// 次级文字（时钟等）。
inline const QColor kTextSecondary(0xC0, 0xC0, 0xC0);
// 悬停高亮。
inline const QColor kHoverBackground(0x3C, 0x3C, 0x3C);
// 按下高亮。
inline const QColor kPressedBackground(0x46, 0x46, 0x46);
// Win10 蓝（强调色）。
inline const QColor kAccentBlue(0x00, 0x78, 0xD7);

// 任务栏高度（逻辑像素）。
inline constexpr int kTaskbarHeight = 48;

}  // namespace w10de::theme

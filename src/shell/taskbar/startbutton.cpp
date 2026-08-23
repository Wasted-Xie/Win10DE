#include "taskbar/startbutton.h"

#include "theme/colors.h"

namespace w10de {

StartButton::StartButton(QWidget* parent) : QPushButton(parent) {
    // 系统发行版 logo 图标（Arch：archlinux-keyring 提供的
    // /usr/share/pixmaps/archlinux-logo.svg）；缺失时回退文字。
    const QIcon distroLogo(QStringLiteral("/usr/share/pixmaps/archlinux-logo.svg"));
    if (!distroLogo.isNull()) {
        setIcon(distroLogo);
        // 图标 1:1 且与任务栏同高（kTaskbarHeight=48），大小固定不变。
        setIconSize(QSize(theme::kTaskbarHeight, theme::kTaskbarHeight));
    } else {
        setText(QStringLiteral("开始"));
    }
    // 正方形按钮（1:1，与任务栏同高），贴任务栏最左（布局无外边距）。
    setFixedSize(theme::kTaskbarHeight, theme::kTaskbarHeight);
    setCursor(Qt::PointingHandCursor);
    // Win10 扁平按钮：默认透明，hover 深灰，按下更深。
    // padding:0 消除 QPushButton 默认内容边距（图标贴屏幕最左，
    // 实测默认 3px 间隙）。
    setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  color: %1;"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0;"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: %3; }")
        .arg(theme::kTextPrimary.name(),
             theme::kHoverBackground.name(),
             theme::kPressedBackground.name()));
    connect(this, &QPushButton::clicked, this, &StartButton::startMenuRequested);
}

}  // namespace w10de

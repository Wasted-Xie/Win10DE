#include "taskbar/startbutton.h"

#include "theme/colors.h"

namespace w10de {

StartButton::StartButton(QWidget* parent) : QPushButton(parent) {
    // 系统发行版 logo 图标（Arch：archlinux-keyring 提供的
    // /usr/share/pixmaps/archlinux-logo.svg）；缺失时回退文字。
    const QIcon distroLogo(QStringLiteral("/usr/share/pixmaps/archlinux-logo.svg"));
    if (!distroLogo.isNull()) {
        setIcon(distroLogo);
        setIconSize(QSize(26, 26));
    } else {
        setText(QStringLiteral("开始"));
    }
    setFixedWidth(64);
    setCursor(Qt::PointingHandCursor);
    // Win10 扁平按钮：默认透明，hover 深灰，按下更深。
    setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  color: %1;"
        "  background: transparent;"
        "  border: none;"
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

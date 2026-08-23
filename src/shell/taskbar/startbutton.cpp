#include "taskbar/startbutton.h"

#include "theme/colors.h"

namespace w10de {

StartButton::StartButton(QWidget* parent) : QPushButton(parent) {
    setText(QStringLiteral("开始"));
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

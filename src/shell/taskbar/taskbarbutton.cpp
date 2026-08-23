#include "taskbar/taskbarbutton.h"

#include "ipc/foreigntoplevel.h"
#include "theme/colors.h"

namespace w10de {

TaskbarButton::TaskbarButton(ForeignToplevelHandle* handle, QWidget* parent)
    : QPushButton(parent), handle_(handle) {
    setCursor(Qt::PointingHandCursor);
    updateFromHandle();

    connect(handle_, &ForeignToplevelHandle::changed, this, &TaskbarButton::updateFromHandle);
    // 点击激活窗口（最小化窗口由合成器负责恢复显示）。
    connect(this, &QPushButton::clicked, handle_, &ForeignToplevelHandle::activate);
}

void TaskbarButton::updateFromHandle() {
    // 标题为空时用 app_id 兜底。
    QString label = handle_->title();
    if (label.isEmpty()) {
        label = handle_->appId();
    }
    if (label.isEmpty()) {
        label = QStringLiteral("(未命名)");
    }
    setText(label);

    // 激活窗口高亮，最小化窗口置灰。
    const QString bg = handle_->activated() ? theme::kAccentBlue.name()
                                            : theme::kHoverBackground.name();
    setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  color: %1;"
        "  background: %2;"
        "  border: none;"
        "  padding: 4px 12px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover { background: %3; }")
        .arg(theme::kTextPrimary.name(), bg, theme::kPressedBackground.name()));
}

}  // namespace w10de

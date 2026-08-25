#include "shell/notification/notificationcenter.h"

#include "shell/ipc/notificationservice.h"
#include "theme/colors.h"

#include <QDateTime>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

namespace w10de {

NotificationCenter::NotificationCenter(QWidget* parent) : QWidget(parent) {
    setFixedSize(380, 480);
    setStyleSheet(QStringLiteral(
        "NotificationCenter { background: %1; color: %2;"
        "  border: 1px solid %3; }")
        .arg(theme::kStartMenuBackground().name(),
             theme::kTextPrimary().name(),
             theme::kHoverBackground().name()));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(6);
    title_ = new QLabel(QStringLiteral("通知"), this);
    title_->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 15px; font-weight: bold; }")
        .arg(theme::kTextPrimary().name()));
    lay->addWidget(title_);
    list_ = new QListWidget(this);
    list_->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; color: %1;"
        "  font-size: 12px; }"
        "QListWidget::item { border-bottom: 1px solid %2; padding: 6px; }")
        .arg(theme::kTextPrimary().name(),
             theme::kHoverBackground().name()));
    lay->addWidget(list_, 1);
}

void NotificationCenter::refresh(const QList<Notification>& history) {    list_->clear();
    if (history.isEmpty()) {
        auto* empty = new QListWidgetItem(QStringLiteral("没有通知"));
        empty->setFlags(Qt::NoItemFlags);
        list_->addItem(empty);
    } else {
        // 倒序（最新在上，Win10 语义）。
        for (auto it = history.crbegin(); it != history.crend(); ++it) {
            const Notification& n = *it;
            QString text = QStringLiteral("%1  %2")
                .arg(n.appName.isEmpty() ? QStringLiteral("通知") : n.appName,
                     n.time.toString(QStringLiteral("HH:mm")));
            if (!n.summary.isEmpty()) {
                text += QStringLiteral("\n%1").arg(n.summary);
            }
            if (!n.body.isEmpty()) {
                text += QStringLiteral("\n%1").arg(n.body);
            }
            list_->addItem(text);
        }
    }
}

// 直接绘制背景（layer-shell 窗口无系统背景；QSS 在部分环境不生效）。
void NotificationCenter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme::kStartMenuBackground());
    p.setPen(theme::kHoverBackground());
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

}  // namespace w10de

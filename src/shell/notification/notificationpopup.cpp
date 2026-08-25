#include "shell/notification/notificationpopup.h"

#include "shell/ipc/notificationservice.h"
#include "theme/colors.h"

#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace w10de {

NotificationPopup::NotificationPopup(QWidget* parent) : QWidget(parent) {
    // Win10 通知卡片：固定宽 360，高度固定 100（内容超长时截断显示）；
    // 固定尺寸保证 layer-shell 表面几何稳定。
    setFixedSize(360, 100);
    setStyleSheet(QStringLiteral(
        "NotificationPopup { background: %1; color: %2;"
        "  border: 1px solid %3; border-radius: 2px; }")
        .arg(theme::kStartMenuBackground().name(),
             theme::kTextPrimary().name(),
             theme::kHoverBackground().name()));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 8, 12, 10);
    lay->setSpacing(2);
    appLabel_ = new QLabel(this);
    appLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; }")
        .arg(theme::kTextSecondary().name()));
    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: bold; }")
        .arg(theme::kTextPrimary().name()));
    bodyLabel_ = new QLabel(this);
    bodyLabel_->setWordWrap(true);
    bodyLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; }")
        .arg(theme::kTextPrimary().name()));
    lay->addWidget(appLabel_);
    lay->addWidget(summaryLabel_);
    lay->addWidget(bodyLabel_);
    setAttribute(Qt::WA_DeleteOnClose, false);
}

void NotificationPopup::showNotification(const Notification& n) {
    appLabel_->setText(n.appName.isEmpty() ? QStringLiteral("通知") : n.appName);
    summaryLabel_->setText(n.summary);
    bodyLabel_->setText(n.body);
    if (n.body.isEmpty()) {
        bodyLabel_->hide();
    } else {
        bodyLabel_->show();
    }
    // 尺寸自适应内容（固定尺寸下 adjustSize 不改变几何，保留以应对
    // 未来改为动态高度）。
    adjustSize();
    qDebug() << "notification popup size:" << size() << "pos:" << pos();
    show();
    raise();
}

void NotificationPopup::mouseReleaseEvent(QMouseEvent* /*e*/) {
    emit clicked();
}

// 直接绘制背景（layer-shell 窗口无系统背景；QSS 在部分环境不生效）。
void NotificationPopup::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme::kStartMenuBackground());
    p.setPen(theme::kHoverBackground());
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

}  // namespace w10de

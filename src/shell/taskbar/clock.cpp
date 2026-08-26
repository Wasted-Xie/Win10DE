#include "taskbar/clock.h"
#include "taskbar/monthcalendar.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include "theme/colors.h"

namespace w10de {

Clock::Clock(QWidget* parent) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                      .arg(theme::kTextSecondary().name()));
    updateTime();

    timer_ = new QTimer(this);
    timer_->setInterval(1000);  // 每秒刷新
    connect(timer_, &QTimer::timeout, this, &Clock::updateTime);
    timer_->start();
}

void Clock::updateTime() {
    const QDateTime now = QDateTime::currentDateTime();
    // Win10 任务栏：上行时间，下行日期（M3 简化：单行 时间\n日期）。
    setText(now.toString(QStringLiteral("HH:mm\nyyyy/M/d")));
}

void Clock::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        // G6：弹出月历（时钟上方居中；审查 S1：显式设置 Qt::Popup——
        // 点击外部自动关闭 + WA_DeleteOnClose；不设则普通顶层窗口永不
        // 自动关闭，每次点击泄漏一个窗口）。
        auto* cal = new MonthCalendar();
        cal->setWindowFlags(Qt::Popup);
        cal->setDate(QDate::currentDate());
        const QPoint global = mapToGlobal(QPoint(0, 0));
        int x = global.x() + width() / 2 - cal->width() / 2;
        int y = global.y() - cal->height() - 4;
        // 审查 L8：屏幕边界钳制（时钟靠右下角时防溢出；多显示器取当前
        // 屏幕可用区）。
        if (QScreen* screen = QGuiApplication::screenAt(global)) {
            const QRect avail = screen->availableGeometry();
            x = qBound(avail.left(), x, avail.right() - cal->width() + 1);
            y = qMax(avail.top(), y);
        }
        cal->move(x, y);
        cal->show();
        return;
    }
    QLabel::mousePressEvent(e);
}

}  // namespace w10de

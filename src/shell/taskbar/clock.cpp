#include "taskbar/clock.h"

#include <QDateTime>
#include <QTimer>
#include <QVBoxLayout>

#include "theme/colors.h"

namespace w10de {

Clock::Clock(QWidget* parent) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                      .arg(theme::kTextSecondary.name()));
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

}  // namespace w10de

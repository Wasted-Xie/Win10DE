// w10clock 闹钟和时钟实现。
#include "systemapps/clock/clockwindow.h"

#include <QApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTime>
#include <QTimeEdit>
#include <QTimer>
#include <QTimeZone>
#include <QVBoxLayout>

#include "theme/colors.h"

namespace w10clock {

qint64 countdownRemainingMs(qint64 targetMs, qint64 nowMs) {
    return targetMs - nowMs;
}

bool alarmDue(int alarmHour, int alarmMinute, const QTime& now,
              const QString& lastFiredMinuteKey, QString* newKey) {
    // 审查 M1（E5）：键含日期（yyyyMMdd HH:mm 补零）——否则闹钟跨天
    // 后同分钟 key 不变、永不重响（连续运行次日失效）。
    if (newKey != nullptr) {
        *newKey = QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))
            + QLatin1Char(' ') + now.toString(QStringLiteral("HH:mm"));
    }
    if (now.hour() != alarmHour || now.minute() != alarmMinute) {
        return false;
    }
    // 同一分钟只触发一次。
    return lastFiredMinuteKey != *newKey;
}

ClockWindow::ClockWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("闹钟和时钟"));
    resize(520, 420);
    buildUi();

    // 每秒刷新（世界时钟/计时器/秒表/闹钟共用）。
    auto* timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this] {
        refreshWorldClocks();
        if (timerRunning_) {
            onTimerTick();
        }
        if (stopwatchStartedMs_ != 0) {
            onStopwatchTick();
        }
        onAlarmTick();
    });
    timer->start();
    refreshWorldClocks();
}

void ClockWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);
    tabs_ = new QTabWidget(central);
    lay->addWidget(tabs_);

    // ---- 世界时钟 ----
    {
        auto* page = new QWidget(tabs_);
        auto* pl = new QVBoxLayout(page);
        worldLabel_ = new QLabel(page);
        worldLabel_->setStyleSheet(QStringLiteral(
            "font-family: monospace; font-size: 14px;"));
        pl->addWidget(worldLabel_);
        pl->addStretch(1);
        tabs_->addTab(page, QStringLiteral("世界时钟"));
    }

    // ---- 计时器 ----
    {
        auto* page = new QWidget(tabs_);
        auto* pl = new QVBoxLayout(page);
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(QStringLiteral("目标时长："), page));
        timerTargetEdit_ = new QTimeEdit(page);
        timerTargetEdit_->setDisplayFormat(QStringLiteral("HH:mm:ss"));
        timerTargetEdit_->setTime(QTime(0, 5, 0));
        row->addWidget(timerTargetEdit_);
        row->addStretch(1);
        pl->addLayout(row);
        timerLabel_ = new QLabel(QStringLiteral("05:00"), page);
        timerLabel_->setStyleSheet(QStringLiteral(
            "font-size: 42px; font-weight: bold; font-family: monospace;"));
        timerLabel_->setAlignment(Qt::AlignCenter);
        pl->addWidget(timerLabel_, 1);
        auto* btnRow = new QHBoxLayout;
        auto* startBtn = new QPushButton(QStringLiteral("启动"), page);
        auto* pauseBtn = new QPushButton(QStringLiteral("暂停"), page);
        auto* resetBtn = new QPushButton(QStringLiteral("复位"), page);
        btnRow->addWidget(startBtn);
        btnRow->addWidget(pauseBtn);
        btnRow->addWidget(resetBtn);
        btnRow->addStretch(1);
        pl->addLayout(btnRow);
        tabs_->addTab(page, QStringLiteral("计时器"));
        connect(startBtn, &QPushButton::clicked, this, &ClockWindow::startTimer);
        connect(pauseBtn, &QPushButton::clicked, this, &ClockWindow::pauseTimer);
        connect(resetBtn, &QPushButton::clicked, this, &ClockWindow::resetTimer);
    }

    // ---- 秒表 ----
    {
        auto* page = new QWidget(tabs_);
        auto* pl = new QVBoxLayout(page);
        stopwatchLabel_ = new QLabel(QStringLiteral("00:00.0"), page);
        stopwatchLabel_->setStyleSheet(QStringLiteral(
            "font-size: 42px; font-weight: bold; font-family: monospace;"));
        stopwatchLabel_->setAlignment(Qt::AlignCenter);
        pl->addWidget(stopwatchLabel_, 1);
        lapsLabel_ = new QLabel(page);
        lapsLabel_->setWordWrap(true);
        pl->addWidget(lapsLabel_);
        auto* btnRow = new QHBoxLayout;
        auto* startBtn = new QPushButton(QStringLiteral("启动/暂停"), page);
        auto* lapBtn = new QPushButton(QStringLiteral("计次"), page);
        auto* resetBtn = new QPushButton(QStringLiteral("复位"), page);
        btnRow->addWidget(startBtn);
        btnRow->addWidget(lapBtn);
        btnRow->addWidget(resetBtn);
        btnRow->addStretch(1);
        pl->addLayout(btnRow);
        tabs_->addTab(page, QStringLiteral("秒表"));
        connect(startBtn, &QPushButton::clicked, this,
                &ClockWindow::startStopwatch);
        connect(lapBtn, &QPushButton::clicked, this,
                &ClockWindow::lapStopwatch);
        connect(resetBtn, &QPushButton::clicked, this,
                &ClockWindow::resetStopwatch);
    }

    // ---- 闹钟 ----
    {
        auto* page = new QWidget(tabs_);
        auto* pl = new QVBoxLayout(page);
        auto* row = new QHBoxLayout;
        alarmTimeEdit_ = new QTimeEdit(page);
        alarmTimeEdit_->setDisplayFormat(QStringLiteral("HH:mm"));
        alarmTimeEdit_->setTime(QTime(7, 0));
        row->addWidget(alarmTimeEdit_);
        auto* addBtn = new QPushButton(QStringLiteral("添加闹钟"), page);
        auto* delBtn = new QPushButton(QStringLiteral("删除"), page);
        row->addWidget(addBtn);
        row->addWidget(delBtn);
        row->addStretch(1);
        pl->addLayout(row);
        alarmList_ = new QListWidget(page);
        pl->addWidget(alarmList_, 1);
        tabs_->addTab(page, QStringLiteral("闹钟"));
        connect(addBtn, &QPushButton::clicked, this, [this] {
            const QString t = alarmTimeEdit_->time().toString(
                QStringLiteral("HH:mm"));
            alarmList_->addItem(t + QStringLiteral("  启用"));
        });
        connect(delBtn, &QPushButton::clicked, this, [this] {
            const int row = alarmList_->currentRow();
            if (row >= 0) {
                delete alarmList_->takeItem(row);
            }
        });
    }

    setCentralWidget(central);
}

void ClockWindow::refreshWorldClocks() {
    const QDateTime now = QDateTime::currentDateTime();
    const QList<QPair<QString, QString>> cities = {
        {QStringLiteral("本地"), QString::fromUtf8(QTimeZone::systemTimeZoneId())},
        {QStringLiteral("北京"), QStringLiteral("Asia/Shanghai")},
        {QStringLiteral("伦敦"), QStringLiteral("Europe/London")},
        {QStringLiteral("纽约"), QStringLiteral("America/New_York")},
        {QStringLiteral("东京"), QStringLiteral("Asia/Tokyo")},
        {QStringLiteral("悉尼"), QStringLiteral("Australia/Sydney")},
    };
    QStringList lines;
    for (const auto& [name, tzId] : cities) {
        QTimeZone tz(tzId.toUtf8());
        if (!tz.isValid()) {
            tz = QTimeZone::systemTimeZone();
        }
        lines << QStringLiteral("%1  %2  UTC%3")
                     .arg(name, -6)
                     .arg(now.toTimeZone(tz).toString(QStringLiteral("HH:mm:ss")),
                          -8)
                     .arg(tz.offsetFromUtc(now) / 3600);
    }
    worldLabel_->setText(lines.join(QLatin1Char('\n')));
}

void ClockWindow::startTimer() {
    // 审查 M2（E5）：暂停后恢复用剩余时间（不再从 QTimeEdit 全时长重建）。
    if (timerRemainingMs_ > 0) {
        timerTargetMs_ = QDateTime::currentMSecsSinceEpoch() + timerRemainingMs_;
        timerRemainingMs_ = 0;
    } else {
        const QTime t = timerTargetEdit_->time();
        const qint64 durationMs = static_cast<qint64>(t.hour()) * 3600000
            + static_cast<qint64>(t.minute()) * 60000
            + static_cast<qint64>(t.second()) * 1000;
        timerTargetMs_ = QDateTime::currentMSecsSinceEpoch() + durationMs;
    }
    timerRunning_ = true;
    onTimerTick();
}

void ClockWindow::pauseTimer() {
    if (!timerRunning_) {
        return;
    }
    timerRunning_ = false;
    // 保存剩余（到点即 0）。
    const qint64 remaining =
        countdownRemainingMs(timerTargetMs_, QDateTime::currentMSecsSinceEpoch());
    timerRemainingMs_ = remaining > 0 ? remaining : 0;
}

void ClockWindow::resetTimer() {
    timerRunning_ = false;
    timerRemainingMs_ = 0;
    const QTime t = timerTargetEdit_->time();
    timerLabel_->setText(t.toString(QStringLiteral("HH:mm:ss")));
}

void ClockWindow::onTimerTick() {
    const qint64 remaining =
        countdownRemainingMs(timerTargetMs_, QDateTime::currentMSecsSinceEpoch());
    if (remaining <= 0) {
        timerRunning_ = false;
        timerLabel_->setText(QStringLiteral("00:00:00"));
        QApplication::beep();
        return;
    }
    const qint64 totalSec = remaining / 1000;
    timerLabel_->setText(QStringLiteral("%1:%2:%3")
        .arg(totalSec / 3600, 2, 10, QLatin1Char('0'))
        .arg((totalSec % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSec % 60, 2, 10, QLatin1Char('0')));
}

void ClockWindow::startStopwatch() {
    if (stopwatchStartedMs_ == 0) {
        stopwatchStartedMs_ = QDateTime::currentMSecsSinceEpoch();
    } else {
        // 暂停：累计进 base。
        stopwatchBaseMs_ += QDateTime::currentMSecsSinceEpoch()
            - stopwatchStartedMs_;
        stopwatchStartedMs_ = 0;
    }
    onStopwatchTick();
}

void ClockWindow::lapStopwatch() {
    const qint64 total = stopwatchBaseMs_
        + (stopwatchStartedMs_ != 0
            ? QDateTime::currentMSecsSinceEpoch() - stopwatchStartedMs_ : 0);
    const qint64 ms = total % 1000;
    const qint64 s = total / 1000;
    laps_.prepend(QStringLiteral("计次 %1：%2:%3.%4")
        .arg(laps_.size() + 1)
        .arg(s / 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 100));
    lapsLabel_->setText(laps_.join(QLatin1Char('\n')));
}

void ClockWindow::resetStopwatch() {
    stopwatchBaseMs_ = 0;
    stopwatchStartedMs_ = 0;
    laps_.clear();
    lapsLabel_->clear();
    stopwatchLabel_->setText(QStringLiteral("00:00.0"));
}

void ClockWindow::onStopwatchTick() {
    const qint64 total = stopwatchBaseMs_
        + (stopwatchStartedMs_ != 0
            ? QDateTime::currentMSecsSinceEpoch() - stopwatchStartedMs_ : 0);
    const qint64 ms = total % 1000;
    const qint64 s = total / 1000;
    stopwatchLabel_->setText(QStringLiteral("%1:%2.%3")
        .arg(s / 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 100));
}

void ClockWindow::onAlarmTick() {
    const QTime now = QTime::currentTime();
    QString newKey;
    for (int i = 0; i < alarmList_->count(); ++i) {
        const QString text = alarmList_->item(i)->text();
        const QTime t = QTime::fromString(text.left(5), QStringLiteral("HH:mm"));
        if (!t.isValid()) {
            continue;
        }
        if (alarmDue(t.hour(), t.minute(), now, lastAlarmKey_, &newKey)) {
            lastAlarmKey_ = newKey;
            QApplication::beep();
            alarmList_->setCurrentRow(i);
            statusBar()->showMessage(
                QStringLiteral("闹钟：%1").arg(t.toString(QStringLiteral("HH:mm"))),
                5000);
        }
    }
}

}  // namespace w10clock

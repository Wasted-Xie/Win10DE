// w10clock —— 闹钟和时钟（可选拓展 E5：世界时钟/计时器/秒表/闹钟）。
//
// 纯 Qt：QTimeZone 城市时间 + QTime 倒计时/秒表 + 闹钟列表（HH:MM，
// 到点 QApplication::beep）。纯逻辑（倒计时/闹钟判定）可测。
#pragma once

#include <QMainWindow>

class QLabel;
class QListWidget;
class QTabWidget;
class QTimeEdit;
class QTimer;

namespace w10clock {

// 倒计时：给定目标时刻（自 epoch 毫秒）计算剩余；<=0 表示已到点。
qint64 countdownRemainingMs(qint64 targetMs, qint64 nowMs);
// 闹钟触发判定（本地时间 HH:MM 相等即触发；同一分钟只触发一次由调用方
// 记忆分钟键）。返回 true = 应触发。
bool alarmDue(int alarmHour, int alarmMinute, const QTime& now,
              const QString& lastFiredMinuteKey, QString* newKey);

class ClockWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ClockWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    // 世界时钟（每秒刷新）。
    void refreshWorldClocks();
    // 计时器。
    void startTimer();
    void pauseTimer();
    void resetTimer();
    void onTimerTick();
    // 秒表。
    void startStopwatch();
    void lapStopwatch();
    void resetStopwatch();
    void onStopwatchTick();
    // 闹钟（每秒检查）。
    void onAlarmTick();

    QTabWidget* tabs_ = nullptr;
    // 世界时钟
    QLabel* worldLabel_ = nullptr;
    // 计时器
    QLabel* timerLabel_ = nullptr;
    QTimeEdit* timerTargetEdit_ = nullptr;
    qint64 timerTargetMs_ = 0;
    qint64 timerRemainingMs_ = 0;  // 审查 M2（E5）：暂停时保存剩余
    bool timerRunning_ = false;
    // 秒表
    QLabel* stopwatchLabel_ = nullptr;
    QLabel* lapsLabel_ = nullptr;
    qint64 stopwatchBaseMs_ = 0;   // 启动时的累计
    qint64 stopwatchStartedMs_ = 0;  // 本次启动时刻（暂停时 0）
    QStringList laps_;
    // 闹钟
    QListWidget* alarmList_ = nullptr;
    QTimeEdit* alarmTimeEdit_ = nullptr;
    QString lastAlarmKey_;
};

}  // namespace w10clock

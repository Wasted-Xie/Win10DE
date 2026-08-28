// CalendarWindow —— 日历主窗口（可选拓展 E11：月视图 + 事件 + 提醒）。
//
// 布局：顶部月份导航（上月/今天/下月 + 年月标题）→ 左月历网格（7 列，
// 有事件的日期带圆点标记）→ 右选中日期事件列表 + [新建][编辑][删除]。
// 提醒：每分钟检查当天事件（准点 + 全天启动一次），libnotify 系统通知
// 优先，无通知服务时应用内状态提示 + 提示音降级（运行期去重）。

#pragma once

#include <QSet>
#include <QWidget>

#include "systemapps/calendar/eventstore.h"

class QButtonGroup;
class QLabel;
class QListWidget;
class QPushButton;
class QGridLayout;
class QTimer;

namespace w10de::calendar {

class CalendarWindow : public QWidget {
    Q_OBJECT
public:
    explicit CalendarWindow(QWidget* parent = nullptr);

    // 供验证：当前网格格子数。
    int cellCount() const;
    // 供验证：事件列表行数。
    int eventCount() const;
    // 供验证：是否已收到提醒（状态栏文案含"提醒"）。
    bool hasReminderShown() const;

private slots:
    void onPrevMonth();
    void onNextMonth();
    void onToday();
    void onCellClicked(const QDate& date);
    void onAdd();
    void onEdit();
    void onDelete();
    void checkReminders();

private:
    void rebuildGrid();
    void refreshEvents();
    void rebuildAll();
    void setStatus(const QString& text, bool ok);
    // 发送一条提醒（libnotify 优先，降级应用内提示 + beep）。
    void notifyEvent(const CalendarEvent& e, bool allDay);

    EventStore store_;
    QDate current_;       // 当前显示年月（day 忽略）
    QDate selected_;      // 选中日期
    QTimer* reminderTimer_ = nullptr;
    // 已提醒事件键（"date|time|id"，审查 S1：改期后新键可再提醒）。
    QSet<QString> notifiedIds_;
    // 当天全天事件已提醒的日期（审查 S2：跨午夜自动复位）。
    QString allDayNotifiedDate_;
    bool reminderShown_ = false;   // 供验证
    QLabel* titleLabel_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QButtonGroup* dayGroup_ = nullptr;  // 审查 M1：日期格选中互斥
    QList<QPushButton*> cellButtons_;
    QListWidget* eventList_ = nullptr;
    QLabel* dateLabel_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* editButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

}  // namespace w10de::calendar

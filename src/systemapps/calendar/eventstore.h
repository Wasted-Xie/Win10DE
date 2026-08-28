// EventStore —— 日历事件存储与月历逻辑（可选拓展 E11 日历完整版）。
//
// 事件存 ~/.config/w10de/calendar.ini（QSettings：events/<id>/*，id 自增；
// W10DE_CALENDAR_CONFIG 覆盖——测试隔离）。月历网格计算为静态可测函数
//（同 G6 shell 组件逻辑，独立实现避免跨组件耦合）。

#pragma once

#include <QDate>
#include <QList>
#include <QSet>
#include <QString>

namespace w10de::calendar {

struct CalendarEvent {
    int id = 0;          // 唯一（自增）
    QString date;        // "yyyy-MM-dd"
    QString time;        // "HH:mm"（空 = 全天）
    QString title;
    QString detail;
};

// ---- 月历网格（静态可测）----

// 月网格单元。
struct Cell {
    QDate date;
    bool inMonth = false;   // 是否当月（网格含前后月补位）
};

// 当月完整网格（周日起始，6 行 × 7 列 = 42 格；含前后月补位）。
QList<Cell> monthCells(int year, int month);

// 当月天数。
int daysInMonth(int year, int month);

// ---- 事件存储 ----

// 提醒判定（静态可测）：从 events（当天列表）筛出时间匹配且未提醒过的事件，
// 并写入 notified 集合去重。全天事件（time 空）不在此列（由调用方启动时
// 单独提醒一次）。
// 审查 S1：去重键为 "date|time|id"（QString）——事件改期（含跨天）后时间
// 变化 → 新键 → 会重新提醒；纯 id 键会使改期事件当天不再提醒。
QList<CalendarEvent> dueEvents(const QList<CalendarEvent>& events,
                               const QString& nowTime,
                               QSet<QString>* notified);

class EventStore {
public:
    // configPathOverride 非空时注入（selftest 隔离）。
    explicit EventStore(const QString& configPathOverride = QString());

    QString configPath() const { return configPath_; }

    // 当天事件（按时间排序，全天在前）。
    QList<CalendarEvent> eventsForDate(const QString& date) const;
    // 精确时间匹配（date + HH:mm 相等；全天事件 time 为空不在此列）。
    QList<CalendarEvent> eventsAtTime(const QString& date,
                                      const QString& time) const;
    // 当天全天事件（time 为空；启动时提醒用）。
    QList<CalendarEvent> allDayEvents(const QString& date) const;
    // 当月所有事件日期集合（"yyyy-MM-dd" 去重排序——月视图标记点）。
    QStringList monthDates(int year, int month) const;

    // CRUD；add 返回新事件（含分配的 id）。
    CalendarEvent add(const QString& date, const QString& time,
                      const QString& title, const QString& detail);
    bool update(const CalendarEvent& e);
    bool remove(int id);

    // 最近一次错误（空 = 成功）。
    QString lastError() const { return lastError_; }

private:
    QString configPath_;
    QString lastError_;
};

}  // namespace w10de::calendar

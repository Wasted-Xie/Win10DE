// CalendarWindow —— 日历主窗口（可选拓展 E11：月视图 + 事件）。
//
// 布局：顶部月份导航（上月/今天/下月 + 年月标题）→ 左月历网格（7 列，
// 有事件的日期带圆点标记）→ 右选中日期事件列表 + [新建][编辑][删除]。

#pragma once

#include <QWidget>

#include "systemapps/calendar/eventstore.h"

class QButtonGroup;
class QLabel;
class QListWidget;
class QPushButton;
class QGridLayout;

namespace w10de::calendar {

class CalendarWindow : public QWidget {
    Q_OBJECT
public:
    explicit CalendarWindow(QWidget* parent = nullptr);

    // 供验证：当前网格格子数。
    int cellCount() const;
    // 供验证：事件列表行数。
    int eventCount() const;

private slots:
    void onPrevMonth();
    void onNextMonth();
    void onToday();
    void onCellClicked(const QDate& date);
    void onAdd();
    void onEdit();
    void onDelete();

private:
    void rebuildGrid();
    void refreshEvents();
    void rebuildAll();
    void setStatus(const QString& text, bool ok);

    EventStore store_;
    QDate current_;       // 当前显示年月（day 忽略）
    QDate selected_;      // 选中日期
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

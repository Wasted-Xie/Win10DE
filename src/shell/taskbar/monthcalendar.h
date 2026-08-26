// 月历组件（G6：任务栏时钟点击弹出的日历，Win10 风格）。
//
// 布局：标题（◀ yyyy 年 M 月 ▶ + 今天）+ 星期表头（一 二 三 四 五 六 日）+
// 6×7 日期网格（周一始；当月白字/非当月灰字/今天蓝圆底白字/悬停浅灰底）+
// 底部今天日期行。翻月/今天/日期点击均支持。
#pragma once

#include <QDate>
#include <QFrame>
#include <QList>

class QMouseEvent;

namespace w10de {

// ---- 纯逻辑（selftest 用，无 UI 依赖）----
// 给定年月生成 6×7=42 天网格（周一起始，含前后月补足）。viewMonth 的 day 忽略。
QList<QDate> calendarCells(int year, int month);
// 该月天数（2 月按闰年）。
int daysInMonth(int year, int month);

class MonthCalendar : public QFrame {
    Q_OBJECT
public:
    explicit MonthCalendar(QWidget* parent = nullptr);
    // 显示到指定日期所在月；today_ 与 selected_ 初始为该日期
    //（today_ 随真实今天重设；selected_ 是用户点击的选中日）。
    void setDate(const QDate& date);

    QSize sizeHint() const override { return QSize(259, 300); }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;

private:
    enum class HitArea { TitlePrev, TitleNext, TodayRow, Grid, None };
    HitArea hitArea(const QPoint& pos, int* gridIndex) const;
    QRect gridRect() const;
    QRect gridCellRect(int index) const;

    QDate viewMonth_{2000, 1, 1};  // 显示的年月（day=1）
    QDate today_{2000, 1, 1};      // 今天（实心蓝圆高亮）
    QDate selected_{2000, 1, 1};   // G6 审查 M2：用户点击的选中日（浅色圆）
    int hoverIndex_ = -1;
};

}  // namespace w10de

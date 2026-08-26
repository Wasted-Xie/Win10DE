// 月历组件实现（G6）。
#include "taskbar/monthcalendar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace w10de {

int daysInMonth(int year, int month) {
    // 审查 L7：非法月份校验（防静默返回错误天数）。
    if (month < 1 || month > 12) {
        return 0;
    }
    static const int kDays[] = {31, 28, 31, 30, 31, 30,
                                31, 31, 30, 31, 30, 31};
    int d = kDays[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0)
                       || year % 400 == 0)) {
        d = 29;  // 闰年
    }
    return d;
}

QList<QDate> calendarCells(int year, int month) {
    QList<QDate> cells;
    // 审查 L7：非法输入返回空（调用方受控，防御性）。
    if (month < 1 || month > 12 || year < 1 || year > 9999) {
        return cells;
    }
    const QDate first(year, month, 1);
    // 周一起始偏移（Qt dayOfWeek: Mon=1..Sun=7 → offset = weekday-1）。
    const int offset = first.dayOfWeek() - 1;
    const QDate start = first.addDays(-offset);
    for (int i = 0; i < 42; ++i) {
        cells.append(start.addDays(i));
    }
    return cells;
}

namespace {

constexpr int kCellW = 37;
constexpr int kCellH = 33;   // 审查 L2：6 行 198px + 表头 22 = 220，今天行上移减少空白
constexpr int kHeaderY = 36;   // 星期表头起始
constexpr int kHeaderH = 22;
constexpr int kGridTop = kHeaderY + kHeaderH;
constexpr int kGridRows = 6;
constexpr int kTodayRowH = 34;
const QColor kBg(0x26, 0x2B, 0x33);          // 面板背景
const QColor kText(0xE8, 0xE8, 0xE8);        // 当月文字
const QColor kDim(0x8A, 0x90, 0x99);         // 非当月/表头/标题
const QColor kAccent(0x00, 0x78, 0xD7);      // 今天（实心）
const QColor kAccentText(0xFF, 0xFF, 0xFF);
const QColor kSelected(0x10, 0x60, 0xA8);    // G6 审查 M2：选中日（浅蓝实心）
const QColor kHover(0x3A, 0x42, 0x4E);       // 悬停
const QColor kBorder(0x1A, 0x1E, 0x24);

}  // namespace

MonthCalendar::MonthCalendar(QWidget* parent) : QFrame(parent) {
    // 注意：Qt::Popup 标志由 Clock 弹出路径设置（--calendar-render 独立
    // 渲染用普通窗口）——审查 S1 修复：弹出路径必须显式设 Popup，否则
    // 普通顶层窗口永不自动关闭（每次点击泄漏一个窗口）。
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(259, 300);  // 审查 L1：7*kCellW=259 与面板宽对齐
}

void MonthCalendar::setDate(const QDate& date) {
    if (date.isValid()) {
        today_ = date;
        selected_ = date;
        viewMonth_ = QDate(date.year(), date.month(), 1);
    }
    update();
}

QRect MonthCalendar::gridRect() const {
    return QRect(0, kGridTop, width(), kGridRows * kCellH);
}

QRect MonthCalendar::gridCellRect(int index) const {
    const int row = index / 7;
    const int col = index % 7;
    return QRect(col * kCellW, kGridTop + row * kCellH, kCellW, kCellH);
}

MonthCalendar::HitArea MonthCalendar::hitArea(const QPoint& pos,
                                              int* gridIndex) const {
    // 审查 L3：标题按钮热区与绘制区统一（按钮绘制高 26、标题文字到 28）。
    if (pos.y() < kHeaderY) {
        if (pos.y() < 28 && pos.x() >= 8 && pos.x() <= 32) {
            return HitArea::TitlePrev;
        }
        if (pos.y() < 28 && pos.x() >= width() - 32 && pos.x() <= width() - 8) {
            return HitArea::TitleNext;
        }
        return HitArea::None;
    }
    if (pos.y() >= height() - kTodayRowH) {
        return HitArea::TodayRow;
    }
    if (gridRect().contains(pos)) {
        const int col = pos.x() / kCellW;
        const int row = (pos.y() - kGridTop) / kCellH;
        if (col >= 0 && col < 7 && row >= 0 && row < kGridRows) {
            *gridIndex = row * 7 + col;
            return HitArea::Grid;
        }
    }
    return HitArea::None;
}

void MonthCalendar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), kBg);
    p.setPen(QPen(kBorder, 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // 标题绘制区 y<28 与热区一致（审查 L3）。
    p.setPen(kDim);
    p.drawText(QRect(8, 0, 24, 26), Qt::AlignCenter, QStringLiteral("◀"));
    p.drawText(QRect(width() - 32, 0, 24, 26), Qt::AlignCenter,
               QStringLiteral("▶"));
    p.setPen(kText);
    QFont titleFont = p.font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    p.setFont(titleFont);
    p.drawText(QRect(32, 0, width() - 64, 28), Qt::AlignCenter,
               QStringLiteral("%1 年 %2 月")
                   .arg(viewMonth_.year()).arg(viewMonth_.month()));
    p.setFont(QFont());

    // 星期表头（周一起始）。
    static const QStringList kDays = {QStringLiteral("一"), QStringLiteral("二"),
                                      QStringLiteral("三"), QStringLiteral("四"),
                                      QStringLiteral("五"), QStringLiteral("六"),
                                      QStringLiteral("日")};
    p.setPen(kDim);
    for (int col = 0; col < 7; ++col) {
        p.drawText(QRect(col * kCellW, kHeaderY, kCellW, kHeaderH),
                   Qt::AlignCenter, kDays.at(col));
    }
    // 表头下分隔线。
    p.setPen(QPen(kBorder, 1));
    p.drawLine(0, kHeaderY + kHeaderH, width(), kHeaderY + kHeaderH);

    // 日期网格。
    const QList<QDate> cells = calendarCells(viewMonth_.year(),
                                             viewMonth_.month());
    for (int i = 0; i < cells.size(); ++i) {
        const QDate d = cells.at(i);
        const QRect r = gridCellRect(i);
        const bool inMonth = d.month() == viewMonth_.month();
        // 审查 L4：补足格中的"今天"不画蓝圆（Win10 语义——非当月一律灰字）。
        const bool isToday = inMonth && (d == today_);
        const bool isSelected = inMonth && (d == selected_);
        const QColor circleColor = isToday ? kAccent : kSelected;
        // 绘制优先级：今天（实心蓝）> 选中（浅蓝）> 悬停（浅灰）> 文字。
        if (isToday || isSelected) {
            p.setPen(Qt::NoPen);
            p.setBrush(circleColor);
            // 审查 L6：正圆（min 尺寸正方形居中，22×22）。
            const int d2 = 22;
            const QRect circle(r.x() + (r.width() - d2) / 2,
                               r.y() + (r.height() - d2) / 2, d2, d2);
            p.drawEllipse(circle);
            p.setPen(kAccentText);
        } else {
            p.setPen(inMonth ? kText : kDim);
            if (i == hoverIndex_) {
                p.fillRect(r, kHover);
            }
        }
        p.drawText(r, Qt::AlignCenter, QString::number(d.day()));
    }
    // 底部今天行。
    p.setPen(kBorder);
    p.drawLine(0, height() - kTodayRowH, width(), height() - kTodayRowH);
    p.setPen(kDim);
    p.drawText(QRect(10, height() - kTodayRowH, width() - 20, kTodayRowH),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("今天 %1").arg(
                   today_.toString(QStringLiteral("yyyy/M/d ddd"))));
}

void MonthCalendar::mousePressEvent(QMouseEvent* e) {
    int gridIndex = -1;
    const HitArea area = hitArea(e->pos(), &gridIndex);
    if (area == HitArea::TitlePrev) {
        viewMonth_ = viewMonth_.addMonths(-1);
        hoverIndex_ = -1;  // 审查 L5：翻月后悬停索引失效
        update();
    } else if (area == HitArea::TitleNext) {
        viewMonth_ = viewMonth_.addMonths(1);
        hoverIndex_ = -1;  // 审查 L5
        update();
    } else if (area == HitArea::TodayRow) {
        setDate(QDate::currentDate());
    } else if (area == HitArea::Grid && gridIndex >= 0) {
        const QList<QDate> cells = calendarCells(viewMonth_.year(),
                                                 viewMonth_.month());
        if (gridIndex < cells.size()) {
            const QDate clicked = cells.at(gridIndex);
            // 审查 M2：点击任意日期 → 选中并高亮（浅蓝圆；与今天实心蓝圆
            // 分离）。点击非当月日期同时切月。
            selected_ = clicked;
            if (clicked.month() != viewMonth_.month()) {
                viewMonth_ = QDate(clicked.year(), clicked.month(), 1);
                hoverIndex_ = -1;
            }
            update();
        }
    }
    QFrame::mousePressEvent(e);
}

void MonthCalendar::mouseMoveEvent(QMouseEvent* e) {
    int gridIndex = -1;
    const HitArea area = hitArea(e->pos(), &gridIndex);
    const int newHover = (area == HitArea::Grid) ? gridIndex : -1;
    if (newHover != hoverIndex_) {
        hoverIndex_ = newHover;
        update();
    }
    QFrame::mouseMoveEvent(e);
}

void MonthCalendar::leaveEvent(QEvent* e) {
    if (hoverIndex_ >= 0) {
        hoverIndex_ = -1;
        update();
    }
    QFrame::leaveEvent(e);
}

}  // namespace w10de

// CalendarWindow 实现（可选拓展 E11：月视图 + 事件 CRUD）。
//
// 月历网格复用静态函数 monthCells（42 格）；日期按钮点击选中 → 右侧事件
// 列表；事件对话框（标题/时间/说明）由新建/编辑触发。

#include "systemapps/calendar/calendarwindow.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace w10de::calendar {

namespace {
const char* kNavBtn =
    "QPushButton{background:#3D3D3D;color:#E0E0E0;border:none;"
    "border-radius:4px;padding:6px 14px;}"
    "QPushButton:hover{background:#4A4A4A;}";
const char* kDayBtn =
    "QPushButton{background:#2D2D2D;color:#C8CDD3;border:none;"
    "border-radius:4px;font-size:13px;}"
    "QPushButton:hover{background:#3D3D3D;}"
    "QPushButton:checked{background:#3D6FB4;color:#FFF;}";
const char* kDayOther =
    "QPushButton{background:#2D2D2D;color:#555;border:none;"
    "border-radius:4px;font-size:13px;}"
    "QPushButton:hover{background:#333;}"
    "QPushButton:checked{background:#3D6FB4;color:#FFF;}";
const char* kDayToday =
    "QPushButton{background:#2D2D2D;color:#3DDC84;border:1px solid #3DDC84;"
    "border-radius:4px;font-size:13px;font-weight:bold;}"
    "QPushButton:hover{background:#333;}"
    "QPushButton:checked{background:#3D6FB4;color:#FFF;"
    "border-color:#3DDC84;}";  // 今天选中：蓝底 + 绿描边（描边不被覆盖）
}  // namespace

CalendarWindow::CalendarWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("日历"));
    setFixedSize(860, 560);
    current_ = QDate::currentDate();
    selected_ = current_;
    // 审查 M1：日期格选中互斥（QButtonGroup exclusive）。
    dayGroup_ = new QButtonGroup(this);
    dayGroup_->setExclusive(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(8);

    // 顶部导航。
    auto* nav = new QHBoxLayout;
    nav->setSpacing(8);
    auto* prevBtn = new QPushButton(QStringLiteral("上月"), this);
    auto* nextBtn = new QPushButton(QStringLiteral("下月"), this);
    auto* todayBtn = new QPushButton(QStringLiteral("今天"), this);
    for (QPushButton* b : {prevBtn, nextBtn, todayBtn}) {
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(kNavBtn);
    }
    connect(prevBtn, &QPushButton::clicked, this, &CalendarWindow::onPrevMonth);
    connect(nextBtn, &QPushButton::clicked, this, &CalendarWindow::onNextMonth);
    connect(todayBtn, &QPushButton::clicked, this, &CalendarWindow::onToday);
    titleLabel_ = new QLabel(this);
    titleLabel_->setStyleSheet(QStringLiteral(
        "color:#FFFFFF; font-size:18px; font-weight:bold;"));
    nav->addWidget(prevBtn);
    nav->addWidget(nextBtn);
    nav->addSpacing(12);
    nav->addWidget(titleLabel_, 1);
    nav->addWidget(todayBtn);
    root->addLayout(nav);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    // 左侧月历。
    auto* calPanel = new QWidget(this);
    auto* calLayout = new QVBoxLayout(calPanel);
    calLayout->setContentsMargins(0, 0, 0, 0);
    calLayout->setSpacing(4);
    // 星期表头（周日起始）。
    const QStringList weekdays = {
        QStringLiteral("日"), QStringLiteral("一"), QStringLiteral("二"),
        QStringLiteral("三"), QStringLiteral("四"), QStringLiteral("五"),
        QStringLiteral("六")};
    auto* headRow = new QHBoxLayout;
    for (const QString& wd : weekdays) {
        auto* l = new QLabel(wd, calPanel);
        l->setAlignment(Qt::AlignCenter);
        l->setStyleSheet(QStringLiteral("color:#9AA0A6; font-size:12px;"));
        headRow->addWidget(l, 1);
    }
    calLayout->addLayout(headRow);
    grid_ = new QGridLayout;
    grid_->setSpacing(3);
    calLayout->addLayout(grid_);
    body->addWidget(calPanel, 1);

    // 右侧事件面板。
    auto* evPanel = new QWidget(this);
    auto* evLayout = new QVBoxLayout(evPanel);
    evLayout->setContentsMargins(0, 0, 0, 0);
    evLayout->setSpacing(6);
    dateLabel_ = new QLabel(this);
    dateLabel_->setStyleSheet(QStringLiteral(
        "color:#FFFFFF; font-size:15px; font-weight:bold;"));
    evLayout->addWidget(dateLabel_);
    eventList_ = new QListWidget(this);
    eventList_->setStyleSheet(QStringLiteral(
        "QListWidget{background:#262626;color:#E0E0E0;border:1px solid #3A3A3A;"
        "border-radius:6px;font-size:13px;}"
        "QListWidget::item{padding:6px;}"
        "QListWidget::item:selected{background:#3D6FB4;}"));
    evLayout->addWidget(eventList_, 1);
    auto* evBtns = new QHBoxLayout;
    addButton_ = new QPushButton(QStringLiteral("新建"), this);
    editButton_ = new QPushButton(QStringLiteral("编辑"), this);
    deleteButton_ = new QPushButton(QStringLiteral("删除"), this);
    for (QPushButton* b : {addButton_, editButton_, deleteButton_}) {
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(kNavBtn);
    }
    connect(addButton_, &QPushButton::clicked, this, &CalendarWindow::onAdd);
    connect(editButton_, &QPushButton::clicked, this, &CalendarWindow::onEdit);
    connect(deleteButton_, &QPushButton::clicked,
            this, &CalendarWindow::onDelete);
    evBtns->addWidget(addButton_);
    evBtns->addWidget(editButton_);
    evBtns->addWidget(deleteButton_);
    evBtns->addStretch();
    evLayout->addLayout(evBtns);
    body->addWidget(evPanel, 1);
    root->addLayout(body, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#9AA0A6; font-size:12px;"));
    root->addWidget(statusLabel_);

    setStyleSheet(QStringLiteral("QWidget{background:#2D2D2D;}"));
    rebuildAll();
}

int CalendarWindow::cellCount() const {
    return cellButtons_.size();
}

int CalendarWindow::eventCount() const {
    return eventList_->count();
}

void CalendarWindow::onPrevMonth() {
    current_ = current_.addMonths(-1);
    // 审查 M3：翻月后选中日期归一化（同一天，超出月末取月末——
    // 避免选中格不在网格内且右侧仍显示旧日期事件）。
    const int maxDay = daysInMonth(current_.year(), current_.month());
    selected_ = QDate(current_.year(), current_.month(),
                      qMin(selected_.day(), maxDay));
    rebuildGrid();
}

void CalendarWindow::onNextMonth() {
    current_ = current_.addMonths(1);
    const int maxDay = daysInMonth(current_.year(), current_.month());
    selected_ = QDate(current_.year(), current_.month(),
                      qMin(selected_.day(), maxDay));
    rebuildGrid();
}

void CalendarWindow::onToday() {
    current_ = QDate::currentDate();
    selected_ = current_;
    rebuildAll();
}

void CalendarWindow::onCellClicked(const QDate& date) {
    selected_ = date;
    // 点击非当月日期：跳到该月。
    if (date.month() != current_.month() || date.year() != current_.year()) {
        current_ = QDate(date.year(), date.month(), 1);
        rebuildGrid();
        return;
    }
    refreshEvents();
}

void CalendarWindow::rebuildGrid() {
    // 清除旧按钮。
    for (QPushButton* b : cellButtons_) {
        grid_->removeWidget(b);
        b->deleteLater();
    }
    cellButtons_.clear();
    titleLabel_->setText(current_.toString(QStringLiteral("yyyy 年 M 月")));
    const QStringList eventDates = store_.monthDates(
        current_.year(), current_.month());
    // Qt6 无 QStringList::toSet——用迭代器构造。
    const QSet<QString> eventSet(eventDates.begin(), eventDates.end());
    const QDate today = QDate::currentDate();
    const auto cells = monthCells(current_.year(), current_.month());
    for (int i = 0; i < cells.size(); ++i) {
        const Cell& c = cells[i];
        auto* btn = new QPushButton(this);
        btn->setText(QString::number(c.date.day()));
        btn->setFocusPolicy(Qt::NoFocus);
        // 今天描边；有事件加圆点标记（文本后跟 "•"）。
        if (c.date == today) {
            btn->setStyleSheet(kDayToday);
        } else if (c.inMonth) {
            btn->setStyleSheet(kDayBtn);
        } else {
            btn->setStyleSheet(kDayOther);
        }
        const bool hasEvent = eventSet.contains(
            c.date.toString(QStringLiteral("yyyy-MM-dd")));
        if (hasEvent) {
            btn->setText(QStringLiteral("%1 •").arg(c.date.day()));
        }
        btn->setCheckable(true);
        dayGroup_->addButton(btn);  // 审查 M1：加入互斥组
        if (c.date == selected_) {
            btn->setChecked(true);
        }
        const QDate d = c.date;
        connect(btn, &QPushButton::clicked, this, [this, d] {
            onCellClicked(d);
        });
        grid_->addWidget(btn, i / 7, i % 7);
        cellButtons_.append(btn);
    }
    refreshEvents();
}

void CalendarWindow::refreshEvents() {
    dateLabel_->setText(selected_.toString(QStringLiteral("yyyy年M月d日 dddd")));
    eventList_->clear();
    const QList<CalendarEvent> events = store_.eventsForDate(
        selected_.toString(QStringLiteral("yyyy-MM-dd")));
    for (const CalendarEvent& e : events) {
        const QString timeText = e.time.isEmpty()
            ? QStringLiteral("全天") : e.time;
        auto* item = new QListWidgetItem(
            QStringLiteral("%1    %2").arg(timeText, e.title),
            eventList_);
        item->setData(Qt::UserRole, e.id);
        if (!e.detail.isEmpty()) {
            item->setToolTip(e.detail);
        }
    }
    update();
}

void CalendarWindow::rebuildAll() {
    rebuildGrid();
}

void CalendarWindow::onAdd() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("新建事件"));
    auto* form = new QFormLayout(&dlg);
    auto* titleEdit = new QLineEdit(&dlg);
    auto* timeEdit = new QTimeEdit(&dlg);
    timeEdit->setTime(QTime(9, 0));
    auto* detailEdit = new QLineEdit(&dlg);
    auto* allDayCheck = new QCheckBox(QStringLiteral("全天"), &dlg);
    form->addRow(QStringLiteral("标题："), titleEdit);
    form->addRow(QStringLiteral("时间："), timeEdit);
    form->addRow(QString(), allDayCheck);
    form->addRow(QStringLiteral("说明："), detailEdit);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString title = titleEdit->text().trimmed();
    if (title.isEmpty()) {
        setStatus(QStringLiteral("事件标题不能为空"), false);
        return;
    }
    const QString time = allDayCheck->isChecked()
        ? QString() : timeEdit->time().toString(QStringLiteral("HH:mm"));
    const CalendarEvent e = store_.add(
        selected_.toString(QStringLiteral("yyyy-MM-dd")), time, title,
        detailEdit->text().trimmed());
    if (e.id == 0) {
        setStatus(store_.lastError(), false);
        return;
    }
    rebuildGrid();
    setStatus(QStringLiteral("已添加事件"), true);
}

void CalendarWindow::onEdit() {
    const auto* item = eventList_->currentItem();
    if (item == nullptr) return;
    const int id = item->data(Qt::UserRole).toInt();
    const QList<CalendarEvent> events = store_.eventsForDate(
        selected_.toString(QStringLiteral("yyyy-MM-dd")));
    CalendarEvent target;
    for (const auto& e : events) {
        if (e.id == id) {
            target = e;
            break;
        }
    }
    if (target.id == 0) return;
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("编辑事件"));
    auto* form = new QFormLayout(&dlg);
    auto* titleEdit = new QLineEdit(target.title, &dlg);
    auto* timeEdit = new QTimeEdit(&dlg);
    if (!target.time.isEmpty()) {
        timeEdit->setTime(QTime::fromString(target.time,
                                            QStringLiteral("HH:mm")));
    } else {
        timeEdit->setTime(QTime(9, 0));
    }
    auto* allDayCheck = new QCheckBox(QStringLiteral("全天"), &dlg);
    allDayCheck->setChecked(target.time.isEmpty());
    auto* detailEdit = new QLineEdit(target.detail, &dlg);
    form->addRow(QStringLiteral("标题："), titleEdit);
    form->addRow(QStringLiteral("时间："), timeEdit);
    form->addRow(QString(), allDayCheck);
    form->addRow(QStringLiteral("说明："), detailEdit);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    target.title = titleEdit->text().trimmed();
    target.time = allDayCheck->isChecked()
        ? QString() : timeEdit->time().toString(QStringLiteral("HH:mm"));
    target.detail = detailEdit->text().trimmed();
    if (target.title.isEmpty()) {
        setStatus(QStringLiteral("事件标题不能为空"), false);
        return;
    }
    if (!store_.update(target)) {
        setStatus(store_.lastError(), false);
        return;
    }
    rebuildGrid();
    setStatus(QStringLiteral("已更新事件"), true);
}

void CalendarWindow::onDelete() {
    const auto* item = eventList_->currentItem();
    if (item == nullptr) return;
    const int id = item->data(Qt::UserRole).toInt();
    const auto ret = QMessageBox::question(
        this, QStringLiteral("删除事件"),
        QStringLiteral("确定删除该事件吗？"));
    if (ret != QMessageBox::Yes) return;
    if (!store_.remove(id)) {
        setStatus(store_.lastError(), false);
        return;
    }
    rebuildGrid();
    setStatus(QStringLiteral("已删除事件"), true);
}

void CalendarWindow::setStatus(const QString& text, bool ok) {
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(ok
        ? QStringLiteral("color:#9AA0A6; font-size:12px;")
        : QStringLiteral("color:#E57373; font-size:12px;"));
}

}  // namespace w10de::calendar

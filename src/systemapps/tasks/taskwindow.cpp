// 任务计划程序主窗口实现（G5）。
#include "systemapps/tasks/taskwindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeEdit>
#include <QVBoxLayout>

#include "theme/colors.h"

namespace w10task {

namespace {

// 触发器模板（新建对话框下拉）。
enum class TriggerTemplate {
    EveryMinute,   // 每分钟
    Hourly,        // 每小时 HH 分
    Daily,         // 每天 HH:MM
    Weekly,        // 每周 星期几 HH:MM
    Monthly,       // 每月 日号 HH:MM
    Custom,        // 自定义
};

QString triggerLabel(TriggerTemplate t) {
    switch (t) {
    case TriggerTemplate::EveryMinute: return QStringLiteral("每分钟");
    case TriggerTemplate::Hourly: return QStringLiteral("每小时（指定分钟）");
    case TriggerTemplate::Daily: return QStringLiteral("每天");
    case TriggerTemplate::Weekly: return QStringLiteral("每周（指定星期）");
    case TriggerTemplate::Monthly: return QStringLiteral("每月（指定日期）");
    case TriggerTemplate::Custom: return QStringLiteral("自定义（cron 字段）");
    }
    return QString();
}

// 审查 S1：按既有字段推断触发器模板（编辑时默认选中，避免"确定即毁掉
// 原调度"）。
TriggerTemplate inferTemplate(const Task& t) {
    const bool dom = t.dayOfMonth >= 0;
    const bool dow = t.dayOfWeek >= 0;
    const bool month = t.month >= 0;
    if (!dom && !dow && !month) {
        if (t.hour < 0 && t.minute < 0) {
            return TriggerTemplate::EveryMinute;
        }
        if (t.hour < 0) {
            return TriggerTemplate::Hourly;  // 仅 minute 指定
        }
        return TriggerTemplate::Daily;
    }
    if (dow && !dom && !month) {
        return TriggerTemplate::Weekly;
    }
    if (dom && !dow && !month) {
        return TriggerTemplate::Monthly;
    }
    return TriggerTemplate::Custom;
}

}  // namespace

// ---- 任务编辑对话框 ----

QDialog* makeTaskDialog(QWidget* parent, const Task& initial, Task* out) {
    auto* dlg = new QDialog(parent);
    dlg->setWindowTitle(initial.id == 0 ? QStringLiteral("新建任务")
                                        : QStringLiteral("编辑任务：%1")
                                              .arg(initial.name));
    dlg->setMinimumWidth(460);
    auto* lay = new QVBoxLayout(dlg);
    auto* form = new QFormLayout;

    auto* nameEdit = new QLineEdit(dlg);
    nameEdit->setText(initial.name);
    nameEdit->setPlaceholderText(QStringLiteral("任务名称"));
    form->addRow(QStringLiteral("名称"), nameEdit);

    auto* cmdEdit = new QLineEdit(dlg);
    cmdEdit->setText(initial.command);
    cmdEdit->setPlaceholderText(QStringLiteral("要执行的命令（如 /usr/bin/backup.sh）"));
    form->addRow(QStringLiteral("命令"), cmdEdit);

    // 触发器模板 + 具体参数。
    auto* tplCombo = new QComboBox(dlg);
    for (int i = 0; i <= static_cast<int>(TriggerTemplate::Custom); ++i) {
        tplCombo->addItem(triggerLabel(static_cast<TriggerTemplate>(i)), i);
    }
    // 审查 S1：编辑时按既有字段推断模板并选中（默认停在"每分钟"会毁掉
    // 原调度）。
    tplCombo->setCurrentIndex(static_cast<int>(inferTemplate(initial)));
    form->addRow(QStringLiteral("触发器"), tplCombo);

    auto* timeEdit = new QTimeEdit(dlg);
    timeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    timeEdit->setTime(QTime(initial.hour >= 0 ? initial.hour : 0,
                            initial.minute >= 0 ? initial.minute : 0));
    form->addRow(QStringLiteral("时间"), timeEdit);

    // 审查 S2：星期下拉含"忽略"项（-1）——控件需能表达 cron 的忽略语义。
    auto* dayCombo = new QComboBox(dlg);
    dayCombo->addItem(QStringLiteral("（忽略）"), -1);
    const QStringList days = {QStringLiteral("周日"), QStringLiteral("周一"),
                              QStringLiteral("周二"), QStringLiteral("周三"),
                              QStringLiteral("周四"), QStringLiteral("周五"),
                              QStringLiteral("周六")};
    for (int i = 0; i < days.size(); ++i) {
        dayCombo->addItem(days.at(i), i);
    }
    if (initial.dayOfWeek >= 0 && initial.dayOfWeek <= 6) {
        dayCombo->setCurrentIndex(initial.dayOfWeek + 1);  // 跳过"忽略"项
    }
    form->addRow(QStringLiteral("星期"), dayCombo);

    // 审查 S2：日期/月份 spin 支持 0=忽略。
    auto* daySpin = new QSpinBox(dlg);
    daySpin->setRange(0, 31);
    daySpin->setSpecialValueText(QStringLiteral("（忽略）"));
    daySpin->setValue(initial.dayOfMonth >= 1 ? initial.dayOfMonth : 0);
    form->addRow(QStringLiteral("日期"), daySpin);

    auto* minuteSpin = new QSpinBox(dlg);
    minuteSpin->setRange(0, 59);
    minuteSpin->setValue(initial.minute >= 0 ? initial.minute : 0);
    form->addRow(QStringLiteral("分钟"), minuteSpin);

    auto* hourSpin = new QSpinBox(dlg);
    hourSpin->setRange(0, 23);
    hourSpin->setValue(initial.hour >= 0 ? initial.hour : 0);
    form->addRow(QStringLiteral("小时"), hourSpin);

    auto* monthSpin = new QSpinBox(dlg);
    monthSpin->setRange(0, 12);
    monthSpin->setSpecialValueText(QStringLiteral("（忽略）"));
    monthSpin->setValue(initial.month >= 1 ? initial.month : 0);
    form->addRow(QStringLiteral("月份"), monthSpin);

    auto* enabledCheck = new QCheckBox(QStringLiteral("启用"), dlg);
    enabledCheck->setChecked(initial.enabled);
    form->addRow(QString(), enabledCheck);

    // 模板联动：仅显示相关控件（简单实现：全部显示，模板决定保存语义）。
    lay->addLayout(form);

    auto* box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, dlg, [dlg, out, nameEdit,
        cmdEdit, tplCombo, timeEdit, dayCombo, daySpin, minuteSpin, hourSpin,
        monthSpin, enabledCheck] {
        const QString name = nameEdit->text().trimmed();
        const QString cmd = cmdEdit->text().trimmed();
        if (name.isEmpty() || cmd.isEmpty()) {
            QMessageBox::warning(dlg, QStringLiteral("任务计划"),
                QStringLiteral("名称与命令不能为空。"));
            return;
        }
        out->name = name;
        out->command = cmd;
        out->enabled = enabledCheck->isChecked();
        // 按模板组装调度字段（-1 = 通配/忽略）。
        out->minute = out->hour = -1;
        out->dayOfMonth = out->month = out->dayOfWeek = -1;
        const auto tpl = static_cast<TriggerTemplate>(tplCombo->currentData().toInt());
        const QTime t = timeEdit->time();
        switch (tpl) {
        case TriggerTemplate::EveryMinute:
            break;  // 全 -1 = 每分钟
        case TriggerTemplate::Hourly:
            out->minute = minuteSpin->value();
            break;
        case TriggerTemplate::Daily:
            out->minute = t.minute();
            out->hour = t.hour();
            break;
        case TriggerTemplate::Weekly:
            out->minute = t.minute();
            out->hour = t.hour();
            out->dayOfWeek = dayCombo->currentData().toInt();  // -1=忽略
            break;
        case TriggerTemplate::Monthly:
            out->minute = t.minute();
            out->hour = t.hour();
            out->dayOfMonth = daySpin->value();
            break;
        case TriggerTemplate::Custom:
            // 审查 S2：Custom 读各字段控件，0/忽略项 → -1（不再无条件
            // 置位 dom/dow/month——原实现导致"每周日意外触发"）。
            out->minute = minuteSpin->value();
            out->hour = hourSpin->value();
            out->dayOfMonth = daySpin->value() == 0 ? -1 : daySpin->value();
            out->month = monthSpin->value() == 0 ? -1 : monthSpin->value();
            out->dayOfWeek = dayCombo->currentData().toInt();
            break;
        }
        dlg->accept();
    });
    QObject::connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    lay->addWidget(box);
    return dlg;
}

// ---- TaskWindow ----

TaskWindow::TaskWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    applyTheme();
    setWindowTitle(QStringLiteral("任务计划程序"));
    resize(820, 480);
    refreshTasks();
}

void TaskWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);

    auto* headRow = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("任务计划程序"), central);
    title->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold;"));
    headRow->addWidget(title);
    headRow->addStretch(1);
    lay->addLayout(headRow);

    table_ = new QTableWidget(central);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("名称"), QStringLiteral("触发器"), QStringLiteral("上次运行"),
         QStringLiteral("结果"), QStringLiteral("状态")});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    lay->addWidget(table_, 1);
    connect(table_, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { editTask(row); });

    auto* btnRow = new QHBoxLayout;
    auto* newBtn = new QPushButton(QStringLiteral("新建任务…"), central);
    auto* editBtn = new QPushButton(QStringLiteral("编辑…"), central);
    auto* delBtn = new QPushButton(QStringLiteral("删除"), central);
    auto* toggleBtn = new QPushButton(QStringLiteral("启用/禁用"), central);
    auto* runBtn = new QPushButton(QStringLiteral("立即运行"), central);
    btnRow->addWidget(newBtn);
    btnRow->addWidget(editBtn);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(toggleBtn);
    btnRow->addWidget(runBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);
    connect(newBtn, &QPushButton::clicked, this, &TaskWindow::newTask);
    connect(editBtn, &QPushButton::clicked, this, [this] {
        editTask(table_->currentRow());
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        deleteTask(table_->currentRow());
    });
    connect(toggleBtn, &QPushButton::clicked, this, [this] {
        toggleTask(table_->currentRow());
    });
    connect(runBtn, &QPushButton::clicked, this, [this] {
        runNow(table_->currentRow());
    });

    statusLabel_ = new QLabel(central);
    lay->addWidget(statusLabel_);
    setCentralWidget(central);
}

void TaskWindow::applyTheme() {
    const QColor bg = w10de::theme::kStartMenuBackground();
    const QColor fg = w10de::theme::kTextPrimary();
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background: %1; color: %2; }"
        "QTableWidget { background: %3; color: %2; border: 1px solid %4; }"
        "QHeaderView::section { background: %3; color: %2;"
        "  border: none; border-bottom: 1px solid %4; padding: 4px; }"
        "QPushButton { background: %4; color: %2; border: none;"
        "  border-radius: 3px; padding: 4px 12px; }"
        "QPushButton:hover { background: %5; }")
        .arg(bg.name(), fg.name(),
             w10de::theme::kTaskbarBackground().name(),
             w10de::theme::kHoverBackground().name(),
             w10de::theme::kPressedBackground().name()));
}

void TaskWindow::refreshTasks() {
    tasks_ = loadTasks();
    table_->setRowCount(tasks_.size());
    for (int i = 0; i < tasks_.size(); ++i) {
        const Task& t = tasks_.at(i);
        table_->setItem(i, 0, new QTableWidgetItem(t.name));
        table_->setItem(i, 1, new QTableWidgetItem(triggerText(t)));
        table_->setItem(i, 2, new QTableWidgetItem(
            t.lastRun.isEmpty() ? QStringLiteral("-") : t.lastRun));
        table_->setItem(i, 3, new QTableWidgetItem(
            t.lastResult.isEmpty() ? QStringLiteral("-") : t.lastResult));
        table_->setItem(i, 4, new QTableWidgetItem(
            t.enabled ? QStringLiteral("已启用") : QStringLiteral("已禁用")));
    }
    statusLabel_->setText(QStringLiteral("%1 个任务（%2）")
        .arg(tasks_.size())
        .arg(tasksConfigPath()));
}

void TaskWindow::saveToDisk() {
    // 审查 M4（TOCTOU）：保存前合并磁盘上（守护写回）的 last_run/
    // last_result——GUI 用陈旧列表全量写会覆盖守护刚更新的执行状态。
    const QList<Task> disk = loadTasks();
    QHash<int, const Task*> byId;
    for (const Task& d : disk) {
        byId.insert(d.id, &d);
    }
    for (Task& t : tasks_) {
        const auto it = byId.constFind(t.id);
        if (it != byId.constEnd() && it.value() != nullptr) {
            t.lastRun = it.value()->lastRun;
            t.lastResult = it.value()->lastResult;
        }
    }
    if (!saveTasks(tasks_)) {
        QMessageBox::warning(this, QStringLiteral("任务计划"),
            QStringLiteral("保存任务配置失败（%1）。").arg(tasksConfigPath()));
        return;
    }
    notifyDaemonReload();
    refreshTasks();
}

void TaskWindow::notifyDaemonReload() {
    // 通知守护重载（org.w10de.Tasks /Tasks Reload；守护未运行时静默）。
    QDBusInterface iface(QStringLiteral("org.w10de.Tasks"),
                         QStringLiteral("/Tasks"),
                         QStringLiteral("org.w10de.Tasks"),
                         QDBusConnection::sessionBus(), this);
    if (iface.isValid()) {
        iface.call(QStringLiteral("Reload"));
    }
}

void TaskWindow::newTask() {
    Task t;
    Task result;
    QDialog* dlg = makeTaskDialog(this, t, &result);
    if (dlg->exec() == QDialog::Accepted) {
        // 分配新 id（现有最大 + 1）。
        int maxId = 0;
        for (const Task& t2 : tasks_) {
            maxId = qMax(maxId, t2.id);
        }
        result.id = maxId + 1;
        tasks_.append(result);
        saveToDisk();
    }
    delete dlg;
}

void TaskWindow::editTask(int row) {
    if (row < 0 || row >= tasks_.size()) {
        return;
    }
    Task result = tasks_.at(row);
    QDialog* dlg = makeTaskDialog(this, tasks_.at(row), &result);
    if (dlg->exec() == QDialog::Accepted) {
        result.id = tasks_.at(row).id;
        result.lastRun = tasks_.at(row).lastRun;
        result.lastResult = tasks_.at(row).lastResult;
        tasks_[row] = result;
        saveToDisk();
    }
    delete dlg;
}

void TaskWindow::deleteTask(int row) {
    if (row < 0 || row >= tasks_.size()) {
        return;
    }
    const QString name = tasks_.at(row).name;
    if (QMessageBox::question(this, QStringLiteral("任务计划"),
            QStringLiteral("删除任务“%1”？").arg(name))
            != QMessageBox::Yes) {
        return;
    }
    tasks_.removeAt(row);
    saveToDisk();
}

void TaskWindow::toggleTask(int row) {
    if (row < 0 || row >= tasks_.size()) {
        return;
    }
    tasks_[row].enabled = !tasks_[row].enabled;
    saveToDisk();
}

void TaskWindow::runNow(int row) {
    if (row < 0 || row >= tasks_.size()) {
        return;
    }
    Task& t = tasks_[row];
    // 经 shell 执行（命令可含参数/管道）。
    const bool ok = QProcess::startDetached(
        QStringLiteral("/bin/sh"), {QStringLiteral("-c"), t.command});
    t.lastRun = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    t.lastResult = ok ? QStringLiteral("OK（已启动）")
                      : QStringLiteral("启动失败");
    saveToDisk();
    statusLabel_->setText(ok
        ? QStringLiteral("已启动：%1").arg(t.command)
        : QStringLiteral("启动失败：%1").arg(t.command));
}

}  // namespace w10task

// monitorwindow.cpp —— 系统监视器 UI 实现。

#include "systemapps/monitor/monitorwindow.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <unistd.h>  // getpid（自杀保护）

#include "systemapps/monitor/sysinfo.h"

namespace w10de::monitor {

namespace {
constexpr int kMaxCores = 32;
}

// ---- GraphWidget ----

GraphWidget::GraphWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(160);
}

void GraphWidget::setHistory(const QVector<double>& data, double maxValue) {
    data_ = data;
    maxValue_ = maxValue > 0 ? maxValue : 100.0;
    update();
}

void GraphWidget::setCaption(const QString& caption) {
    caption_ = caption;
    update();
}

void GraphWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const int w = width();
    const int h = height();

    // 背景。
    p.fillRect(rect(), QColor(0x1E, 0x1E, 0x1E));

    // 网格线（每 25% 一条水平线）。
    p.setPen(QPen(QColor(0x3C, 0x3C, 0x3C), 1));
    for (int i = 1; i < 4; ++i) {
        const int y = h - h * i / 4;
        p.drawLine(0, y, w, y);
    }

    // 数据折线（右对齐滚动）。
    const int n = data_.size();
    if (n >= 2) {
        const double xStep = static_cast<double>(w) / (SysInfo::kHistory - 1);
        QPolygonF poly;
        for (int i = 0; i < n; ++i) {
            const double v = data_[i] < 0 ? 0 : (data_[i] > maxValue_ ? maxValue_ : data_[i]);
            const double x = w - static_cast<double>(n - 1 - i) * xStep;
            const double y = h - h * v / maxValue_;
            poly << QPointF(x, y);
        }
        p.setPen(QPen(QColor(0x00, 0x78, 0xD7), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(poly);
    }

    // 标题。
    if (!caption_.isEmpty()) {
        p.setPen(QColor(0xE0, 0xE0, 0xE0));
        p.drawText(8, 20, caption_);
    }
}

// ---- MonitorWindow ----

MonitorWindow::MonitorWindow(QWidget* parent) : QMainWindow(parent) {
    sys_ = new SysInfo();
    sys_->sample();  // 首次基准。

    setWindowTitle(QStringLiteral("任务管理器"));
    resize(720, 560);
    buildUi();

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MonitorWindow::refresh);
    timer_->start(1000);
    refresh();
}

MonitorWindow::~MonitorWindow() {
    delete sys_;
}

void MonitorWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    tabs_ = new QTabWidget(central);
    root->addWidget(tabs_);

    // ---- 性能页（原有内容）----
    auto* perfPage = new QWidget(tabs_);
    auto* perfLay = new QVBoxLayout(perfPage);
    perfLay->setContentsMargins(8, 8, 8, 8);
    perfLay->setSpacing(10);

    cpuGraph_ = new GraphWidget(perfPage);
    cpuGraph_->setCaption(QStringLiteral("CPU 使用率"));
    perfLay->addWidget(cpuGraph_, 1);

    cpuSummary_ = new QLabel(perfPage);
    cpuSummary_->setStyleSheet("color:#E0E0E0;");
    perfLay->addWidget(cpuSummary_);

    // 每核进度条网格。
    auto* coreGrid = new QGridLayout;
    coreGrid->setSpacing(6);
    const int cores = sys_->coreCount();
    perCoreCount_ = cores > 32 ? 32 : cores;
    for (int i = 0; i < perCoreCount_; ++i) {
        perCoreLabels_[i] = new QLabel(QStringLiteral("CPU%1").arg(i), perfPage);
        perCoreLabels_[i]->setStyleSheet("color:#C0C0C0;");
        perCoreBars_[i] = new QProgressBar(perfPage);
        perCoreBars_[i]->setRange(0, 100);
        perCoreBars_[i]->setTextVisible(false);
        perCoreBars_[i]->setFixedHeight(10);
        perCoreBars_[i]->setStyleSheet(
            "QProgressBar { background:#333333; border:none; border-radius:2px; }"
            "QProgressBar::chunk { background:#0078D7; }");
        coreGrid->addWidget(perCoreLabels_[i], i / 4, (i % 4) * 2);
        coreGrid->addWidget(perCoreBars_[i], i / 4, (i % 4) * 2 + 1);
    }
    perfLay->addLayout(coreGrid);

    // 内存。
    auto* memRow = new QHBoxLayout;
    memLabel_ = new QLabel(perfPage);
    memLabel_->setStyleSheet("color:#E0E0E0;");
    memBar_ = new QProgressBar(perfPage);
    memBar_->setRange(0, 100);
    memBar_->setFixedHeight(14);
    memBar_->setStyleSheet(
        "QProgressBar { background:#333333; border:none; border-radius:2px; }"
        "QProgressBar::chunk { background:#0078D7; }");
    memRow->addWidget(memLabel_, 0);
    memRow->addWidget(memBar_, 1);
    perfLay->addLayout(memRow);

    // 磁盘/网络。
    diskNetLabel_ = new QLabel(perfPage);
    diskNetLabel_->setStyleSheet("color:#C0C0C0;");
    perfLay->addWidget(diskNetLabel_);
    tabs_->addTab(perfPage, QStringLiteral("性能"));

    // ---- 进程页（KDE-GAP #2）----
    auto* procPage = new QWidget(tabs_);
    auto* procLay = new QVBoxLayout(procPage);
    procLay->setContentsMargins(8, 8, 8, 8);
    procLay->setSpacing(8);

    procTable_ = new QTableWidget(procPage);
    procTable_->setColumnCount(4);
    procTable_->setHorizontalHeaderLabels(
        {QStringLiteral("PID"), QStringLiteral("名称"), QStringLiteral("CPU%"),
         QStringLiteral("内存")});
    procTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    procTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    procTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    procTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    procTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    procTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    procTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    procTable_->setAlternatingRowColors(true);
    procLay->addWidget(procTable_, 1);

    auto* killBtn = new QPushButton(QStringLiteral("结束进程"), procPage);
    connect(killBtn, &QPushButton::clicked, this, &MonitorWindow::killSelected);
    procLay->addWidget(killBtn, 0, Qt::AlignLeft);
    tabs_->addTab(procPage, QStringLiteral("进程"));

    setCentralWidget(central);
}

void MonitorWindow::refresh() {
    sys_->sample();

    // CPU 曲线。
    QVector<double> hist;
    for (double v : sys_->cpuTotalHistory()) {
        hist.append(v);
    }
    cpuGraph_->setHistory(hist, 100.0);
    cpuGraph_->setCaption(
        QStringLiteral("CPU 使用率  %1%").arg(sys_->cpuTotal(), 0, 'f', 0));

    cpuSummary_->setText(
        QStringLiteral("使用率 %1%    处理器: %2 个核心")
            .arg(sys_->cpuTotal(), 0, 'f', 0)
            .arg(sys_->coreCount()));

    // 每核。
    const auto& perCore = sys_->cpuPerCore();
    for (int i = 0; i < perCoreCount_; ++i) {
        const double v = i < static_cast<int>(perCore.size()) ? perCore[static_cast<size_t>(i)] : 0.0;
        perCoreBars_[i]->setValue(static_cast<int>(std::lround(v)));
        perCoreLabels_[i]->setText(
            QStringLiteral("CPU%1 %2%").arg(i).arg(static_cast<int>(std::lround(v))));
    }

    // 内存。
    const auto& m = sys_->mem();
    const double usedPct = m.usedPercent();
    memBar_->setValue(static_cast<int>(std::lround(usedPct)));
    const double totalGb = static_cast<double>(m.totalKb) / (1024.0 * 1024.0);
    const double usedGb = static_cast<double>(m.totalKb - m.availableKb) /
                          (1024.0 * 1024.0);
    memLabel_->setText(
        QStringLiteral("内存  %1%    已用 %2 GB / %3 GB（swap %4%）")
            .arg(usedPct, 0, 'f', 0)
            .arg(usedGb, 0, 'f', 1)
            .arg(totalGb, 0, 'f', 1)
            .arg(m.swapUsedPercent(), 0, 'f', 0));

    // 磁盘/网络。
    diskNetLabel_->setText(
        QStringLiteral("磁盘 %1：读 %2 MB/s  写 %3 MB/s      "
                       "网络 %4：收 %5 KB/s  发 %6 KB/s")
            .arg(QString::fromStdString(sys_->diskDevice()),
                 QString::number(sys_->diskReadMBps(), 'f', 2),
                 QString::number(sys_->diskWriteMBps(), 'f', 2),
                 QString::fromStdString(sys_->netInterface()),
                 QString::number(sys_->netRxKBps(), 'f', 1),
                 QString::number(sys_->netTxKBps(), 'f', 1)));

    refreshProcesses();
}

void MonitorWindow::refreshProcesses() {
    const auto procs = sys_->processList();
    // 限制显示行数（避免超大列表卡 UI；保留前 200 个按 CPU 排序的）。
    // 审查 L1：直接一次 setRowCount（原实现先设全量再截断，多建 N 行）。
    const int rows = static_cast<int>(procs.size()) > 200
        ? 200 : static_cast<int>(procs.size());
    procTable_->setRowCount(rows);
    for (int i = 0; i < rows; ++i) {
        const ProcInfo& p = procs[static_cast<size_t>(i)];
        procTable_->setItem(i, 0, new QTableWidgetItem(QString::number(p.pid)));
        procTable_->setItem(i, 1, new QTableWidgetItem(
            QString::fromStdString(p.cmdline.length() > 60
                ? p.cmdline.substr(0, 60) + "…" : p.cmdline)));
        procTable_->setItem(i, 2, new QTableWidgetItem(
            QString::number(p.cpuPercent, 'f', 1)));
        procTable_->setItem(i, 3, new QTableWidgetItem(
            QStringLiteral("%1 MB").arg(p.rssKB / 1024.0, 0, 'f', 1)));
    }
}

void MonitorWindow::killSelected() {
    const int row = procTable_->currentRow();
    if (row < 0) {
        return;
    }
    const int pid = procTable_->item(row, 0)->text().toInt();
    const QString name = procTable_->item(row, 1)->text();
    // 审查 L5：防止结束自身（自杀）。
    if (pid == static_cast<int>(::getpid())) {
        QMessageBox::information(this, QStringLiteral("结束进程"),
                                 QStringLiteral("不能结束监视器自身。"));
        return;
    }
    // 审查 M3：提供"强制结束"（SIGKILL）选项。
    const auto reply = QMessageBox::question(
        this, QStringLiteral("结束进程"),
        QStringLiteral("结束进程 %1（PID %2）？\n"
                       "（选择“取消”关闭；如需强制请选“强制结束”）")
            .arg(name).arg(pid),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::YesToAll,
        QMessageBox::No);
    bool force = false;
    if (reply == QMessageBox::YesToAll) {
        force = true;
    } else if (reply != QMessageBox::Yes) {
        return;
    }
    // 审查 L5：文案区分"信号已发送"与"发送失败"（SIGTERM 送达≠进程退出）。
    if (SysInfo::killProcess(pid, force)) {
        QMessageBox::information(this, QStringLiteral("结束进程"),
                                 QStringLiteral("已发送%1。进程可能稍后退出。")
                                     .arg(force ? QStringLiteral("强制结束信号")
                                                : QStringLiteral("结束信号")));
    } else {
        QMessageBox::warning(this, QStringLiteral("结束进程"),
                             QStringLiteral("发送信号失败（权限不足或进程已退出）。"));
    }
    refreshProcesses();
}

}  // namespace w10de::monitor

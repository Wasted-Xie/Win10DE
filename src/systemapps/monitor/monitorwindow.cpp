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
    setMinimumHeight(120);
}

void GraphWidget::setHistory(const QVector<double>& data, double maxValue) {
    data_ = data;
    dataB_.clear();
    dual_ = false;
    maxValue_ = maxValue > 0 ? maxValue : 100.0;
    update();
}

void GraphWidget::setDualHistory(const QVector<double>& dataA,
                                 const QVector<double>& dataB,
                                 double maxValue, const QColor& colorA,
                                 const QColor& colorB, const QString& legendA,
                                 const QString& legendB) {
    data_ = dataA;
    dataB_ = dataB;
    dual_ = true;
    // G4 审查 L4：y 轴上限 EMA 平滑（峰值不瞬间拉高压扁曲线、低谷不
    // 立即贴底）——上升快（0.6 权重新峰值）、下降慢（0.85 保留旧值）。
    const double target = maxValue > 0 ? maxValue : 100.0;
    maxValue_ = qMax(1.0, qMax(lastMax_ * 0.85, target * 0.6));
    lastMax_ = maxValue_;
    colorA_ = colorA;
    colorB_ = colorB;
    legendA_ = legendA;
    legendB_ = legendB;
    update();
}

void GraphWidget::setCaption(const QString& caption) {
    caption_ = caption;
    update();
}

namespace {

// 折线绘制（右对齐滚动；值钳制 [0, maxValue]）。
void drawSeries(QPainter* p, const QVector<double>& data, double maxValue,
                int w, int h, const QColor& color) {
    const int n = data.size();
    if (n < 2) {
        return;
    }
    const double xStep = static_cast<double>(w)
        / (static_cast<double>(w) > 0 ? (n > 1 ? n - 1 : 1) : 1);
    QPolygonF poly;
    for (int i = 0; i < n; ++i) {
        const double v = data[i] < 0 ? 0
            : (data[i] > maxValue ? maxValue : data[i]);
        const double x = w - static_cast<double>(n - 1 - i) * xStep;
        const double y = h - h * v / maxValue;
        poly << QPointF(x, y);
    }
    p->setPen(QPen(color, 2));
    p->setBrush(Qt::NoBrush);
    p->drawPolyline(poly);
}

}  // namespace

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

    if (dual_) {
        drawSeries(&p, data_, maxValue_, w, h, colorA_);
        drawSeries(&p, dataB_, maxValue_, w, h, colorB_);
        // 图例（右上）。
        p.setPen(colorA_);
        p.drawText(w - 200, 16, QStringLiteral("— %1").arg(legendA_));
        p.setPen(colorB_);
        p.drawText(w - 100, 16, QStringLiteral("— %1").arg(legendB_));
    } else {
        drawSeries(&p, data_, maxValue_, w, h, colorA_);
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
    resize(900, 680);  // G4：4 图布局需更大窗口
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

    // ---- 性能页（G4：CPU/内存/磁盘/网络 4 图）----
    auto* perfPage = new QWidget(tabs_);
    auto* perfLay = new QVBoxLayout(perfPage);
    perfLay->setContentsMargins(8, 8, 8, 8);
    perfLay->setSpacing(8);

    // 第一行：CPU + 内存曲线。
    auto* row1 = new QHBoxLayout;
    cpuGraph_ = new GraphWidget(perfPage);
    cpuGraph_->setCaption(QStringLiteral("CPU 使用率"));
    row1->addWidget(cpuGraph_, 1);
    memGraph_ = new GraphWidget(perfPage);
    memGraph_->setCaption(QStringLiteral("内存使用率"));
    row1->addWidget(memGraph_, 1);
    perfLay->addLayout(row1, 1);

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

    // 第二行：磁盘 + 网络曲线（G4 双序列）。
    auto* row2 = new QHBoxLayout;
    diskGraph_ = new GraphWidget(perfPage);
    diskGraph_->setCaption(QStringLiteral("磁盘读写"));
    row2->addWidget(diskGraph_, 1);
    netGraph_ = new GraphWidget(perfPage);
    netGraph_->setCaption(QStringLiteral("网络收发"));
    row2->addWidget(netGraph_, 1);
    perfLay->addLayout(row2, 1);

    // 磁盘/网络详情（含 G4 累计总量）。
    diskNetLabel_ = new QLabel(perfPage);
    diskNetLabel_->setStyleSheet("color:#C0C0C0;");
    diskNetLabel_->setWordWrap(true);
    perfLay->addWidget(diskNetLabel_);
    tabs_->addTab(perfPage, QStringLiteral("性能"));

    // ---- 进程页（KDE-GAP #2）----
    auto* procPage = new QWidget(tabs_);
    auto* procLay = new QVBoxLayout(procPage);
    procLay->setContentsMargins(8, 8, 8, 8);
    procLay->setSpacing(8);

    procTable_ = new QTableWidget(procPage);
    // G4：加每进程磁盘 IO 列（读/写 KB/s）。
    procTable_->setColumnCount(6);
    procTable_->setHorizontalHeaderLabels(
        {QStringLiteral("PID"), QStringLiteral("名称"), QStringLiteral("CPU%"),
         QStringLiteral("内存"), QStringLiteral("IO 读"), QStringLiteral("IO 写")});
    procTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    procTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    procTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    procTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    procTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    procTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
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

    // G4：内存曲线。
    QVector<double> memHist;
    for (double v : sys_->memHistory()) {
        memHist.append(v);
    }
    const auto& m = sys_->mem();
    memGraph_->setHistory(memHist, 100.0);
    memGraph_->setCaption(
        QStringLiteral("内存使用率  %1%").arg(m.usedPercent(), 0, 'f', 0));

    // G4：磁盘曲线（双序列读/写 MB/s；max 自适应历史峰值）。
    QVector<double> drHist, dwHist;
    double diskMax = 1.0;
    for (double v : sys_->diskReadHistory()) {
        drHist.append(v);
        diskMax = std::max(diskMax, v);
    }
    for (double v : sys_->diskWriteHistory()) {
        dwHist.append(v);
        diskMax = std::max(diskMax, v);
    }
    diskGraph_->setDualHistory(drHist, dwHist, diskMax * 1.3,
                               QColor(0, 120, 215), QColor(16, 180, 110),
                               QStringLiteral("读"), QStringLiteral("写"));
    diskGraph_->setCaption(QStringLiteral("磁盘读写（MB/s）"));

    // G4：网络曲线（双序列收/发 KB/s）。
    QVector<double> nrHist, ntHist;
    double netMax = 1.0;
    for (double v : sys_->netRxHistory()) {
        nrHist.append(v);
        netMax = std::max(netMax, v);
    }
    for (double v : sys_->netTxHistory()) {
        ntHist.append(v);
        netMax = std::max(netMax, v);
    }
    netGraph_->setDualHistory(nrHist, ntHist, netMax * 1.3,
                              QColor(0, 120, 215), QColor(16, 180, 110),
                              QStringLiteral("收"), QStringLiteral("发"));
    netGraph_->setCaption(QStringLiteral("网络收发（KB/s）"));

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

    // 磁盘/网络（G4：速率 + 累计总量）。
    const auto humanBytes = [](unsigned long long bytes) {
        if (bytes >= 1024ull * 1024 * 1024) {
            return QStringLiteral("%1 GB").arg(
                bytes / (1024.0 * 1024 * 1024), 0, 'f', 1);
        }
        if (bytes >= 1024ull * 1024) {
            return QStringLiteral("%1 MB").arg(
                bytes / (1024.0 * 1024), 0, 'f', 1);
        }
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    };
    diskNetLabel_->setText(
        QStringLiteral("磁盘 %1：读 %2 MB/s  写 %3 MB/s（累计读 %4 / 写 %5）\n"
                       "网络 %6：收 %7 KB/s  发 %8 KB/s（累计收 %9 / 发 %10）")
            .arg(QString::fromStdString(sys_->diskDevice()),
                 QString::number(sys_->diskReadMBps(), 'f', 2),
                 QString::number(sys_->diskWriteMBps(), 'f', 2),
                 humanBytes(sys_->diskReadTotalBytes()),
                 humanBytes(sys_->diskWriteTotalBytes()),
                 QString::fromStdString(sys_->netInterface()),
                 QString::number(sys_->netRxKBps(), 'f', 1),
                 QString::number(sys_->netTxKBps(), 'f', 1),
                 humanBytes(sys_->netRxTotalBytes()),
                 humanBytes(sys_->netTxTotalBytes())));

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
        // G4：每进程 IO 读写速率（KB/s；无权限/内核线程为 0）。
        procTable_->setItem(i, 4, new QTableWidgetItem(
            p.ioReadKBps > 0 ? QString::number(p.ioReadKBps, 'f', 1)
                             : QStringLiteral("-")));
        procTable_->setItem(i, 5, new QTableWidgetItem(
            p.ioWriteKBps > 0 ? QString::number(p.ioWriteKBps, 'f', 1)
                              : QStringLiteral("-")));
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

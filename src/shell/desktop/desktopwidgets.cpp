// desktopwidgets.cpp —— 桌面小部件实现。

#include "shell/desktop/desktopwidgets.h"

#include <QDateTime>
#include <QFile>
#include <QLocale>
#include <QPainter>
#include <QTextStream>

#include <cstdio>

#include "ipc/config.h"

namespace w10de {

WidgetsConfig loadWidgetsConfig(const std::string& configPath) {
    WidgetsConfig cfg;
    const Config c = Config::load(configPath);
    cfg.showClock = c.getInt("widgets", "show_clock", 1) != 0;
    cfg.showSysinfo = c.getInt("widgets", "show_sysinfo", 0) != 0;
    return cfg;
}

int readMemoryPercent() {
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }
    QTextStream ts(&f);
    qulonglong total = 0, available = 0;
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        if (line.startsWith(QStringLiteral("MemTotal:"))) {
            total = line.section(QLatin1Char(' '), 1, 1).toULongLong();
        } else if (line.startsWith(QStringLiteral("MemAvailable:"))) {
            available = line.section(QLatin1Char(' '), 1, 1).toULongLong();
            break;
        }
    }
    if (total == 0) {
        return -1;
    }
    if (available == 0) {
        available = total;  // 老内核无 MemAvailable：视为空闲
    }
    return static_cast<int>((total - available) * 100 / total);
}

int readCpuPercent(CpuBaseline& baseline) {
    // /proc/stat 首行 cpu：user nice system idle iowait irq softirq steal
    // 审查 M1：iowait 属空闲（等待 I/O 不算忙），分母须含 irq/softirq/
    // steal——只读前 4 字段会在 I/O 负载下虚高 CPU%。
    QFile f(QStringLiteral("/proc/stat"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }
    const QByteArray line = f.readLine();  // "cpu  ..."
    unsigned long long user = 0, nice = 0, system = 0, idle = 0;
    unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
    std::sscanf(line.constData(),
                "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                &user, &nice, &system, &idle,
                &iowait, &irq, &softirq, &steal);
    const unsigned long long idleAll = idle + iowait;
    const unsigned long long total =
        user + nice + system + idleAll + irq + softirq + steal;
    if (!baseline.haveBaseline) {
        // 首次调用：记录基线，返回 0（无历史可算增量）。
        baseline.prevTotal = total;
        baseline.prevIdle = idleAll;
        baseline.haveBaseline = true;
        return 0;
    }
    if (total < baseline.prevTotal) {
        // 计数器回绕：重置基线。
        baseline.prevTotal = total;
        baseline.prevIdle = idleAll;
        return 0;
    }
    const unsigned long long dTotal = total - baseline.prevTotal;
    const unsigned long long dIdle = idleAll - baseline.prevIdle;
    baseline.prevTotal = total;
    baseline.prevIdle = idleAll;
    if (dTotal == 0) {
        return 0;
    }
    return static_cast<int>((dTotal - dIdle) * 100 / dTotal);
}

DesktopWidgets::DesktopWidgets(QWidget* desktop, const WidgetsConfig& cfg,
                               QObject* parent)
    : QObject(parent) {
    // 审查 L6：desktop 必须非空（QLabel 无父会成为独立顶层窗口）。
    Q_ASSERT(desktop != nullptr);
    // 时钟（右上）：深色半透明底 + 白色文字（桌面可读性）。
    clockLabel_ = new QLabel(desktop);
    clockLabel_->setStyleSheet(
        QStringLiteral("background: rgba(0,0,0,120); color: #ffffff;"
                       "border-radius: 4px; padding: 6px 12px;"));
    clockLabel_->setAlignment(Qt::AlignCenter);
    clockLabel_->setVisible(cfg.showClock);

    // 系统信息（左上）：同款样式。
    sysinfoLabel_ = new QLabel(desktop);
    sysinfoLabel_->setStyleSheet(
        QStringLiteral("background: rgba(0,0,0,120); color: #ffffff;"
                       "border-radius: 4px; padding: 6px 12px;"));
    sysinfoLabel_->setVisible(cfg.showSysinfo);

    clockTimer_ = new QTimer(this);
    clockTimer_->setInterval(1000);
    connect(clockTimer_, &QTimer::timeout, this, &DesktopWidgets::refreshClock);
    // 审查 M2：时钟 timer 仅显示时运行（配置关闭不空转）。
    if (cfg.showClock) {
        clockTimer_->start();
        refreshClock();
    }

    sysinfoTimer_ = new QTimer(this);
    sysinfoTimer_->setInterval(2000);
    connect(sysinfoTimer_, &QTimer::timeout,
            this, &DesktopWidgets::refreshSysinfo);
    // 系统信息仅在显示时刷新（配置关闭则 timer 不启动）。
    if (cfg.showSysinfo) {
        sysinfoTimer_->start();
        refreshSysinfo();
    }
}

void DesktopWidgets::reposition(int desktopWidth, int desktopHeight) {
    if (clockLabel_ != nullptr && clockLabel_->isVisible()) {
        clockLabel_->adjustSize();
        clockLabel_->move(desktopWidth - clockLabel_->width() - kMargin,
                          kMargin);
    }
    if (sysinfoLabel_ != nullptr && sysinfoLabel_->isVisible()) {
        sysinfoLabel_->adjustSize();
        sysinfoLabel_->move(kMargin, kMargin);
    }
}

void DesktopWidgets::setClockVisible(bool visible) {
    if (clockLabel_ == nullptr) {
        return;
    }
    clockLabel_->setVisible(visible);
    // 审查 M2：时钟 timer 随可见性启停（隐藏不每秒 setText 空转）。
    if (clockTimer_ != nullptr) {
        if (visible) {
            clockTimer_->start();
            refreshClock();
        } else {
            clockTimer_->stop();
        }
    }
}

void DesktopWidgets::setSysinfoVisible(bool visible) {
    if (sysinfoLabel_ != nullptr) {
        sysinfoLabel_->setVisible(visible);
        // 隐藏时停系统信息刷新（避免每 2s 读 /proc 空转）；显示时重启。
        if (sysinfoTimer_ != nullptr) {
            if (visible) {
                sysinfoTimer_->start();
                refreshSysinfo();
            } else {
                sysinfoTimer_->stop();
            }
        }
    }
}

void DesktopWidgets::refreshClock() {
    if (clockLabel_ == nullptr) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    // 审查 L3：固定格式日期（LongFormat 中文下长度逐日变化，
    // 右上角对齐导致左边缘每日微移）。
    const QString date = now.toString(QStringLiteral("yyyy年M月d日"));
    const QString time = now.toString(QStringLiteral("HH:mm:ss"));
    // 时间大字体 + 日期小字。
    clockLabel_->setText(
        QStringLiteral("<div style='font-size:26px;'>%1</div>"
                       "<div style='font-size:13px; color:#dddddd;'>%2</div>")
            .arg(time, date));
    clockLabel_->adjustSize();
    if (clockLabel_->parentWidget() != nullptr) {
        const int dw = clockLabel_->parentWidget()->width();
        clockLabel_->move(dw - clockLabel_->width() - kMargin, kMargin);
    }
}

void DesktopWidgets::refreshSysinfo() {
    if (sysinfoLabel_ == nullptr) {
        return;
    }
    const int cpu = readCpuPercent(cpuBaseline_);
    const int mem = readMemoryPercent();
    const QString cpuText = cpu < 0 ? QStringLiteral("--")
                                    : QStringLiteral("%1%").arg(cpu);
    const QString memText = mem < 0 ? QStringLiteral("--")
                                    : QStringLiteral("%1%").arg(mem);
    sysinfoLabel_->setText(
        QStringLiteral("<div style='font-size:14px;'>CPU %1 &nbsp; 内存 %2</div>")
            .arg(cpuText, memText));
    sysinfoLabel_->adjustSize();
}

}  // namespace w10de

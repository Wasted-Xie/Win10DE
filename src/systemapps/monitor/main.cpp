// w10monitor —— 系统监视器（Win10 任务管理器风格）。
//
// 通用接口（docs/SYSTEMAPPS.md）：独立二进制 + D-Bus 单实例激活
// （org.w10de.Apps.Monitor，Activate(s path)；path 忽略）。
// CLI：w10monitor [--selftest]。
//
// 自测：--selftest 用 offscreen 平台读取 /proc 两次采样，断言：
//   - 核心数 >= 1、内存总量 > 0、使用率在 [0,100]、速率 >= 0
//   - 连续两次采样不崩溃（增量计算稳定性）

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/monitor/monitorwindow.h"
#include "systemapps/monitor/sysinfo.h"
#include "theme/colors.h"

namespace {

int runSelfTest() {
    QTextStream out(stdout);
    using w10de::monitor::SysInfo;

    SysInfo info;
    info.sample();  // 基准
    info.sample();  // 增量

    int rc = 0;
    auto fail = [&](const char* what) {
        out << "SELFTEST FAIL: " << what << "\n";
        rc = 1;
    };

    if (info.coreCount() < 1) {
        fail("coreCount < 1");
    }
    if (info.mem().totalKb == 0) {
        fail("mem totalKb == 0 (/proc/meminfo 不可读?)");
    }
    const double cpu = info.cpuTotal();
    if (cpu < 0.0 || cpu > 100.0) {
        fail("cpu usage out of [0,100]");
    }
    if (info.mem().usedPercent() < 0.0 || info.mem().usedPercent() > 100.0) {
        fail("mem usage out of [0,100]");
    }
    if (info.netRxKBps() < 0.0 || info.netTxKBps() < 0.0 ||
            info.diskReadMBps() < 0.0 || info.diskWriteMBps() < 0.0) {
        fail("negative rate");
    }
    // 历史缓冲长度约束。
    if (static_cast<int>(info.cpuTotalHistory().size()) > SysInfo::kHistory) {
        fail("history exceeds kHistory");
    }
    // 每核数量与历史结构一致。
    if (info.cpuPerCoreHistory().size() !=
            static_cast<size_t>(info.coreCount())) {
        fail("per-core history size mismatch");
    }

    out << "SELFTEST OK: cores=" << info.coreCount()
        << " cpu=" << QString::number(cpu, 'f', 1) << "%"
        << " mem=" << QString::number(info.mem().usedPercent(), 'f', 1) << "%"
        << " disk=" << info.diskDevice().c_str()
        << " (" << QString::number(info.diskReadMBps(), 'f', 2) << " MB/s)"
        << " net=" << info.netInterface().c_str()
        << " (rx " << QString::number(info.netRxKBps(), 'f', 1) << " KB/s)\n";
    return rc;
}

}  // namespace

int main(int argc, char* argv[]) {
    // 日志即时可见（重定向到文件时 stderr 全缓冲，kill 会丢日志）。
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            break;
        }
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10monitor"));
    QApplication::setApplicationDisplayName(QStringLiteral("任务管理器"));

    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (QApplication::arguments().contains(QStringLiteral("--selftest"))) {
        return runSelfTest();
    }

    // 单实例：既有实例在运行则激活并退出（path 忽略）。
    if (w10de::app::tryActivateExisting(QStringLiteral("Monitor"), QString())) {
        return 0;
    }
    if (!w10de::app::registerService(
            QStringLiteral("Monitor"),
            [](const QString&) {
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* mw = qobject_cast<w10de::monitor::MonitorWindow*>(w)) {
                        mw->show();
                        mw->raise();
                        mw->activateWindow();
                        break;
                    }
                }
            },
            &app)) {
        return 0;
    }

    w10de::monitor::MonitorWindow window;
    window.show();
    return QApplication::exec();
}

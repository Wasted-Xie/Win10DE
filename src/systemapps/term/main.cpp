// w10term —— Win10 风格终端（系统应用）。
//
// 通用接口（docs/SYSTEMAPPS.md）：独立二进制 + D-Bus 单实例激活
// （org.w10de.Apps.Terminal，Activate(s path)；path 忽略——终端无导航语义）。
// CLI：w10term [--selftest]。
//
// 自测：w10term --selftest 用 PTY 启动 `bash -c "echo ..."` 断言输出
// 捕获（无 GUI，offscreen + QCoreApplication 事件循环）。

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QtDebug>
#include <QTextStream>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/term/terminalpty.h"
#include "systemapps/term/termwindow.h"
#include "theme/colors.h"

namespace {

// ---- PTY 自测（headless）：bash -c 输出经 pty 回读 ----
int runSelfTest() {
    QTextStream out(stdout);
    w10de::term::TerminalPty pty;

    QByteArray received;
    bool done = false;
    int rc = 1;
    QObject::connect(&pty, &w10de::term::TerminalPty::outputReady,
                     [&](const QByteArray& data) {
        received += data;
        // bash 输出回显命令本身 + 结果；断言关键标记出现即可。
        if (received.contains("w10term-pty-ok")) {
            done = true;
            rc = 0;
            // 延迟 stop（不在 activated 槽栈内关 fd/删 notifier）。
            QTimer::singleShot(0, &pty, [&pty] { pty.stop(); });
        }
    });

    if (!pty.start()) {
        out << "SELFTEST FAIL: forkpty start failed\n";
        return 1;
    }
    // 交互 shell 无法直接喂命令；改用非交互断言：直接在 pty 写命令
    //（bash -i 会回显并执行）。
    pty.write("echo w10term-pty-ok; exit\n");

    // ANSI 解析单测（readOnly QPlainTextEdit 的文字插入是否生效）。
    {
        w10de::term::TerminalEdit edit;
        edit.appendAnsi("\x1b]0;fake-title\x07\x1b[?2004h"
                        "[root@host win10de]# \x1b[31mred\x1b[0m plain\n");
        const QString text = edit.toPlainText();
        if (!text.contains(QStringLiteral("root@host"))) {
            out << "SELFTEST FAIL: ANSI text not inserted (readOnly?)\n";
            return 1;
        }
        if (!text.contains(QStringLiteral("red plain"))) {
            out << "SELFTEST FAIL: SGR text not inserted\n";
            return 1;
        }
        out << "OK ansi-extraction (text='" << text.trimmed() << "')\n";
    }

    // 超时 5s。
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    pty.stop();
    if (!done) {
        out << "SELFTEST FAIL: timeout, received="
            << QString::fromUtf8(received.left(120)) << "\n";
        return 1;
    }
    out << "SELFTEST PASS (pty round-trip, bytes=" << received.size() << ")\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    // 日志即时可见（重定向到文件时 stderr 全缓冲，kill 会丢日志——调试
    // 多次误判"事件循环卡死"，实为缓冲未 flush）。
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // selftest：headless 用 offscreen 平台（QApplication 需显示平台，
    // offscreen 提供窗口系统抽象；PTY 逻辑不依赖真实显示）。
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            break;
        }
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10term"));
    QApplication::setApplicationDisplayName(QStringLiteral("终端"));

    // 主题（与 compositor/w10shell 同源 [theme] 配置；终端 UI 用主题色）。
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (QApplication::arguments().contains(QStringLiteral("--selftest"))) {
        return runSelfTest();
    }

    // 单实例：既有实例在运行则激活并退出（path 忽略）。
    if (w10de::app::tryActivateExisting(QStringLiteral("Terminal"), QString())) {
        return 0;
    }
    if (!w10de::app::registerService(
            QStringLiteral("Terminal"),
            [](const QString&) {
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* tw = qobject_cast<w10de::term::TermWindow*>(w)) {
                        tw->show();
                        tw->raise();
                        tw->activateWindow();
                        break;
                    }
                }
            },
            &app)) {
        return 0;
    }

    w10de::term::TermWindow window;
    window.show();
    return QApplication::exec();
}

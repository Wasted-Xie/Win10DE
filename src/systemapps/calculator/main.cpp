// w10calc —— Win10 标准计算器（系统应用）。
//
// 通用接口（docs/SYSTEMAPPS.md）：独立二进制 + D-Bus 单实例激活
// （org.w10de.Apps.Calculator，Activate(s path)；path 忽略）。
// CLI：w10calc [--selftest]。
//
// 自测：--selftest 用 offscreen 平台创建 CalculatorWindow，模拟按键序列
// 断言运算结果（纯逻辑，无真实显示）。

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QTextStream>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/calculator/calculatorwindow.h"
#include "theme/colors.h"

namespace {

// 按键序列驱动 + 显示断言。
int assertSequence(w10de::calc::CalculatorWindow* w, QTextStream& out,
                   const char* name, const QStringList& keys,
                   const QString& expected) {
    for (const QString& k : keys) {
        if (!w10de::calc::CalculatorWindow::pressKey(w, k)) {
            out << "SELFTEST FAIL: unknown key '" << k << "' in " << name << "\n";
            return 1;
        }
    }
    // 审查 L1：用 displayText() 访问器（findChildren<QLabel> 无顺序契约）。
    const QString shown = w->displayText();
    if (shown != expected) {
        out << "SELFTEST FAIL: " << name << " got '" << shown
            << "' expected '" << expected << "'\n";
        return 1;
    }
    out << "OK " << name << " = " << shown << "\n";
    return 0;
}

int runSelfTest() {
    QTextStream out(stdout);
    w10de::calc::CalculatorWindow w;
    int rc = 0;

    // 1+2=3
    rc |= assertSequence(&w, out, "1+2",
                         {"1", "+", "2", "="}, QStringLiteral("3"));
    w10de::calc::CalculatorWindow::pressKey(&w, QStringLiteral("C"));
    // 10×10=100
    rc |= assertSequence(&w, out, "10x10",
                         {"1", "0", "×", "1", "0", "="}, QStringLiteral("100"));
    w10de::calc::CalculatorWindow::pressKey(&w, QStringLiteral("C"));
    // 5−3=2
    rc |= assertSequence(&w, out, "5-3",
                         {"5", "−", "3", "="}, QStringLiteral("2"));
    w10de::calc::CalculatorWindow::pressKey(&w, QStringLiteral("C"));
    // 100+10%=110（百分号后按 =）
    rc |= assertSequence(&w, out, "100+10%",
                         {"1", "0", "0", "+", "1", "0", "%", "="},
                         QStringLiteral("110"));
    w10de::calc::CalculatorWindow::pressKey(&w, QStringLiteral("C"));
    // 单独 10%=0.1
    rc |= assertSequence(&w, out, "10%",
                         {"1", "0", "%"}, QStringLiteral("0.1"));
    w10de::calc::CalculatorWindow::pressKey(&w, QStringLiteral("C"));
    // 7÷0 → 错误
    rc |= assertSequence(&w, out, "7/0",
                         {"7", "÷", "0", "="}, QStringLiteral("无法除以零"));
    // M3：错误态后输入新数字 = 重新开始（不再继续旧表达式 7÷2=3.5）。
    w10de::calc::CalculatorWindow::pressKey(&w, QStringLiteral("2"));
    rc |= assertSequence(&w, out, "after-error-2",
                         {"="}, QStringLiteral("2"));
    w10de::calc::CalculatorWindow::pressKey(&w, QStringLiteral("C"));
    // 小数：0.5×2=1（. 前补 0）
    rc |= assertSequence(&w, out, "0.5x2",
                         {".", "5", "×", "2", "="}, QStringLiteral("1"));
    w10de::calc::CalculatorWindow::pressKey(&w, QStringLiteral("C"));
    // 连续运算：2+3+4=9
    rc |= assertSequence(&w, out, "2+3+4",
                         {"2", "+", "3", "+", "4", "="}, QStringLiteral("9"));

    if (rc == 0) {
        out << "SELFTEST PASS (calculator logic)\n";
    }
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
    QApplication::setApplicationName(QStringLiteral("w10calc"));
    QApplication::setApplicationDisplayName(QStringLiteral("计算器"));

    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (QApplication::arguments().contains(QStringLiteral("--selftest"))) {
        return runSelfTest();
    }

    // 单实例：既有实例在运行则激活并退出（path 忽略）。
    if (w10de::app::tryActivateExisting(QStringLiteral("Calculator"), QString())) {
        return 0;
    }
    if (!w10de::app::registerService(
            QStringLiteral("Calculator"),
            [](const QString&) {
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* cw = qobject_cast<w10de::calc::CalculatorWindow*>(w)) {
                        cw->show();
                        cw->raise();
                        cw->activateWindow();
                        break;
                    }
                }
            },
            &app)) {
        return 0;
    }

    w10de::calc::CalculatorWindow window;
    window.show();
    return QApplication::exec();
}

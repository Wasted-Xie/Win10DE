// w10charmap —— 字符映射表入口（可选拓展 E4）。
//
// 用法：w10charmap / w10charmap --selftest。
// 自测：码点↔字符映射（ASCII/中文/代理区拒绝/越界）+ 区块边界。

#include <QApplication>
#include <QDir>
#include <QTextStream>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/charmap/charmapwindow.h"
#include "ipc/config.h"
#include "ipc/theme.h"
#include "theme/colors.h"

namespace {

int runSelfTest() {
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };
    using w10charmap::codepointToChar;
    using w10charmap::charToCodepoint;
    // 1) 码点 → 字符。
    if (codepointToChar(0x41) != QStringLiteral("A")) {
        return fail(QStringLiteral("U+0041 != A"));
    }
    if (codepointToChar(0x4E2D) != QStringLiteral("中")) {
        return fail(QStringLiteral("U+4E2D != 中"));
    }
    if (codepointToChar(0x1F600) != QStringLiteral("\U0001F600")) {
        return fail(QStringLiteral("U+1F600 表情符号失败"));
    }
    // 2) 代理区/越界拒绝。
    if (!codepointToChar(0xD800).isEmpty()
            || !codepointToChar(0xDFFF).isEmpty()
            || !codepointToChar(-1).isEmpty()
            || !codepointToChar(0x110000).isEmpty()) {
        return fail(QStringLiteral("代理区/越界未拒绝"));
    }
    // 3) 字符 → 码点。
    if (charToCodepoint(QStringLiteral("A")) != 0x41
            || charToCodepoint(QStringLiteral("中")) != 0x4E2D) {
        return fail(QStringLiteral("charToCodepoint 错误"));
    }
    out << "OK codepoint-map\n";
    // 4) 区块边界（每个区块 start<=end、start>=0、end<=0x10FFFF、
    //    start 与 end 同页对齐关系正确）。
    {
        const QList<w10charmap::Block>& blks = w10charmap::blocks();
        if (blks.isEmpty()) {
            return fail(QStringLiteral("区块列表为空"));
        }
        for (const w10charmap::Block& b : blks) {
            if (b.start < 0 || b.end > 0x10FFFF || b.start > b.end) {
                return fail(QStringLiteral("区块边界非法"));
            }
            // 每区块应含"中"（CJK 区块）或"A"（拉丁区块）验证可显示性。
            bool hasDisplayable = false;
            for (int cp = b.start; cp <= b.end && cp <= b.start + 0xFFFF; ++cp) {
                if (!codepointToChar(cp).isEmpty()) {
                    hasDisplayable = true;
                    break;
                }
            }
            if (!hasDisplayable) {
                return fail(QStringLiteral("区块无任何可显示字符"));
            }
        }
        out << "OK blocks (" << blks.size() << ")\n";
    }
    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            selftest = true;
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--help") == 0) {
            std::printf("Usage: w10charmap [--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10charmap: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (selftest) {
        QApplication app(argc, argv);
        return runSelfTest();
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10charmap"));
    QApplication::setApplicationDisplayName(QStringLiteral("字符映射表"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (w10de::app::tryActivateExisting(QStringLiteral("Charmap"))) {
        return 0;
    }
    w10de::app::registerService(
        QStringLiteral("Charmap"),
        [](const QString&) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* cw = qobject_cast<w10charmap::CharmapWindow*>(w)) {
                    cw->show();
                    cw->raise();
                    cw->activateWindow();
                    break;
                }
            }
        },
        &app);

    w10charmap::CharmapWindow window;
    window.show();
    return QApplication::exec();
}

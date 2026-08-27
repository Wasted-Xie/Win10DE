// w10osk —— 屏幕键盘入口（可选拓展 E8）。
//
// 用法：w10osk [--selftest] [--render <png>]。
// --selftest：布局数据自测（键覆盖度/keysym 非零/shift 层/修饰键判定）。
// --render：offscreen 渲染键盘窗口到 PNG（验证布局）。

#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QSet>
#include <QTextStream>
#include <QWidget>

#include <cstdio>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "systemapps/appipc.h"
#include "systemapps/osk/osk.h"
#include "systemapps/osk/oskwindow.h"
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
    using w10de::osk::allKeys;
    using w10de::osk::isModifier;
    using w10de::osk::layout;

    // 1) 布局结构：5 行，行键数非空。
    {
        const auto rows = layout(false);
        if (rows.size() != 5) return fail(QStringLiteral("布局行数 != 5"));
        for (const auto& row : rows) {
            if (row.isEmpty()) return fail(QStringLiteral("存在空行"));
        }
        out << "OK layout-rows\n";
    }
    // 2) 键覆盖度：26 字母 + 10 数字 + 常用标点 + 功能键，keysym 全部非零。
    {
        QSet<uint32_t> syms;
        for (const auto& k : allKeys()) {
            if (k.keysym == 0) return fail(QStringLiteral("存在 keysym=0 的键"));
            syms.insert(k.keysym);
        }
        const uint32_t expect[] = {
            XKB_KEY_a, XKB_KEY_z, XKB_KEY_0, XKB_KEY_9, XKB_KEY_space,
            XKB_KEY_BackSpace, XKB_KEY_Return, XKB_KEY_Tab, XKB_KEY_Escape,
            XKB_KEY_Left, XKB_KEY_Right, XKB_KEY_Up, XKB_KEY_Down,
            XKB_KEY_Shift_L, XKB_KEY_Shift_R, XKB_KEY_Control_L,
            XKB_KEY_Alt_L, XKB_KEY_minus, XKB_KEY_equal, XKB_KEY_bracketleft,
            XKB_KEY_bracketright, XKB_KEY_backslash, XKB_KEY_semicolon,
            XKB_KEY_apostrophe, XKB_KEY_comma, XKB_KEY_period, XKB_KEY_slash};
        for (uint32_t s : expect) {
            if (!syms.contains(s)) {
                return fail(QStringLiteral("缺少键 0x%1").arg(s, 0, 16));
            }
        }
        out << "OK key-coverage\n";
    }
    // 3) shifted 层：数字上层标点 + 字母大写 label。
    {
        const auto rows = layout(true);
        const auto base = layout(false);
        // 行 0 数字（索引 1..10）：label 应为 !@#$%^&*()。
        const QString up = QStringLiteral("!@#$%^&*()");
        for (int i = 0; i < 10; ++i) {
            if (rows[0][1 + i].label != QString(up.at(i))) {
                return fail(QStringLiteral("shifted 数字上层错误"));
            }
            // keysym 不变（小写层；Shift 由 xkb 组合）。
            if (rows[0][1 + i].keysym != base[0][1 + i].keysym) {
                return fail(QStringLiteral("shifted 不应改 keysym"));
            }
        }
        // 字母键 label 大写。
        if (rows[1][1].label != QStringLiteral("Q")) {
            return fail(QStringLiteral("shifted 字母未大写"));
        }
        if (rows[1][1].keysym != XKB_KEY_q) {
            return fail(QStringLiteral("shifted 字母 keysym 错误"));
        }
        out << "OK shift-layer\n";
    }
    // 4) isModifier 判定。
    {
        if (!isModifier(XKB_KEY_Shift_L) || !isModifier(XKB_KEY_Shift_R)
                || !isModifier(XKB_KEY_Control_L) || !isModifier(XKB_KEY_Alt_L)
                || isModifier(XKB_KEY_a) || isModifier(XKB_KEY_Return)) {
            return fail(QStringLiteral("isModifier 判定错误"));
        }
        out << "OK modifiers\n";
    }
    out << "SELFTEST PASS\n";
    return 0;
}

int runRender(const QString& pngPath) {
    using w10de::osk::OskWindow;
    OskWindow window;
    window.show();
    QApplication::processEvents();
    const QPixmap pm = window.grab();
    if (!pm.save(pngPath)) {
        std::fprintf(stderr, "render save failed\n");
        return 1;
    }
    std::printf("RENDER OK %s %dx%d rows=%d\n", qPrintable(pngPath),
                pm.width(), pm.height(), window.rowCount());
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QString renderPath;
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            selftest = true;
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--render") == 0 && i + 1 < argc) {
            renderPath = QString::fromLocal8Bit(argv[++i]);
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--help") == 0) {
            std::printf("Usage: w10osk [--selftest] [--render <png>]\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10osk: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (selftest) {
        QApplication app(argc, argv);
        return runSelfTest();
    }
    if (!renderPath.isEmpty()) {
        QApplication app(argc, argv);
        return runRender(renderPath);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10osk"));
    QApplication::setApplicationDisplayName(QStringLiteral("屏幕键盘"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (w10de::app::tryActivateExisting(QStringLiteral("Osk"))) {
        return 0;
    }
    w10de::app::registerService(
        QStringLiteral("Osk"),
        [](const QString&) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* ow = qobject_cast<w10de::osk::OskWindow*>(w)) {
                    ow->show();
                    ow->raise();
                    ow->activateWindow();
                    break;
                }
            }
        },
        &app);

    w10de::osk::OskWindow window;
    window.show();
    return QApplication::exec();
}

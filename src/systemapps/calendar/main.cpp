// w10calendar —— 日历入口（可选拓展 E11：月视图 + 事件）。
//
// 用法：w10calendar [--selftest] [--render <png>]。
// --selftest：月历网格/事件存储自测（注入临时配置，不碰真实日历）。
// --render：offscreen 渲染窗口到 PNG（验证布局）。

#include <QApplication>
#include <QDir>
#include <QSet>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTextStream>
#include <QWidget>

#include <cstdio>

#include "systemapps/appipc.h"
#include "systemapps/calendar/calendarwindow.h"
#include "systemapps/calendar/eventstore.h"
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
    using w10de::calendar::daysInMonth;
    using w10de::calendar::monthCells;

    // 1) 月历网格：42 格 + 前后月补位 + 周日起始对齐。
    {
        // 2026-08-01 是周六（可查：已知值）。周日起始 → 首格 7 月 26 日。
        const auto cells = monthCells(2026, 8);
        if (cells.size() != 42) return fail(QStringLiteral("网格不是 42 格"));
        if (cells[0].date != QDate(2026, 7, 26) || cells[0].inMonth) {
            return fail(QStringLiteral("网格首格应为上月补位 7/26"));
        }
        if (!cells[6].inMonth || cells[6].date != QDate(2026, 8, 1)) {
            return fail(QStringLiteral("8/1 应位于第一行末（周六）"));
        }
        int inMonth = 0;
        for (const auto& c : cells) {
            if (c.inMonth) ++inMonth;
        }
        if (inMonth != daysInMonth(2026, 8)) {
            return fail(QStringLiteral("当月天数不匹配"));
        }
        if (daysInMonth(2024, 2) != 29 || daysInMonth(2026, 2) != 28) {
            return fail(QStringLiteral("闰年判定错误"));
        }
        // 审查 M4：跨年补位（2026-01-01 为周四 → 首格 2025-12-28）。
        const auto jan = monthCells(2026, 1);
        if (jan[0].date != QDate(2025, 12, 28) || jan[0].inMonth) {
            return fail(QStringLiteral("跨年网格首格错误"));
        }
        if (jan[3].date != QDate(2025, 12, 31) || jan[4].date != QDate(2026, 1, 1)
                || !jan[4].inMonth) {
            return fail(QStringLiteral("跨年网格对齐错误"));
        }
        out << "OK month-cells\n";
    }

    // 2) 事件存储 CRUD + 排序 + 月份聚合（注入临时配置）。
    {
        QTemporaryDir tmp;
        if (!tmp.isValid()) return fail(QStringLiteral("临时目录创建失败"));
        const QString cfg = tmp.path() + QStringLiteral("/cal.ini");
        w10de::calendar::EventStore store(cfg);

        // 添加 3 条（乱序时间）。
        const auto e1 = store.add(QStringLiteral("2026-08-27"),
                                  QStringLiteral("14:30"),
                                  QStringLiteral("会议"), QString());
        const auto e2 = store.add(QStringLiteral("2026-08-27"), QString(),
                                  QStringLiteral("全天事项"), QStringLiteral("备注"));
        const auto e3 = store.add(QStringLiteral("2026-08-28"),
                                  QStringLiteral("09:00"),
                                  QStringLiteral("生日"), QString());
        if (e1.id == 0 || e2.id == 0 || e3.id == 0
                || e2.id != e1.id + 1 || e3.id != e2.id + 1) {
            return fail(QStringLiteral("事件 id 分配错误"));
        }
        // 同天排序：全天在前。
        const auto day = store.eventsForDate(QStringLiteral("2026-08-27"));
        if (day.size() != 2 || day[0].title != QStringLiteral("全天事项")
                || day[1].title != QStringLiteral("会议")) {
            return fail(QStringLiteral("同天事件排序错误"));
        }
        // 月份聚合。
        const auto dates = store.monthDates(2026, 8);
        if (dates != QStringList({QStringLiteral("2026-08-27"),
                                  QStringLiteral("2026-08-28")})) {
            return fail(QStringLiteral("月份聚合错误"));
        }
        // 更新。
        w10de::calendar::CalendarEvent up = e1;
        up.title = QStringLiteral("会议（改期）");
        up.time = QStringLiteral("16:00");
        if (!store.update(up)) return fail(QStringLiteral("更新失败"));
        const auto after = store.eventsForDate(QStringLiteral("2026-08-27"));
        if (after[1].title != QStringLiteral("会议（改期）")
                || after[1].time != QStringLiteral("16:00")) {
            return fail(QStringLiteral("更新未生效"));
        }
        // 删除。
        if (!store.remove(e2.id)) return fail(QStringLiteral("删除失败"));
        if (store.eventsForDate(QStringLiteral("2026-08-27")).size() != 1) {
            return fail(QStringLiteral("删除未生效"));
        }
        // 删除不存在 id。
        if (store.remove(9999)) return fail(QStringLiteral("删除不存在 id 应失败"));
        // 审查 M2：update 不存在 id 应失败。
        w10de::calendar::CalendarEvent ghost = e1;
        ghost.id = 7777;
        if (store.update(ghost)) return fail(QStringLiteral("更新不存在 id 应失败"));
        // 审查 S3：存储层拒绝空标题。
        w10de::calendar::CalendarEvent empty = e1;
        empty.title = QStringLiteral("   ");
        if (store.update(empty)) return fail(QStringLiteral("空标题应被拒绝"));
        // 审查 M4：损坏 INI 容错（读侧不崩溃）。
        {
            QFile bad(cfg);
            if (bad.open(QIODevice::WriteOnly)) {
                bad.write("\xff\xfe garbage \x00\x01");
                bad.close();
            }
            if (!store.eventsForDate(QStringLiteral("2026-08-27")).isEmpty()) {
                return fail(QStringLiteral("损坏配置读侧未容错"));
            }
        }
        out << "OK event-store\n";
    }
    out << "SELFTEST PASS\n";
    return 0;
}

int runRender(const QString& pngPath) {
    using w10de::calendar::CalendarWindow;
    CalendarWindow window;
    window.show();
    QApplication::processEvents();
    const QPixmap pm = window.grab();
    if (!pm.save(pngPath)) {
        std::fprintf(stderr, "render save failed\n");
        return 1;
    }
    std::printf("RENDER OK %s %dx%d cells=%d events=%d\n",
                qPrintable(pngPath), pm.width(), pm.height(),
                window.cellCount(), window.eventCount());
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
            std::printf("Usage: w10calendar [--selftest] [--render <png>]\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10calendar: unknown option: %s\n", argv[i]);
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
    QApplication::setApplicationName(QStringLiteral("w10calendar"));
    QApplication::setApplicationDisplayName(QStringLiteral("日历"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (w10de::app::tryActivateExisting(QStringLiteral("Calendar"))) {
        return 0;
    }
    w10de::app::registerService(
        QStringLiteral("Calendar"),
        [](const QString&) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* cw = qobject_cast<w10de::calendar::CalendarWindow*>(w)) {
                    cw->show();
                    cw->raise();
                    cw->activateWindow();
                    break;
                }
            }
        },
        &app);

    w10de::calendar::CalendarWindow window;
    window.show();
    return QApplication::exec();
}

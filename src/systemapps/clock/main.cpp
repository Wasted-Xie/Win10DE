// w10clock —— 闹钟和时钟入口（可选拓展 E5）。
//
// 用法：w10clock / w10clock --selftest。
// 自测：倒计时剩余计算、闹钟触发判定（同分钟去重）、时区城市偏移。

#include <QApplication>
#include <QDir>
#include <QTextStream>
#include <QTime>
#include <QTimeZone>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/clock/clockwindow.h"
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
    using w10clock::countdownRemainingMs;
    using w10clock::alarmDue;
    // 1) 倒计时剩余。
    if (countdownRemainingMs(2000, 1000) != 1000
            || countdownRemainingMs(1000, 2000) != -1000) {
        return fail(QStringLiteral("倒计时剩余计算错误"));
    }
    out << "OK countdown\n";
    // 2) 闹钟触发判定（同分钟去重 + 次日同分钟重触发）。
    {
        QString key;
        if (!alarmDue(7, 0, QTime(7, 0, 5), QString(), &key)) {
            return fail(QStringLiteral("到点未触发"));
        }
        if (alarmDue(7, 0, QTime(7, 0, 40), key, &key)) {
            return fail(QStringLiteral("同分钟重复触发"));
        }
        if (alarmDue(7, 0, QTime(7, 1, 0), key, &key)) {
            return fail(QStringLiteral("7:01 不应触发 7:00 闹钟"));
        }
        if (alarmDue(7, 0, QTime(8, 0, 0), key, &key)) {
            return fail(QStringLiteral("8:00 不应触发 7:00 闹钟"));
        }
        // 上次触发是"昨天"（过去日期 key，格式与 alarmDue 生成一致）→
        // 今天同分钟应重新触发（跨天重置）。
        if (!alarmDue(7, 0, QTime(7, 0, 0),
                      QStringLiteral("20200101 07:00"), &key)) {
            return fail(QStringLiteral("跨天同分钟未重新触发"));
        }
        out << "OK alarm-due\n";
    }
    // 3) 时区：北京固定 +8（无夏令时）；纽约与本地至少一个城市差异存在。
    {
        QTimeZone beijing("Asia/Shanghai");
        if (!beijing.isValid()
                || beijing.offsetFromUtc(QDateTime::currentDateTime()) != 8 * 3600) {
            return fail(QStringLiteral("北京时区偏移 != +8"));
        }
        QTimeZone ny("America/New_York");
        if (ny.isValid()
                && ny.offsetFromUtc(QDateTime::currentDateTime())
                    == beijing.offsetFromUtc(QDateTime::currentDateTime())) {
            return fail(QStringLiteral("纽约与北京偏移不应相同"));
        }
        out << "OK timezones\n";
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
            std::printf("Usage: w10clock [--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10clock: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (selftest) {
        QApplication app(argc, argv);
        return runSelfTest();
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10clock"));
    QApplication::setApplicationDisplayName(QStringLiteral("闹钟和时钟"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (w10de::app::tryActivateExisting(QStringLiteral("Clock"))) {
        return 0;
    }
    w10de::app::registerService(
        QStringLiteral("Clock"),
        [](const QString&) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* cw = qobject_cast<w10clock::ClockWindow*>(w)) {
                    cw->show();
                    cw->raise();
                    cw->activateWindow();
                    break;
                }
            }
        },
        &app);

    w10clock::ClockWindow window;
    window.show();
    return QApplication::exec();
}

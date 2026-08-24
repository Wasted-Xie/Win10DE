// w10settings —— Win10DE 设置中心（系统应用，KDE System Settings 风格）。
//
// 通用接口（docs/SYSTEMAPPS.md）：独立二进制 + D-Bus 单实例激活
// （org.w10de.Apps.Settings，Activate(s)）。
//
// 自测：w10settings --selftest <tmpdir> 验证配置写入（set/save → 读回），
// 无 GUI，供 headless 验证。

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "systemapps/appipc.h"
#include "systemapps/settings/settingswindow.h"
#include "theme/colors.h"

namespace {

// ---- 配置写入自测（headless）----
int runSelfTest(const QString& baseDir) {
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };
    if (!QDir().mkpath(baseDir)) {
        return fail(QStringLiteral("创建目录失败"));
    }
    const QString cfg = baseDir + QStringLiteral("/config.ini");

    // 预置配置（含注释与顺序，验证保存保序）。
    {
        QFile f(cfg);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写预置配置失败"));
        }
        QTextStream ts(&f);
        ts << "# Win10DE test config\n"
           << "[output]\n"
           << "width = 1920\n"
           << "height = 1080\n"
           << "\n"
           << "[theme]\n"
           << "mode = dark\n";
    }

    // 1) 修改既有键 + 追加新键。
    w10de::Config config = w10de::Config::load(cfg.toStdString());
    if (config.get("output", "width") != "1920") {
        return fail(QStringLiteral("预置读取失败"));
    }
    config.set("theme", "mode", "light");
    config.set("wallpaper", "path", "/tmp/wp.png");
    if (!config.save(cfg.toStdString())) {
        return fail(QStringLiteral("save 失败"));
    }

    // 2) 读回校验。
    const w10de::Config reread = w10de::Config::load(cfg.toStdString());
    if (reread.get("theme", "mode") != "light") {
        return fail(QStringLiteral("mode 未保存"));
    }
    if (reread.get("wallpaper", "path") != "/tmp/wp.png") {
        return fail(QStringLiteral("wallpaper 未保存"));
    }
    if (reread.get("output", "width") != "1920") {
        return fail(QStringLiteral("既有键被破坏"));
    }

    // 3) 文件内容保序（注释应保留，新 section 追加在末尾）。
    QFile f(cfg);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fail(QStringLiteral("读回文件失败"));
    }
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    if (!content.contains(QStringLiteral("# Win10DE test config")) ||
            !content.contains(QStringLiteral("mode = light")) ||
            !content.contains(QStringLiteral("[wallpaper]")) ||
            !content.contains(QStringLiteral("width = 1920"))) {
        return fail(QStringLiteral("文件内容保序/保留失败"));
    }
    out << "OK config read/write\n";
    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            break;
        }
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10settings"));
    QApplication::setApplicationDisplayName(QStringLiteral("设置"));

    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    const QStringList args = QApplication::arguments();
    const int selftestIdx = args.indexOf(QStringLiteral("--selftest"));
    if (selftestIdx >= 0) {
        const QString dir = selftestIdx + 1 < args.size()
            ? args.at(selftestIdx + 1)
            : QDir::tempPath() + QStringLiteral("/w10settings-selftest");
        return runSelfTest(dir);
    }

    // 单实例（Settings 无路径参数，Activate 仅置前）。
    if (w10de::app::tryActivateExisting(QStringLiteral("Settings"))) {
        return 0;
    }
    if (!w10de::app::registerService(
            QStringLiteral("Settings"),
            [](const QString&) {
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* sw =
                            qobject_cast<w10de::settings::SettingsWindow*>(w)) {
                        sw->show();
                        sw->raise();
                        sw->activateWindow();
                        break;
                    }
                }
            },
            &app)) {
        return 0;
    }

    w10de::settings::SettingsWindow window;
    window.show();
    return QApplication::exec();
}

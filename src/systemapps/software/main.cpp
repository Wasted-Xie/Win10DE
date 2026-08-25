// w10software —— 软件中心（已安装应用管理，Win10"应用和功能"风格）。
//
// 通用接口（docs/SYSTEMAPPS.md）：独立二进制 + D-Bus 单实例激活
// （org.w10de.Apps.Software，Activate(s path)；path 忽略）。
// CLI：w10software [--selftest]。
//
// 自测：--selftest 用 offscreen 平台扫描 .desktop 并断言：
//   - 扫描数 > 0（系统必有应用）
//   - 每个应用有名称与命令
//   - 解析器 locale 回退正常

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/software/softwarestore.h"
#include "systemapps/software/storewindow.h"
#include "theme/colors.h"

namespace {

int runSelfTest() {
    QTextStream out(stdout);
    using w10de::software::SoftwareStore;
    using w10de::software::AppInfo;

    const auto apps = SoftwareStore::listInstalled();
    out << "scanned " << apps.size() << " desktop apps\n";
    if (apps.empty()) {
        out << "SELFTEST FAIL: no desktop apps found\n";
        return 1;
    }
    int named = 0;
    int flatpak = 0;
    for (const AppInfo& a : apps) {
        if (!a.name.isEmpty() && !a.exec.isEmpty()) {
            ++named;
        }
        if (a.source == w10de::software::AppSource::Flatpak) {
            ++flatpak;
        }
    }
    if (named == 0) {
        out << "SELFTEST FAIL: no app with name+exec\n";
        return 1;
    }
    out << "SELFTEST OK: named=" << named << " flatpak=" << flatpak
        << " flatpakCli=" << (SoftwareStore::flatpakAvailable() ? "yes" : "no")
        << "\n";

    // 解析器 locale 回退单测（无 locale 后缀 → Name 生效）。
    {
        const QString testDir = QDir::tempPath() + QStringLiteral("/w10de-store-test");
        QDir().mkpath(testDir);
        const QString path = testDir + QStringLiteral("/t.desktop");
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write("[Desktop Entry]\nType=Application\nName=TestApp\n"
                    "Name[zh_CN]=\u6d4b\u8bd5\u5e94\u7528\nExec=echo hi\n"
                    "Icon=utilities-terminal\nCategories=Utility;\n");
            f.close();
            const auto entry = SoftwareStore::parseDesktopFile(path);
            if (entry.name.isEmpty() || entry.exec.isEmpty()) {
                out << "SELFTEST FAIL: parse test\n";
                return 1;
            }
            out << "OK parse-locale (name='" << entry.name << "')\n";
            QFile::remove(path);
        }
    }
    out << "SELFTEST PASS (software store)\n";
    return 0;
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
    QApplication::setApplicationName(QStringLiteral("w10software"));
    QApplication::setApplicationDisplayName(QStringLiteral("软件中心"));

    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (QApplication::arguments().contains(QStringLiteral("--selftest"))) {
        return runSelfTest();
    }

    // 单实例：既有实例在运行则激活并退出（path 忽略）。
    if (w10de::app::tryActivateExisting(QStringLiteral("Software"), QString())) {
        return 0;
    }
    if (!w10de::app::registerService(
            QStringLiteral("Software"),
            [](const QString&) {
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* sw = qobject_cast<w10de::software::StoreWindow*>(w)) {
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

    w10de::software::StoreWindow window;
    window.show();
    return QApplication::exec();
}

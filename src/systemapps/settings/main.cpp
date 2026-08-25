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

#include <cmath>  // std::isfinite/std::abs（审查：输入设置 NaN 校验）
#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/settings/audioinfo.h"
#include "systemapps/settings/defaultapps.h"
#include "systemapps/settings/powerinfo.h"
#include "systemapps/settings/settingswindow.h"
#include "ipc/inputsettings.h"
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

    // 4) 电源信息（sysfs 电池/背光；WSL 有虚拟 BAT1，真机有真实电池）。
    const w10de::settings::BatteryInfo bat = w10de::settings::PowerInfo::battery();
    if (bat.present) {
        // percent 允许 -1（无 capacity 驱动的设备——审查 L9 放宽）。
        if (bat.percent < -1 || bat.percent > 100) {
            return fail(QStringLiteral("电池电量越界 %1").arg(bat.percent));
        }
        if (bat.status.isEmpty()) {
            return fail(QStringLiteral("电池状态为空"));
        }
        out << "OK battery: " << bat.device.toUtf8() << " "
            << bat.percent << "% " << bat.status.toUtf8() << "\n";
    } else {
        out << "OK battery: none\n";
    }
    const w10de::settings::BacklightInfo bl = w10de::settings::PowerInfo::backlight();
    if (bl.present && (bl.maxBrightness <= 0 || bl.brightness < 0 ||
            bl.brightness > bl.maxBrightness)) {
        return fail(QStringLiteral("背光亮度越界"));
    }
    out << (bl.present ? "OK backlight: " : "OK backlight: none")
        << (bl.present ? bl.device.toUtf8() : QByteArray()) << "\n";

    // 5) 音量换算（pa_volume 0..0x10000 → 0-100）。
    if (w10de::settings::AudioInfo::paVolumeToPercent(0) != 0) {
        return fail(QStringLiteral("paVolumeToPercent(0) != 0"));
    }
    if (w10de::settings::AudioInfo::paVolumeToPercent(0x8000) != 50) {
        return fail(QStringLiteral("paVolumeToPercent(0x8000) != 50"));
    }
    if (w10de::settings::AudioInfo::paVolumeToPercent(0x10000) != 100) {
        return fail(QStringLiteral("paVolumeToPercent(0x10000) != 100"));
    }
    out << "OK volume-convert\n";

    // 6) 默认应用 mimeapps.list 读写（保留注释/其他段）。
    const QString mimeapps = baseDir + QStringLiteral("/mimeapps.list");
    {
        QFile f(mimeapps);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写预置 mimeapps 失败"));
        }
        QTextStream ts(&f);
        ts << "# test\n"
           << "[Added Associations]\n"
           << "text/html=old.desktop\n";
    }
    {
        auto defaults = w10de::settings::DefaultApps::loadMimeDefaults(mimeapps);
        if (!defaults.isEmpty()) {
            return fail(QStringLiteral("空段不应有内容"));
        }
        defaults.insert(QStringLiteral("x-scheme-handler/http"), QStringLiteral("firefox.desktop"));
        defaults.insert(QStringLiteral("inode/directory"), QStringLiteral("w10explorer.desktop"));
        if (!w10de::settings::DefaultApps::saveMimeDefaults(mimeapps, defaults)) {
            return fail(QStringLiteral("保存 mimeapps 失败"));
        }
        const auto reread = w10de::settings::DefaultApps::loadMimeDefaults(mimeapps);
        if (reread.value(QStringLiteral("x-scheme-handler/http"))
                != QStringLiteral("firefox.desktop")) {
            return fail(QStringLiteral("http 默认未保存"));
        }
        if (reread.value(QStringLiteral("inode/directory"))
                != QStringLiteral("w10explorer.desktop")) {
            return fail(QStringLiteral("文件管理器默认未保存"));
        }
        QFile rf(mimeapps);
        if (!rf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return fail(QStringLiteral("读回 mimeapps 失败"));
        }
        const QString content = QString::fromUtf8(rf.readAll());
        if (!content.contains(QStringLiteral("# test")) ||
                !content.contains(QStringLiteral("[Added Associations]")) ||
                !content.contains(QStringLiteral("[Default Applications]"))) {
            return fail(QStringLiteral("mimeapps 保序/保留失败"));
        }
    }
    out << "OK mimeapps-defaults\n";

    // 7) 输入设备设置 [input] 读写（load/save → 读回 + 边界钳制）。
    {
        const std::string cfgStr = cfg.toStdString();
        w10de::ipc::InputSettings s;
        s.pointerSpeed = -0.75;
        s.naturalScroll = true;
        s.leftHanded = false;
        s.tapToClick = true;
        s.repeatRate = 30;
        s.repeatDelay = 250;
        if (!w10de::ipc::InputSettings::save(cfgStr, s)) {
            return fail(QStringLiteral("输入设置保存失败"));
        }
        const w10de::ipc::InputSettings r =
            w10de::ipc::InputSettings::load(cfgStr);
        // 审查 L：double 用容差比较（精确 == 对 -0.75 等二进制表示脆弱）。
        if (std::abs(r.pointerSpeed - (-0.75)) > 1e-9 || !r.naturalScroll ||
                r.leftHanded || !r.tapToClick || r.repeatRate != 30 ||
                r.repeatDelay != 250) {
            return fail(QStringLiteral("输入设置读回不一致"));
        }
        // 默认值：全新配置应回退内置默认（25/600，速度 0）。
        const w10de::ipc::InputSettings d =
            w10de::ipc::InputSettings::load(
                (baseDir + QStringLiteral("/empty-config.ini")).toStdString());
        if (d.pointerSpeed != 0.0 || d.naturalScroll || d.leftHanded ||
                d.tapToClick || d.repeatRate != 25 || d.repeatDelay != 600) {
            return fail(QStringLiteral("输入设置默认值不符"));
        }
        // 越界钳制：speed > 1 / < -1 应钳到边界（审查 L：补下限）。
        {
            w10de::ipc::InputSettings over = s;
            over.pointerSpeed = 5.0;
            const QString overPath = baseDir + QStringLiteral("/over.ini");
            if (!w10de::ipc::InputSettings::save(
                    overPath.toStdString(), over)) {
                return fail(QStringLiteral("越界配置保存失败"));
            }
            const auto clamped =
                w10de::ipc::InputSettings::load(overPath.toStdString());
            if (clamped.pointerSpeed != 1.0) {
                return fail(QStringLiteral("speed 上界未钳制"));
            }
            w10de::ipc::InputSettings under = s;
            under.pointerSpeed = -5.0;
            const QString underPath = baseDir + QStringLiteral("/under.ini");
            if (!w10de::ipc::InputSettings::save(
                    underPath.toStdString(), under)) {
                return fail(QStringLiteral("下界配置保存失败"));
            }
            const auto clampedUnder =
                w10de::ipc::InputSettings::load(underPath.toStdString());
            if (clampedUnder.pointerSpeed != -1.0) {
                return fail(QStringLiteral("speed 下界未钳制"));
            }
        }
        // 审查 M/L：非数字与 NaN 应回退 0.0（NaN 比较恒 false 会穿透钳制）。
        {
            const QString nanPath = baseDir + QStringLiteral("/nan.ini");
            {
                QFile f(nanPath);
                if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    return fail(QStringLiteral("写 NaN 配置失败"));
                }
                QTextStream ts(&f);
                ts << "[input]\npointer_speed = nan\n";
            }
            const auto nanLoaded =
                w10de::ipc::InputSettings::load(nanPath.toStdString());
            if (nanLoaded.pointerSpeed != 0.0 ||
                    !std::isfinite(nanLoaded.pointerSpeed)) {
                return fail(QStringLiteral("NaN 未回退 0.0"));
            }
            const QString junkPath = baseDir + QStringLiteral("/junk.ini");
            {
                QFile f(junkPath);
                if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    return fail(QStringLiteral("写垃圾配置失败"));
                }
                QTextStream ts(&f);
                ts << "[input]\npointer_speed = abc\n";
            }
            const auto junkLoaded =
                w10de::ipc::InputSettings::load(junkPath.toStdString());
            if (junkLoaded.pointerSpeed != 0.0) {
                return fail(QStringLiteral("非数字未回退 0.0"));
            }
        }
    }
    out << "OK input-settings\n";
    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    // 日志即时可见（重定向到文件时 stderr 全缓冲，kill 丢日志——w10term
    // 调试教训同款）。
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
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
    // --page <name>：启动即切到指定分类页（headless 渲染验证用）。
    const int pageIdx = args.indexOf(QStringLiteral("--page"));
    if (pageIdx >= 0 && pageIdx + 1 < args.size()) {
        window.selectCategory(args.at(pageIdx + 1));
    }
    return QApplication::exec();
}

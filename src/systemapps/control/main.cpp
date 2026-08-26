// w10control —— Win10DE 控制面板（系统应用，docs/WIN10-GAP.md G1）。
//
// 与 w10settings 区别（Win10 语义）：设置 = 现代分类导航 + 搜索 + 即时
// 应用；控制面板 = 传统"按类别"图标网格 + 模态对话框（应用/确定/取消）。
// 两者共享同一后端（config.ini + org.w10de.Compositor D-Bus + info 类），
// 可更改全部功能。
//
// 自测：w10control --selftest <tmpdir> 验证 Config remove/sectionKeys 与
// 主页/对话框构建（offscreen，不 exec），供 headless 验证。

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/control/categorydialogs.h"
#include "systemapps/control/controlwindow.h"
#include "ipc/config.h"
#include "ipc/shortcuts.h"
#include "ipc/windowrules.h"
#include "theme/colors.h"

namespace {

// ---- 自测（headless）----
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

    // 1) Config::remove + sectionKeys（G1 窗口规则删除/枚举依赖）。
    {
        QFile f(cfg);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写预置配置失败"));
        }
        QTextStream ts(&f);
        ts << "[theme]\n"
           << "mode = dark\n"
           << "\n"
           << "[window_rules]\n"
           << "term_ontop = app_id=w10term;always_on_top\n"
           << "calc_ws2 = app_id=w10calc;workspace=1\n"
           << "keep = title=Keep;borderless\n";
    }
    {
        w10de::Config config = w10de::Config::load(cfg.toStdString());
        const auto keys = config.sectionKeys("window_rules");
        if (keys.size() != 3) {
            return fail(QStringLiteral("sectionKeys 数量 != 3（%1）")
                .arg(static_cast<int>(keys.size())));
        }
        bool foundTerm = false, foundCalc = false;
        for (const std::string& k : keys) {
            if (k == "term_ontop") foundTerm = true;
            if (k == "calc_ws2") foundCalc = true;
        }
        if (!foundTerm || !foundCalc) {
            return fail(QStringLiteral("sectionKeys 内容不符"));
        }
        config.remove("window_rules", "term_ontop");
        config.set("window_rules", "new_rule",
                   "app_id=w10calc;always_on_top|workspace=2");
        if (!config.save(cfg.toStdString())) {
            return fail(QStringLiteral("remove 后 save 失败"));
        }
        const w10de::Config reread = w10de::Config::load(cfg.toStdString());
        if (reread.sectionKeys("window_rules").size() != 3) {
            return fail(QStringLiteral("remove 未生效（应 3 键）"));
        }
        if (reread.get("window_rules", "term_ontop") != "") {
            return fail(QStringLiteral("被删键仍可读"));
        }
        if (reread.get("window_rules", "new_rule") == "") {
            return fail(QStringLiteral("新增键未保存"));
        }
        if (reread.get("theme", "mode") != "dark") {
            return fail(QStringLiteral("其他段被破坏"));
        }
        // 文件级校验：被删行不再出现。
        QFile rf(cfg);
        if (!rf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return fail(QStringLiteral("读回文件失败"));
        }
        const QString content = QString::fromUtf8(rf.readAll());
        if (content.contains(QStringLiteral("term_ontop"))) {
            return fail(QStringLiteral("被删行仍留在文件"));
        }
        if (!content.contains(QStringLiteral("new_rule"))) {
            return fail(QStringLiteral("新键行缺失"));
        }
    }
    out << "OK config-remove-sectionKeys\n";

    // 2) 窗口规则解析含 name（G1 编辑/删除定位）。
    {
        const auto rules = w10de::ipc::loadWindowRules(cfg.toStdString());
        if (rules.size() != 3) {
            return fail(QStringLiteral("规则数 != 3"));
        }
        bool foundNew = false;
        for (const w10de::ipc::WindowRule& r : rules) {
            if (r.name == "new_rule") {
                foundNew = true;
                if (!r.alwaysOnTop || r.workspace != 2) {
                    return fail(QStringLiteral("new_rule 动作解析不符"));
                }
            }
        }
        if (!foundNew) {
            return fail(QStringLiteral("规则名未填入"));
        }
    }
    out << "OK windowrule-name\n";

    // 2b) saveRules 序列化格式往返（app_id&title 组合 / | 动作 /
    //     geometry 逗号——与 parseRuleLine 解析器兼容性）。
    {
        w10de::Config config = w10de::Config::load(cfg.toStdString());
        config.set("window_rules", "geo_rule",
                   "app_id=w10calc&title=Calc;always_on_top|geometry=100,50,800,600");
        config.set("window_rules", "title_only",
                   "title=Notes;borderless|workspace=2");
        if (!config.save(cfg.toStdString())) {
            return fail(QStringLiteral("序列化往返：save 失败"));
        }
        const auto rules = w10de::ipc::loadWindowRules(cfg.toStdString());
        bool okGeo = false, okTitle = false;
        for (const w10de::ipc::WindowRule& r : rules) {
            if (r.name == "geo_rule") {
                okGeo = r.matchAppId == "w10calc" && r.matchTitle == "Calc"
                    && r.alwaysOnTop && r.hasGeometry
                    && r.geomX == 100 && r.geomY == 50
                    && r.geomW == 800 && r.geomH == 600;
            }
            if (r.name == "title_only") {
                okTitle = r.matchTitle == "Notes" && r.borderless
                    && r.workspace == 2;
            }
        }
        if (!okGeo || !okTitle) {
            return fail(QStringLiteral("规则序列化往返不一致"));
        }
    }
    out << "OK rule-serialize-roundtrip\n";

    // 3) 快捷键文本解析（G1 编辑校验复用）。
    {
        const w10de::ShortcutBinding b = w10de::parseShortcut("ctrl+shift+a");
        if (!b.valid()) {
            return fail(QStringLiteral("ctrl+shift+a 解析失败"));
        }
        if (w10de::parseShortcut("junk").valid()) {
            return fail(QStringLiteral("非法绑定未拒绝"));
        }
    }
    out << "OK shortcut-parse\n";

    // 4) 主页与各对话框构建（offscreen；不 exec）。
    {
        w10de::control::ControlWindow window;
        window.show();
        QList<QDialog*> dialogs;
        dialogs << new w10de::control::SystemSecurityDialog(&window);
        dialogs << new w10de::control::AppearanceDialog(&window);
        dialogs << new w10de::control::HardwareSoundDialog(&window);
        dialogs << new w10de::control::NetworkDialog(&window);
        dialogs << new w10de::control::ProgramsDialog(&window);
        dialogs << new w10de::control::ClockRegionDialog(&window);
        dialogs << new w10de::control::ShortcutsDialog(&window);
        dialogs << new w10de::control::WindowRulesDialog(&window);
        for (QDialog* d : dialogs) {
            d->show();  // 触发布局与 paint（offscreen 有效）
        }
        // 显式触发显示页刷新（compositor 未运行 → 显示"不可用"即可）。
        if (auto* hw = qobject_cast<w10de::control::HardwareSoundDialog*>(
                dialogs.at(2))) {
            hw->refreshOutputs();
        }
        for (QDialog* d : dialogs) {
            delete d;
        }
    }
    out << "OK dialogs-build\n";
    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    // 日志即时可见（重定向到文件时 stderr 全缓冲，kill 丢日志）。
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            break;
        }
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10control"));
    QApplication::setApplicationDisplayName(QStringLiteral("控制面板"));

    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    const QStringList args = QApplication::arguments();
    const int selftestIdx = args.indexOf(QStringLiteral("--selftest"));
    if (selftestIdx >= 0) {
        const QString dir = selftestIdx + 1 < args.size()
            ? args.at(selftestIdx + 1)
            : QDir::tempPath() + QStringLiteral("/w10control-selftest");
        return runSelfTest(dir);
    }

    // 单实例（Control 无路径参数，Activate 仅置前）。
    if (w10de::app::tryActivateExisting(QStringLiteral("Control"))) {
        return 0;
    }
    if (!w10de::app::registerService(
            QStringLiteral("Control"),
            [](const QString&) {
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* cw =
                            qobject_cast<w10de::control::ControlWindow*>(w)) {
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

    w10de::control::ControlWindow window;
    window.show();
    return QApplication::exec();
}

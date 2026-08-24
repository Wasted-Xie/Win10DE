// w10explorer —— Win10 风格文件资源管理器（系统应用）。
//
// 通用接口（docs/SYSTEMAPPS.md）：独立二进制 + D-Bus 单实例激活
// （org.w10de.Apps.Explorer，Activate(s path)）；CLI：w10explorer [path]。
//
// 自测：w10explorer --selftest <tmpdir> 在临时目录执行文件操作序列
// （复制/剪切/粘贴/重命名/新建/回收站），无 GUI，供 headless 验证。

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QtDebug>

#include "systemapps/appipc.h"
#include "systemapps/explorer/explorerwindow.h"
#include "systemapps/explorer/fileops.h"
#include "theme/colors.h"

namespace {

using namespace w10de::explorer;

// ---- 文件操作自测（headless）----
int runSelfTest(const QString& baseDir) {
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };

    // 准备：a.txt、dir/sub.txt
    QDir base(baseDir);
    if (!base.exists()) {
        QDir().mkpath(baseDir);
    }
    QFile a(baseDir + QStringLiteral("/a.txt"));
    if (!a.open(QIODevice::WriteOnly)) {
        return fail(QStringLiteral("创建 a.txt 失败"));
    }
    a.write("hello");
    a.close();
    QDir().mkpath(baseDir + QStringLiteral("/dir"));
    QFile s(baseDir + QStringLiteral("/dir/sub.txt"));
    if (!s.open(QIODevice::WriteOnly)) {
        return fail(QStringLiteral("创建 sub.txt 失败"));
    }
    s.write("sub");
    s.close();

    // 1) 复制粘贴：a.txt → copy
    FileOps::setClipboard({baseDir + QStringLiteral("/a.txt")}, PasteMode::Copy);
    PasteResult r = FileOps::pasteTo(baseDir + QStringLiteral("/copy"));
    if (r.ok != 1 || !QFileInfo::exists(baseDir + QStringLiteral("/copy/a.txt"))) {
        return fail(QStringLiteral("复制粘贴失败 ok=%1").arg(r.ok));
    }
    out << "OK copy-paste\n";

    // 2) 剪切粘贴（移动）：copy/a.txt → moved/a.txt
    QDir().mkpath(baseDir + QStringLiteral("/moved"));
    FileOps::setClipboard({baseDir + QStringLiteral("/copy/a.txt")}, PasteMode::Move);
    r = FileOps::pasteTo(baseDir + QStringLiteral("/moved"));
    if (r.ok != 1 || !QFileInfo::exists(baseDir + QStringLiteral("/moved/a.txt")) ||
            QFileInfo::exists(baseDir + QStringLiteral("/copy/a.txt"))) {
        return fail(QStringLiteral("剪切粘贴失败 ok=%1").arg(r.ok));
    }
    out << "OK move-paste\n";

    // 3) 冲突命名：moved/a.txt 再复制 → a (2).txt
    FileOps::setClipboard({baseDir + QStringLiteral("/moved/a.txt")}, PasteMode::Copy);
    r = FileOps::pasteTo(baseDir + QStringLiteral("/moved"));
    if (r.ok != 1 ||
            !QFileInfo::exists(baseDir + QStringLiteral("/moved/a (2).txt"))) {
        return fail(QStringLiteral("冲突命名失败 ok=%1").arg(r.ok));
    }
    out << "OK conflict-rename\n";

    // 4) 递归复制目录：dir → dir (2)（冲突命名，Windows 语义）
    FileOps::setClipboard({baseDir + QStringLiteral("/dir")}, PasteMode::Copy);
    r = FileOps::pasteTo(baseDir);
    if (r.ok != 1 ||
            !QFileInfo::exists(baseDir + QStringLiteral("/dir (2)/sub.txt"))) {
        return fail(QStringLiteral("目录递归复制失败 ok=%1").arg(r.ok));
    }
    out << "OK dir-copy\n";

    // 5) 重命名：a.txt → renamed.txt
    const QString ren = FileOps::rename(baseDir + QStringLiteral("/a.txt"),
                                        QStringLiteral("renamed.txt"));
    if (!ren.isEmpty() || !QFileInfo::exists(baseDir + QStringLiteral("/renamed.txt"))) {
        return fail(QStringLiteral("重命名失败: %1").arg(ren));
    }
    out << "OK rename\n";

    // 6) 新建文件夹
    const QString nf = FileOps::makeDir(baseDir);
    if (nf.isEmpty() || !QFileInfo::exists(nf)) {
        return fail(QStringLiteral("新建文件夹失败"));
    }
    out << "OK new-folder\n";

    // 7) 移到回收站：renamed.txt → Trash/files
    if (!FileOps::moveToTrash(baseDir + QStringLiteral("/renamed.txt"))) {
        return fail(QStringLiteral("回收站失败"));
    }
    const QString trash =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + QStringLiteral("/.local/share/Trash/files");
    if (QFileInfo::exists(baseDir + QStringLiteral("/renamed.txt")) ||
            !QFileInfo::exists(trash + QStringLiteral("/renamed.txt"))) {
        return fail(QStringLiteral("回收站文件缺失"));
    }
    out << "OK trash\n";

    // 8) 大小合计：moved/ 含 a.txt + a (2).txt（各 5 字节）= 10
    if (FileOps::totalSize({baseDir + QStringLiteral("/moved")}) != 10) {
        return fail(QStringLiteral("大小合计错误"));
    }
    out << "OK total-size\n";

    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    // selftest：headless/无显示环境用 offscreen 平台（剪贴板/窗口可用）。
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            break;
        }
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10explorer"));
    QApplication::setApplicationDisplayName(QStringLiteral("文件资源管理器"));

    // 主题（与 compositor/w10shell 同源 [theme] 配置）。
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    const QStringList args = QApplication::arguments();
    const int selftestIdx = args.indexOf(QStringLiteral("--selftest"));
    if (selftestIdx >= 0) {
        const QString dir = selftestIdx + 1 < args.size()
            ? args.at(selftestIdx + 1)
            : QDir::tempPath() + QStringLiteral("/w10explorer-selftest");
        return runSelfTest(dir);
    }

    const QString startPath = args.size() > 1 ? args.at(1) : QString();

    // 单实例：既有实例在运行则激活并退出。
    if (w10de::app::tryActivateExisting(QStringLiteral("Explorer"), startPath)) {
        return 0;
    }
    if (!w10de::app::registerService(
            QStringLiteral("Explorer"),
            [](const QString& path) {
                // 激活回调：置前既有窗口并导航（经 qApp 全局找窗口）。
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* ew = qobject_cast<w10de::explorer::ExplorerWindow*>(w)) {
                        if (!path.isEmpty()) {
                            ew->navigateTo(path);
                        }
                        ew->show();
                        ew->raise();
                        ew->activateWindow();
                        break;
                    }
                }
            },
            &app)) {
        // 服务注册失败（可能竞态）：既有实例已接管。
        return 0;
    }

    w10de::explorer::ExplorerWindow window;
    window.show();
    return QApplication::exec();
}

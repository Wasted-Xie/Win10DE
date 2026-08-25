// w10trash —— 回收站窗口（系统应用，KDE-GAP 中优先 #3）。
//
// 通用接口（docs/SYSTEMAPPS.md）：独立二进制 + D-Bus 单实例激活
// （org.w10de.Apps.Trash，Activate 置前）。
//
// 自测：w10trash --selftest <tmpdir> —— 在临时目录模拟完整回收站：
// 列表解析（名称/Path 解码/DeletionDate）→ 恢复（含目标冲突改名）→
// 彻底删除 → 清空，无 GUI，供 headless 验证。

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QUrl>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/trash/trashstore.h"
#include "systemapps/trash/trashwindow.h"
#include "theme/colors.h"

namespace {

int runSelfTest(const QString& baseDir) {
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };
    // 审查 L10：幂等——上次运行残留的临时目录先清理（避免中断后
    // 残留破坏本次断言）。
    QDir(baseDir).removeRecursively();
    // 临时回收站目录：<base>/Trash/{files,info}
    const QString trash = baseDir + QStringLiteral("/Trash");
    const QString files = trash + QStringLiteral("/files");
    const QString info = trash + QStringLiteral("/info");
    if (!QDir().mkpath(files) || !QDir().mkpath(info)) {
        return fail(QStringLiteral("创建临时回收站失败"));
    }
    // 预置：被删文件 a.txt（原位置 <base>/orig/a.txt）+ info。
    const QString origDir = baseDir + QStringLiteral("/orig");
    if (!QDir().mkpath(origDir)) {
        return fail(QStringLiteral("创建原目录失败"));
    }
    {
        QFile f(origDir + QStringLiteral("/a.txt"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写原文件失败"));
        }
        f.write("hello trash");
        f.close();
    }
    {
        QFile f(files + QStringLiteral("/a.txt"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写回收站文件失败"));
        }
        f.write("hello trash");
        f.close();
    }
    {
        // 中文路径 + 空格验证百分号编码/解码往返。
        const QString enc = QString::fromUtf8(QUrl::toPercentEncoding(
            origDir + QStringLiteral("/a.txt")));
        QFile f(info + QStringLiteral("/a.txt.trashinfo"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写 trashinfo 失败"));
        }
        f.write("[Trash Info]\n");
        f.write("Path=" + enc.toUtf8() + "\n");
        f.write("DeletionDate=2026-08-25T10:30:00\n");
        f.close();
    }
    // 预置第二个条目：b.log（无对应文件？不——files 有 b.log 但 info 缺失，
    // 验证 info 缺失条目的容错）。
    {
        QFile f(files + QStringLiteral("/b.log"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写 b.log 失败"));
        }
        f.write("orphan");
        f.close();
    }

    w10de::trash::TrashStore store(trash);

    // 1) 列表解析。
    {
        const auto entries = store.list();
        if (entries.size() != 2) {
            return fail(QStringLiteral("列表项数 != 2（实际 %1）")
                .arg(entries.size()));
        }
        bool foundA = false, foundB = false;
        for (const auto& e : entries) {
            if (e.name == QStringLiteral("a.txt")) {
                foundA = true;
                if (e.originalPath != origDir + QStringLiteral("/a.txt")) {
                    return fail(QStringLiteral("Path 解码错误：%1")
                        .arg(e.originalPath));
                }
                if (!e.deletionDate.isValid() ||
                        e.deletionDate.toString(Qt::ISODate)
                            != QStringLiteral("2026-08-25T10:30:00")) {
                    return fail(QStringLiteral("DeletionDate 解析错误"));
                }
            } else if (e.name == QStringLiteral("b.log")) {
                foundB = true;
                if (!e.originalPath.isEmpty()) {
                    return fail(QStringLiteral("info 缺失条目不应有 Path"));
                }
            }
        }
        if (!foundA || !foundB) {
            return fail(QStringLiteral("列表缺条目"));
        }
    }
    out << "OK list\n";

    // 2) 恢复（目标冲突 → 改名 (1)）。
    {
        // 先在原位置放一个同名文件制造冲突。
        QFile f(origDir + QStringLiteral("/a.txt"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写冲突文件失败"));
        }
        f.write("conflict");
        f.close();

        w10de::trash::TrashEntry e;
        e.name = QStringLiteral("a.txt");
        if (!store.restore(e)) {
            return fail(QStringLiteral("恢复失败"));
        }
        // 恢复目标应为 a(1).txt（原 a.txt 已被占）。
        QFileInfo restored(origDir + QStringLiteral("/a(1).txt"));
        if (!restored.exists()) {
            return fail(QStringLiteral("冲突改名恢复失败"));
        }
        // info 应已删除（条目不再属于回收站）。
        if (QFile::exists(info + QStringLiteral("/a.txt.trashinfo"))) {
            return fail(QStringLiteral("恢复后 trashinfo 未删除"));
        }
        // 回收站 files 里应只剩 b.log。
        const auto entries = store.list();
        if (entries.size() != 1 || entries.first().name
                != QStringLiteral("b.log")) {
            return fail(QStringLiteral("恢复后列表不符"));
        }
    }
    out << "OK restore-conflict\n";

    // 3) 彻底删除 b.log；顺带验证 restore 失败路径（b.log 无 info，
    //    不可恢复——审查 M-6）。
    {
        w10de::trash::TrashEntry e;
        e.name = QStringLiteral("b.log");
        if (store.restore(e)) {
            return fail(QStringLiteral("无 info 条目不应可恢复"));
        }
        if (!store.permanentDelete(e)) {
            return fail(QStringLiteral("彻底删除失败"));
        }
        if (!store.list().isEmpty()) {
            return fail(QStringLiteral("彻底删除后列表非空"));
        }
    }
    out << "OK permanent-delete\n";

    // 3.5) symlink 回归（审查 S1-1）：彻底删除 symlink-to-dir 只删链接，
    //      不递归删除链接目标内容。
    {
        const QString target = baseDir + QStringLiteral("/symlink-target");
        QDir().mkpath(target);
        {
            QFile f(target + QStringLiteral("/keep.txt"));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                return fail(QStringLiteral("写 symlink 目标失败"));
            }
            f.write("keep me");
        }
        if (!QFile::link(target, files + QStringLiteral("/linkdir"))) {
            return fail(QStringLiteral("创建 symlink 失败"));
        }
        // 无 info 条目：permanentDelete 只删链接本身。
        w10de::trash::TrashEntry e;
        e.name = QStringLiteral("linkdir");
        if (!store.permanentDelete(e)) {
            return fail(QStringLiteral("symlink 条目彻底删除失败"));
        }
        if (QFileInfo::exists(files + QStringLiteral("/linkdir"))) {
            return fail(QStringLiteral("symlink 未删除"));
        }
        if (!QFileInfo::exists(target + QStringLiteral("/keep.txt"))) {
            return fail(QStringLiteral("symlink 目标内容被误删（数据损坏）"));
        }
    }
    out << "OK symlink-safe\n";

    // 3.6) 相对 Path 拒绝恢复（审查 S1-2）：Path 非绝对路径必须失败。
    {
        QFile f(files + QStringLiteral("/rel.txt"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写相对路径条目失败"));
        }
        f.write("rel");
        f.close();
        {
            QFile fi(info + QStringLiteral("/rel.txt.trashinfo"));
            if (!fi.open(QIODevice::WriteOnly | QIODevice::Text)) {
                return fail(QStringLiteral("写相对路径 info 失败"));
            }
            fi.write("[Trash Info]\nPath=relative/path.txt\n");
            fi.close();
        }
        w10de::trash::TrashEntry e;
        e.name = QStringLiteral("rel.txt");
        if (store.restore(e)) {
            return fail(QStringLiteral("相对 Path 不应被恢复"));
        }
        // 条目应仍在回收站（未被移动）。
        if (!QFileInfo::exists(files + QStringLiteral("/rel.txt"))) {
            return fail(QStringLiteral("相对 Path 恢复后条目丢失"));
        }
        // 清理。
        if (!store.permanentDelete(e)) {
            return fail(QStringLiteral("清理相对 Path 条目失败"));
        }
    }
    out << "OK relative-path-reject\n";

    // 4) 清空（预置 2 项 + 孤儿 info 后 empty；审查 M-6：孤儿
    //    .trashinfo 无对应 files 条目，empty 应一并清理）。
    {
        for (const QString& n : {QStringLiteral("c.txt"),
                                 QStringLiteral("d.txt")}) {
            QFile f(files + QLatin1Char('/') + n);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                return fail(QStringLiteral("预置清空测试项失败"));
            }
            f.write("x");
        }
        {
            QFile f(info + QStringLiteral("/orphan.txt.trashinfo"));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                return fail(QStringLiteral("写孤儿 info 失败"));
            }
            f.write("[Trash Info]\nPath=/tmp/orphan.txt\n");
            f.close();
        }
        if (!store.empty()) {
            return fail(QStringLiteral("清空失败"));
        }
        const auto entries = store.list();
        if (!entries.isEmpty()) {
            return fail(QStringLiteral("清空后仍有条目"));
        }
        if (QDir(files).entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                .size() != 0) {
            return fail(QStringLiteral("清空后 files 目录残留"));
        }
        if (QDir(info).entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                .size() != 0) {
            return fail(QStringLiteral("清空后 info 目录残留（孤儿未清理）"));
        }
    }
    out << "OK empty\n";
    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            break;
        }
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10trash"));
    QApplication::setApplicationDisplayName(QStringLiteral("回收站"));

    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    const QStringList args = QApplication::arguments();
    const int selftestIdx = args.indexOf(QStringLiteral("--selftest"));
    if (selftestIdx >= 0) {
        const QString dir = selftestIdx + 1 < args.size()
            ? args.at(selftestIdx + 1)
            : QDir::tempPath() + QStringLiteral("/w10trash-selftest");
        return runSelfTest(dir);
    }

    if (w10de::app::tryActivateExisting(QStringLiteral("Trash"))) {
        return 0;
    }
    // 审查 M-5：无 session bus（headless/精简环境）时 registerService
    // 返回 false——不应静默退出，降级为普通运行（tryActivateExisting
    // 已拦截既有实例，降级仅失去单实例约束）。
    if (!w10de::app::registerService(
            QStringLiteral("Trash"),
            [](const QString&) {
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* tw =
                            qobject_cast<w10de::trash::TrashWindow*>(w)) {
                        tw->show();
                        tw->raise();
                        tw->activateWindow();
                        break;
                    }
                }
            },
            &app)) {
        qWarning("w10trash: D-Bus 单实例服务注册失败，降级为普通运行");
    }

    w10de::trash::TrashWindow window;
    window.show();
    return QApplication::exec();
}

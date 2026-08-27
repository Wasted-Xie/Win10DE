// w10cleanup —— 磁盘清理入口（可选拓展 E7）。
//
// 用法：w10cleanup [--selftest] [--render <png>]。
// --selftest：扫描/清理/大小格式化自测（临时目录注入，不碰真实文件）。
// --render：offscreen 渲染窗口到 PNG（验证布局）。

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QTextStream>
#include <QTemporaryDir>
#include <QWidget>

#include <cstdio>

#include <algorithm>

#include "systemapps/appipc.h"
#include "systemapps/cleanup/cleanupwindow.h"
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
    using w10de::cleanup::CleanupItem;
    using w10de::cleanup::CleanupScanner;
    using w10de::cleanup::dirSize;
    using w10de::cleanup::formatSize;

    // 1) formatSize 边界。
    if (formatSize(0) != "0 B" || formatSize(1023) != "1023 B"
            || formatSize(1024) != "1 KB" || formatSize(1536) != "2 KB"
            || formatSize(1048576) != "1.0 MB"
            || formatSize(1536LL * 1024 * 1024) != "1.5 GB") {
        return fail(QStringLiteral("formatSize 错误"));
    }
    out << "OK format-size\n";

    // 2) dirSize：临时目录树（文件 + 子目录 + 空目录 + 不存在的路径）。
    {
        QTemporaryDir tmp;
        if (!tmp.isValid()) return fail(QStringLiteral("临时目录创建失败"));
        const QString base = tmp.path();
        QFile f(base + QStringLiteral("/a.bin"));
        if (!f.open(QIODevice::WriteOnly)) return fail(QStringLiteral("写文件失败"));
        f.write(QByteArray(100, 'x'));
        f.close();
        QDir().mkpath(base + QStringLiteral("/sub"));
        QFile f2(base + QStringLiteral("/sub/b.bin"));
        if (!f2.open(QIODevice::WriteOnly)) return fail(QStringLiteral("写子文件失败"));
        f2.write(QByteArray(50, 'y'));
        f2.close();
        if (dirSize(base) != 150) {
            return fail(QStringLiteral("dirSize 统计错误：%1")
                            .arg(dirSize(base)));
        }
        if (dirSize(base + QStringLiteral("/nope")) != -1) {
            return fail(QStringLiteral("不存在路径应返回 -1"));
        }
        // 单文件。
        if (dirSize(base + QStringLiteral("/a.bin")) != 100) {
            return fail(QStringLiteral("单文件大小错误"));
        }
        out << "OK dir-size\n";
    }

    // 3) 注入 home/trash 的类别扫描 + 清理（不碰真实目录）。
    {
        QTemporaryDir tmp;
        if (!tmp.isValid()) return fail(QStringLiteral("临时目录创建失败"));
        const QString home = tmp.path();
        QDir().mkpath(home + QStringLiteral("/.cache/app1"));
        QDir().mkpath(home + QStringLiteral("/.cache/thumbnails"));
        QFile f1(home + QStringLiteral("/.cache/app1/data.bin"));
        if (!f1.open(QIODevice::WriteOnly)) return fail(QStringLiteral("写缓存失败"));
        f1.write(QByteArray(1000, 'a'));
        f1.close();
        QFile f2(home + QStringLiteral("/.cache/thumbnails/t.png"));
        if (!f2.open(QIODevice::WriteOnly)) return fail(QStringLiteral("写缩略图失败"));
        f2.write(QByteArray(500, 'b'));
        f2.close();
        const QString trash = home + QStringLiteral("/Trash");
        QDir().mkpath(trash + QStringLiteral("/files"));
        QDir().mkpath(trash + QStringLiteral("/info"));
        QFile f3(trash + QStringLiteral("/files/gone.txt"));
        if (!f3.open(QIODevice::WriteOnly)) return fail(QStringLiteral("写回收站失败"));
        f3.write(QByteArray(200, 'c'));
        f3.close();
        QFile f4(trash + QStringLiteral("/info/gone.txt.trashinfo"));
        if (!f4.open(QIODevice::WriteOnly)) return fail(QStringLiteral("写 info 失败"));
        f4.write("[Trash Info]\nPath=/tmp/gone.txt\nDeletionDate=2026-01-01T00:00:00\n");
        f4.close();

        CleanupScanner scanner(home, trash);
        const auto items = scanner.scan();
        if (items.size() != 4) return fail(QStringLiteral("类别数量错误"));
        // 回收站。
        const auto trashIt = std::find_if(
            items.begin(), items.end(), [](const auto& i) {
                return i.id == QStringLiteral("trash"); });
        if (trashIt == items.end() || trashIt->sizeBytes != 200) {
            return fail(QStringLiteral("回收站大小错误"));
        }
        // 用户缓存（1000 + 500 缩略图）。
        const auto cacheIt = std::find_if(
            items.begin(), items.end(), [](const auto& i) {
                return i.id == QStringLiteral("usercache"); });
        if (cacheIt == items.end() || cacheIt->sizeBytes != 1500) {
            return fail(QStringLiteral("用户缓存大小错误"));
        }
        // 缩略图单独条目。
        const auto thumbsIt = std::find_if(
            items.begin(), items.end(), [](const auto& i) {
                return i.id == QStringLiteral("thumbnails"); });
        if (thumbsIt == items.end() || thumbsIt->sizeBytes != 500) {
            return fail(QStringLiteral("缩略图大小错误"));
        }
        // /tmp 不可清理。
        const auto tmpIt = std::find_if(
            items.begin(), items.end(), [](const auto& i) {
                return i.id == QStringLiteral("tmp"); });
        if (tmpIt == items.end() || tmpIt->cleanable) {
            return fail(QStringLiteral("临时文件类别错误"));
        }
        out << "OK scan\n";

        // 清理回收站。
        if (scanner.clean(*trashIt) != 200) {
            return fail(QStringLiteral("回收站清理释放错误"));
        }
        if (dirSize(trash + QStringLiteral("/files")) != 0) {
            return fail(QStringLiteral("回收站清理未清空"));
        }
        out << "OK clean-trash\n";
        // 清理用户缓存（含缩略图；目录本身保留）。
        if (scanner.clean(*cacheIt) != 1500) {
            return fail(QStringLiteral("用户缓存清理释放错误"));
        }
        if (!QDir(home + QStringLiteral("/.cache")).exists()) {
            return fail(QStringLiteral("缓存目录本身不应被删除"));
        }
        if (dirSize(home + QStringLiteral("/.cache")) != 0) {
            return fail(QStringLiteral("缓存内容未清空"));
        }
        out << "OK clean-cache\n";
    }

    // 4) symlink 安全（审查 S2/L1/L10）：dirSize 不跟随；清理只删链接本身，
    //    绝不递归删除链接目标内容。
    {
        QTemporaryDir tmp;
        if (!tmp.isValid()) return fail(QStringLiteral("临时目录创建失败"));
        const QString base = tmp.path();
        // "用户文件"（链接目标，绝不能被误删）。
        QDir().mkpath(base + QStringLiteral("/victim"));
        QFile fv(base + QStringLiteral("/victim/keep.txt"));
        if (!fv.open(QIODevice::WriteOnly)) return fail(QStringLiteral("写目标失败"));
        fv.write(QByteArray(300, 'k'));
        fv.close();
        // 缓存目录 + 内嵌 symlink（目录与文件各一）。
        QDir().mkpath(base + QStringLiteral("/home/.cache/app"));
        QFile fc(base + QStringLiteral("/home/.cache/app/data.bin"));
        if (!fc.open(QIODevice::WriteOnly)) return fail(QStringLiteral("写缓存失败"));
        fc.write(QByteArray(100, 'c'));
        fc.close();
        if (!QFile::link(base + QStringLiteral("/victim"),
                         base + QStringLiteral("/home/.cache/app/linkdir"))
                || !QFile::link(base + QStringLiteral("/victim/keep.txt"),
                                base + QStringLiteral("/home/.cache/app/linkfile"))) {
            return fail(QStringLiteral("创建 symlink 失败"));
        }
        // dirSize：不跟随 symlink（目录与文件都返回 0）。
        if (dirSize(base + QStringLiteral("/home/.cache/app/linkdir")) != 0
                || dirSize(base + QStringLiteral("/home/.cache/app/linkfile")) != 0) {
            return fail(QStringLiteral("dirSize 跟随了 symlink"));
        }
        if (dirSize(base + QStringLiteral("/home/.cache")) != 100) {
            return fail(QStringLiteral("symlink 大小被计入"));
        }
        // 清理缓存：链接目标内容必须保留。
        CleanupScanner scanner2(base + QStringLiteral("/home"),
                                base + QStringLiteral("/Trash"));
        CleanupItem ci;
        ci.id = QStringLiteral("usercache");
        if (scanner2.clean(ci) != 100) {
            return fail(QStringLiteral("清理释放错误（含 symlink）"));
        }
        if (!QFile::exists(base + QStringLiteral("/victim/keep.txt"))) {
            return fail(QStringLiteral("symlink 目标被误删"));
        }
        if (dirSize(base + QStringLiteral("/victim")) != 300) {
            return fail(QStringLiteral("链接目标内容受损"));
        }
        if (!QDir(base + QStringLiteral("/home/.cache")).exists()) {
            return fail(QStringLiteral("缓存目录本身不应被删除"));
        }
        out << "OK symlink-safe\n";
    }

    out << "SELFTEST PASS\n";
    return 0;
}

int runRender(const QString& pngPath) {
    using w10de::cleanup::CleanupWindow;
    CleanupWindow window;
    window.show();
    QApplication::processEvents();
    const QPixmap pm = window.grab();
    if (!pm.save(pngPath)) {
        std::fprintf(stderr, "render save failed\n");
        return 1;
    }
    std::printf("RENDER OK %s %dx%d items=%d selected=%lld\n",
                qPrintable(pngPath), pm.width(), pm.height(),
                window.itemCount(),
                static_cast<long long>(window.selectedBytes()));
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
            std::printf("Usage: w10cleanup [--selftest] [--render <png>]\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10cleanup: unknown option: %s\n", argv[i]);
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
    QApplication::setApplicationName(QStringLiteral("w10cleanup"));
    QApplication::setApplicationDisplayName(QStringLiteral("磁盘清理"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (w10de::app::tryActivateExisting(QStringLiteral("Cleanup"))) {
        return 0;
    }
    w10de::app::registerService(
        QStringLiteral("Cleanup"),
        [](const QString&) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* cw = qobject_cast<w10de::cleanup::CleanupWindow*>(w)) {
                    cw->show();
                    cw->raise();
                    cw->activateWindow();
                    break;
                }
            }
        },
        &app);

    w10de::cleanup::CleanupWindow window;
    window.show();
    return QApplication::exec();
}

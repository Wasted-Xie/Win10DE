#include "systemapps/explorer/fileops.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QStandardPaths>
#include <QUrl>

#include <functional>

namespace w10de::explorer {

namespace {

// 内部剪贴板模式标记（QClipboard 的 uri-list 不携带 copy/cut 语义）。
PasteMode g_mode = PasteMode::Copy;

QString homeTrashDir() {
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + QStringLiteral("/.local/share/Trash");
}

// 生成回收站内不冲突的文件名（同名加 (n)）。
QString uniqueTrashName(const QString& dir, const QString& base) {
    const QFileInfo info(base);
    const QString stem = info.completeBaseName();
    const QString ext = info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();
    if (!QFileInfo::exists(dir + QLatin1Char('/') + base)) {
        return base;
    }
    for (int n = 2; n < 100000; ++n) {
        const QString candidate =
            QStringLiteral("%1 (%2)%3").arg(stem).arg(n).arg(ext);
        if (!QFileInfo::exists(dir + QLatin1Char('/') + candidate)) {
            return candidate;
        }
    }
    return base;  // 极端情况退回原值（覆盖风险可忽略）
}

}  // namespace

void FileOps::setClipboard(const QStringList& paths, PasteMode mode) {
    auto* mime = new QMimeData();
    QList<QUrl> urls;
    for (const QString& p : paths) {
        urls.append(QUrl::fromLocalFile(p));
    }
    mime->setUrls(urls);
    QGuiApplication::clipboard()->setMimeData(mime);
    g_mode = mode;
}

QStringList FileOps::clipboardPaths() {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    QStringList out;
    if (mime == nullptr || !mime->hasUrls()) {
        return out;
    }
    for (const QUrl& u : mime->urls()) {
        if (u.isLocalFile()) {
            out.append(u.toLocalFile());
        }
    }
    return out;
}

PasteMode FileOps::clipboardMode() { return g_mode; }

void FileOps::clearClipboard() {
    QGuiApplication::clipboard()->clear();
    g_mode = PasteMode::Copy;
}

QString FileOps::uniqueName(const QString& dir, const QString& base) {
    if (!QFileInfo::exists(dir + QLatin1Char('/') + base)) {
        return base;
    }
    const QFileInfo info(base);
    const QString stem = info.completeBaseName();
    const QString ext = info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();
    for (int n = 2; n < 100000; ++n) {
        const QString candidate =
            QStringLiteral("%1 (%2)%3").arg(stem).arg(n).arg(ext);
        if (!QFileInfo::exists(dir + QLatin1Char('/') + candidate)) {
            return candidate;
        }
    }
    return base;
}

bool FileOps::copyRecursive(const QString& src, const QString& dst) {
    const QFileInfo si(src);
    if (si.isDir()) {
        if (!QDir().mkpath(dst)) {
            return false;
        }
        const QDir d(src);
        bool all = true;
        for (const QFileInfo& e : d.entryInfoList(
                 QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
            if (!copyRecursive(e.absoluteFilePath(),
                               dst + QLatin1Char('/') + e.fileName())) {
                all = false;
            }
        }
        return all;
    }
    return QFile::copy(src, dst);
}

PasteResult FileOps::pasteTo(const QString& targetDir) {
    PasteResult result;
    if (!QDir().mkpath(targetDir)) {
        result.firstError = targetDir;
        result.skipped = -1;  // 目标目录不可创建
        return result;
    }
    const QStringList srcs = clipboardPaths();
    if (srcs.isEmpty()) {
        return result;
    }
    const bool move = g_mode == PasteMode::Move;
    for (const QString& src : srcs) {
        const QFileInfo si(src);
        if (!si.exists()) {
            ++result.skipped;
            continue;
        }
        const QString dst = uniqueName(targetDir, si.fileName());
        const QString dstPath = targetDir + QLatin1Char('/') + dst;
        bool ok = false;
        if (move) {
            // 移动：优先 rename（同文件系统原子）；跨设备回退 copy+remove。
            ok = QFile::rename(src, dstPath);
            if (!ok) {
                ok = copyRecursive(src, dstPath) && QFile::remove(src);
            }
        } else {
            ok = copyRecursive(src, dstPath);
        }
        if (ok) {
            ++result.ok;
        } else {
            ++result.skipped;
            if (result.firstError.isEmpty()) {
                result.firstError = src;
            }
        }
    }
    if (move) {
        clearClipboard();  // Windows：剪切粘贴完成后剪贴板内容清除
    }
    return result;
}

bool FileOps::moveToTrash(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        return false;
    }
    const QString trash = homeTrashDir();
    const QString filesDir = trash + QStringLiteral("/files");
    const QString infoDir = trash + QStringLiteral("/info");
    if (!QDir().mkpath(filesDir) || !QDir().mkpath(infoDir)) {
        return false;
    }
    const QString name = uniqueTrashName(filesDir, info.fileName());
    const QString dstFile = filesDir + QLatin1Char('/') + name;
    if (!QFile::rename(path, dstFile)) {
        return false;  // 跨设备等场景：不上报成功
    }
    // info/<name>.trashinfo：Path（百分号编码的绝对路径）+ DeletionDate。
    QFile f(infoDir + QLatin1Char('/') + name + QStringLiteral(".trashinfo"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray enc = QUrl::toPercentEncoding(info.absoluteFilePath());
    f.write("[Trash Info]\n");
    f.write("Path=" + enc + "\n");
    f.write("DeletionDate=" + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8() + "\n");
    f.close();
    return true;
}

QString FileOps::rename(const QString& path, const QString& newName) {
    if (newName.isEmpty() || newName.contains(QLatin1Char('/'))) {
        return QStringLiteral("无效名称");
    }
    const QFileInfo info(path);
    const QString dst = info.absolutePath() + QLatin1Char('/') + newName;
    if (dst == path) {
        return QString();
    }
    if (QFileInfo::exists(dst)) {
        return QStringLiteral("目标已存在");
    }
    return QFile::rename(path, dst) ? QString() : QStringLiteral("重命名失败");
}

QString FileOps::makeDir(const QString& dir) {
    QString name = QStringLiteral("新建文件夹");
    int n = 2;
    while (QFileInfo::exists(dir + QLatin1Char('/') + name)) {
        name = QStringLiteral("新建文件夹 (%1)").arg(n++);
    }
    return QDir().mkpath(dir + QLatin1Char('/') + name)
        ? dir + QLatin1Char('/') + name
        : QString();
}

qint64 FileOps::totalSize(const QStringList& paths) {
    qint64 total = 0;
    std::function<void(const QString&)> walk = [&](const QString& p) {
        const QFileInfo fi(p);
        if (fi.isDir()) {
            for (const QFileInfo& e : QDir(p).entryInfoList(
                     QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
                walk(e.absoluteFilePath());
            }
        } else if (fi.isFile()) {
            total += fi.size();
        }
    };
    for (const QString& p : paths) {
        walk(p);
    }
    return total;
}

}  // namespace w10de::explorer

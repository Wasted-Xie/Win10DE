// CleanupScanner 实现（可选拓展 E7 磁盘清理）。
//
// 统计用 QDirIterator 递归（不跟随 symlink——避免循环与跨设备扫爆）；
// 清理删白名单路径内容（保留目录本身），回收站走 TrashStore::empty()。

#include "systemapps/cleanup/cleanupscanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtDebug>

#include "systemapps/trash/trashstore.h"

namespace w10de::cleanup {

namespace {

// 目录子项数（顶层，含目录；QDir::System 使 broken symlink 也被计入——
// 审查 L7）。
qint64 entryCount(const QString& path) {
    QDir d(path);
    if (!d.exists()) return 0;
    return static_cast<qint64>(d.entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System).size());
}

// 不跟随符号链接的递归删除（审查 S2：Qt 6.11 的 QDir::removeRecursively
// 对 symlink-to-dir 会递归删除链接目标内容——嵌套链接可能越出缓存白名单
// 误删用户文件；此实现任何层级只删链接本身）。
bool removeTreeNoFollow(const QString& path) {
    bool ok = true;
    const QFileInfoList es = QDir(path).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System);
    for (const QFileInfo& fi : es) {
        if (fi.isSymLink()) {
            if (!QFile::remove(fi.absoluteFilePath())) ok = false;
        } else if (fi.isDir()) {
            if (!removeTreeNoFollow(fi.absoluteFilePath())) ok = false;
        } else {
            if (!QFile::remove(fi.absoluteFilePath())) ok = false;
        }
    }
    return ok;
}

// 删除目录全部内容（保留目录本身）。返回是否全部成功。
bool clearDirContents(const QString& path) {
    QDir d(path);
    if (!d.exists()) return true;
    bool ok = true;
    const QFileInfoList entries = d.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System);
    for (const QFileInfo& fi : entries) {
        if (fi.isDir() && !fi.isSymLink()) {
            if (!removeTreeNoFollow(fi.absoluteFilePath())) ok = false;
        } else {
            if (!QFile::remove(fi.absoluteFilePath())) ok = false;
        }
    }
    return ok;
}

}  // namespace

qint64 dirSize(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) return -1;
    // 审查 L1：symlink 判定先于 isFile/isDir（symlink-to-file 的 isFile()
    // 为真会"跟随"返回目标大小，与"不跟随"契约矛盾）。
    if (fi.isSymLink()) return 0;
    if (fi.isFile()) return fi.size();
    if (!fi.isDir()) return -1;
    qint64 total = 0;
    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.isSymLink()) continue;  // 跳过 symlink
        if (fi.isDir()) continue;      // 目录项大小不计入
        const qint64 s = fi.size();
        if (s > 0) total += s;
    }
    return total;
}

QString formatSize(qint64 bytes) {
    if (bytes < 0) return QStringLiteral("-");
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1.0) return QStringLiteral("%1 B").arg(bytes);
    const double mb = kb / 1024.0;
    if (mb < 1.0) return QStringLiteral("%1 KB").arg(kb, 0, 'f', 0);
    const double gb = mb / 1024.0;
    if (gb < 1.0) return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(gb, 0, 'f', 1);
}

CleanupScanner::CleanupScanner(const QString& homeOverride,
                               const QString& trashDirOverride) {
    homeDir_ = homeOverride.isEmpty() ? QDir::homePath() : homeOverride;
    cacheDir_ = homeDir_ + QStringLiteral("/.cache");
    if (!trashDirOverride.isEmpty()) {
        trashDir_ = trashDirOverride;
    } else {
        trashDir_ = QStandardPaths::writableLocation(
                        QStandardPaths::GenericDataLocation)
                    + QStringLiteral("/Trash");
    }
}

QList<CleanupItem> CleanupScanner::scan() {
    QList<CleanupItem> items;

    // 1) 回收站。
    {
        CleanupItem it;
        it.id = QStringLiteral("trash");
        it.label = QStringLiteral("回收站");
        const QString files = trashDir_ + QStringLiteral("/files");
        it.path = files;  // 清理/释放统计基于 files 内容
        const qint64 sz = dirSize(files);
        it.exists = sz >= 0;
        it.sizeBytes = sz > 0 ? sz : 0;
        it.detail = it.exists
            ? QStringLiteral("回收站中的 %1 个已删除项目（%2）")
                  .arg(entryCount(files)).arg(formatSize(it.sizeBytes))
            : QStringLiteral("回收站为空");
        items.append(it);
    }
    // 2) 用户缓存 ~/.cache（细分 top 5 子目录）。
    // 审查 M1：单遍 QDirIterator 同时累计总大小与各顶层子目录大小
    //（避免每子目录一遍递归的 O(N²) 全量遍历）。
    {
        CleanupItem it;
        it.id = QStringLiteral("usercache");
        it.label = QStringLiteral("用户缓存");
        it.path = cacheDir_;
        // top 5 大子目录细分（单遍遍历按顶层子目录归集，同时累计总大小——
        // 审查 M1：避免 O(N²) 全量遍历）。
        struct Sub { QString name; qint64 size = 0; };
        QList<Sub> subs;
        qint64 totalBytes = 0;
        QDir d(cacheDir_);
        if (d.exists()) {
            QHash<QString, qint64> subSizes;
            QDirIterator it2(cacheDir_, QDir::AllEntries | QDir::NoDotAndDotDot
                                           | QDir::System,
                             QDirIterator::Subdirectories);
            while (it2.hasNext()) {
                it2.next();
                const QFileInfo fi = it2.fileInfo();
                if (fi.isSymLink() || fi.isDir()) continue;
                const qint64 s = fi.size();
                if (s <= 0) continue;
                totalBytes += s;
                const QString rel = it2.filePath().mid(cacheDir_.length() + 1);
                const QString top = rel.section(QLatin1Char('/'), 0, 0);
                if (!top.isEmpty()) subSizes[top] += s;
            }
            for (auto it3 = subSizes.constBegin(); it3 != subSizes.constEnd();
                 ++it3) {
                subs.append({it3.key(), it3.value()});
            }
            std::sort(subs.begin(), subs.end(),
                      [](const Sub& a, const Sub& b) { return a.size > b.size; });
        }
        it.exists = d.exists();
        it.sizeBytes = totalBytes;
        QStringList parts;
        for (int i = 0; i < qMin(5, subs.size()); ++i) {
            parts << QStringLiteral("%1 %2").arg(subs[i].name,
                                                 formatSize(subs[i].size));
        }
        it.detail = it.exists
            ? (parts.isEmpty()
                   ? QStringLiteral("缓存为空")
                   : QStringLiteral("应用缓存 %1：%2")
                         .arg(formatSize(it.sizeBytes), parts.join(QStringLiteral("，"))))
            : QStringLiteral("无缓存目录");
        items.append(it);
    }
    // 3) 缩略图缓存。
    {
        const QString thumbs = cacheDir_ + QStringLiteral("/thumbnails");
        CleanupItem it;
        it.id = QStringLiteral("thumbnails");
        it.label = QStringLiteral("缩略图缓存");
        it.path = thumbs;
        const qint64 sz = dirSize(thumbs);
        it.exists = sz >= 0;
        it.sizeBytes = sz > 0 ? sz : 0;
        it.detail = it.exists
            ? QStringLiteral("文件管理器缩略图（%1）").arg(formatSize(it.sizeBytes))
            : QStringLiteral("无缩略图缓存");
        items.append(it);
    }
    // 4) 临时文件 /tmp（仅显示，不可清理）。
    {
        CleanupItem it;
        it.id = QStringLiteral("tmp");
        it.label = QStringLiteral("临时文件");
        it.path = QStringLiteral("/tmp");
        const qint64 sz = dirSize(QStringLiteral("/tmp"));
        it.exists = sz >= 0;
        it.sizeBytes = sz > 0 ? sz : 0;
        it.cleanable = false;
        it.detail = QStringLiteral("系统临时目录（%1，由系统管理）")
                        .arg(formatSize(it.sizeBytes));
        items.append(it);
    }
    return items;
}

qint64 CleanupScanner::clean(const CleanupItem& item) {
    // 审查 M2：白名单在 API 内强制执行——目标路径按 id 重建，忽略调用方
    // 传入的 item.path（防任意目录内容删除）。
    QString target;
    if (item.id == QStringLiteral("trash")) {
        target = trashDir_ + QStringLiteral("/files");
    } else if (item.id == QStringLiteral("usercache")) {
        target = cacheDir_;
    } else if (item.id == QStringLiteral("thumbnails")) {
        target = cacheDir_ + QStringLiteral("/thumbnails");
    } else {
        lastError_ = QStringLiteral("该项目不可清理");
        return -1;
    }
    // 兜底校验（防御性：id 与路径映射必须落在白名单）。
    if (target != trashDir_ + QStringLiteral("/files")
            && target != cacheDir_
            && target != cacheDir_ + QStringLiteral("/thumbnails")) {
        lastError_ = QStringLiteral("清理目标不在白名单");
        return -1;
    }
    const qint64 beforeSize = dirSize(target);
    const qint64 before = beforeSize > 0 ? beforeSize : 0;
    bool ok = true;
    if (item.id == QStringLiteral("trash")) {
        // 复用 TrashStore（freedesktop Trash：files + info 全清）。
        w10de::trash::TrashStore store(trashDir_);
        ok = store.empty();
        if (!ok) lastError_ = store.lastError().isEmpty()
            ? QStringLiteral("清空回收站失败") : store.lastError();
    } else {
        ok = clearDirContents(target);
        if (!ok) lastError_ = QStringLiteral("删除缓存内容失败：%1")
                                  .arg(target);
    }
    if (!ok) return -1;
    // 审查 L2：after 单次计算（两次遍历间文件系统变化会不一致）。
    const qint64 afterSize = dirSize(target);
    const qint64 after = afterSize > 0 ? afterSize : 0;
    return qMax<qint64>(0, before - after);
}

}  // namespace w10de::cleanup

// trashstore.cpp —— 回收站数据访问实现。

#include "systemapps/trash/trashstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

#include <algorithm>   // std::sort
#include <cerrno>      // errno（审查 T2：EXDEV 等跨设备错误透传）
#include <cstring>     // std::strerror
#include <filesystem>  // broken symlink 枚举（审查 T1）
#include <utility>     // std::move

namespace w10de::trash {

namespace {

// 审查 M-2：条目名必须是纯名称（files/ 条目名天然不含 '/'；
// 防御外部调用方传入 ".."/"a/b" 造成路径穿越）。
bool isSafeEntryName(const QString& name) {
    return !name.isEmpty()
        && name != QStringLiteral(".")
        && name != QStringLiteral("..")
        && !name.contains(QLatin1Char('/'))
        && !name.contains(QLatin1Char('\\'));
}

// 目标路径已存在时生成不冲突名：base(1).ext、base(2).ext…
QString uniqueRestoreName(const QString& targetPath) {
    if (!QFileInfo::exists(targetPath)) {
        return targetPath;
    }
    const QFileInfo info(targetPath);
    const QString dir = info.absolutePath();
    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix();
    // 审查 L1：上限与 w10explorer uniqueTrashName 一致（100000）。
    for (int i = 1; i < 100000; ++i) {
        const QString candidate = suffix.isEmpty()
            ? QStringLiteral("%1/%2(%3)").arg(dir, stem).arg(i)
            : QStringLiteral("%1/%2(%3).%4").arg(dir, stem).arg(i).arg(suffix);
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return targetPath;  // 极端冲突（>99999）：让 rename 失败由调用方处理
}

}  // namespace

TrashStore::TrashStore(const QString& trashDirOverride) {
    if (!trashDirOverride.isEmpty()) {
        trashDir_ = trashDirOverride;
    } else {
        trashDir_ = QStandardPaths::writableLocation(
                        QStandardPaths::GenericDataLocation)
                    + QStringLiteral("/Trash");
    }
}

TrashEntry TrashStore::readInfo(const QString& name) const {
    TrashEntry e;
    e.name = name;
    QFile f(infoDir() + QLatin1Char('/') + name + QStringLiteral(".trashinfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return e;  // info 缺失：仅名称可用
    }
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        if (line.startsWith(QStringLiteral("Path="))) {
            e.originalPath = QUrl::fromPercentEncoding(
                line.mid(5).toUtf8());
        } else if (line.startsWith(QStringLiteral("DeletionDate="))) {
            // "DeletionDate=" 前缀 13 字符。
            e.deletionDate = QDateTime::fromString(line.mid(13), Qt::ISODate);
        }
    }
    return e;
}

QList<TrashEntry> TrashStore::list() const {
    QList<TrashEntry> result;
    // 审查 T1：QDir::entryList 无法枚举 broken symlink（Qt 实测）——
    // 改用 std::filesystem::directory_iterator（可列出链接本身）。
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it(fs::path(filesDir().toStdString()), ec);
    const fs::directory_iterator end;
    for (; it != end && !ec; it.increment(ec)) {
        const QString name = QString::fromStdString(
            it->path().filename().string());
        if (name.isEmpty() || name == QStringLiteral(".") ||
                name == QStringLiteral("..")) {
            continue;
        }
        result.append(readInfo(name));
    }
    // 审查 M-3/L7：不混用排序键——无删除时间的条目视为最早统一排最后；
    // 相同时间用名称（stable_sort 保持稳定）。
    std::stable_sort(result.begin(), result.end(),
              [](const TrashEntry& a, const TrashEntry& b) {
                  const bool va = a.deletionDate.isValid();
                  const bool vb = b.deletionDate.isValid();
                  if (va != vb) {
                      return va;  // 有时间的排前面
                  }
                  if (va) {
                      return a.deletionDate > b.deletionDate;
                  }
                  return a.name > b.name;
              });
    return result;
}

bool TrashStore::restore(const TrashEntry& entry) {
    lastError_.clear();
    if (!isSafeEntryName(entry.name)) {
        lastError_ = QStringLiteral("无效条目名");
        return false;
    }
    const QString src = filesDir() + QLatin1Char('/') + entry.name;
    if (!QFileInfo::exists(src)) {
        lastError_ = QStringLiteral("回收站内条目不存在");
        return false;
    }
    // 原始路径以 info/<name>.trashinfo 为准（调用方只需传 name）。
    const TrashEntry full = readInfo(entry.name);
    // 审查 S1-2：spec 要求 Path 为绝对路径。cleanPath 规范化 "../"；
    // 相对路径会被解析到进程 cwd 导致恢复错位或任意写入，一律拒绝。
    const QString dst = QDir::cleanPath(full.originalPath);
    if (dst.isEmpty() || !QFileInfo(dst).isAbsolute()) {
        lastError_ = QStringLiteral("缺少原始路径信息（info 损坏）");
        return false;
    }
    // 父目录不存在则创建（原始父目录可能已被删除）。
    if (!QDir().mkpath(QFileInfo(dst).absolutePath())) {
        lastError_ = QStringLiteral("无法创建目标目录");
        return false;
    }
    const QString target = uniqueRestoreName(dst);
    if (!QFile::rename(src, target)) {
        // 审查 T2：透传 errno 原因（跨设备 EXDEV 等）。
        lastError_ = QStringLiteral("移动失败：%1").arg(
            QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    // 成功恢复后删除 info（条目不再属于回收站）。
    QFile::remove(infoDir() + QLatin1Char('/') + entry.name
                  + QStringLiteral(".trashinfo"));
    return true;
}

bool TrashStore::permanentDelete(const TrashEntry& entry) {
    if (!isSafeEntryName(entry.name)) {
        return false;
    }
    const QString path = filesDir() + QLatin1Char('/') + entry.name;
    const QFileInfo info(path);
    // broken symlink 的 exists() 为 false，但链接本身可删。
    if (!info.exists() && !info.isSymLink()) {
        return false;
    }
    bool ok = false;
    if (info.isSymLink()) {
        // 审查 S1-1：只删链接本身。QDir::removeRecursively 对
        // symlink-to-dir 会递归删除链接目标目录的全部内容
        // （Qt 6.11 实测），绝不可走 isDir 分支。
        ok = QFile::remove(path);
    } else if (info.isDir()) {
        ok = QDir(path).removeRecursively();
    } else {
        ok = QFile::remove(path);
    }
    if (ok) {
        QFile::remove(infoDir() + QLatin1Char('/') + entry.name
                      + QStringLiteral(".trashinfo"));
    }
    return ok;
}

bool TrashStore::empty() {
    // 审查 T1：同 list()——filesystem 枚举（含 broken symlink）。
    namespace fs = std::filesystem;
    bool allOk = true;
    std::error_code ec;
    fs::directory_iterator it(fs::path(filesDir().toStdString()), ec);
    const fs::directory_iterator end;
    for (; it != end && !ec; it.increment(ec)) {
        const QString name = QString::fromStdString(
            it->path().filename().string());
        if (name.isEmpty() || name == QStringLiteral(".") ||
                name == QStringLiteral("..")) {
            continue;
        }
        TrashEntry e;
        e.name = name;
        if (!permanentDelete(e)) {
            allOk = false;
        }
    }
    // 清理孤儿 info（files 缺失的 .trashinfo，list 不展示但残留占空间）。
    // 审查 M-1：broken symlink 无法被 entryList 枚举且 QFileInfo::exists
    // 为 false——孤儿判断必须补 isSymLink，否则会误删仍有效条目（含
    // broken symlink 条目）的 trashinfo，使其不可恢复。
    const QDir files(filesDir());
    const QDir infoDir_(infoDir());
    const QStringList infoNames = infoDir_.entryList(
        QStringList() << QStringLiteral("*.trashinfo"),
        QDir::Files | QDir::NoDotAndDotDot);
    for (const QString& name : infoNames) {
        const QString stem = name.left(name.size() - 10);
        if (!files.exists(stem)
                && !QFileInfo(filesDir() + QLatin1Char('/') + stem)
                       .isSymLink()) {
            QFile::remove(infoDir() + QLatin1Char('/') + name);
        }
    }
    return allOk;
}

}  // namespace w10de::trash

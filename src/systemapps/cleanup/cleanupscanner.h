// CleanupScanner —— 磁盘清理扫描/执行（可选拓展 E7 磁盘清理）。
//
// 类别：回收站（freedesktop Trash，复用 trash/trashstore.cpp 的 TrashStore）、
// 用户缓存 ~/.cache（顶层子目录递归统计 + 清理内容）、缩略图缓存
// ~/.cache/thumbnails、临时文件 /tmp（仅显示不可清理——系统管理）。
// 安全：清理只删白名单路径（回收站 files 内容 / ~/.cache 下子项），
// 保留目录本身；home 与 trash 目录可注入（selftest 隔离）。
#pragma once

#include <QList>
#include <QString>

namespace w10de::cleanup {

// 目录递归大小（字节；不跟随 symlink；路径不存在或失败返回 -1）。
qint64 dirSize(const QString& path);

// 人类可读大小（B/KB/MB/GB，Win10 风格四舍五入一位小数）。
QString formatSize(qint64 bytes);

struct CleanupItem {
    QString id;           // "trash" / "usercache" / "thumbnails" / "tmp"
    QString label;        // 显示名
    QString detail;       // 说明（含细分信息）
    QString path;         // 目标路径（回收站为 trash 目录）
    qint64 sizeBytes = 0;
    bool cleanable = true;  // false = 仅显示（/tmp）
    bool exists = false;    // 路径是否存在
};

class CleanupScanner {
public:
    // homeOverride/trashDirOverride 非空时注入（selftest 隔离测试）。
    explicit CleanupScanner(const QString& homeOverride = QString(),
                            const QString& trashDirOverride = QString());

    // 同步扫描全部类别。
    QList<CleanupItem> scan();

    // 清理指定类别；返回实际释放字节数（失败返回 -1 并设置 lastError）。
    qint64 clean(const CleanupItem& item);

    QString lastError() const { return lastError_; }

    QString homeDir() const { return homeDir_; }
    QString trashDir() const { return trashDir_; }
    QString cacheDir() const { return cacheDir_; }

private:
    QString homeDir_;     // 实际 home（注入或真实）
    QString trashDir_;    // 回收站目录
    QString cacheDir_;    // ~/.cache
    QString lastError_;
};

}  // namespace w10de::cleanup

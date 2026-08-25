// trashstore —— 回收站数据访问（freedesktop Trash spec）。
//
// KDE-GAP 中优先 #3：回收站窗口的数据层，纯逻辑无 UI（selftest 可测）。
// 布局：<XDG_DATA_HOME>/Trash/files/<name>（被删文件/目录）
//       <XDG_DATA_HOME>/Trash/info/<name>.trashinfo（元数据）
//         [Trash Info]
//         Path=<百分号编码的原始绝对路径>
//         DeletionDate=<ISO 8601>
// w10explorer 的 FileOps::moveToTrash 按同一格式写入（互操作）。
// 已知简化（文档记录）：仅主回收站（~/.local/share/Trash）；系统分区
// 顶层回收站（/.Trash-<uid>）与跨设备删除不在 MVP 范围。
// symlink 条目：permanentDelete 只删链接本身（不递归目标，审查 S1-1）；
// broken symlink 无法被目录枚举列出（列表不可见，empty 可清其 info）。

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace w10de::trash {

struct TrashEntry {
    QString name;           // files/ 下的条目名（含扩展名）
    QString originalPath;   // 解码后的原始绝对路径
    QDateTime deletionDate; // 删除时间（trashinfo 无该行时为无效时间）
};

class TrashStore {
public:
    // trashDirOverride：selftest 注入临时回收站目录；空 = 主回收站。
    explicit TrashStore(const QString& trashDirOverride = QString());

    // 列出回收站全部条目（files/ 项 + 对应 .trashinfo；info 缺失时
    // 仅名称可用，originalPath 为空）。
    QList<TrashEntry> list() const;

    // 恢复单条：移回 originalPath（父目录不存在则创建；目标已存在则
    // 自动改名 base(1).ext…）。成功删除对应 .trashinfo 并返回 true。
    bool restore(const TrashEntry& entry);

    // 彻底删除单条（files + info）。
    bool permanentDelete(const TrashEntry& entry);

    // 清空回收站（全部 files + info）。返回是否全部成功。
    bool empty();

    QString trashDir() const { return trashDir_; }

private:
    QString filesDir() const { return trashDir_ + QStringLiteral("/files"); }
    QString infoDir() const { return trashDir_ + QStringLiteral("/info"); }
    // 解析 info/<name>.trashinfo → 条目（名称缺失/损坏时返回空字段）。
    TrashEntry readInfo(const QString& name) const;

    QString trashDir_;
};

}  // namespace w10de::trash

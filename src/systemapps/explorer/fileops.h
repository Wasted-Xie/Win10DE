// 文件操作（对标 Windows 文件资源管理器核心语义）。
//
// 纯逻辑类（无 Qt GUI 依赖），供 ExplorerWindow 与自测（--selftest）共用：
//   复制/剪切/粘贴（含冲突命名）、删除到回收站（freedesktop trash spec）、
//   重命名、新建文件夹、选中项大小合计。
#pragma once

#include <QString>
#include <QStringList>

class QUrl;

namespace w10de::explorer {

// 粘贴模式（Windows 语义：剪切粘贴 = 移动，复制粘贴 = 复制）。
enum class PasteMode { Copy, Move };

// 一次粘贴操作的结果统计。
struct PasteResult {
    int ok = 0;        // 成功项数
    int skipped = 0;   // 因冲突/失败跳过项数
    QString firstError;  // 首个错误信息（空 = 全部成功）
};

class FileOps {
public:
    // 把路径列表放入剪贴板（QMimeData text/uri-list + 内部模式标记）。
    // mode 记录是复制还是剪切；剪切粘贴 = 移动。
    static void setClipboard(const QStringList& paths, PasteMode mode);
    // 读取剪贴板文件列表（空 = 剪贴板无文件）。
    static QStringList clipboardPaths();
    static PasteMode clipboardMode();
    static void clearClipboard();

    // 粘贴到目标目录：Copy = 复制；Move = 移动（源剪贴板已标记剪切）。
    // 冲突：目标已存在则跳过（返回 skipped），不覆盖（Windows 默认提示，
    // MVP 跳过并计入 skipped）。
    static PasteResult pasteTo(const QString& targetDir);

    // 移动到回收站（freedesktop trash spec：~/.local/share/Trash）。
    // 返回 false 表示失败（路径非法/跨设备 rename 失败等）。
    static bool moveToTrash(const QString& path);

    // 重命名（返回错误信息，空 = 成功）。
    static QString rename(const QString& path, const QString& newName);
    // 新建文件夹（在 dir 下生成"新建文件夹"，重名自动加序号）。
    static QString makeDir(const QString& dir);

    // 选中项大小合计（递归目录；失败项计 0）。
    static qint64 totalSize(const QStringList& paths);

private:
    // 生成不冲突的目标名：foo.txt → foo (2).txt（Windows 语义）。
    static QString uniqueName(const QString& dir, const QString& base);
    // 递归复制（copy 语义）；返回 false 失败。
    static bool copyRecursive(const QString& src, const QString& dst);
};

}  // namespace w10de::explorer

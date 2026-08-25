// filetype —— 文件类型探测（w10viewer：文本/PDF/图像查看器）。
//
// KDE-GAP 中优先 #2：查看器支持三种内容类型 + 未知兜底。
// 判定策略：扩展名白名单 + 内容嗅探（PDF %PDF- 头优先于扩展名，
// 防止伪装文件误导渲染引擎）。

#pragma once

#include <QString>

namespace w10de::viewer {

enum class FileKind {
    Text,    // 文本（.txt/.md/.log/.ini/.conf/.json/.cpp/.h 等）
    Pdf,     // PDF（application/pdf）
    Image,   // 图像（png/jpg/bmp/webp/svg 等 Qt 可读格式）
    Unknown, // 不支持的类型
};

// 探测文件类型：先嗅探内容（PDF 头/文本可行性），再按扩展名归类。
// text/plain 判定：扩展名在文本白名单，或可嗅探为无 NUL 的 UTF-8 文本。
FileKind detectFileKind(const QString& path);

// 按类型取人类可读描述（状态栏/未知类型提示用）。
QString fileKindName(FileKind kind);

}  // namespace w10de::viewer

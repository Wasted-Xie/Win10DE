// filetype.cpp —— 文件类型探测实现。

#include "systemapps/viewer/filetype.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringList>

namespace w10de::viewer {

namespace {

// 文本扩展名白名单（常用源码/配置/文档）。
const QSet<QString>& textExtensions() {
    static const QSet<QString> s = {
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"),
        QStringLiteral("log"), QStringLiteral("ini"), QStringLiteral("conf"),
        QStringLiteral("cfg"), QStringLiteral("json"), QStringLiteral("xml"),
        QStringLiteral("yaml"), QStringLiteral("yml"), QStringLiteral("toml"),
        QStringLiteral("csv"), QStringLiteral("tsv"),
        QStringLiteral("c"), QStringLiteral("h"), QStringLiteral("cpp"),
        QStringLiteral("hpp"), QStringLiteral("cc"), QStringLiteral("cxx"),
        QStringLiteral("py"), QStringLiteral("js"), QStringLiteral("ts"),
        QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"),
        QStringLiteral("java"), QStringLiteral("rs"), QStringLiteral("go"),
        QStringLiteral("sql"), QStringLiteral("html"), QStringLiteral("htm"),
        QStringLiteral("css"), QStringLiteral("license"), QStringLiteral("readme"),
        QStringLiteral("diff"), QStringLiteral("patch"),
    };
    return s;
}

// 图像扩展名（Qt 支持的读取格式子集；svg 由 QImageReader 直读）。
const QSet<QString>& imageExtensions() {
    static const QSet<QString> s = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("gif"),
        QStringLiteral("svg"), QStringLiteral("svgz"), QStringLiteral("xpm"),
        QStringLiteral("ico"), QStringLiteral("tif"), QStringLiteral("tiff"),
    };
    return s;
}

}  // namespace

FileKind detectFileKind(const QString& path) {
    // 1) 内容嗅探：PDF 魔数（%PDF-）优先于扩展名（防伪装文件误导）。
    //    文件不可读时 head 为空，退回扩展名判定。
    QFile f(path);
    QByteArray head;
    if (f.open(QIODevice::ReadOnly)) {
        head = f.read(8);
        if (head.startsWith("%PDF-")) {
            return FileKind::Pdf;
        }
    }
    // 2) 扩展名归类（对不可读/不存在的路径也有效——selftest 断言）。
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QStringLiteral("pdf")) {
        return FileKind::Pdf;
    }
    if (imageExtensions().contains(ext)) {
        return FileKind::Image;
    }
    if (textExtensions().contains(ext)) {
        return FileKind::Text;
    }
    // 3) 内容兜底：无 NUL 的 UTF-8 文本（跳过二进制）。
    //    只读取前 4KB 判定，避免大文件整读。
    if (head.isEmpty()) {
        return FileKind::Unknown;  // 文件打不开且扩展名未知
    }
    f.seek(0);
    const QByteArray sample = f.read(4096);
    if (sample.contains('\0')) {
        return FileKind::Unknown;
    }
    // UTF-8 合法性粗检：0xC0-0xDF 后须跟 1 个连续字节，0xE0-0xEF 后 2 个。
    int i = 0;
    while (i < sample.size()) {
        const unsigned char c = static_cast<unsigned char>(sample.at(i));
        int need = 0;
        if (c < 0x80) {
            ++i;
            continue;
        } else if (c >= 0xC2 && c <= 0xDF) {
            need = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            need = 2;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3;
        } else {
            return FileKind::Unknown;  // 非法 UTF-8 或二进制
        }
        if (i + need >= sample.size()) {
            // 样本截断：按文本处理（大文本文件首 4KB 可能切断多字节字符）。
            return FileKind::Text;
        }
        // 审查 L1：首字节范围约束（防 overlong/surrogate/超范围编码）。
        if (need >= 2 && c == 0xE0 && (static_cast<unsigned char>(sample.at(i + 1)) & 0xE0) != 0xA0) {
            return FileKind::Unknown;  // overlong
        }
        if (need >= 2 && c == 0xED && (static_cast<unsigned char>(sample.at(i + 1)) & 0xE0) == 0x80) {
            return FileKind::Unknown;  // surrogate D800-DFFF
        }
        if (need >= 3 && c == 0xF0 && (static_cast<unsigned char>(sample.at(i + 1)) & 0xF0) != 0x90) {
            return FileKind::Unknown;  // 超出 U+10000 范围
        }
        if (need >= 3 && c == 0xF4 && (static_cast<unsigned char>(sample.at(i + 1)) & 0xF0) != 0x80) {
            return FileKind::Unknown;  // 超出 U+10FFFF
        }
        for (int k = 1; k <= need; ++k) {
            const unsigned char cont =
                static_cast<unsigned char>(sample.at(i + k));
            if ((cont & 0xC0) != 0x80) {
                return FileKind::Unknown;
            }
        }
        i += need + 1;
    }
    return FileKind::Text;
}

QString fileKindName(FileKind kind) {
    switch (kind) {
    case FileKind::Text: return QStringLiteral("文本");
    case FileKind::Pdf: return QStringLiteral("PDF 文档");
    case FileKind::Image: return QStringLiteral("图像");
    case FileKind::Unknown: return QStringLiteral("未知类型");
    }
    return QStringLiteral("未知类型");
}

}  // namespace w10de::viewer

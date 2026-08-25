// DefaultApps —— 默认应用设置（第二批收官：对标 KDE System Settings →
// Default Applications）。
//
// 用 xdg 标准 mimeapps.list（~/.config/mimeapps.list）[Default Applications]
// 段：mimeType=desktopId（xdg-mime 同款格式，直接读写文件）。
// 类别：浏览器（x-scheme-handler/http、https、text/html）、邮件
// （x-scheme-handler/mailto）、文件管理器（inode/directory）。
#pragma once

#include <QString>
#include <QStringList>

namespace w10de::settings {

struct DesktopApp {
    QString id;        // 文件名（如 firefox.desktop）
    QString name;      // Name= 字段
    QString exec;      // Exec= 字段
};

// 默认应用类别（KDE Default Applications 的主要类别）。
enum class DefaultKind {
    Browser,   // x-scheme-handler/http + https + text/html
    Mail,      // x-scheme-handler/mailto
    FileManager,  // inode/directory
    Count,
};
QString defaultKindMime(DefaultKind kind);         // 主 mime 类型（http）
QStringList defaultKindMimes(DefaultKind kind);    // 全部 mime 类型
QString defaultKindLabel(DefaultKind kind);        // 中文标签

class DefaultApps {
public:
    // 扫描 .desktop 应用（/usr/share/applications + ~/.local/share/applications）。
    static QList<DesktopApp> listApplications();

    // 读 mimeapps.list 的 [Default Applications] 段 → mime → desktopId。
    static QMap<QString, QString> loadMimeDefaults(const QString& path);
    // 写 mimeapps.list（保留注释/其他段；替换/追加 [Default Applications]）。
    static bool saveMimeDefaults(const QString& path,
                                 const QMap<QString, QString>& defaults);

    // 便捷：查询/设置某类别的默认应用。
    static QString currentDefault(const QString& mimeappsPath, DefaultKind kind);
    static bool setDefault(const QString& mimeappsPath, DefaultKind kind,
                           const QString& desktopId);
};

}  // namespace w10de::settings

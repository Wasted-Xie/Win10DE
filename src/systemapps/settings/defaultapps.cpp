#include "systemapps/settings/defaultapps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

namespace w10de::settings {

QString defaultKindMime(DefaultKind kind) {
    switch (kind) {
    case DefaultKind::Browser: return QStringLiteral("x-scheme-handler/http");
    case DefaultKind::Mail: return QStringLiteral("x-scheme-handler/mailto");
    case DefaultKind::FileManager: return QStringLiteral("inode/directory");
    case DefaultKind::Viewer: return QStringLiteral("image/png");
    default: return QString();
    }
}

QStringList defaultKindMimes(DefaultKind kind) {
    switch (kind) {
    case DefaultKind::Browser:
        // 审查 M5：只设 scheme handler（http/https）；text/html 属文件
        // 关联，覆盖它会静默破坏用户对本地 HTML 的关联。
        return {QStringLiteral("x-scheme-handler/http"),
                QStringLiteral("x-scheme-handler/https")};
    case DefaultKind::Mail:
        return {QStringLiteral("x-scheme-handler/mailto")};
    case DefaultKind::FileManager:
        return {QStringLiteral("inode/directory")};
    case DefaultKind::Viewer:
        // 审查 O2：查看器类别——图像/PDF/文本（w10viewer 支持范围）。
        return {QStringLiteral("image/png"), QStringLiteral("image/jpeg"),
                QStringLiteral("image/bmp"), QStringLiteral("image/webp"),
                QStringLiteral("image/gif"), QStringLiteral("image/svg+xml"),
                QStringLiteral("application/pdf"),
                QStringLiteral("text/plain")};
    default:
        return {};
    }
}

QString defaultKindLabel(DefaultKind kind) {
    switch (kind) {
    case DefaultKind::Browser: return QStringLiteral("Web 浏览器");
    case DefaultKind::Mail: return QStringLiteral("邮件客户端");
    case DefaultKind::FileManager: return QStringLiteral("文件管理器");
    case DefaultKind::Viewer: return QStringLiteral("查看器（图像/PDF/文本）");
    default: return QString();
    }
}

QList<DesktopApp> DefaultApps::listApplications() {
    QList<DesktopApp> apps;
    // 审查 M4：尊重 XDG_DATA_HOME；审查 M7：用户目录后扫 + 已见 id 跳过
    //（用户目录覆盖系统目录是 xdg 语义）。
    QStringList dirs = {QStringLiteral("/usr/share/applications")};
    const QString local = QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation) + QStringLiteral("/applications");
    if (QDir(local).exists()) {
        dirs.append(local);
    }
    QSet<QString> seen;
    // 审查 M6：.desktop Name 解析——只取 [Desktop Entry] 主段，按 locale
    // 匹配 Name[lang]（fallback Name[lang] → Name）。
    const QString lang = QLocale::system().name();  // "zh_CN" / "en_US"
    for (const QString& dir : dirs) {
        const QFileInfoList entries = QDir(dir).entryInfoList(
            QStringList() << QStringLiteral("*.desktop"), QDir::Files);
        for (const QFileInfo& fi : entries) {
            if (seen.contains(fi.fileName())) {
                continue;  // 用户目录覆盖系统目录（审查 M7）
            }
            seen.insert(fi.fileName());
            DesktopApp app;
            app.id = fi.fileName();
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            const QString content = QString::fromUtf8(f.readAll());
            // 只解析第一个 [Desktop Entry] 段（跳过 [Desktop Action] 等）。
            const QStringList lines = content.split(QLatin1Char('\n'));
            bool inMain = false;
            QString name, exec;
            const QRegularExpression sectionRe(QStringLiteral("^\\s*\\[([^]]+)\\]\\s*$"));
            for (const QString& line : lines) {
                const auto m = sectionRe.match(line);
                if (m.hasMatch()) {
                    inMain = m.captured(1).trimmed()
                        == QStringLiteral("Desktop Entry");
                    if (inMain) {
                        continue;
                    }
                    if (!name.isEmpty()) {
                        break;  // 主段已解析完（进入其他段）
                    }
                    continue;
                }
                if (!inMain || !name.isEmpty()) {
                    continue;
                }
                if (line.startsWith(QStringLiteral("Name[%1]=").arg(lang))) {
                    name = line.mid(line.indexOf(QLatin1Char('=')) + 1).trimmed();
                } else if (line.startsWith(QStringLiteral("Name="))) {
                    name = line.mid(line.indexOf(QLatin1Char('=')) + 1).trimmed();
                } else if (line.startsWith(QStringLiteral("Exec="))) {
                    exec = line.mid(line.indexOf(QLatin1Char('=')) + 1).trimmed();
                }
            }
            app.name = name.isEmpty() ? fi.baseName() : name;
            app.exec = exec;
            apps.append(app);
        }
    }
    return apps;
}

QMap<QString, QString> DefaultApps::loadMimeDefaults(const QString& path) {
    QMap<QString, QString> defaults;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return defaults;
    }
    bool inDefaultSection = false;
    const QRegularExpression sectionRe(QStringLiteral("^\\s*\\[([^]]+)\\]\\s*$"));
    const QRegularExpression kvRe(QStringLiteral("^\\s*([^=#;]+?)\\s*=\\s*(.+?)\\s*$"));
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        const auto sectionMatch = sectionRe.match(line);
        if (sectionMatch.hasMatch()) {
            inDefaultSection = sectionMatch.captured(1).trimmed()
                == QStringLiteral("Default Applications");
            continue;
        }
        if (!inDefaultSection) {
            continue;
        }
        const auto kvMatch = kvRe.match(line);
        if (kvMatch.hasMatch()) {
            // 审查 L1：值是分号分隔列表（spec），取第一个作为默认 id。
            defaults.insert(kvMatch.captured(1).trimmed(),
                            kvMatch.captured(2).trimmed().section(QLatin1Char(';'), 0, 0));
        }
    }
    return defaults;
}

bool DefaultApps::saveMimeDefaults(const QString& path,
                                   const QMap<QString, QString>& defaults) {
    // 保留原文件的注释与其他段；替换 [Default Applications] 段。
    QStringList lines;
    QFile f(path);
    bool existing = f.exists();
    if (existing && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        bool inDefaultSection = false;
        const QRegularExpression sectionRe(QStringLiteral("^\\s*\\[([^]]+)\\]\\s*$"));
        while (!ts.atEnd()) {
            const QString line = ts.readLine();
            const auto m = sectionRe.match(line);
            if (m.hasMatch()) {
                inDefaultSection = m.captured(1).trimmed()
                    == QStringLiteral("Default Applications");
                if (inDefaultSection) {
                    continue;  // 旧段跳过（将由新内容替换）
                }
            } else if (inDefaultSection) {
                continue;
            }
            lines.append(line);
        }
        f.close();
    }
    // 追加新段（审查 L3：空 map 不写空段——删除旧段即可；非空时补段头）。
    if (!defaults.isEmpty()) {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty()) {
            lines.append(QString());
        }
        lines.append(QStringLiteral("[Default Applications]"));
        for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) {
            lines.append(QStringLiteral("%1=%2").arg(it.key(), it.value()));
        }
    }

    // 审查 M1：原子写（QSaveFile：临时文件 + rename，失败不损坏原文件）。
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream os(&out);
    for (const QString& line : lines) {
        os << line << '\n';
    }
    return out.commit();
}

QString DefaultApps::currentDefault(const QString& mimeappsPath, DefaultKind kind) {
    const auto defaults = loadMimeDefaults(mimeappsPath);
    // 主 mime 类型优先；无则任一关联。
    const QString primary = defaultKindMime(kind);
    if (defaults.contains(primary)) {
        return defaults.value(primary);
    }
    for (const QString& mime : defaultKindMimes(kind)) {
        if (defaults.contains(mime)) {
            return defaults.value(mime);
        }
    }
    return QString();
}

bool DefaultApps::setDefault(const QString& mimeappsPath, DefaultKind kind,
                             const QString& desktopId) {
    auto defaults = loadMimeDefaults(mimeappsPath);
    for (const QString& mime : defaultKindMimes(kind)) {
        defaults.insert(mime, desktopId);
    }
    return saveMimeDefaults(mimeappsPath, defaults);
}

}  // namespace w10de::settings

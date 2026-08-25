// softwarestore.cpp —— .desktop 扫描与操作实现。

#include "systemapps/software/softwarestore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace w10de::software {

namespace {

// locale 优先取值：Name[zh_CN] > Name[zh] > Name（KDE/GLib 同规则简化版）。
// 审查 L2：LC_ALL > LC_MESSAGES > LANG；LANGUAGE 冒号分隔候选展开。
QString localizedKey(const QStringList& lines, const QString& baseKey,
                     const QStringList& localeCandidates) {
    for (const QString& loc : localeCandidates) {
        const QString key = baseKey + QLatin1Char('[') + loc + QLatin1Char(']');
        for (const QString& line : lines) {
            if (line.startsWith(key + QLatin1Char('='))) {
                const QString v = line.mid(key.size() + 1).trimmed();
                if (!v.isEmpty()) {
                    return v;  // 审查 L3：空值视为未提供，继续回退
                }
            }
        }
    }
    // 回退无后缀。
    for (const QString& line : lines) {
        if (line.startsWith(baseKey + QLatin1Char('='))) {
            return line.mid(baseKey.size() + 1).trimmed();
        }
    }
    return QString();
}

// 当前 locale 候选（审查 L2：LC_ALL > LC_MESSAGES > LANG；LANGUAGE 展开）。
QStringList localeCandidates() {
    QStringList out;
    QString lang = qEnvironmentVariable("LC_ALL");
    if (lang.isEmpty()) {
        lang = qEnvironmentVariable("LC_MESSAGES");
    }
    if (lang.isEmpty()) {
        lang = qEnvironmentVariable("LANG");
    }
    // LANGUAGE=zh_CN:zh 优先展开为候选（冒号分隔，首项优先）。
    const QString language = qEnvironmentVariable("LANGUAGE");
    if (!language.isEmpty()) {
        const QStringList parts = language.split(QLatin1Char(':'), Qt::SkipEmptyParts);
        for (const QString& p : parts) {
            if (!out.contains(p)) {
                out << p;
            }
        }
    }
    const QString langCode = lang.section(QLatin1Char('.'), 0, 0);  // zh_CN
    if (!langCode.isEmpty() && !out.contains(langCode)) {
        out << langCode;
        const QString shortCode = langCode.section(QLatin1Char('_'), 0, 0);
        if (shortCode != langCode && !out.contains(shortCode)) {
            out << shortCode;
        }
    }
    out << QStringLiteral("en_US") << QStringLiteral("en");
    return out;
}

bool isFlatpakDesktop(const QString& path) {
    // 审查 M2：系统级与用户级 Flatpak 导出目录都要识别。
    return path.startsWith(QStringLiteral("/var/lib/flatpak/exports/")) ||
           path.contains(QStringLiteral("/.local/share/flatpak/exports/"));
}

// 从 .desktop 的 Exec 推断 flatpak 应用 id（Exec=flatpak run --branch=... org.app）。
// 审查 M3：跳过带参数的选项（--branch=X / -b X）、剥引号、校验点分三段。
QString flatpakIdFromExec(const QString& exec) {
    if (!exec.contains(QStringLiteral("flatpak run"))) {
        return QString();
    }
    const QStringList parts = exec.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    bool afterRun = false;
    for (int i = 0; i < parts.size(); ++i) {
        const QString p = parts[i];
        if (afterRun) {
            // 跳过选项及其参数。
            if (p == QStringLiteral("--branch") || p == QStringLiteral("-b") ||
                    p == QStringLiteral("--user") || p == QStringLiteral("--system") ||
                    p == QStringLiteral("--arch") || p == QStringLiteral("--file-forwarding")) {
                if ((p == QStringLiteral("--branch") || p == QStringLiteral("-b") ||
                     p == QStringLiteral("--arch")) && i + 1 < parts.size()) {
                    ++i;  // 跳参数
                }
                continue;
            }
            if (p.startsWith(QLatin1Char('-'))) {
                continue;  // 其他选项
            }
            QString id = p;
            id.remove(QLatin1Char('"'));  // 剥引号
            // 校验点分三段（org.app.App 形式）；不匹配视为解析失败。
            if (id.count(QLatin1Char('.')) >= 2) {
                return id;
            }
            return QString();
        }
        if (p == QStringLiteral("run")) {
            afterRun = true;
        }
    }
    return QString();
}

// 图标解析：优先返回图标名（QIcon(name) 走 XDG 图标主题），其次探测常见
// 目录（审查 M6：补 SVG/scalable/用户目录）。
QString resolveIconPath(const QString& icon) {
    if (icon.isEmpty()) {
        return QString();
    }
    if (icon.contains(QLatin1Char('/'))) {
        return QFileInfo::exists(icon) ? icon : QString();
    }
    // 常见图标目录探测（含 SVG；覆盖用户与 Flatpak 导出图标）。
    const QString home = QDir::homePath();
    const QStringList dirs = {
        home + QStringLiteral("/.local/share/icons/hicolor/64x64/apps"),
        home + QStringLiteral("/.local/share/icons/hicolor/scalable/apps"),
        home + QStringLiteral("/.local/share/flatpak/exports/share/icons/hicolor/64x64/apps"),
        home + QStringLiteral("/.local/share/flatpak/exports/share/icons/hicolor/scalable/apps"),
        QStringLiteral("/var/lib/flatpak/exports/share/icons/hicolor/64x64/apps"),
        QStringLiteral("/var/lib/flatpak/exports/share/icons/hicolor/scalable/apps"),
        QStringLiteral("/usr/share/icons/hicolor/64x64/apps"),
        QStringLiteral("/usr/share/icons/hicolor/scalable/apps"),
        QStringLiteral("/usr/share/icons/Adwaita/64x64/apps"),
        QStringLiteral("/usr/share/pixmaps"),
    };
    for (const QString& dir : dirs) {
        const QString png = dir + QLatin1Char('/') + icon + QStringLiteral(".png");
        if (QFileInfo::exists(png)) {
            return png;
        }
        const QString svg = dir + QLatin1Char('/') + icon + QStringLiteral(".svg");
        if (QFileInfo::exists(svg)) {
            return svg;
        }
    }
    return QString();
}

}  // namespace

SoftwareStore::DesktopEntry SoftwareStore::parseDesktopFile(const QString& path) {
    DesktopEntry entry;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return entry;
    }
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    const QStringList lines = content.split(QLatin1Char('\n'));

    // 仅处理 [Desktop Entry] 段（跳过其他组）。
    const QStringList locale = localeCandidates();
    bool inMain = false;
    QStringList mainLines;
    for (const QString& line : lines) {
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1Char('['))) {
            inMain = (t == QStringLiteral("[Desktop Entry]"));
            continue;
        }
        if (inMain) {
            mainLines << t;
        }
    }

    entry.name = localizedKey(mainLines, QStringLiteral("Name"), locale);
    entry.comment = localizedKey(mainLines, QStringLiteral("Comment"), locale);
    entry.exec = localizedKey(mainLines, QStringLiteral("Exec"), locale);
    entry.icon = localizedKey(mainLines, QStringLiteral("Icon"), locale);
    entry.categories = localizedKey(mainLines, QStringLiteral("Categories"), locale);
    entry.type = localizedKey(mainLines, QStringLiteral("Type"), locale);
    // 审查 M1：NoDisplay/Hidden 布尔键（true/1 视为真）。
    const QString noDisplay = localizedKey(mainLines, QStringLiteral("NoDisplay"), locale);
    const QString hidden = localizedKey(mainLines, QStringLiteral("Hidden"), locale);
    entry.noDisplay = (noDisplay == QStringLiteral("true") || noDisplay == QStringLiteral("1"));
    entry.hidden = (hidden == QStringLiteral("true") || hidden == QStringLiteral("1"));
    return entry;
}

std::vector<AppInfo> SoftwareStore::listInstalled() {
    std::vector<AppInfo> apps;
    // 目录顺序：用户 > 用户 Flatpak > 系统 Flatpak > 系统（审查 M2 补用户级）。
    const QString userApps =
        QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    const QStringList searchDirs = {
        userApps,
        QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications"),
        QStringLiteral("/var/lib/flatpak/exports/share/applications"),
        QStringLiteral("/usr/share/applications"),
    };
    QSet<QString> seenIds;  // 审查 L8：QSet 查重 O(1)
    for (const QString& dir : searchDirs) {
        QDir d(dir);
        if (!d.exists()) {
            continue;
        }
        const QFileInfoList files = d.entryInfoList(
            {QStringLiteral("*.desktop")},
            QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo& fi : files) {
            const QString id = fi.completeBaseName();
            if (seenIds.contains(id)) {
                continue;  // 用户/Flatpak 优先（前面的目录先扫）
            }
            seenIds.insert(id);

            AppInfo app;
            app.id = id;
            const DesktopEntry entry = parseDesktopFile(fi.absoluteFilePath());
            // 审查 M1：过滤 NoDisplay/Hidden/非 Application。
            if (entry.noDisplay || entry.hidden) {
                continue;
            }
            if (!entry.type.isEmpty() && entry.type != QStringLiteral("Application")) {
                continue;
            }
            app.name = entry.name.isEmpty() ? id : entry.name;
            app.exec = entry.exec;
            app.comment = entry.comment;
            app.icon = resolveIconPath(entry.icon);
            app.category = entry.categories.section(QLatin1Char(';'), 0, 0);
            if (isFlatpakDesktop(fi.absoluteFilePath())) {
                app.source = AppSource::Flatpak;
                app.flatpakId = flatpakIdFromExec(entry.exec);
            } else if (fi.absoluteFilePath().startsWith(userApps)) {
                app.source = AppSource::User;
            } else {
                app.source = AppSource::System;
            }
            apps.push_back(std::move(app));
        }
    }
    return apps;
}

bool SoftwareStore::launch(const AppInfo& app) {
    if (app.exec.isEmpty()) {
        return false;
    }
    // Exec 字段含 % 占位符（%U/%F 等）——替换为空（分离式启动无参数）。
    // 审查 L1：%% 先转义为字面 %。
    QString cmd = app.exec;
    cmd.replace(QStringLiteral("%%"), QStringLiteral("%"));
    cmd.replace(QRegularExpression(QStringLiteral("%[a-zA-Z]")), QString());
    // 分离式启动（QProcess 脱离，不等待）。限制：无 shell 语义
    //（管道/重定向/环境前缀不适用，多数 .desktop 为简单命令）。
    return QProcess::startDetached(cmd);
}

bool SoftwareStore::uninstallFlatpak(const AppInfo& app, QString* errorOut) {
    if (app.source != AppSource::Flatpak || app.flatpakId.isEmpty()) {
        return false;
    }
    QProcess proc;
    proc.start(QStringLiteral("flatpak"),
               {QStringLiteral("uninstall"), QStringLiteral("-y"), app.flatpakId});
    if (!proc.waitForFinished(120000)) {
        return false;
    }
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    // 审查 L9：失败时透传 stderr 供排查（依赖缺失/运行时占用等）。
    if (!ok && errorOut != nullptr) {
        *errorOut = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    }
    return ok;
}

bool SoftwareStore::flatpakAvailable() {
    QProcess proc;
    proc.start(QStringLiteral("flatpak"), {QStringLiteral("--version")});
    if (!proc.waitForFinished(5000)) {
        return false;
    }
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

}  // namespace w10de::software

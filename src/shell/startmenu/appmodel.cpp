#include "startmenu/appmodel.h"

#include <algorithm>
#include <utility>

#include <QDir>
#include <QFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

namespace w10de {

namespace {

// 从 [Desktop Entry] 段读取指定键（处理带引号的值）。
QString readKey(const QString& content, const QString& key) {
    const QStringList lines = content.split(QLatin1Char('\n'));
    bool inDesktopEntry = false;
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line == QStringLiteral("[Desktop Entry]")) {
            inDesktopEntry = true;
            continue;
        }
        if (inDesktopEntry && line.startsWith(QLatin1Char('['))) {
            break;  // 下一个段
        }
        if (!inDesktopEntry || !line.startsWith(key + QLatin1Char('='))) {
            continue;
        }
        QString value = line.mid(key.size() + 1).trimmed();
        if (value.size() >= 2 &&
                ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) ||
                 (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
            value = value.mid(1, value.size() - 2);
        }
        return value;
    }
    return QString();
}

void scanDirectory(const QString& dirPath, QList<AppEntry>* out, QSet<QString>* seen) {
    QDir dir(dirPath);
    if (!dir.exists()) {
        return;
    }
    const QStringList files =
        dir.entryList(QStringList() << QStringLiteral("*.desktop"), QDir::Files);
    for (const QString& file : files) {
        QFile f(dir.filePath(file));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString content = QTextStream(&f).readAll();
        f.close();

        // 跳过隐藏/终端/非应用条目。
        if (readKey(content, QStringLiteral("NoDisplay")) == QStringLiteral("true")) {
            continue;
        }
        if (readKey(content, QStringLiteral("Type")) != QStringLiteral("Application")) {
            continue;
        }
        if (readKey(content, QStringLiteral("Hidden")) == QStringLiteral("true")) {
            continue;
        }

        AppEntry entry;
        entry.name = readKey(content, QStringLiteral("Name"));
        entry.icon = readKey(content, QStringLiteral("Icon"));
        entry.exec = readKey(content, QStringLiteral("Exec"));
        if (entry.name.isEmpty() || entry.exec.isEmpty()) {
            continue;
        }
        // 按（名称,Exec）去重。
        const QString dedupKey = entry.name + QLatin1Char('\n') + entry.exec;
        if (seen->contains(dedupKey)) {
            continue;
        }
        seen->insert(dedupKey);
        out->append(std::move(entry));
    }
}

}  // namespace

QList<AppEntry> scanDesktopApplications() {
    QList<AppEntry> apps;
    // 已见条目去重（同一应用可能同时存在于系统与用户目录）。
    QSet<QString> seen;

    // 系统目录。
    scanDirectory(QStringLiteral("/usr/share/applications"), &apps, &seen);
    scanDirectory(QStringLiteral("/usr/local/share/applications"), &apps, &seen);
    // 用户目录。
    const QString userApps =
        QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (!userApps.isEmpty()) {
        scanDirectory(userApps, &apps, &seen);
    }

    // 按名称排序（稳定性对开始菜单体验重要）。
    std::sort(apps.begin(), apps.end(), [](const AppEntry& a, const AppEntry& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    return apps;
}

}  // namespace w10de

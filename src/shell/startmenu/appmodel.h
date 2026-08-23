// 应用条目模型：扫描 .desktop 文件（系统 + 用户目录）。
#pragma once

#include <QList>
#include <QString>

namespace w10de {

// 一个可启动的应用。
struct AppEntry {
    QString name;
    QString icon;   // 图标主题名（空则用默认图标）
    QString exec;   // 启动命令（含可能的 %f/%u 等占位符）
};

// 扫描标准 .desktop 目录（/usr/share/applications、
// /usr/local/share/applications、~/.local/share/applications），
// 解析 Name/Icon/Exec，跳过 NoDisplay=true 与隐藏项。
QList<AppEntry> scanDesktopApplications();

}  // namespace w10de

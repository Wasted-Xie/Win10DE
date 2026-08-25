// softwarestore —— 软件中心数据源（已安装桌面应用管理）。
//
// 对标 Win10"应用和功能" + 启动：扫描系统/用户/Flatpak 导出目录的
// .desktop 文件，提供应用列表（名称/图标/命令/描述/类别/来源）与
// 启动/卸载操作。无系统包管理器依赖（安全：不安装/卸载系统包；
// Flatpak 应用经 flatpak CLI 卸载，真机可用）。
//
// 数据源（顺序去重，id 优先）：
//   /usr/share/applications
//   ~/.local/share/applications
//   /var/lib/flatpak/exports/share/applications（若存在）

#pragma once

#include <QString>
#include <QStringList>
#include <vector>

namespace w10de::software {

enum class AppSource { System, User, Flatpak };

struct AppInfo {
    QString id;        // desktop id（文件名去 .desktop）
    QString name;      // 显示名（Name，locale 优先）
    QString exec;      // Exec 命令
    QString comment;   // 描述（Comment）
    QString icon;      // 图标名或路径
    QString category;  // Categories 首项
    QString flatpakId; // Flatpak 应用 id（flatpak run 参数；非 flatpak 为空）
    AppSource source = AppSource::System;
};

class SoftwareStore {
public:
    // 扫描已安装桌面应用（每次调用重新扫描；含 Flatpak 导出目录）。
    static std::vector<AppInfo> listInstalled();

    // 应用详情（locale 优先的 Name/Comment；审查 M1：含过滤字段）。
    struct DesktopEntry {
        QString name, comment, exec, icon, categories, type;
        bool noDisplay = false;
        bool hidden = false;
    };
    // 解析单个 .desktop 文件（locale 选择：LC_ALL > LC_MESSAGES > LANG；
    // LANGUAGE 冒号分隔候选展开）。
    static DesktopEntry parseDesktopFile(const QString& path);

    // 启动应用（QProcess 分离式，与开始菜单一致）。
    static bool launch(const AppInfo& app);

    // 卸载：仅 Flatpak 应用支持（flatpak uninstall -y <id>）。
    // 返回 false 表示不可卸载（系统应用）或失败；errorOut 可接收 stderr
    //（审查 M4/L9：异步化由 UI 层用 QProcess 完成或接受同步阻塞）。
    static bool uninstallFlatpak(const AppInfo& app, QString* errorOut = nullptr);

    // Flatpak CLI 是否可用（flatpak --version 成功）。
    static bool flatpakAvailable();
};

}  // namespace w10de::software

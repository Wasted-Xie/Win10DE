// StoreWindow —— 软件中心主窗口（Win10"应用和功能"+ 商店风格）。
//
// 布局：
//   ┌─ 标题栏（Win10 风格主题色）──────────────────────────────┐
//   ├─ 顶部：搜索框（按名称/描述过滤）    [已安装 N 个应用]      │
//   ├─ 主体：左=应用网格（图标+名称），右=详情面板               │
//   │  详情：名称/描述/命令/来源/类别 + 启动/卸载按钮            │
//   └──────────────────────────────────────────────────────────┘
// 数据源 SoftwareStore（.desktop 扫描）；卸载仅 Flatpak 应用可用。

#pragma once

#include <QMainWindow>
#include <QProcess>  // slot 签名（moc 需要完整类型）

#include <QList>
#include <QStringList>

#include "systemapps/software/softwarestore.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace w10de::software {

class StoreWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit StoreWindow(QWidget* parent = nullptr);

private slots:
    void onSearchChanged(const QString& text);
    void onAppSelected(QListWidgetItem* item);
    void onUninstallFinished(int exitCode, QProcess::ExitStatus status);

private:
    void buildUi();
    void rebuildList();
    void showDetails(const AppInfo* app);
    void launchSelected();
    void uninstallSelected();

    std::vector<AppInfo> apps_;
    QListWidget* grid_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* countLabel_ = nullptr;
    // 详情面板
    QLabel* detailName_ = nullptr;
    QLabel* detailIcon_ = nullptr;
    QLabel* detailComment_ = nullptr;
    QLabel* detailMeta_ = nullptr;
    QPushButton* launchBtn_ = nullptr;
    QPushButton* uninstallBtn_ = nullptr;
    QLabel* status_ = nullptr;
    // 异步卸载（审查 M4：不阻塞 GUI）
    QProcess* uninstallProc_ = nullptr;
    bool flatpakCli_ = false;  // 构造时检测（轻微 L4）
    int visible_ = 0;
    QString selectedId_;
};

}  // namespace w10de::software

// 开始菜单（layer-shell overlay 层，Win10 磁贴风格）。
//
// Win10 布局（自左向右）：
//   左侧窄栏 48px（与开始按钮等宽）：顶部 ☰ 汉堡（展开/折叠 200px）、
//   底部功能区（账户 → 设置/文档/图片 → 电源，电源弹关机/重启/睡眠菜单）
//   应用列表列 240px（5×开始按钮宽）：顶部搜索框 + 全部应用（文本列表）
//   磁贴区 288px（6×开始按钮宽）：应用磁贴网格
// 搜索（KRunner/开始菜单语义）：输入关键词过滤应用 + 搜索主目录文件，
// 结果显示在应用列表列（应用 + 文件混合，文件用系统默认方式打开）。
// 由开始按钮切换显示；Esc 隐藏；overlay 层获得键盘焦点。
#pragma once

#include <QWidget>
#include <QVector>

#include "startmenu/appmodel.h"
#include "startmenu/tilebutton.h"

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QScrollArea;
class QToolButton;
class FlowLayout;

namespace w10de {

namespace ipc {
class FileIndex;  // KDE-GAP #5：文件索引（前向声明）
}

class StartMenu : public QWidget {
    Q_OBJECT
public:
    explicit StartMenu(QWidget* parent = nullptr);

    // 切换显示状态（开始按钮调用）。
    void toggle();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    // 汉堡按钮：展开/折叠左侧栏。
    void toggleSidebar();
    // 电源按钮：弹出关机/重启/睡眠菜单。
    void showPowerMenu();
    // 搜索框输入：过滤应用 + 搜索文件。
    void onSearchChanged(const QString& text);

private:
    void launchTile(const QString& exec);
    void launchApplication(QListWidgetItem* item);
    void rebuildAppList(const QString& filter = QString());
    void rebuildTiles();
    QToolButton* makeSideButton(const QString& icon, const QString& text);
    // 主目录文件搜索（递归，深度/数量受限）；返回可显示路径列表。
    QStringList searchFiles(const QString& keyword) const;
    // 应用名/Exec 是否匹配关键词。
    static bool appMatches(const AppEntry& app, const QString& keyword);

    QWidget* tilesHost_ = nullptr;       // 磁贴流宿主（含 FlowLayout）
    QListWidget* appList_ = nullptr;     // 应用列表列（5×按钮宽）
    QLineEdit* searchBox_ = nullptr;     // 顶部搜索框（Win10 开始菜单搜索）
    QVector<TileButton*> tiles_;         // 磁贴（尺寸可自由设置）
    QWidget* sidebar_ = nullptr;
    QToolButton* hamburgerBtn_ = nullptr;
    QToolButton* accountBtn_ = nullptr;
    QToolButton* powerBtn_ = nullptr;
    QVector<QToolButton*> sideButtons_;  // 功能区按钮（账户与电源之间）
    QList<AppEntry> appEntries_;         // 扫描缓存（搜索过滤用）
    bool sidebarExpanded_ = false;
    bool searchActive_ = false;          // 搜索模式（结果列表替代默认视图）
    // KDE-GAP #5：文件索引搜索（后台索引，替代实时 QDirIterator）。
    ipc::FileIndex* fileIndex_ = nullptr;
    class QTimer* searchDebounce_ = nullptr;  // 搜索防抖（审查 M4）

    static constexpr int kSidebarWidth = 48;             // 折叠：与开始按钮等宽
    static constexpr int kSidebarExpandedWidth = 200;    // 展开：显示文字标签
    static constexpr int kSidebarButtonHeight = 48;
    static constexpr int kAppListWidth = 5 * 48;         // 应用列表列
    static constexpr int kTilesWidth = 6 * 48 + 28;      // 磁贴区：6 小磁贴 + 7×4px 间隙
};

}  // namespace w10de

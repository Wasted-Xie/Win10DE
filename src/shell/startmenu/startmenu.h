// 开始菜单（layer-shell overlay 层，Win10 磁贴风格）。
//
// Win10 布局：左侧窄栏（48px，与开始按钮等宽）+
//   顶部 ☰ 汉堡按钮（点击展开/折叠左侧栏，展开 200px 显示文字标签）
//   底部功能区：账户按钮、功能按钮（设置/文档/图片）、电源按钮（最底，
//   点击弹出关机/重启/睡眠菜单，菜单宽与展开区域等宽）
// 主区域：应用磁贴网格（AppModel 扫描 .desktop）。
// 由开始按钮切换显示；Esc 隐藏；overlay 层获得键盘焦点。
#pragma once

#include <QWidget>
#include <QVector>

class QListWidget;
class QListWidgetItem;
class QToolButton;

namespace w10de {

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

private:
    void launchApplication(QListWidgetItem* item);
    void rebuildAppList();
    QToolButton* makeSideButton(const QString& icon, const QString& text);

    QListWidget* appGrid_ = nullptr;
    QWidget* sidebar_ = nullptr;
    QToolButton* hamburgerBtn_ = nullptr;
    QToolButton* accountBtn_ = nullptr;
    QToolButton* powerBtn_ = nullptr;
    QVector<QToolButton*> sideButtons_;  // 功能区按钮（账户与电源之间）
    bool sidebarExpanded_ = false;

    static constexpr int kSidebarWidth = 48;             // 折叠：与开始按钮等宽
    static constexpr int kSidebarExpandedWidth = 200;    // 展开：显示文字标签
    static constexpr int kSidebarButtonHeight = 48;
};

}  // namespace w10de

// w10control 主窗口 —— Win10 控制面板"按类别"视图。
//
// 与传统"设置"（左侧分类导航 + 搜索）不同：主页为类别图标网格，点入
// 传统对话框（应用/确定/取消）。功能全集与 w10settings 一致（G1）。
#pragma once

#include <QMainWindow>

class QLineEdit;
class QToolButton;

namespace w10de::control {

class ControlWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ControlWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void applyTheme();
    // 打开类别对话框（模态；返回后刷新主页状态栏）。
    void openCategory(const QString& id);
    void onSearchChanged(const QString& text);

    QLineEdit* searchBox_ = nullptr;
    QToolButton* categoryButtons_[6] = {};
};

}  // namespace w10de::control

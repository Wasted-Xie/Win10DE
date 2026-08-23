// 开始菜单（layer-shell overlay 层，Win10 磁贴风格）。
//
// 应用列表来自 .desktop 扫描（AppModel）；点击磁贴启动应用。
// 由开始按钮切换显示；Esc 隐藏；overlay 层获得键盘焦点。
#pragma once

#include <QWidget>

class QListWidget;
class QListWidgetItem;

namespace w10de {

class StartMenu : public QWidget {
    Q_OBJECT
public:
    explicit StartMenu(QWidget* parent = nullptr);

    // 切换显示状态（开始按钮调用）。
    void toggle();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void launchApplication(QListWidgetItem* item);
    void rebuildAppList();

    QListWidget* appGrid_ = nullptr;
};

}  // namespace w10de

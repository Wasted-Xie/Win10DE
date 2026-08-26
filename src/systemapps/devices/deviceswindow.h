// 设备管理器主窗口（G3：Win10 设备管理器风格——左侧硬件树 + 右侧属性）。
#pragma once

#include <QMainWindow>

#include "systemapps/devices/devicemodel.h"

class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace w10dev {

class DevicesWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DevicesWindow(QWidget* parent = nullptr);
    // 刷新硬件树（selftest/工具栏共用）。
    void refreshHardware();

private:
    void buildUi();
    void applyTheme();
    void populateTree(const QList<Device>& categories);
    void onItemSelected(QTreeWidgetItem* item, int column);
    void showDeviceDetails(const Device& device);

    QTreeWidget* tree_ = nullptr;
    QTableWidget* detailTable_ = nullptr;
    QList<Device> model_;
};

}  // namespace w10dev

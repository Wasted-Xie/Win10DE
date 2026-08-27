// DisksWindow —— 磁盘管理主窗口（可选拓展 E10，只读视图）。
//
// 左树（驱动器 → 分区），右详情（属性表）。只读浏览：无任何写操作按钮。
// 底部状态区显示数据源（udisks 可用性）与"只读"提示。

#pragma once

#include <QWidget>

#include "systemapps/disks/diskscanner.h"

class QLabel;
class QListWidget;
class QTableWidget;
class QPushButton;

namespace w10de::disks {

class DisksWindow : public QWidget {
    Q_OBJECT
public:
    explicit DisksWindow(QWidget* parent = nullptr);

    // 供验证：树节点数（驱动器 + 分区）。
    int treeCount() const;
    // 供验证：详情表行数。
    int detailRows() const;

private slots:
    void onRefresh();
    void onSelectionChanged();

private:
    void rescan();
    void showDetails(int driveIdx, int partIdx);

    QList<DriveInfo> drives_;
    QListWidget* tree_ = nullptr;
    QTableWidget* detailTable_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
};

}  // namespace w10de::disks

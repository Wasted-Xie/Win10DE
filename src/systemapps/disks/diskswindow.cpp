// DisksWindow 实现（可选拓展 E10 磁盘管理，只读视图）。

#include "systemapps/disks/diskswindow.h"

#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace w10de::disks {

namespace {
constexpr int kNameCol = 0;
constexpr int kValueCol = 1;
const char* kListStyle =
    "QListWidget{background:#262626;color:#E0E0E0;border:1px solid #3A3A3A;"
    "border-radius:6px;font-size:13px;}"
    "QListWidget::item{padding:6px;}"
    "QListWidget::item:selected{background:#3D6FB4;}";
const char* kTableStyle =
    "QTableWidget{background:#262626;color:#E0E0E0;border:1px solid #3A3A3A;"
    "border-radius:6px;font-size:13px;}"
    "QHeaderView::section{background:#333;color:#C8CDD3;border:none;"
    "padding:4px;}";
const char* kBtnStyle =
    "QPushButton{background:#3D3D3D;color:#E0E0E0;border:none;"
    "border-radius:4px;padding:7px 16px;}"
    "QPushButton:hover{background:#4A4A4A;}";
}  // namespace

DisksWindow::DisksWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("磁盘管理"));
    setFixedSize(720, 480);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("磁盘管理"), this);
    title->setStyleSheet(QStringLiteral(
        "color:#FFFFFF; font-size:20px; font-weight:bold;"));
    root->addWidget(title);

    auto* sub = new QLabel(QStringLiteral("只读浏览：磁盘 / 分区 / 挂载信息（不提供写操作）"),
                           this);
    sub->setStyleSheet(QStringLiteral("color:#9AA0A6; font-size:12px;"));
    root->addWidget(sub);

    auto* hrow = new QHBoxLayout;
    hrow->setSpacing(10);

    // 左：驱动器/分区树。
    tree_ = new QListWidget(this);
    tree_->setFixedWidth(260);
    tree_->setStyleSheet(kListStyle);
    connect(tree_, &QListWidget::currentRowChanged,
            this, &DisksWindow::onSelectionChanged);
    hrow->addWidget(tree_);

    // 右：详情表。
    detailTable_ = new QTableWidget(0, 2, this);
    detailTable_->setHorizontalHeaderLabels({QStringLiteral("属性"),
                                             QStringLiteral("值")});
    detailTable_->horizontalHeader()->setStretchLastSection(true);
    detailTable_->verticalHeader()->setVisible(false);
    detailTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailTable_->setSelectionMode(QAbstractItemView::NoSelection);
    detailTable_->setColumnWidth(kNameCol, 130);
    detailTable_->setStyleSheet(kTableStyle);
    hrow->addWidget(detailTable_, 1);
    root->addLayout(hrow, 1);

    // 底部：状态 + 刷新。
    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#9AA0A6; font-size:12px;"));
    root->addWidget(statusLabel_);

    refreshButton_ = new QPushButton(QStringLiteral("刷新"), this);
    refreshButton_->setFocusPolicy(Qt::NoFocus);
    refreshButton_->setStyleSheet(kBtnStyle);
    connect(refreshButton_, &QPushButton::clicked,
            this, &DisksWindow::onRefresh);
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(refreshButton_);
    root->addLayout(btnRow);

    setStyleSheet(QStringLiteral("QWidget{background:#2D2D2D;}"));
    rescan();
}

int DisksWindow::treeCount() const {
    return tree_->count();
}

int DisksWindow::detailRows() const {
    return detailTable_->rowCount();
}

void DisksWindow::onRefresh() {
    rescan();
}

void DisksWindow::onSelectionChanged() {
    const int row = tree_->currentRow();
    if (row < 0) {
        detailTable_->setRowCount(0);
        return;
    }
    // 树项：驱动器行存 drive index；分区行存 drive:part。
    const QString data = tree_->currentItem()->data(Qt::UserRole).toString();
    const QStringList parts = data.split(QLatin1Char(':'));
    int driveIdx = -1;
    int partIdx = -1;
    if (parts.size() == 1) {
        driveIdx = parts[0].toInt();
    } else if (parts.size() == 2) {
        driveIdx = parts[0].toInt();
        partIdx = parts[1].toInt();
    }
    showDetails(driveIdx, partIdx);
}

void DisksWindow::rescan() {
    drives_ = scanDrives();
    tree_->clear();
    for (int d = 0; d < drives_.size(); ++d) {
        const DriveInfo& drv = drives_[d];
        auto* item = new QListWidgetItem(
            QStringLiteral("  %1  %2").arg(drv.name, formatBytes(drv.sizeBytes)),
            tree_);
        item->setData(Qt::UserRole, QString::number(d));
        item->setIcon(QIcon::fromTheme(QStringLiteral("drive-harddisk")));
        for (int p = 0; p < drv.partitions.size(); ++p) {
            const PartitionInfo& part = drv.partitions[p];
            auto* pitem = new QListWidgetItem(
                QStringLiteral("    %1  %2").arg(part.name,
                                                 formatBytes(part.sizeBytes)),
                tree_);  // 构造器已自动 addItem（审查 M2：勿重复 addItem）
            pitem->setData(Qt::UserRole,
                           QStringLiteral("%1:%2").arg(d).arg(p));
            pitem->setIcon(QIcon::fromTheme(QStringLiteral("drive-partition")));
        }
    }
    if (tree_->count() > 0) {
        tree_->setCurrentRow(0);
    }
    statusLabel_->setText(QStringLiteral("共 %1 块磁盘 / %2 个分区（只读视图）")
                              .arg(drives_.size())
                              .arg([this] {
        int n = 0;
        for (const auto& d : drives_) n += d.partitions.size();
        return n;
    }()));
}

void DisksWindow::showDetails(int driveIdx, int partIdx) {
    detailTable_->setRowCount(0);
    if (driveIdx < 0 || driveIdx >= drives_.size()) {
        return;
    }
    const DriveInfo& drv = drives_[driveIdx];
    const auto addRow = [this](const QString& k, const QString& v) {
        const int r = detailTable_->rowCount();
        detailTable_->insertRow(r);
        auto* kItem = new QTableWidgetItem(k);
        auto* vItem = new QTableWidgetItem(v);
        kItem->setForeground(QColor(0x9A, 0xA0, 0xA6));
        detailTable_->setItem(r, kNameCol, kItem);
        detailTable_->setItem(r, kValueCol, vItem);
    };
    if (partIdx < 0) {
        // 驱动器详情。
        addRow(QStringLiteral("名称"), drv.name);
        addRow(QStringLiteral("设备"), drv.path);
        addRow(QStringLiteral("总大小"), formatBytes(drv.sizeBytes));
        addRow(QStringLiteral("型号"), drv.model.isEmpty()
                  ? QStringLiteral("—") : drv.model);
        addRow(QStringLiteral("可移动"), drv.removable
                  ? QStringLiteral("是") : QStringLiteral("否"));
        addRow(QStringLiteral("分区数"),
               QString::number(drv.partitions.size()));
    } else if (partIdx < drv.partitions.size()) {
        const PartitionInfo& part = drv.partitions[partIdx];
        addRow(QStringLiteral("分区"), part.name);
        addRow(QStringLiteral("设备"), part.path);
        addRow(QStringLiteral("大小"), formatBytes(part.sizeBytes));
        addRow(QStringLiteral("文件系统"), part.fsType.isEmpty()
                  ? QStringLiteral("—") : part.fsType);
        addRow(QStringLiteral("挂载点"), part.mountPoint.isEmpty()
                  ? QStringLiteral("未挂载") : part.mountPoint);
        addRow(QStringLiteral("卷标"), part.label.isEmpty()
                  ? QStringLiteral("—") : part.label);
    }
}

}  // namespace w10de::disks

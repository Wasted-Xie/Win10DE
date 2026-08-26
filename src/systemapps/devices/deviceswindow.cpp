// 设备管理器主窗口实现（G3）。
#include "systemapps/devices/deviceswindow.h"

#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "theme/colors.h"

namespace w10dev {

namespace {

QIcon makeCategoryIcon(const QString& category) {
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor stroke(200, 205, 215);
    const QColor fill(90, 130, 190);
    p.setPen(QPen(stroke, 2));
    p.setBrush(fill);
    const QRect r(2, 2, 28, 28);
    if (category == QLatin1String("cpu")) {
        // 芯片：方形 + 引脚。
        p.drawRoundedRect(r.adjusted(6, 6, -6, -6), 3, 3);
        p.setPen(QPen(stroke, 1));
        for (int i = 0; i < 3; ++i) {
            const int y = r.top() + 5 + i * 9;
            p.drawLine(r.left() + 2, y, r.left() + 6, y);
            p.drawLine(r.right() - 6, y, r.right() - 2, y);
        }
    } else if (category == QLatin1String("memory")) {
        // 内存条。
        p.drawRoundedRect(r.adjusted(6, 10, -6, -10), 2, 2);
        p.setPen(QPen(stroke, 1));
        for (int i = 0; i < 3; ++i) {
            const int x = r.left() + 10 + i * 6;
            p.drawLine(x, r.top() + 12, x, r.bottom() - 12);
        }
    } else if (category == QLatin1String("disk")) {
        // 硬盘圆盘。
        p.drawEllipse(r.adjusted(4, 4, -4, -4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r.adjusted(10, 10, -10, -10));
        p.setBrush(fill);
        p.drawEllipse(QPoint(r.center().x(), r.center().y()), 3, 3);
    } else if (category == QLatin1String("gpu")) {
        // 显示器。
        p.drawRoundedRect(r.adjusted(4, 4, -4, -10), 3, 3);
        p.drawLine(r.center().x() - 6, r.bottom() - 5, r.center().x() + 6, r.bottom() - 5);
        p.drawLine(r.center().x(), r.bottom() - 5, r.center().x(), r.bottom() - 2);
    } else if (category == QLatin1String("network")) {
        // 网络插口。
        p.drawRoundedRect(r.adjusted(7, 4, -7, -4), 2, 2);
        p.setPen(QPen(stroke, 1));
        for (int i = 0; i < 4; ++i) {
            const int x = r.left() + 10 + i * 4;
            p.drawLine(x, r.top() + 6, x, r.bottom() - 6);
        }
    } else if (category == QLatin1String("usb")) {
        // USB 插头。
        p.drawRoundedRect(r.adjusted(10, 4, -4, -8), 2, 2);
        p.drawRect(r.adjusted(10, 16, -10, -2));
    } else if (category == QLatin1String("pci")) {
        // 扩展卡。
        p.drawRoundedRect(r.adjusted(6, 6, -6, -10), 2, 2);
        p.drawLine(r.center().x(), r.bottom() - 8, r.center().x(), r.bottom() - 2);
    } else if (category == QLatin1String("input")) {
        // 键盘。
        p.drawRoundedRect(r.adjusted(4, 8, -4, -4), 3, 3);
        p.setPen(QPen(stroke, 1));
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 5; ++j) {
                p.drawPoint(r.left() + 8 + j * 5, r.top() + 11 + i * 4);
            }
        }
    } else {
        p.drawRoundedRect(r, 3, 3);
    }
    p.end();
    return QIcon(pm);
}

}  // namespace

DevicesWindow::DevicesWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    applyTheme();
    setWindowTitle(QStringLiteral("设备管理器"));
    resize(820, 520);
    refreshHardware();
}

void DevicesWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);

    // 顶部：标题 + 刷新。
    auto* headRow = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("设备管理器"), central);
    title->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold;"));
    headRow->addWidget(title);
    headRow->addStretch(1);
    auto* refreshBtn = new QToolButton(central);
    refreshBtn->setText(QStringLiteral("刷新"));
    refreshBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    headRow->addWidget(refreshBtn);
    lay->addLayout(headRow);
    connect(refreshBtn, &QToolButton::clicked, this,
            &DevicesWindow::refreshHardware);

    // 主体：左树 + 右详情。
    auto* splitter = new QSplitter(Qt::Horizontal, central);
    tree_ = new QTreeWidget(splitter);
    tree_->setHeaderLabels({QStringLiteral("设备"), QStringLiteral("状态")});
    tree_->setColumnWidth(0, 220);
    tree_->header()->setStretchLastSection(true);
    detailTable_ = new QTableWidget(splitter);
    detailTable_->setColumnCount(2);
    detailTable_->setHorizontalHeaderLabels(
        {QStringLiteral("属性"), QStringLiteral("值")});
    detailTable_->horizontalHeader()->setStretchLastSection(true);
    detailTable_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    detailTable_->verticalHeader()->setVisible(false);
    detailTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailTable_->setSelectionMode(QAbstractItemView::NoSelection);
    splitter->addWidget(tree_);
    splitter->addWidget(detailTable_);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    lay->addWidget(splitter, 1);
    setCentralWidget(central);

    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const QList<QTreeWidgetItem*> sel = tree_->selectedItems();
        if (!sel.isEmpty()) {
            onItemSelected(sel.first(), 0);
        }
    });
    statusBar()->showMessage(QStringLiteral("就绪"));
}

void DevicesWindow::applyTheme() {
    const QColor bg = w10de::theme::kStartMenuBackground();
    const QColor fg = w10de::theme::kTextPrimary();
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget, QSplitter { background: %1; color: %2; }"
        "QTreeWidget, QTableWidget { background: %3; color: %2;"
        "  border: 1px solid %4; }"
        "QHeaderView::section { background: %3; color: %2;"
        "  border: none; border-bottom: 1px solid %4; padding: 4px; }"
        "QToolButton { background: %4; color: %2; border: none;"
        "  border-radius: 3px; padding: 4px 12px; }"
        "QToolButton:hover { background: %5; }"
        "QTreeWidget::item:selected { background: %6; color: %7; }")
        .arg(bg.name(), fg.name(),
             w10de::theme::kTaskbarBackground().name(),
             w10de::theme::kHoverBackground().name(),
             w10de::theme::kPressedBackground().name(),
             w10de::theme::kAccentBlue().name(),
             w10de::theme::kAccentText().name()));
}

void DevicesWindow::refreshHardware() {
    model_ = scanHardware();
    populateTree(model_);
    statusBar()->showMessage(QStringLiteral("已扫描 %1 类设备").arg(model_.size()));
}

void DevicesWindow::populateTree(const QList<Device>& categories) {
    tree_->clear();
    for (const Device& cat : categories) {
        auto* catItem = new QTreeWidgetItem(tree_);
        catItem->setText(0, cat.name);
        catItem->setIcon(0, makeCategoryIcon(cat.category));
        catItem->setData(0, Qt::UserRole, cat.category);
        catItem->setExpanded(true);
        for (const Device& dev : cat.children) {
            auto* devItem = new QTreeWidgetItem(catItem);
            devItem->setText(0, dev.name);
            devItem->setText(1, dev.status);
            // 审查 M3（G3）：存稳定匹配键（UserRole=类别，UserRole+2=key），
            // 定位不再依赖 name（同类别重名设备会错显示）。
            devItem->setData(0, Qt::UserRole, cat.category);
            devItem->setData(0, Qt::UserRole + 2, dev.key);
        }
    }
}

void DevicesWindow::onItemSelected(QTreeWidgetItem* item, int /*column*/) {
    const QString category = item->data(0, Qt::UserRole).toString();
    if (item->parent() == nullptr) {
        // 类别节点：显示类别说明。
        detailTable_->clearContents();
        detailTable_->setRowCount(1);
        detailTable_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("类别")));
        detailTable_->setItem(0, 1,
            new QTableWidgetItem(QStringLiteral("%1（%2 个设备）")
                .arg(item->text(0)).arg(item->childCount())));
        return;
    }
    // 设备节点：按 类别+稳定键（UserRole+2）定位（审查 M3：name 匹配对
    // 重名设备不可靠）。
    const QString key = item->data(0, Qt::UserRole + 2).toString();
    for (const Device& cat : model_) {
        if (cat.category != category) {
            continue;
        }
        for (const Device& dev : cat.children) {
            if (dev.key == key) {
                showDeviceDetails(dev);
                return;
            }
        }
    }
    // 查找失败（数据不一致兜底）：清空详情避免残留上一次内容。
    detailTable_->clearContents();
    detailTable_->setRowCount(0);
    statusBar()->showMessage(QStringLiteral("设备信息不可用"));
}

void DevicesWindow::showDeviceDetails(const Device& device) {
    detailTable_->clearContents();
    detailTable_->setRowCount(device.props.size() + 2);
    int row = 0;
    detailTable_->setItem(row, 0, new QTableWidgetItem(QStringLiteral("设备")));
    detailTable_->setItem(row, 1, new QTableWidgetItem(device.name));
    ++row;
    detailTable_->setItem(row, 0, new QTableWidgetItem(QStringLiteral("状态")));
    detailTable_->setItem(row, 1, new QTableWidgetItem(device.status));
    ++row;
    for (const DeviceProperty& p : device.props) {
        detailTable_->setItem(row, 0, new QTableWidgetItem(p.key));
        detailTable_->setItem(row, 1, new QTableWidgetItem(p.value));
        ++row;
    }
    statusBar()->showMessage(QStringLiteral("设备：%1").arg(device.name));
}

}  // namespace w10dev

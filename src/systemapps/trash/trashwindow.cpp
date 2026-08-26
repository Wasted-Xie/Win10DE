// trashwindow.cpp —— 回收站窗口实现。

#include "systemapps/trash/trashwindow.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "systemapps/trash/trashstore.h"
#include "theme/colors.h"

namespace w10de::trash {

TrashWindow::TrashWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    const QColor bg = theme::kStartMenuBackground();
    const QColor fg = theme::kTextPrimary();
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background: %1; color: %2; }"
        "QToolBar { background: %3; border: none; padding: 2px; }"
        "QToolButton { color: %2; padding: 4px 8px; border-radius: 2px; }"
        "QToolButton:hover { background: %4; }"
        "QTreeWidget { background: %1; color: %2; border: 1px solid %4;"
        "  alternate-background-color: %5; }"
        "QHeaderView::section { background: %3; color: %2;"
        "  border: none; border-right: 1px solid %4; padding: 4px; }"
        "QTreeWidget::item { padding: 3px; }"
        "QTreeWidget::item:selected { background: %6; color: %7; }")
        .arg(bg.name(), fg.name(),
             theme::kTaskbarBackground().name(),
             theme::kHoverBackground().name(),
             theme::kPressedBackground().name(),
             theme::kAccentBlue().name(),
             theme::kAccentText().name()));
    resize(760, 480);
    setWindowTitle(QStringLiteral("回收站"));
    refresh();
}

TrashWindow::~TrashWindow() = default;

void TrashWindow::buildUi() {
    auto* toolbar = addToolBar(QStringLiteral("工具栏"));
    toolbar->setMovable(false);
    restoreAction_ = toolbar->addAction(QStringLiteral("恢复"));
    deleteAction_ = toolbar->addAction(QStringLiteral("彻底删除"));
    emptyAction_ = toolbar->addAction(QStringLiteral("清空回收站"));
    connect(restoreAction_, &QAction::triggered,
            this, &TrashWindow::restoreSelected);
    connect(deleteAction_, &QAction::triggered,
            this, &TrashWindow::deleteSelected);
    connect(emptyAction_, &QAction::triggered,
            this, &TrashWindow::emptyTrash);

    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);
    lay->setContentsMargins(8, 8, 8, 8);

    tree_ = new QTreeWidget(central);
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({QStringLiteral("名称"),
                            QStringLiteral("原始位置"),
                            QStringLiteral("删除时间")});
    tree_->setRootIsDecorated(false);
    tree_->setAlternatingRowColors(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    lay->addWidget(tree_);

    emptyLabel_ = new QLabel(QStringLiteral("回收站为空"), central);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setStyleSheet(QStringLiteral("color: %1; font-size: 15px;")
        .arg(theme::kTextSecondary().name()));
    lay->addWidget(emptyLabel_);
    emptyLabel_->hide();

    setCentralWidget(central);
    setStatusBar(new QStatusBar(this));

    connect(tree_, &QTreeWidget::itemSelectionChanged,
            this, &TrashWindow::updateActions);
    // 审查 T3：双击打开原始位置（不再直接恢复，避免误触）。
    connect(tree_, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem*, int) {
                openSelectedLocation();
            });
}

void TrashWindow::refresh() {
    tree_->clear();
    const QList<TrashEntry> entries = store_.list();
    for (const TrashEntry& e : entries) {
        auto* item = new QTreeWidgetItem(tree_);
        item->setText(0, e.name);
        item->setText(1, e.originalPath.isEmpty()
            ? QStringLiteral("（信息缺失）") : e.originalPath);
        item->setText(2, e.deletionDate.isValid()
            ? e.deletionDate.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
            : QStringLiteral("—"));
        item->setData(0, Qt::UserRole, e.name);
        // 审查 L9：info 缺失（无原始路径）不可恢复——UserRole+1 标记，
        // updateActions 据此禁用恢复按钮（避免点击必失败）。
        item->setData(0, Qt::UserRole + 1, !e.originalPath.isEmpty());
    }
    const bool hasItems = !entries.isEmpty();
    tree_->setVisible(hasItems);
    emptyLabel_->setVisible(!hasItems);
    updateActions();
    setStatus(hasItems
        ? QStringLiteral("回收站：%1 项").arg(entries.size())
        : QStringLiteral("回收站为空"));
}

QList<QTreeWidgetItem*> TrashWindow::selectedItems() const {
    return tree_->selectedItems();
}

void TrashWindow::updateActions() {
    const auto items = selectedItems();
    const bool has = !items.isEmpty();
    restoreAction_->setEnabled(false);
    // 审查 L9：仅当选中的条目都有原始路径（info 缺失不可恢复）时启用。
    if (has) {
        bool allRestorable = true;
        for (QTreeWidgetItem* item : items) {
            if (!item->data(0, Qt::UserRole + 1).toBool()) {
                allRestorable = false;
                break;
            }
        }
        restoreAction_->setEnabled(allRestorable);
    }
    deleteAction_->setEnabled(has);
    emptyAction_->setEnabled(tree_->topLevelItemCount() > 0);
}

void TrashWindow::restoreSelected() {
    const auto items = selectedItems();
    if (items.isEmpty()) {
        return;
    }
    int ok = 0;
    int failed = 0;
    QString firstError;
    for (QTreeWidgetItem* item : items) {
        TrashEntry e;
        e.name = item->data(0, Qt::UserRole).toString();
        if (store_.restore(e)) {
            ++ok;
        } else {
            ++failed;
            // 审查 T2：透传失败原因（EXDEV 等）。
            if (firstError.isEmpty()) {
                firstError = store_.lastError();
            }
        }
    }
    // 审查 M-4：refresh() 会重设计数消息，操作结果用超时消息避免被覆盖。
    refresh();
    statusBar()->showMessage(failed == 0
        ? QStringLiteral("已恢复 %1 项").arg(ok)
        : QStringLiteral("恢复完成：成功 %1，失败 %2（%3）")
              .arg(ok).arg(failed).arg(firstError),
        8000);
}

// 审查 T3：双击打开条目的原始位置（Windows 回收站双击语义更接近
// "打开位置"；恢复保留在工具栏，避免误触移出回收站）。
void TrashWindow::openSelectedLocation() {
    const auto items = selectedItems();
    if (items.size() != 1) {
        return;
    }
    const QString name = items.first()->data(0, Qt::UserRole).toString();
    // 从 info 读原始路径（与恢复同源）。
    const TrashEntry e = store_.entryByName(name);
    if (e.originalPath.isEmpty()) {
        statusBar()->showMessage(
            QStringLiteral("信息缺失，无法定位原始位置"), 5000);
        return;
    }
    const QString dir = QFileInfo(e.originalPath).absolutePath();
    if (!QFileInfo::exists(dir)) {
        statusBar()->showMessage(
            QStringLiteral("原始目录已不存在：%1").arg(dir), 5000);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void TrashWindow::deleteSelected() {
    const auto items = selectedItems();
    if (items.isEmpty()) {
        return;
    }
    // 确认（Windows 语义：彻底删除前提示）。
    if (QMessageBox::question(this, QStringLiteral("回收站"),
            QStringLiteral("彻底删除选中的 %1 项？此操作不可恢复。")
                .arg(items.size()))
            != QMessageBox::Yes) {
        return;
    }
    int ok = 0;
    int failed = 0;
    for (QTreeWidgetItem* item : items) {
        TrashEntry e;
        e.name = item->data(0, Qt::UserRole).toString();
        if (store_.permanentDelete(e)) {
            ++ok;
        } else {
            ++failed;
        }
    }
    // 审查 M-4：refresh() 会重设计数消息，操作结果用超时消息避免被覆盖。
    refresh();
    statusBar()->showMessage(failed == 0
        ? QStringLiteral("已彻底删除 %1 项").arg(ok)
        : QStringLiteral("删除完成：成功 %1，失败 %2").arg(ok).arg(failed),
        8000);
}

void TrashWindow::emptyTrash() {
    if (QMessageBox::question(this, QStringLiteral("回收站"),
            QStringLiteral("清空回收站？所有项目将被永久删除。"))
            != QMessageBox::Yes) {
        return;
    }
    const bool ok = store_.empty();
    // 审查 M-4：refresh() 会重设计数消息，操作结果用超时消息避免被覆盖。
    refresh();
    statusBar()->showMessage(ok ? QStringLiteral("回收站已清空")
                                : QStringLiteral("清空完成（部分项失败）"),
                             8000);
}

void TrashWindow::setStatus(const QString& text) {
    statusBar()->showMessage(text);
}

}  // namespace w10de::trash

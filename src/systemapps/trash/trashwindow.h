// trashwindow —— 回收站窗口（KDE-GAP 中优先 #3）。
//
// 对标 Windows 回收站：三列列表（名称/原始位置/删除时间）+ 工具栏
// （恢复/彻底删除/清空回收站）。数据层 TrashStore（freedesktop spec）。
// 双击条目 = 恢复（与 Windows 语义一致）。

#pragma once

#include <QMainWindow>

#include "systemapps/trash/trashstore.h"  // 值成员需完整类型

class QAction;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace w10de::trash {

class TrashWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit TrashWindow(QWidget* parent = nullptr);
    ~TrashWindow() override;

    // 刷新列表（构造后/恢复删除后调用）。
    void refresh();

private slots:
    void restoreSelected();
    void deleteSelected();
    void emptyTrash();

private:
    void buildUi();
    void updateActions();
    // 选中条目（0..n-1）。
    QList<QTreeWidgetItem*> selectedItems() const;
    void setStatus(const QString& text);

    TrashStore store_;
    QTreeWidget* tree_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QAction* restoreAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* emptyAction_ = nullptr;
};

}  // namespace w10de::trash

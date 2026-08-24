// Win10 风格文件资源管理器主窗口（系统应用，通用接口见 SYSTEMAPPS.md）。
#pragma once

#include <QMainWindow>

#include <QModelIndex>
#include <QStringList>

#include "systemapps/explorer/fileops.h"  // PasteMode

class QFileSystemModel;
class QLineEdit;
class QListView;
class QPushButton;
class QTreeView;
class QLabel;

namespace w10de::explorer {

class ExplorerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ExplorerWindow(QWidget* parent = nullptr);

    // 导航到路径（激活接口回调/地址栏/双击目录共用）。
    void navigateTo(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void goBack();
    void goForward();
    void goUp();
    void refresh();
    void addressEntered();
    void quickAccessClicked(const QModelIndex& index);
    void openItem(const QModelIndex& index);
    void showContextMenu(const QPoint& pos);
    void updateStatus();

private:
    void buildUi();
    void applyTheme();
    void updateNavState();
    void openSelected();
    void copySelected(PasteMode mode);
    void paste();
    void deleteSelected();
    void renameSelected();
    void newFolder();
    void showProperties();
    bool confirmDelete(const QStringList& paths);

    QFileSystemModel* model_ = nullptr;
    QTreeView* treeView_ = nullptr;        // 左侧目录树
    QListView* fileView_ = nullptr;        // 右侧文件区（图标模式）
    QLineEdit* addressBar_ = nullptr;
    QPushButton* backBtn_ = nullptr;
    QPushButton* fwdBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QString currentDir_;
    QStringList backStack_;
    QStringList fwdStack_;
    bool navigating_ = false;
};

}  // namespace w10de::explorer

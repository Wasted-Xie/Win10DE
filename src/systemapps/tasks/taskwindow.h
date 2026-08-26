// 任务计划程序主窗口（G5：Win10 任务计划程序风格）。
#pragma once

#include <QMainWindow>

#include "systemapps/tasks/taskstore.h"

class QLabel;
class QTableWidget;
class QTimer;

namespace w10task {

class TaskWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit TaskWindow(QWidget* parent = nullptr);
    // 刷新任务列表（selftest/工具栏共用）。
    void refreshTasks();

private:
    void buildUi();
    void applyTheme();
    void newTask();
    void editTask(int row);
    void deleteTask(int row);
    void toggleTask(int row);
    void runNow(int row);
    void saveToDisk();
    // 经 org.w10de.Tasks Reload 通知守护（失败静默：守护未运行）。
    void notifyDaemonReload();

    QTableWidget* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QList<Task> tasks_;
};

// 任务编辑对话框（新建/编辑共用；exec 后经 fields 读取）。
QDialog* makeTaskDialog(QWidget* parent, const Task& initial,
                        Task* out);

}  // namespace w10task

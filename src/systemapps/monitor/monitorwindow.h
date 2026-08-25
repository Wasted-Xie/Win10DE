// MonitorWindow —— 系统监视器主窗口（Win10 任务管理器"性能"页风格）。
//
// 布局：
//   ┌─ 标题栏（Win10 风格，复用 w10term/w10settings 的主题色）────┐
//   ├──────────────────────────────────────────────────────────────┤
//   │ [CPU 曲线图（自绘，60 点滚动）]                                │
//   │ 使用率 xx%  处理器: N 核                                     │
//   ├──────────────────────────────────────────────────────────────┤
//   │ 每核使用率: [CPU0 ▓▓▓▓ 12%] [CPU1 ▓ 3%] ...                  │
//   ├──────────────────────────────────────────────────────────────┤
//   │ 内存: [▓▓▓▓▓▓▓░░░░ 62%]  已用 12.3GB / 16.0GB（swap 1%）     │
//   ├──────────────────────────────────────────────────────────────┤
//   │ 磁盘(dev): 读 xx MB/s  写 xx MB/s  网络(iface): 收/发 KB/s   │
//   └──────────────────────────────────────────────────────────────┘
// 每秒刷新（QTimer）；数据源 SysInfo（/proc）。

#pragma once

#include <QMainWindow>

#include <QVector>

class QLabel;
class QProgressBar;
class QTabWidget;
class QTableWidget;
class QTimer;
class QWidget;

namespace w10de::monitor {

class SysInfo;

// 自绘曲线图（CPU 使用率历史）。
class GraphWidget : public QWidget {
    Q_OBJECT
public:
    explicit GraphWidget(QWidget* parent = nullptr);

    void setHistory(const QVector<double>& data, double maxValue = 100.0);
    void setCaption(const QString& caption);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QVector<double> data_;
    double maxValue_ = 100.0;
    QString caption_;
};

class MonitorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MonitorWindow(QWidget* parent = nullptr);
    ~MonitorWindow() override;

private:
    void refresh();
    void refreshProcesses();
    void buildUi();
    void killSelected();

    SysInfo* sys_ = nullptr;
    QTimer* timer_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    // 性能页
    GraphWidget* cpuGraph_ = nullptr;
    QLabel* cpuSummary_ = nullptr;
    QLabel* perCoreLabels_[32] = {};
    QProgressBar* perCoreBars_[32] = {};
    int perCoreCount_ = 0;
    QLabel* memLabel_ = nullptr;
    QProgressBar* memBar_ = nullptr;
    QLabel* diskNetLabel_ = nullptr;
    // 进程页（KDE-GAP #2）
    QTableWidget* procTable_ = nullptr;
};

}  // namespace w10de::monitor

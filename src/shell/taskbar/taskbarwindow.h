// 任务栏主窗口（layer-shell bottom 层，全宽）。
#pragma once

#include <QWidget>

class QHBoxLayout;

namespace w10de {

class StartButton;
class Clock;
class ForeignToplevelManager;
class TaskbarButton;
class TrayArea;

class TaskbarWindow : public QWidget {
    Q_OBJECT
public:
    explicit TaskbarWindow(QWidget* parent = nullptr);

    // 开始按钮（main 连接开始菜单切换）。
    StartButton* startButton() const { return startButton_; }

private:
    // 合成器窗口列表变化。
    void onToplevelAdded(class ForeignToplevelHandle* handle);
    void onToplevelRemoved(class ForeignToplevelHandle* handle);

    QHBoxLayout* layout_ = nullptr;
    StartButton* startButton_ = nullptr;
    QWidget* windowListArea_ = nullptr;
    QHBoxLayout* windowListLayout_ = nullptr;
    TrayArea* trayArea_ = nullptr;
    Clock* clock_ = nullptr;
    ForeignToplevelManager* toplevelManager_ = nullptr;
};

}  // namespace w10de

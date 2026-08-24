#include "taskbar/taskbarwindow.h"

#include <QHBoxLayout>

#include "ipc/foreigntoplevel.h"
#include "taskbar/clock.h"
#include "taskbar/startbutton.h"
#include "taskbar/taskbarbutton.h"
#include "theme/colors.h"
#include "tray/trayarea.h"

namespace w10de {

TaskbarWindow::TaskbarWindow(QWidget* parent) : QWidget(parent) {
    setFixedHeight(theme::kTaskbarHeight);
    setStyleSheet(QStringLiteral("QWidget { background: %1; }")
                      .arg(theme::kTaskbarBackground().name()));

    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    // 开始按钮。
    startButton_ = new StartButton(this);
    layout_->addWidget(startButton_);

    // 窗口列表区（foreign-toplevel 窗口按钮）。
    windowListArea_ = new QWidget(this);
    windowListArea_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    windowListLayout_ = new QHBoxLayout(windowListArea_);
    windowListLayout_->setContentsMargins(4, 0, 4, 0);
    windowListLayout_->setSpacing(4);
    windowListLayout_->addStretch();
    layout_->addWidget(windowListArea_, 1);

    // 托盘区（SNI 宿主，时钟左侧）。
    trayArea_ = new TrayArea(this);
    layout_->addWidget(trayArea_);

    // 时钟（右端）。
    clock_ = new Clock(this);
    clock_->setFixedWidth(110);
    layout_->addWidget(clock_);

    // 合成器窗口列表。
    toplevelManager_ = new ForeignToplevelManager(this);
    connect(toplevelManager_, &ForeignToplevelManager::toplevelAdded,
            this, &TaskbarWindow::onToplevelAdded);
    connect(toplevelManager_, &ForeignToplevelManager::toplevelRemoved,
            this, &TaskbarWindow::onToplevelRemoved);
    // 信号连接完成后才启动（等待 registry 与初始窗口事件）。
    toplevelManager_->start();
}

void TaskbarWindow::onToplevelAdded(ForeignToplevelHandle* handle) {
    auto* button = new TaskbarButton(handle, windowListArea_);
    // 插入到 stretch 之前（保持右对齐时钟）。
    windowListLayout_->insertWidget(windowListLayout_->count() - 1, button);
}

void TaskbarWindow::onToplevelRemoved(ForeignToplevelHandle* handle) {
    // 找到对应按钮并移除（按钮由 layout 管理，删除后自动销毁）。
    const auto buttons = windowListArea_->findChildren<TaskbarButton*>();
    for (TaskbarButton* button : buttons) {
        if (button->handle() == handle) {
            windowListLayout_->removeWidget(button);
            button->deleteLater();
            return;
        }
    }
}

}  // namespace w10de

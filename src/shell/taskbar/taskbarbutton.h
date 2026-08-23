// 任务栏窗口按钮（foreign-toplevel 窗口项）。
#pragma once

#include <QPushButton>

namespace w10de {

class ForeignToplevelHandle;

// 任务栏中的一个窗口项：显示标题，点击激活窗口，激活态高亮。
class TaskbarButton : public QPushButton {
    Q_OBJECT
public:
    explicit TaskbarButton(ForeignToplevelHandle* handle, QWidget* parent = nullptr);

    ForeignToplevelHandle* handle() const { return handle_; }

private:
    void updateFromHandle();

    ForeignToplevelHandle* handle_ = nullptr;
};

}  // namespace w10de

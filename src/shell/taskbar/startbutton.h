// 开始按钮（Win10 风格：无边框扁平按钮，带"开始"文字占位）。
#pragma once

#include <QPushButton>

namespace w10de {

class StartButton : public QPushButton {
    Q_OBJECT
public:
    explicit StartButton(QWidget* parent = nullptr);

signals:
    // 点击开始按钮（M3 下一轮连接开始菜单弹出）。
    void startMenuRequested();
};

}  // namespace w10de

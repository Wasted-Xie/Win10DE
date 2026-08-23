// 任务栏时钟（Win10 风格：时间 + 日期）。
#pragma once

#include <QLabel>

class QTimer;

namespace w10de {

class Clock : public QLabel {
    Q_OBJECT
public:
    explicit Clock(QWidget* parent = nullptr);

private:
    void updateTime();

    QTimer* timer_ = nullptr;
};

}  // namespace w10de

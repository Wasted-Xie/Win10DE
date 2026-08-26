// 任务栏时钟（Win10 风格：时间 + 日期；G6：点击弹出月历）。
#pragma once

#include <QLabel>

class QMouseEvent;
class QTimer;

namespace w10de {

class Clock : public QLabel {
    Q_OBJECT
public:
    explicit Clock(QWidget* parent = nullptr);

protected:
    // G6：左键点击 → 时钟上方弹出月历（Qt::Popup 点击外部自动关闭）。
    void mousePressEvent(QMouseEvent* e) override;

private:
    void updateTime();

    QTimer* timer_ = nullptr;
};

}  // namespace w10de

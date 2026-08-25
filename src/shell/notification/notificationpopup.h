// 通知弹窗（Win10 风格：右下角卡片，超时自动消失，点击打开通知中心）。
#pragma once

#include <QWidget>

#include <QLabel>

namespace w10de {

struct Notification;

class NotificationPopup : public QWidget {
    Q_OBJECT
public:
    explicit NotificationPopup(QWidget* parent = nullptr);

    // 显示一条通知（替换当前内容）。
    void showNotification(const Notification& n);

signals:
    // 用户点击弹窗（打开通知中心历史）。
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    QLabel* appLabel_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* bodyLabel_ = nullptr;
};

}  // namespace w10de

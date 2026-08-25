// 通知中心（Win10 操作中心：通知历史列表，右下角弹出）。
#pragma once

#include <QWidget>

class QListWidget;
class QLabel;

namespace w10de {

class Notification;

class NotificationCenter : public QWidget {
    Q_OBJECT
public:
    explicit NotificationCenter(QWidget* parent = nullptr);

    // 刷新历史列表并显示。
    void refresh(const QList<Notification>& history);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QListWidget* list_ = nullptr;
    QLabel* title_ = nullptr;
};

}  // namespace w10de

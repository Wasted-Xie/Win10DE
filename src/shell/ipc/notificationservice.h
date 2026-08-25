// Win10DE 通知服务：实现 org.freedesktop.Notifications D-Bus 接口
//（libnotify 标准，任何应用可发通知）。收到 Notify → 弹窗 + 入历史。
#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

namespace w10de {

struct Notification {
    uint id = 0;
    QString appName;
    QString appIcon;
    QString summary;
    QString body;
    QStringList actions;
    QVariantMap hints;
    QDateTime time;
};

class NotificationService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    explicit NotificationService(QObject* parent = nullptr);

    // 通知历史（供通知中心显示）。
    const QList<Notification>& history() const { return history_; }

signals:
    // 新通知到达（弹窗/历史 UI 订阅）。
    void notificationReceived(const w10de::Notification& n);

public slots:
    // org.freedesktop.Notifications.Notify
    uint Notify(const QString& app_name, uint replaces_id,
                const QString& app_icon, const QString& summary,
                const QString& body, const QStringList& actions,
                const QVariantMap& hints, int expire_timeout);
    // org.freedesktop.Notifications.CloseNotification
    void CloseNotification(uint id);
    // org.freedesktop.Notifications.GetCapabilities
    QStringList GetCapabilities();
    // org.freedesktop.Notifications.GetServerInformation
    QStringList GetServerInformation();

private:
    QList<Notification> history_;
    uint nextId_ = 1;
    static constexpr int kMaxHistory = 50;
};

}  // namespace w10de

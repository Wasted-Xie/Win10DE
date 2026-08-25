#include "shell/ipc/notificationservice.h"

#include <QDBusConnection>
#include <QDateTime>

namespace w10de {

NotificationService::NotificationService(QObject* parent) : QObject(parent) {
    // 注册服务（桌面环境独占 org.freedesktop.Notifications）。
    if (!QDBusConnection::sessionBus().registerService(
            QStringLiteral("org.freedesktop.Notifications"))) {
        qWarning() << "notifications: 服务名注册失败（已有其他通知服务？）";
        return;
    }
    if (!QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/org/freedesktop/Notifications"), this,
            QDBusConnection::ExportAllSlots)) {
        qWarning() << "notifications: 对象注册失败";
    }
}

uint NotificationService::Notify(const QString& app_name, uint replaces_id,
                                 const QString& app_icon, const QString& summary,
                                 const QString& body, const QStringList& actions,
                                 const QVariantMap& hints, int expire_timeout) {
    Notification n;
    // replaces_id：非 0 时替换既有通知（MVP：直接新增，替换留历史 UI 去重）。
    n.id = nextId_++;
    n.appName = app_name;
    n.appIcon = app_icon;
    n.summary = summary;
    n.body = body;
    n.actions = actions;
    n.hints = hints;
    n.time = QDateTime::currentDateTime();
    history_.append(n);
    while (history_.size() > kMaxHistory) {
        history_.removeFirst();
    }
    emit notificationReceived(n);
    return n.id;
}

void NotificationService::CloseNotification(uint id) {
    for (int i = 0; i < history_.size(); ++i) {
        if (history_[i].id == id) {
            history_.removeAt(i);
            break;
        }
    }
}

QStringList NotificationService::GetCapabilities() {
    return {QStringLiteral("body"), QStringLiteral("actions"),
            QStringLiteral("persistence")};
}

QStringList NotificationService::GetServerInformation() {
    // (name, vendor, version, spec-version)
    return {QStringLiteral("w10de-notifications"),
            QStringLiteral("Win10DE"),
            QStringLiteral("0.1.0"),
            QStringLiteral("1.2")};
}

}  // namespace w10de

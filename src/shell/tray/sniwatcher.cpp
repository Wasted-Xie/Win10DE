#include "tray/sniwatcher.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>

namespace w10de {

namespace {
constexpr auto kWatcherService = "org.kde.StatusNotifierWatcher";
constexpr auto kWatcherPath = "/StatusNotifierWatcher";
}  // namespace

SniWatcher::SniWatcher(QObject* parent) : QObject(parent) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    // 已有 watcher（如 Plasma）时不覆盖。
    // 注意：QStringLiteral 实参必须是字符串字面量（不能是变量）。
    if (!bus.registerService(QStringLiteral("org.kde.StatusNotifierWatcher"))) {
        qWarning() << "SniWatcher: failed to register service" << kWatcherService
                   << "(another watcher already running?)";
        return;
    }
    if (!bus.registerObject(QStringLiteral("/StatusNotifierWatcher"), this,
                            QDBusConnection::ExportAllSlots |
                                QDBusConnection::ExportAllSignals)) {
        qWarning() << "SniWatcher: failed to register object" << kWatcherPath;
        return;
    }
    registered_ = true;
    qInfo() << "SniWatcher: StatusNotifierWatcher ready on" << kWatcherService;

    // 监听 NameOwnerChanged：item 进程退出时清理残留并通知移除。
    bus.connect(QStringLiteral("org.freedesktop.DBus"),
                QStringLiteral("/org/freedesktop/DBus"),
                QStringLiteral("org.freedesktop.DBus"),
                QStringLiteral("NameOwnerChanged"),
                this, SLOT(onNameOwnerChanged(QString, QString, QString)));
}

void SniWatcher::RegisterStatusNotifierItem(const QString& service) {
    const QString normalized = normalizeItemService(service);
    if (normalized.isEmpty()) {
        qWarning() << "SniWatcher: invalid item service" << service;
        return;
    }
    if (items_.contains(normalized)) {
        return;  // 重复注册
    }
    items_.append(normalized);
    qInfo() << "SniWatcher: item registered" << normalized;
    emit StatusNotifierItemRegistered(normalized);
}

void SniWatcher::RegisterStatusNotifierHost(const QString& /*service*/) {
    // 本宿主即 watcher；其他宿主注册时告知。
    emit StatusNotifierHostRegistered();
}

QStringList SniWatcher::RegisteredStatusNotifierItems() const {
    return items_;
}

bool SniWatcher::IsStatusNotifierHostRegistered() const {
    return true;
}

int SniWatcher::ProtocolVersion() const {
    return 0;
}

void SniWatcher::onNameOwnerChanged(const QString& name, const QString& /*oldOwner*/,
                                    const QString& newOwner) {
    // item 进程退出（newOwner 为空）：移除其注册并通知。
    if (!newOwner.isEmpty()) {
        return;
    }
    const QString prefix = name + QLatin1Char('/');
    for (int i = items_.size() - 1; i >= 0; --i) {
        if (items_.at(i).startsWith(prefix)) {
            const QString service = items_.takeAt(i);
            qInfo() << "SniWatcher: item unregistered (owner gone)" << service;
            emit StatusNotifierItemUnregistered(service);
        }
    }
}

QString SniWatcher::normalizeItemService(const QString& service) const {
    if (service.isEmpty()) {
        return QString();
    }
    if (service.startsWith(QLatin1Char('/'))) {
        // 仅对象路径：服务名取调用者。
        return message().service() + service;
    }
    // "service/path" 形式：直接使用。
    return service;
}

}  // namespace w10de

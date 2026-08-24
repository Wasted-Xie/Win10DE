#include "systemapps/appipc.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QObject>

namespace w10de::app {

namespace {

// D-Bus 服务对象：持有 Activate 槽，经 Qt 元对象系统暴露。
class AppServiceObject : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.w10de.Apps")
public:
    explicit AppServiceObject(std::function<void(const QString&)> onActivate,
                              QObject* parent)
        : QObject(parent), onActivate_(std::move(onActivate)) {}

public slots:
    // D-Bus Activate(s path)：激活既有窗口（可携带入口参数）。
    // 方法名须为 Activate（协议约定，跨语言大小写敏感）。
    void Activate(const QString& path) {
        if (onActivate_) {
            onActivate_(path);
        }
    }

private:
    std::function<void(const QString&)> onActivate_;
};

}  // namespace

bool tryActivateExisting(const QString& appName, const QString& path) {
    const QString service = QStringLiteral("org.w10de.Apps.%1").arg(appName);
    QDBusInterface iface(service, QStringLiteral("/App"),
                         QStringLiteral("org.w10de.Apps"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        return false;  // 服务未运行
    }
    // 激活既有实例（path 可为空）。
    QDBusMessage reply = iface.call(QStringLiteral("Activate"), path);
    return reply.type() != QDBusMessage::ErrorMessage;
}

bool registerService(const QString& appName,
                     std::function<void(const QString& path)> onActivate,
                     QObject* parent) {
    const QString service = QStringLiteral("org.w10de.Apps.%1").arg(appName);
    auto* obj = new AppServiceObject(std::move(onActivate), parent);
    if (!QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/App"), obj,
            QDBusConnection::ExportAllSlots)) {
        return false;
    }
    if (!QDBusConnection::sessionBus().registerService(service)) {
        return false;
    }
    return true;
}

}  // namespace w10de::app

#include "appipc.moc"

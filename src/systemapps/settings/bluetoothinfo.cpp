// bluetoothinfo.cpp —— Bluez D-Bus 查询实现。
//
// 审查 S1 修复：GetManagedObjects 返回签名 a{oa{sa{sv}}}，Qt6 的
// QDBusReply<QVariantMap>（a{sv}）严格签名不匹配恒失效（qDBusReplyFill
// 按签名比较，dict 一律解组为 QDBusArgument）→ 改用类型化注册
// （KDE bluez-qt 同款做法）：
//   using ManagedObjectList = QMap<QDBusObjectPath, QMap<QString, QVariantMap>>;

#include "systemapps/settings/bluetoothinfo.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QMap>
#include <QVariantMap>

// ManagedObjectList 与元类型注册必须在全局命名空间（Qt 要求）。
using ManagedObjectList = QMap<QDBusObjectPath, QMap<QString, QVariantMap>>;
Q_DECLARE_METATYPE(ManagedObjectList)

namespace w10de::settings {

namespace {

constexpr char kService[] = "org.bluez";
constexpr char kPath[] = "/";

bool isAdapterPath(const QString& path) {
    // /org/bluez/hci0（hci10 等由前缀天然覆盖；审查 L3：接口存在性二次检查兜底）
    return path.startsWith(QStringLiteral("/org/bluez/hci"));
}

// 读取一次 GetManagedObjects（签名 a{oa{sa{sv}}}；调用前注册元类型）。
QDBusReply<ManagedObjectList> managedObjects() {
    static const bool registered = [] {
        qDBusRegisterMetaType<ManagedObjectList>();
        return true;
    }();
    (void)registered;
    QDBusInterface bluez(kService, kPath,
                         QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                         QDBusConnection::systemBus());
    return bluez.call(QStringLiteral("GetManagedObjects"));
}

}  // namespace

BluetoothStatus BluetoothInfo::query() {
    BluetoothStatus st;
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        st.errorText = QStringLiteral("无系统 D-Bus");
        return st;
    }

    QDBusReply<ManagedObjectList> reply = managedObjects();
    if (!reply.isValid()) {
        // 审查 M3：服务缺失/方法失败 → available=false + 错误透传。
        st.available = false;
        st.errorText = QStringLiteral("Bluez 服务不可用或 GetManagedObjects 失败");
        return st;
    }
    st.available = true;

    const ManagedObjectList objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QString path = it.key().path();
        const QMap<QString, QVariantMap> ifaces = it.value();

        if (isAdapterPath(path) && ifaces.contains(QStringLiteral("org.bluez.Adapter1"))) {
            // 审查 L4：多适配器时只取第一个（单开关 UI 语义）。
            if (st.adapterAddress.isEmpty()) {
                const QVariantMap props =
                    ifaces.value(QStringLiteral("org.bluez.Adapter1"));
                st.powered = props.value(QStringLiteral("Powered")).toBool();
                st.adapterAlias = props.value(QStringLiteral("Alias")).toString();
                st.adapterAddress = props.value(QStringLiteral("Address")).toString();
            }
        } else if (path.startsWith(QStringLiteral("/org/bluez/hci")) &&
                   ifaces.contains(QStringLiteral("org.bluez.Device1"))) {
            const QVariantMap props =
                ifaces.value(QStringLiteral("org.bluez.Device1"));
            BluetoothDevice dev;
            dev.name = props.value(QStringLiteral("Name")).toString();
            dev.address = props.value(QStringLiteral("Address")).toString();
            dev.connected = props.value(QStringLiteral("Connected")).toBool();
            dev.paired = props.value(QStringLiteral("Paired")).toBool();
            if (dev.name.isEmpty()) {
                dev.name = dev.address;  // 审查 L2：无 Name 直接显示 MAC（更直观）
            }
            st.devices.append(dev);
        }
    }

    return st;
}

bool BluetoothInfo::setPowered(bool on) {
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        return false;
    }
    QDBusReply<ManagedObjectList> reply = managedObjects();
    if (!reply.isValid()) {
        return false;
    }
    // 找第一个适配器。
    QString adapter;
    const ManagedObjectList objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        if (isAdapterPath(it.key().path()) &&
                it.value().contains(QStringLiteral("org.bluez.Adapter1"))) {
            adapter = it.key().path();
            break;
        }
    }
    if (adapter.isEmpty()) {
        return false;
    }
    QDBusInterface adapterIface(
        kService, adapter, QStringLiteral("org.freedesktop.DBus.Properties"), bus);
    QDBusMessage msg = adapterIface.call(
        QStringLiteral("Set"), QStringLiteral("org.bluez.Adapter1"),
        QStringLiteral("Powered"), QVariant::fromValue(QDBusVariant(on)));
    return msg.type() != QDBusMessage::ErrorMessage;
}

}  // namespace w10de::settings

// networkinfo.cpp —— NetworkManager D-Bus 查询实现。

#include "systemapps/settings/networkinfo.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace w10de::settings {

namespace {

constexpr char kService[] = "org.freedesktop.NetworkManager";
constexpr char kPath[] = "/org/freedesktop/NetworkManager";

// 读取属性（失败返回 false）。
bool readProperty(const QString& iface, const QString& path,
                  const QString& prop, QVariant* out) {
    QDBusInterface obj(kService, path, iface, QDBusConnection::systemBus());
    if (!obj.isValid()) {
        return false;
    }
    const QVariant v = obj.property(prop.toLatin1().constData());
    if (!v.isValid()) {
        return false;
    }
    *out = v;
    return true;
}

}  // namespace

NetworkStatus NetworkInfo::query() {
    NetworkStatus st;

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        return st;  // 无 system bus → 不可用
    }

    QDBusInterface nm(kService, kPath,
                      QStringLiteral("org.freedesktop.NetworkManager"), bus);
    if (!nm.isValid()) {
        return st;  // 无 NetworkManager 服务
    }
    st.available = true;

    // 连接状态（NM_STATE_*：20 unknown / 30 asleping / 40 disconnected /
    // 50 connecting / 60 connected-linking / 70 connected）。
    const QVariant stateVar = nm.property("State");
    const uint state = stateVar.isValid() ? stateVar.toUInt() : 0;
    st.connected = (state == 70);

    // 活动连接列表。
    QDBusReply<QList<QDBusObjectPath>> reply =
        nm.call(QStringLiteral("GetActiveConnections"));
    if (reply.isValid()) {
        for (const QDBusObjectPath& connPath : reply.value()) {
            const QString p = connPath.path();
            NetworkConnection conn;
            conn.path = p;
            QVariant v;
            if (readProperty(
                    QStringLiteral("org.freedesktop.NetworkManager.Connection.Active"),
                    p, QStringLiteral("Id"), &v)) {
                conn.id = v.toString();
            }
            if (readProperty(
                    QStringLiteral("org.freedesktop.NetworkManager.Connection.Active"),
                    p, QStringLiteral("Type"), &v)) {
                conn.type = v.toString();
            }
            st.connections.append(conn);
        }
    }

    st.stateText = st.connected
        ? QStringLiteral("已连接")
        : (state == 60 ? QStringLiteral("已连接（仅站点）") : QStringLiteral("未连接"));

    // IPv4：遍历设备，匹配其 ActiveConnection 对应的连接路径后取 Ip4Config
    // → AddressData（审查 M1：按路径精确匹配，避免多连接错配）。
    QDBusReply<QList<QDBusObjectPath>> devReply =
        nm.call(QStringLiteral("GetDevices"));
    if (devReply.isValid()) {
        for (const QDBusObjectPath& devPath : devReply.value()) {
            QVariant v;
            if (!readProperty(QStringLiteral("org.freedesktop.NetworkManager.Device"),
                              devPath.path(), QStringLiteral("ActiveConnection"), &v)) {
                continue;
            }
            const QString activeConn = v.value<QDBusObjectPath>().path();
            // 找到该连接对应的连接条目（按路径匹配）。
            bool found = false;
            for (NetworkConnection& conn : st.connections) {
                if (conn.path == activeConn) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                continue;
            }
            QVariant ipVar;
            if (!readProperty(QStringLiteral("org.freedesktop.NetworkManager.Device"),
                              devPath.path(), QStringLiteral("Ip4Config"), &ipVar)) {
                continue;
            }
            const QString ipPath = ipVar.value<QDBusObjectPath>().path();
            if (ipPath.isEmpty() || ipPath == QStringLiteral("/")) {
                continue;
            }
            QDBusInterface ipCfg(
                kService, ipPath,
                QStringLiteral("org.freedesktop.NetworkManager.IP4Config"), bus);
            const QVariant addrVar = ipCfg.property("AddressData");
            // 审查 S2：AddressData 为 aa{sv}。属性解组形态取决于元类型注册：
            // - 未注册 → QVariant(QDBusArgument)（真实环境默认）
            // - 已注册 QList<QVariantMap> → 直接解组为 QVariantList
            // 两种都处理（mock 验证实测 property() 返回后者时 canConvert 为 false）。
            QString ip;
            if (addrVar.canConvert<QDBusArgument>()) {
                QDBusArgument arg = addrVar.value<QDBusArgument>();
                arg.beginArray();
                while (!arg.atEnd()) {
                    QVariantMap m;
                    arg >> m;  // a{sv} → QVariantMap（Qt 已注册）
                    if (m.contains(QStringLiteral("address"))) {
                        ip = m.value(QStringLiteral("address")).toString();
                        break;
                    }
                }
                arg.endArray();
            } else if (addrVar.type() == QVariant::List ||
                       addrVar.canConvert<QVariantList>()) {
                const QVariantList list = addrVar.toList();
                for (const QVariant& a : list) {
                    const QVariantMap m = a.toMap();
                    if (m.contains(QStringLiteral("address"))) {
                        ip = m.value(QStringLiteral("address")).toString();
                        break;
                    }
                }
            }
            if (!ip.isEmpty()) {
                for (NetworkConnection& conn : st.connections) {
                    if (conn.path == activeConn) {
                        conn.ip = ip;
                        break;
                    }
                }
            }
        }
    }

    return st;
}

}  // namespace w10de::settings

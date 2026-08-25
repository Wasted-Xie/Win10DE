// networkinfo —— 网络状态查询（NetworkManager D-Bus）。
//
// 经 system bus 的 org.freedesktop.NetworkManager 查询：
//   - State 属性（70=已连接、60=连接中、40=未连接）
//   - GetActiveConnections → 各活动连接的 Id/Type
// 服务缺失（未装 NetworkManager / 无 system bus）时 available=false，
// UI 显示"服务不可用"（与音频模块同款降级）。

#pragma once

#include <QList>
#include <QString>

namespace w10de::settings {

struct NetworkConnection {
    QString id;    // 名称（SSID / 有线连接名）
    QString type;  // "802-11-wireless" / "802-3-ethernet" 等
    QString ip;    // IPv4（尽力获取；失败为空）
    QString path;  // 连接对象路径（审查 M1：设备→连接匹配用）
};

struct NetworkStatus {
    bool available = false;  // NetworkManager 服务可达
    bool connected = false;  // 有活动连接
    QString stateText;       // 状态描述（已连接 / 未连接 / 服务不可用）
    QList<NetworkConnection> connections;
};

class NetworkInfo {
public:
    // 查询当前状态（每次调用重新走 D-Bus；失败返回 available=false）。
    static NetworkStatus query();
};

}  // namespace w10de::settings

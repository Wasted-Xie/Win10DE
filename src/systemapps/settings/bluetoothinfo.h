// bluetoothinfo —— 蓝牙状态查询（Bluez D-Bus）。
//
// 经 system bus 的 org.bluez 查询（GetManagedObjects）：
//   - 适配器（/org/bluez/hci0）：Powered（开关）、Alias、Discovering
//   - 设备（/org/bluez/hci0/dev_XX）：Name、Connected、Paired
// 服务缺失（未装 Bluez / 无 system bus）时 available=false，
// UI 显示"服务不可用"（与音频/网络模块同款降级）。
// 开关适配器：Set("org.bluez.Adapter1", "Powered", ...)。

#pragma once

#include <QList>
#include <QString>

namespace w10de::settings {

struct BluetoothDevice {
    QString name;       // 设备名（无则用地址）
    QString address;    // MAC 地址（XX:XX:XX:XX:XX:XX）
    bool connected = false;
    bool paired = false;
};

struct BluetoothStatus {
    bool available = false;   // Bluez 服务可达
    bool powered = false;     // 适配器电源
    QString adapterAlias;     // 适配器别名（hci0）
    QString adapterAddress;
    QList<BluetoothDevice> devices;
    QString errorText;        // 服务缺失描述
};

class BluetoothInfo {
public:
    // 查询当前状态（D-Bus；失败返回 available=false）。
    static BluetoothStatus query();

    // 设置适配器电源（true=开 false=关）。返回是否成功。
    static bool setPowered(bool on);
};

}  // namespace w10de::settings

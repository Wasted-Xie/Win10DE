// PowerInfo —— 电源信息读取（第二批电源管理 UI）。
//
// 直接读 sysfs（/sys/class/power_supply 与 /sys/class/backlight）：
// - 电池：遍历 BAT*（capacity 百分比、status Charging/Discharging/Full、
//   energy_now 剩余能量）——所有 Linux 内核都暴露该接口（KDE 用 UPower
//   D-Bus 封装同一数据；sysfs 直读无依赖，WSL/真机均可验证）。
// - 背光：/sys/class/backlight/*（max_brightness/brightness 读写）。
#pragma once

#include <QString>
#include <QStringList>

namespace w10de::settings {

struct BatteryInfo {
    bool present = false;      // 检测到电池
    int percent = -1;          // 电量百分比 0-100（-1 = 不可读）
    QString status;            // Charging / Discharging / Full / 空
    QString device;            // 设备名（BAT0 等）
    qint64 energyNowUwh = -1;  // 剩余能量（µWh；不可读为 -1）
    bool energyIsCharge = false;  // true = energyNowUwh 实为 charge_now（µAh，
                                  // 需按 mAh 标注——审查 M1 单位换算）
};

struct BacklightInfo {
    bool present = false;
    QString device;            // 设备名（intel_backlight 等）
    int maxBrightness = 0;
    int brightness = 0;
};

class PowerInfo {
public:
    // 主电池（第一个 BAT*）；无电池返回 present=false。
    static BatteryInfo battery();
    // 第一个背光设备；无返回 present=false。
    static BacklightInfo backlight();
    // 设置背光亮度（0..max）；失败返回 false。
    static bool setBrightness(const QString& device, int value);
    // 背光设备可写探测（无权限时 UI 禁用滑块——审查 M4）。
    static bool backlightWritable(const QString& device);
};

}  // namespace w10de::settings

#include "systemapps/settings/powerinfo.h"

#include <QDir>
#include <QFile>

#include <unistd.h>  // access / W_OK（背光可写探测）

namespace w10de::settings {

namespace {

// 读 sysfs 文件的整数值；文件缺失/非法返回 def。
int readInt(const QString& path, int def) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return def;
    }
    bool ok = false;
    const int v = QString::fromUtf8(f.readAll()).trimmed().toInt(&ok);
    return ok ? v : def;
}

QString readText(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(f.readAll()).trimmed();
}

}  // namespace

BatteryInfo PowerInfo::battery() {
    BatteryInfo info;
    // 主电池：BAT* 前缀且 type=Battery（审查 M5：避免误选 hidpp_battery_0
    // 等外设电池——仅按 type 过滤会依赖字母序巧合）。
    const QString supplyDir = QStringLiteral("/sys/class/power_supply");
    const QStringList devices = QDir(supplyDir).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& dev : devices) {
        if (!dev.startsWith(QStringLiteral("BAT"))) {
            continue;
        }
        if (readText(supplyDir + QLatin1Char('/') + dev + QStringLiteral("/type"))
                != QStringLiteral("Battery")) {
            continue;
        }
        info.present = true;
        info.device = dev;
        info.percent = readInt(supplyDir + QLatin1Char('/') + dev +
                                   QStringLiteral("/capacity"), -1);
        info.status = readText(supplyDir + QLatin1Char('/') + dev +
                               QStringLiteral("/status"));
        // 剩余能量：优先 energy_now（µWh）；回退 charge_now（µAh）——
        // 标注单位不同（审查 M1），不强行换算（需 voltage_now）。
        info.energyNowUwh = readInt(supplyDir + QLatin1Char('/') + dev +
                                        QStringLiteral("/energy_now"), -1);
        if (info.energyNowUwh >= 0) {
            info.energyIsCharge = false;
        } else {
            info.energyNowUwh = readInt(supplyDir + QLatin1Char('/') + dev +
                                            QStringLiteral("/charge_now"), -1);
            info.energyIsCharge = true;
        }
        break;  // 只取主电池（多内置电池场景 v1 标注"主电池"）
    }
    return info;
}

BacklightInfo PowerInfo::backlight() {
    BacklightInfo info;
    const QString backlightDir = QStringLiteral("/sys/class/backlight");
    const QStringList devices = QDir(backlightDir).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot);
    if (devices.isEmpty()) {
        return info;
    }
    info.present = true;
    info.device = devices.first();
    info.maxBrightness = readInt(backlightDir + QLatin1Char('/') + info.device +
                                     QStringLiteral("/max_brightness"), 0);
    info.brightness = readInt(backlightDir + QLatin1Char('/') + info.device +
                                  QStringLiteral("/brightness"), 0);
    return info;
}

bool PowerInfo::setBrightness(const QString& device, int value) {
    QFile f(QStringLiteral("/sys/class/backlight/%1/brightness").arg(device));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    const qint64 written = f.write(QByteArray::number(value));
    f.close();
    // 检查写入是否完整（审查 M4：写失败不能误报成功）。
    return written == static_cast<qint64>(QByteArray::number(value).size());
}

// 背光设备是否可写（access W_OK 探测；无权限时 UI 禁用滑块——审查 M4）。
bool PowerInfo::backlightWritable(const QString& device) {
    return ::access(
        (QStringLiteral("/sys/class/backlight/%1/brightness").arg(device))
            .toLocal8Bit().constData(),
        W_OK) == 0;
}

}  // namespace w10de::settings

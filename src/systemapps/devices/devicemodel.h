// 设备管理器硬件模型（G3：sysfs/proc 数据源）。
//
// 对标 Win10 设备管理器：硬件树（类别 + 设备子项）+ 设备详情（属性键值）。
// 数据源全部为内核直出（无 lspci/lsusb 外部命令依赖）：
//   CPU    /proc/cpuinfo（型号/核心数/频率）
//   内存   /proc/meminfo（MemTotal）
//   磁盘   /sys/block/*（model/size/rotational）
//   显卡   /sys/class/drm/card*/device（vendor/device/driver）
//   网络   /sys/class/net/*（MAC/operstate/speed/driver）
//   USB    /sys/bus/usb/devices/*（idVendor/idProduct/manufacturer/product）
//   PCI    /sys/bus/pci/devices/*（vendor/device/class/driver）
//   输入   /proc/bus/input/devices（Name/Handlers/Phys）
// 所有读取做健壮性防护：文件缺失/不可读/空值一律返回空或降级（不崩溃）。
#pragma once

#include <QList>
#include <QString>

namespace w10dev {

struct DeviceProperty {
    QString key;
    QString value;
};

struct Device {
    QString name;        // 显示名（类别项 = 类别名；设备项 = 设备名）
    QString category;    // 类别标识（cpu/memory/disk/gpu/network/usb/pci/input）
    // 稳定匹配键（审查 M3：同类别重名设备定位用——disk=设备节点名、
    // usb=总线地址、pci=PCI 地址、gpu=sysfs 路径、network=接口名、
    // cpu/memory/input=类别名）。
    QString key;
    QString status;      // 状态描述（如"运行正常"；不可用设备显示原因）
    QList<DeviceProperty> props;  // 详情键值（空 = 无详情）
    QList<Device> children;       // 子项（设备管理器层级：类别 → 设备）
};

// 类别名（树顶层节点）。
QString categoryLabel(const QString& category);

// 全量扫描硬件（各类别顺序固定：计算机 → 处理器 → 内存 → 磁盘 → 显卡 →
// 网络 → USB → PCI → 输入）。缺失的类别仍返回空设备（UI 显示"无设备"）。
QList<Device> scanHardware();

// 单项扫描（selftest 与 UI 共用；各自内部容错）。
QList<Device> scanCpu();
QList<Device> scanMemory();
QList<Device> scanDisks();
QList<Device> scanGpus();
QList<Device> scanNetworks();
QList<Device> scanUsb();
QList<Device> scanPci();
QList<Device> scanInput();

// 读 sysfs 文本文件（去首尾空白）；失败返回空。
QString readSysfs(const QString& path);

}  // namespace w10dev

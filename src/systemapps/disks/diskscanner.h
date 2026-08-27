// DiskScanner —— 磁盘/分区只读浏览（可选拓展 E10 磁盘管理）。
//
// 数据源：sysfs（/sys/class/block）+ /proc/mounts（挂载点/文件系统），
// 纯只读、无外部依赖；udisks2 D-Bus 服务可用时（/org/freedesktop/UDisks2）
// 增强型号/序列号信息，不可用自动降级（WSL 无 udisks 服务场景）。
// 本模块不提供任何写操作（分区/格式化均不实现——只读视图）。
#pragma once

#include <QList>
#include <QString>

namespace w10de::disks {

struct PartitionInfo {
    QString name;        // sda1
    QString path;        // /dev/sda1
    qint64 sizeBytes = 0;
    QString fsType;      // 挂载表文件系统（未挂载为空）
    QString mountPoint;  // / 等（未挂载为空）
    QString label;       // 卷标（udisks 可用时；否则空）
};

struct DriveInfo {
    QString name;        // sda / nvme0n1
    QString path;        // /dev/sda
    qint64 sizeBytes = 0;
    QString model;       // sysfs device/model（不可读为空）
    bool removable = false;
    QList<PartitionInfo> partitions;
};

// 扫描全部物理磁盘（过滤 loop/ram/zram 虚拟设备）。
QList<DriveInfo> scanDrives();

// 分区名 → 父盘名（审查 S1：sda1→sda、nvme0n1p1→nvme0n1、mmcblk0p1→mmcblk0）。
// 静态可测（selftest 断言）。
QString deriveParent(const QString& partitionName);

// 解析 /proc/mounts 文本 → (设备路径 → 挂载点/文件系统) 映射。
// 静态可测（selftest 注入样本）。
struct MountEntry {
    QString device;     // /dev/sda1
    QString mountPoint; // /
    QString fsType;     // ext4
};
QList<MountEntry> parseMounts(const QString& procMountsText);

// 人类可读大小。
QString formatBytes(qint64 bytes);

}  // namespace w10de::disks

// DiskScanner 实现（可选拓展 E10 磁盘管理，只读）。
//
// sysfs 扫描：/sys/class/block/<name>/partition 存在 → 分区；整盘大小 =
// size × 512；型号读 device/model；removable 标志读 removable 文件。
// 挂载点/文件系统来自 /proc/mounts（设备路径精确匹配）。
// 虚拟设备过滤：loop/ram/zram/dm-/md（同设备管理器 G3 做法）。

#include "systemapps/disks/diskscanner.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTextStream>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>

namespace w10de::disks {

namespace {

// 读 sysfs 单行文本（路径不存在返回空串）。
QString readSysfs(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(f.readAll()).trimmed();
}

bool isVirtualDevice(const QString& name) {
    // loop*/ram*/zram*/dm-*/md* 为虚拟设备（过滤）。
    if (name.startsWith(QLatin1String("loop"))
            || name.startsWith(QLatin1String("ram"))
            || name.startsWith(QLatin1String("zram"))
            || name.startsWith(QLatin1String("dm-"))
            || name.startsWith(QLatin1String("md"))
            || name == QLatin1String("sr0")) {
        return true;
    }
    return false;
}

// udisks2 卷标（服务可用时）：/org/freedesktop/UDisks2/block_devices/<name>
// 的 IdLabel 属性。服务不可用返回空（静默降级）。
// 审查 M1：服务可用性只探测一次（避免每分区重建接口 + 无谓 IPC）；
// 同步调用收紧超时（默认 25s——udisks 响应慢时 UI 冻结）。
QString udisksLabel(const QString& name) {
    static bool checked = false;
    static bool available = false;
    if (!checked) {
        checked = true;
        QDBusConnection conn = QDBusConnection::systemBus();
        available = conn.isConnected()
            && conn.interface()->isServiceRegistered(
                QStringLiteral("org.freedesktop.UDisks2"));
    }
    if (!available) {
        return QString();
    }
    QDBusInterface iface(QStringLiteral("org.freedesktop.UDisks2"),
                         QStringLiteral("/org/freedesktop/UDisks2/block_devices/")
                             + name,
                         QStringLiteral("org.freedesktop.UDisks2.Block"),
                         QDBusConnection::systemBus());
    if (!iface.isValid()) {
        return QString();
    }
    iface.setTimeout(500);
    // property() 返回 QVariant（无 QDBusReply 包装）。
    const QVariant v = iface.property("IdLabel");
    return v.isValid() ? v.toString() : QString();
}

}  // namespace

QList<MountEntry> parseMounts(const QString& procMountsText) {
    QList<MountEntry> entries;
    // 审查 L4：直接按行切分（避免 QTextStream 的 const_cast 迁就）。
    const QStringList lines = procMountsText.split(
        QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        // /dev/sda1 / ext4 rw,relatime 0 0
        const QStringList parts = trimmed.split(QLatin1Char(' '),
                                                Qt::SkipEmptyParts);
        if (parts.size() < 3) continue;
        MountEntry e;
        e.device = parts[0];
        e.mountPoint = parts[1];
        e.fsType = parts[2];
        entries.append(e);
    }
    return entries;
}

QString deriveParent(const QString& name) {
    QString parent = name;
    // 审查 S1：先剥离尾随分区号（nvme0n1p1 → nvme0n1p），再剥离 p 分隔符
    // （→ nvme0n1）。旧顺序（先 p 后数字）对 NVMe/eMMC 命名推导错误。
    while (!parent.isEmpty() && parent.back().isDigit()) {
        parent.chop(1);
    }
    if (parent.endsWith(QLatin1Char('p')) && parent.size() > 1) {
        parent.chop(1);
    }
    return parent;
}

QList<DriveInfo> scanDrives() {
    QList<DriveInfo> drives;
    QDir sysBlock(QStringLiteral("/sys/class/block"));
    if (!sysBlock.exists()) {
        return drives;
    }
    const QStringList names = sysBlock.entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDir::Name);

    // 挂载表（一次解析）。
    QFile mountsFile(QStringLiteral("/proc/mounts"));
    QString mountsText;
    if (mountsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        mountsText = QString::fromUtf8(mountsFile.readAll());
        mountsFile.close();
    }
    const QList<MountEntry> mounts = parseMounts(mountsText);
    // 设备路径（/dev/sda1）→ 挂载条目下标（审查 L1：存索引而非指针——
    // 指针方案依赖 QList 存储稳定性，存索引更稳健）。
    QHash<QString, int> mountIndex;
    for (int i = 0; i < mounts.size(); ++i) {
        mountIndex.insert(mounts[i].device, i);
    }

    // 先建驱动器骨架（非分区条目）。
    QHash<QString, int> driveIndex;  // name → drives[] 下标
    for (const QString& name : names) {
        if (isVirtualDevice(name)) continue;
        const QString sys = QStringLiteral("/sys/class/block/") + name;
        const bool isPart = QFile::exists(sys + QStringLiteral("/partition"));
        if (isPart) continue;  // 分区在第二步归入驱动器
        DriveInfo d;
        d.name = name;
        d.path = QStringLiteral("/dev/") + name;
        bool okSize = false;
        const qint64 sectors = readSysfs(sys + QStringLiteral("/size"))
                                   .toLongLong(&okSize);
        d.sizeBytes = okSize && sectors > 0 ? sectors * 512 : 0;
        d.model = readSysfs(sys + QStringLiteral("/device/model"));
        d.removable = readSysfs(sys + QStringLiteral("/removable"))
                          == QStringLiteral("1");
        driveIndex.insert(name, drives.size());
        drives.append(d);
    }
    // 分区归入父驱动器。
    for (const QString& name : names) {
        if (isVirtualDevice(name)) continue;
        const QString sys = QStringLiteral("/sys/class/block/") + name;
        const bool isPart = QFile::exists(sys + QStringLiteral("/partition"));
        if (!isPart) continue;
        PartitionInfo p;
        p.name = name;
        p.path = QStringLiteral("/dev/") + name;
        bool okSize = false;
        const qint64 sectors = readSysfs(sys + QStringLiteral("/size"))
                                   .toLongLong(&okSize);
        p.sizeBytes = okSize && sectors > 0 ? sectors * 512 : 0;
        // 父盘名（审查 S1：deriveParent 处理 sd/nvme/mmc 命名）。
        const QString parent = deriveParent(name);
        const auto it = mountIndex.constFind(p.path);
        if (it != mountIndex.constEnd() && it.value() >= 0
                && it.value() < mounts.size()) {
            p.mountPoint = mounts[it.value()].mountPoint;
            p.fsType = mounts[it.value()].fsType;
        }
        p.label = udisksLabel(name);
        const int di = driveIndex.value(parent, -1);
        if (di >= 0 && di < drives.size()) {
            drives[di].partitions.append(p);
        } else {
            // 父盘缺失（分区盘但整盘被过滤）：告警而非静默丢弃（审查 S1）。
            qWarning("disks: partition %s has no visible parent drive",
                     qPrintable(name));
        }
    }
    return drives;
}

QString formatBytes(qint64 bytes) {
    if (bytes < 0) return QStringLiteral("-");
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1.0) return QStringLiteral("%1 B").arg(bytes);
    const double mb = kb / 1024.0;
    if (mb < 1.0) return QStringLiteral("%1 KB").arg(kb, 0, 'f', 0);
    const double gb = mb / 1024.0;
    if (gb < 1.0) return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
    const double tb = gb / 1024.0;
    if (tb < 1.0) return QStringLiteral("%1 GB").arg(gb, 0, 'f', 1);
    return QStringLiteral("%1 TB").arg(tb, 0, 'f', 1);
}

}  // namespace w10de::disks

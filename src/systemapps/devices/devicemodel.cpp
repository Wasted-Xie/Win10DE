// 设备管理器硬件扫描实现（sysfs/proc，健壮性优先）。
#include "systemapps/devices/devicemodel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <utility>  // std::move

namespace w10dev {

namespace {

// 读文件首行并去空白。
QString readFirstLine(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString line = QString::fromUtf8(f.readLine()).trimmed();
    f.close();
    return line;
}

// /sys 下 driver 符号链接目标名（"driver" 链接 → 驱动名）。
QString driverName(const QString& deviceSysfsPath) {
    const QFileInfo info(deviceSysfsPath + QStringLiteral("/driver"));
    return info.exists() ? info.symLinkTarget().section(QLatin1Char('/'), -1)
                         : QString();
}

// 字节数（十进制）→ 人类可读（KB/MB/GB）。
QString humanSize(qulonglong bytes) {
    if (bytes >= 1024ull * 1024 * 1024) {
        return QStringLiteral("%1 GB")
            .arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 1);
    }
    if (bytes >= 1024ull * 1024) {
        return QStringLiteral("%1 MB")
            .arg(bytes / (1024.0 * 1024), 0, 'f', 1);
    }
    return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
}

}  // namespace

QString categoryLabel(const QString& category) {
    if (category == QLatin1String("cpu")) return QStringLiteral("处理器");
    if (category == QLatin1String("memory")) return QStringLiteral("内存");
    if (category == QLatin1String("disk")) return QStringLiteral("磁盘驱动器");
    if (category == QLatin1String("gpu")) return QStringLiteral("显卡");
    if (category == QLatin1String("network")) return QStringLiteral("网络适配器");
    if (category == QLatin1String("usb")) return QStringLiteral("USB 设备");
    if (category == QLatin1String("pci")) return QStringLiteral("PCI 设备");
    if (category == QLatin1String("input")) return QStringLiteral("输入设备");
    return QStringLiteral("其他设备");
}

QString readSysfs(const QString& path) {
    return readFirstLine(path);
}

QList<Device> scanCpu() {
    QList<Device> out;
    QFile f(QStringLiteral("/proc/cpuinfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }
    QString model;
    int physical = 0;
    int cores = 0;
    int logical = 0;
    QString mhz;
    // 审查 M1（G3）：x86 有 "model name"，ARM64 无该键（只有
    // implementer/part 或 "Processor"）——fallback 链保证 CPU 类别不
    // 整体缺失。
    const QRegularExpression modelRe(
        QStringLiteral("^(?:model name|Processor|Hardware)\\s*:\\s*(.+)$"));
    const QRegularExpression physicalRe(
        QStringLiteral("^physical id\\s*:\\s*(\\d+)$"));
    const QRegularExpression coresRe(
        QStringLiteral("^cpu cores\\s*:\\s*(\\d+)$"));
    const QRegularExpression mhzRe(QStringLiteral("^cpu MHz\\s*:\\s*([0-9.]+)$"));
    const QRegularExpression procRe(QStringLiteral("^processor\\s*:\\s*(\\d+)$"));
    const QRegularExpression implRe(
        QStringLiteral("^CPU implementer\\s*:\\s*(.+)$"));
    const QRegularExpression partRe(
        QStringLiteral("^CPU part\\s*:\\s*(.+)$"));
    QSet<QString> seenPhysical;
    QString implementer, part;
    QTextStream ts(&f);
    QString line;
    while (ts.readLineInto(&line)) {
        const QString t = line.trimmed();
        QRegularExpressionMatch m;
        if ((m = modelRe.match(t)).hasMatch()) {
            model = m.captured(1).trimmed();
        } else if ((m = physicalRe.match(t)).hasMatch()) {
            seenPhysical.insert(m.captured(1));
            physical = seenPhysical.size();
        } else if ((m = coresRe.match(t)).hasMatch()) {
            cores = m.captured(1).toInt();
        } else if ((m = mhzRe.match(t)).hasMatch()) {
            mhz = m.captured(1);
        } else if ((m = procRe.match(t)).hasMatch()) {
            logical = m.captured(1).toInt() + 1;  // 最大 processor 编号 + 1
        } else if ((m = implRe.match(t)).hasMatch()) {
            implementer = m.captured(1).trimmed();
        } else if ((m = partRe.match(t)).hasMatch()) {
            part = m.captured(1).trimmed();
        }
    }
    f.close();
    if (model.isEmpty()) {
        // ARM fallback：CPU implementer/part 拼装（如 "0x41 (ARM Ltd)"）。
        if (!implementer.isEmpty() || !part.isEmpty()) {
            model = QStringLiteral("ARM CPU %1/%2")
                .arg(implementer, part.isEmpty() ? QStringLiteral("?") : part);
        } else {
            return out;  // 无任何可识别字段（极端环境）
        }
    }
    Device d;
    d.name = model;
    d.category = QStringLiteral("cpu");
    d.key = QStringLiteral("cpu");
    d.status = QStringLiteral("运行正常");
    d.props.append({QStringLiteral("型号"), model});
    if (physical > 0) {
        d.props.append({QStringLiteral("物理封装"), QString::number(physical)});
    }
    if (cores > 0) {
        d.props.append({QStringLiteral("每封装核心数"), QString::number(cores)});
    }
    if (logical > 0) {
        d.props.append({QStringLiteral("逻辑处理器"), QString::number(logical)});
    }
    if (!mhz.isEmpty()) {
        d.props.append({QStringLiteral("当前频率"), mhz + QStringLiteral(" MHz")});
    }
    out.append(d);
    return out;
}

QList<Device> scanMemory() {
    QList<Device> out;
    const QString memTotal = readSysfs(QStringLiteral("/proc/meminfo"));
    if (memTotal.isEmpty()) {
        return out;
    }
    // "MemTotal:       16279592 kB"
    const QRegularExpression re(
        QStringLiteral("^MemTotal:\\s*(\\d+)\\s*kB$"));
    const QRegularExpressionMatch m = re.match(memTotal);
    if (!m.hasMatch()) {
        return out;
    }
    Device d;
    d.name = QStringLiteral("系统内存");
    d.category = QStringLiteral("memory");
    d.key = QStringLiteral("memory");
    d.status = QStringLiteral("运行正常");
    const qulonglong kB = m.captured(1).toULongLong();
    d.props.append({QStringLiteral("总容量"),
                    humanSize(kB * 1024)});
    d.props.append({QStringLiteral("容量（kB）"), m.captured(1)});
    out.append(d);
    return out;
}

QList<Device> scanDisks() {
    QList<Device> out;
    QDir block(QStringLiteral("/sys/block"));
    const QStringList entries = block.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& name : entries) {
        // 审查 M2（G3）：过滤虚拟/内存盘（loop/ram/zram/dm/md）——WSL 下
        // 28 个 loop 全上屏且误标 SSD 无意义；保留物理/虚拟光驱与真磁盘
        // （sd/nvme/mmcblk/vd/sr）。
        const bool virtualDisk = name.startsWith(QStringLiteral("loop"))
            || name.startsWith(QStringLiteral("ram"))
            || name.startsWith(QStringLiteral("zram"))
            || name.startsWith(QStringLiteral("dm-"))
            || name.startsWith(QStringLiteral("md"));
        if (virtualDisk) {
            continue;
        }
        // dev 属性存在 = 块设备（过滤非设备目录）。
        const QString devPath = block.filePath(name) + QStringLiteral("/dev");
        if (!QFileInfo::exists(devPath)) {
            continue;
        }
        Device d;
        d.name = name;
        d.category = QStringLiteral("disk");
        d.key = name;  // 审查 M3：设备节点名作稳定匹配键
        d.status = QStringLiteral("运行正常");
        const QString base = block.filePath(name);
        // 磁盘型号（device/model；loop/ram 无）。
        const QString model = readSysfs(base + QStringLiteral("/device/model"));
        if (!model.isEmpty()) {
            d.name = QStringLiteral("%1（%2）").arg(name, model);
            d.props.append({QStringLiteral("型号"), model});
        }
        // 大小：size = 512 字节扇区数。
        const QString sizeSectors = readSysfs(base + QStringLiteral("/size"));
        bool okSize = false;
        const qulonglong sectors = sizeSectors.toULongLong(&okSize);
        if (okSize && sectors > 0) {
            d.props.append({QStringLiteral("容量"), humanSize(sectors * 512)});
        }
        // 旋转介质（1=HDD 0=SSD）。
        const QString rotational = readSysfs(base + QStringLiteral("/queue/rotational"));
        if (!rotational.isEmpty()) {
            d.props.append({QStringLiteral("介质类型"),
                rotational == QLatin1String("1") ? QStringLiteral("机械硬盘（HDD）")
                                                 : QStringLiteral("固态硬盘（SSD）")});
        }
        // 只读标志。
        const QString ro = readSysfs(base + QStringLiteral("/ro"));
        if (ro == QLatin1String("1")) {
            d.props.append({QStringLiteral("只读"), QStringLiteral("是")});
        }
        if (d.props.isEmpty()) {
            d.props.append({QStringLiteral("设备节点"),
                            QStringLiteral("/dev/%1").arg(name)});
        }
        out.append(d);
    }
    return out;
}

QList<Device> scanGpus() {
    QList<Device> out;
    QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList entries = drm.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QSet<QString> seen;  // 同一 GPU 多 cardN 去重（按 device 路径）
    for (const QString& entry : entries) {
        if (!entry.startsWith(QStringLiteral("card"))) {
            continue;
        }
        const QString devPath = drm.filePath(entry) + QStringLiteral("/device");
        if (!QFileInfo::exists(devPath)) {
            continue;
        }
        if (seen.contains(devPath)) {
            continue;  // card0/card0-DP-1 等指向同一 device
        }
        seen.insert(devPath);
        Device d;
        const QString vendor = readSysfs(devPath + QStringLiteral("/vendor"));
        const QString deviceId = readSysfs(devPath + QStringLiteral("/device"));
        const QString driver = driverName(devPath);
        const QString name = driver.isEmpty()
            ? QStringLiteral("显示适配器 %1:%2").arg(vendor, deviceId)
            : QStringLiteral("显示适配器（%1）").arg(driver);
        d.name = name;
        d.category = QStringLiteral("gpu");
        d.key = devPath;  // 审查 M3：sysfs 路径作稳定匹配键
        d.status = QStringLiteral("运行正常");
        if (!vendor.isEmpty()) {
            d.props.append({QStringLiteral("厂商 ID"), vendor});
        }
        if (!deviceId.isEmpty()) {
            d.props.append({QStringLiteral("设备 ID"), deviceId});
        }
        if (!driver.isEmpty()) {
            d.props.append({QStringLiteral("驱动"), driver});
        }
        d.props.append({QStringLiteral("Sysfs 路径"), devPath});
        out.append(d);
    }
    return out;
}

QList<Device> scanNetworks() {
    QList<Device> out;
    QDir net(QStringLiteral("/sys/class/net"));
    const QStringList entries = net.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& name : entries) {
        if (name == QLatin1String("lo")) {
            continue;  // 回环接口非硬件
        }
        const QString base = net.filePath(name);
        Device d;
        d.name = name;
        d.category = QStringLiteral("network");
        d.key = name;  // 审查 M3：接口名作稳定匹配键
        const QString operstate = readSysfs(base + QStringLiteral("/operstate"));
        d.status = (operstate == QLatin1String("up"))
            ? QStringLiteral("运行正常") : QStringLiteral("未连接");
        const QString mac = readSysfs(base + QStringLiteral("/address"));
        if (!mac.isEmpty()) {
            d.props.append({QStringLiteral("MAC 地址"), mac});
        }
        const QString speedMbps = readSysfs(base + QStringLiteral("/speed"));
        if (!speedMbps.isEmpty() && speedMbps != QLatin1String("-1")) {
            d.props.append({QStringLiteral("链路速率"),
                QStringLiteral("%1 Mbps").arg(speedMbps)});
        }
        const QString driver = driverName(base + QStringLiteral("/device"));
        if (!driver.isEmpty()) {
            d.props.append({QStringLiteral("驱动"), driver});
        }
        if (d.props.isEmpty()) {
            d.props.append({QStringLiteral("接口"), name});
        }
        out.append(d);
    }
    return out;
}

QList<Device> scanUsb() {
    QList<Device> out;
    QDir usb(QStringLiteral("/sys/bus/usb/devices"));
    const QStringList entries = usb.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& name : entries) {
        const QString base = usb.filePath(name);
        // 审查（G3 轻微）：根 hub（usb1/usb2）**有** idVendor=1d6b 与
        // product 名（对标 Windows 根集线器，正常显示）；接口子目录
        // （1-1:1.0）两者皆空 → 跳过。
        const QString product = readSysfs(base + QStringLiteral("/product"));
        const QString manufacturer =
            readSysfs(base + QStringLiteral("/manufacturer"));
        const QString idVendor = readSysfs(base + QStringLiteral("/idVendor"));
        const QString idProduct = readSysfs(base + QStringLiteral("/idProduct"));
        if (idVendor.isEmpty() && product.isEmpty()) {
            continue;  // 非设备节点（接口子目录等）
        }
        Device d;
        d.name = !product.isEmpty()
            ? product
            : QStringLiteral("USB 设备 %1%2")
                  .arg(idVendor,
                       idProduct.isEmpty() ? QString()
                                           : QStringLiteral(":%1").arg(idProduct));
        d.category = QStringLiteral("usb");
        d.key = name;  // 审查 M3：总线地址作稳定匹配键
        d.status = QStringLiteral("运行正常");
        if (!manufacturer.isEmpty()) {
            d.props.append({QStringLiteral("制造商"), manufacturer});
        }
        if (!idVendor.isEmpty()) {
            d.props.append({QStringLiteral("厂商 ID"), idVendor});
        }
        if (!idProduct.isEmpty()) {
            d.props.append({QStringLiteral("产品 ID"), idProduct});
        }
        const QString speed = readSysfs(base + QStringLiteral("/speed"));
        if (!speed.isEmpty()) {
            d.props.append({QStringLiteral("速率"), speed + QStringLiteral(" Mbps")});
        }
        d.props.append({QStringLiteral("总线地址"), name});
        out.append(d);
    }
    return out;
}

QList<Device> scanPci() {
    QList<Device> out;
    QDir pci(QStringLiteral("/sys/bus/pci/devices"));
    const QStringList entries = pci.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& name : entries) {
        const QString base = pci.filePath(name);
        const QString vendor = readSysfs(base + QStringLiteral("/vendor"));
        const QString deviceId = readSysfs(base + QStringLiteral("/device"));
        if (vendor.isEmpty()) {
            continue;
        }
        const QString driver = driverName(base);
        Device d;
        d.name = QStringLiteral("%1:%2").arg(vendor, deviceId);
        d.category = QStringLiteral("pci");
        d.key = name;  // 审查 M3：PCI 地址作稳定匹配键
        d.status = driver.isEmpty() ? QStringLiteral("无驱动")
                                    : QStringLiteral("运行正常");
        if (!driver.isEmpty()) {
            d.props.append({QStringLiteral("驱动"), driver});
        }
        const QString className = readSysfs(base + QStringLiteral("/class"));
        if (!className.isEmpty()) {
            d.props.append({QStringLiteral("类别"), className});
        }
        d.props.append({QStringLiteral("PCI 地址"), name});
        out.append(d);
    }
    return out;
}

QList<Device> scanInput() {
    QList<Device> out;
    QFile f(QStringLiteral("/proc/bus/input/devices"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }
    // 用索引而非 Device* 指向 QList 元素（append 增长可能 realloc 使指针
    // 悬垂——审查 c158c402 前自查加固）。
    int currentIndex = -1;
    QTextStream ts(&f);
    QString line;
    while (ts.readLineInto(&line)) {
        const QString t = line.trimmed();
        if (t.isEmpty()) {
            currentIndex = -1;
            continue;
        }
        if (t.startsWith(QLatin1Char('I'))) {
            out.append(Device());
            currentIndex = out.size() - 1;
            out[currentIndex].category = QStringLiteral("input");
            out[currentIndex].key = QStringLiteral("input-%1").arg(currentIndex);
            out[currentIndex].name = QStringLiteral("输入设备");
            out[currentIndex].status = QStringLiteral("运行正常");
        } else if (t.startsWith(QLatin1String("N: ")) && currentIndex >= 0) {
            // N: Name="..."（引号内）
            const int q1 = t.indexOf(QLatin1Char('"'));
            const int q2 = t.lastIndexOf(QLatin1Char('"'));
            if (q1 >= 0 && q2 > q1) {
                out[currentIndex].name = t.mid(q1 + 1, q2 - q1 - 1);
            }
        } else if (t.startsWith(QLatin1String("H: ")) && currentIndex >= 0) {
            out[currentIndex].props.append({QStringLiteral("处理程序"),
                                            t.mid(3).trimmed()});
        } else if (t.startsWith(QLatin1String("P: ")) && currentIndex >= 0) {
            out[currentIndex].props.append({QStringLiteral("物理路径"),
                                            t.mid(3).trimmed()});
        }
    }
    f.close();
    // 去除无名称条目。
    QList<Device> filtered;
    for (const Device& d : out) {
        if (d.name != QStringLiteral("输入设备")) {
            filtered.append(d);
        }
    }
    return filtered;
}

QList<Device> scanHardware() {
    QList<Device> categories;
    const auto add = [&categories](const QString& category,
                                   QList<Device> items) {
        Device cat;
        cat.name = categoryLabel(category);
        cat.category = category;
        cat.children = std::move(items);
        if (cat.children.isEmpty()) {
            Device none;
            none.name = QStringLiteral("无设备");
            none.category = category;
            none.key = QStringLiteral("none");
            none.status = QStringLiteral("未检测到（当前环境无对应硬件）");
            cat.children.append(none);
        }
        // 审查（G3 轻微）：move 追加（Qt6 QList COW 下差异小，语义更清晰）。
        categories.append(std::move(cat));
    };
    add(QStringLiteral("cpu"), scanCpu());
    add(QStringLiteral("memory"), scanMemory());
    add(QStringLiteral("disk"), scanDisks());
    add(QStringLiteral("gpu"), scanGpus());
    add(QStringLiteral("network"), scanNetworks());
    add(QStringLiteral("usb"), scanUsb());
    add(QStringLiteral("pci"), scanPci());
    add(QStringLiteral("input"), scanInput());
    return categories;
}

}  // namespace w10dev

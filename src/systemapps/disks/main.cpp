// w10disks —— 磁盘管理入口（可选拓展 E10：只读浏览）。
//
// 用法：w10disks [--selftest] [--render <png>]。
// --selftest：sysfs 扫描 / 挂载表解析 / 大小格式化自测。
// --render：offscreen 渲染窗口到 PNG（验证布局）。

#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QTextStream>
#include <QWidget>

#include <cstdio>

#include <algorithm>

#include "systemapps/appipc.h"
#include "systemapps/disks/diskscanner.h"
#include "systemapps/disks/diskswindow.h"
#include "ipc/config.h"
#include "ipc/theme.h"
#include "theme/colors.h"

namespace {

int runSelfTest() {
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };
    using w10de::disks::formatBytes;
    using w10de::disks::parseMounts;
    using w10de::disks::scanDrives;

    // 1) formatBytes 边界（含审查 L8：舍入与负数分支）。
    if (formatBytes(0) != "0 B" || formatBytes(1023) != "1023 B"
            || formatBytes(1024) != "1 KB" || formatBytes(1536) != "2 KB"
            || formatBytes(1048576) != "1.0 MB"
            || formatBytes(1073741824LL) != "1.0 GB"
            || formatBytes(1099511627776LL) != "1.0 TB"
            || formatBytes(-5) != "-") {
        return fail(QStringLiteral("formatBytes 错误"));
    }
    out << "OK format\n";

    // 1b) 审查 S1/M3：父盘名推导（sd / NVMe / eMMC 命名）。
    {
        using w10de::disks::deriveParent;
        if (deriveParent(QStringLiteral("sda1")) != QStringLiteral("sda")
                || deriveParent(QStringLiteral("nvme0n1p1"))
                    != QStringLiteral("nvme0n1")
                || deriveParent(QStringLiteral("nvme0n1p10"))
                    != QStringLiteral("nvme0n1")
                || deriveParent(QStringLiteral("mmcblk0p1"))
                    != QStringLiteral("mmcblk0")
                || deriveParent(QStringLiteral("sdb"))
                    != QStringLiteral("sdb")) {
            return fail(QStringLiteral("deriveParent 推导错误"));
        }
        out << "OK derive-parent\n";
    }

    // 2) /proc/mounts 解析（注入样本）。
    {
        const QString sample =
            "rootfs / rootfs rw 0 0\n"
            "sysfs /sys sysfs rw,nosuid 0 0\n"
            "/dev/sda1 / ext4 rw,relatime 0 0\n"
            "/dev/sda2 /boot vfat rw,relatime 0 0\n"
            "overlay /overlay overlay rw 0 0\n"
            "//10.0.0.1/share /mnt/share cifs rw 0 0\n";
        const auto mounts = parseMounts(sample);
        if (mounts.size() != 6) {
            return fail(QStringLiteral("挂载条目数错误：%1").arg(mounts.size()));
        }
        // 找 sda1。
        const auto it = std::find_if(
            mounts.begin(), mounts.end(), [](const auto& m) {
                return m.device == QStringLiteral("/dev/sda1"); });
        if (it == mounts.end() || it->mountPoint != QStringLiteral("/")
                || it->fsType != QStringLiteral("ext4")) {
            return fail(QStringLiteral("sda1 挂载解析错误"));
        }
        const auto it2 = std::find_if(
            mounts.begin(), mounts.end(), [](const auto& m) {
                return m.device == QStringLiteral("//10.0.0.1/share"); });
        if (it2 == mounts.end() || it2->mountPoint != QStringLiteral("/mnt/share")
                || it2->fsType != QStringLiteral("cifs")) {
            return fail(QStringLiteral("CIFS 挂载解析错误"));
        }
        out << "OK parse-mounts\n";
    }

    // 3) 真实 sysfs 扫描（WSL 环境：至少 1 块整盘 + 无 loop 设备混入）。
    {
        const auto drives = scanDrives();
        if (drives.isEmpty()) {
            out << "WARN no-drives（当前环境无 /sys/class/block 磁盘）\n";
        } else {
            bool foundRoot = false;
            qint64 totalSize = 0;
            for (const auto& d : drives) {
                if (d.name.startsWith(QStringLiteral("loop"))
                        || d.name.startsWith(QStringLiteral("ram"))) {
                    return fail(QStringLiteral("虚拟设备未被过滤：%1")
                                    .arg(d.name));
                }
                totalSize += d.sizeBytes;
                // 分区挂载点应有根分区。
                for (const auto& p : d.partitions) {
                    if (p.mountPoint == QStringLiteral("/")) {
                        foundRoot = true;
                    }
                }
            }
            if (totalSize <= 0) {
                return fail(QStringLiteral("磁盘大小全为 0"));
            }
            out << "OK scan drives=" << drives.size() << " total="
                << totalSize << " root_found=" << (foundRoot ? 1 : 0) << "\n";
        }
    }
    out << "SELFTEST PASS\n";
    return 0;
}

int runRender(const QString& pngPath) {
    using w10de::disks::DisksWindow;
    DisksWindow window;
    window.show();
    QApplication::processEvents();
    const QPixmap pm = window.grab();
    if (!pm.save(pngPath)) {
        std::fprintf(stderr, "render save failed\n");
        return 1;
    }
    std::printf("RENDER OK %s %dx%d tree=%d details=%d\n",
                qPrintable(pngPath), pm.width(), pm.height(),
                window.treeCount(), window.detailRows());
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QString renderPath;
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            selftest = true;
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--render") == 0 && i + 1 < argc) {
            renderPath = QString::fromLocal8Bit(argv[++i]);
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--help") == 0) {
            std::printf("Usage: w10disks [--selftest] [--render <png>]\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10disks: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (selftest) {
        QApplication app(argc, argv);
        return runSelfTest();
    }
    if (!renderPath.isEmpty()) {
        QApplication app(argc, argv);
        return runRender(renderPath);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10disks"));
    QApplication::setApplicationDisplayName(QStringLiteral("磁盘管理"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (w10de::app::tryActivateExisting(QStringLiteral("Disks"))) {
        return 0;
    }
    w10de::app::registerService(
        QStringLiteral("Disks"),
        [](const QString&) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* dw = qobject_cast<w10de::disks::DisksWindow*>(w)) {
                    dw->show();
                    dw->raise();
                    dw->activateWindow();
                    break;
                }
            }
        },
        &app);

    w10de::disks::DisksWindow window;
    window.show();
    return QApplication::exec();
}

// w10devices —— 设备管理器（WIN10-GAP G3：硬件树 + 设备详情）。
//
// 数据源 sysfs/proc（无外部命令依赖）。headless 验证：WSL 有
// /proc/cpuinfo、/proc/meminfo、/sys/block、/sys/class/net、/sys/bus/pci；
// 无 DRM/USB 时对应类别显示"无设备"（降级）。
//
// 自测：w10devices --selftest 验证扫描函数健壮性与解析正确性
//（offscreen，不建窗口）。

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/devices/devicemodel.h"
#include "systemapps/devices/deviceswindow.h"
#include "ipc/config.h"
#include "ipc/theme.h"
#include "theme/colors.h"

namespace {

// ---- 自测（headless）----
int runSelfTest() {
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };

    // 1) CPU 解析（WSL 必有 /proc/cpuinfo）。
    {
        const QList<w10dev::Device> cpus = w10dev::scanCpu();
        if (cpus.isEmpty()) {
            return fail(QStringLiteral("CPU 扫描为空（/proc/cpuinfo 缺失？）"));
        }
        const w10dev::Device& cpu = cpus.first();
        if (cpu.name.isEmpty() || cpu.props.isEmpty()) {
            return fail(QStringLiteral("CPU 详情为空"));
        }
        bool hasModel = false;
        for (const w10dev::DeviceProperty& p : cpu.props) {
            if (p.key == QStringLiteral("型号") && !p.value.isEmpty()) {
                hasModel = true;
            }
        }
        if (!hasModel) {
            return fail(QStringLiteral("CPU 型号属性缺失"));
        }
        out << "OK cpu: " << cpu.name.left(40) << "\n";
    }
    // 2) 内存（MemTotal 解析）。
    {
        const QList<w10dev::Device> mems = w10dev::scanMemory();
        if (mems.isEmpty()) {
            return fail(QStringLiteral("内存扫描为空"));
        }
        if (!mems.first().props.isEmpty()
                && mems.first().props.first().key != QStringLiteral("总容量")) {
            return fail(QStringLiteral("内存属性顺序不符"));
        }
        out << "OK memory: " << (mems.first().props.isEmpty()
            ? QString() : mems.first().props.first().value) << "\n";
    }
    // 3) 磁盘/网络扫描健壮性（存在或空都不崩溃；有则字段合理）。
    {
        const QList<w10dev::Device> disks = w10dev::scanDisks();
        for (const w10dev::Device& d : disks) {
            for (const w10dev::DeviceProperty& p : d.props) {
                if (p.key == QStringLiteral("容量")) {
                    // 审查 M5（G3）：小盘输出 KB——断言放宽（KB/MB/GB）。
                    if (!p.value.contains(QStringLiteral("KB"))
                            && !p.value.contains(QStringLiteral("MB"))
                            && !p.value.contains(QStringLiteral("GB"))) {
                        return fail(QStringLiteral("磁盘容量格式异常：%1")
                                        .arg(p.value));
                    }
                }
            }
        }
        out << "OK disks: " << disks.size() << "\n";
        const QList<w10dev::Device> nets = w10dev::scanNetworks();
        out << "OK networks: " << nets.size() << "\n";
    }
    // 4) 显卡/USB/PCI/输入健壮性（无设备时不崩溃）。
    {
        const QList<w10dev::Device> gpus = w10dev::scanGpus();
        const QList<w10dev::Device> usbs = w10dev::scanUsb();
        const QList<w10dev::Device> pcis = w10dev::scanPci();
        const QList<w10dev::Device> inputs = w10dev::scanInput();
        out << "OK gpu=" << gpus.size() << " usb=" << usbs.size()
            << " pci=" << pcis.size() << " input=" << inputs.size() << "\n";
    }
    // 5) 全量扫描 + 类别结构（含"无设备"占位）。
    {
        const QList<w10dev::Device> cats = w10dev::scanHardware();
        if (cats.size() != 8) {
            return fail(QStringLiteral("类别数 != 8（%1）").arg(cats.size()));
        }
        for (const w10dev::Device& cat : cats) {
            if (cat.children.isEmpty()) {
                return fail(QStringLiteral("类别 %1 无子项（应含占位）").arg(cat.name));
            }
        }
        // 类别顺序。
        if (cats.first().category != QStringLiteral("cpu")) {
            return fail(QStringLiteral("类别顺序不符（首个应 cpu）"));
        }
        out << "OK hardware-tree: " << cats.size() << " categories\n";
    }
    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            break;
        }
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10devices"));
    QApplication::setApplicationDisplayName(QStringLiteral("设备管理器"));

    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    const QStringList args = QApplication::arguments();
    if (args.contains(QStringLiteral("--selftest"))) {
        return runSelfTest();
    }

    // 单实例（Devices 无路径参数，Activate 仅置前）。
    if (w10de::app::tryActivateExisting(QStringLiteral("Devices"))) {
        return 0;
    }
    if (!w10de::app::registerService(
            QStringLiteral("Devices"),
            [](const QString&) {
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* dw = qobject_cast<w10dev::DevicesWindow*>(w)) {
                        dw->show();
                        dw->raise();
                        dw->activateWindow();
                        break;
                    }
                }
            },
            &app)) {
        return 0;
    }

    w10dev::DevicesWindow window;
    window.show();
    return QApplication::exec();
}

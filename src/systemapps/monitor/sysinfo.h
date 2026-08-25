// sysinfo —— 系统监视器数据源（/proc 采样）。
//
// Win10 任务管理器"性能"页对标：CPU 使用率（总量 + 每核）、内存（含
// swap）、磁盘读写速率、网络收发速率。全部数据源为 /proc（无特权依赖，
// 与 Linux 系统监视器如 GNOME System Monitor 同源）。
//
// 用法：每秒调用一次 sample()；结果经各 getter 读取；历史缓冲（60 点）
// 供曲线绘制。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace w10de::monitor {

// 一次 /proc/stat 的 CPU 计数器（一个核或全部核合计）。
struct CpuCounters {
    unsigned long long user = 0, nice = 0, system = 0, idle = 0,
                       iowait = 0, irq = 0, softirq = 0, steal = 0;
    unsigned long long total() const {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }
    // 忙 = 总 - 空闲（iowait 计入等待 IO 的忙）。
    unsigned long long busy() const { return total() - idle - iowait; }
};

// /proc/meminfo 摘要。
struct MemStats {
    unsigned long long totalKb = 0, availableKb = 0,
                       swapTotalKb = 0, swapFreeKb = 0;
    double usedPercent() const {
        return totalKb ? 100.0 * static_cast<double>(totalKb - availableKb) /
                             static_cast<double>(totalKb)
                       : 0.0;
    }
    double swapUsedPercent() const {
        return swapTotalKb ? 100.0 * static_cast<double>(swapTotalKb - swapFreeKb) /
                                 static_cast<double>(swapTotalKb)
                           : 0.0;
    }
};

struct NetCounters {
    unsigned long long rx = 0, tx = 0;
};
struct DiskCounters {
    unsigned long long readBytes = 0, writeBytes = 0;
};

class SysInfo {
public:
    SysInfo();

    // 采样一次：读 /proc 并与上次采样做增量（速率/使用率）。
    // 首次调用仅建立基准（速率 0）；之后每秒调用即得每秒速率。
    void sample();

    int coreCount() const { return coreCount_; }

    // 本次采样值。
    double cpuTotal() const { return cpuTotal_; }
    const std::vector<double>& cpuPerCore() const { return cpuPerCore_; }
    const MemStats& mem() const { return mem_; }
    double netRxKBps() const { return netRxKBps_; }
    double netTxKBps() const { return netTxKBps_; }
    double diskReadMBps() const { return diskReadMBps_; }
    double diskWriteMBps() const { return diskWriteMBps_; }
    const std::string& netInterface() const { return netInterface_; }
    const std::string& diskDevice() const { return diskDevice_; }

    // 历史（曲线）：最近 kHistory 次采样的总量/每核使用率。
    const std::vector<double>& cpuTotalHistory() const { return cpuTotalHistory_; }
    const std::vector<std::vector<double>>& cpuPerCoreHistory() const {
        return cpuPerCoreHistory_;
    }
    static constexpr int kHistory = 60;

private:
    // 读取 /proc 原始数据（失败返回默认 0/空）。preferred 非空时优先返回
    // 该对象（上次选中，保持增量可比；消失才按累计最大重选）。
    static CpuCounters readCpuTotal();
    static std::vector<CpuCounters> readCpuPerCore();
    static MemStats readMemStats();
    static NetCounters readNetCounters(std::string* iface,
                                       const std::string& preferred = {});
    static DiskCounters readDiskCounters(std::string* device,
                                         const std::string& preferred = {});

    static double ratePerSec(unsigned long long cur, unsigned long long prev,
                             long long dtMs);

    int coreCount_ = 0;
    CpuCounters prevCpuTotal_{};
    std::vector<CpuCounters> prevCpuPerCore_;
    NetCounters prevNet_{};
    DiskCounters prevDisk_{};
    long long prevTimeMs_ = 0;
    bool first_ = true;

    double cpuTotal_ = 0.0;
    std::vector<double> cpuPerCore_;
    std::vector<double> cpuTotalHistory_;
    std::vector<std::vector<double>> cpuPerCoreHistory_;
    MemStats mem_{};
    double netRxKBps_ = 0.0, netTxKBps_ = 0.0;
    double diskReadMBps_ = 0.0, diskWriteMBps_ = 0.0;
    std::string netInterface_, diskDevice_;
};

}  // namespace w10de::monitor

// sysinfo.cpp —— /proc 采样实现（CPU/内存/磁盘/网络）。

#include "systemapps/monitor/sysinfo.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <algorithm>
#include <csignal>
#include <dirent.h>
#include <iterator>
#include <unistd.h>

namespace w10de::monitor {

namespace {

// 逐行读 /proc 文件（失败返回 false）。
bool readLines(const std::string& path, std::vector<std::string>* lines) {
    std::ifstream f(path);
    if (!f) {
        return false;
    }
    std::string line;
    lines->clear();
    while (std::getline(f, line)) {
        lines->push_back(line);
    }
    return true;
}

unsigned long long parseField(const std::string& line, const char* key) {
    // 形如 "MemTotal:       16384000 kB" / "cpu  123 45 ..."
    const std::string prefix(key);
    if (line.rfind(prefix, 0) == 0) {
        const char* p = line.c_str() + prefix.size();
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        return std::strtoull(p, nullptr, 10);
    }
    return 0;
}

// /proc/net/dev 行解析：" eth0: 123 0 0 0 0 0 0 0 456 ..."
// 返回 rx/tx 字节（第 1/9 个数字字段，0 基计数）。
NetCounters parseNetLine(const std::string& line) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return {};
    }
    NetCounters c;
    unsigned long long fields[16] = {};
    int n = 0;
    const char* p = line.c_str() + colon + 1;
    while (*p && n < 16) {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        char* end = nullptr;
        unsigned long long v = std::strtoull(p, &end, 10);
        if (end == p) {
            break;
        }
        fields[n++] = v;
        p = end;
    }
    if (n >= 9) {
        c.rx = fields[0];
        c.tx = fields[8];
    }
    return c;
}

// /proc/diskstats 行解析：" 8       0 sda 123 4 5678 9 10 11 1234 12 0 0 0"
// 字段（1 基）：1=major 2=minor 3=name 4=reads 6=sectors_read
// 10=writes 14=sectors_written（经典 14 字段；新内核可能有 18+ 字段，
// 但扇区计数位置稳定）。
DiskCounters parseDiskLine(const std::string& line, std::string* name) {
    std::istringstream iss(line);
    std::vector<std::string> f;
    std::string tok;
    while (iss >> tok) {
        f.push_back(tok);
    }
    DiskCounters c;
    if (f.size() < 14) {
        return c;
    }
    *name = f[2];
    c.readBytes = std::strtoull(f[5].c_str(), nullptr, 10) * 512;
    c.writeBytes = std::strtoull(f[9].c_str(), nullptr, 10) * 512;
    return c;
}

bool isInterestingInterface(const std::string& name) {
    // 忽略环回/虚拟接口。
    return !name.empty() && name != "lo" &&
           name.rfind("docker", 0) != 0 && name.rfind("veth", 0) != 0 &&
           name.rfind("br-", 0) != 0 && name.rfind("virbr", 0) != 0 &&
           name.rfind("tun", 0) != 0;
}

bool isInterestingDisk(const std::string& name) {
    return name.rfind("loop", 0) != 0 && name.rfind("ram", 0) != 0 &&
           name.rfind("zram", 0) != 0 && name != "sr0";
}

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

SysInfo::SysInfo() {
    const auto perCore = readCpuPerCore();
    coreCount_ = static_cast<int>(perCore.size());
    if (coreCount_ < 1) {
        coreCount_ = 1;
    }
    cpuPerCore_.assign(coreCount_, 0.0);
    cpuPerCoreHistory_.assign(coreCount_, std::vector<double>(1, 0.0));
    prevCpuTotal_ = readCpuTotal();
    prevCpuPerCore_ = readCpuPerCore();
    prevNet_ = readNetCounters(&netInterface_);
    prevDisk_ = readDiskCounters(&diskDevice_);
    prevTimeMs_ = nowMs();
}

CpuCounters SysInfo::readCpuTotal() {
    CpuCounters c;
    std::vector<std::string> lines;
    if (!readLines("/proc/stat", &lines) || lines.empty()) {
        return c;
    }
    std::istringstream iss(lines[0]);  // "cpu  user nice system idle ..."
    std::string tag;
    iss >> tag;
    if (tag != "cpu") {
        return c;
    }
    iss >> c.user >> c.nice >> c.system >> c.idle >> c.iowait >> c.irq >>
        c.softirq >> c.steal;
    return c;
}

std::vector<CpuCounters> SysInfo::readCpuPerCore() {
    std::vector<CpuCounters> out;
    std::vector<std::string> lines;
    if (!readLines("/proc/stat", &lines)) {
        return out;
    }
    for (const auto& line : lines) {
        if (line.rfind("cpu", 0) != 0 || line.size() < 5 ||
            line[3] < '0' || line[3] > '9') {
            continue;  // 只要 cpu0/cpu1...
        }
        std::istringstream iss(line);
        std::string tag;
        CpuCounters c;
        iss >> tag >> c.user >> c.nice >> c.system >> c.idle >> c.iowait >>
            c.irq >> c.softirq >> c.steal;
        out.push_back(c);
    }
    return out;
}

MemStats SysInfo::readMemStats() {
    MemStats m;
    std::vector<std::string> lines;
    if (!readLines("/proc/meminfo", &lines)) {
        return m;
    }
    for (const auto& line : lines) {
        if (unsigned long long v = parseField(line, "MemTotal:")) {
            m.totalKb = v;
        } else if (unsigned long long v = parseField(line, "MemAvailable:")) {
            m.availableKb = v;
        } else if (unsigned long long v = parseField(line, "SwapTotal:")) {
            m.swapTotalKb = v;
        } else if (unsigned long long v = parseField(line, "SwapFree:")) {
            m.swapFreeKb = v;
        }
    }
    if (m.availableKb == 0) {
        // MemAvailable 可能在旧内核缺失：用 MemFree 兜底（审查 M1——
        // 原条件误写 totalKb==0，缺行时可用性恒 0 显示 100% 已用）。
        for (const auto& line : lines) {
            if (unsigned long long v = parseField(line, "MemFree:")) {
                m.availableKb = v;
                break;
            }
        }
    }
    return m;
}

NetCounters SysInfo::readNetCounters(std::string* iface,
                                     const std::string& preferred) {
    NetCounters best;
    std::vector<std::string> lines;
    if (!readLines("/proc/net/dev", &lines)) {
        return best;
    }
    // 审查 M3：优先返回 preferred（上次选中的接口），对象消失才重选——
    // 否则每次采样按累计流量重选会在运行时切换对象，prev/cur 不可比致速率跳变。
    bool preferredSeen = false;
    for (const auto& line : lines) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string name = line.substr(0, colon);
        const size_t first = name.find_first_not_of(" \t");
        if (first != std::string::npos) {
            name = name.substr(first);
        }
        if (!isInterestingInterface(name)) {
            continue;
        }
        const NetCounters c = parseNetLine(line);
        if (!preferred.empty() && name == preferred) {
            *iface = name;
            preferredSeen = true;
            if (c.rx + c.tx > 0) {
                return c;  // 稳定选中对象
            }
            best = c;  // 累计 0 时兜底记录，继续扫描
            continue;
        }
        if (c.rx + c.tx > best.rx + best.tx) {
            best = c;
            *iface = name;
        }
    }
    if (preferredSeen) {
        *iface = preferred;  // preferred 存在但累计 0：保持对象
    }
    return best;
}

DiskCounters SysInfo::readDiskCounters(std::string* device,
                                       const std::string& preferred) {
    DiskCounters best;
    std::vector<std::string> lines;
    if (!readLines("/proc/diskstats", &lines)) {
        return best;
    }
    bool preferredSeen = false;
    for (const auto& line : lines) {
        std::string name;
        const DiskCounters c = parseDiskLine(line, &name);
        if (name.empty() || !isInterestingDisk(name)) {
            continue;
        }
        if (!preferred.empty() && name == preferred) {
            *device = name;
            preferredSeen = true;
            if (c.readBytes + c.writeBytes > 0) {
                return c;
            }
            best = c;
            continue;
        }
        if (c.readBytes + c.writeBytes > best.readBytes + best.writeBytes) {
            best = c;
            *device = name;
        }
    }
    if (preferredSeen) {
        *device = preferred;
    }
    return best;
}

double SysInfo::ratePerSec(unsigned long long cur, unsigned long long prev,
                           long long dtMs) {
    if (dtMs <= 0 || cur < prev) {
        return 0.0;
    }
    return static_cast<double>(cur - prev) * 1000.0 / dtMs;
}

void SysInfo::sample() {
    const long long t = nowMs();
    const long long dt = t - prevTimeMs_;
    prevTimeMs_ = t;

    // ---- CPU ----
    const CpuCounters total = readCpuTotal();
    const std::vector<CpuCounters> perCore = readCpuPerCore();
    if (!first_ && dt > 0) {
        // 审查 M2：无符号下溢守卫——/proc/stat 读取失败（返回全 0）或
        // 计数器回绕时 cur < prev，跳过本次增量（保持上次值）。
        const unsigned long long dTotal =
            total.total() >= prevCpuTotal_.total() ? total.total() - prevCpuTotal_.total() : 0;
        const unsigned long long dBusy =
            total.busy() >= prevCpuTotal_.busy() ? total.busy() - prevCpuTotal_.busy() : 0;
        cpuTotal_ = dTotal ? 100.0 * static_cast<double>(dBusy) / dTotal : 0.0;
        cpuPerCore_.assign(static_cast<size_t>(coreCount_), 0.0);
        for (int i = 0; i < coreCount_; ++i) {
            const size_t idx = static_cast<size_t>(i);
            if (idx < perCore.size() && idx < prevCpuPerCore_.size()) {
                const unsigned long long dt2 =
                    perCore[idx].total() >= prevCpuPerCore_[idx].total()
                        ? perCore[idx].total() - prevCpuPerCore_[idx].total()
                        : 0;
                const unsigned long long db2 =
                    perCore[idx].busy() >= prevCpuPerCore_[idx].busy()
                        ? perCore[idx].busy() - prevCpuPerCore_[idx].busy()
                        : 0;
                cpuPerCore_[idx] = dt2 ? 100.0 * static_cast<double>(db2) / dt2 : 0.0;
            }
        }
    } else {
        cpuTotal_ = 0.0;
        cpuPerCore_.assign(static_cast<size_t>(coreCount_), 0.0);
    }
    prevCpuTotal_ = total;
    prevCpuPerCore_ = perCore;

    // 历史缓冲（滚动）。
    cpuTotalHistory_.push_back(cpuTotal_);
    if (static_cast<int>(cpuTotalHistory_.size()) > kHistory) {
        cpuTotalHistory_.erase(cpuTotalHistory_.begin());
    }
    for (int i = 0; i < coreCount_; ++i) {
        auto& h = cpuPerCoreHistory_[static_cast<size_t>(i)];
        h.push_back(i < static_cast<int>(cpuPerCore_.size()) ? cpuPerCore_[static_cast<size_t>(i)] : 0.0);
        if (static_cast<int>(h.size()) > kHistory) {
            h.erase(h.begin());
        }
    }

    // ---- 内存 ----
    mem_ = readMemStats();

    // ---- 网络 / 磁盘（速率；审查 M3：preferred = 上次选中，对象稳定）----
    NetCounters net = readNetCounters(&netInterface_, netInterface_);
    DiskCounters disk = readDiskCounters(&diskDevice_, diskDevice_);
    if (!first_) {
        netRxKBps_ = ratePerSec(net.rx, prevNet_.rx, dt) / 1024.0;
        netTxKBps_ = ratePerSec(net.tx, prevNet_.tx, dt) / 1024.0;
        diskReadMBps_ = ratePerSec(disk.readBytes, prevDisk_.readBytes, dt) /
                        (1024.0 * 1024.0);
        diskWriteMBps_ = ratePerSec(disk.writeBytes, prevDisk_.writeBytes, dt) /
                         (1024.0 * 1024.0);
    } else {
        netRxKBps_ = netTxKBps_ = 0.0;
        diskReadMBps_ = diskWriteMBps_ = 0.0;
    }
    prevNet_ = net;
    prevDisk_ = disk;
    first_ = false;
}

// ---- 进程管理（KDE-GAP 高优先 #2）----

namespace {

// 解析 /proc/<pid>/stat：返回 name（comm）与 utime+stime ticks。
// comm 可能含空格/括号，从第一个 '(' 到最后一个 ')' 截取。
bool readProcStat(int pid, std::string* name, unsigned long long* cpuTicks,
                  unsigned long long* rssBytes) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    if (!f) {
        return false;
    }
    std::string line;
    std::getline(f, line);
    f.close();
    const size_t open = line.find('(');
    const size_t close = line.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return false;
    }
    if (name != nullptr) {
        *name = line.substr(open + 1, close - open - 1);
    }
    // 括号后字段：state(0) ppid(1) ... utime(11) stime(12) ... rss(21)
    // （原字段 3 起为括号后索引 0：utime 原 14→11、stime 原 15→12、
    // rss 原 24→21。审查 S1：原实现用 13/14/23 取到 cutime/cstime/
    // startcode——CPU% 恒近 0、内存显示地址值）。
    std::istringstream iss(line.substr(close + 1));
    std::vector<std::string> fields;
    std::string tok;
    while (iss >> tok) {
        fields.push_back(tok);
    }
    if (fields.size() < 22) {
        return false;
    }
    if (cpuTicks != nullptr) {
        // 审查 L2：rss 内核侧为 long，极端换出可为负——strtoll 并钳制。
        const long long utime = std::strtoll(fields[11].c_str(), nullptr, 10);
        const long long stime = std::strtoll(fields[12].c_str(), nullptr, 10);
        *cpuTicks = utime > 0 ? static_cast<unsigned long long>(utime) : 0;
        *cpuTicks += stime > 0 ? static_cast<unsigned long long>(stime) : 0;
    }
    if (rssBytes != nullptr) {
        const long long pages = std::strtoll(fields[21].c_str(), nullptr, 10);
        const unsigned long long p = pages > 0 ? static_cast<unsigned long long>(pages) : 0;
        *rssBytes = p * static_cast<unsigned long long>(sysconf(_SC_PAGESIZE));
    }
    return true;
}

std::string readCmdline(int pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/cmdline",
                    std::ios::binary);
    if (!f) {
        return std::string();
    }
    std::string data((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    // NUL 分隔 → 空格连接。
    for (char& ch : data) {
        if (ch == '\0') {
            ch = ' ';
        }
    }
    while (!data.empty() && data.back() == ' ') {
        data.pop_back();
    }
    return data;
}

}  // namespace

std::vector<ProcInfo> SysInfo::processList() {
    std::vector<ProcInfo> out;

    // 总 CPU ticks（相对进程% 基准：100 * dproc / dtotal * coreCount）。
    const CpuCounters total = readCpuTotal();
    const unsigned long long totalTicks = total.total();
    const unsigned long long dTotal = totalTicks >= prevCpuTotalTicks_
        ? totalTicks - prevCpuTotalTicks_ : 0;
    prevCpuTotalTicks_ = totalTicks;

    DIR* dir = opendir("/proc");
    if (dir == nullptr) {
        return out;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') {
            continue;  // 仅数字目录（pid）
        }
        const int pid = std::atoi(ent->d_name);
        ProcInfo info;
        info.pid = pid;
        unsigned long long cpuTicks = 0;
        if (!readProcStat(pid, &info.name, &cpuTicks, &info.rssKB)) {
            continue;  // 进程已退出或权限不足
        }
        info.rssKB /= 1024;
        info.cmdline = readCmdline(pid);
        if (info.cmdline.empty()) {
            info.cmdline = info.name;  // 内核线程
        }

        // CPU%：相对单核（两次采样增量 / 总 CPU 增量 * 核心数）。
        const auto prevIt = prevProcCpu_.find(pid);
        if (prevIt != prevProcCpu_.end() && dTotal > 0 &&
                cpuTicks >= prevIt->second) {
            const unsigned long long dproc = cpuTicks - prevIt->second;
            // 审查 L3：钳制（pid 复用/异常时 dproc 可能超 dTotal，防尖峰）。
            const unsigned long long clamped =
                dproc > dTotal ? dTotal : dproc;
            info.cpuPercent = 100.0 * static_cast<double>(clamped) /
                              static_cast<double>(dTotal) * coreCount_;
        }
        prevProcCpu_[pid] = cpuTicks;
        out.push_back(std::move(info));
    }
    closedir(dir);

    // 审查 M1：清理已退出进程的缓存（防止 map 随 pid 重用无限增长）。
    for (auto it = prevProcCpu_.begin(); it != prevProcCpu_.end();) {
        if (std::find_if(out.begin(), out.end(), [&](const ProcInfo& p) {
                return p.pid == it->first;
            }) == out.end()) {
            it = prevProcCpu_.erase(it);
        } else {
            ++it;
        }
    }

    // 按 CPU% 降序。
    std::sort(out.begin(), out.end(), [](const ProcInfo& a, const ProcInfo& b) {
        return a.cpuPercent > b.cpuPercent;
    });
    return out;
}

bool SysInfo::killProcess(int pid, bool force) {
    if (pid <= 1) {
        return false;  // 保护 init
    }
    return ::kill(pid, force ? SIGKILL : SIGTERM) == 0;
}

}  // namespace w10de::monitor

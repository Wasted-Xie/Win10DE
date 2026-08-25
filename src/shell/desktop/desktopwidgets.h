// desktopwidgets —— 桌面小部件（KDE-GAP 中优先 #7：对标 Plasma 桌面部件
// 的轻量 MVP——时钟 + 系统信息）。
//
// 两个自绘/QLabel 组合叠加在桌面背景层上：
//   - 时钟：右上角（日期 + 时间，QTimer 每秒刷新）
//   - 系统信息：左上角（CPU 占用 + 内存占用，QTimer 每 2 秒刷新，
//     数据源 /proc/stat + /proc/meminfo——与系统监视器同源）
// 配置（[widgets] 段，compositor/shell 共读 config.ini）：
//   [widgets]
//   show_clock = 1      # 时钟小部件（0/1，默认 1）
//   show_sysinfo = 1    # 系统信息小部件（0/1，默认 0）
//
// 已知简化（文档记录）：无拖放/自定义（Plasma 的完整部件框架不在 MVP）；
// 位置固定（时钟右上/系统信息左上）；真机可在此基础上扩展。

#pragma once

#include <QLabel>
#include <QString>
#include <QTimer>

namespace w10de {

struct WidgetsConfig {
    bool showClock = true;
    bool showSysinfo = false;
};

// 从 config.ini 的 [widgets] 段加载（缺省：时钟开、系统信息关）。
WidgetsConfig loadWidgetsConfig(const std::string& configPath);

// 读 /proc/meminfo 内存占用百分比（0-100；失败返回 -1）。
int readMemoryPercent();

// 读 /proc/stat 的 CPU 占用百分比（增量；首次调用返回 0——无历史基线）。
// 基线由调用方持有（多实例互不污染；审查 M3）。
struct CpuBaseline {
    unsigned long long prevTotal = 0;
    unsigned long long prevIdle = 0;
    bool haveBaseline = false;
};
int readCpuPercent(CpuBaseline& baseline);

// 桌面小部件集合（两个 QLabel + 各自刷新 timer）。
class DesktopWidgets : public QObject {
    Q_OBJECT
public:
    explicit DesktopWidgets(QWidget* desktop, const WidgetsConfig& cfg,
                            QObject* parent = nullptr);
    ~DesktopWidgets() override = default;

    // 按桌面尺寸重新定位（resizeEvent 时调用）。
    void reposition(int desktopWidth, int desktopHeight);

    // 小部件可见性（配置变更后调用）。
    void setClockVisible(bool visible);
    void setSysinfoVisible(bool visible);

private:
    void refreshClock();
    void refreshSysinfo();

    QLabel* clockLabel_ = nullptr;
    QLabel* sysinfoLabel_ = nullptr;
    QTimer* clockTimer_ = nullptr;
    QTimer* sysinfoTimer_ = nullptr;
    CpuBaseline cpuBaseline_;  // CPU 增量基线（成员，审查 M3）
    // 部件固定边距。
    static constexpr int kMargin = 16;
};

}  // namespace w10de

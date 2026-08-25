// inputsettings —— 输入设备设置（KDE-GAP 中优先：鼠标/键盘/触摸板）。
//
// [input] 配置段（compositor 与 w10settings 共享）：
//   pointer_speed   指针加速度 [-1.0, 1.0]（libinput accel speed）
//   natural_scroll  自然滚动（0/1；触摸板与鼠标）
//   left_handed     左主键（0/1）
//   tap_to_click    轻触点击（0/1；触摸板）
//   repeat_rate     键盘重复率（字符/秒）
//   repeat_delay    键盘重复延迟（毫秒）
// compositor 启动时读取并应用到 libinput 设备与 wlr_keyboard；
// w10settings 修改后写回并通过 D-Bus 热应用。

#pragma once

#include <string>

namespace w10de::ipc {

struct InputSettings {
    double pointerSpeed = 0.0;   // [-1,1]
    bool naturalScroll = false;
    bool leftHanded = false;
    bool tapToClick = false;
    int repeatRate = 25;         // 字符/秒（wl_keyboard 默认 25）
    int repeatDelay = 600;       // 毫秒（wl_keyboard 默认 600）

    // 从 config.ini 的 [input] 段加载（缺省用内置默认）。
    static InputSettings load(const std::string& configPath);
    // 写回 [input] 段（保留其他段）。
    static bool save(const std::string& configPath, const InputSettings& s);
};

}  // namespace w10de::ipc

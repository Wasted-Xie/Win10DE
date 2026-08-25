// nightlight —— Night Light 夜间色温（KDE-GAP 低优先，对标 KDE Night Color
// 精简版：固定色温 + 手动时间窗）。
//
// [night_light] 配置段（compositor 启动读取，定时检查切换）：
//   enabled = 0/1        总开关（默认 0）
//   temperature = 3500   夜间色温开尔文（1000-8000，默认 3500）
//   start_time = 18:00   夜间开始（HH:MM，24 小时制）
//   end_time = 06:00     夜间结束（HH:MM；跨午夜时间窗）
//
// 实现：Tanner Helland 经典色温→RGB 算法（对 16-bit gamma 表按通道增益
// 缩放）；wlroots 0.19 经 wlr_output_state_set_gamma_lut + commit 应用。
// 已知简化（文档记录）：无自动日出日落（需地理位置/太阳计算）与渐变
// 过渡（KDE 有 1 秒平滑，此处直接切换）。

#pragma once

#include <cstdint>
#include <string>

namespace w10de::ipc {

struct NightLightConfig {
    bool enabled = false;
    int temperature = 3500;  // 夜间色温（K）
    int startMinutes = 18 * 60;  // 18:00
    int endMinutes = 6 * 60;     // 06:00
};

// 从 config.ini 的 [night_light] 段加载（非法值回退默认）。
NightLightConfig loadNightLightConfig(const std::string& configPath);

// 给定当日分钟（0-1439）判断是否处于夜间时间窗（支持跨午夜）。
bool isNightActive(const NightLightConfig& cfg, int dayMinutes);

// 色温 → 16-bit gamma 表（Tanner Helland 算法）。ramp 长度须 ≥ size；
// 输出为 0..0xFFFF 的单调递增表（增益缩放后钳制）。
// 返回 false 表示色温非法（未写表）。
bool buildGammaRamps(int temperature, size_t size,
                     uint16_t* r, uint16_t* g, uint16_t* b);

}  // namespace w10de::ipc

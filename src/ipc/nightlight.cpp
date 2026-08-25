// nightlight.cpp —— Night Light 实现。

#include "ipc/nightlight.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ipc/config.h"

namespace w10de::ipc {

NightLightConfig loadNightLightConfig(const std::string& configPath) {
    NightLightConfig cfg;
    const Config c = Config::load(configPath);
    cfg.enabled = c.getInt("night_light", "enabled", 0) != 0;
    cfg.temperature = c.getInt("night_light", "temperature", 3500);
    if (cfg.temperature < 1000) cfg.temperature = 1000;
    if (cfg.temperature > 8000) cfg.temperature = 8000;
    // start_time/end_time：HH:MM（24h）。严格 5 字符校验——
    // 宽松输入（"2:30"）会因 substr 错位解析错分钟。
    auto parseTime = [](const std::string& v, int fallback) -> int {
        if (v.size() != 5 || v[2] != ':') {
            return fallback;
        }
        // 审查 M1：atoi 对 "1x:3y" 等非数字宽容易解析出意外时间，
        // 先逐字符 isdigit 校验。
        for (std::size_t i = 0; i < 5; ++i) {
            if (i != 2 &&
                    !std::isdigit(static_cast<unsigned char>(v[i]))) {
                return fallback;
            }
        }
        const int h = std::atoi(v.substr(0, 2).c_str());
        const int m = std::atoi(v.substr(3, 2).c_str());
        if (h < 0 || h > 23 || m < 0 || m > 59) {
            return fallback;
        }
        return h * 60 + m;
    };
    cfg.startMinutes = parseTime(c.get("night_light", "start_time", "18:00"),
                                 18 * 60);
    cfg.endMinutes = parseTime(c.get("night_light", "end_time", "06:00"),
                               6 * 60);
    // 审查 M2：start==end 在跨午夜分支下恒真（全天开启），语义未定义——
    // 按非法值回退默认（与其余字段契约一致）。
    if (cfg.startMinutes == cfg.endMinutes) {
        cfg.startMinutes = 18 * 60;
        cfg.endMinutes = 6 * 60;
    }
    return cfg;
}

bool isNightActive(const NightLightConfig& cfg, int dayMinutes) {
    if (!cfg.enabled || dayMinutes < 0 || dayMinutes >= 24 * 60) {
        return false;
    }
    if (cfg.startMinutes < cfg.endMinutes) {
        // 同日内（如 08:00-18:00）。
        return dayMinutes >= cfg.startMinutes && dayMinutes < cfg.endMinutes;
    }
    // 跨午夜（如 18:00-06:00）。
    return dayMinutes >= cfg.startMinutes || dayMinutes < cfg.endMinutes;
}

namespace {

// Tanner Helland 色温 → 单通道 0-255 增益。
double channelGain(int temperature, char channel) {
    const double temp = temperature / 100.0;
    switch (channel) {
    case 'r':
        if (temp <= 66.0) {
            return 255.0;
        }
        return std::max(0.0, std::min(255.0,
            329.698727446 * std::pow(temp - 60.0, -0.1332047592)));
    case 'g':
        if (temp <= 66.0) {
            return std::max(0.0, std::min(255.0,
                99.4708025861 * std::log(temp) - 161.1195681661));
        }
        return std::max(0.0, std::min(255.0,
            288.1221695283 * std::pow(temp - 60.0, -0.0755148492)));
    case 'b':
        if (temp >= 66.0) {
            return 255.0;
        }
        if (temp <= 19.0) {
            return 0.0;
        }
        return std::max(0.0, std::min(255.0,
            138.5177312231 * std::log(temp - 10.0) - 305.0447927307));
    }
    return 255.0;
}

}  // namespace

bool buildGammaRamps(int temperature, size_t size,
                     uint16_t* r, uint16_t* g, uint16_t* b) {
    if (temperature < 1000 || temperature > 8000 ||
            size == 0 || r == nullptr || g == nullptr || b == nullptr) {
        return false;
    }
    const double gainR = channelGain(temperature, 'r') / 255.0;
    const double gainG = channelGain(temperature, 'g') / 255.0;
    const double gainB = channelGain(temperature, 'b') / 255.0;
    // 审查 S2：size==1 时 (size-1)==0 除零 → NaN → uint16 转换 UB（表全 0
    // = 黑屏）。单点表无渐变，直接取满增益。
    const double v = (size == 1) ? 1.0 : 0.0;
    for (size_t i = 0; i < size; ++i) {
        // 线性 ramp 0..0xFFFF 按通道增益缩放（KDE Night Color 同思路）。
        const double s = (size == 1) ? v
                                     : static_cast<double>(i) / (size - 1);
        r[i] = static_cast<uint16_t>(
            std::min(65535.0, s * gainR * 65535.0));
        g[i] = static_cast<uint16_t>(
            std::min(65535.0, s * gainG * 65535.0));
        b[i] = static_cast<uint16_t>(
            std::min(65535.0, s * gainB * 65535.0));
    }
    return true;
}

}  // namespace w10de::ipc

// inputsettings.cpp —— [input] 配置读写实现。

#include "ipc/inputsettings.h"

#include <cmath>  // std::isfinite（审查 M：NaN 穿透钳制）
#include <cstdlib>

#include "ipc/config.h"

namespace w10de::ipc {

InputSettings InputSettings::load(const std::string& configPath) {
    InputSettings s;
    const Config cfg = Config::load(configPath);
    // Config 无 getDouble：get 字符串转 double（缺省回退 0.0）。
    // 审查 M：strtod 必须校验 endptr 与有限性——"nan" 的比较恒 false
    // 会穿透 [-1,1] 钳制成为 NaN 并传播到 libinput/D-Bus/UI；
    // 非数字串（"abc"）静默变 0.0 与 getInt 的严格校验语义不一致。
    const std::string speedStr = cfg.get("input", "pointer_speed", "0.0");
    char* end = nullptr;
    const double parsed = std::strtod(speedStr.c_str(), &end);
    if (end == speedStr.c_str() || end == nullptr || *end != '\0' ||
            !std::isfinite(parsed)) {
        s.pointerSpeed = 0.0;
    } else {
        s.pointerSpeed = parsed;
    }
    if (s.pointerSpeed < -1.0) s.pointerSpeed = -1.0;
    if (s.pointerSpeed > 1.0) s.pointerSpeed = 1.0;
    s.naturalScroll = cfg.getInt("input", "natural_scroll", 0) != 0;
    s.leftHanded = cfg.getInt("input", "left_handed", 0) != 0;
    s.tapToClick = cfg.getInt("input", "tap_to_click", 0) != 0;
    s.repeatRate = cfg.getInt("input", "repeat_rate", 25);
    s.repeatDelay = cfg.getInt("input", "repeat_delay", 600);
    return s;
}

bool InputSettings::save(const std::string& configPath, const InputSettings& s) {
    Config cfg = Config::load(configPath);
    cfg.set("input", "pointer_speed", std::to_string(s.pointerSpeed));
    cfg.set("input", "natural_scroll", s.naturalScroll ? "1" : "0");
    cfg.set("input", "left_handed", s.leftHanded ? "1" : "0");
    cfg.set("input", "tap_to_click", s.tapToClick ? "1" : "0");
    cfg.set("input", "repeat_rate", std::to_string(s.repeatRate));
    cfg.set("input", "repeat_delay", std::to_string(s.repeatDelay));
    return cfg.save(configPath);
}

}  // namespace w10de::ipc

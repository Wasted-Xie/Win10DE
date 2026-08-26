// w10screenshot 捕获核心（wlr-screencopy 客户端，同步单次捕获）。
//
// 从原 main.cpp 提取为可复用接口：CLI（--fullscreen/--region/--window/
// --delay）与交互模式（ScreenshotWindow）共用。纯 C++，无 Qt 依赖。
// 协议时序：capture_output → buffer/flags/buffer_done → copy → ready。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace w10shot {

struct CaptureOptions {
    std::string outputName;  // 空 = 第一个输出
    // 区域裁剪（输出逻辑坐标；0,0 + hasRegion=false = 全屏）。
    bool hasRegion = false;
    int regionX = 0;
    int regionY = 0;
    int regionW = 0;
    int regionH = 0;
};

// 同步捕获：连接 Wayland（WAYLAND_DISPLAY），捕获指定输出，按需裁剪。
// 成功返回 true：rgba 为 RGBA8（w*h*4，裁剪后尺寸），width/height 为
// 输出尺寸。失败返回 false 并填 err（连接失败/协议错误/超时/格式不支持）。
// 捕获期间阻塞（内部 poll 循环，最多 ~5s）。
bool captureOutput(const CaptureOptions& opts, std::vector<uint8_t>* rgba,
                   int* width, int* height, std::string* err);

// 裁剪 RGBA buffer（RGBA8 4 字节/像素，stride = width*4）。
// region 钳制到 buffer 内；结果写入 cropped。返回裁剪后尺寸
//（w<=0 或 h<=0 或越界时返回 0,0 且 cropped 清空）。
void cropRgba(const std::vector<uint8_t>& rgba, int width, int height,
              int x, int y, int w, int h, std::vector<uint8_t>* cropped,
              int* outW, int* outH);

}  // namespace w10shot

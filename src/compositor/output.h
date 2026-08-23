// 输出管理（M0：headless 单输出）
//
// 职责：挂载渲染子系统、注册输出全局、配置自定义模式、
// 帧循环渲染 scene、帧数到达后截图验证并退出。
#pragma once

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
}

#include <string>

namespace w10de {

class Compositor;

class Output {
public:
    Output(Compositor& compositor, wlr_output* output);
    ~Output();

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    wlr_output* wlr() const { return output_; }
    // 初始化是否完整（init_render / scene output 创建失败时为 false）。
    bool isValid() const { return sceneOutput_ != nullptr; }

private:
    // 输出 frame 事件：渲染 scene 一帧，按需截图并终止事件循环。
    void handleFrame();
    // 渲染一帧到输出 state 的 buffer，读回像素写 PNG，并验证中心像素
    // 为背景色（Win10 蓝），作为渲染管线工作的证据。
    bool takeScreenshot(const std::string& path);

    static void handleFrameThunk(wl_listener* listener, void* data);

    Compositor& compositor_;
    wlr_output* output_ = nullptr;
    wlr_scene_output* sceneOutput_ = nullptr;
    wlr_scene_rect* backgroundRect_ = nullptr;  // Win10 蓝背景（每输出一个）
    wl_listener frameListener_ = {};
    int framesRendered_ = 0;
};

}  // namespace w10de

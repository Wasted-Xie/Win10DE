#include "compositor/output.h"
#include "compositor/util.h"

#include <cstring>
#include <vector>

#include <drm_fourcc.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "compositor/server.h"
#include "compositor/seat.h"

namespace w10de {

namespace {

// 截图验证：单色画面时校验中心像素==桌面背景色（主题驱动，见 takeScreenshot）。

// 把 DRM 四字符码格式的一行像素（XRGB8888/ARGB8888/XBGR8888/ABGR8888）
// 转换为 stb 期望的 RGBA 顺序。返回 false 表示格式不支持。
bool convertRowToRgba(const uint8_t* src, int width, uint32_t format, uint8_t* dst) {
    for (int x = 0; x < width; ++x) {
        const uint8_t* p = src + static_cast<size_t>(x) * 4;
        uint8_t r, g, b, a;
        switch (format) {
        // 小端内存字节序：[B,G,R,X]
        case DRM_FORMAT_XRGB8888:
            b = p[0]; g = p[1]; r = p[2]; a = 0xFF; break;
        // [B,G,R,A]
        case DRM_FORMAT_ARGB8888:
            b = p[0]; g = p[1]; r = p[2]; a = p[3]; break;
        // [R,G,B,X]
        case DRM_FORMAT_XBGR8888:
            r = p[0]; g = p[1]; b = p[2]; a = 0xFF; break;
        // [R,G,B,A]
        case DRM_FORMAT_ABGR8888:
            r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;
        default:
            return false;
        }
        dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = a;
        dst += 4;
    }
    return true;
}

}  // namespace

Output::Output(Compositor& compositor, wlr_output* output)
    : compositor_(compositor), output_(output) {
    // 构造中途失败被析构时，wl_list_remove 需作用于已初始化的链表。
    wl_list_init(&frameListener_.link);

    // 把 allocator/renderer 挂到输出上，供渲染与 swapchain 使用。
    if (!wlr_output_init_render(output_, compositor.allocator(), compositor.renderer())) {
        wlr_log(WLR_ERROR, "wlr_output_init_render failed on output '%s'", output_->name);
        return;
    }
    // 输出全局由 wlr_output_layout_add_auto() 自动注册（在其配置完成后）。

    sceneOutput_ = wlr_scene_output_create(compositor.scene(), output_);
    if (sceneOutput_ == nullptr) {
        wlr_log(WLR_ERROR, "wlr_scene_output_create failed on output '%s'", output_->name);
        return;
    }

    // 配置输出：启用 + 自定义模式（--width/--height）+ 缩放 1.0。
    const int width = compositor.options().width;
    const int height = compositor.options().height;
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    wlr_output_state_set_custom_mode(&state, width, height, 0);
    wlr_output_state_set_scale(&state, 1.0f);
    if (!wlr_output_test_state(output_, &state)) {
        wlr_log(WLR_ERROR, "output '%s' rejected state %dx%d", output_->name, width, height);
        wlr_output_state_finish(&state);
        // 构造失败路径：销毁已创建的 scene output，避免泄漏
        // （正常路径的 sceneOutput_ 由 wlr_scene_destroy 统一回收）。
        wlr_scene_output_destroy(sceneOutput_);
        sceneOutput_ = nullptr;
        return;
    }
    if (!wlr_output_commit_state(output_, &state)) {
        wlr_log(WLR_ERROR, "failed to commit output state on '%s'", output_->name);
    }
    wlr_output_state_finish(&state);

    // 加入输出布局（自动放置），并显式关联 scene output 与布局位置
    // （wlr_scene_attach_output_layout 不会自动关联已有 scene output）。
    struct wlr_output_layout_output* layoutOutput =
        wlr_output_layout_add_auto(compositor.outputLayout(), output_);
    if (layoutOutput != nullptr && compositor.sceneOutputLayout() != nullptr) {
        wlr_scene_output_layout_add_output(
            compositor.sceneOutputLayout(), layoutOutput, sceneOutput_);
    }
    int lx = 0, ly = 0;
    if (layoutOutput != nullptr) {
        lx = layoutOutput->x;
        ly = layoutOutput->y;
    }

    // 背景矩形铺满输出实际分辨率（Win10 蓝 #0078D7 默认；主题驱动）。
    // 注意：不能用 options 的固定尺寸（DRM 后端输出分辨率由系统决定）。
    // 挂到 backgroundAnchor：位于所有窗口之下。
    int bgW = 0, bgH = 0;
    wlr_output_effective_resolution(output_, &bgW, &bgH);
    if (bgW < 1) bgW = 1;
    if (bgH < 1) bgH = 1;
    const ThemeColor& bg = compositor.theme().desktopBg;
    const float bgColor[4] = {bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f, 1.0f};
    backgroundRect_ = wlr_scene_rect_create(compositor.backgroundAnchor(), bgW, bgH, bgColor);
    if (backgroundRect_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create background rect on '%s'", output_->name);
        wlr_scene_output_destroy(sceneOutput_);
        sceneOutput_ = nullptr;
        return;
    }
    wlr_scene_node_set_position(&backgroundRect_->node, lx, ly);

    frameListener_.notify = handleFrameThunk;
    wl_signal_add(&output_->events.frame, &frameListener_);

    // 输出 commit 监听：mode/scale/位置热应用后同步背景矩形（审查 M1——
    // 否则 SetMode 放大分辨率/SetPosition 移动后背景矩形小于输出，露出
    // 未绘制区域；scene 输出尺寸/位置自动跟随，但背景矩形需手动更新）。
    commitListener_.notify = handleCommitThunk;
    wl_signal_add(&output_->events.commit, &commitListener_);

    wlr_log(WLR_INFO, "output '%s' configured: %dx%d at (%d,%d)", output_->name,
            output_->width, output_->height, lx, ly);
}

Output::~Output() {
    wl_list_remove(&frameListener_.link);
    wl_list_remove(&commitListener_.link);
}

void Output::handleFrameThunk(wl_listener* listener, void* /*data*/) {
    auto* output = W10DE_CONTAINER_OF(listener, Output, frameListener_);
    output->handleFrame();
}

void Output::handleCommitThunk(wl_listener* listener, void* /*data*/) {
    auto* output = W10DE_CONTAINER_OF(listener, Output, commitListener_);
    output->handleCommit();
}

void Output::handleCommit() {
    if (backgroundRect_ == nullptr) {
        return;
    }
    // 背景矩形铺满新有效分辨率，位置跟随布局坐标。
    int w = 0, h = 0;
    wlr_output_effective_resolution(output_, &w, &h);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    wlr_scene_rect_set_size(backgroundRect_, w, h);
    wlr_box box{};
    wlr_output_layout_get_box(compositor_.outputLayout(), output_, &box);
    if (box.width != 0 || box.height != 0) {
        wlr_scene_node_set_position(&backgroundRect_->node, box.x, box.y);
    }
}

void Output::handleFrame() {
    // 渲染并提交 scene 到输出。
    if (!wlr_scene_output_commit(sceneOutput_, nullptr)) {
        wlr_log(WLR_ERROR, "wlr_scene_output_commit failed on '%s'", output_->name);
    }
    ++framesRendered_;
    wlr_log(WLR_DEBUG, "frame %d rendered on '%s'", framesRendered_, output_->name);

    // M7 续：headless 验证用定时工作区切换（--switch-ws f:n，按帧序应用）。
    for (const auto& [frame, workspace] : compositor_.options().workspaceSwitches) {
        if (framesRendered_ == frame) {
            compositor_.switchWorkspace(workspace);
        }
    }
    // Alt+Tab 验证：指定帧显示切换器（headless 截图验证 UI）。
    if (compositor_.options().alttabTestFrame > 0 &&
            framesRendered_ == compositor_.options().alttabTestFrame &&
            compositor_.seat() != nullptr) {
        compositor_.seat()->debugShowAltTab();
    }
    // Snap 布局验证（KDE-GAP #3）：指定帧显示布局选择器（headless 截图验证 UI）。
    static bool snaplayoutTestFired = false;
    if (compositor_.options().snaplayoutTestFrame > 0 &&
            framesRendered_ == compositor_.options().snaplayoutTestFrame &&
            !snaplayoutTestFired && compositor_.seat() != nullptr) {
        snaplayoutTestFired = true;
        compositor_.seat()->debugShowSnapLayout();
    }
    // 剪贴板历史验证：指定帧触发 Win+V 面板（headless 截图验证 UI）。
    // 多输出时各输出帧号独立，用进程级标志保证只触发一次（审查 L5）。
    static bool clipboardTestFired = false;
    if (compositor_.options().clipboardTestFrame > 0 &&
            framesRendered_ == compositor_.options().clipboardTestFrame &&
            !clipboardTestFired) {
        clipboardTestFired = true;
        compositor_.toggleClipboardHistory();
    }
    // M8：推进窗口动画一帧（Snap/还原平滑移动）。
    // 审查：仅第一个输出推进，多输出时动画速度不随输出数翻倍
    //（tickAnimations 遍历全部窗口，每输出一次即整体推进一次）。
    if (compositor_.firstOutput() == output_) {
        compositor_.tickAnimations();
    }

    const int limit = compositor_.options().frames;
    if (limit > 0 && framesRendered_ >= limit) {
        // 审查 M3（多显示器排列）：每输出独立计帧，若各自执行截图会
        // 竞态双写同一路径 + 重复 terminate——仅第一个输出截图并终止。
        if (compositor_.firstOutput() == output_) {
            int exitCode = 0;
            if (!compositor_.options().screenshotPath.empty()) {
                if (!takeScreenshot(compositor_.options().screenshotPath)) {
                    exitCode = 1;
                }
            }
            compositor_.setExitCode(exitCode);
            wl_display_terminate(compositor_.display());
        } else {
            // 非首个输出达限：继续排帧等待主输出 terminate（terminate
            // 后事件循环退出，本输出即停）。
            wlr_output_schedule_frame(output_);
        }
        return;
    }
    // 请求下一帧：headless 后端无垂直同步（frame timer 仅在 enable commit
    // 时续期），须显式 schedule_frame（内部用 idle timer 补帧）——否则
    // 渲染停在第二帧（真实编译验证）。
    wlr_output_schedule_frame(output_);
}

bool Output::takeScreenshot(const std::string& path) {
    // 用 scene 渲染一帧到输出 state 的 buffer（复用输出 swapchain），
    // 再从 buffer 读回像素 —— 避免手动创建 buffer / 依赖 renderer read_pixels。
    //
    // 限制：wlr_buffer_begin_data_ptr_access 仅对支持 DATA_PTR 的缓冲区可用
    // （headless + 软件 allocator 下成立）；DRM/gbm 后端的 DMA-BUF 不支持，
    // 该路径会失败并返回 false。M0 冒烟验证仅在 headless 下进行。
    wlr_output_state state;
    wlr_output_state_init(&state);
    if (!wlr_scene_output_build_state(sceneOutput_, &state, nullptr)) {
        wlr_log(WLR_ERROR, "wlr_scene_output_build_state failed on '%s'", output_->name);
        wlr_output_state_finish(&state);
        return false;
    }
    if (state.buffer == nullptr) {
        wlr_log(WLR_ERROR, "screenshot: no buffer produced for '%s'", output_->name);
        wlr_output_state_finish(&state);
        return false;
    }

    void* data = nullptr;
    uint32_t format = 0;
    size_t stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(state.buffer,
            WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format, &stride)) {
        wlr_log(WLR_ERROR, "screenshot: buffer data ptr access unsupported on '%s'",
                output_->name);
        wlr_output_state_finish(&state);
        return false;
    }

    const int width = state.buffer->width;
    const int height = state.buffer->height;
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);

    bool ok = true;
    const auto* src = static_cast<const uint8_t*>(data);
    for (int y = 0; y < height && ok; ++y) {
        ok = convertRowToRgba(src + static_cast<size_t>(y) * stride,
                              width, format, rgba.data() + static_cast<size_t>(y) * width * 4);
    }
    wlr_buffer_end_data_ptr_access(state.buffer);
    wlr_output_state_finish(&state);

    if (!ok) {
        wlr_log(WLR_ERROR, "screenshot: unsupported pixel format 0x%08x", format);
        return false;
    }

    if (stbi_write_png(path.c_str(), width, height, 4, rgba.data(), width * 4) == 0) {
        wlr_log(WLR_ERROR, "failed to write screenshot to '%s'", path.c_str());
        return false;
    }
    wlr_log(WLR_INFO, "screenshot saved to '%s' (%dx%d, format 0x%08x)",
            path.c_str(), width, height, format);

    // 验证渲染输出（渲染管线工作的证据）：
    // - 画面存在多种颜色（有 layer surface/窗口内容渲染，如壁纸渐变+
    //   任务栏）→ 渲染成功；
    // - 全为单一颜色（M0 纯背景冒烟场景）→ 校验中心等于预期背景色。
    // （原实现只校验中心==背景色，完整桌面渲染时中心被壁纸渐变覆盖，
    //   被误判失败——真实运行验证 center=#0073CD。）
    const size_t center = (static_cast<size_t>(height / 2) * width + width / 2) * 4;
    bool multiColor = false;
    uint8_t firstR = rgba[0], firstG = rgba[1], firstB = rgba[2];
    for (size_t i = 0; i < static_cast<size_t>(width) * height * 4; i += 64) {
        if (rgba[i] != firstR || rgba[i + 1] != firstG || rgba[i + 2] != firstB) {
            multiColor = true;
            break;
        }
    }
    if (multiColor) {
        wlr_log(WLR_INFO, "pixel verification passed (content rendered, "
                "center = #%02X%02X%02X)",
                rgba[center], rgba[center + 1], rgba[center + 2]);
        return true;
    }
    // 纯色画面：校验中心 == 桌面背景色（主题驱动）。
    const ThemeColor& bg = compositor_.theme().desktopBg;
    if (rgba[center] == bg.r && rgba[center + 1] == bg.g &&
            rgba[center + 2] == bg.b) {
        wlr_log(WLR_INFO, "pixel verification passed (solid background, "
                "center = #%02X%02X%02X)",
                rgba[center], rgba[center + 1], rgba[center + 2]);
        return true;
    }
    wlr_log(WLR_ERROR, "pixel verification FAILED: center = #%02X%02X%02X, expected #%02X%02X%02X",
            rgba[center], rgba[center + 1], rgba[center + 2],
            bg.r, bg.g, bg.b);
    return false;
}

}  // namespace w10de

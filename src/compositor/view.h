// 视图：一个 xdg-toplevel 窗口（M1：浮动窗口管理基础）
//
// 职责：scene 节点管理、map/unmap、窗口状态（最大化/最小化）、
// 窗口请求（移动/缩放/关闭）转发给 Seat，标题与应用 id 跟踪（M2 标题栏用）。
#pragma once

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
}

namespace w10de {

class Compositor;
class Seat;

// 标题栏装饰区域（M2a：SSD 标题栏）。
enum class DecorationArea {
    None,          // 内容区（客户端 surface）
    TitleBar,      // 标题栏空白区（可拖动）
    MinButton,     // 最小化按钮
    MaxButton,     // 最大化/还原按钮
    CloseButton,   // 关闭按钮
};

class View {
public:
    // Win10 风格标题栏尺寸（逻辑像素）。
    static constexpr int kTitleBarHeight = 32;
    static constexpr int kButtonWidth = 46;

    View(Compositor& compositor, wlr_xdg_toplevel* toplevel);
    ~View();

    View(const View&) = delete;
    View& operator=(const View&) = delete;

    // ---- 状态 ----
    bool mapped() const { return mapped_; }
    int x() const { return x_; }
    int y() const { return y_; }
    // 当前窗口几何（逻辑坐标）。
    int width() const;
    int height() const;
    bool contains(double lx, double ly) const;

    // 最大化前几何（取消最大化时恢复）。
    void setRestoreGeometry(int x, int y, int w, int h);
    bool hasRestoreGeometry() const { return hasRestoreGeometry_; }
    void restoreGeometry(int* x, int* y, int* w, int* h) const;

    wlr_xdg_toplevel* toplevel() const { return toplevel_; }
    wlr_scene_tree* sceneTree() const { return sceneTree_; }
    wlr_scene_tree* decorationTree() const { return decorationTree_; }
    const char* title() const { return toplevel_->title; }
    const char* appId() const { return toplevel_->app_id; }

    // 装饰区域命中检测（lx, ly 为布局坐标）。标题栏在内容区上方。
    DecorationArea decorationAt(double lx, double ly) const;

    // ---- 窗口操作（由 Seat / 未来任务栏调用）----
    void setActivated(bool activated);
    void close();      // 请求客户端关闭
    void setMaximized(bool maximize);
    bool maximized() const { return maximized_; }
    void setMinimized(bool minimize);
    bool minimized() const { return minimized_; }
    void moveTo(int x, int y);
    void resize(int width, int height);  // 请求新尺寸（客户端可能拒绝）

private:
    // ---- wlroots 事件回调 ----
    static void handleMap(wl_listener* l, void* data);
    static void handleUnmap(wl_listener* l, void* data);
    static void handleDestroy(wl_listener* l, void* data);
    static void handleCommit(wl_listener* l, void* data);
    static void handleRequestMove(wl_listener* l, void* data);
    static void handleRequestResize(wl_listener* l, void* data);
    static void handleRequestMaximize(wl_listener* l, void* data);
    static void handleRequestMinimize(wl_listener* l, void* data);
    static void handleRequestFullscreen(wl_listener* l, void* data);
    static void handleSetTitle(wl_listener* l, void* data);
    static void handleSetAppId(wl_listener* l, void* data);

    // 创建/更新 SSD 标题栏装饰（宽度跟随内容几何）。
    void createDecoration();
    void updateDecoration();
    // 装饰在布局中的坐标（标题栏左上角）。
    int decorationX() const { return x_; }
    int decorationY() const { return y_; }

    // ---- foreign-toplevel（任务栏窗口列表协议）----
    void createForeignToplevel();
    void destroyForeignToplevel();
    static void handleFtlMaximize(wl_listener* l, void* data);
    static void handleFtlMinimize(wl_listener* l, void* data);
    static void handleFtlActivate(wl_listener* l, void* data);
    static void handleFtlFullscreen(wl_listener* l, void* data);
    static void handleFtlClose(wl_listener* l, void* data);
    static void handleFtlDestroy(wl_listener* l, void* data);

    Compositor& compositor_;
    wlr_xdg_toplevel* toplevel_ = nullptr;
    wlr_scene_tree* sceneTree_ = nullptr;  // 内容区 scene 节点
    bool mapped_ = false;
    int x_ = 0, y_ = 0;
    bool maximized_ = false;
    bool minimized_ = false;
    bool positionInitialized_ = false;  // 首次 map 已设初始位置（remap 不重置）
    bool hasRestoreGeometry_ = false;   // 是否保存了最大化前几何
    int restoreX_ = 0, restoreY_ = 0, restoreW_ = 0, restoreH_ = 0;

    // ---- SSD 装饰节点（scene 根上，位于内容区上方）----
    wlr_scene_tree* decorationTree_ = nullptr;
    wlr_scene_rect* titleBarRect_ = nullptr;
    wlr_scene_rect* minButtonRect_ = nullptr;
    wlr_scene_rect* maxButtonRect_ = nullptr;
    wlr_scene_rect* closeButtonRect_ = nullptr;

    // ---- foreign-toplevel handle（任务栏协议）----
    wlr_foreign_toplevel_handle_v1* ftHandle_ = nullptr;
    wl_listener ftMaximize_ = {}, ftMinimize_ = {}, ftActivate_ = {};
    wl_listener ftFullscreen_ = {}, ftClose_ = {}, ftDestroy_ = {};

    wl_listener map_ = {}, unmap_ = {}, destroy_ = {}, commit_ = {};
    wl_listener requestMove_ = {}, requestResize_ = {}, requestMaximize_ = {};
    wl_listener requestMinimize_ = {}, requestFullscreen_ = {};
    wl_listener setTitle_ = {}, setAppId_ = {};
};

}  // namespace w10de

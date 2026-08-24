// XWayland 窗口（X11 客户端兼容，M7）。
//
// M7 续：XView 增加 SSD 服务端装饰（与 xdg View 同款标题栏：Win10 深灰
// 标题栏 + 最小化/最大化/关闭按钮 + 标题文字）与 foreign-toplevel handle
//（任务栏窗口列表协议，与 xdg 窗口统一显示）。
#pragma once

extern "C" {
#include <wayland-server-core.h>
extern "C" {
// wlroots headers lack extern "C" guards; required in C++.
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/xwayland.h>
#include <wlr/util/log.h>
}  // extern "C" (wlroots)
}

#include "compositor/shadow.h"
#include "compositor/titletext.h"
#include "compositor/view.h"  // DecorationArea 枚举（与 xdg View 共用）

namespace w10de {

class Compositor;
class Seat;

// 与 xdg View 共享的标题栏装饰区域定义（View::DecorationArea 同值）。
// 复用 View 的枚举，避免两套定义漂移。
class XView {
public:
    XView(Compositor& compositor, wlr_xwayland_surface* xsurface);
    ~XView();

    XView(const XView&) = delete;
    XView& operator=(const XView&) = delete;

    bool mapped() const { return mapped_; }
    int x() const { return x_; }
    int y() const { return y_; }
    // 内容区尺寸（不含标题栏装饰）。
    int width() const { return xsurface_->width; }
    int height() const { return xsurface_->height; }
    bool contains(double lx, double ly) const;

    wlr_xwayland_surface* xsurface() const { return xsurface_; }
    wlr_surface* surface() const { return xsurface_->surface; }
    wlr_scene_surface* sceneSurface() const { return sceneSurface_; }
    const char* title() const { return xsurface_->title; }

    // 装饰区域命中检测（与 View::decorationAt 同语义；标题栏在内容上方）。
    DecorationArea decorationAt(double lx, double ly) const;
    // M2b hover：设置当前悬停的装饰区域（Seat 调用）。
    void setHoverArea(DecorationArea area);
    DecorationArea hoverArea() const { return hoverArea_; }

    // 窗口操作（任务栏/激活）。
    void activate(bool on);
    void close();
    void setMaximized(bool on);
    bool maximized() const { return maximized_; }
    void setMinimized(bool on);
    void moveTo(int x, int y);

    // ---- 多工作区（M7 续）----
    // 所属工作区（0..kWorkspaceCount-1）。
    int workspace() const { return workspace_; }
    void setWorkspace(int workspace);
    // 按 工作区 + mapped + minimized 刷新 scene 节点显隐。
    void applyVisibility();
    bool minimized() const { return minimized_; }

    // ---- M8 窗口动画（帧插值，与 View 同款）----
    void animateMoveTo(int x, int y);
    void tickAnimation();
    void cancelAnimation();
    bool animating() const { return animActive_; }

    // 内容区 scene 节点（Seat 命中/置顶用）。
    wlr_scene_tree* decorationTree() const { return decorationTree_; }

private:
    static void handleAssociate(wl_listener* l, void* data);
    static void handleDissociate(wl_listener* l, void* data);
    static void handleMap(wl_listener* l, void* data);
    static void handleUnmap(wl_listener* l, void* data);
    static void handleDestroy(wl_listener* l, void* data);
    static void handleRequestActivate(wl_listener* l, void* data);
    static void handleRequestClose(wl_listener* l, void* data);
    static void handleRequestConfigure(wl_listener* l, void* data);
    static void handleRequestMaximize(wl_listener* l, void* data);
    static void handleRequestMinimize(wl_listener* l, void* data);
    static void handleSetTitle(wl_listener* l, void* data);
    static void handleSetClass(wl_listener* l, void* data);
    static void handleSetOverrideRedirect(wl_listener* l, void* data);
    static void handleCommit(wl_listener* l, void* data);

    // SSD 装饰（M7 续，与 View 同款）。
    void createDecoration();
    void updateDecoration();
    // M2b：渲染标题文字（cairo/pango → scene buffer）。
    void renderTitle();
    // M8：渲染/更新窗口阴影。
    void updateShadow();

    // ---- foreign-toplevel（任务栏窗口列表协议，M7 续）----
    void createForeignToplevel();
    void destroyForeignToplevel();
    static void handleFtlMaximize(wl_listener* l, void* data);
    static void handleFtlMinimize(wl_listener* l, void* data);
    static void handleFtlActivate(wl_listener* l, void* data);
    static void handleFtlFullscreen(wl_listener* l, void* data);
    static void handleFtlClose(wl_listener* l, void* data);
    static void handleFtlDestroy(wl_listener* l, void* data);

    Compositor& compositor_;
    wlr_xwayland_surface* xsurface_ = nullptr;
    wlr_scene_surface* sceneSurface_ = nullptr;
    bool mapped_ = false;
    bool minimized_ = false;
    bool maximized_ = false;
    // 是否带 SSD 装饰（override-redirect 窗口无装饰/无任务栏条目，审查 #8）。
    bool hasDecoration_ = true;
    // 所属工作区（M7 续）；X11 窗口创建时归属当前工作区。
    int workspace_ = 0;
    // M8 动画状态（位置插值；动画期间用户拖动会取消）。
    bool animActive_ = false;
    double animFromX_ = 0, animFromY_ = 0;
    double animToX_ = 0, animToY_ = 0;
    float animT_ = 0.0f;
    int x_ = 0, y_ = 0;

    // ---- SSD 装饰节点（scene 根上，位于内容区上方）----
    wlr_scene_tree* decorationTree_ = nullptr;
    wlr_scene_rect* titleBarRect_ = nullptr;
    wlr_scene_rect* minButtonRect_ = nullptr;
    wlr_scene_rect* maxButtonRect_ = nullptr;
    wlr_scene_rect* closeButtonRect_ = nullptr;
    // M2b 标题文字（scene buffer + cairo/pango 渲染）。
    wlr_scene_buffer* titleTextNode_ = nullptr;
    TitleTextBuffer* titleText_ = nullptr;  // 当前渲染的 buffer（owned）
    // M8 窗口阴影（scene buffer + 自绘渐变；装饰树最底层子节点）。
    wlr_scene_buffer* shadowNode_ = nullptr;
    ShadowBuffer* shadow_ = nullptr;  // 当前阴影 buffer（owned）
    // M2b hover 打磨：当前悬停的装饰区域。
    DecorationArea hoverArea_ = DecorationArea::None;

    // ---- foreign-toplevel handle（任务栏协议）----
    wlr_foreign_toplevel_handle_v1* ftHandle_ = nullptr;
    wl_listener ftMaximize_ = {}, ftMinimize_ = {}, ftActivate_ = {};
    wl_listener ftFullscreen_ = {}, ftClose_ = {}, ftDestroy_ = {};

    wl_listener associate_ = {}, dissociate_ = {}, map_ = {}, unmap_ = {}, destroy_ = {};
    wl_listener requestActivate_ = {}, requestClose_ = {}, requestConfigure_ = {};
    wl_listener requestMaximize_ = {}, requestMinimize_ = {}, setTitle_ = {};
    wl_listener setClass_ = {}, setOverrideRedirect_ = {};
    wl_listener commit_ = {};
};

}  // namespace w10de

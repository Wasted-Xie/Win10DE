// XWayland 窗口（X11 客户端兼容，M7）。
//
// 基础版：map/unmap、激活/关闭/最大化/最小化、任务栏集成（走 View 的
// foreign-toplevel 机制之外，直接由 compositor 列表管理）。
// 服务端装饰（SSD）与拖动在后续里程碑统一（与 xdg View 共用装饰逻辑）。
#pragma once

extern "C" {
#include <wayland-server-core.h>
extern "C" {
// wlroots headers lack extern "C" guards; required in C++.
#include <wlr/types/wlr_scene.h>
#include <wlr/xwayland.h>
#include <wlr/util/log.h>
}  // extern "C" (wlroots)
}

namespace w10de {

class Compositor;
class Seat;

class XView {
public:
    XView(Compositor& compositor, wlr_xwayland_surface* xsurface);
    ~XView();

    XView(const XView&) = delete;
    XView& operator=(const XView&) = delete;

    bool mapped() const { return mapped_; }
    int x() const { return x_; }
    int y() const { return y_; }
    int width() const { return xsurface_->width; }
    int height() const { return xsurface_->height; }
    bool contains(double lx, double ly) const;

    wlr_xwayland_surface* xsurface() const { return xsurface_; }
    wlr_surface* surface() const { return xsurface_->surface; }
    wlr_scene_surface* sceneSurface() const { return sceneSurface_; }
    const char* title() const { return xsurface_->title; }

    // 窗口操作（任务栏/激活）。
    void activate(bool on);
    void close();
    void setMaximized(bool on);
    void setMinimized(bool on);
    void moveTo(int x, int y);

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

    Compositor& compositor_;
    wlr_xwayland_surface* xsurface_ = nullptr;
    wlr_scene_surface* sceneSurface_ = nullptr;
    bool mapped_ = false;
    int x_ = 0, y_ = 0;

    wl_listener associate_ = {}, dissociate_ = {}, map_ = {}, unmap_ = {}, destroy_ = {};
    wl_listener requestActivate_ = {}, requestClose_ = {}, requestConfigure_ = {};
    wl_listener requestMaximize_ = {}, requestMinimize_ = {}, setTitle_ = {};
};

}  // namespace w10de

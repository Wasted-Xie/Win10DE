#include "compositor/xview.h"

#include "compositor/seat.h"
#include "compositor/server.h"

namespace w10de {

XView::XView(Compositor& compositor, wlr_xwayland_surface* xsurface)
    : compositor_(compositor), xsurface_(xsurface) {
    // 先初始化全部 listener：构造中途失败被 delete 时 remove 安全。
    wl_list_init(&associate_.link);
    wl_list_init(&dissociate_.link);
    wl_list_init(&map_.link);
    wl_list_init(&unmap_.link);
    wl_list_init(&destroy_.link);
    wl_list_init(&requestActivate_.link);
    wl_list_init(&requestClose_.link);
    wl_list_init(&requestConfigure_.link);
    wl_list_init(&requestMaximize_.link);
    wl_list_init(&requestMinimize_.link);
    wl_list_init(&setTitle_.link);

    // 事件：surface 在 associate 事件后才有效（new_surface 时可能为 NULL）。
    associate_.notify = handleAssociate;
    wl_signal_add(&xsurface->events.associate, &associate_);
    dissociate_.notify = handleDissociate;
    wl_signal_add(&xsurface->events.dissociate, &dissociate_);
    destroy_.notify = handleDestroy;
    wl_signal_add(&xsurface->events.destroy, &destroy_);
    requestActivate_.notify = handleRequestActivate;
    wl_signal_add(&xsurface->events.request_activate, &requestActivate_);
    requestClose_.notify = handleRequestClose;
    wl_signal_add(&xsurface->events.request_close, &requestClose_);
    requestConfigure_.notify = handleRequestConfigure;
    wl_signal_add(&xsurface->events.request_configure, &requestConfigure_);
    requestMaximize_.notify = handleRequestMaximize;
    wl_signal_add(&xsurface->events.request_maximize, &requestMaximize_);
    requestMinimize_.notify = handleRequestMinimize;
    wl_signal_add(&xsurface->events.request_minimize, &requestMinimize_);
    setTitle_.notify = handleSetTitle;
    wl_signal_add(&xsurface->events.set_title, &setTitle_);

    wlr_log(WLR_INFO, "new xwayland surface (window 0x%x)", xsurface_->window_id);
}

XView::~XView() {
    compositor_.removeXView(this);
    wl_list_remove(&associate_.link);
    wl_list_remove(&dissociate_.link);
    wl_list_remove(&map_.link);
    wl_list_remove(&unmap_.link);
    wl_list_remove(&destroy_.link);
    wl_list_remove(&requestActivate_.link);
    wl_list_remove(&requestClose_.link);
    wl_list_remove(&requestConfigure_.link);
    wl_list_remove(&requestMaximize_.link);
    wl_list_remove(&requestMinimize_.link);
    wl_list_remove(&setTitle_.link);
}

bool XView::contains(double lx, double ly) const {
    return lx >= x_ && lx < x_ + width() && ly >= y_ && ly < y_ + height();
}

void XView::activate(bool on) {
    wlr_xwayland_surface_activate(xsurface_, on);
    if (on && mapped_ && surface() != nullptr) {
        compositor_.seat()->focusSurface(surface(), true);
        compositor_.raiseXView(this);
    }
}

void XView::close() {
    wlr_xwayland_surface_close(xsurface_);
}

void XView::setMaximized(bool on) {
    wlr_xwayland_surface_set_maximized(xsurface_, on, on);
}

void XView::setMinimized(bool on) {
    wlr_xwayland_surface_set_minimized(xsurface_, on);
}

void XView::moveTo(int x, int y) {
    x_ = x;
    y_ = y;
    // 通知 X11 客户端新几何（XWM 会处理）。
    wlr_xwayland_surface_configure(xsurface_, x_, y_, width(), height());
    if (sceneSurface_ != nullptr) {
        wlr_scene_node_set_position(&sceneSurface_->buffer->node, x_, y_);
    }
}

// ---- 事件回调 ----

void XView::handleAssociate(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, associate_);
    if (self->xsurface_->surface == nullptr) {
        wlr_log(WLR_ERROR, "xwayland surface associated without surface");
        return;
    }
    // scene 节点：挂 viewAnchor（与 xdg 窗口同层）。
    self->sceneSurface_ =
        wlr_scene_surface_create(self->compositor_.viewAnchor(), self->xsurface_->surface);
    if (self->sceneSurface_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create scene surface for xwayland window");
        return;
    }
    // map/unmap 在 wlr_surface 上（dissociate 后重新 associate 时再次 add）。
    self->map_.notify = handleMap;
    wl_signal_add(&self->xsurface_->surface->events.map, &self->map_);
    self->unmap_.notify = handleUnmap;
    wl_signal_add(&self->xsurface_->surface->events.unmap, &self->unmap_);
}

void XView::handleDissociate(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, dissociate_);
    // X11 窗口隐藏（unmap）时 wlroots 使 surface 无效并发 dissociate：
    // 必须销毁 scene 节点、摘除 map/unmap 监听（否则同 surface 重新
    // associate 时二次 add 形成链表自环崩溃）。
    if (self->sceneSurface_ != nullptr) {
        wlr_scene_node_destroy(&self->sceneSurface_->buffer->node);
        self->sceneSurface_ = nullptr;
    }
    wl_list_remove(&self->map_.link);
    wl_list_init(&self->map_.link);
    wl_list_remove(&self->unmap_.link);
    wl_list_init(&self->unmap_.link);
    self->mapped_ = false;
    self->compositor_.removeXView(self);
}

void XView::handleMap(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, map_);
    self->mapped_ = true;
    self->compositor_.addXView(self);
    // 初始位置：X11 客户端提供（xsurface->x/y 由 XWM 设置）。
    // 宽高为 0 时（客户端尚未 configure）仅设位置、不回传 0 尺寸。
    if (self->xsurface_->width > 0 && self->xsurface_->height > 0) {
        self->moveTo(self->xsurface_->x, self->xsurface_->y);
    } else if (self->sceneSurface_ != nullptr) {
        wlr_scene_node_set_position(&self->sceneSurface_->buffer->node,
                                    self->xsurface_->x, self->xsurface_->y);
    }
    wlr_log(WLR_INFO, "xwayland window mapped: '%s'",
            self->title() != nullptr ? self->title() : "");
}

void XView::handleUnmap(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, unmap_);
    self->mapped_ = false;
    self->compositor_.removeXView(self);
    self->compositor_.seat()->unfocusSurface(self->surface());
}

void XView::handleDestroy(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, destroy_);
    delete self;  // wlroots destroy 信号是最后一个事件。
}

void XView::handleRequestActivate(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, requestActivate_);
    self->activate(true);
}

void XView::handleRequestClose(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, requestClose_);
    self->close();
}

void XView::handleRequestConfigure(wl_listener* listener, void* data) {
    auto* self = wl_container_of(listener, self, requestConfigure_);
    auto* event = static_cast<wlr_xwayland_surface_configure_event*>(data);
    // 采用客户端请求的几何（X11 客户端自主定位）。
    self->x_ = event->x;
    self->y_ = event->y;
    wlr_xwayland_surface_configure(self->xsurface_, event->x, event->y,
                                   event->width, event->height);
    if (self->sceneSurface_ != nullptr) {
        wlr_scene_node_set_position(&self->sceneSurface_->buffer->node, event->x, event->y);
    }
}

void XView::handleRequestMaximize(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, requestMaximize_);
    self->setMaximized(true);
}

void XView::handleRequestMinimize(wl_listener* listener, void* data) {
    auto* self = wl_container_of(listener, self, requestMinimize_);
    auto* event = static_cast<wlr_xwayland_minimize_event*>(data);
    self->setMinimized(event->minimize);
}

void XView::handleSetTitle(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, setTitle_);
    wlr_log(WLR_DEBUG, "xwayland title set: '%s'",
            self->title() != nullptr ? self->title() : "");
}

}  // namespace w10de

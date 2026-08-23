#include "compositor/view.h"

#include "compositor/seat.h"
#include "compositor/server.h"

namespace w10de {

View::View(Compositor& compositor, wlr_xdg_toplevel* toplevel)
    : compositor_(compositor), toplevel_(toplevel) {
    // 先初始化全部 listener：构造中途失败被 delete 时，析构对未 add 的
    // listener 执行 wl_list_remove 也安全（自指链表自摘除）。
    wl_list_init(&map_.link);
    wl_list_init(&unmap_.link);
    wl_list_init(&destroy_.link);
    wl_list_init(&commit_.link);
    wl_list_init(&requestMove_.link);
    wl_list_init(&requestResize_.link);
    wl_list_init(&requestMaximize_.link);
    wl_list_init(&requestMinimize_.link);
    wl_list_init(&requestFullscreen_.link);
    wl_list_init(&setTitle_.link);
    wl_list_init(&setAppId_.link);
    wl_list_init(&ftMaximize_.link);
    wl_list_init(&ftMinimize_.link);
    wl_list_init(&ftActivate_.link);
    wl_list_init(&ftFullscreen_.link);
    wl_list_init(&ftClose_.link);
    wl_list_init(&ftDestroy_.link);

    wlr_xdg_surface* xdgSurface = toplevel->base;
    wlr_surface* surface = xdgSurface->surface;

    // scene 节点：map 后内容可见（wlr_scene 内部跟踪 surface map 状态）。
    // 挂到 viewAnchor，保证 z 序位于 background/bottom 层之上。
    sceneTree_ = wlr_scene_xdg_surface_create(compositor.viewAnchor(), xdgSurface);
    if (sceneTree_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create scene xdg surface for toplevel");
        return;
    }

    // 监听 surface map/unmap（0.19 中 xdg_surface 不再自带 map/unmap 事件）。
    map_.notify = handleMap;
    wl_signal_add(&surface->events.map, &map_);
    unmap_.notify = handleUnmap;
    wl_signal_add(&surface->events.unmap, &unmap_);
    destroy_.notify = handleDestroy;
    wl_signal_add(&toplevel->events.destroy, &destroy_);
    // 内容尺寸变化时同步标题栏宽度。
    commit_.notify = handleCommit;
    wl_signal_add(&surface->events.commit, &commit_);

    // SSD 标题栏装饰（M2a）。
    createDecoration();
    // foreign-toplevel handle：任务栏窗口列表协议（M3 前置）。
    createForeignToplevel();

    requestMove_.notify = handleRequestMove;
    wl_signal_add(&toplevel->events.request_move, &requestMove_);
    requestResize_.notify = handleRequestResize;
    wl_signal_add(&toplevel->events.request_resize, &requestResize_);
    requestMaximize_.notify = handleRequestMaximize;
    wl_signal_add(&toplevel->events.request_maximize, &requestMaximize_);
    requestMinimize_.notify = handleRequestMinimize;
    wl_signal_add(&toplevel->events.request_minimize, &requestMinimize_);
    requestFullscreen_.notify = handleRequestFullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &requestFullscreen_);
    setTitle_.notify = handleSetTitle;
    wl_signal_add(&toplevel->events.set_title, &setTitle_);
    setAppId_.notify = handleSetAppId;
    wl_signal_add(&toplevel->events.set_app_id, &setAppId_);

    wlr_log(WLR_INFO, "new toplevel view created");
}

View::~View() {
    // 从合成器视图列表移除（若仍在其中）。
    compositor_.removeView(this);
    destroyForeignToplevel();
    // 装饰树是纯场景节点（sceneTree_ 由 wlr_scene_xdg_surface 内部管理，
    // 装饰树需手动销毁，否则每窗口泄漏 5 个节点）。
    if (decorationTree_ != nullptr) {
        wlr_scene_node_destroy(&decorationTree_->node);
    }
    wl_list_remove(&map_.link);
    wl_list_remove(&unmap_.link);
    wl_list_remove(&destroy_.link);
    wl_list_remove(&commit_.link);
    wl_list_remove(&requestMove_.link);
    wl_list_remove(&requestResize_.link);
    wl_list_remove(&requestMaximize_.link);
    wl_list_remove(&requestMinimize_.link);
    wl_list_remove(&requestFullscreen_.link);
    wl_list_remove(&setTitle_.link);
    wl_list_remove(&setAppId_.link);
    wl_list_remove(&ftMaximize_.link);
    wl_list_remove(&ftMinimize_.link);
    wl_list_remove(&ftActivate_.link);
    wl_list_remove(&ftFullscreen_.link);
    wl_list_remove(&ftClose_.link);
    wl_list_remove(&ftDestroy_.link);
    wlr_log(WLR_INFO, "toplevel view destroyed");
}

int View::width() const {
    return toplevel_->base->geometry.width;
}

int View::height() const {
    return toplevel_->base->geometry.height;
}

bool View::contains(double lx, double ly) const {
    // 命中范围 = 标题栏装饰区（上方 32px）+ 内容区。
    if (ly >= y_ && ly < y_ + kTitleBarHeight && lx >= x_ && lx < x_ + width()) {
        return true;
    }
    return lx >= x_ && lx < x_ + width() &&
           ly >= y_ + kTitleBarHeight && ly < y_ + kTitleBarHeight + height();
}

DecorationArea View::decorationAt(double lx, double ly) const {
    if (!mapped_) {
        return DecorationArea::None;
    }
    const double dx = lx - x_;
    const double dy = ly - y_;
    if (dy < 0 || dy >= kTitleBarHeight || dx < 0 || dx >= width()) {
        return DecorationArea::None;
    }
    // 按钮从右往左排列（Win10 布局）；窄窗口时按钮区收缩（不越界）。
    const int w = width();
    const int closeX = w - kButtonWidth > 0 ? w - kButtonWidth : 0;
    const int maxX = w - 2 * kButtonWidth > 0 ? w - 2 * kButtonWidth : 0;
    const int minX = w - 3 * kButtonWidth > 0 ? w - 3 * kButtonWidth : 0;
    if (dx >= closeX) return DecorationArea::CloseButton;
    if (dx >= maxX) return DecorationArea::MaxButton;
    if (dx >= minX) return DecorationArea::MinButton;
    return DecorationArea::TitleBar;
}

void View::setActivated(bool activated) {
    wlr_xdg_toplevel_set_activated(toplevel_, activated);
    if (ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_activated(ftHandle_, activated);
    }
}

void View::close() {
    wlr_xdg_toplevel_send_close(toplevel_);
}

void View::setMaximized(bool maximize) {
    if (maximized_ == maximize) {
        // 协议要求：每次 request_maximize/fullscreen 都必须响应 configure，
        // 即使状态未变（wlr_xdg_toplevel_set_maximized 内部 schedule_configure）。
        wlr_xdg_toplevel_set_maximized(toplevel_, maximize);
        return;
    }
    maximized_ = maximize;
    wlr_xdg_toplevel_set_maximized(toplevel_, maximize);
    if (ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_maximized(ftHandle_, maximized_);
    }

    // 未映射时（客户端 map 前请求最大化，常见于启动即最大化应用）只记录
    // 状态，几何由 handleMap 首次定位后应用；此时 geometry 还是 0，保存
    // 恢复几何会得到 (0,0,0,0)。
    if (!mapped_) {
        wlr_log(WLR_INFO, "view maximized=%d (applied on map)", maximized_);
        return;
    }

    if (maximized_) {
        // 保存恢复几何（仅首次进入最大化时）。
        if (!hasRestoreGeometry_) {
            setRestoreGeometry(x_, y_, width(), height());
        }
        moveTo(0, 0);
        // 最大化到可用区（扣除任务栏等独占区），而非整个输出。
        int outW = 0, outH = 0;
        if (compositor_.outputUsableSize(compositor_.firstOutput(), &outW, &outH)) {
            // 内容区高度 = 可用区高度 - 标题栏（最大化时标题栏仍可见）。
            int contentH = outH - kTitleBarHeight;
            if (contentH < 1) {
                contentH = 1;  // 极端小可用区时避免非法尺寸
            }
            resize(outW, contentH);
        }
    } else {
        // 恢复最大化前几何。
        if (hasRestoreGeometry_) {
            int rx = 0, ry = 0, rw = 0, rh = 0;
            restoreGeometry(&rx, &ry, &rw, &rh);
            moveTo(rx, ry);
            resize(rw, rh);
            hasRestoreGeometry_ = false;
        }
    }
    wlr_log(WLR_INFO, "view maximized=%d", maximized_);
}

void View::setRestoreGeometry(int x, int y, int w, int h) {
    restoreX_ = x;
    restoreY_ = y;
    restoreW_ = w;
    restoreH_ = h;
    hasRestoreGeometry_ = true;
}

void View::restoreGeometry(int* x, int* y, int* w, int* h) const {
    *x = restoreX_;
    *y = restoreY_;
    *w = restoreW_;
    *h = restoreH_;
}

void View::setMinimized(bool minimize) {
    minimized_ = minimize;
    // 整个窗口（内容 + 装饰）一起隐藏/恢复。
    if (decorationTree_ != nullptr) {
        wlr_scene_node_set_enabled(&decorationTree_->node, !minimized_);
    }
    wlr_scene_node_set_enabled(&sceneTree_->node, !minimized_);
    if (minimized_) {
        setActivated(false);
    }
    if (ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_minimized(ftHandle_, minimized_);
    }
    wlr_log(WLR_INFO, "view minimized=%d", minimized_);
}

void View::moveTo(int x, int y) {
    x_ = x;
    y_ = y;
    // 装饰树在 (x, y)，内容区在其下方 kTitleBarHeight 处。
    if (decorationTree_ != nullptr) {
        wlr_scene_node_set_position(&decorationTree_->node, x_, y_);
    }
    wlr_scene_node_set_position(&sceneTree_->node, x_, y_ + kTitleBarHeight);
}

void View::resize(int width, int height) {
    // 仅请求；客户端 configure/ack 后几何才会变化。
    wlr_xdg_toplevel_set_size(toplevel_, width, height);
}

// ---- SSD 装饰 ----

void View::createDecoration() {
    // 装饰树挂在 viewAnchor（内容区上方，同窗口 z 序），位置由 moveTo 同步。
    decorationTree_ = wlr_scene_tree_create(compositor_.viewAnchor());
    if (decorationTree_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create decoration tree");
        return;
    }
    // Win10 深色标题栏 #2D2D2D；按钮：最小化/最大化浅灰，关闭红色。
    const float titleBarColor[4] = {0x2D / 255.0f, 0x2D / 255.0f, 0x2D / 255.0f, 1.0f};
    const float buttonColor[4] = {0x3C / 255.0f, 0x3C / 255.0f, 0x3C / 255.0f, 1.0f};
    const float closeColor[4] = {0xE8 / 255.0f, 0x11 / 255.0f, 0x23 / 255.0f, 1.0f};

    titleBarRect_ = wlr_scene_rect_create(decorationTree_, 0, kTitleBarHeight, titleBarColor);
    minButtonRect_ = wlr_scene_rect_create(decorationTree_, kButtonWidth, kTitleBarHeight, buttonColor);
    maxButtonRect_ = wlr_scene_rect_create(decorationTree_, kButtonWidth, kTitleBarHeight, buttonColor);
    closeButtonRect_ = wlr_scene_rect_create(decorationTree_, kButtonWidth, kTitleBarHeight, closeColor);
    if (titleBarRect_ == nullptr || minButtonRect_ == nullptr ||
            maxButtonRect_ == nullptr || closeButtonRect_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create decoration rects");
        return;
    }

    // 未映射时隐藏。
    wlr_scene_node_set_enabled(&decorationTree_->node, false);
}

void View::updateDecoration() {
    if (decorationTree_ == nullptr) {
        return;
    }
    const int w = width();
    // 标题栏背景铺满内容宽度。
    if (titleBarRect_ != nullptr) {
        wlr_scene_rect_set_size(titleBarRect_, w, kTitleBarHeight);
    }
    // 按钮从右往左：关闭(46) 最大化(46) 最小化(46)；窄窗口时 clamp 到 0。
    const int closeX = w - kButtonWidth > 0 ? w - kButtonWidth : 0;
    const int maxX = w - 2 * kButtonWidth > 0 ? w - 2 * kButtonWidth : 0;
    const int minX = w - 3 * kButtonWidth > 0 ? w - 3 * kButtonWidth : 0;
    if (closeButtonRect_ != nullptr) {
        wlr_scene_node_set_position(&closeButtonRect_->node, closeX, 0);
    }
    if (maxButtonRect_ != nullptr) {
        wlr_scene_node_set_position(&maxButtonRect_->node, maxX, 0);
    }
    if (minButtonRect_ != nullptr) {
        wlr_scene_node_set_position(&minButtonRect_->node, minX, 0);
    }
}

// ---- foreign-toplevel（任务栏窗口列表协议）----

void View::createForeignToplevel() {
    wlr_foreign_toplevel_manager_v1* manager = compositor_.foreignToplevelManager();
    if (manager == nullptr) {
        return;
    }
    ftHandle_ = wlr_foreign_toplevel_handle_v1_create(manager);
    if (ftHandle_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create foreign toplevel handle");
        return;
    }
    wlr_foreign_toplevel_handle_v1_set_title(ftHandle_,
        title() != nullptr ? title() : "");
    wlr_foreign_toplevel_handle_v1_set_app_id(ftHandle_,
        appId() != nullptr ? appId() : "");
    wlr_foreign_toplevel_handle_v1_set_activated(ftHandle_, false);
    if (wlr_output* output = compositor_.firstOutput(); output != nullptr) {
        wlr_foreign_toplevel_handle_v1_output_enter(ftHandle_, output);
    }

    ftMaximize_.notify = handleFtlMaximize;
    wl_signal_add(&ftHandle_->events.request_maximize, &ftMaximize_);
    ftMinimize_.notify = handleFtlMinimize;
    wl_signal_add(&ftHandle_->events.request_minimize, &ftMinimize_);
    ftActivate_.notify = handleFtlActivate;
    wl_signal_add(&ftHandle_->events.request_activate, &ftActivate_);
    ftFullscreen_.notify = handleFtlFullscreen;
    wl_signal_add(&ftHandle_->events.request_fullscreen, &ftFullscreen_);
    ftClose_.notify = handleFtlClose;
    wl_signal_add(&ftHandle_->events.request_close, &ftClose_);
    ftDestroy_.notify = handleFtlDestroy;
    wl_signal_add(&ftHandle_->events.destroy, &ftDestroy_);
}

void View::destroyForeignToplevel() {
    if (ftHandle_ == nullptr) {
        return;
    }
    // 先摘除监听再销毁（销毁会触发 destroy 信号，避免访问已释放链表）。
    // remove 后重新 init：析构体后续还会 remove 一遍，须保证链表有效。
    wl_list_remove(&ftMaximize_.link);
    wl_list_init(&ftMaximize_.link);
    wl_list_remove(&ftMinimize_.link);
    wl_list_init(&ftMinimize_.link);
    wl_list_remove(&ftActivate_.link);
    wl_list_init(&ftActivate_.link);
    wl_list_remove(&ftFullscreen_.link);
    wl_list_init(&ftFullscreen_.link);
    wl_list_remove(&ftClose_.link);
    wl_list_init(&ftClose_.link);
    wl_list_remove(&ftDestroy_.link);
    wl_list_init(&ftDestroy_.link);
    wlr_foreign_toplevel_handle_v1_destroy(ftHandle_);
    ftHandle_ = nullptr;
}

void View::handleFtlMaximize(wl_listener* listener, void* data) {
    auto* view = wl_container_of(listener, view, ftMaximize_);
    auto* event = static_cast<wlr_foreign_toplevel_handle_v1_maximized_event*>(data);
    view->setMaximized(event->maximized);
}

void View::handleFtlMinimize(wl_listener* listener, void* data) {
    auto* view = wl_container_of(listener, view, ftMinimize_);
    auto* event = static_cast<wlr_foreign_toplevel_handle_v1_minimized_event*>(data);
    view->setMinimized(event->minimized);
}

void View::handleFtlActivate(wl_listener* listener, void* /*data*/) {
    auto* view = wl_container_of(listener, view, ftActivate_);
    // 任务栏点击窗口：恢复显示（若最小化）、聚焦并置顶（仅已映射窗口）。
    if (!view->mapped_) {
        return;
    }
    if (view->minimized_) {
        view->setMinimized(false);
    }
    view->compositor_.seat()->focusView(view);
    view->compositor_.raiseView(view);
}

void View::handleFtlFullscreen(wl_listener* listener, void* data) {
    auto* view = wl_container_of(listener, view, ftFullscreen_);
    auto* event = static_cast<wlr_foreign_toplevel_handle_v1_fullscreen_event*>(data);
    // M2a：fullscreen 暂按最大化处理。
    view->setMaximized(event->fullscreen);
}

void View::handleFtlClose(wl_listener* listener, void* /*data*/) {
    auto* view = wl_container_of(listener, view, ftClose_);
    view->close();
}

void View::handleFtlDestroy(wl_listener* listener, void* /*data*/) {
    auto* view = wl_container_of(listener, view, ftDestroy_);
    // handle 即将释放：摘除全部 ft 监听并重新初始化（析构可能再次 remove）。
    wl_list_remove(&view->ftMaximize_.link);
    wl_list_init(&view->ftMaximize_.link);
    wl_list_remove(&view->ftMinimize_.link);
    wl_list_init(&view->ftMinimize_.link);
    wl_list_remove(&view->ftActivate_.link);
    wl_list_init(&view->ftActivate_.link);
    wl_list_remove(&view->ftFullscreen_.link);
    wl_list_init(&view->ftFullscreen_.link);
    wl_list_remove(&view->ftClose_.link);
    wl_list_init(&view->ftClose_.link);
    wl_list_remove(&view->ftDestroy_.link);
    wl_list_init(&view->ftDestroy_.link);
    view->ftHandle_ = nullptr;
}

// ---- 事件回调 ----

void View::handleMap(wl_listener* listener, void* /*data*/) {
    auto* view = wl_container_of(listener, view, map_);
    view->mapped_ = true;
    // remap（unmap 后再次 map）场景：复位最小化状态并恢复显示。
    if (view->minimized_) {
        view->setMinimized(false);
    }
    view->compositor_.addView(view);
    // 初始位置：仅首次 map 层叠放置，remap 保持原位置。
    if (!view->positionInitialized_) {
        static int cascade = 0;
        const int offset = 40;
        view->moveTo(100 + cascade * offset % 400, 80 + cascade * offset % 300);
        ++cascade;
        view->positionInitialized_ = true;
    }
    // map 前客户端已请求最大化（启动即最大化）：此时应用最大化几何，
    // 并保存当前（层叠后的）位置作为恢复几何。
    if (view->maximized_) {
        int outW = 0, outH = 0;
        if (view->compositor_.outputUsableSize(
                view->compositor_.firstOutput(), &outW, &outH)) {
            if (!view->hasRestoreGeometry()) {
                view->setRestoreGeometry(view->x(), view->y(),
                                         view->width(), view->height());
            }
            view->moveTo(0, 0);
            int contentH = outH - View::kTitleBarHeight;
            if (contentH < 1) {
                contentH = 1;
            }
            view->resize(outW, contentH);
        }
    }
    // 显示装饰并同步尺寸。
    if (view->decorationTree_ != nullptr) {
        wlr_scene_node_set_enabled(&view->decorationTree_->node, true);
    }
    view->updateDecoration();
    view->setActivated(true);
    view->compositor_.seat()->focusView(view);
    // 置顶：map 顺序可能与创建顺序不同，确保新窗口在 z 序最上
    //（与 views_ 列表"末尾最上"的语义一致）。
    view->compositor_.raiseView(view);
    wlr_log(WLR_INFO, "view mapped: '%s' (%s) %dx%d at %d,%d",
            view->title() ? view->title() : "",
            view->appId() ? view->appId() : "",
            view->width(), view->height(), view->x(), view->y());
}

void View::handleUnmap(wl_listener* listener, void* /*data*/) {
    auto* view = wl_container_of(listener, view, unmap_);
    view->mapped_ = false;
    if (view->decorationTree_ != nullptr) {
        wlr_scene_node_set_enabled(&view->decorationTree_->node, false);
    }
    view->compositor_.removeView(view);
    view->compositor_.seat()->unfocusView(view);
}

void View::handleCommit(wl_listener* listener, void* /*data*/) {
    auto* view = wl_container_of(listener, view, commit_);
    if (view->mapped_) {
        view->updateDecoration();
    }
}

void View::handleDestroy(wl_listener* listener, void* /*data*/) {
    auto* view = wl_container_of(listener, view, destroy_);
    delete view;  // wlroots destroy 信号是最后一个事件，此后对象不再被引用。
}

void View::handleRequestMove(wl_listener* listener, void* data) {
    auto* view = wl_container_of(listener, view, requestMove_);
    auto* event = static_cast<wlr_xdg_toplevel_move_event*>(data);
    // 校验 serial 来自 seat 最近的输入事件，防伪造。
    if (view->mapped_ && event->seat != nullptr &&
            wlr_seat_client_validate_event_serial(event->seat, event->serial)) {
        view->compositor_.seat()->beginMove(view);
    }
}

void View::handleRequestResize(wl_listener* listener, void* data) {
    auto* view = wl_container_of(listener, view, requestResize_);
    auto* event = static_cast<wlr_xdg_toplevel_resize_event*>(data);
    if (view->mapped_ && event->seat != nullptr &&
            wlr_seat_client_validate_event_serial(event->seat, event->serial)) {
        view->compositor_.seat()->beginResize(view, event->edges);
    }
}

void View::handleRequestMaximize(wl_listener* listener, void* /*data*/) {
    auto* view = wl_container_of(listener, view, requestMaximize_);
    // 事件不携带目标状态，以客户端请求的为准（避免无条件翻转失步）。
    view->setMaximized(view->toplevel()->requested.maximized);
}

void View::handleRequestMinimize(wl_listener* listener, void* /*data*/) {
    auto* view = wl_container_of(listener, view, requestMinimize_);
    // 客户端 set_minimized 请求即请求最小化。
    view->setMinimized(true);
}

void View::handleRequestFullscreen(wl_listener* listener, void* /*data*/) {
    // M2a：fullscreen 暂按最大化处理，后续里程碑完善独立 fullscreen 状态。
    auto* view = wl_container_of(listener, view, requestFullscreen_);
    wlr_log(WLR_INFO, "fullscreen request (M2a: treating as maximize)");
    view->setMaximized(view->toplevel()->requested.fullscreen);
}

void View::handleSetTitle(wl_listener* listener, void* /*data*/) {
    auto* view = wl_container_of(listener, view, setTitle_);
    wlr_log(WLR_DEBUG, "toplevel title set: '%s'", view->title() ? view->title() : "");
    if (view->ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_title(view->ftHandle_,
            view->title() != nullptr ? view->title() : "");
    }
}

void View::handleSetAppId(wl_listener* listener, void* /*data*/) {
    auto* view = wl_container_of(listener, view, setAppId_);
    wlr_log(WLR_DEBUG, "toplevel app_id set: '%s'", view->appId() ? view->appId() : "");
    if (view->ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_app_id(view->ftHandle_,
            view->appId() != nullptr ? view->appId() : "");
    }
}

}  // namespace w10de

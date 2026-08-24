#include "compositor/xview.h"
#include "compositor/util.h"

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
    wl_list_init(&setClass_.link);
    wl_list_init(&setOverrideRedirect_.link);
    wl_list_init(&commit_.link);
    wl_list_init(&ftMaximize_.link);
    wl_list_init(&ftMinimize_.link);
    wl_list_init(&ftActivate_.link);
    wl_list_init(&ftFullscreen_.link);
    wl_list_init(&ftClose_.link);
    wl_list_init(&ftDestroy_.link);

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
    // 审查 #17：X11 class 变化时更新任务栏 app_id。
    setClass_.notify = handleSetClass;
    wl_signal_add(&xsurface->events.set_class, &setClass_);
    // 审查 #8：override-redirect 窗口（X11 菜单/工具提示）可中途切换，
    // 动态摘除装饰（无装饰时直接无操作）。
    setOverrideRedirect_.notify = handleSetOverrideRedirect;
    wl_signal_add(&xsurface->events.set_override_redirect, &setOverrideRedirect_);

    // 新窗口归属当前工作区（M7 续：多工作区）。
    workspace_ = compositor.currentWorkspace();

    // 审查 #8：override-redirect 窗口不加 SSD 装饰、不进任务栏
    //（位置/尺寸由客户端自由控制，无 WM 标题栏语义）。
    if (xsurface->override_redirect) {
        hasDecoration_ = false;
        wlr_log(WLR_INFO, "xwayland override-redirect window (no decoration)");
    } else {
        // SSD 装饰（M7 续；与 xdg View 同款 Win10 标题栏）。
        createDecoration();
        // foreign-toplevel handle：任务栏窗口列表协议（M7 续）。
        createForeignToplevel();
    }

    wlr_log(WLR_INFO, "new xwayland surface (window 0x%x)", xsurface_->window_id);
}

XView::~XView() {
    compositor_.removeXView(this);
    destroyForeignToplevel();
    // 装饰树是纯场景节点，需手动销毁（sceneSurface_ 由 dissociate 管理）。
    if (decorationTree_ != nullptr) {
        wlr_scene_node_destroy(&decorationTree_->node);
    }
    // 标题文字 buffer（scene buffer 已随装饰树销毁，这里释放像素）。
    if (titleText_ != nullptr) {
        wlr_buffer_drop(&titleText_->base);
        titleText_ = nullptr;
    }
    // M8 阴影 buffer（scene buffer 已随装饰树销毁，这里释放像素）。
    if (shadow_ != nullptr) {
        wlr_buffer_drop(&shadow_->base);
        shadow_ = nullptr;
    }
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
    wl_list_remove(&setClass_.link);
    wl_list_remove(&setOverrideRedirect_.link);
    wl_list_remove(&commit_.link);
    wl_list_remove(&ftMaximize_.link);
    wl_list_remove(&ftMinimize_.link);
    wl_list_remove(&ftActivate_.link);
    wl_list_remove(&ftFullscreen_.link);
    wl_list_remove(&ftClose_.link);
    wl_list_remove(&ftDestroy_.link);
}

bool XView::contains(double lx, double ly) const {
    // 无装饰（override-redirect）时仅内容区。
    if (!hasDecoration_) {
        return lx >= x_ && lx < x_ + width() && ly >= y_ && ly < y_ + height();
    }
    // 命中范围 = 标题栏装饰区（上方 32px）+ 内容区。
    if (ly >= y_ && ly < y_ + View::kTitleBarHeight &&
            lx >= x_ && lx < x_ + width()) {
        return true;
    }
    return lx >= x_ && lx < x_ + width() &&
           ly >= y_ + View::kTitleBarHeight &&
           ly < y_ + View::kTitleBarHeight + height();
}

DecorationArea XView::decorationAt(double lx, double ly) const {
    if (!mapped_ || !hasDecoration_) {
        return DecorationArea::None;
    }
    const double dx = lx - x_;
    const double dy = ly - y_;
    if (dy < 0 || dy >= View::kTitleBarHeight || dx < 0 || dx >= width()) {
        return DecorationArea::None;
    }
    // 按钮从右往左排列（Win10 布局）；窄窗口时按钮区收缩（不越界）。
    const int w = width();
    const int closeX = w - View::kButtonWidth > 0 ? w - View::kButtonWidth : 0;
    const int maxX = w - 2 * View::kButtonWidth > 0 ? w - 2 * View::kButtonWidth : 0;
    const int minX = w - 3 * View::kButtonWidth > 0 ? w - 3 * View::kButtonWidth : 0;
    if (dx >= closeX) return DecorationArea::CloseButton;
    if (dx >= maxX) return DecorationArea::MaxButton;
    if (dx >= minX) return DecorationArea::MinButton;
    return DecorationArea::TitleBar;
}

void XView::setHoverArea(DecorationArea area) {
    if (hoverArea_ == area || decorationTree_ == nullptr) {
        return;
    }
    hoverArea_ = area;
    // hover 打磨：按钮背景高亮（关闭更亮红）。
    const float defaultButton[4] = {0x3C / 255.0f, 0x3C / 255.0f, 0x3C / 255.0f, 1.0f};
    const float hoverButton[4] = {0x5C / 255.0f, 0x5C / 255.0f, 0x5C / 255.0f, 1.0f};
    const float defaultClose[4] = {0xE8 / 255.0f, 0x11 / 255.0f, 0x23 / 255.0f, 1.0f};
    const float hoverClose[4] = {0xF1 / 255.0f, 0x70 / 255.0f, 0x7A / 255.0f, 1.0f};
    if (minButtonRect_ != nullptr) {
        wlr_scene_rect_set_color(minButtonRect_,
            area == DecorationArea::MinButton ? hoverButton : defaultButton);
    }
    if (maxButtonRect_ != nullptr) {
        wlr_scene_rect_set_color(maxButtonRect_,
            area == DecorationArea::MaxButton ? hoverButton : defaultButton);
    }
    if (closeButtonRect_ != nullptr) {
        wlr_scene_rect_set_color(closeButtonRect_,
            area == DecorationArea::CloseButton ? hoverClose : defaultClose);
    }
}

void XView::activate(bool on) {
    if (on && mapped_ && surface() != nullptr) {
        // 统一焦点入口（审查 #2）：focusSurface 会失活全部窗口（含 XView
        // 自身），随后再激活本窗口覆盖，保证跨类型激活状态唯一。
        compositor_.seat()->focusSurface(surface(), true);
        wlr_xwayland_surface_activate(xsurface_, true);
        if (ftHandle_ != nullptr) {
            wlr_foreign_toplevel_handle_v1_set_activated(ftHandle_, true);
        }
        compositor_.raiseXView(this);
    } else {
        wlr_xwayland_surface_activate(xsurface_, false);
        if (ftHandle_ != nullptr) {
            wlr_foreign_toplevel_handle_v1_set_activated(ftHandle_, false);
        }
    }
}

void XView::close() {
    wlr_xwayland_surface_close(xsurface_);
}

void XView::setMaximized(bool on) {
    maximized_ = on;
    wlr_xwayland_surface_set_maximized(xsurface_, on, on);
    if (ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_maximized(ftHandle_, on);
    }
}

void XView::setMinimized(bool on) {
    minimized_ = on;
    wlr_xwayland_surface_set_minimized(xsurface_, on);
    if (on) {
        activate(false);
    }
    if (ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_minimized(ftHandle_, minimized_);
    }
    applyVisibility();
}

void XView::moveTo(int x, int y) {
    x_ = x;
    y_ = y;
    // 装饰树在 (x, y)，内容区在其下方 kTitleBarHeight 处（无装饰时同坐标）。
    const int contentY = hasDecoration_ ? y_ + View::kTitleBarHeight : y_;
    if (decorationTree_ != nullptr) {
        wlr_scene_node_set_position(&decorationTree_->node, x_, y_);
    }
    if (sceneSurface_ != nullptr) {
        wlr_scene_node_set_position(&sceneSurface_->buffer->node, x_, contentY);
    }
    // 通知 X11 客户端新几何（XWM 会处理）。
    wlr_xwayland_surface_configure(xsurface_, x_, y_, width(), height());
}

// ---- 多工作区（M7 续）----

void XView::setWorkspace(int workspace) {
    if (workspace < 0 || workspace >= Compositor::kWorkspaceCount) {
        return;
    }
    if (workspace_ == workspace) {
        return;
    }
    workspace_ = workspace;
    applyVisibility();
}

void XView::applyVisibility() {
    const bool visible = mapped_ && !minimized_ &&
                         workspace_ == compositor_.currentWorkspace();
    if (decorationTree_ != nullptr) {
        wlr_scene_node_set_enabled(&decorationTree_->node, visible);
    }
    if (sceneSurface_ != nullptr) {
        wlr_scene_node_set_enabled(&sceneSurface_->buffer->node, visible);
    }
}

// ---- M8 窗口动画（帧插值，与 View 同款）----

void XView::animateMoveTo(int x, int y) {
    if (!mapped_) {
        moveTo(x, y);
        return;
    }
    animFromX_ = x_;
    animFromY_ = y_;
    animToX_ = x;
    animToY_ = y;
    animT_ = 0.0f;
    animActive_ = true;
}

void XView::tickAnimation() {
    if (!animActive_) {
        return;
    }
    constexpr float kStep = 0.12f;
    animT_ += kStep;
    if (animT_ >= 1.0f) {
        animT_ = 1.0f;
        moveTo(static_cast<int>(animToX_), static_cast<int>(animToY_));
        animActive_ = false;
        return;
    }
    const float eased = 1.0f - (1.0f - animT_) * (1.0f - animT_);
    const int nx = static_cast<int>(animFromX_ + (animToX_ - animFromX_) * eased);
    const int ny = static_cast<int>(animFromY_ + (animToY_ - animFromY_) * eased);
    moveTo(nx, ny);
}

void XView::cancelAnimation() {
    animActive_ = false;
}

// ---- SSD 装饰（M7 续，与 View 同款）----

void XView::createDecoration() {
    // 装饰树挂在 viewAnchor（内容区上方，同窗口 z 序），位置由 moveTo 同步。
    decorationTree_ = wlr_scene_tree_create(compositor_.viewAnchor());
    if (decorationTree_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create xview decoration tree");
        return;
    }
    // Win10 深色标题栏 #2D2D2D；按钮：最小化/最大化浅灰，关闭红色。
    const float titleBarColor[4] = {0x2D / 255.0f, 0x2D / 255.0f, 0x2D / 255.0f, 1.0f};
    const float buttonColor[4] = {0x3C / 255.0f, 0x3C / 255.0f, 0x3C / 255.0f, 1.0f};
    const float closeColor[4] = {0xE8 / 255.0f, 0x11 / 255.0f, 0x23 / 255.0f, 1.0f};

    // z 序（decorationTree_ 子节点，后创建者在上）：
    //   1. 窗口阴影（最底，覆盖窗口外 8px）
    //   2. 标题栏背景（最底） 3. 标题文字 4. 三个按钮（最顶）
    shadowNode_ = wlr_scene_buffer_create(decorationTree_, nullptr);
    if (shadowNode_ != nullptr) {
        wlr_scene_node_set_position(&shadowNode_->node,
                                    -kShadowSize, -kShadowSize);
    } else {
        wlr_log(WLR_ERROR, "failed to create xview shadow node");
    }
    titleBarRect_ = wlr_scene_rect_create(decorationTree_, 0, View::kTitleBarHeight, titleBarColor);
    titleTextNode_ = wlr_scene_buffer_create(decorationTree_, nullptr);
    if (titleTextNode_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create xview title text node");
    }
    minButtonRect_ = wlr_scene_rect_create(decorationTree_, View::kButtonWidth, View::kTitleBarHeight, buttonColor);
    maxButtonRect_ = wlr_scene_rect_create(decorationTree_, View::kButtonWidth, View::kTitleBarHeight, buttonColor);
    closeButtonRect_ = wlr_scene_rect_create(decorationTree_, View::kButtonWidth, View::kTitleBarHeight, closeColor);
    if (titleBarRect_ == nullptr || minButtonRect_ == nullptr ||
            maxButtonRect_ == nullptr || closeButtonRect_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create xview decoration rects");
        return;
    }

    // 未映射时隐藏。
    wlr_scene_node_set_enabled(&decorationTree_->node, false);
}

void XView::updateDecoration() {
    if (decorationTree_ == nullptr) {
        return;
    }
    const int w = width();
    if (titleBarRect_ != nullptr) {
        wlr_scene_rect_set_size(titleBarRect_, w, View::kTitleBarHeight);
    }
    // 按钮从右往左：关闭(46) 最大化(46) 最小化(46)；窄窗口时 clamp 到 0。
    const int closeX = w - View::kButtonWidth > 0 ? w - View::kButtonWidth : 0;
    const int maxX = w - 2 * View::kButtonWidth > 0 ? w - 2 * View::kButtonWidth : 0;
    const int minX = w - 3 * View::kButtonWidth > 0 ? w - 3 * View::kButtonWidth : 0;
    if (closeButtonRect_ != nullptr) {
        wlr_scene_node_set_position(&closeButtonRect_->node, closeX, 0);
    }
    if (maxButtonRect_ != nullptr) {
        wlr_scene_node_set_position(&maxButtonRect_->node, maxX, 0);
    }
    if (minButtonRect_ != nullptr) {
        wlr_scene_node_set_position(&minButtonRect_->node, minX, 0);
    }
    // 标题文字：左侧 padding 12，宽到最小化按钮左缘。
    if (titleTextNode_ != nullptr) {
        const int textW = w - 3 * View::kButtonWidth - 24;
        wlr_scene_node_set_position(&titleTextNode_->node, 12, 0);
        const int oldTextW = titleText_ != nullptr ? titleText_->base.width : -1;
        if (textW != oldTextW) {
            renderTitle();
        }
    }
}

void XView::renderTitle() {
    if (titleTextNode_ == nullptr || decorationTree_ == nullptr) {
        return;
    }
    const char* t = title();
    // 标题文字区域：宽 = 标题栏 - 3 按钮 - 左右 padding。
    const int textW = width() - 3 * View::kButtonWidth - 24;
    // 空标题 / 文字区不可用（过窄）：清空 scene buffer 并释放旧引用。
    TitleTextBuffer* next = nullptr;
    if (textW > 0 && t != nullptr && *t != '\0') {
        static const float kTextColor[3] = {1.0f, 1.0f, 1.0f};
        next = renderTitleText(t, textW, View::kTitleBarHeight, kTextColor);
    }
    if (next == nullptr && titleText_ == nullptr) {
        return;
    }
    wlr_scene_buffer_set_buffer(titleTextNode_,
        next != nullptr ? &next->base : nullptr);
    if (titleText_ != nullptr) {
        wlr_buffer_drop(&titleText_->base);
    }
    titleText_ = next;
    wlr_scene_node_set_position(&titleTextNode_->node, 12, 0);
}

// M8：渲染/更新窗口阴影（尺寸变化时重绘；位置固定跟随装饰树）。
// 守卫与 updateDecoration/renderTitle 一致（含 decorationTree_），
// 防止装饰树销毁后 shadowNode_ 悬垂被使用（审查 S2）。
void XView::updateShadow() {
    if (decorationTree_ == nullptr || shadowNode_ == nullptr) {
        return;
    }
    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) {
        return;
    }
    const int shadowW = w + 2 * kShadowSize;
    const int shadowH = View::kTitleBarHeight + h + 2 * kShadowSize;
    if (shadow_ != nullptr && shadow_->base.width == shadowW &&
            shadow_->base.height == shadowH) {
        return;  // 尺寸未变（位置由装饰树移动携带），无需重绘
    }
    ShadowBuffer* next = renderShadow(w, View::kTitleBarHeight + h, kShadowSize);
    if (next == nullptr) {
        return;
    }
    wlr_scene_buffer_set_buffer(shadowNode_, &next->base);
    if (shadow_ != nullptr) {
        wlr_buffer_drop(&shadow_->base);
    }
    shadow_ = next;
    wlr_scene_node_set_position(&shadowNode_->node, -kShadowSize, -kShadowSize);
}

// ---- foreign-toplevel（任务栏窗口列表协议，M7 续）----

void XView::createForeignToplevel() {
    wlr_foreign_toplevel_manager_v1* manager = compositor_.foreignToplevelManager();
    if (manager == nullptr) {
        return;
    }
    ftHandle_ = wlr_foreign_toplevel_handle_v1_create(manager);
    if (ftHandle_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create xview foreign toplevel handle");
        return;
    }
    wlr_foreign_toplevel_handle_v1_set_title(ftHandle_,
        title() != nullptr ? title() : "");
    wlr_foreign_toplevel_handle_v1_set_app_id(ftHandle_, "X11");
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

void XView::destroyForeignToplevel() {
    if (ftHandle_ == nullptr) {
        return;
    }
    // 先摘除监听再销毁（销毁会触发 destroy 信号，避免访问已释放链表）。
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

void XView::handleFtlMaximize(wl_listener* listener, void* data) {
    auto* view = W10DE_CONTAINER_OF(listener, XView, ftMaximize_);
    auto* event = static_cast<wlr_foreign_toplevel_handle_v1_maximized_event*>(data);
    view->setMaximized(event->maximized);
}

void XView::handleFtlMinimize(wl_listener* listener, void* data) {
    auto* view = W10DE_CONTAINER_OF(listener, XView, ftMinimize_);
    auto* event = static_cast<wlr_foreign_toplevel_handle_v1_minimized_event*>(data);
    view->setMinimized(event->minimized);
}

void XView::handleFtlActivate(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, XView, ftActivate_);
    // 任务栏点击窗口：恢复显示（若最小化）、聚焦并置顶（仅已映射窗口）。
    if (!view->mapped_) {
        return;
    }
    // 审查 #6：窗口在别的桌面时先切换过去（Win10 任务栏语义）。
    if (view->workspace() != view->compositor_.currentWorkspace()) {
        wlr_log(WLR_INFO, "ftl activate: switching to workspace %d",
                view->workspace());
        view->compositor_.switchWorkspace(view->workspace());
    }
    if (view->minimized_) {
        view->setMinimized(false);
    }
    view->activate(true);
    view->compositor_.raiseXView(view);
}

void XView::handleFtlFullscreen(wl_listener* listener, void* data) {
    auto* view = W10DE_CONTAINER_OF(listener, XView, ftFullscreen_);
    auto* event = static_cast<wlr_foreign_toplevel_handle_v1_fullscreen_event*>(data);
    view->setMaximized(event->fullscreen);
}

void XView::handleFtlClose(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, XView, ftClose_);
    view->close();
}

void XView::handleFtlDestroy(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, XView, ftDestroy_);
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

void XView::handleAssociate(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, associate_);
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
    // 重建节点后按当前工作区/最小化状态刷新显隐（M7 续）。
    self->applyVisibility();
    // 位置：内容区在装饰下方 kTitleBarHeight（SSD，M7 续；无装饰同坐标）。
    const int contentY = self->hasDecoration_
        ? self->y_ + View::kTitleBarHeight : self->y_;
    wlr_scene_node_set_position(&self->sceneSurface_->buffer->node,
                                self->x_, contentY);
    // map/unmap 在 wlr_surface 上（dissociate 后重新 associate 时再次 add）。
    self->map_.notify = handleMap;
    wl_signal_add(&self->xsurface_->surface->events.map, &self->map_);
    self->unmap_.notify = handleUnmap;
    wl_signal_add(&self->xsurface_->surface->events.unmap, &self->unmap_);
    // 内容尺寸变化时同步标题栏宽度。
    self->commit_.notify = handleCommit;
    wl_signal_add(&self->xsurface_->surface->events.commit, &self->commit_);
}

void XView::handleDissociate(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, dissociate_);
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
    wl_list_remove(&self->commit_.link);
    wl_list_init(&self->commit_.link);
    self->mapped_ = false;
    self->cancelAnimation();  // 隐藏后动画不应继续空转（审查轻微项）
    self->applyVisibility();
    self->compositor_.removeXView(self);
}

void XView::handleMap(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, map_);
    self->mapped_ = true;
    self->compositor_.addXView(self);
    // 可见性按 工作区+最小化 统一判定（M7 续）。
    self->applyVisibility();
    // 初始位置：X11 客户端提供（xsurface->x/y 由 XWM 设置）。
    // 宽高为 0 时（客户端尚未 configure）仅设位置、不回传 0 尺寸。
    if (self->xsurface_->width > 0 && self->xsurface_->height > 0) {
        self->moveTo(self->xsurface_->x, self->xsurface_->y);
    } else if (self->sceneSurface_ != nullptr) {
        const int contentY = self->hasDecoration_
            ? self->y_ + View::kTitleBarHeight : self->y_;
        wlr_scene_node_set_position(&self->sceneSurface_->buffer->node,
                                    self->xsurface_->x, contentY);
        // 审查 #11：同时设置装饰树位置，避免标题栏滞留 (0,0)。
        if (self->decorationTree_ != nullptr) {
            wlr_scene_node_set_position(&self->decorationTree_->node,
                                        self->xsurface_->x, self->y_);
        }
    }
    // 显示装饰并同步尺寸（M7 续）。
    self->updateDecoration();
    self->renderTitle();
    self->updateShadow();  // M8：map 时渲染窗口阴影
    // 审查 #7：新 X11 窗口置顶（XWM 惯例：新窗口可见并位于最上）。
    self->compositor_.raiseXView(self);
    wlr_log(WLR_INFO, "xwayland window mapped: '%s'",
            self->title() != nullptr ? self->title() : "");
}

void XView::handleUnmap(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, unmap_);
    self->mapped_ = false;
    self->cancelAnimation();  // 隐藏后动画不应继续空转（审查轻微项）
    self->applyVisibility();
    self->compositor_.removeXView(self);
    // 审查 #13：先判空再解引用。
    if (self->compositor_.seat() != nullptr) {
        self->compositor_.seat()->unfocusSurface(self->surface());
        self->compositor_.seat()->updateHover();
    }
}

void XView::handleDestroy(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, destroy_);
    delete self;  // wlroots destroy 信号是最后一个事件。
}

void XView::handleCommit(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, commit_);
    if (self->mapped_) {
        self->updateDecoration();
        self->updateShadow();  // M8：尺寸变化时重绘阴影
    }
}

void XView::handleRequestActivate(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, requestActivate_);
    self->activate(true);
}

void XView::handleRequestClose(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, requestClose_);
    self->close();
}

void XView::handleRequestConfigure(wl_listener* listener, void* data) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, requestConfigure_);
    auto* event = static_cast<wlr_xwayland_surface_configure_event*>(data);
    // 采用客户端请求的几何（X11 客户端自主定位）。
    self->x_ = event->x;
    self->y_ = event->y;
    wlr_xwayland_surface_configure(self->xsurface_, event->x, event->y,
                                   event->width, event->height);
    self->moveTo(self->x_, self->y_);
    self->updateDecoration();
}

void XView::handleRequestMaximize(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, requestMaximize_);
    self->setMaximized(true);
}

void XView::handleRequestMinimize(wl_listener* listener, void* data) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, requestMinimize_);
    auto* event = static_cast<wlr_xwayland_minimize_event*>(data);
    self->setMinimized(event->minimize);
}

void XView::handleSetTitle(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, setTitle_);
    wlr_log(WLR_DEBUG, "xwayland title set: '%s'",
            self->title() != nullptr ? self->title() : "");
    if (self->ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_title(self->ftHandle_,
            self->title() != nullptr ? self->title() : "");
    }
    // M2b：标题变化时刷新标题栏文字。
    self->renderTitle();
}

// 审查 #17：X11 class（app_id 来源）变化时更新任务栏条目。
// 字段名经 WlrootsPatchHeaders 补丁为 class_（'class' 是 C++ 保留字）。
void XView::handleSetClass(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, setClass_);
    const char* cls = self->xsurface_->class_;
    wlr_log(WLR_DEBUG, "xwayland class set: '%s'",
            cls != nullptr ? cls : "");
    if (self->ftHandle_ != nullptr && cls != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_app_id(self->ftHandle_, cls);
    }
}

// 审查 #8：窗口中途切换 override-redirect 时动态摘除装饰/任务栏条目。
void XView::handleSetOverrideRedirect(wl_listener* listener, void* /*data*/) {
    auto* self = W10DE_CONTAINER_OF(listener, XView, setOverrideRedirect_);
    const bool want = self->xsurface_->override_redirect;
    if (want == !self->hasDecoration_) {
        return;  // 状态未变化
    }
    self->hasDecoration_ = !want;
    if (want) {
        // 进入 override-redirect：移除装饰与任务栏条目。
        if (self->decorationTree_ != nullptr) {
            wlr_scene_node_destroy(&self->decorationTree_->node);
            self->decorationTree_ = nullptr;
        }
        // 审查 S2：全部装饰节点指针必须同步置 null（树已销毁，悬垂指针
        // 会在 commit → updateShadow 时触发 UAF——其守卫不含 decorationTree_）。
        self->titleBarRect_ = nullptr;
        self->minButtonRect_ = nullptr;
        self->maxButtonRect_ = nullptr;
        self->closeButtonRect_ = nullptr;
        self->titleTextNode_ = nullptr;
        self->shadowNode_ = nullptr;
        if (self->titleText_ != nullptr) {
            wlr_buffer_drop(&self->titleText_->base);
            self->titleText_ = nullptr;
        }
        if (self->shadow_ != nullptr) {
            wlr_buffer_drop(&self->shadow_->base);
            self->shadow_ = nullptr;
        }
        self->destroyForeignToplevel();
        self->hoverArea_ = DecorationArea::None;
    } else {
        // 退出 override-redirect：重建装饰。
        self->createDecoration();
        self->createForeignToplevel();
    }
    self->applyVisibility();
    wlr_log(WLR_INFO, "xwayland window override_redirect=%d", want);
}

}  // namespace w10de

#include "compositor/seat.h"
#include "compositor/util.h"

#include <linux/input-event-codes.h>  // BTN_LEFT 等

// XKB 键符宏（XKB_KEY_q 等）在独立头文件中。
#include <xkbcommon/xkbcommon-keysyms.h>

extern "C" {
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/util/edges.h>
}

#include "compositor/server.h"
#include "compositor/layer_shell.h"
#include "compositor/view.h"
#include "compositor/xview.h"

namespace w10de {

Seat::Seat(Compositor& compositor) : compositor_(compositor) {
    // 先初始化全部 listener：构造中途失败时析构对未 add 的 listener
    // 执行 wl_list_remove 也是安全的（自指链表自摘除）。
    wl_list_init(&cursorMotion_.link);
    wl_list_init(&cursorMotionAbs_.link);
    wl_list_init(&cursorButton_.link);
    wl_list_init(&cursorAxis_.link);
    wl_list_init(&cursorFrame_.link);
    wl_list_init(&requestSetCursor_.link);
    wl_list_init(&requestSetSelection_.link);
    wl_list_init(&requestSetPrimarySelection_.link);
    wl_list_init(&keyboardKey_.link);
    wl_list_init(&keyboardModifiers_.link);
    wl_list_init(&keyboardDestroy_.link);

    seat_ = wlr_seat_create(compositor.display(), "seat0");
    if (seat_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create wlr_seat");
        return;
    }
    wlr_seat_set_capabilities(seat_,
        WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);

    cursor_ = wlr_cursor_create();
    if (cursor_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create wlr_cursor");
        return;
    }
    wlr_cursor_attach_output_layout(cursor_, compositor.outputLayout());

    cursorMgr_ = wlr_xcursor_manager_create("default", 24);
    if (cursorMgr_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create xcursor manager");
        return;
    }
    // 加载默认缩放（1.0）下的光标主题；无主题时保持默认光标。
    wlr_xcursor_manager_load(cursorMgr_, 1.0f);

    // 指针事件（wlr_cursor 汇总所有指针设备）。
    cursorMotion_.notify = handleCursorMotion;
    wl_signal_add(&cursor_->events.motion, &cursorMotion_);
    cursorMotionAbs_.notify = handleCursorMotionAbsolute;
    wl_signal_add(&cursor_->events.motion_absolute, &cursorMotionAbs_);
    cursorButton_.notify = handleCursorButton;
    wl_signal_add(&cursor_->events.button, &cursorButton_);
    cursorAxis_.notify = handleCursorAxis;
    wl_signal_add(&cursor_->events.axis, &cursorAxis_);
    cursorFrame_.notify = handleCursorFrame;
    wl_signal_add(&cursor_->events.frame, &cursorFrame_);

    // 客户端请求设置光标。
    requestSetCursor_.notify = handleRequestSetCursor;
    wl_signal_add(&seat_->events.request_set_cursor, &requestSetCursor_);

    // 剪贴板请求。
    requestSetSelection_.notify = handleRequestSetSelection;
    wl_signal_add(&seat_->events.request_set_selection, &requestSetSelection_);
    requestSetPrimarySelection_.notify = handleRequestSetPrimarySelection;
    wl_signal_add(&seat_->events.request_set_primary_selection, &requestSetPrimarySelection_);
}

Seat::~Seat() {
    wl_list_remove(&cursorMotion_.link);
    wl_list_remove(&cursorMotionAbs_.link);
    wl_list_remove(&cursorButton_.link);
    wl_list_remove(&cursorAxis_.link);
    wl_list_remove(&cursorFrame_.link);
    wl_list_remove(&requestSetCursor_.link);
    wl_list_remove(&requestSetSelection_.link);
    wl_list_remove(&requestSetPrimarySelection_.link);
    wl_list_remove(&keyboardKey_.link);
    wl_list_remove(&keyboardModifiers_.link);
    wl_list_remove(&keyboardDestroy_.link);
    if (cursor_ != nullptr) {
        wlr_cursor_destroy(cursor_);
    }
    if (seat_ != nullptr) {
        wlr_seat_destroy(seat_);
    }
    if (cursorMgr_ != nullptr) {
        wlr_xcursor_manager_destroy(cursorMgr_);
    }
}

void Seat::handleNewInput(wlr_input_device* device) {
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        if (keyboard_ != nullptr) {
            // M1 仅使用第一个键盘（listener 为单实例，不支持多键盘热插拔）。
            wlr_log(WLR_DEBUG, "ignoring extra keyboard device");
            break;
        }
        auto* kb = wlr_keyboard_from_input_device(device);
        configureKeyboard(kb);
        keyboardKey_.notify = handleKeyboardKey;
        wl_signal_add(&kb->events.key, &keyboardKey_);
        keyboardModifiers_.notify = handleKeyboardModifiers;
        wl_signal_add(&kb->events.modifiers, &keyboardModifiers_);
        keyboardDestroy_.notify = handleKeyboardDestroy;
        wl_signal_add(&kb->base.events.destroy, &keyboardDestroy_);
        wlr_seat_set_keyboard(seat_, kb);
        keyboard_ = kb;
        wlr_log(WLR_INFO, "keyboard device added: '%s'",
                device->name ? device->name : "");
        break;
    }
    case WLR_INPUT_DEVICE_POINTER:
        if (cursor_ == nullptr) {
            // 构造失败路径防护（cursor 创建失败时 seat 不可用）。
            wlr_log(WLR_ERROR, "pointer device ignored: cursor unavailable");
            break;
        }
        wlr_cursor_attach_input_device(cursor_, device);
        wlr_log(WLR_INFO, "pointer device added: '%s'",
                device->name ? device->name : "");
        break;
    default:
        // M1 暂不支持触摸/数位板/开关设备。
        wlr_log(WLR_DEBUG, "ignoring input device type %d", device->type);
        break;
    }
}

void Seat::configureKeyboard(wlr_keyboard* kb) {
    // 使用系统默认布局；M7 配置系统支持自定义规则。
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (ctx == nullptr) {
        wlr_log(WLR_ERROR, "failed to create xkb context");
        return;
    }
    xkb_keymap* keymap = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == nullptr) {
        wlr_log(WLR_ERROR, "failed to create xkb keymap");
        xkb_context_unref(ctx);
        return;
    }
    if (!wlr_keyboard_set_keymap(kb, keymap)) {
        wlr_log(WLR_ERROR, "failed to set keyboard keymap");
    }
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
}

// ---- 窗口交互 ----

void Seat::beginMove(View* view) {
    if (!view->mapped()) {
        return;
    }
    endInteractive();
    dragMode_ = DragMode::Move;
    dragView_ = view;
    // M8：用户拖动开始即取消布局动画（避免与插值竞态）。
    view->cancelAnimation();
    grabX_ = cursor_->x;
    grabY_ = cursor_->y;
    grabGeomX_ = view->x();
    grabGeomY_ = view->y();
    wlr_log(WLR_DEBUG, "begin move view at (%d,%d)", grabGeomX_, grabGeomY_);
}

void Seat::beginResize(View* view, uint32_t edges) {
    if (!view->mapped()) {
        return;
    }
    endInteractive();
    dragMode_ = DragMode::Resize;
    dragView_ = view;
    // M8：用户 resize 开始即取消布局动画（与 move 路径一致，审查 M1）。
    view->cancelAnimation();
    resizeEdges_ = edges;
    grabX_ = cursor_->x;
    grabY_ = cursor_->y;
    grabGeomX_ = view->x();
    grabGeomY_ = view->y();
    grabGeomW_ = view->width();
    grabGeomH_ = view->height();
    wlr_log(WLR_DEBUG, "begin resize view edges=0x%x", edges);
}

// M7 续：XWayland 窗口标题栏拖动（SSD；无 Resize 边缘支持）。
void Seat::beginMoveXView(XView* view) {
    if (!view->mapped()) {
        return;
    }
    endInteractive();
    dragMode_ = DragMode::Move;
    dragXView_ = view;
    // M8：用户拖动开始即取消布局动画（避免与插值竞态）。
    view->cancelAnimation();
    grabX_ = cursor_->x;
    grabY_ = cursor_->y;
    grabGeomX_ = view->x();
    grabGeomY_ = view->y();
    wlr_log(WLR_DEBUG, "begin move xview at (%d,%d)", grabGeomX_, grabGeomY_);
}

void Seat::endInteractive() {
    if (dragMode_ == DragMode::None) {
        return;
    }
    dragMode_ = DragMode::None;
    dragView_ = nullptr;
    dragXView_ = nullptr;
    wlr_log(WLR_DEBUG, "end interactive operation");
    // 拖动期间 processCursorMotion 提前返回不刷 hover；结束后立即重算，
    // 使高亮与真实光标位置一致（审查 #8）。
    updateHover();
}

// ---- 焦点 ----

void Seat::focusView(View* view) {
    if (focusedView_ == view) {
        return;
    }
    focusedView_ = view;
    // 键盘焦点进入目标视图。
    wlr_surface* surface = view->toplevel()->base->surface;
    wlr_keyboard* kb = wlr_seat_get_keyboard(seat_);
    if (kb != nullptr && surface != nullptr) {
        wlr_seat_keyboard_notify_enter(seat_, surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
    // 激活状态唯一（M3 任务栏将显示激活高亮）。
    for (View* v : compositor_.views()) {
        v->setActivated(v == view);
    }
    // 审查 #2：XWayland 窗口同步失活（跨类型焦点唯一）。
    for (XView* xv : compositor_.xviews()) {
        xv->activate(false);
    }
}

void Seat::focusSurface(wlr_surface* surface, bool deactivateViews) {
    focusedView_ = nullptr;
    wlr_keyboard* kb = wlr_seat_get_keyboard(seat_);
    if (kb != nullptr && surface != nullptr) {
        wlr_seat_keyboard_notify_enter(seat_, surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
    if (deactivateViews) {
        // 层表面（如开始菜单）获得输入时所有窗口失活。
        for (View* v : compositor_.views()) {
            v->setActivated(false);
        }
        // 审查 #2：XWayland 窗口同步失活。
        for (XView* xv : compositor_.xviews()) {
            xv->activate(false);
        }
    }
}

void Seat::unfocusView(View* view) {
    if (focusedView_ != view) {
        return;
    }
    focusedView_ = nullptr;
    wlr_seat_keyboard_notify_clear_focus(seat_);
}

void Seat::unfocusSurface(wlr_surface* surface) {
    if (seat_->keyboard_state.focused_surface == surface) {
        unfocusAll();
    }
}

void Seat::unfocusAll() {
    focusedView_ = nullptr;
    wlr_seat_keyboard_notify_clear_focus(seat_);
    wlr_seat_pointer_notify_clear_focus(seat_);
    endInteractive();
    // 审查 #2：全部窗口（xdg + XWayland）失活，保证焦点/激活唯一。
    for (View* v : compositor_.views()) {
        v->setActivated(false);
    }
    for (XView* xv : compositor_.xviews()) {
        xv->activate(false);
    }
}

void Seat::onViewDestroyed(View* view) {
    // 拖动中的视图被销毁：终止交互，避免悬垂指针。
    if (dragView_ == view) {
        endInteractive();
    }
    if (hoverView_ == view) {
        hoverView_ = nullptr;
    }
    unfocusView(view);
}

// M7 续：XWayland 视图销毁时清理引用。
void Seat::onXViewDestroyed(XView* view) {
    if (dragXView_ == view) {
        endInteractive();
    }
    if (hoverXView_ == view) {
        hoverXView_ = nullptr;
    }
}

// ---- 命中检测 ----

View* Seat::viewAt(double lx, double ly) const {
    const auto& views = compositor_.views();
    for (auto it = views.rbegin(); it != views.rend(); ++it) {
        View* view = *it;
        // 仅命中当前工作区的可见窗口（M7 续：多工作区）。
        if (view->mapped() && view->workspace() == compositor_.currentWorkspace() &&
                view->contains(lx, ly)) {
            return view;
        }
    }
    return nullptr;
}

wlr_surface* Seat::surfaceAt(double lx, double ly, double* sx, double* sy,
                             LayerSurface** hitLayer) const {
    // 锁定时只有锁屏 surface 可交互（普通窗口/层表面已被隐藏）。
    if (compositor_.sessionLocked()) {
        wlr_surface* lock = compositor_.lockSurface();
        if (lock != nullptr && lock->mapped) {
            *sx = lx;
            *sy = ly;
            if (hitLayer != nullptr) {
                *hitLayer = nullptr;
            }
            return lock;
        }
        return nullptr;
    }
    // 按 z 序从顶到底：overlay → top → 窗口 → bottom → background。
    // 层锚顺序为 background < bottom < view < top < overlay。
    const auto& layers = compositor_.layerSurfaces();
    auto checkLayer = [&](int layer) -> wlr_surface* {
        for (LayerSurface* ls : layers) {
            if (static_cast<int>(ls->layer()->current.layer) != layer) {
                continue;
            }
            wlr_surface* s = ls->surfaceAt(lx, ly, sx, sy);
            if (s != nullptr) {
                if (hitLayer != nullptr) {
                    *hitLayer = ls;
                }
                return s;
            }
        }
        return nullptr;
    };
    // 1. 窗口之上的层（overlay/top）。
    if (wlr_surface* s = checkLayer(ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY); s != nullptr) {
        return s;
    }
    if (wlr_surface* s = checkLayer(ZWLR_LAYER_SHELL_V1_LAYER_TOP); s != nullptr) {
        return s;
    }
    // 1.5 XWayland 窗口（MVP：与 xdg 窗口同层，命中顺序在 xdg 之前）。
    for (auto it = compositor_.xviews().rbegin(); it != compositor_.xviews().rend(); ++it) {
        XView* xview = *it;
        // 仅当前工作区的可见窗口（M7 续：多工作区）。
        if (xview->mapped() && xview->workspace() == compositor_.currentWorkspace() &&
                xview->surface() != nullptr && xview->contains(lx, ly)) {
            // 装饰区（标题栏）不由内容命中：仅内容区返回 surface
            //（SSD，M7 续；装饰区由调用方处理按钮/拖动）。
            if (xview->decorationAt(lx, ly) != DecorationArea::None) {
                if (hitLayer != nullptr) {
                    *hitLayer = nullptr;
                }
                return nullptr;
            }
            *sx = lx - xview->x();
            *sy = ly - xview->y() - View::kTitleBarHeight;
            if (hitLayer != nullptr) {
                *hitLayer = nullptr;
            }
            return xview->surface();
        }
    }
    // 2. 窗口（内容区；装饰区由调用方另行判断）。
    View* view = viewAt(lx, ly);
    if (view != nullptr) {
        if (view->toplevel()->base->surface != nullptr &&
                view->decorationAt(lx, ly) == DecorationArea::None) {
            *sx = lx - view->x();
            *sy = ly - view->y() - View::kTitleBarHeight;
            if (hitLayer != nullptr) {
                *hitLayer = nullptr;
            }
            return view->toplevel()->base->surface;
        }
        // 装饰区（标题栏/按钮）：不向窗口之下的层 fallthrough ——
        // 标题栏上的滚轮/右键不得落到桌面/壁纸层（M2a 既有缺陷修正）；
        // 返回 null，由调用方处理（左键按钮/拖动，其余清指针焦点）。
        if (hitLayer != nullptr) {
            *hitLayer = nullptr;
        }
        return nullptr;
    }
    // 3. 窗口之下的层（bottom/background）。
    if (wlr_surface* s = checkLayer(ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM); s != nullptr) {
        return s;
    }
    return checkLayer(ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND);
}

// ---- 指针事件 ----

void Seat::handleCursorMotion(wl_listener* listener, void* data) {
    auto* seat = W10DE_CONTAINER_OF(listener, Seat, cursorMotion_);
    auto* event = static_cast<wlr_pointer_motion_event*>(data);
    // 0.19 签名：wlr_cursor_move(cursor, wlr_input_device*, dx, dy)。
    wlr_cursor_move(seat->cursor_, &event->pointer->base, event->delta_x, event->delta_y);
    seat->processCursorMotion(event->time_msec);
}

void Seat::handleCursorMotionAbsolute(wl_listener* listener, void* data) {
    auto* seat = W10DE_CONTAINER_OF(listener, Seat, cursorMotionAbs_);
    auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
    wlr_cursor_warp_absolute(seat->cursor_, &event->pointer->base, event->x, event->y);
    seat->processCursorMotion(event->time_msec);
}

void Seat::handleCursorButton(wl_listener* listener, void* data) {
    auto* seat = W10DE_CONTAINER_OF(listener, Seat, cursorButton_);
    auto* event = static_cast<wlr_pointer_button_event*>(data);
    seat->processCursorButton(event->time_msec, event->button, event->state);
}

void Seat::handleCursorAxis(wl_listener* listener, void* data) {
    auto* seat = W10DE_CONTAINER_OF(listener, Seat, cursorAxis_);
    auto* event = static_cast<wlr_pointer_axis_event*>(data);
    // 转发滚轮事件；M1 不做缩放等处理。
    wlr_seat_pointer_notify_axis(seat->seat_, event->time_msec,
        event->orientation, event->delta, event->delta_discrete,
        event->source, event->relative_direction);
}

void Seat::handleCursorFrame(wl_listener* listener, void* /*data*/) {
    auto* seat = W10DE_CONTAINER_OF(listener, Seat, cursorFrame_);
    wlr_seat_pointer_notify_frame(seat->seat_);
}

void Seat::handleRequestSetCursor(wl_listener* listener, void* data) {
    auto* self = W10DE_CONTAINER_OF(listener, Seat, requestSetCursor_);
    auto* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
    // M1 简化：不做 serial 校验，直接采纳客户端光标。
    if (event->surface != nullptr && self->seat_->pointer_state.focused_surface != nullptr) {
        wlr_cursor_set_surface(self->cursor_, event->surface,
            event->hotspot_x, event->hotspot_y);
    } else {
        wlr_cursor_unset_image(self->cursor_);
    }
}

void Seat::processCursorMotion(uint32_t timeMsec) {
    if (dragMode_ != DragMode::None && dragView_ != nullptr) {
        const double dx = cursor_->x - grabX_;
        const double dy = cursor_->y - grabY_;
        if (dragMode_ == DragMode::Move) {
            dragView_->moveTo(grabGeomX_ + static_cast<int>(dx),
                              grabGeomY_ + static_cast<int>(dy));
        } else if (dragMode_ == DragMode::Resize) {
            int newX = grabGeomX_, newY = grabGeomY_;
            int newW = grabGeomW_, newH = grabGeomH_;
            // 各边独立处理：右/下边推进，左/上边反向并平移窗口。
            if (resizeEdges_ & WLR_EDGE_RIGHT) {
                newW = grabGeomW_ + static_cast<int>(dx);
            } else if (resizeEdges_ & WLR_EDGE_LEFT) {
                newX = grabGeomX_ + static_cast<int>(dx);
                newW = grabGeomW_ - static_cast<int>(dx);
            }
            if (resizeEdges_ & WLR_EDGE_BOTTOM) {
                newH = grabGeomH_ + static_cast<int>(dy);
            } else if (resizeEdges_ & WLR_EDGE_TOP) {
                newY = grabGeomY_ + static_cast<int>(dy);
                newH = grabGeomH_ - static_cast<int>(dy);
            }
            constexpr int kMinSize = 50;
            // 最小尺寸钳制需联动位置：左/上边缘拖动时窗口右/下边缘固定。
            if (newW < kMinSize) {
                newW = kMinSize;
                if (resizeEdges_ & WLR_EDGE_LEFT) {
                    newX = grabGeomX_ + grabGeomW_ - kMinSize;
                }
            }
            if (newH < kMinSize) {
                newH = kMinSize;
                if (resizeEdges_ & WLR_EDGE_TOP) {
                    newY = grabGeomY_ + grabGeomH_ - kMinSize;
                }
            }
            dragView_->moveTo(newX, newY);
            dragView_->resize(newW, newH);
        }
        // 拖动中不更新指针 focus（保持在发起拖动的视图上）。
        return;
    }
    // M7 续：XWayland 窗口拖动（Move 模式）。
    if (dragMode_ != DragMode::None && dragXView_ != nullptr) {
        const double dx = cursor_->x - grabX_;
        const double dy = cursor_->y - grabY_;
        dragXView_->moveTo(grabGeomX_ + static_cast<int>(dx),
                           grabGeomY_ + static_cast<int>(dy));
        return;
    }

    // 统一命中检测（层表面 + 窗口），按 z 序返回实际 surface。
    LayerSurface* hitLayer = nullptr;
    double sx = 0, sy = 0;
    wlr_surface* surface = surfaceAt(cursor_->x, cursor_->y, &sx, &sy, &hitLayer);
    if (surface != nullptr) {
        wlr_seat_pointer_notify_enter(seat_, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat_, timeMsec, sx, sy);
    } else {
        // 未命中任何可输入表面（含窗口装饰区）：清除指针焦点。
        wlr_seat_pointer_notify_clear_focus(seat_);
    }
    // M2b hover 打磨：跟踪窗口装饰按钮的悬停（高亮反馈）。
    updateHover();
    // 注意：frame 事件统一由 wlr_cursor 的 frame 事件（handleCursorFrame）
    // 发送，此处不再单独发送，避免每批事件出现两个 frame。
}

void Seat::debugShowAltTab() {
    if (alttab_ == nullptr) {
        alttab_ = std::make_unique<AltTabSwitcher>(compositor_);
    }
    alttab_->show();
}

void Seat::updateHover() {
    // 命中最上层窗口的装饰区；非按钮区域（标题栏空白/内容区）清除高亮。
    // overlay/top 层表面（开始菜单等）遮挡窗口时不高亮被盖住的按钮：
    // 视觉反馈与实际输入（surfaceAt 中 overlay/top 优先）保持一致。
    bool occluded = false;
    double sx = 0, sy = 0;
    LayerSurface* hitLayer = nullptr;
    if (wlr_surface* s = surfaceAt(cursor_->x, cursor_->y, &sx, &sy, &hitLayer);
            s != nullptr && hitLayer != nullptr) {
        const int layerNum = static_cast<int>(hitLayer->layer()->current.layer);
        if (layerNum == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY ||
                layerNum == ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
            occluded = true;
        }
    }
    // xdg 窗口与 XWayland 窗口（M7 续）取命中的一方。
    View* view = occluded ? nullptr : viewAt(cursor_->x, cursor_->y);
    XView* xview = nullptr;
    if (view == nullptr && !occluded) {
        for (auto it = compositor_.xviews().rbegin();
                it != compositor_.xviews().rend(); ++it) {
            XView* xv = *it;
            if (xv->mapped() && xv->workspace() == compositor_.currentWorkspace() &&
                    xv->contains(cursor_->x, cursor_->y)) {
                xview = xv;
                break;
            }
        }
    }
    DecorationArea area = DecorationArea::None;
    if (view != nullptr && view->mapped()) {
        area = view->decorationAt(cursor_->x, cursor_->y);
    } else if (xview != nullptr) {
        area = xview->decorationAt(cursor_->x, cursor_->y);
    }
    if (area != DecorationArea::MinButton &&
            area != DecorationArea::MaxButton &&
            area != DecorationArea::CloseButton) {
        area = DecorationArea::None;  // 仅按钮有 hover 视觉
    }
    // 目标窗口变化或 hover 区域变化时刷新视觉。
    const bool targetChanged = hoverView_ != view || hoverXView_ != xview;
    if (targetChanged || (hoverView_ != nullptr && hoverView_->hoverArea() != area) ||
            (hoverXView_ != nullptr && hoverXView_->hoverArea() != area)) {
        if (hoverView_ != nullptr && hoverView_ != view) {
            hoverView_->setHoverArea(DecorationArea::None);
        }
        if (hoverXView_ != nullptr && hoverXView_ != xview) {
            hoverXView_->setHoverArea(DecorationArea::None);
        }
        hoverView_ = view;
        hoverXView_ = xview;
        if (view != nullptr) {
            view->setHoverArea(area);
        }
        if (xview != nullptr) {
            xview->setHoverArea(area);
        }
    } else if (view != nullptr) {
        view->setHoverArea(area);
    } else if (xview != nullptr) {
        xview->setHoverArea(area);
    }
}

void Seat::processCursorButton(uint32_t timeMsec, uint32_t button,
                               enum wl_pointer_button_state state) {
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        const double lx = cursor_->x, ly = cursor_->y;
        // 拖动中：不转发任何 press（避免幽灵 press/release 与提前结束拖动）；
        // 发起键（LEFT）的 release 由下方 RELEASED 分支结束拖动。
        if (dragMode_ != DragMode::None) {
            return;
        }

        // 先做一次完整命中（后续分支复用其结果）。
        LayerSurface* hitLayer = nullptr;
        double sx = 0, sy = 0;
        wlr_surface* s = surfaceAt(lx, ly, &sx, &sy, &hitLayer);
        const int hitLayerNum =
            hitLayer != nullptr ? static_cast<int>(hitLayer->layer()->current.layer) : -1;

        // 1. 窗口之上的层表面（overlay/top）优先于窗口装饰。
        if (s != nullptr && hitLayer != nullptr &&
                (hitLayerNum == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY ||
                 hitLayerNum == ZWLR_LAYER_SHELL_V1_LAYER_TOP)) {
            // 层表面请求键盘交互时（如开始菜单）转移键盘焦点。
            if (hitLayer->layer()->current.keyboard_interactive !=
                    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
                focusSurface(s, true);
            }
            // 确保 wlr_seat 指针焦点与实际命中一致（拖动结束等路径可能使
            // 内部焦点为 null/过期，不 enter 则 notify_button 被丢弃）。
            if (seat_->pointer_state.focused_surface != s) {
                wlr_seat_pointer_notify_enter(seat_, s, sx, sy);
            }
            wlr_seat_pointer_notify_button(seat_, timeMsec, button, state);
            pressedButtons_.insert(button);
            return;
        }

        // 2. 窗口装饰区（仅左键；标题栏按钮/拖动由合成器处理）。
        if (button == BTN_LEFT) {
            View* hit = viewAt(lx, ly);
            if (hit != nullptr && hit->decorationAt(lx, ly) != DecorationArea::None) {
                focusView(hit);
                compositor_.raiseView(hit);
                switch (hit->decorationAt(lx, ly)) {
                case DecorationArea::CloseButton:
                    wlr_log(WLR_INFO, "titlebar: close '%s'", hit->title() ? hit->title() : "");
                    hit->close();
                    return;
                case DecorationArea::MinButton:
                    hit->setMinimized(!hit->minimized());
                    return;
                case DecorationArea::MaxButton:
                    hit->setMaximized(!hit->maximized());
                    return;
                case DecorationArea::TitleBar:
                    beginMove(hit);  // SSD 下标题栏按下直接拖动
                    return;
                case DecorationArea::None:
                    break;  // 不可达（已判 != None）
                }
            }
            // M7 续：XWayland 窗口装饰区（SSD 同款按钮/拖动）。
            XView* xhit = nullptr;
            for (auto it = compositor_.xviews().rbegin();
                    it != compositor_.xviews().rend(); ++it) {
                XView* xv = *it;
                if (xv->mapped() && xv->workspace() == compositor_.currentWorkspace() &&
                        xv->contains(lx, ly)) {
                    xhit = xv;
                    break;
                }
            }
            if (xhit != nullptr && xhit->decorationAt(lx, ly) != DecorationArea::None) {
                xhit->activate(true);
                compositor_.raiseXView(xhit);
                switch (xhit->decorationAt(lx, ly)) {
                case DecorationArea::CloseButton:
                    wlr_log(WLR_INFO, "xview titlebar: close '%s'",
                            xhit->title() ? xhit->title() : "");
                    xhit->close();
                    return;
                case DecorationArea::MinButton:
                    xhit->setMinimized(!xhit->minimized());
                    return;
                case DecorationArea::MaxButton:
                    xhit->setMaximized(!xhit->maximized());
                    return;
                case DecorationArea::TitleBar:
                    beginMoveXView(xhit);  // SSD 下标题栏按下直接拖动
                    return;
                case DecorationArea::None:
                    break;
                }
            }
        }

        // 3. 窗口内容区 / XWayland / bottom / background 层表面。
        if (s != nullptr) {
            if (button == BTN_LEFT && hitLayer == nullptr) {
                // 窗口内容区：聚焦所属窗口（xdg 或 XWayland）。
                View* view = viewAt(lx, ly);
                if (view != nullptr) {
                    focusView(view);
                    compositor_.raiseView(view);
                } else {
                    for (auto it = compositor_.xviews().rbegin();
                            it != compositor_.xviews().rend(); ++it) {
                        XView* xv = *it;
                        // 仅当前工作区 + 内容区（装饰区已在分支 2 处理）。
                        if (xv->mapped() && xv->workspace() == compositor_.currentWorkspace() &&
                                xv->contains(lx, ly) &&
                                xv->decorationAt(lx, ly) == DecorationArea::None) {
                            focusSurface(xv->surface(), true);
                            compositor_.raiseXView(xv);
                            break;
                        }
                    }
                }
            }
            if (hitLayer != nullptr &&
                    hitLayer->layer()->current.keyboard_interactive !=
                        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
                focusSurface(s, true);
            }
            // 同层表面分支：press 转发前确保 enter（拖动结束后焦点可能过期）。
            if (seat_->pointer_state.focused_surface != s) {
                wlr_seat_pointer_notify_enter(seat_, s, sx, sy);
            }
            wlr_seat_pointer_notify_button(seat_, timeMsec, button, state);
            pressedButtons_.insert(button);
            return;
        }

        // 4. 空白处：清除指针焦点并吞掉 press（避免误转发给旧焦点 surface）。
        wlr_seat_pointer_notify_clear_focus(seat_);
        return;
    }

    // RELEASED
    if (dragMode_ != DragMode::None && button == BTN_LEFT) {
        // 拖动发起键释放：结束拖动（拖动期间未转发 press，release 也不转发）。
        endInteractive();
        return;
    }
    // 仅转发有对应 press 的按键（装饰/空白吞掉的 press 无 release）。
    if (!pressedButtons_.contains(button)) {
        return;
    }
    pressedButtons_.erase(button);
    wlr_seat_pointer_notify_button(seat_, timeMsec, button, state);
    // frame 事件由 handleCursorFrame 统一发送。

    if (button == BTN_LEFT) {
        endInteractive();
    }
}

// ---- 键盘事件 ----

void Seat::handleKeyboardKey(wl_listener* listener, void* data) {
    auto* seat = W10DE_CONTAINER_OF(listener, Seat, keyboardKey_);
    auto* event = static_cast<wlr_keyboard_key_event*>(data);
    seat->processKey(event->time_msec, event->keycode, event->state);
}

void Seat::handleKeyboardModifiers(wl_listener* listener, void* /*data*/) {
    auto* seat = W10DE_CONTAINER_OF(listener, Seat, keyboardModifiers_);
    if (seat->keyboard_ != nullptr) {
        wlr_seat_keyboard_notify_modifiers(seat->seat_, &seat->keyboard_->modifiers);
    }
}

void Seat::handleKeyboardDestroy(wl_listener* listener, void* /*data*/) {
    auto* seat = W10DE_CONTAINER_OF(listener, Seat, keyboardDestroy_);
    // 键盘对象即将释放：摘除挂在其事件上的监听（含本监听自身），
    // 避免对象释放后 Seat 析构 remove 时访问已释放链表（UAF）。
    wl_list_remove(&seat->keyboardKey_.link);
    wl_list_init(&seat->keyboardKey_.link);
    wl_list_remove(&seat->keyboardModifiers_.link);
    wl_list_init(&seat->keyboardModifiers_.link);
    wl_list_remove(&seat->keyboardDestroy_.link);
    wl_list_init(&seat->keyboardDestroy_.link);
    // 清空 wlr_seat 内部键盘引用：wlr_seat 不监听键盘设备销毁，
    // 不显式清除会使 wlr_seat_get_keyboard 返回悬垂指针（UAF）。
    wlr_seat_set_keyboard(seat->seat_, nullptr);
    seat->keyboard_ = nullptr;
    wlr_log(WLR_INFO, "keyboard device removed");
}

void Seat::processKey(uint32_t timeMsec, uint32_t keycode, wl_keyboard_key_state state) {
    if (keyboard_ == nullptr || keyboard_->xkb_state == nullptr) {
        return;
    }
    // wlroots keycode 比 xkb keycode 小 8。
    const xkb_keycode_t xkbKeycode = keycode + 8;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(keyboard_->xkb_state, xkbKeycode);
    const uint32_t mods = wlr_keyboard_get_modifiers(keyboard_);

    // ---- Alt+Tab 窗口切换（Win10 语义）----
    const bool isAlt = sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R;
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        // Alt 按下后 Tab/Shift+Tab 循环切换（不转发给客户端）。
        if (sym == XKB_KEY_Tab && (mods & WLR_MODIFIER_ALT)) {
            if (alttab_ == nullptr) {
                alttab_ = std::make_unique<AltTabSwitcher>(compositor_);
            }
            if (!alttab_->active()) {
                alttab_->show();
            } else if (mods & WLR_MODIFIER_SHIFT) {
                alttab_->prev();
            } else {
                alttab_->next();
            }
            return;
        }
    } else if (state == WL_KEYBOARD_KEY_STATE_RELEASED && isAlt) {
        // 松开 Alt：应用切换结果。
        if (alttab_ != nullptr && alttab_->active()) {
            alttab_->hideAndApply();
        }
    }

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        // 快捷键分发（[shortcuts] 配置驱动，第二批）：匹配当前按键的
        // 修饰组合 + 键名 → 动作。修饰键精确匹配（仅该组合，不含其他
        // 修饰——等价原 pureLogo 语义并支持 ctrl/alt/shift 组合）。
        // 0xFD = LOGO|CTRL|ALT|SHIFT|MOD2|MOD3|MOD5（wlr 位；审查 M1：
        // 不含 CAPS(0x02)——当前 wlroots 0.19 get_modifiers 只返回
        // depressed|latched 不含 locked，CAPS 不参与匹配；显式列出避免
        // 升级隐患与错误注释）。
        const uint32_t modMask = 0xFD;
        const auto& bindings = compositor_.shortcuts();
        for (int a = 0; a < static_cast<int>(ShortcutAction::Count); ++a) {
            const ShortcutBinding& b = bindings[static_cast<size_t>(a)];
            if (!b.valid()) {
                continue;
            }
            // 键符匹配：shift+字母 配置（如 "shift+a"）时 xkb 返回大写
            // 键符（XKB_KEY_A），统一转小写比较（审查 L1；符号键如
            // shift+1→'!' 不在键名表内，默认绑定无 shift 组合）。
            const xkb_keysym_t lowered = xkb_keysym_to_lower(sym);
            if ((sym == b.sym || lowered == b.sym) &&
                    (mods & modMask) == b.mods) {
                dispatchShortcut(static_cast<ShortcutAction>(a));
                return;  // 快捷键不转发给客户端
            }
        }
    }

    wlr_seat_keyboard_notify_key(seat_, timeMsec, keycode, state);
}

// 快捷键动作分发（配置驱动后的动作实现；与原硬编码行为一致）。
void Seat::dispatchShortcut(ShortcutAction action) {
    switch (action) {
    case ShortcutAction::Close:
        wlr_log(WLR_INFO, "shortcut: close focused view");
        if (focusedView_ != nullptr) {
            focusedView_->close();
        }
        break;
    case ShortcutAction::Maximize:
        if (focusedView_ != nullptr) {
            focusedView_->setMaximized(!focusedView_->maximized());
        }
        break;
    case ShortcutAction::Minimize:
        if (focusedView_ != nullptr) {
            focusedView_->setMinimized(!focusedView_->minimized());
        }
        break;
    case ShortcutAction::SnapLeft:
        if (focusedView_ != nullptr) {
            // 已在左半屏时再按则还原（Win10 行为：半屏 ↔ 浮动）。
            if (focusedView_->snapEdge() == SnapEdge::Left) {
                focusedView_->unsnap();
            } else {
                focusedView_->snapTo(SnapEdge::Left);
            }
        }
        break;
    case ShortcutAction::SnapRight:
        if (focusedView_ != nullptr) {
            if (focusedView_->snapEdge() == SnapEdge::Right) {
                focusedView_->unsnap();
            } else {
                focusedView_->snapTo(SnapEdge::Right);
            }
        }
        break;
    case ShortcutAction::SnapUp:
        if (focusedView_ != nullptr) {
            focusedView_->setMaximized(true);
        }
        break;
    case ShortcutAction::SnapDown:
        if (focusedView_ != nullptr) {
            // 最大化 → 还原；贴边 → 还原（Win10 Win+↓ 语义）。
            if (focusedView_->isFullAreaLayout()) {
                if (focusedView_->maximized()) {
                    focusedView_->setMaximized(false);
                } else {
                    focusedView_->unsnap();
                }
            }
        }
        break;
    case ShortcutAction::Lock:
        wlr_log(WLR_INFO, "shortcut: lock screen");
        compositor_.launchLockScreen();
        break;
    case ShortcutAction::Quit:
        wlr_log(WLR_INFO, "shortcut: quit compositor");
        wl_display_terminate(compositor_.display());
        break;
    case ShortcutAction::Clipboard:
        wlr_log(WLR_INFO, "shortcut: clipboard history");
        compositor_.toggleClipboardHistory();
        break;
    case ShortcutAction::Workspace1:
    case ShortcutAction::Workspace2:
    case ShortcutAction::Workspace3:
    case ShortcutAction::Workspace4: {
        const int ws = static_cast<int>(action) -
            static_cast<int>(ShortcutAction::Workspace1);
        wlr_log(WLR_INFO, "shortcut: switch to workspace %d", ws);
        compositor_.switchWorkspace(ws);
        break;
    }
    default:
        break;
    }
}

// ---- 剪贴板 ----

void Seat::handleRequestSetSelection(wl_listener* listener, void* data) {
    auto* seat = W10DE_CONTAINER_OF(listener, Seat, requestSetSelection_);
    auto* event = static_cast<wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(seat->seat_, event->source, event->serial);
}

void Seat::handleRequestSetPrimarySelection(wl_listener* listener, void* data) {
    auto* seat = W10DE_CONTAINER_OF(listener, Seat, requestSetPrimarySelection_);
    auto* event = static_cast<wlr_seat_request_set_primary_selection_event*>(data);
    wlr_seat_set_primary_selection(seat->seat_, event->source, event->serial);
}

}  // namespace w10de

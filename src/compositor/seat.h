// 输入与焦点管理（M1：指针 + 键盘 + 光标 + 窗口拖动）
#pragma once

extern "C" {
#include <wayland-server-core.h>
extern "C" {
// wlroots headers lack extern "C" guards; required in C++.
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
}  // extern "C" (wlroots)
}

#include <xkbcommon/xkbcommon.h>

#include <set>

namespace w10de {

class Compositor;
class View;
class XView;
class LayerSurface;

class Seat {
public:
    explicit Seat(Compositor& compositor);
    ~Seat();

    Seat(const Seat&) = delete;
    Seat& operator=(const Seat&) = delete;

    // 新输入设备接入（由 Compositor::handleNewInput 转发）。
    void handleNewInput(wlr_input_device* device);

    // ---- 窗口交互（由 View 的 request_move/resize 触发）----
    void beginMove(View* view);
    void beginResize(View* view, uint32_t edges);
    // M7 续：XWayland 窗口标题栏拖动（SSD）。
    void beginMoveXView(XView* view);
    void endInteractive();

    // ---- 焦点 ----
    View* focusedView() const { return focusedView_; }
    void focusView(View* view);
    void unfocusView(View* view);
    // 键盘焦点到任意 surface（layer surface 用）；layerSurface 非空时
    // 所有窗口失活（层表面如开始菜单获得输入）。
    void focusSurface(wlr_surface* surface, bool deactivateViews);
    // 若键盘焦点在该 surface 上则清除（窗口 unmap 时用）。
    void unfocusSurface(wlr_surface* surface);
    // 清空全部焦点（键盘+指针），锁屏等场景用。
    void unfocusAll();
    // 视图销毁时清理本 seat 中的引用（焦点/拖动），由 Compositor::removeView 调用。
    void onViewDestroyed(View* view);
    // M7 续：XView 销毁时清理引用（焦点/hover/拖动），由 Compositor::removeXView 调用。
    void onXViewDestroyed(XView* view);
    // M2b hover 打磨：更新装饰按钮悬停高亮。
    void updateHover();

    wlr_seat* seat() const { return seat_; }

private:
    // 命中检测：返回光标处最上层已映射视图。
    View* viewAt(double lx, double ly) const;
    // 命中检测（含层表面）：按 z 序 overlay → top → 窗口 → bottom → background
    // 返回命中的 surface 与 surface-local 坐标；未命中返回 nullptr。
    // hitLayer 输出命中的层表面（供按钮/focus 处理），非层表面时为 nullptr。
    wlr_surface* surfaceAt(double lx, double ly, double* sx, double* sy,
                           LayerSurface** hitLayer) const;
    // 设置键盘 keymap（xkb）并交给 seat。
    void configureKeyboard(wlr_keyboard* kb);

    // ---- 指针事件 ----
    static void handleCursorMotion(wl_listener* l, void* data);
    static void handleCursorMotionAbsolute(wl_listener* l, void* data);
    static void handleCursorButton(wl_listener* l, void* data);
    static void handleCursorAxis(wl_listener* l, void* data);
    static void handleCursorFrame(wl_listener* l, void* data);
    static void handleRequestSetCursor(wl_listener* l, void* data);

    // ---- 键盘事件 ----
    static void handleKeyboardKey(wl_listener* l, void* data);
    static void handleKeyboardModifiers(wl_listener* l, void* data);
    static void handleKeyboardDestroy(wl_listener* l, void* data);

    // ---- 剪贴板/拖放请求 ----
    static void handleRequestSetSelection(wl_listener* l, void* data);
    static void handleRequestSetPrimarySelection(wl_listener* l, void* data);

    void processCursorMotion(uint32_t timeMsec);
    void processCursorButton(uint32_t timeMsec, uint32_t button,
                             enum wl_pointer_button_state state);
    void processKey(uint32_t timeMsec, uint32_t keycode, wl_keyboard_key_state state);

    Compositor& compositor_;
    wlr_seat* seat_ = nullptr;
    wlr_cursor* cursor_ = nullptr;
    wlr_xcursor_manager* cursorMgr_ = nullptr;
    wlr_keyboard* keyboard_ = nullptr;

    View* focusedView_ = nullptr;
    // M2b hover 打磨：当前 hover 的视图（装饰按钮高亮跟踪）。
    View* hoverView_ = nullptr;
    // M7 续：当前 hover 的 XWayland 视图（装饰按钮高亮跟踪）。
    XView* hoverXView_ = nullptr;
    // 已转发 press 的按键（按 button 跟踪，release 只转发有对应 press 的
    // 按键，避免"幽灵 release"；标题栏/空白处吞掉的 press 不在此集合）。
    std::set<uint32_t> pressedButtons_;

    // 窗口拖动状态（MOVE/RESIZE）。
    enum class DragMode { None, Move, Resize };
    DragMode dragMode_ = DragMode::None;
    View* dragView_ = nullptr;
    // M7 续：拖动中的 XWayland 窗口（Move 模式；XView 无 Resize）。
    XView* dragXView_ = nullptr;
    uint32_t resizeEdges_ = 0;
    double grabX_ = 0.0, grabY_ = 0.0;        // 拖动开始时光标位置（布局坐标）
    int grabGeomX_ = 0, grabGeomY_ = 0;       // 拖动开始时视图位置
    int grabGeomW_ = 0, grabGeomH_ = 0;       // 拖动开始时视图尺寸

    wl_listener cursorMotion_ = {}, cursorMotionAbs_ = {}, cursorButton_ = {};
    wl_listener cursorAxis_ = {}, cursorFrame_ = {}, requestSetCursor_ = {};
    wl_listener keyboardKey_ = {}, keyboardModifiers_ = {}, keyboardDestroy_ = {};
    wl_listener requestSetSelection_ = {}, requestSetPrimarySelection_ = {};
};

}  // namespace w10de

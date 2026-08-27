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

#include <memory>
#include <set>
#include <vector>

#include "compositor/alttab.h"
#include "compositor/snaplayout.h"
#include "ipc/inputsettings.h"
#include "ipc/shortcuts.h"

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

    // ---- 输入设备设置（KDE-GAP 中优先：鼠标/键盘/触摸板）----
    // 应用 [input] 配置到当前设备（指针 libinput + 键盘重复率）；
    // handleNewInput 与 D-Bus 热应用均调用。
    void applyInputSettings(const w10de::ipc::InputSettings& s);
    // 当前生效设置（D-Bus GetInputSettings）。
    w10de::ipc::InputSettings inputSettings() const { return inputSettings_; }
    // 单个指针设备应用 libinput 配置（applyInputSettings 与热插拔共用）。
    void applyPointerSettings(wlr_input_device* device);

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
    // 当前键盘焦点 surface（层表面 unmap 补偿用；无焦点返回 nullptr）。
    wlr_surface* keyboardFocusedSurface() const {
        return seat_ != nullptr ? seat_->keyboard_state.focused_surface : nullptr;
    }
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
    // headless 验证（--alttab-test）：显示切换器（不应用选择）。
    void debugShowAltTab();
    // headless 验证：显示 Snap 布局选择器（--snaplayout-test 帧钩子）。
    void debugShowSnapLayout();

    // ---- 虚拟键盘注入（E8 屏幕键盘：CompositorDbus::InputKey）----
    // keysym → keycode（真实键盘 keymap 反查；无真实键盘用默认 us keymap
    // 并给 seat 通知 keymap）→ wlr_seat 键盘通知。修饰键更新经
    // notify_modifiers 同步到 xkb state。
    void injectKey(uint32_t keysym, bool pressed);
    // keysym → keycode 反查（headless 自测可测；无效返回 XKB_KEYCODE_INVALID）。
    static xkb_keycode_t keysymToKeycode(xkb_keymap* kmap, xkb_keysym_t sym);
    // 无真实键盘时确保合成虚拟键盘已建并绑定 seat（客户端获得 keymap）。
    void ensureVirtualKeyboard();

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

    // ---- 指针设备生命周期 ----
    // 设备销毁回调（审查 S1：指针设备无 destroy 监听 → 设备移除后
    // pointerDevices_ 残留悬垂指针，D-Bus 热应用遍历即 UAF）。
    static void handlePointerDestroy(wl_listener* l, void* data);

    // ---- 剪贴板/拖放请求 ----
    static void handleRequestSetSelection(wl_listener* l, void* data);
    static void handleRequestSetPrimarySelection(wl_listener* l, void* data);

    void processCursorMotion(uint32_t timeMsec);
    void processCursorButton(uint32_t timeMsec, uint32_t button,
                             enum wl_pointer_button_state state);
    void processKey(uint32_t timeMsec, uint32_t keycode, wl_keyboard_key_state state);
    // 快捷键动作分发（[shortcuts] 配置驱动，第二批）。
    void dispatchShortcut(ShortcutAction action);

    Compositor& compositor_;
    wlr_seat* seat_ = nullptr;
    wlr_cursor* cursor_ = nullptr;
    wlr_xcursor_manager* cursorMgr_ = nullptr;
    wlr_keyboard* keyboard_ = nullptr;

    // E8：默认 keymap（无真实键盘时虚拟注入用）+ 所属 xkb context。
    xkb_context* xkbContext_ = nullptr;
    xkb_keymap* defaultKeymap_ = nullptr;
    // 无真实键盘时合成虚拟 wlr_keyboard 绑定 seat（客户端借此收到 keymap，
    // wlr 0.19 无 wlr_seat_keyboard_notify_keymap）。
    wlr_keyboard* virtualKeyboard_ = nullptr;
    // 注入修饰键跟踪：wlr 0.19 的 seat keyboard_state 无 xkb_state，
    // 虚拟注入的 Shift/Ctrl/Alt 状态由独立 xkb_state 维护（真实键盘
    // 混合场景下修饰键以注入侧为准——屏幕键盘主场景为无实体键盘）。
    xkb_state* injectState_ = nullptr;
    // 审查 M3：injectState_ 所属 keymap（keymap 变化时重建）。
    xkb_keymap* injectStateKeymap_ = nullptr;
    bool keymapNotified_ = false;

    View* focusedView_ = nullptr;
    // M2b hover 打磨：当前 hover 的视图（装饰按钮高亮跟踪）。
    View* hoverView_ = nullptr;
    // M7 续：当前 hover 的 XWayland 视图（装饰按钮高亮跟踪）。
    XView* hoverXView_ = nullptr;
    // Alt+Tab 窗口切换器（Win10 风格）。
    std::unique_ptr<AltTabSwitcher> alttab_;
    // Snap 布局选择器（KDE-GAP #3：Win+Z 3×3 网格）。
    std::unique_ptr<SnapLayoutSwitcher> snaplayout_;
    // 输入设备设置（KDE-GAP 中优先；启动时从 [input] 加载）。
    w10de::ipc::InputSettings inputSettings_;
    // 指针设备条目（libinput 配置应用遍历用；含 destroy 监听防悬垂——
    // 审查 S1：设备热拔插后必须从 vector 移除，否则 D-Bus 热应用 UAF）。
    struct PointerDevice {
        Seat* seat = nullptr;
        wlr_input_device* device = nullptr;
        wl_listener destroyListener = {};
    };
    std::vector<std::unique_ptr<PointerDevice>> pointerDevices_;
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

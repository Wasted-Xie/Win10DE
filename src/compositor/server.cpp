#include "compositor/server.h"
#include "compositor/util.h"

#include <algorithm>
#include <cstdlib>  // free / getenv / strcmp
#include <cstring>
#include <unistd.h>  // fork / execlp / setsid

extern "C" {
#include <wlr/backend/headless.h>
#include <wlr/types/wlr_subcompositor.h>
}

#include "compositor/output.h"
#include "compositor/layer_shell.h"
#include "compositor/seat.h"
#include "compositor/view.h"
#include "compositor/xview.h"

namespace w10de {

Compositor::Compositor(CompositorOptions opts) : options_(std::move(opts)) {}

Compositor::~Compositor() {
    if (display_ == nullptr) {
        return;
    }
    // 清理顺序：
    // 1. 断开客户端 —— 触发各 View 的 destroy（delete this），视图列表清空；
    // 2. 销毁 seat（cursor/seat）；
    // 3. 销毁 Output（摘除 frame listener）—— 必须在 wlr_backend_destroy
    //    之前，否则 headless 后端销毁输出时 wlr_output_finish 会断言
    //    frame 事件监听列表为空而 abort；
    // 4. 销毁场景（连带 scene outputs）与输出布局；
    // 5. 依次销毁 allocator / renderer / backend / display。
    wl_display_destroy_clients(display_);
    seat_.reset();
    outputs_.clear();
    // 正常情况下 wl_display_destroy_clients 已触发各 LayerSurface 的
    // destroy 回调（delete this + 移除）；剩余者防御性清理。
    for (LayerSurface* ls : layerSurfaces_) {
        delete ls;
    }
    layerSurfaces_.clear();
    // 摘除后端/协议事件监听（此时 backend 仍存活）。
    wl_list_remove(&newOutputListener_.link);
    wl_list_remove(&newInputListener_.link);
    wl_list_remove(&newToplevelListener_.link);
    wl_list_remove(&newDecorationListener_.link);
    wl_list_remove(&newLayerSurfaceListener_.link);
    wl_list_remove(&newLockListener_.link);
    wl_list_remove(&lockNewSurfaceListener_.link);
    wl_list_remove(&lockSurfaceMapListener_.link);
    wl_list_remove(&lockUnlockListener_.link);
    wl_list_remove(&lockDestroyListener_.link);
    wl_list_remove(&lockSurfaceDestroyListener_.link);
    wl_list_remove(&newXSurfaceListener_.link);
    wl_list_remove(&xwaylandReadyListener_.link);
    // 显式销毁 XWayland（wlroots 要求 compositor 调用；display 销毁不会
    // 自动触发，否则 X server 子进程/xwm/XView 残留）。
    if (xwayland_ != nullptr) {
        wlr_xwayland_destroy(xwayland_);
        xwayland_ = nullptr;
    }
    if (scene_ != nullptr) {
        // 0.19 无 wlr_scene_destroy：scene 本身是根节点，经 tree.node 销毁。
        wlr_scene_node_destroy(&scene_->tree.node);
    }
    if (outputLayout_ != nullptr) {
        wlr_output_layout_destroy(outputLayout_);
    }
    if (allocator_ != nullptr) {
        wlr_allocator_destroy(allocator_);
    }
    if (renderer_ != nullptr) {
        wlr_renderer_destroy(renderer_);
    }
    if (backend_ != nullptr) {
        wlr_backend_destroy(backend_);
    }
    wl_display_destroy(display_);
}

bool Compositor::init() {
    wlr_log_init(options_.verbose ? WLR_DEBUG : WLR_INFO, nullptr);

    // listener 先初始化：init 中途失败走析构时 remove 安全。
    wl_list_init(&newOutputListener_.link);
    wl_list_init(&newInputListener_.link);
    wl_list_init(&newToplevelListener_.link);
    wl_list_init(&newDecorationListener_.link);
    wl_list_init(&newLayerSurfaceListener_.link);
    wl_list_init(&newLockListener_.link);
    wl_list_init(&lockNewSurfaceListener_.link);
    wl_list_init(&lockSurfaceMapListener_.link);
    wl_list_init(&lockUnlockListener_.link);
    wl_list_init(&lockDestroyListener_.link);
    wl_list_init(&lockSurfaceDestroyListener_.link);
    wl_list_init(&newXSurfaceListener_.link);
    wl_list_init(&xwaylandReadyListener_.link);

    display_ = wl_display_create();
    if (display_ == nullptr) {
        wlr_log(WLR_ERROR, "wl_display_create failed");
        return false;
    }
    wl_event_loop* loop = wl_display_get_event_loop(display_);

    // ---- 后端 ----
    // WLR_BACKEND=headless（默认，用于冒烟/CI）：显式创建并添加输出。
    // 其他值（wayland/drm 等）：交给 autocreate，并携带 session。
    const char* backendEnv = getenv("WLR_BACKEND");
    wlr_session* session = nullptr;
    if (backendEnv == nullptr || std::strcmp(backendEnv, "headless") == 0) {
        backend_ = wlr_headless_backend_create(loop);
        if (backend_ == nullptr) {
            wlr_log(WLR_ERROR, "failed to create headless backend");
            return false;
        }
        // headless 后端初始无输出；new_output 在 wlr_backend_start() 时触发。
        if (wlr_headless_add_output(backend_, options_.width, options_.height) == nullptr) {
            wlr_log(WLR_ERROR, "failed to add headless output");
            return false;
        }
    } else {
        backend_ = wlr_backend_autocreate(loop, &session);
        if (backend_ == nullptr) {
            wlr_log(WLR_ERROR, "failed to autocreate backend (WLR_BACKEND=%s)", backendEnv);
            return false;
        }
    }

    // ---- 渲染 ----
    renderer_ = wlr_renderer_autocreate(backend_);
    if (renderer_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create renderer");
        return false;
    }
    if (!wlr_renderer_init_wl_display(renderer_, display_)) {
        wlr_log(WLR_ERROR, "failed to init wl_display on renderer");
        return false;
    }

    allocator_ = wlr_allocator_autocreate(backend_, renderer_);
    if (allocator_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create allocator");
        return false;
    }

    // ---- 场景与协议 ----
    scene_ = wlr_scene_create();
    if (scene_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create scene");
        return false;
    }

    // 层锚树：z 序 background < bottom < view < top < overlay。
    // 注意：scene 后建者在上面，因此 bottom 锚必须在 viewAnchor 之前创建。
    // 窗口（View）挂 viewAnchor；层表面（任务栏/桌面/锁屏）挂对应锚；
    // 背景矩形挂 backgroundAnchor。
    backgroundAnchor_ = wlr_scene_tree_create(&scene_->tree);
    bottomAnchor_ = wlr_scene_tree_create(&scene_->tree);
    viewAnchor_ = wlr_scene_tree_create(&scene_->tree);
    topAnchor_ = wlr_scene_tree_create(&scene_->tree);
    overlayAnchor_ = wlr_scene_tree_create(&scene_->tree);
    if (backgroundAnchor_ == nullptr || viewAnchor_ == nullptr ||
            bottomAnchor_ == nullptr || topAnchor_ == nullptr ||
            overlayAnchor_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create layer anchor trees");
        return false;
    }

    // wl_compositor：客户端创建 surface 的入口。
    compositor_ = wlr_compositor_create(display_, 4, renderer_);
    if (compositor_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create wl_compositor");
        return false;
    }
    if (wlr_subcompositor_create(display_) == nullptr) {
        wlr_log(WLR_ERROR, "failed to create wl_subcompositor");
        return false;
    }

    // 输出布局：多输出的 2D 坐标空间；scene 输出位置随之同步。
    outputLayout_ = wlr_output_layout_create(display_);
    if (outputLayout_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create output layout");
        return false;
    }
    // scene 输出布局关联：Output 创建 scene output 后需显式 add_output
    // 同步位置（attach 本身不会自动关联已有 scene output）。
    sceneOutputLayout_ = wlr_scene_attach_output_layout(scene_, outputLayout_);
    if (sceneOutputLayout_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to attach output layout to scene");
        return false;
    }

    // xdg-shell：客户端顶层窗口。
    xdgShell_ = wlr_xdg_shell_create(display_, 3);
    if (xdgShell_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create xdg-shell");
        return false;
    }

    // xdg-decoration：向客户端声明服务端装饰（Win10 统一标题栏视觉）。
    // M2a 强制 SSD；客户端（如 GTK/Qt）将因此不绘制自己的标题栏。
    decorationManager_ = wlr_xdg_decoration_manager_v1_create(display_);
    if (decorationManager_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create xdg-decoration manager");
        return false;
    }

    // layer-shell：Shell UI（任务栏/桌面/锁屏）的层表面协议（M3 前置）。
    layerShell_ = wlr_layer_shell_v1_create(display_, 4);
    if (layerShell_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create layer-shell");
        return false;
    }

    // foreign-toplevel：任务栏窗口列表协议（M3 前置）。
    foreignToplevelManager_ = wlr_foreign_toplevel_manager_v1_create(display_);
    if (foreignToplevelManager_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create foreign-toplevel manager");
        return false;
    }

    // ---- 输入 ----
    seat_ = std::make_unique<Seat>(*this);
    if (seat_->seat() == nullptr) {
        wlr_log(WLR_ERROR, "failed to create seat");
        return false;
    }

    // ---- 事件监听 ----
    newOutputListener_.notify = handleNewOutput;
    wl_signal_add(&backend_->events.new_output, &newOutputListener_);
    newInputListener_.notify = handleNewInput;
    wl_signal_add(&backend_->events.new_input, &newInputListener_);
    newToplevelListener_.notify = handleNewToplevel;
    wl_signal_add(&xdgShell_->events.new_toplevel, &newToplevelListener_);
    newDecorationListener_.notify = handleNewDecoration;
    wl_signal_add(&decorationManager_->events.new_toplevel_decoration, &newDecorationListener_);
    newLayerSurfaceListener_.notify = handleNewLayerSurface;
    wl_signal_add(&layerShell_->events.new_surface, &newLayerSurfaceListener_);

    // ---- 锁屏（ext-session-lock-v1，M6）----
    sessionLockManager_ = wlr_session_lock_manager_v1_create(display_);
    if (sessionLockManager_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create session-lock manager");
        return false;
    }
    newLockListener_.notify = handleNewLock;
    wl_signal_add(&sessionLockManager_->events.new_lock, &newLockListener_);

    // ---- XWayland（X11 客户端兼容，M7；lazy：有 X11 客户端才启动）----
    xwayland_ = wlr_xwayland_create(display_, compositor_, true);
    if (xwayland_ == nullptr) {
        // 非致命：headless/无 X11 环境（如 WSL）下 XWayland 不可用，
        // Wayland 功能不受影响；仅 X11 客户端兼容缺失。
        wlr_log(WLR_INFO, "xwayland unavailable, X11 clients not supported");
    } else {
        wlr_xwayland_set_seat(xwayland_, seat_->seat());
        newXSurfaceListener_.notify = handleNewXSurface;
        wl_signal_add(&xwayland_->events.new_surface, &newXSurfaceListener_);
        xwaylandReadyListener_.notify = handleXWaylandReady;
        wl_signal_add(&xwayland_->events.ready, &xwaylandReadyListener_);
    }

    if (!wlr_backend_start(backend_)) {
        wlr_log(WLR_ERROR, "failed to start backend");
        return false;
    }
    return true;
}

int Compositor::run() {
    const char* socket = nullptr;
    std::string ownedSocket;  // add_socket_auto 返回的 strdup 内存
    if (!options_.socketName.empty()) {
        if (wl_display_add_socket(display_, options_.socketName.c_str()) != 0) {
            wlr_log(WLR_ERROR, "failed to add socket '%s'", options_.socketName.c_str());
            return 1;
        }
        socket = options_.socketName.c_str();
    } else {
        const char* autoSocket = wl_display_add_socket_auto(display_);
        if (autoSocket == nullptr) {
            wlr_log(WLR_ERROR, "failed to add display socket");
            return 1;
        }
        // 注意：返回值指向 display 内部存储的 socket 名（非 strdup），
        // 不能 free——真实编译验证（free 触发 invalid pointer 崩溃）。
        ownedSocket = autoSocket;
        socket = ownedSocket.c_str();
    }
    wlr_log(WLR_INFO, "Win10DE compositor (M7) running on wayland socket '%s'", socket);

    wl_display_run(display_);
    wlr_log(WLR_INFO, "compositor exiting with code %d", exitCode_);
    return exitCode_;
}

// ---- 视图列表 ----

void Compositor::addView(View* view) {
    if (std::find(views_.begin(), views_.end(), view) == views_.end()) {
        views_.push_back(view);
    }
}

void Compositor::removeView(View* view) {
    views_.erase(std::remove(views_.begin(), views_.end(), view), views_.end());
    if (seat_ != nullptr) {
        seat_->onViewDestroyed(view);
    }
}

void Compositor::raiseView(View* view) {
    auto it = std::find(views_.begin(), views_.end(), view);
    if (it != views_.end()) {
        views_.erase(it);
        views_.push_back(view);
    }
    // 窗口由内容（sceneTree_）与装饰（decorationTree_）两个节点组成。
    // 先置顶内容，再把装饰放到内容之上：保证窗口整体置顶且装饰在最上。
    if (view->sceneTree() != nullptr) {
        wlr_scene_node_raise_to_top(&view->sceneTree()->node);
        wlr_scene_tree* decoration = view->decorationTree();
        if (decoration != nullptr) {
            wlr_scene_node_place_above(&decoration->node, &view->sceneTree()->node);
        }
    }
}

// ---- XWayland 窗口列表 ----

void Compositor::addXView(XView* view) {
    if (std::find(xviews_.begin(), xviews_.end(), view) == xviews_.end()) {
        xviews_.push_back(view);
    }
}

void Compositor::removeXView(XView* view) {
    xviews_.erase(std::remove(xviews_.begin(), xviews_.end(), view), xviews_.end());
}

void Compositor::raiseXView(XView* view) {
    auto it = std::find(xviews_.begin(), xviews_.end(), view);
    if (it != xviews_.end()) {
        xviews_.erase(it);
        xviews_.push_back(view);
    }
    // XWayland 窗口无独立装饰节点（M8 统一），scene 节点置顶即可。
    wlr_scene_surface* sceneSurface = view->sceneSurface();
    if (sceneSurface != nullptr) {
        wlr_scene_node_raise_to_top(&sceneSurface->buffer->node);
    }
}

bool Compositor::outputSize(int* width, int* height) const {
    if (outputs_.empty()) {
        return false;
    }
    wlr_output_effective_resolution(outputs_.front()->wlr(), width, height);
    return *width > 0 && *height > 0;
}

wlr_output* Compositor::firstOutput() const {
    return outputs_.empty() ? nullptr : outputs_.front()->wlr();
}

// ---- 事件回调 ----

void Compositor::handleNewOutput(wl_listener* listener, void* data) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, newOutputListener_);
    auto* output = static_cast<wlr_output*>(data);

    auto out = std::make_unique<Output>(*compositor, output);
    if (!out->isValid()) {
        wlr_log(WLR_ERROR, "failed to initialize output '%s'", output->name);
        return;
    }
    compositor->outputs_.push_back(std::move(out));
}

void Compositor::handleNewInput(wl_listener* listener, void* data) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, newInputListener_);
    auto* device = static_cast<wlr_input_device*>(data);
    compositor->seat_->handleNewInput(device);
}

void Compositor::handleNewToplevel(wl_listener* listener, void* data) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, newToplevelListener_);
    auto* toplevel = static_cast<wlr_xdg_toplevel*>(data);

    auto* view = new View(*compositor, toplevel);
    if (view->sceneTree() == nullptr) {
        wlr_log(WLR_ERROR, "failed to create view for toplevel");
        delete view;
        return;
    }
    // 视图所有权为自身（destroy 回调中 delete this），此处仅保留列表引用。
    compositor->addView(view);
}

void Compositor::handleNewDecoration(wl_listener* listener, void* data) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, newDecorationListener_);
    auto* decoration = static_cast<wlr_xdg_toplevel_decoration_v1*>(data);
    // 强制服务端装饰（Win10 统一标题栏）。客户端请求（request_mode）
    // 在本里程碑被忽略；M8 视觉打磨时支持客户端切换。
    // 注意：set_mode 内部调用 wlr_xdg_surface_schedule_configure，在 surface
    // 未初始化（客户端首次 commit 前）时断言 abort——真实运行验证：Qt 在
    // 首 commit 前即请求 decoration。未初始化则挂 commit 监听延迟设置。
    wlr_xdg_surface* xdgSurface = decoration->toplevel->base;
    if (xdgSurface->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        compositor->pendingDecoration_ = decoration;
        compositor->decorationCommitListener_.notify = handleDecorationCommit;
        wl_signal_add(&xdgSurface->surface->events.commit,
                      &compositor->decorationCommitListener_);
        compositor->decorationDestroyListener_.notify = handleDecorationDestroy;
        wl_signal_add(&decoration->events.destroy,
                      &compositor->decorationDestroyListener_);
    }
    wlr_log(WLR_DEBUG, "toplevel decoration: server-side (direct or deferred)");
}

void Compositor::handleDecorationCommit(wl_listener* listener, void* /*data*/) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, decorationCommitListener_);
    if (compositor->pendingDecoration_ == nullptr) {
        return;
    }
    wlr_xdg_surface* xdgSurface = compositor->pendingDecoration_->toplevel->base;
    if (!xdgSurface->initialized) {
        return;  // 尚未初始化：继续等待下次 commit
    }
    // surface 已初始化：应用 SSD 并清理一次性监听。
    wl_list_remove(&compositor->decorationCommitListener_.link);
    wl_list_init(&compositor->decorationCommitListener_.link);
    wl_list_remove(&compositor->decorationDestroyListener_.link);
    wl_list_init(&compositor->decorationDestroyListener_.link);
    wlr_xdg_toplevel_decoration_v1* decoration = compositor->pendingDecoration_;
    compositor->pendingDecoration_ = nullptr;
    wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void Compositor::handleDecorationDestroy(wl_listener* listener, void* /*data*/) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, decorationDestroyListener_);
    // pending decoration 在初始化前被销毁（客户端断开等）：清理监听。
    if (compositor->pendingDecoration_ != nullptr) {
        wl_list_remove(&compositor->decorationCommitListener_.link);
        wl_list_init(&compositor->decorationCommitListener_.link);
        wl_list_remove(&compositor->decorationDestroyListener_.link);
        wl_list_init(&compositor->decorationDestroyListener_.link);
        compositor->pendingDecoration_ = nullptr;
    }
}

void Compositor::handleNewLayerSurface(wl_listener* listener, void* data) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, newLayerSurfaceListener_);
    auto* surface = static_cast<wlr_layer_surface_v1*>(data);

    auto* layerSurface = new LayerSurface(*compositor, surface);
    if (!layerSurface->isValid()) {
        wlr_log(WLR_ERROR, "failed to initialize layer surface");
        delete layerSurface;
        return;
    }
    // 所有权为自身（destroy 回调 delete this），此处仅保留列表引用。
    compositor->layerSurfaces_.push_back(layerSurface);
    // 注意：此处不能 arrange——new_surface 在 get_layer_surface 请求处理中
    // 同步发出，此时 surface->initialized 尚为 false，wlr_layer_surface_v1_
    // configure 内部会 assert 崩溃；首次 arrange 由首次 commit 的
    // handleCommit 触发（此时 current 状态已就绪）。
}

void Compositor::arrangeLayers() {
    for (const auto& out : outputs_) {
        wlr_output* output = out->wlr();
        // 输出必须已在布局中（wlr_output_layout_get_box 对未加入的输出
        // 返回空 box，几何会全错）。
        if (wlr_output_layout_get(outputLayout_, output) == nullptr) {
            continue;
        }
        wlr_box fullArea;
        wlr_output_layout_get_box(outputLayout_, output, &fullArea);
        wlr_box usableArea = fullArea;
        // 按层序（background → bottom → top → overlay）逐层排列；
        // 同层按加入顺序（列表顺序）。独占区逐层递减可用区域。
        // 只跳过未初始化（initialized==false，尚未首次 commit）的表面——
        // wlr_layer_surface_v1_configure 仅 assert(initialized)；**未 map 的
        // 表面也必须 arrange**（首次 configure 是客户端 attach buffer 从而
        // map 的前提；若按 mapped 过滤会死锁——真实运行验证：Qt 层表面
        // 永远等不到 configure 而无法 map）。
        for (int layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
                layer <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; ++layer) {
            for (LayerSurface* ls : layerSurfaces_) {
                wlr_layer_surface_v1* l = ls->layer();
                if (!l->initialized) {
                    continue;
                }
                if (l->output == output &&
                        static_cast<int>(l->pending.layer) == layer) {
                    ls->arrange(&fullArea, &usableArea);
                }
            }
        }
        // 缓存该输出的最终可用区（窗口最大化避开任务栏等独占区）。
        usableAreas_[output] = usableArea;
    }
}

bool Compositor::outputUsableSize(wlr_output* output, int* width, int* height) const {
    auto it = usableAreas_.find(output);
    if (it != usableAreas_.end() && it->second.width > 0 && it->second.height > 0) {
        *width = it->second.width;
        *height = it->second.height;
        return true;
    }
    // 尚未排过层表面（或输出无独占区）：回退到输出分辨率。
    wlr_output_effective_resolution(output, width, height);
    return *width > 0 && *height > 0;
}

void Compositor::removeLayerSurface(LayerSurface* layerSurface) {
    layerSurfaces_.erase(
        std::remove(layerSurfaces_.begin(), layerSurfaces_.end(), layerSurface),
        layerSurfaces_.end());
}

wlr_scene_tree* Compositor::layerAnchor(int layer) const {
    switch (layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return backgroundAnchor_;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return bottomAnchor_;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        return overlayAnchor_;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
    default:
        return topAnchor_;
    }
}

// ---- 锁屏（ext-session-lock-v1，M6）----

wlr_surface* Compositor::lockSurface() const {
    if (sessionLockSurface_ == nullptr) {
        return nullptr;
    }
    return sessionLockSurface_->surface;
}

void Compositor::setSessionLocked(bool locked) {
    if (sessionLocked_ == locked) {
        return;
    }
    sessionLocked_ = locked;
    // 隐藏/恢复普通内容（除 lock surface 外：它挂在 scene 根，不受锚影响）。
    // 锁定期间桌面/任务栏/开始菜单/窗口全部不可见、不可交互。
    wlr_scene_node_set_enabled(&backgroundAnchor_->node, !locked);
    wlr_scene_node_set_enabled(&bottomAnchor_->node, !locked);
    wlr_scene_node_set_enabled(&viewAnchor_->node, !locked);
    wlr_scene_node_set_enabled(&topAnchor_->node, !locked);
    wlr_scene_node_set_enabled(&overlayAnchor_->node, !locked);
    if (seat_ != nullptr) {
        // 清空焦点：锁定后只有 lock surface 可交互（map 时聚焦）。
        seat_->unfocusAll();
    }
    wlr_log(WLR_INFO, "session %s", locked ? "locked" : "unlocked");
}

void Compositor::launchLockScreen() {
    // 启动锁屏进程（通过 PATH 查找 w10lock）。锁屏自身是 session-lock
    // 客户端，锁定/解锁由协议驱动，本函数只负责拉起进程。
    const pid_t pid = fork();
    if (pid == 0) {
        // 子进程：脱离会话，exec 锁屏。
        setsid();
        execlp("w10lock", "w10lock", nullptr);
        _exit(127);  // exec 失败（未找到）
    }
    if (pid < 0) {
        wlr_log(WLR_ERROR, "failed to fork w10lock");
    } else {
        wlr_log(WLR_INFO, "launched lock screen (pid %d)", pid);
    }
}

void Compositor::handleNewLock(wl_listener* listener, void* data) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, newLockListener_);
    auto* lock = static_cast<wlr_session_lock_v1*>(data);
    if (compositor->sessionLock_ != nullptr) {
        // 不支持的第二个锁：销毁（发送 finished），避免客户端永久等待 locked。
        wlr_log(WLR_ERROR, "multiple session locks not supported; rejecting");
        wlr_session_lock_v1_destroy(lock);
        return;
    }
    compositor->sessionLock_ = lock;
    // 确认锁定（客户端等待 locked 事件后才认为锁屏生效）。
    wlr_session_lock_v1_send_locked(lock);

    compositor->lockNewSurfaceListener_.notify = handleLockNewSurface;
    wl_signal_add(&lock->events.new_surface, &compositor->lockNewSurfaceListener_);
    compositor->lockUnlockListener_.notify = handleLockUnlock;
    wl_signal_add(&lock->events.unlock, &compositor->lockUnlockListener_);
    compositor->lockDestroyListener_.notify = handleLockDestroy;
    wl_signal_add(&lock->events.destroy, &compositor->lockDestroyListener_);
}

void Compositor::handleLockNewSurface(wl_listener* listener, void* data) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, lockNewSurfaceListener_);
    auto* lockSurface = static_cast<wlr_session_lock_surface_v1*>(data);
    if (compositor->sessionLockSurface_ != nullptr) {
        wlr_log(WLR_ERROR, "multiple lock surfaces not supported; ignoring");
        return;
    }
    compositor->sessionLockSurface_ = lockSurface;

    // scene 节点：挂 scene 根（锁定期间唯一可见内容）。
    compositor->sessionLockSceneSurface_ =
        wlr_scene_surface_create(&compositor->scene_->tree, lockSurface->surface);
    if (compositor->sessionLockSceneSurface_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create scene surface for lock");
        compositor->sessionLockSurface_ = nullptr;  // 避免悬垂指针
        return;
    }

    // map 时锁定并聚焦；销毁时清理。
    compositor->lockSurfaceMapListener_.notify = handleLockSurfaceMap;
    wl_signal_add(&lockSurface->surface->events.map, &compositor->lockSurfaceMapListener_);
    compositor->lockSurfaceDestroyListener_.notify = handleLockSurfaceDestroy;
    wl_signal_add(&lockSurface->events.destroy, &compositor->lockSurfaceDestroyListener_);

    // configure 全屏（取首个输出尺寸；M6 单锁屏 surface）。
    // 无输出时用配置尺寸兜底（否则 surface 永不 configure，
    // 客户端 commit 触发 COMMIT_BEFORE_FIRST_ACK 协议错误）。
    int w = 0, h = 0;
    if (!compositor->outputSize(&w, &h)) {
        w = compositor->options().width;
        h = compositor->options().height;
        wlr_log(WLR_INFO, "no output for lock surface; using fallback %dx%d", w, h);
    }
    wlr_session_lock_surface_v1_configure(lockSurface, w, h);
}

void Compositor::handleLockSurfaceMap(wl_listener* listener, void* /*data*/) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, lockSurfaceMapListener_);
    // 锁屏 surface 显示：置顶、隐藏普通内容、聚焦锁屏。
    if (compositor->sessionLockSceneSurface_ != nullptr) {
        wlr_scene_node_raise_to_top(&compositor->sessionLockSceneSurface_->buffer->node);
    }
    compositor->setSessionLocked(true);
    if (wlr_surface* surface = compositor->lockSurface(); surface != nullptr) {
        compositor->seat_->focusSurface(surface, true);
    }
}

void Compositor::handleLockUnlock(wl_listener* listener, void* /*data*/) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, lockUnlockListener_);
    compositor->setSessionLocked(false);
    // 立即禁用锁屏场景节点（客户端销毁 surface 前有窗口期，防止画面残留）。
    if (compositor->sessionLockSceneSurface_ != nullptr) {
        wlr_scene_node_set_enabled(
            &compositor->sessionLockSceneSurface_->buffer->node, false);
    }
    // 摘除旧 lock 上的监听（unlock 后 lock 对象即将销毁；不依赖 wlroots
    // 内部实现细节，避免监听串挂到下一个 lock 对象）。
    wl_list_remove(&compositor->lockNewSurfaceListener_.link);
    wl_list_init(&compositor->lockNewSurfaceListener_.link);
    wl_list_remove(&compositor->lockUnlockListener_.link);
    wl_list_init(&compositor->lockUnlockListener_.link);
    wl_list_remove(&compositor->lockDestroyListener_.link);
    wl_list_init(&compositor->lockDestroyListener_.link);
    // 摘除 lock surface 上的监听：unlock 后 surface 即将随 lock 对象销毁，
    // 显式摘除避免依赖 wlroots 内部的级联销毁顺序。
    wl_list_remove(&compositor->lockSurfaceMapListener_.link);
    wl_list_init(&compositor->lockSurfaceMapListener_.link);
    wl_list_remove(&compositor->lockSurfaceDestroyListener_.link);
    wl_list_init(&compositor->lockSurfaceDestroyListener_.link);
    compositor->sessionLock_ = nullptr;  // 允许新的锁屏请求
    compositor->sessionLockSurface_ = nullptr;
    compositor->sessionLockSceneSurface_ = nullptr;
}

void Compositor::handleLockDestroy(wl_listener* listener, void* /*data*/) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, lockDestroyListener_);
    // 锁对象销毁（客户端崩溃/正常结束）：先摘除挂在其事件上的监听，
    // 否则 wlr_session_lock_v1_destroy 的 assert(empty(listeners)) 会 abort。
    wl_list_remove(&compositor->lockNewSurfaceListener_.link);
    wl_list_init(&compositor->lockNewSurfaceListener_.link);
    wl_list_remove(&compositor->lockUnlockListener_.link);
    wl_list_init(&compositor->lockUnlockListener_.link);
    wl_list_remove(&compositor->lockDestroyListener_.link);
    wl_list_init(&compositor->lockDestroyListener_.link);
    // 立即禁用锁屏场景节点（lock 对象销毁后 surface 即将销毁，防止画面残留）。
    if (compositor->sessionLockSceneSurface_ != nullptr) {
        wlr_scene_node_set_enabled(
            &compositor->sessionLockSceneSurface_->buffer->node, false);
    }
    // 恢复会话。
    compositor->setSessionLocked(false);
    compositor->sessionLock_ = nullptr;
    compositor->sessionLockSurface_ = nullptr;
    compositor->sessionLockSceneSurface_ = nullptr;
}

void Compositor::handleLockSurfaceDestroy(wl_listener* listener, void* /*data*/) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, lockSurfaceDestroyListener_);
    // 先摘除挂在该 surface/对象上的监听，避免 wlroots 销毁断言。
    wl_list_remove(&compositor->lockSurfaceMapListener_.link);
    wl_list_init(&compositor->lockSurfaceMapListener_.link);
    wl_list_remove(&compositor->lockSurfaceDestroyListener_.link);
    wl_list_init(&compositor->lockSurfaceDestroyListener_.link);
    compositor->sessionLockSurface_ = nullptr;
    compositor->sessionLockSceneSurface_ = nullptr;
}

// ---- XWayland ----

void Compositor::handleNewXSurface(wl_listener* listener, void* data) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, newXSurfaceListener_);
    auto* xsurface = static_cast<wlr_xwayland_surface*>(data);
    // 所有权为自身（destroy 回调 delete this）；map 时加入列表。
    auto* view = new XView(*compositor, xsurface);
    if (view->xsurface() == nullptr) {
        wlr_log(WLR_ERROR, "failed to create xview");
        delete view;
    }
}

void Compositor::handleXWaylandReady(wl_listener* listener, void* /*data*/) {
    auto* compositor = W10DE_CONTAINER_OF(listener, Compositor, xwaylandReadyListener_);
    // XWayland 就绪：向环境暴露 DISPLAY（X11 客户端连接用）。
    if (compositor->xwayland_ != nullptr &&
            compositor->xwayland_->display_name != nullptr) {
        setenv("DISPLAY", compositor->xwayland_->display_name, 1);
        wlr_log(WLR_INFO, "xwayland ready on DISPLAY=%s",
                compositor->xwayland_->display_name);
    }
}

}  // namespace w10de

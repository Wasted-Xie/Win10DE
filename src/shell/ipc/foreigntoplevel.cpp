#include "ipc/foreigntoplevel.h"

#include <algorithm>
#include <cstring>

#include <QGuiApplication>

#include <QtGui/qpa/qplatformnativeinterface.h>

#include <wayland-client.h>

namespace w10de {

// ---- ForeignToplevelHandle ----

namespace {

// 协议监听器表（wayland-scanner 生成的结构）。
const zwlr_foreign_toplevel_handle_v1_listener kHandleListener = {
    .title = ForeignToplevelHandle::onTitle,
    .app_id = ForeignToplevelHandle::onAppId,
    .output_enter = ForeignToplevelHandle::onOutputEnter,
    .output_leave = ForeignToplevelHandle::onOutputLeave,
    .state = ForeignToplevelHandle::onState,
    .done = ForeignToplevelHandle::onDone,
    .closed = ForeignToplevelHandle::onClosed,
    .parent = ForeignToplevelHandle::onParent,
};

}  // namespace

ForeignToplevelHandle::ForeignToplevelHandle(zwlr_foreign_toplevel_handle_v1* handle,
                                             wl_seat* seat)
    : handle_(handle), seat_(seat) {
    zwlr_foreign_toplevel_handle_v1_add_listener(handle_, &kHandleListener, this);
}

ForeignToplevelHandle::~ForeignToplevelHandle() {
    // 主动销毁协议对象（closed 后或析构时；合成器侧对象随 closed 失效）。
    if (handle_ != nullptr) {
        zwlr_foreign_toplevel_handle_v1_destroy(handle_);
    }
}

void ForeignToplevelHandle::activate() {
    if (seat_ == nullptr) {
        return;
    }
    // 协议参数是 wl_seat* 对象（非名称字符串）。
    zwlr_foreign_toplevel_handle_v1_activate(handle_, seat_);
}

void ForeignToplevelHandle::close() {
    zwlr_foreign_toplevel_handle_v1_close(handle_);
}

void ForeignToplevelHandle::setMaximized(bool maximized) {
    if (maximized) {
        zwlr_foreign_toplevel_handle_v1_set_maximized(handle_);
    } else {
        zwlr_foreign_toplevel_handle_v1_unset_maximized(handle_);
    }
}

void ForeignToplevelHandle::setMinimized(bool minimized) {
    if (minimized) {
        zwlr_foreign_toplevel_handle_v1_set_minimized(handle_);
    } else {
        zwlr_foreign_toplevel_handle_v1_unset_minimized(handle_);
    }
}

// ---- 协议事件回调 ----

void ForeignToplevelHandle::onTitle(void* data, zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                    const char* title) {
    auto* self = static_cast<ForeignToplevelHandle*>(data);
    self->title_ = QString::fromUtf8(title);
}

void ForeignToplevelHandle::onAppId(void* data, zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                    const char* appId) {
    auto* self = static_cast<ForeignToplevelHandle*>(data);
    self->appId_ = QString::fromUtf8(appId);
}

void ForeignToplevelHandle::onOutputEnter(void* data, zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                          wl_output* /*output*/) {
    // M3 暂不按输出区分窗口（单输出场景）。
    Q_UNUSED(data)
}

void ForeignToplevelHandle::onOutputLeave(void* data, zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                          wl_output* /*output*/) {
    Q_UNUSED(data)
}

void ForeignToplevelHandle::onState(void* data, zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                    wl_array* state) {
    auto* self = static_cast<ForeignToplevelHandle*>(data);
    self->stateMaximized_ = false;
    self->stateMinimized_ = false;
    self->stateActivated_ = false;
    self->stateFullscreen_ = false;
    // state 数组元素是协议枚举值（uint32）。
    const uint32_t* it = static_cast<const uint32_t*>(state->data);
    const uint32_t* end = it + state->size / sizeof(uint32_t);
    for (; it < end; ++it) {
        switch (*it) {
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
            self->stateMaximized_ = true;
            break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
            self->stateMinimized_ = true;
            break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
            self->stateActivated_ = true;
            break;
        case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
            self->stateFullscreen_ = true;
            break;
        default:
            break;
        }
    }
}

void ForeignToplevelHandle::onDone(void* data, zwlr_foreign_toplevel_handle_v1* /*handle*/) {
    // 一批属性更新结束：通知 UI 刷新。
    auto* self = static_cast<ForeignToplevelHandle*>(data);
    emit self->changed();
}

void ForeignToplevelHandle::onClosed(void* data, zwlr_foreign_toplevel_handle_v1* /*handle*/) {
    auto* self = static_cast<ForeignToplevelHandle*>(data);
    emit self->closed();
}

void ForeignToplevelHandle::onParent(void* data, zwlr_foreign_toplevel_handle_v1* /*handle*/,
                                     zwlr_foreign_toplevel_handle_v1* /*parent*/) {
    // M3 暂不处理窗口父子关系（对话框归属等）。
    Q_UNUSED(data)
}

// ---- ForeignToplevelManager ----

namespace {

const zwlr_foreign_toplevel_manager_v1_listener kManagerListener = {
    .toplevel = ForeignToplevelManager::managerToplevel,
    .finished = ForeignToplevelManager::managerFinished,
};

const wl_registry_listener kRegistryListener = {
    .global = ForeignToplevelManager::registryGlobal,
    .global_remove = ForeignToplevelManager::registryGlobalRemove,
};

}  // namespace

ForeignToplevelManager::ForeignToplevelManager(QObject* parent) : QObject(parent) {
    // 获取 Wayland display（Qt 6 的 QNativeInterface::QWaylandApplication）。
    auto* waylandApp =
        qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    wl_display* display = waylandApp != nullptr ? waylandApp->display() : nullptr;
    if (display == nullptr) {
        qWarning("ForeignToplevelManager: no Wayland display (not running on Wayland?)");
        return;
    }

    registry_ = wl_display_get_registry(display);
    if (registry_ == nullptr) {
        qWarning("ForeignToplevelManager: failed to get registry");
        return;
    }
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    // roundtrip 移入 start()：调用方先 connect 信号再启动，
    // 避免初始窗口事件在 connect 前丢失。
}

void ForeignToplevelManager::start() {
    wl_display* display = nullptr;
    if (auto* waylandApp =
            qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        waylandApp != nullptr) {
        display = waylandApp->display();
    }
    if (display == nullptr) {
        return;
    }
    wl_display_roundtrip(display);  // 等待 global 通告
    wl_display_roundtrip(display);  // 等待 manager 的初始 toplevel 事件
}

ForeignToplevelManager::~ForeignToplevelManager() {
    // 停止监听并释放（协议约定 manager 不销毁，stop 后由合成器 finished）。
    if (manager_ != nullptr) {
        zwlr_foreign_toplevel_manager_v1_stop(manager_);
    }
    qDeleteAll(handles_);
    handles_.clear();
    if (registry_ != nullptr) {
        wl_registry_destroy(registry_);
    }
}

void ForeignToplevelManager::registryGlobal(void* data, wl_registry* registry, uint32_t name,
                                            const char* interface, uint32_t version) {
    auto* self = static_cast<ForeignToplevelManager*>(data);
    if (std::strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        // 协议 version 3（含 parent 事件）。
        self->manager_ = static_cast<zwlr_foreign_toplevel_manager_v1*>(
            wl_registry_bind(registry, name,
                             &zwlr_foreign_toplevel_manager_v1_interface,
                             std::min<uint32_t>(version, 3)));
        zwlr_foreign_toplevel_manager_v1_add_listener(self->manager_, &kManagerListener, self);
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0 && self->seat_ == nullptr) {
        // activate 请求需要 wl_seat 对象。
        self->seat_ = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface,
                             std::min<uint32_t>(version, 7)));
    }
}

void ForeignToplevelManager::registryGlobalRemove(void* /*data*/, wl_registry* /*registry*/,
                                                  uint32_t /*name*/) {
    // M3 不处理 manager 热移除。
}

void ForeignToplevelManager::managerToplevel(void* data,
                                             zwlr_foreign_toplevel_manager_v1* /*manager*/,
                                             zwlr_foreign_toplevel_handle_v1* handle) {
    auto* self = static_cast<ForeignToplevelManager*>(data);
    auto* toplevel = new ForeignToplevelHandle(handle, self->seat_);
    self->handles_.push_back(toplevel);
    // closed 时从列表移除并销毁（Handle 析构销毁协议对象）。
    QObject::connect(toplevel, &ForeignToplevelHandle::closed, self, [self, toplevel]() {
        self->handles_.erase(std::remove(self->handles_.begin(), self->handles_.end(), toplevel),
                             self->handles_.end());
        emit self->toplevelRemoved(toplevel);
        toplevel->deleteLater();
    });
    emit self->toplevelAdded(toplevel);
}

void ForeignToplevelManager::managerFinished(void* data,
                                             zwlr_foreign_toplevel_manager_v1* /*manager*/) {
    // 合成器销毁 manager（对象已失效）：置空，析构时不再 stop（避免向
    // 已销毁对象发请求触发协议错误）。
    auto* self = static_cast<ForeignToplevelManager*>(data);
    self->manager_ = nullptr;
}

}  // namespace w10de

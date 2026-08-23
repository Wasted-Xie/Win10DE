// foreign-toplevel 客户端绑定：任务栏窗口列表的数据源。
//
// 通过 wlr-foreign-toplevel-management-unstable-v1 协议获取合成器管理的
// 窗口列表（标题/应用/状态），并支持任务栏操作（激活/关闭/最大化/最小化）。
// 协议代码由 wayland-scanner 在构建时生成（CMake 中配置）。
#pragma once

#include <QObject>
#include <QString>
#include <vector>

extern "C" {
#include <wayland-client-core.h>
#include "foreign-toplevel-client-protocol.h"
}

// wl_seat 在 wayland-client-protocol.h 中定义；此处前向声明即可（仅指针成员）。
struct wl_seat;

namespace w10de {

// 一个远程窗口（任务栏项的数据模型）。
class ForeignToplevelHandle : public QObject {
    Q_OBJECT
public:
    explicit ForeignToplevelHandle(zwlr_foreign_toplevel_handle_v1* handle,
                                   wl_seat* seat);
    ~ForeignToplevelHandle() override;

    QString title() const { return title_; }
    QString appId() const { return appId_; }
    bool maximized() const { return stateMaximized_; }
    bool minimized() const { return stateMinimized_; }
    bool activated() const { return stateActivated_; }
    bool fullscreen() const { return stateFullscreen_; }

public slots:
    // 任务栏操作：请求合成器执行。
    void activate();
    void close();
    void setMaximized(bool maximized);
    void setMinimized(bool minimized);

signals:
    // 属性（标题/状态）更新完毕（done 事件）。
    void changed();
    // 窗口关闭（closed 事件）。
    void closed();

public:
    // 协议事件回调必须 public：命名空间级 listener 结构体（wayland-scanner
    // 生成的 add_listener 表）在类外取地址，private 会编译失败。
    // ---- 协议事件回调（静态 C 函数，data 为 ForeignToplevelHandle*）----
    static void onTitle(void* data, zwlr_foreign_toplevel_handle_v1* h, const char* title);
    static void onAppId(void* data, zwlr_foreign_toplevel_handle_v1* h, const char* appId);
    static void onOutputEnter(void* data, zwlr_foreign_toplevel_handle_v1* h, wl_output* output);
    static void onOutputLeave(void* data, zwlr_foreign_toplevel_handle_v1* h, wl_output* output);
    static void onState(void* data, zwlr_foreign_toplevel_handle_v1* h, wl_array* state);
    static void onDone(void* data, zwlr_foreign_toplevel_handle_v1* h);
    static void onClosed(void* data, zwlr_foreign_toplevel_handle_v1* h);
    static void onParent(void* data, zwlr_foreign_toplevel_handle_v1* h,
                         zwlr_foreign_toplevel_handle_v1* parent);

private:
    zwlr_foreign_toplevel_handle_v1* handle_ = nullptr;
    wl_seat* seat_ = nullptr;  // activate 请求用
    QString title_;
    QString appId_;
    bool stateMaximized_ = false;
    bool stateMinimized_ = false;
    bool stateActivated_ = false;
    bool stateFullscreen_ = false;
};

// 合成器 foreign-toplevel 管理器（绑定协议全局，跟踪全部窗口）。
class ForeignToplevelManager : public QObject {
    Q_OBJECT
public:
    explicit ForeignToplevelManager(QObject* parent = nullptr);
    ~ForeignToplevelManager() override;

    bool isValid() const { return manager_ != nullptr; }
    const std::vector<ForeignToplevelHandle*>& handles() const { return handles_; }

    // 在连接信号后调用：等待 registry 通告与初始 toplevel 事件。
    // （构造内 roundtrip 会早于调用方的 connect，丢失初始窗口事件。）
    void start();

signals:
    void toplevelAdded(ForeignToplevelHandle* handle);
    void toplevelRemoved(ForeignToplevelHandle* handle);

public:
    // 协议事件回调必须 public（类外 listener 结构体取地址）。
    // ---- wl_registry / manager 事件回调 ----
    static void registryGlobal(void* data, wl_registry* registry, uint32_t name,
                               const char* interface, uint32_t version);
    static void registryGlobalRemove(void* data, wl_registry* registry, uint32_t name);
    static void managerToplevel(void* data, zwlr_foreign_toplevel_manager_v1* manager,
                                zwlr_foreign_toplevel_handle_v1* handle);
    static void managerFinished(void* data, zwlr_foreign_toplevel_manager_v1* manager);

private:
    zwlr_foreign_toplevel_manager_v1* manager_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_seat* seat_ = nullptr;  // activate 请求用（合成器按 seat 名匹配）
    std::vector<ForeignToplevelHandle*> handles_;
};

}  // namespace w10de

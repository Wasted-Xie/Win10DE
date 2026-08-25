// w10lock —— 锁屏客户端（ext-session-lock-v1，M6）
//
// 绑定 session-lock 协议：创建全屏锁屏 surface，用 Qt 离屏渲染时钟画面
// 并提交（wl_shm buffer）。键盘事件经 xkbcommon 解析 keysym（KDE-GAP #4：
// 密码输入需要区分字符/退格/回车）。
#pragma once

extern "C" {
#include <wayland-client.h>
#include "ext-session-lock-client-protocol.h"
#include <xkbcommon/xkbcommon.h>
}

#include <functional>
#include <cstdint>

namespace w10de {

class LockClient {
public:
    explicit LockClient(wl_display* display);
    ~LockClient();

    bool isValid() const;

    // 请求锁定；onLocked 在合成器 locked 事件后调用（锁定已生效）。
    void lock(std::function<void()> onLocked);
    // 解锁（销毁锁对象，合成器恢复会话）。
    void unlock();

    // configure 事件后回调（ack 已完成）：用于提交首帧（锁屏渲染起点）。
    void setConfiguredCallback(std::function<void()> cb) { onConfigured_ = std::move(cb); }
    // finished 事件后回调（合成器结束锁定/锁对象失效）：用于退出进程。
    void setFinishedCallback(std::function<void()> cb) { onFinished_ = std::move(cb); }

    // 把 ARGB32 帧提交到锁屏 surface（宽高须与 configure 一致）。
    void present(const void* argb, int width, int height);

    // 键盘事件回调（KDE-GAP #4：keysym + 按下/释放；替代任意键回调）。
    void setKeySymCallback(std::function<void(xkb_keysym_t sym, bool pressed)> cb);

    // 当前锁屏 surface 尺寸（configure 事件设置）。
    int width() const { return width_; }
    int height() const { return height_; }

    // ---- 协议事件回调（listener 结构体在类外匿名命名空间初始化，须 public）----
    static void registryGlobal(void* data, wl_registry* registry, uint32_t name,
                               const char* interface, uint32_t version);
    static void registryGlobalRemove(void* data, wl_registry* registry, uint32_t name);
    static void lockLocked(void* data, ext_session_lock_v1* lock);
    static void lockFinished(void* data, ext_session_lock_v1* lock);
    static void lockSurfaceConfigure(void* data, ext_session_lock_surface_v1* surface,
                                     uint32_t serial, uint32_t width, uint32_t height);
    static void keyboardKey(void* data, wl_keyboard* keyboard, uint32_t serial,
                            uint32_t time, uint32_t key, uint32_t state);
    static void keyboardKeymap(void* data, wl_keyboard* keyboard, uint32_t format,
                               int32_t fd, uint32_t size);
    static void keyboardEnter(void* data, wl_keyboard* keyboard, uint32_t serial,
                              wl_surface* surface, wl_array* keys);
    static void keyboardLeave(void* data, wl_keyboard* keyboard, uint32_t serial,
                              wl_surface* surface);
    static void keyboardModifiers(void* data, wl_keyboard* keyboard, uint32_t serial,
                                  uint32_t modsDepressed, uint32_t modsLatched,
                                  uint32_t modsLocked, uint32_t group);
    static void keyboardRepeatInfo(void* data, wl_keyboard* keyboard,
                                   int32_t rate, int32_t delay);
    static void seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities);
    static void seatName(void* data, wl_seat* seat, const char* name);

private:
    // 创建/销毁 shm buffer（双 buffer 交替提交）。
    bool createBuffer(int width, int height);
    void destroyBuffer();

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    wl_output* output_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    ext_session_lock_manager_v1* manager_ = nullptr;
    ext_session_lock_v1* lock_ = nullptr;
    ext_session_lock_surface_v1* lockSurface_ = nullptr;
    wl_surface* wlSurface_ = nullptr;

    wl_shm_pool* pool_ = nullptr;
    wl_buffer* buffer_[2] = {};    // 双 buffer 交替（避免覆写正在渲染的帧）
    int bufferIndex_ = 0;
    uint8_t* mappedData_ = nullptr;   // shm buffer 内存映射基址（含两个 buffer）
    size_t mappedSize_ = 0;
    size_t frameSize_ = 0;
    int bufferW_ = 0, bufferH_ = 0, stride_ = 0;

    // xkbcommon（KDE-GAP #4：keysym 解析）。
    xkb_context* xkbCtx_ = nullptr;
    xkb_keymap* xkbKeymap_ = nullptr;
    xkb_state* xkbState_ = nullptr;

    std::function<void()> onLocked_;
    std::function<void()> onConfigured_;
    std::function<void()> onFinished_;
    std::function<void(xkb_keysym_t, bool)> onKeySym_;
    bool locked_ = false;
    int width_ = 0, height_ = 0;
};

}  // namespace w10de

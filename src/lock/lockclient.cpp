#include "lock/lockclient.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <QDebug>

namespace w10de {

namespace {

const wl_registry_listener kRegistryListener = {
    .global = LockClient::registryGlobal,
    .global_remove = LockClient::registryGlobalRemove,
};

const ext_session_lock_v1_listener kLockListener = {
    .locked = LockClient::lockLocked,
    .finished = LockClient::lockFinished,
};

const ext_session_lock_surface_v1_listener kLockSurfaceListener = {
    .configure = LockClient::lockSurfaceConfigure,
};

const wl_keyboard_listener kKeyboardListener = {
    .keymap = LockClient::keyboardKeymap,
    .enter = LockClient::keyboardEnter,
    .leave = LockClient::keyboardLeave,
    .key = LockClient::keyboardKey,
    .modifiers = LockClient::keyboardModifiers,
    .repeat_info = LockClient::keyboardRepeatInfo,
};

const wl_seat_listener kSeatListener = {
    .capabilities = LockClient::seatCapabilities,
    .name = LockClient::seatName,
};

}  // namespace

LockClient::LockClient(wl_display* display) : display_(display) {
    xkbCtx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);
}

LockClient::~LockClient() {
    if (xkbState_ != nullptr) {
        xkb_state_unref(xkbState_);
    }
    if (xkbKeymap_ != nullptr) {
        xkb_keymap_unref(xkbKeymap_);
    }
    if (xkbCtx_ != nullptr) {
        xkb_context_unref(xkbCtx_);
    }
    destroyBuffer();
    if (lockSurface_ != nullptr) {
        ext_session_lock_surface_v1_destroy(lockSurface_);
    }
    if (lock_ != nullptr) {
        // 锁定中销毁必须用 unlock_and_destroy（destroy 触发 INVALID_DESTROY
        // 协议错误）；未锁定用普通 destroy。
        if (locked_) {
            ext_session_lock_v1_unlock_and_destroy(lock_);
        } else {
            ext_session_lock_v1_destroy(lock_);
        }
    }
    if (wlSurface_ != nullptr) {
        wl_surface_destroy(wlSurface_);
    }
    if (keyboard_ != nullptr) {
        wl_keyboard_release(keyboard_);
    }
    if (seat_ != nullptr) {
        wl_seat_release(seat_);
    }
    if (registry_ != nullptr) {
        wl_registry_destroy(registry_);
    }
}

bool LockClient::isValid() const {
    return manager_ != nullptr && shm_ != nullptr && seat_ != nullptr &&
           compositor_ != nullptr && output_ != nullptr;
}

void LockClient::lock(std::function<void()> onLocked) {
    onLocked_ = std::move(onLocked);
    lock_ = ext_session_lock_manager_v1_lock(manager_);
    ext_session_lock_v1_add_listener(lock_, &kLockListener, this);

    // 为锁创建 surface（M6：首个输出一个锁屏 surface）。
    wlSurface_ = wl_compositor_create_surface(compositor_);
    lockSurface_ = ext_session_lock_v1_get_lock_surface(lock_, wlSurface_, output_);
    ext_session_lock_surface_v1_add_listener(lockSurface_, &kLockSurfaceListener, this);
    // 注意：此处不能 commit（未 attach buffer 且未 ack configure 是协议错误）；
    // 首帧由 configure 回调 ack 后提交。
    wl_display_roundtrip(display_);  // 等 configure（回调中 ack + 渲染首帧）
    wl_display_roundtrip(display_);  // 等 locked 事件
}

void LockClient::unlock() {
    if (lock_ != nullptr) {
        // 仅在 locked 事件已发送后才可用 unlock_and_destroy；否则协议错误
        //（invalid_unlock）。与析构分支一致。
        if (locked_) {
            ext_session_lock_v1_unlock_and_destroy(lock_);
        } else {
            ext_session_lock_v1_destroy(lock_);
        }
        lock_ = nullptr;
    }
    locked_ = false;
    // 锁 surface 随锁对象销毁失效；客户端侧对象显式销毁。
    if (lockSurface_ != nullptr) {
        ext_session_lock_surface_v1_destroy(lockSurface_);
        lockSurface_ = nullptr;
    }
}

void LockClient::present(const void* argb, int width, int height) {
    if (lockSurface_ == nullptr) {
        return;
    }
    if (buffer_[0] == nullptr || bufferW_ != width || bufferH_ != height) {
        destroyBuffer();
        if (!createBuffer(width, height)) {
            return;
        }
    }
    // 双 buffer 交替：写当前帧，attach 上一帧提交的 buffer（合成器可能
    // 仍在读），避免撕裂/花屏。
    const int index = bufferIndex_;
    bufferIndex_ = 1 - bufferIndex_;
    const auto* src = static_cast<const uint8_t*>(argb);
    uint8_t* dst = mappedData_ + static_cast<size_t>(index) * frameSize_;
    for (int y = 0; y < height; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * stride_,
                    src + static_cast<size_t>(y) * width * 4,
                    static_cast<size_t>(width) * 4);
    }

    wl_surface_attach(wlSurface_, buffer_[index], 0, 0);
    wl_surface_damage_buffer(wlSurface_, 0, 0, width, height);
    wl_surface_commit(wlSurface_);
}

void LockClient::setKeySymCallback(std::function<void(xkb_keysym_t, bool)> cb) {
    onKeySym_ = std::move(cb);
}

// ---- registry 绑定 ----

void LockClient::registryGlobal(void* data, wl_registry* registry, uint32_t name,
                                const char* interface, uint32_t version) {
    auto* self = static_cast<LockClient*>(data);
    if (std::strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        self->manager_ = static_cast<ext_session_lock_manager_v1*>(
            wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface,
                             std::min<uint32_t>(version, 1)));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        self->shm_ = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        self->seat_ = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface,
                             std::min<uint32_t>(version, 7)));
        wl_seat_add_listener(self->seat_, &kSeatListener, self);
    } else if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        self->compositor_ = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface,
                             std::min<uint32_t>(version, 6)));
    } else if (std::strcmp(interface, wl_output_interface.name) == 0 &&
               self->output_ == nullptr) {
        // 首个输出（锁屏目标）。
        self->output_ = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, 1));
    }
}

void LockClient::registryGlobalRemove(void* /*data*/, wl_registry* /*registry*/,
                                      uint32_t /*name*/) {
    // M6 不处理热移除。
}

// ---- session-lock 事件 ----

void LockClient::lockLocked(void* data, ext_session_lock_v1* /*lock*/) {
    auto* self = static_cast<LockClient*>(data);
    self->locked_ = true;
    if (self->onLocked_) {
        self->onLocked_();
    }
}

void LockClient::lockFinished(void* data, ext_session_lock_v1* /*lock*/) {
    auto* self = static_cast<LockClient*>(data);
    self->locked_ = false;
    // 合成器结束锁定（锁对象已失效）：通知调用方退出锁屏进程。
    if (self->onFinished_) {
        self->onFinished_();
    }
}

void LockClient::lockSurfaceConfigure(void* data, ext_session_lock_surface_v1* surface,
                                      uint32_t serial, uint32_t width, uint32_t height) {
    auto* self = static_cast<LockClient*>(data);
    self->width_ = static_cast<int>(width);
    self->height_ = static_cast<int>(height);
    ext_session_lock_surface_v1_ack_configure(surface, serial);
    // ack 后立即渲染并提交首帧（合成器可能等首帧才发 locked）。
    if (self->onConfigured_) {
        self->onConfigured_();
    }
}

// ---- 键盘 ----

void LockClient::seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities) {
    auto* self = static_cast<LockClient*>(data);
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && self->keyboard_ == nullptr) {
        self->keyboard_ = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(self->keyboard_, &kKeyboardListener, self);
    }
}

void LockClient::seatName(void* /*data*/, wl_seat* /*seat*/, const char* /*name*/) {}

void LockClient::keyboardKey(void* data, wl_keyboard* /*keyboard*/, uint32_t /*serial*/,
                             uint32_t /*time*/, uint32_t key, uint32_t state) {
    auto* self = static_cast<LockClient*>(data);
    // KDE-GAP #4：xkbcommon 解析 keysym（密码输入需要字符/退格/回车区分）。
    if (self->onKeySym_ && self->xkbState_ != nullptr) {
        const xkb_keycode_t code = key + 8;  // wlroots keycode 比 xkb 小 8
        const xkb_keysym_t sym =
            xkb_state_key_get_one_sym(self->xkbState_, code);
        self->onKeySym_(sym, state == WL_KEYBOARD_KEY_STATE_PRESSED);
    }
}

void LockClient::keyboardKeymap(void* data, wl_keyboard* /*keyboard*/,
                                uint32_t format, int32_t fd, uint32_t size) {
    // KDE-GAP #4：解析 keymap 供 keysym 查询（原实现直接关闭 fd）。
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
        close(fd);
        return;
    }
    char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
    close(fd);
    if (map == MAP_FAILED) {
        return;
    }
    auto* self = static_cast<LockClient*>(data);
    if (self->xkbKeymap_ != nullptr) {
        xkb_keymap_unref(self->xkbKeymap_);
    }
    if (self->xkbState_ != nullptr) {
        xkb_state_unref(self->xkbState_);
    }
    self->xkbKeymap_ = xkb_keymap_new_from_string(
        self->xkbCtx_, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (self->xkbKeymap_ != nullptr) {
        self->xkbState_ = xkb_state_new(self->xkbKeymap_);
    }
}

void LockClient::keyboardEnter(void* /*data*/, wl_keyboard* /*keyboard*/, uint32_t /*serial*/,
                               wl_surface* /*surface*/, wl_array* /*keys*/) {}
void LockClient::keyboardLeave(void* /*data*/, wl_keyboard* /*keyboard*/,
                               uint32_t /*serial*/, wl_surface* /*surface*/) {}
void LockClient::keyboardModifiers(void* data, wl_keyboard* /*keyboard*/,
                                   uint32_t /*serial*/, uint32_t depressed,
                                   uint32_t latched, uint32_t locked,
                                   uint32_t group) {
    // KDE-GAP #4：更新 xkb 状态（Shift 等修饰影响 keysym 解析）。
    auto* self = static_cast<LockClient*>(data);
    if (self->xkbState_ != nullptr) {
        xkb_state_update_mask(self->xkbState_, depressed, latched, locked, 0, 0, group);
    }
}
void LockClient::keyboardRepeatInfo(void* /*data*/, wl_keyboard* /*keyboard*/,
                                    int32_t /*rate*/, int32_t /*delay*/) {}

// ---- shm buffer ----

bool LockClient::createBuffer(int width, int height) {
    stride_ = width * 4;
    // 双 buffer：pool 大小为两帧（先以 size_t 计算防有符号溢出）。
    const size_t frameSize = static_cast<size_t>(stride_) * height;
    if (frameSize > static_cast<size_t>(INT32_MAX) / 2) {
        qWarning("LockClient: buffer too large (%zu bytes/frame)", frameSize);
        return false;
    }
    frameSize_ = frameSize;
    mappedSize_ = frameSize * 2;
    char name[64];
    std::snprintf(name, sizeof(name), "/w10lock-%d", getpid());
    // 先清理可能残留的同名 shm 对象（崩溃残留 + pid 重用场景）。
    shm_unlink(name);
    const int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        qWarning("LockClient: shm_open failed");
        return false;
    }
    shm_unlink(name);
    if (ftruncate(fd, static_cast<off_t>(mappedSize_)) != 0) {
        close(fd);
        return false;
    }
    mappedData_ = static_cast<uint8_t*>(
        mmap(nullptr, mappedSize_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (mappedData_ == MAP_FAILED) {
        mappedData_ = nullptr;
        close(fd);
        return false;
    }
    pool_ = wl_shm_create_pool(shm_, fd, static_cast<int32_t>(mappedSize_));
    // 两个 buffer 各占 pool 的一半，交替提交。
    buffer_[0] = wl_shm_pool_create_buffer(pool_, 0, width, height, stride_,
                                           WL_SHM_FORMAT_ARGB8888);
    buffer_[1] = wl_shm_pool_create_buffer(
        pool_, static_cast<int32_t>(frameSize), width, height, stride_,
        WL_SHM_FORMAT_ARGB8888);
    bufferIndex_ = 0;
    bufferW_ = width;
    bufferH_ = height;
    close(fd);
    return buffer_[0] != nullptr && buffer_[1] != nullptr;
}

void LockClient::destroyBuffer() {
    for (int i = 0; i < 2; ++i) {
        if (buffer_[i] != nullptr) {
            wl_buffer_destroy(buffer_[i]);
            buffer_[i] = nullptr;
        }
    }
    if (pool_ != nullptr) {
        wl_shm_pool_destroy(pool_);
        pool_ = nullptr;
    }
    if (mappedData_ != nullptr) {
        munmap(mappedData_, mappedSize_);
        mappedData_ = nullptr;
    }
    mappedSize_ = 0;
    frameSize_ = 0;
    bufferW_ = bufferH_ = 0;
}

}  // namespace w10de

// Win10DE 开发验证工具：最小 xdg-toplevel 键盘客户端（E8 屏幕键盘注入验证）。
//
// 用途：在 headless compositor 上开窗口并监听 wl_keyboard（keymap/key 事件），
// 验证 D-Bus InputKey 注入的按键到达焦点窗口。
// 构建（WSL）：
//   cc e8-keyclient.c xdg-shell-protocol.c -o e8-keyclient \
//      $(pkg-config --cflags --libs wayland-client)
// 运行：WAYLAND_DISPLAY=<socket> ./e8-keyclient
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

static struct wl_display* display;
static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct xdg_wm_base* xdg_wm;
static struct wl_surface* surface;
static struct xdg_surface* xdg_surface;
static struct xdg_toplevel* xdg_toplevel;
static bool configured = false;
static bool keymap_received = false;

static void xdg_wm_base_ping(void* data, struct xdg_wm_base* wm, uint32_t serial) {
    xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void* data, struct xdg_surface* s, uint32_t serial) {
    xdg_surface_ack_configure(s, serial);
    configured = true;
}
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void* data, struct xdg_toplevel* t,
                                   int32_t w, int32_t h, struct wl_array* states) {}
static void xdg_toplevel_close(void* data, struct xdg_toplevel* t) {
    printf("keyclient: close requested\n");
    exit(0);
}
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

// ---- wl_keyboard 监听 ----
static void keyboard_keymap(void* data, struct wl_keyboard* kb, uint32_t format,
                            int32_t fd, uint32_t size) {
    keymap_received = true;
    printf("KEYMAP format=%u size=%u\n", format, size);
    close(fd);
}
static void keyboard_enter(void* data, struct wl_keyboard* kb, uint32_t serial,
                           struct wl_surface* s, struct wl_array* keys) {
    printf("FOCUS_IN\n");
}
static void keyboard_leave(void* data, struct wl_keyboard* kb, uint32_t serial,
                           struct wl_surface* s) {
    printf("FOCUS_OUT\n");
}
static void keyboard_key(void* data, struct wl_keyboard* kb, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state) {
    printf("KEY keycode=%u state=%u\n", key, state);
    fflush(stdout);
}
static void keyboard_modifiers(void* data, struct wl_keyboard* kb, uint32_t serial,
                               uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group) {
    printf("MODS depressed=%u\n", mods_depressed);
}
static void keyboard_repeat_info(void* data, struct wl_keyboard* kb,
                                 int32_t rate, int32_t delay) {}
static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void* data, struct wl_seat* seat, uint32_t caps) {
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        struct wl_keyboard* kb = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(kb, &keyboard_listener, NULL);
        printf("SEAT keyboard available\n");
    }
}
static void seat_name(void* data, struct wl_seat* seat, const char* name) {}
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void registry_global(void* data, struct wl_registry* registry, uint32_t name,
                            const char* interface, uint32_t version) {
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        xdg_wm = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(xdg_wm, &xdg_wm_base_listener, NULL);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        struct wl_seat* seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
        wl_seat_add_listener(seat, &seat_listener, NULL);
    }
}
static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int create_shm_file(size_t size) {
    char name[] = "/wl_shm-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static struct wl_buffer* create_buffer(int width, int height, int stride,
                                       void** pixels_out) {
    const int size = stride * height;
    int fd = create_shm_file(size);
    if (fd < 0) return NULL;
    void* data = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer* buffer =
        wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                  WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    *pixels_out = data;
    return buffer;
}

int main(int argc, char* argv[]) {
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "keyclient: cannot connect\n");
        return 1;
    }
    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !xdg_wm) {
        fprintf(stderr, "keyclient: missing globals\n");
        return 1;
    }
    surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(xdg_toplevel, "KeyClient");
    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    // attach 白色 buffer（无内容 surface 不 map，无法获得焦点）。
    void* pixels = NULL;
    struct wl_buffer* buffer =
        create_buffer(320, 240, 320 * 4, &pixels);
    if (buffer == NULL) {
        fprintf(stderr, "keyclient: buffer create failed\n");
        return 1;
    }
    memset(pixels, 0xEE, (size_t)(320 * 240 * 4));
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, 320, 240);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    // 等键盘焦点（compositor 自动聚焦窗口）。
    int rounds = 0;
    while (rounds++ < 200 && !keymap_received) {
        wl_display_roundtrip(display);
        usleep(20000);
    }
    printf("READY keymap=%d\n", keymap_received ? 1 : 0);
    fflush(stdout);
    // 事件循环（Ctrl-C 或 close 退出）。
    while (wl_display_dispatch(display) != -1) {}
    return 0;
}

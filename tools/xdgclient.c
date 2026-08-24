// Win10DE 开发验证工具：最小 xdg-toplevel 客户端。
//
// 用途：在 headless compositor 上开一个带标题的窗口，
// 验证标题栏文字渲染（M2b）与窗口管理基础路径。
// 构建（WSL）：
//   wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h
//   wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c
//   cc xdgclient.c xdg-shell-protocol.c -o xdgclient $(pkg-config --cflags --libs wayland-client)
// 运行：WAYLAND_DISPLAY=<compositor socket> ./xdgclient [title]
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
static const char* win_title = "Win10DE Test Window";
static int win_w = 640, win_h = 480;

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
                                   int32_t w, int32_t h, struct wl_array* states) {
    // 尺寸由命令行指定；收到首个 configure 后即可 map。
}
static void xdg_toplevel_close(void* data, struct xdg_toplevel* t) {
    printf("xdgclient: toplevel close requested\n");
    exit(0);
}
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
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
    if (fd < 0) {
        return -1;
    }
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    unlink(name);
    return fd;
}

static struct wl_buffer* make_buffer(int width, int height) {
    const int stride = width * 4;
    const size_t size = (size_t)stride * height;
    int fd = create_shm_file(size);
    if (fd < 0) {
        return NULL;
    }
    void* data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    // 蓝绿色窗口内容（便于截图识别）。
    uint8_t* px = data;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            px[y * stride + x * 4 + 0] = 0xB0;  // B
            px[y * stride + x * 4 + 1] = 0x30;  // G
            px[y * stride + x * 4 + 2] = 0x20;  // R
            px[y * stride + x * 4 + 3] = 0xFF;  // A
        }
    }
    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(
        pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

int main(int argc, char** argv) {
    if (argc > 1) {
        win_title = argv[1];
    }
    // 可选参数：<W>x<H> 指定窗口尺寸（窄窗口测试用），如 "100x100"。
    if (argc > 2) {
        if (sscanf(argv[2], "%dx%d", &win_w, &win_h) != 2 ||
                win_w <= 0 || win_h <= 0) {
            fprintf(stderr, "xdgclient: invalid size '%s' (expect WxH)\n", argv[2]);
            return 1;
        }
    }
    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "xdgclient: no wayland display\n");
        return 1;
    }
    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (compositor == NULL || shm == NULL || xdg_wm == NULL) {
        fprintf(stderr, "xdgclient: missing globals\n");
        return 1;
    }

    surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(xdg_toplevel, win_title);
    xdg_toplevel_set_app_id(xdg_toplevel, "w10de.xdgclient");
    wl_surface_commit(surface);

    // 等首个 configure（ack 后 attach buffer 提交）。
    while (!configured) {
        wl_display_dispatch(display);
    }
    struct wl_buffer* buffer = make_buffer(win_w, win_h);
    if (buffer == NULL) {
        fprintf(stderr, "xdgclient: failed to create buffer\n");
        return 1;
    }
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, win_w, win_h);
    wl_surface_commit(surface);

    printf("xdgclient: window '%s' up (%dx%d), running\n", win_title, win_w, win_h);
    while (wl_display_dispatch(display) != -1) {
        // 保持运行
    }
    return 0;
}

# Win10DE —— 外部依赖编排（系统优先，缺失/版本不符时从源码编译兜底）
#
# 覆盖 wlroots 0.19 生态全部库依赖（固定版本 URL，见 DepSource.cmake 机制）：
#   wayland / wayland-protocols / libdrm / pixman / libxkbcommon /
#   libdisplay-info / libliftoff / libseat / libevdev / libinput / hwdata /
#   egl+glesv2+gbm（mesa，GL 渲染/DRM 分配器用）/ cairo / pango（标题文字）。
#
# 语义（W10DE_DEP_SOURCE=auto 默认）：
#   - 系统 pkg-config 满足版本 → 直接用系统包；
#   - 否则从固定 URL 下载源码编译到 ${W10DE_DEPS_PREFIX} 并注入
#     PKG_CONFIG_PATH（后续依赖与项目 find 均可见）。
# 可选构建工具：meson + ninja（源码编译时必需，发行版包名：meson/ninja）。
#
# 说明：
#   - libudev（libinput/mesa 的 udev 依赖）假定系统提供（主流发行版必有）；
#   - wlroots 的 renderers/backends/allocators 默认 auto：GL（egl/glesv2/gbm）
#     或 libseat 缺失时自动降级（pixman 渲染 + shm 分配器），headless 可用；
#     DRM 真机需要 GL 时系统装 mesa 或 -DW10DE_DEP_SOURCE=always 编译 mesa。
#
# 下载源（无代理/受限网络环境）：
#   - 依赖统一走 gitlab.freedesktop.org（release 或 archive），单一域名便于
#     配置代理/加速；原独立域名（dri.freedesktop.org / cairographics.org /
#     xkbcommon.org / mesa.freedesktop.org / download.gnome.org）在部分网络
#     不可达（实测），故全部改为 gitlab archive（tag 名带项目前缀如
#     pixman-0.43.4 / libevdev-1.13.3 / mesa-24.2.8，已逐个验证 200）；
#   - gitlab.freedesktop.org **无国内镜像站**（TUNA/USTC/阿里实测无）；
#     GitHub 有官方 mirror 仓库（wayland/wayland-protocols/mesa/pango 等，
#     可经 gh-proxy.com 前缀国内直连），DepSource 多源机制（URL 分号列表）
#     支持为个别依赖追加；hwdata 已配 gh-proxy 镜像；
#   - **离线构建**：把 tarball 预置到 ${W10DE_DEPS_DL}（build/_deps/downloads/）
#     即跳过下载；可在有网环境构建一次后复用该缓存目录。

# 依赖项定义：NAME 用于开关与日志；PC 支持分号分隔多候选（如 libdrm;drm）。
# 版本为最低要求前缀（空 = 不校验）；URL 为固定版本 tarball。
# SHA256 暂留空（下载后可用 file(SHA256) 计算补充，增强可复现性）。

# ---- wayland-protocols（wlroots >= 1.41；纯 XML，无编译）----
w10de_dep_system_or_source(
    NAME wayland-protocols
    PC wayland-protocols
    VERSION 1.41
    URL https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases/1.41/downloads/wayland-protocols-1.41.tar.xz;https://github.com/gitlab-freedesktop-mirrors/wayland-protocols/archive/refs/tags/1.41.tar.gz;https://gh-proxy.com/https://github.com/gitlab-freedesktop-mirrors/wayland-protocols/archive/refs/tags/1.41.tar.gz
    BUILD meson
)

# ---- wayland（wlroots >= 1.23.1；libwayland + wayland-scanner）----
# PC 候选：wayland-client / wayland-server（无 wayland-1.0.pc 的发行版）。
# 源：gitlab 官方 + GitHub mirror（gitlab-freedesktop-mirrors 组织）+ gh-proxy 加速。
w10de_dep_system_or_source(
    NAME wayland
    PC wayland-client;wayland-server
    VERSION 1.23
    URL https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.23.1/downloads/wayland-1.23.1.tar.xz;https://github.com/gitlab-freedesktop-mirrors/wayland/archive/refs/tags/1.23.1.tar.gz;https://gh-proxy.com/https://github.com/gitlab-freedesktop-mirrors/wayland/archive/refs/tags/1.23.1.tar.gz
    BUILD meson
    MESON_ARGS -Ddocumentation=false -Dtests=false
)

# ---- libdrm（wlroots >= 2.4.122；Arch 包 pkg 名为 libdrm，Debian 系为 drm）----
# tag 名带 libdrm- 前缀（mesa/libdrm 仓库）。
w10de_dep_system_or_source(
    NAME libdrm
    PC libdrm;drm
    VERSION 2.4.122
    URL https://gitlab.freedesktop.org/mesa/libdrm/-/archive/libdrm-2.4.123/libdrm-2.4.123.tar.gz
    BUILD meson
    MESON_ARGS -Dtests=false
)

# ---- pixman（wlroots >= 0.43.0；软件渲染）----
w10de_dep_system_or_source(
    NAME pixman
    PC pixman-1
    VERSION 0.43
    URL https://gitlab.freedesktop.org/pixman/pixman/-/archive/pixman-0.43.4/pixman-pixman-0.43.4.tar.gz
    BUILD meson
    MESON_ARGS -Dtests=disabled -Dgtk=disabled
)

# ---- libxkbcommon（键盘 xkb）----
# 官方源 xkbcommon.org（部分网络不可达）；gitlab 仓库（xorg/lib/libxkbcommon）
# 无新版 release tag（实测 1.7.0 各 tag 均 404）→ 保持官方单源，失败时走
# 离线缓存（预置 tarball 到 ${W10DE_DEPS_DL}）。
w10de_dep_system_or_source(
    NAME libxkbcommon
    PC xkbcommon
    URL https://xkbcommon.org/download/libxkbcommon-1.7.0.tar.xz
    BUILD meson
    MESON_ARGS -Denable-docs=false -Denable-tools=false -Denable-x11=false -Denable-wayland=false -Denable-xkbregistry=false
)

# ---- libdisplay-info（DRM 显示器 EDID 解析）----
w10de_dep_system_or_source(
    NAME libdisplay-info
    PC libdisplay-info
    URL https://gitlab.freedesktop.org/emersion/libdisplay-info/-/releases/0.2.0/downloads/libdisplay-info-0.2.0.tar.xz
    BUILD meson
    MESON_ARGS -Dtests=false
)

# ---- libliftoff（DRM 合成层；wlroots >= 0.5.0）----
w10de_dep_system_or_source(
    NAME libliftoff
    PC libliftoff
    VERSION 0.5
    URL https://gitlab.freedesktop.org/emersion/libliftoff/-/releases/0.5.0/downloads/libliftoff-0.5.0.tar.xz
    BUILD meson
    MESON_ARGS -Dtests=false
)

# ---- libseat（会话/seat 管理；DRM 后端用，缺失时 wlroots 降级）----
w10de_dep_system_or_source(
    NAME libseat
    PC libseat
    URL https://gitlab.freedesktop.org/seatd/libseat/-/releases/0.8.0/downloads/libseat-0.8.0.tar.xz
    BUILD meson
    MESON_ARGS -Dtests=false -Dexamples=false
)

# ---- libevdev（libinput 的输入设备解析依赖）----
w10de_dep_system_or_source(
    NAME libevdev
    PC libevdev
    URL https://gitlab.freedesktop.org/libevdev/libevdev/-/archive/libevdev-1.13.3/libevdev-libevdev-1.13.3.tar.gz
    BUILD meson
    MESON_ARGS -Ddocumentation=disabled
)

# ---- libinput（输入后端；依赖 libevdev + 系统 libudev）----
w10de_dep_system_or_source(
    NAME libinput
    PC libinput
    VERSION 1.26
    URL https://gitlab.freedesktop.org/libinput/libinput/-/releases/1.26.2/downloads/libinput-1.26.2.tar.xz
    BUILD meson
    MESON_ARGS -Dtests=false -Ddebug-gui=false -Ddocumentation=false
)

# ---- hwdata（pnp.ids 等设备数据；DRM 后端用，缺失时 wlroots 降级）----
# GitHub 源：官方 + gh-proxy.com 加速（国内可直连；两源逐个尝试）。
w10de_dep_system_or_source(
    NAME hwdata
    PC hwdata
    URL https://github.com/vcrhonek/hwdata/archive/refs/tags/v0.385.tar.gz;https://gh-proxy.com/https://github.com/vcrhonek/hwdata/archive/refs/tags/v0.385.tar.gz
    BUILD meson
    MESON_ARGS -Ddefs=disabled
)

# ---- egl / glesv2 / gbm（mesa：GL 渲染 + GBM 分配器）----
# 系统缺失且需要 GL（DRM 真机）时从源码编译 mesa 最小集（swrast，无 LLVM）。
# 仅 headless/pixman 场景无需本项（wlroots renderers=auto 自动降级）。
if(NOT DEFINED W10DE_MESA_DONE)
    pkg_check_modules(EGL_SYS IMPORTED_TARGET egl)
    pkg_check_modules(GLESV2_SYS IMPORTED_TARGET glesv2)
    pkg_check_modules(GBM_SYS IMPORTED_TARGET gbm)
    set(_gl_ok FALSE)
    if(EGL_SYS_FOUND AND GLESV2_SYS_FOUND AND GBM_SYS_FOUND)
        set(_gl_ok TRUE)
    endif()
    if(W10DE_DEP_SOURCE STREQUAL "always" OR NOT _gl_ok)
        if(W10DE_DEP_SOURCE STREQUAL "never")
            message(FATAL_ERROR
                "GL stack (egl/glesv2/gbm) missing and W10DE_DEP_SOURCE=never")
        endif()
        message(STATUS "mesa: GL stack missing, building from source"
                       " (swrast, llvm disabled; DRM 真机需要)")
        w10de_dep_system_or_source(
            NAME mesa
            PC egl
            URL https://gitlab.freedesktop.org/mesa/mesa/-/archive/mesa-24.2.8/mesa-mesa-24.2.8.tar.gz;https://github.com/mirror/mesa/archive/refs/tags/mesa-24.2.8.tar.gz;https://gh-proxy.com/https://github.com/mirror/mesa/archive/refs/tags/mesa-24.2.8.tar.gz
            BUILD meson
            MESON_ARGS
                -Dgallium-drivers=swrast
                -Dvulkan-drivers=
                -Ddri-drivers=
                -Dglx=disabled
                -Dgbm=enabled
                -Degl=enabled
                -Dopengl=true
                -Dllvm=disabled
                -Dplatforms=
                -Dtools=
        )
    else()
        message(STATUS "GL stack: using system (egl/glesv2/gbm)")
    endif()
    set(W10DE_MESA_DONE TRUE)
endif()

# ---- cairo / pango（标题栏文字渲染，compositor 侧）----
# 依赖 freetype/fontconfig/glib/harfbuzz/zlib/libpng 假定系统提供。
w10de_dep_system_or_source(
    NAME cairo
    PC cairo
    URL https://gitlab.freedesktop.org/cairo/cairo/-/archive/1.18.2/cairo-1.18.2.tar.gz
    BUILD meson
    MESON_ARGS -Dtests=disabled -Dspectre=disabled -Dsymbol-lookup=disabled
)

w10de_dep_system_or_source(
    NAME pango
    PC pangocairo
    URL https://gitlab.freedesktop.org/pango/pango/-/archive/1.54.0/pango-1.54.0.tar.gz;https://github.com/GNOME/pango/archive/refs/tags/1.54.0.tar.gz;https://gh-proxy.com/https://github.com/GNOME/pango/archive/refs/tags/1.54.0.tar.gz
    BUILD meson
    MESON_ARGS -Dtests=false -Dgtk_doc=false -Dintrospection=disabled
)

# ---- Qt6 / layer-shell-qt（shell 客户端；留源码编译接口）----
# 本次不实现 Qt6 从源码编译（工程量极大）。缺失时提示：
# 装系统包，或 -DW10DE_BUILD_SHELL=OFF 仅构建 compositor。
if(W10DE_BUILD_SHELL AND NOT Qt6_FOUND AND NOT LayerShellQt_FOUND)
    message(STATUS
        "Qt6/layer-shell-qt: 系统缺失。可 -DW10DE_BUILD_SHELL=OFF 跳过 shell，"
        "或安装系统包（qt6-base qt6-wayland layer-shell-qt）。"
        "Qt6 从源码编译（superbuild 扩展）为后续工作项。")
endif()

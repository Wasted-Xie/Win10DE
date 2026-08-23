cmake_minimum_required(VERSION 3.20)
project(Win10DE
    VERSION 0.1.0
    DESCRIPTION "A Windows 10 style desktop environment for Linux (wlroots + Qt6)"
    LANGUAGES C CXX)   # C：wayland-scanner 生成的协议 .c

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------------------
# 依赖（参考 docs/ARCHITECTURE.md 第 10 节）
# wlroots 0.19 获取策略（跨发行版）：系统包优先，头需 C++ 补丁时注入补丁
# 副本，缺失/版本不符时自动编译 vendored 源码（见 cmake/WlrootsCompat.cmake）。
# ---------------------------------------------------------------------------
include(cmake/WlrootsCompat.cmake)
# libdrm 的 pkg-config 名在发行版间不同：Debian 系为 drm，Arch 为 libdrm。
pkg_check_modules(DRM IMPORTED_TARGET drm)
if(NOT DRM_FOUND)
    pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
endif()
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

# ---- M3 shell 客户端（Qt）----
# 默认启用；无 Qt 环境时可 -DW10DE_BUILD_SHELL=OFF 只构建 compositor。
option(W10DE_BUILD_SHELL "Build the Qt shell client (taskbar/start menu)" ON)
if(W10DE_BUILD_SHELL)
    # layer-shell 绑定：KDE layer-shell-qt（Plasma 生产使用，API 稳定）。
    # 备选：Qt 6.5+ 原生 QWaylandLayerShell（API 文档不足，暂不用）。
    find_package(Qt6 REQUIRED COMPONENTS Widgets WaylandClient DBus)
    find_package(LayerShellQt REQUIRED)
endif()

add_subdirectory(src/compositor)
if(W10DE_BUILD_SHELL)
    add_subdirectory(src/shell)
    add_subdirectory(src/lock)
endif()
# add_subdirectory(src/tray)     # 已并入 src/shell/tray/

# ---- 安装（会话启动）----
install(TARGETS w10compositor RUNTIME DESTINATION bin)
if(W10DE_BUILD_SHELL)
    install(TARGETS w10shell w10lock RUNTIME DESTINATION bin)
endif()
install(PROGRAMS src/session/w10-session DESTINATION bin)
install(FILES src/session/w10de.conf.example DESTINATION share/w10de)

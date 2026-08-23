# Win10DE —— wlroots 获取策略（跨发行版兼容）
#
# 目标：任何主流发行版上都能拿到 wlroots 0.19 的可用头/库：
#   auto（默认）：
#     1. 系统 pkg-config 提供 0.19.x 且头文件 C++ 兼容（罕见，发行版已处理）
#        → 直接使用系统包；
#     2. 系统提供 0.19.x 但头含 C99 `[static N]` / C++ 关键字
#        （class/namespace，wlroots 上游原版）→ 构建目录生成"补丁头副本"，
#        补丁目录以 -I 优先于系统 -isystem 注入（不改系统文件）；
#     3. 系统 wlroots 缺失或版本 != 0.19（Debian trixie 0.18、Ubuntu 0.17、
#        Arch 新版本 0.20）→ 自动用 meson 编译 vendored 源码
#        （third_party/wlroots，头已打补丁），装到构建目录。
#   system：强制走 1/2（系统包缺失时报错）。
#   vendored：强制走 3（忽略系统包）。
#
# 输出变量：
#   WLROOTS_TARGET          链接目标（PkgConfig::*）
#   WLR_EXTRA_INCLUDE_DIRS  补丁头目录（可能为空），调用方须以 BEFORE 加入 include

find_package(PkgConfig REQUIRED)

set(W10DE_WLROOTS_STRATEGY "auto" CACHE STRING
    "wlroots 获取策略: auto / system / vendored")

# ---- 探测系统 wlroots ----
pkg_check_modules(WLR_SYS IMPORTED_TARGET wlroots-0.19)
if(NOT WLR_SYS_FOUND)
    pkg_check_modules(WLR_SYS IMPORTED_TARGET wlroots)
endif()

# 头 C++ 兼容检测：wlr_scene.h 是否含 C99 "[static"；生成协议头是否已安装。
set(_wlr_inc_dir "")
set(_scene_header "")
set(_has_gen_hdr FALSE)
if(WLR_SYS_FOUND)
    foreach(_dir IN LISTS WLR_SYS_INCLUDE_DIRS)
        if(EXISTS "${_dir}/wlr/types/wlr_scene.h")
            set(_scene_header "${_dir}/wlr/types/wlr_scene.h")
            if(NOT _wlr_inc_dir)
                set(_wlr_inc_dir "${_dir}")
            endif()
        endif()
        if(EXISTS "${_dir}/wlr/types/wlr-layer-shell-unstable-v1-protocol.h")
            set(_has_gen_hdr TRUE)
        endif()
    endforeach()
endif()

set(_sys_compatible FALSE)
if(WLR_SYS_FOUND AND WLR_SYS_VERSION MATCHES "^0\\.19\\.")
    if(_scene_header)
        file(READ "${_scene_header}" _scene_content)
        if(NOT _scene_content MATCHES "\\[static")
            set(_sys_compatible TRUE)
        endif()
    endif()
endif()

if(W10DE_WLROOTS_STRATEGY STREQUAL "system")
    if(NOT WLR_SYS_FOUND)
        message(FATAL_ERROR "W10DE_WLROOTS_STRATEGY=system but no system wlroots found")
    endif()
    if(WLR_SYS_VERSION MATCHES "^0\\.19\\." AND _sys_compatible)
        message(STATUS "wlroots: system ${WLR_SYS_VERSION} (C++ compatible)")
        set(WLROOTS_TARGET PkgConfig::WLR_SYS)
        set(WLR_EXTRA_INCLUDE_DIRS "")
    elseif(WLR_SYS_VERSION MATCHES "^0\\.19\\.")
        # 系统 0.19 头需补丁
        include("${CMAKE_CURRENT_LIST_DIR}/WlrootsPatchHeaders.cmake")
    else()
        message(FATAL_ERROR
            "W10DE_WLROOTS_STRATEGY=system but system wlroots is ${WLR_SYS_VERSION}"
            " (need 0.19); install wlroots 0.19 or use vendored")
    endif()
elseif(W10DE_WLROOTS_STRATEGY STREQUAL "vendored")
    include("${CMAKE_CURRENT_LIST_DIR}/WlrootsBuildVendored.cmake")
else()
    # auto
    if(WLR_SYS_FOUND AND WLR_SYS_VERSION MATCHES "^0\\.19\\." AND _sys_compatible)
        message(STATUS "wlroots: system ${WLR_SYS_VERSION} (C++ compatible headers)")
        set(WLROOTS_TARGET PkgConfig::WLR_SYS)
        set(WLR_EXTRA_INCLUDE_DIRS "")
    elseif(WLR_SYS_FOUND AND WLR_SYS_VERSION MATCHES "^0\\.19\\." AND _has_gen_hdr)
        # 系统 0.19 头需补丁且生成协议头齐全 → 补丁头注入（快）
        message(STATUS "wlroots: system ${WLR_SYS_VERSION}, patching headers for C++")
        include("${CMAKE_CURRENT_LIST_DIR}/WlrootsPatchHeaders.cmake")
    else()
        # 缺失 / 版本不符 / 缺生成协议头 → vendored 编译
        if(WLR_SYS_FOUND)
            message(STATUS "wlroots: system ${WLR_SYS_VERSION} unsuitable,"
                           " building vendored 0.19.0")
        else()
            message(STATUS "wlroots: no system package, building vendored 0.19.0")
        endif()
        include("${CMAKE_CURRENT_LIST_DIR}/WlrootsBuildVendored.cmake")
    endif()
endif()

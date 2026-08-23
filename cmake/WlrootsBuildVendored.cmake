# Win10DE —— 编译 vendored wlroots 0.19（发行版版本不符/缺失时兜底）
#
# 用 meson 编译 third_party/wlroots 到构建目录（一次性，configure 阶段执行）：
#   - **编译原版源码**（头+实现一致，C 编译不受 C++ 补丁影响）
#   - -Dwerror=false：规避新版 libinput 枚举不兼容（Arch 1.31 实测）
#   - 安装后对安装头统一做 C++ 补丁注入（WlrootsPatchHeaders）
# 需要 meson + ninja + wlroots 构建依赖（wayland-server/libdrm/xkbcommon/
# pixman/libinput/libdisplay-info/libseat/hwdata/libliftoff/egl 等）。
#
# 输出：WLROOTS_TARGET、WLR_EXTRA_INCLUDE_DIRS

find_program(WLR_MESON_EXE meson REQUIRED
    DOC "meson（编译 vendored wlroots 需要；发行版包名：Debian meson / Arch meson / Fedora meson）")
find_program(WLR_NINJA_EXE ninja REQUIRED
    DOC "ninja（编译 vendored wlroots 需要）")

set(_wlr_src "${CMAKE_SOURCE_DIR}/third_party/wlroots")
set(_wlr_build "${CMAKE_BINARY_DIR}/wlroots-build")
set(_wlr_prefix "${CMAKE_BINARY_DIR}/wlroots-install")
set(_wlr_done "${CMAKE_BINARY_DIR}/wlroots-build.stamp")

if(NOT EXISTS "${_wlr_done}")
    # 清理失败的残留构建目录（stamp 缺失 = 上次未完成）。
    if(EXISTS "${_wlr_build}")
        file(REMOVE_RECURSE "${_wlr_build}")
    endif()

    message(STATUS "wlroots: configuring vendored 0.19.0 (meson)...")
    execute_process(
        COMMAND "${WLR_MESON_EXE}" setup "${_wlr_build}"
            -Dexamples=false -Dwerror=false
            "--prefix=${_wlr_prefix}"
        WORKING_DIRECTORY "${_wlr_src}"
        RESULT_VARIABLE _r
        OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR
            "meson setup vendored wlroots failed:\n${_out}\n${_err}")
    endif()

    message(STATUS "wlroots: building vendored 0.19.0 (ninja)...")
    execute_process(
        COMMAND "${WLR_NINJA_EXE}" -C "${_wlr_build}"
        RESULT_VARIABLE _r
        OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "ninja vendored wlroots failed:\n${_out}\n${_err}")
    endif()

    execute_process(
        COMMAND "${WLR_NINJA_EXE}" -C "${_wlr_build}" install
        RESULT_VARIABLE _r
        OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "ninja install vendored wlroots failed:\n${_out}\n${_err}")
    endif()

    # wlroots 0.19 的 install 规则不安装生成的协议头（如
    # wlr-layer-shell-unstable-v1-protocol.h），而 wlr_*.h 源码头会引用它们
    # —— 从 build/protocol 补装到安装 include 目录（真实编译验证）。
    file(GLOB _gen_headers "${_wlr_build}/protocol/*.h")
    if(_gen_headers)
        file(COPY ${_gen_headers}
            DESTINATION "${_wlr_prefix}/include/wlroots-0.19/wlr/types/")
    endif()

    file(WRITE "${_wlr_done}" "built")
    message(STATUS "wlroots: vendored 0.19.0 built in ${_wlr_prefix}")
endif()

# 让后续 pkg_check_modules 找到本地安装。
set(ENV{PKG_CONFIG_PATH} "${_wlr_prefix}/lib/pkgconfig")

pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET wlroots-0.19)
if(NOT WLROOTS_FOUND)
    message(FATAL_ERROR "vendored wlroots not found after build")
endif()

# 安装头为上游原版：统一做 C++ 补丁注入。
set(WLROOTS_INC_SOURCE "${_wlr_prefix}/include/wlroots-0.19")
set(WLROOTS_TARGET_RAW PkgConfig::WLROOTS)
include("${CMAKE_CURRENT_LIST_DIR}/WlrootsPatchHeaders.cmake")

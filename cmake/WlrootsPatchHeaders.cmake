# Win10DE —— 系统 wlroots 0.19 头的 C++ 兼容补丁注入
#
# 上游 wlroots 头按 C 设计，C++ 直接解析会失败（g++16 实测）：
#   - C99 数组参数 `const float color[static 4]`（wlr_scene.h）
#   - C++ 保留字成员 `char *class`（xwayland.h）、`char *namespace`
#     （wlr_layer_shell_v1.h + 生成的协议头）
# 处理：拷贝源 include 树到构建目录，仅对上述头应用文本补丁
# （等价、不改实现；**实现 .c 用原版头编译，不受影响**），补丁目录以
# -I 优先于系统 -isystem 注入。
#
# 前置：WLROOTS_INC_SOURCE —— wlroots include 目录
#       （系统 /usr/include/wlroots-0.19，或 vendored 安装 prefix/include/wlroots-0.19）
# 输出：WLROOTS_TARGET、WLR_EXTRA_INCLUDE_DIRS

if(NOT DEFINED WLROOTS_INC_SOURCE)
    if(NOT _wlr_inc_dir)
        message(FATAL_ERROR "wlroots include dir not found")
    endif()
    set(WLROOTS_INC_SOURCE "${_wlr_inc_dir}")
endif()
if(NOT EXISTS "${WLROOTS_INC_SOURCE}/wlr")
    message(FATAL_ERROR "wlroots include dir invalid: ${WLROOTS_INC_SOURCE}")
endif()

set(_patch_dir "${CMAKE_BINARY_DIR}/wlroots-cpp-patched")

# 拷贝 wlroots include 树（只拷头文件，保留 wlr/ 目录结构）。
file(REMOVE_RECURSE "${_patch_dir}")
file(MAKE_DIRECTORY "${_patch_dir}")
file(COPY "${WLROOTS_INC_SOURCE}/wlr" DESTINATION "${_patch_dir}"
    PATTERN "*.h" PATTERN "*.hpp")

# 逐个应用补丁（字符串级，行为等价）。
set(_patch_files
    "${_patch_dir}/wlr/types/wlr_scene.h"
    "${_patch_dir}/wlr/xwayland/xwayland.h"
    "${_patch_dir}/wlr/types/wlr_layer_shell_v1.h"
    "${_patch_dir}/wlr/types/wlr-layer-shell-unstable-v1-protocol.h")
foreach(_f IN LISTS _patch_files)
    if(NOT EXISTS "${_f}")
        continue()
    endif()
    file(READ "${_f}" _content)
    # C99 静态数组参数（wlr_scene.h）
    string(REGEX REPLACE "\\[static 4\\]" "[4]" _content "${_content}")
    string(REGEX REPLACE "\\[static 3\\]" "[3]" _content "${_content}")
    string(REGEX REPLACE "\\[static 9\\]" "[9]" _content "${_content}")
    # C++ 保留字成员
    string(REPLACE "char *class;" "char *class_;" _content "${_content}")
    string(REPLACE "char *namespace;" "char *namespace_;" _content "${_content}")
    # 生成协议头中的参数名 namespace（无 C++ namespace 关键字，安全全替换）
    string(REGEX REPLACE "\\bnamespace\\b" "namespace_" _content "${_content}")
    file(WRITE "${_f}" "${_content}")
endforeach()

message(STATUS "wlroots: patched headers in ${_patch_dir}")
if(DEFINED WLROOTS_TARGET_RAW)
    set(WLROOTS_TARGET "${WLROOTS_TARGET_RAW}")
else()
    set(WLROOTS_TARGET PkgConfig::WLR_SYS)
endif()
set(WLR_EXTRA_INCLUDE_DIRS "${_patch_dir}")

# Win10DE —— 依赖获取策略：系统优先，缺失/版本不符时从源码编译兜底
#
# 通用机制：每个外部依赖按
#   1) 系统 pkg-config 提供且版本满足 → 直接用系统包（零成本）；
#   2) 否则从固定版本 URL 下载源码 → 编译安装到
#      ${CMAKE_BINARY_DIR}/_deps/prefix（可复现、可增量）→ 供项目链接。
#
# 策略开关（CACHE）：
#   W10DE_DEP_SOURCE  auto（默认，缺啥编啥）| always（全部源码）| never（缺则报错）
#
# 使用（在 cmake/DepsCompat.cmake 中为每个依赖调用）：
#   w10de_dep_system_or_source(
#       NAME wayland
#       PC wayland-1.0
#       VERSION 1.23
#       URL https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.23.1/downloads/wayland-1.23.1.tar.xz
#       SHA256 <可选；空则跳过校验>
#       BUILD meson   # meson | cmake | autotools
#       MESON_ARGS -Ddocumentation=false
#   )
#
# 输出：${NAME}_OK=TRUE（系统或源码任一满足）；系统命中时额外定义
#   PkgConfig::${NAME}_SYS IMPORTED target 供链接；源码构建后统一经
#   PKG_CONFIG_PATH/CMAKE_PREFIX_PATH 注入再探测。

find_package(PkgConfig REQUIRED)

set(W10DE_DEP_SOURCE "auto" CACHE STRING
    "依赖获取策略: auto(缺啥编啥) / always(全部源码) / never(缺则报错)")

set(W10DE_DEPS_PREFIX "${CMAKE_BINARY_DIR}/_deps/prefix" CACHE PATH
    "源码编译依赖的统一安装前缀")
set(W10DE_DEPS_DL "${CMAKE_BINARY_DIR}/_deps/downloads" CACHE PATH
    "依赖源码下载缓存目录")

# 下载源选择策略：fastest（默认，逐源测延迟/速度后选最优）或 ordered（按列表顺序）。
set(W10DE_DOWNLOAD_SELECT "fastest" CACHE STRING
    "下载源选择: fastest(测延迟选最优) / ordered(按 URL 列表顺序)")
# 源探测超时（毫秒）：探测不可达源时单次等待上限。
set(W10DE_DOWNLOAD_PROBE_MS "4000" CACHE STRING
    "下载源延迟探测超时(ms)")

# 强制指定依赖走源码（分号分隔名列表；auto 模式下覆盖个别依赖用于验证）。
set(W10DE_FORCE_SOURCE_DEPS "" CACHE STRING
    "强制走源码编译的依赖名列表（如 'wayland;libdrm'，auto 模式下验证用）")

# 把源码编译前缀注入 pkg-config / CMake 搜索路径（构建完一个依赖即刷新，
# 保证依赖链（如 wayland → libdrm → wlroots）逐层可见。
function(w10de_dep_refresh_prefix)
    set(_pc "${W10DE_DEPS_PREFIX}/lib/pkgconfig:${W10DE_DEPS_PREFIX}/lib/x86_64-linux-gnu/pkgconfig")
    set(_pc_path "$ENV{PKG_CONFIG_PATH}")
    if(_pc_path)
        set(_pc "${_pc}:${_pc_path}")
    endif()
    set(ENV{PKG_CONFIG_PATH} "${_pc}")
    list(APPEND CMAKE_PREFIX_PATH "${W10DE_DEPS_PREFIX}")
endfunction()

# 版本满足判定：要求系统版本 >= 要求版本（点分段数值比较；要求为空则满足）。
function(w10de_dep_version_ok ver req out)
    if(NOT req)
        set(${out} TRUE PARENT_SCOPE)
        return()
    endif()
    string(REPLACE "." ";" _ver_parts "${ver}")
    string(REPLACE "." ";" _req_parts "${req}")
    list(LENGTH _ver_parts _ver_len)
    list(LENGTH _req_parts _req_len)
    set(_ok TRUE)
    set(_i 0)
    while(_i LESS _req_len)
        if(_i LESS _ver_len)
            list(GET _ver_parts ${_i} _v_n)
        else()
            set(_v_n 0)
        endif()
        list(GET _req_parts ${_i} _r_n)
        if(_v_n LESS _r_n)
            set(_ok FALSE)
            break()
        elseif(_v_n GREATER _r_n)
            break()
        endif()
        math(EXPR _i "${_i} + 1")
    endwhile()
    set(${out} ${_ok} PARENT_SCOPE)
endfunction()

function(w10de_dep_system_or_source)
    cmake_parse_arguments(DEP "" "NAME;PC;VERSION;URL;SHA256;BUILD"
        "MESON_ARGS;CMAKE_ARGS;CONFIGURE_ARGS" ${ARGN})

    if(NOT DEP_NAME OR NOT DEP_PC)
        message(FATAL_ERROR "w10de_dep_system_or_source: NAME and PC required")
    endif()
    if(NOT DEP_URL)
        message(FATAL_ERROR "w10de_dep_system_or_source: URL required for ${DEP_NAME}")
    endif()
    if(NOT DEP_BUILD)
        set(DEP_BUILD meson)
    endif()

    # 是否被强制走源码
    set(_forced FALSE)
    if(W10DE_FORCE_SOURCE_DEPS)
        foreach(_d IN LISTS W10DE_FORCE_SOURCE_DEPS)
            if(_d STREQUAL DEP_NAME)
                set(_forced TRUE)
            endif()
        endforeach()
    endif()

    # ---- 1) 探测系统 ----
    if(W10DE_DEP_SOURCE STREQUAL "always")
        set(_use_system FALSE)
    elseif(_forced)
        set(_use_system FALSE)
    else()
        # PC 支持分号分隔多候选（如 libdrm;drm），任一满足即可。
        set(_sys_found FALSE)
        foreach(_pc IN LISTS DEP_PC)
            pkg_check_modules(${DEP_NAME}_SYS IMPORTED_TARGET ${_pc})
            if(${DEP_NAME}_SYS_FOUND)
                set(_sys_found TRUE)
                break()
            endif()
        endforeach()
        if(_sys_found)
            if(DEP_VERSION)
                w10de_dep_version_ok("${${DEP_NAME}_SYS_VERSION}" "${DEP_VERSION}" _ver_ok)
            else()
                set(_ver_ok TRUE)
            endif()
            if(_ver_ok)
                set(_use_system TRUE)
            else()
                message(STATUS "${DEP_NAME}: system ${${DEP_NAME}_SYS_VERSION}"
                        " < required ${DEP_VERSION}, building from source")
                set(_use_system FALSE)
            endif()
        else()
            set(_use_system FALSE)
        endif()
    endif()

    if(_use_system)
        message(STATUS "${DEP_NAME}: using system ${${DEP_NAME}_SYS_VERSION}")
        set(${DEP_NAME}_OK TRUE PARENT_SCOPE)
        return()
    endif()

    if(W10DE_DEP_SOURCE STREQUAL "never")
        message(FATAL_ERROR
            "${DEP_NAME}: not found in system and W10DE_DEP_SOURCE=never")
    endif()

    # ---- 2) 源码构建（同步 execute_process，增量 stamp）----
    find_program(_NINJA ninja REQUIRED)
    find_program(_MAKE make REQUIRED)
    if(DEP_BUILD STREQUAL "meson")
        find_program(_MESON meson REQUIRED
            DOC "meson（源码编译 ${DEP_NAME} 需要；发行版包名：meson）")
    endif()

    set(_dl_dir "${W10DE_DEPS_DL}")
    file(MAKE_DIRECTORY "${_dl_dir}")
    set(_stamp "${W10DE_DEPS_PREFIX}/.${DEP_NAME}.stamp")

    if(NOT EXISTS "${_stamp}")
        # 下载（带 SHA256 校验，可选）。URL 支持分号分隔多源（官方 + 镜像 +
        # 加速通道）；tarball 已存在于下载缓存目录时跳过下载（离线构建：
        # 可把提前下载好的 tarball 放入 ${W10DE_DEPS_DL}）。
        list(GET DEP_URL 0 _first_url)
        get_filename_component(_tarball "${_first_url}" NAME)
        set(_archive "${_dl_dir}/${_tarball}")
        if(NOT EXISTS "${_archive}")
            # 优先系统 curl（CMake 内置 TLS 在部分环境下证书校验失败，实测
            # WSL/Arch 报 SSL connect error；curl CLI 走系统 CA 正常）。
            find_program(_CURL curl)

            # ---- 源排序（fastest）：逐源测延迟，选择最优顺序 ----
            set(_url_order "${DEP_URL}")
            if(_CURL AND W10DE_DOWNLOAD_SELECT STREQUAL "fastest")
                set(_probed "")
                foreach(_u IN LISTS DEP_URL)
                    # --range 0-0（GET 首字节）比 HEAD 更接近真实下载行为
                    #（gitlab archive 的 HEAD/GET 状态码可能不一致）。
                    execute_process(COMMAND "${_CURL}" -o /dev/null -s
                            --range 0-0 --max-time ${W10DE_DOWNLOAD_PROBE_MS}
                            -w "%{http_code} %{time_connect}" "${_u}"
                        RESULT_VARIABLE _pr
                        OUTPUT_VARIABLE _po ERROR_VARIABLE _pe)
                    string(STRIP "${_po}" _po)
                    list(GET _po 0 _pcode)
                    if(_pr EQUAL 0 AND _pcode MATCHES "^[0-9]+$" AND _pcode LESS 500)
                        list(GET _po 1 _ptime)
                        list(APPEND _probed "${_ptime}|${_u}")
                    else()
                        list(APPEND _probed "9999|${_u}")
                    endif()
                endforeach()
                # 按延迟升序（时间|URL 字符串排序，数字前缀同位数有效）。
                list(SORT _probed)
                set(_url_order "")
                foreach(_p IN LISTS _probed)
                    string(FIND "${_p}" "|" _sep)
                    string(SUBSTRING "${_p}" 0 ${_sep} _pt)
                    math(EXPR _u0 "${_sep} + 1")
                    string(SUBSTRING "${_p}" ${_u0} -1 _pu)
                    list(APPEND _url_order "${_pu}")
                endforeach()
                message(STATUS "${DEP_NAME}: source order (fastest first): ${_url_order}")
            endif()

            set(_downloaded FALSE)
            foreach(_url IN LISTS _url_order)
                message(STATUS "${DEP_NAME}: downloading ${_url}")
                if(_CURL)
                    execute_process(COMMAND "${_CURL}" -fL --retry 2 --retry-all-errors
                            --retry-delay 2 -o "${_archive}" "${_url}"
                        RESULT_VARIABLE _dl_code
                        OUTPUT_VARIABLE _dl_out ERROR_VARIABLE _dl_err)
                else()
                    file(DOWNLOAD "${_url}" "${_archive}"
                        STATUS _dl_status SHOW_PROGRESS)
                    list(GET _dl_status 0 _dl_code)
                endif()
                # 魔数校验：gitlab 对不存在的 ref/路径返回 HTTP 200 + HTML
                # 错误页（实测），须拒绝非压缩包内容并换下一源。
                set(_magic_ok FALSE)
                if(_dl_code EQUAL 0 AND EXISTS "${_archive}")
                    file(READ "${_archive}" _magic LIMIT 4 HEX)
                    if(_magic MATCHES "^1f8b"        # gzip
                       OR _magic MATCHES "^fd377a"   # xz
                       OR _magic MATCHES "^425a"     # bzip2
                       OR _magic MATCHES "^757374"   # tar
                       OR _magic MATCHES "^504b")    # zip
                        set(_magic_ok TRUE)
                    endif()
                endif()
                if(_dl_code EQUAL 0 AND _magic_ok)
                    set(_downloaded TRUE)
                    break()
                endif()
                # 失败：清掉残留文件（HTML 错误页或半成品）换下一源。
                file(REMOVE "${_archive}")
                message(STATUS "${DEP_NAME}: ${_url} failed (code=${_dl_code}"
                        " magic_ok=${_magic_ok}), trying next source")
            endforeach()
            if(NOT _downloaded)
                message(FATAL_ERROR
                    "${DEP_NAME}: all download sources failed"
                    " (${DEP_URL}); 可将 tarball 预置于 ${_dl_dir} 离线构建"
                    " 或配置 W10DE_DEP_MIRROR（见 DepsCompat.cmake）")
            endif()
        endif()
        if(DEP_SHA256)
            file(SHA256 "${_archive}" _hash)
            if(NOT _hash STREQUAL DEP_SHA256)
                message(FATAL_ERROR
                    "${DEP_NAME}: SHA256 mismatch (got ${_hash}, want ${DEP_SHA256})")
            endif()
        endif()

        # 解包（到独立临时目录，避免 glob 误匹配同前缀的兄弟依赖源码，
        # 如 wayland-* 匹配 wayland-protocols）。
        set(_src "${W10DE_DEPS_PREFIX}/src/${DEP_NAME}")
        set(_src_tmp "${W10DE_DEPS_PREFIX}/src/.${DEP_NAME}.tmp")
        file(REMOVE_RECURSE "${_src}" "${_src_tmp}")
        file(MAKE_DIRECTORY "${_src_tmp}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${_archive}"
            WORKING_DIRECTORY "${_src_tmp}"
            RESULT_VARIABLE _r)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR "${DEP_NAME}: extract failed (${DEP_URL})")
        endif()
        # 定位解出的唯一顶层目录并重命名到 _src。
        file(GLOB _extracted "${_src_tmp}/*")
        list(LENGTH _extracted _n)
        if(_n EQUAL 1 AND IS_DIRECTORY "${_extracted}")
            file(RENAME "${_extracted}" "${_src}")
            file(REMOVE_RECURSE "${_src_tmp}")
        else()
            file(RENAME "${_src_tmp}" "${_src}")
        endif()

        set(_build_dir "${W10DE_DEPS_PREFIX}/build/${DEP_NAME}")
        file(REMOVE_RECURSE "${_build_dir}")
        file(MAKE_DIRECTORY "${_build_dir}")

        if(DEP_BUILD STREQUAL "meson")
            message(STATUS "${DEP_NAME}: meson setup...")
            execute_process(COMMAND "${_MESON}" setup "${_build_dir}"
                    "--prefix=${W10DE_DEPS_PREFIX}" -Dwerror=false
                    ${DEP_MESON_ARGS}
                WORKING_DIRECTORY "${_src}"
                RESULT_VARIABLE _r
                OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
            if(NOT _r EQUAL 0)
                message(FATAL_ERROR
                    "${DEP_NAME}: meson setup failed:\n${_out}\n${_err}")
            endif()
        elseif(DEP_BUILD STREQUAL "autotools")
            message(STATUS "${DEP_NAME}: configure...")
            execute_process(COMMAND "${_src}/configure"
                    "--prefix=${W10DE_DEPS_PREFIX}" ${DEP_CONFIGURE_ARGS}
                WORKING_DIRECTORY "${_build_dir}"
                RESULT_VARIABLE _r
                OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
            if(NOT _r EQUAL 0)
                message(FATAL_ERROR
                    "${DEP_NAME}: configure failed:\n${_out}\n${_err}")
            endif()
        endif()

        message(STATUS "${DEP_NAME}: building...")
        if(DEP_BUILD STREQUAL "meson")
            execute_process(COMMAND "${_NINJA}" -C "${_build_dir}"
                RESULT_VARIABLE _r OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
        else()
            execute_process(COMMAND "${_MAKE}" -C "${_build_dir}" -j
                RESULT_VARIABLE _r OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
        endif()
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR "${DEP_NAME}: build failed:\n${_out}\n${_err}")
        endif()

        message(STATUS "${DEP_NAME}: installing...")
        if(DEP_BUILD STREQUAL "meson")
            execute_process(COMMAND "${_NINJA}" -C "${_build_dir}" install
                RESULT_VARIABLE _r OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
        else()
            execute_process(COMMAND "${_MAKE}" -C "${_build_dir}" install
                RESULT_VARIABLE _r OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
        endif()
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR "${DEP_NAME}: install failed:\n${_out}\n${_err}")
        endif()

        file(WRITE "${_stamp}" "built")
        message(STATUS "${DEP_NAME}: built from source in ${W10DE_DEPS_PREFIX}")
    endif()

    # 刷新前缀注入，供后续依赖与项目 find 使用。
    w10de_dep_refresh_prefix()
    set(${DEP_NAME}_OK TRUE PARENT_SCOPE)
endfunction()

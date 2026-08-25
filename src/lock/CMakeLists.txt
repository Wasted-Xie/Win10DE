# w10lock —— 锁屏进程（ext-session-lock-v1，M6）

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
# libpam 无 pkg-config 文件（Arch pam 包）：find_library 查找。
find_library(PAM_LIBRARY NAMES pam REQUIRED)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# ---- wayland-scanner 生成 ext-session-lock 客户端代码 ----
set(SESSION_LOCK_XML ${CMAKE_SOURCE_DIR}/protocols/ext-session-lock-v1.xml)
set(SESSION_LOCK_HDR ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-client-protocol.h)
set(SESSION_LOCK_SRC ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-protocol.c)

add_custom_command(
    OUTPUT ${SESSION_LOCK_HDR}
    COMMAND ${WAYLAND_SCANNER} client-header ${SESSION_LOCK_XML} ${SESSION_LOCK_HDR}
    DEPENDS ${SESSION_LOCK_XML})
add_custom_command(
    OUTPUT ${SESSION_LOCK_SRC}
    COMMAND ${WAYLAND_SCANNER} private-code ${SESSION_LOCK_XML} ${SESSION_LOCK_SRC}
    DEPENDS ${SESSION_LOCK_XML})

add_executable(w10lock
    main.cpp
    lockclient.cpp
    pamcheck.cpp
    ${SESSION_LOCK_SRC}
    ${SESSION_LOCK_HDR}   # 生成头列入依赖，避免并行构建时序竞争
)

target_include_directories(w10lock PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}   # 生成的协议头
)

target_link_libraries(w10lock PRIVATE
    Qt6::Gui
    Qt6::WaylandClient
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::XKBCOMMON
    ${PAM_LIBRARY}
)

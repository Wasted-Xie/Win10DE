# w10shell —— Win10DE Shell 客户端（Qt Widgets，layer-shell 客户端）
#
# M3：任务栏（bottom 层）+ 窗口列表（foreign-toplevel）+ 开始菜单（后续）。
# layer-shell 绑定：KDE layer-shell-qt（LayerShellQt 命名空间）。
# foreign-toplevel 客户端：wayland-scanner 从 wlr-protocols XML 生成。

# Q_OBJECT 类需要 moc 预处理。
set(CMAKE_AUTOMOC ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# ---- wayland-scanner 生成 foreign-toplevel 客户端代码 ----
set(FOREIGN_TOPLEVEL_XML
    ${CMAKE_SOURCE_DIR}/third_party/wlroots/protocol/wlr-foreign-toplevel-management-unstable-v1.xml)
set(FOREIGN_TOPLEVEL_HDR ${CMAKE_CURRENT_BINARY_DIR}/foreign-toplevel-client-protocol.h)
set(FOREIGN_TOPLEVEL_SRC ${CMAKE_CURRENT_BINARY_DIR}/foreign-toplevel-protocol.c)

add_custom_command(
    OUTPUT ${FOREIGN_TOPLEVEL_HDR}
    COMMAND ${WAYLAND_SCANNER} client-header ${FOREIGN_TOPLEVEL_XML} ${FOREIGN_TOPLEVEL_HDR}
    DEPENDS ${FOREIGN_TOPLEVEL_XML})
add_custom_command(
    OUTPUT ${FOREIGN_TOPLEVEL_SRC}
    COMMAND ${WAYLAND_SCANNER} private-code ${FOREIGN_TOPLEVEL_XML} ${FOREIGN_TOPLEVEL_SRC}
    DEPENDS ${FOREIGN_TOPLEVEL_XML})

add_executable(w10shell
    main.cpp
    theme/colors.cpp
    desktop/desktopwindow.cpp
    taskbar/taskbarwindow.cpp
    taskbar/clock.cpp
    taskbar/startbutton.cpp
    taskbar/taskbarbutton.cpp
    startmenu/appmodel.cpp
    startmenu/startmenu.cpp
    startmenu/tilebutton.cpp
    startmenu/flowlayout.cpp
    tray/sniwatcher.cpp
    tray/trayicon.cpp
    tray/trayarea.cpp
    ipc/lockservice.cpp
    ipc/foreigntoplevel.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${FOREIGN_TOPLEVEL_SRC}
    ${FOREIGN_TOPLEVEL_HDR}   # 生成头列入依赖，避免并行构建时序竞争
)

target_include_directories(w10shell PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # 支持 "startmenu/xxx.h"、"tray/xxx.h" 等内部前缀写法
    ${CMAKE_CURRENT_BINARY_DIR}   # 生成的协议头
)

# layer-shell-qt 导出的 target 名随版本不同：Arch 为 LayerShellQt::Interface，
# 部分发行版/旧版本为 LayerShellQt::LayerShellQt —— 两者兼容。
if(TARGET LayerShellQt::Interface)
    set(LAYERSHELLQT_TARGET LayerShellQt::Interface)
else()
    set(LAYERSHELLQT_TARGET LayerShellQt::LayerShellQt)
endif()

target_link_libraries(w10shell PRIVATE
    Qt6::Widgets
    Qt6::WaylandClient
    Qt6::DBus
    ${LAYERSHELLQT_TARGET}
    PkgConfig::WAYLAND_CLIENT
)

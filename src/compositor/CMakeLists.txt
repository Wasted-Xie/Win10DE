# w10compositor —— Win10DE 合成器
#
# M0：headless compositor 骨架（生命周期 + 输出帧循环 + 截图验证）。
# M1+ 将加入 xdg-shell / seat / XWayland 等。

add_executable(w10compositor
    main.cpp
    server.cpp
    output.cpp
    seat.cpp
    view.cpp
    layer_shell.cpp
    xview.cpp
    titletext.cpp
    shadow.cpp
    alttab.cpp
    snaplayout.cpp
    dbus_service.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/inputsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/shortcuts.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
)

target_include_directories(w10compositor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/stb   # stb_image_write.h（PNG 输出）
)
# 补丁头优先于系统 wlroots 头（BEFORE 修饰符须位于 PRIVATE 之前）。
if(WLR_EXTRA_INCLUDE_DIRS)
    target_include_directories(w10compositor BEFORE PRIVATE ${WLR_EXTRA_INCLUDE_DIRS})
endif()

# wlroots 不稳定接口需要该宏。
target_compile_definitions(w10compositor PRIVATE WLR_USE_UNSTABLE)

# wayland-server：wlroots.pc 的依赖在 Requires.private（仅静态链接传递），
# 而 compositor 直接调用 wl_display_create 等 API，须显式链接。
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
# M2b 标题栏文字（cairo/pango）。
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(PANGO REQUIRED IMPORTED_TARGET pangocairo)
# D-Bus 服务（org.w10de.Compositor：显示设置 IPC，第二批）。
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
# 输入设备设置（libinput 指针配置：accel/natural/left-handed/tap）。
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)

target_link_libraries(w10compositor PRIVATE
    ${WLROOTS_TARGET}
    PkgConfig::DRM
    PkgConfig::XKBCOMMON
    PkgConfig::WAYLAND_SERVER
    PkgConfig::CAIRO
    PkgConfig::PANGO
    PkgConfig::DBUS
    PkgConfig::LIBINPUT
)

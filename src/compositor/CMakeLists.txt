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
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
)

target_include_directories(w10compositor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/stb   # stb_image_write.h（PNG 输出）
)

# wlroots 不稳定接口需要该宏。
target_compile_definitions(w10compositor PRIVATE WLR_USE_UNSTABLE)

target_link_libraries(w10compositor PRIVATE
    PkgConfig::WLROOTS
    PkgConfig::DRM
    PkgConfig::XKBCOMMON
)

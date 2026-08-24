# w10explorer —— Win10 风格文件资源管理器（系统应用，docs/SYSTEMAPPS.md）
#
# 独立二进制 + 通用接口（appipc 单实例 D-Bus 激活）；安装 .desktop 供
# 开始菜单/桌面 appmodel 扫描启动。

# appipc 静态库（后续系统应用复用）。
add_library(systemapps_appipc STATIC
    appipc.cpp
)
target_include_directories(systemapps_appipc PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(systemapps_appipc PUBLIC
    Qt6::Core
    Qt6::DBus
)
set_target_properties(systemapps_appipc PROPERTIES AUTOMOC ON)

add_executable(w10explorer
    explorer/main.cpp
    explorer/explorerwindow.cpp
    explorer/fileops.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10explorer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10explorer PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10explorer PROPERTIES AUTOMOC ON)

# .desktop 注册（开始菜单/桌面启动入口）。
set(EXPLORER_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10explorer.desktop")
file(WRITE "${EXPLORER_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=文件资源管理器\n"
    "Name[en]=File Explorer\n"
    "Comment=浏览和管理文件\n"
    "Exec=w10explorer %U\n"
    "Icon=system-file-manager\n"
    "Terminal=false\n"
    "Categories=Utility;FileManager;\n"
    "StartupNotify=true\n")
install(FILES "${EXPLORER_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10explorer RUNTIME DESTINATION bin)

# ---- w10settings：设置中心（KDE System Settings 风格）----
add_executable(w10settings
    settings/main.cpp
    settings/settingswindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10settings PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10settings PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10settings PROPERTIES AUTOMOC ON)

set(SETTINGS_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10settings.desktop")
file(WRITE "${SETTINGS_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=设置\n"
    "Name[en]=Settings\n"
    "Comment=系统设置（主题/壁纸/会话）\n"
    "Exec=w10settings\n"
    "Icon=preferences-system\n"
    "Terminal=false\n"
    "Categories=Settings;System;\n"
    "StartupNotify=true\n")
install(FILES "${SETTINGS_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10settings RUNTIME DESTINATION bin)

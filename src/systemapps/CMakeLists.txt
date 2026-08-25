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
    settings/powerinfo.cpp
    settings/audioinfo.cpp
    settings/defaultapps.cpp
    settings/networkinfo.cpp
    settings/bluetoothinfo.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/inputsettings.cpp
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
# libpulse：音频控制（第二批；pipewire-pulse 提供 Pulse 兼容服务）。
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
target_link_libraries(w10settings PRIVATE PkgConfig::PULSE)
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

# ---- w10term：终端（PTY + ANSI 渲染）----
add_executable(w10term
    term/main.cpp
    term/termwindow.cpp
    term/terminalpty.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10term PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10term PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10term PROPERTIES AUTOMOC ON)

set(TERM_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10term.desktop")
file(WRITE "${TERM_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=终端\n"
    "Name[en]=Terminal\n"
    "Comment=命令提示符终端\n"
    "Exec=w10term\n"
    "Icon=utilities-terminal\n"
    "Terminal=false\n"
    "Categories=System;TerminalEmulator;\n"
    "StartupNotify=true\n")
install(FILES "${TERM_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10term RUNTIME DESTINATION bin)

# ---- w10screenshot：截图工具（wlr-screencopy 协议客户端，第三批）----
set(SCREENCOPY_XML
    ${CMAKE_SOURCE_DIR}/third_party/wlroots/protocol/wlr-screencopy-unstable-v1.xml)
set(SCREENCOPY_HDR ${CMAKE_CURRENT_BINARY_DIR}/wlr-screencopy-unstable-v1-client-protocol.h)
set(SCREENCOPY_SRC ${CMAKE_CURRENT_BINARY_DIR}/wlr-screencopy-protocol.c)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
add_custom_command(
    OUTPUT ${SCREENCOPY_HDR}
    COMMAND ${WAYLAND_SCANNER} client-header ${SCREENCOPY_XML} ${SCREENCOPY_HDR}
    DEPENDS ${SCREENCOPY_XML})
add_custom_command(
    OUTPUT ${SCREENCOPY_SRC}
    COMMAND ${WAYLAND_SCANNER} private-code ${SCREENCOPY_XML} ${SCREENCOPY_SRC}
    DEPENDS ${SCREENCOPY_XML})

add_executable(w10screenshot
    screenshot/main.cpp
    ${SCREENCOPY_SRC}
    ${SCREENCOPY_HDR}   # 生成头列入依赖，避免并行构建时序竞争
)
target_include_directories(w10screenshot PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/stb
)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
target_link_libraries(w10screenshot PRIVATE PkgConfig::WAYLAND_CLIENT)
install(TARGETS w10screenshot RUNTIME DESTINATION bin)

set(SCREENSHOT_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10screenshot.desktop")
file(WRITE "${SCREENSHOT_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=截图工具\n"
    "Name[en]=Screenshot\n"
    "Comment=捕获屏幕并保存为 PNG（wlr-screencopy）\n"
    "Exec=w10screenshot\n"
    "Icon=camera-photo\n"
    "Terminal=false\n"
    "Categories=Utility;Graphics;\n"
    "StartupNotify=true\n")
install(FILES "${SCREENSHOT_DESKTOP}" DESTINATION share/applications)

# ---- w10monitor：系统监视器（任务管理器风格，第三批）----
add_executable(w10monitor
    monitor/main.cpp
    monitor/monitorwindow.cpp
    monitor/sysinfo.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10monitor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10monitor PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10monitor PROPERTIES AUTOMOC ON)

set(MONITOR_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10monitor.desktop")
file(WRITE "${MONITOR_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=任务管理器\n"
    "Name[en]=Task Manager\n"
    "Comment=系统监视器（CPU/内存/磁盘/网络）\n"
    "Exec=w10monitor\n"
    "Icon=utilities-system-monitor\n"
    "Terminal=false\n"
    "Categories=System;Monitor;\n"
    "StartupNotify=true\n")
install(FILES "${MONITOR_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10monitor RUNTIME DESTINATION bin)

# ---- w10calc：计算器（Win10 标准模式，第三批）----
add_executable(w10calc
    calculator/main.cpp
    calculator/calculatorwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10calc PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10calc PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10calc PROPERTIES AUTOMOC ON)

set(CALC_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10calc.desktop")
file(WRITE "${CALC_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=计算器\n"
    "Name[en]=Calculator\n"
    "Comment=标准计算器\n"
    "Exec=w10calc\n"
    "Icon=accessories-calculator\n"
    "Terminal=false\n"
    "Categories=Utility;Calculator;\n"
    "StartupNotify=true\n")
install(FILES "${CALC_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10calc RUNTIME DESTINATION bin)

# ---- w10software：软件中心（已安装应用管理，KDE-GAP 高优先 #1）----
add_executable(w10software
    software/main.cpp
    software/storewindow.cpp
    software/softwarestore.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10software PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10software PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10software PROPERTIES AUTOMOC ON)

set(SOFTWARE_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10software.desktop")
file(WRITE "${SOFTWARE_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=软件中心\n"
    "Name[en]=Software Center\n"
    "Comment=已安装应用管理（启动/卸载）\n"
    "Exec=w10software\n"
    "Icon=system-software-install\n"
    "Terminal=false\n"
    "Categories=System;PackageManager;\n"
    "StartupNotify=true\n")
install(FILES "${SOFTWARE_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10software RUNTIME DESTINATION bin)

# ---- w10viewer：文本/PDF/图像查看器（KDE-GAP 中优先 #2）----
# PDF 渲染：poppler-qt6（Okular 同款后端；Qt6Pdf 无 Arch 官方包）。
pkg_check_modules(POPPLER_QT6 REQUIRED IMPORTED_TARGET poppler-qt6)
add_executable(w10viewer
    viewer/main.cpp
    viewer/viewerwindow.cpp
    viewer/filetype.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10viewer PRIVATE
    Qt6::Widgets
    Qt6::DBus
    PkgConfig::POPPLER_QT6
    systemapps_appipc
)
set_target_properties(w10viewer PROPERTIES AUTOMOC ON)

set(VIEWER_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10viewer.desktop")
file(WRITE "${VIEWER_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=查看器\n"
    "Name[en]=Viewer\n"
    "Comment=文本、PDF 与图像查看器\n"
    "Exec=w10viewer %f\n"
    "Icon=document-viewer\n"
    "Terminal=false\n"
    "MimeType=text/plain;text/markdown;text/x-log;application/pdf;"
    "image/png;image/jpeg;image/bmp;image/webp;image/svg+xml;image/gif;\n"
    "Categories=Graphics;Office;Viewer;\n"
    "StartupNotify=true\n")
install(FILES "${VIEWER_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10viewer RUNTIME DESTINATION bin)

# ---- w10trash：回收站窗口（KDE-GAP 中优先 #3）----
# freedesktop Trash spec：~/.local/share/Trash/{files,info}（w10explorer
# 删除进回收站同格式互操作）。
add_executable(w10trash
    trash/main.cpp
    trash/trashwindow.cpp
    trash/trashstore.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10trash PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10trash PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10trash PROPERTIES AUTOMOC ON)

set(TRASH_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10trash.desktop")
file(WRITE "${TRASH_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=回收站\n"
    "Name[en]=Trash\n"
    "Comment=查看与恢复已删除文件\n"
    "Exec=w10trash\n"
    "Icon=user-trash-full\n"
    "Terminal=false\n"
    "Categories=System;Utility;\n"
    "StartupNotify=true\n")
install(FILES "${TRASH_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10trash RUNTIME DESTINATION bin)

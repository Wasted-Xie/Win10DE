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
    common/monitorarrangement.cpp
    common/ruleeditdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/inputsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/nightlight.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/shortcuts.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/windowrules.cpp
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

# ---- w10control：控制面板（Win10 传统"按类别"视图，WIN10-GAP G1）----
# 与 w10settings 共享后端（config.ini + org.w10de.Compositor D-Bus + info
# 类），覆盖全部功能（G1 要求双入口可改所有功能）。
add_executable(w10control
    control/main.cpp
    control/controlwindow.cpp
    control/categorydialogs.cpp
    common/monitorarrangement.cpp
    common/ruleeditdialog.cpp
    settings/powerinfo.cpp
    settings/audioinfo.cpp
    settings/defaultapps.cpp
    settings/networkinfo.cpp
    settings/bluetoothinfo.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/inputsettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/nightlight.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/shortcuts.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/windowrules.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10control PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10control PRIVATE
    Qt6::Widgets
    Qt6::DBus
    PkgConfig::PULSE   # audioinfo（libpulse 客户端）
    systemapps_appipc
)
set_target_properties(w10control PROPERTIES AUTOMOC ON)

set(CONTROL_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10control.desktop")
file(WRITE "${CONTROL_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=控制面板\n"
    "Name[en]=Control Panel\n"
    "Comment=传统控制面板（所有系统设置）\n"
    "Exec=w10control\n"
    "Icon=preferences-system\n"
    "Terminal=false\n"
    "Categories=Settings;System;\n"
    "StartupNotify=true\n")
install(FILES "${CONTROL_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10control RUNTIME DESTINATION bin)

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

# ---- w10screenshot：截图工具（G2：区域/窗口/延时 + 交互模式）----
# 捕获核心 capture.cpp（纯 C++，wlr-screencopy 客户端）+ Qt 交互 UI。
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
    screenshot/capture.cpp
    screenshot/screenshotwindow.cpp
    ${SCREENCOPY_SRC}
    ${SCREENCOPY_HDR}   # 生成头列入依赖，避免并行构建时序竞争
)
target_include_directories(w10screenshot PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src          # systemapps/screenshot/*.h（G2）
    ${CMAKE_SOURCE_DIR}/third_party/stb
)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
target_link_libraries(w10screenshot PRIVATE
    PkgConfig::WAYLAND_CLIENT
    Qt6::Widgets
    Qt6::DBus
)
set_target_properties(w10screenshot PROPERTIES AUTOMOC ON)
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
    Qt6::Core5Compat  # 审查 V5：GB18030 文本编码（QTextCodec）
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
    # 审查 V4：MimeType 与 filetype 白名单对齐（含 tiff/xpm/ico/csv/xml 等）。
    "MimeType=text/plain;text/markdown;text/x-log;text/csv;text/xml;"
    "text/yaml;text/x-json;text/x-c;text/x-c++;text/x-shellscript;"
    "application/pdf;image/png;image/jpeg;image/bmp;image/webp;image/gif;"
    "image/svg+xml;image/tiff;image/x-xpixmap;image/x-ico;\n"
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

# ---- w10devices：设备管理器（WIN10-GAP G3：硬件树 + 设备详情）----
# 数据源 sysfs/proc（无外部命令依赖）；8 类别（处理器/内存/磁盘/显卡/
# 网络/USB/PCI/输入）。
add_executable(w10devices
    devices/main.cpp
    devices/devicemodel.cpp
    devices/deviceswindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10devices PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10devices PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10devices PROPERTIES AUTOMOC ON)

set(DEVICES_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10devices.desktop")
file(WRITE "${DEVICES_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=设备管理器\n"
    "Name[en]=Device Manager\n"
    "Comment=查看硬件设备与详情（sysfs/proc）\n"
    "Exec=w10devices\n"
    "Icon=preferences-system\n"
    "Terminal=false\n"
    "Categories=System;HardwareSettings;\n"
    "StartupNotify=true\n")
install(FILES "${DEVICES_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10devices RUNTIME DESTINATION bin)

# ---- w10recorder：录音机（可选拓展 E6：libpulse record/playback）----
add_executable(w10recorder
    recorder/main.cpp
    recorder/recorder.cpp
    recorder/recorderwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10recorder PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10recorder PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
target_link_libraries(w10recorder PRIVATE PkgConfig::PULSE)
set_target_properties(w10recorder PROPERTIES AUTOMOC ON)

set(RECORDER_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10recorder.desktop")
file(WRITE "${RECORDER_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=录音机\n"
    "Name[en]=Voice Recorder\n"
    "Comment=录音与播放（PulseAudio）\n"
    "Exec=w10recorder\n"
    "Icon=audio-recorder\n"
    "Terminal=false\n"
    "Categories=AudioVideo;Audio;\n"
    "StartupNotify=true\n")
install(FILES "${RECORDER_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10recorder RUNTIME DESTINATION bin)

# ---- w10cleanup：磁盘清理（可选拓展 E7：回收站 + 缓存扫描清理）----
add_executable(w10cleanup
    cleanup/main.cpp
    cleanup/cleanupscanner.cpp
    cleanup/cleanupwindow.cpp
    trash/trashstore.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10cleanup PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10cleanup PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10cleanup PROPERTIES AUTOMOC ON)

set(CLEANUP_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10cleanup.desktop")
file(WRITE "${CLEANUP_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=磁盘清理\n"
    "Name[en]=Disk Cleanup\n"
    "Comment=清理回收站与系统缓存\n"
    "Exec=w10cleanup\n"
    "Icon=edit-clear\n"
    "Terminal=false\n"
    "Categories=System;Utility;\n"
    "StartupNotify=true\n")
install(FILES "${CLEANUP_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10cleanup RUNTIME DESTINATION bin)

# ---- w10osk：屏幕键盘（可选拓展 E8：D-Bus InputKey 注入）----
add_executable(w10osk
    osk/main.cpp
    osk/osk.cpp
    osk/oskwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10osk PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10osk PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
# keysym 宏（xkbcommon-keysyms.h）仅头文件，无需链接库。
find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON IMPORTED_TARGET xkbcommon)
target_link_libraries(w10osk PRIVATE PkgConfig::XKBCOMMON)
set_target_properties(w10osk PROPERTIES AUTOMOC ON)

set(OSK_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10osk.desktop")
file(WRITE "${OSK_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=屏幕键盘\n"
    "Name[en]=On-Screen Keyboard\n"
    "Comment=虚拟键盘（经 compositor 注入按键）\n"
    "Exec=w10osk\n"
    "Icon=input-keyboard\n"
    "Terminal=false\n"
    "Categories=Utility;Accessibility;\n"
    "StartupNotify=true\n")
install(FILES "${OSK_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10osk RUNTIME DESTINATION bin)

# ---- w10rdp：远程桌面连接（可选拓展 E9：wlfreerdp3/xfreerdp3 封装）----
# 构建不依赖 freerdp（QProcess 调外部客户端）；运行需安装 freerdp 包。
add_executable(w10rdp
    rdp/main.cpp
    rdp/rdpclient.cpp
    rdp/rdpwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10rdp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10rdp PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10rdp PROPERTIES AUTOMOC ON)

set(RDP_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10rdp.desktop")
file(WRITE "${RDP_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=远程桌面连接\n"
    "Name[en]=Remote Desktop Connection\n"
    "Comment=RDP 客户端（FreeRDP）\n"
    "Exec=w10rdp\n"
    "Icon=preferences-desktop-remote-desktop\n"
    "Terminal=false\n"
    "Categories=Network;RemoteAccess;\n"
    "StartupNotify=true\n")
install(FILES "${RDP_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10rdp RUNTIME DESTINATION bin)

# ---- w10disks：磁盘管理（可选拓展 E10：sysfs 只读 + udisks 增强）----
# 构建不依赖 udisks（QDBus 探测，服务缺失自动降级 sysfs）。
add_executable(w10disks
    disks/main.cpp
    disks/diskscanner.cpp
    disks/diskswindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10disks PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10disks PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10disks PROPERTIES AUTOMOC ON)

set(DISKS_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10disks.desktop")
file(WRITE "${DISKS_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=磁盘管理\n"
    "Name[en]=Disk Management\n"
    "Comment=只读浏览磁盘与分区\n"
    "Exec=w10disks\n"
    "Icon=drive-harddisk\n"
    "Terminal=false\n"
    "Categories=System;Utility;\n"
    "StartupNotify=true\n")
install(FILES "${DISKS_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10disks RUNTIME DESTINATION bin)

# ---- w10calendar：日历（可选拓展 E11：月视图 + 事件 CRUD）----
add_executable(w10calendar
    calendar/main.cpp
    calendar/eventstore.cpp
    calendar/calendarwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10calendar PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10calendar PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10calendar PROPERTIES AUTOMOC ON)

set(CALENDAR_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10calendar.desktop")
file(WRITE "${CALENDAR_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=日历\n"
    "Name[en]=Calendar\n"
    "Comment=月视图与事件管理\n"
    "Exec=w10calendar\n"
    "Icon=office-calendar\n"
    "Terminal=false\n"
    "Categories=Office;Calendar;\n"
    "StartupNotify=true\n")
install(FILES "${CALENDAR_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10calendar RUNTIME DESTINATION bin)

# ---- w10tasks：任务计划程序（WIN10-GAP G5：cron 等价 GUI + 调度守护）----
# 双模式：GUI（w10tasks）管理任务；--daemon 常驻每分钟检查执行
#（会话 w10-session 拉起；D-Bus org.w10de.Tasks Reload）。
add_executable(w10tasks
    tasks/main.cpp
    tasks/taskstore.cpp
    tasks/taskwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10tasks PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10tasks PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10tasks PROPERTIES AUTOMOC ON)

set(TASKS_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10tasks.desktop")
file(WRITE "${TASKS_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=任务计划程序\n"
    "Name[en]=Task Scheduler\n"
    "Comment=创建/编辑/启停定时任务（cron 等价）\n"
    "Exec=w10tasks\n"
    "Icon=appointment-new\n"
    "Terminal=false\n"
    "Categories=System;Utility;\n"
    "StartupNotify=true\n")
install(FILES "${TASKS_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10tasks RUNTIME DESTINATION bin)

# ---- w10sticky：便笺（可选拓展 E1：置顶便签，QTextEdit 轻量）----
add_executable(w10sticky
    sticky/main.cpp
    sticky/stickywindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10sticky PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10sticky PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10sticky PROPERTIES AUTOMOC ON)

set(STICKY_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10sticky.desktop")
file(WRITE "${STICKY_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=便笺\n"
    "Name[en]=Sticky Notes\n"
    "Comment=置顶便签\n"
    "Exec=w10sticky\n"
    "Icon=appointment-new\n"
    "Terminal=false\n"
    "Categories=Utility;TextEditor;\n"
    "StartupNotify=true\n")
install(FILES "${STICKY_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10sticky RUNTIME DESTINATION bin)

# ---- w10paint：画图（可选拓展 E2：QPainter 打开/绘制/保存 PNG）----
add_executable(w10paint
    paint/main.cpp
    paint/paintwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10paint PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10paint PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10paint PROPERTIES AUTOMOC ON)

set(PAINT_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10paint.desktop")
file(WRITE "${PAINT_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=画图\n"
    "Name[en]=Paint\n"
    "Comment=绘图（画笔/形状/颜色）\n"
    "Exec=w10paint\n"
    "Icon=applications-graphics\n"
    "Terminal=false\n"
    "Categories=Graphics;Painting;\n"
    "MimeType=image/png;image/jpeg;image/bmp;\n"
    "StartupNotify=true\n")
install(FILES "${PAINT_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10paint RUNTIME DESTINATION bin)

# ---- w10pad：写字板（可选拓展 E3：QTextEdit 富文本）----
add_executable(w10pad
    pad/main.cpp
    pad/padwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10pad PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10pad PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10pad PROPERTIES AUTOMOC ON)

set(PAD_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10pad.desktop")
file(WRITE "${PAD_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=写字板\n"
    "Name[en]=WordPad\n"
    "Comment=富文本编辑器\n"
    "Exec=w10pad\n"
    "Icon=accessories-text-editor\n"
    "Terminal=false\n"
    "Categories=Utility;TextEditor;\n"
    "MimeType=text/plain;text/html;\n"
    "StartupNotify=true\n")
install(FILES "${PAD_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10pad RUNTIME DESTINATION bin)

# ---- w10charmap：字符映射表（可选拓展 E4：Unicode 区块浏览）----
add_executable(w10charmap
    charmap/main.cpp
    charmap/charmapwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10charmap PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10charmap PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10charmap PROPERTIES AUTOMOC ON)

set(CHARMAP_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10charmap.desktop")
file(WRITE "${CHARMAP_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=字符映射表\n"
    "Name[en]=Character Map\n"
    "Comment=特殊字符浏览与复制\n"
    "Exec=w10charmap\n"
    "Icon=accessories-character-map\n"
    "Terminal=false\n"
    "Categories=Utility;TextEditor;\n"
    "StartupNotify=true\n")
install(FILES "${CHARMAP_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10charmap RUNTIME DESTINATION bin)

# ---- w10clock：闹钟和时钟（可选拓展 E5：世界时钟/计时器/秒表/闹钟）----
add_executable(w10clock
    clock/main.cpp
    clock/clockwindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/config.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/shell/theme/colors.cpp
)
target_include_directories(w10clock PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/shell   # theme/colors.h
)
target_link_libraries(w10clock PRIVATE
    Qt6::Widgets
    Qt6::DBus
    systemapps_appipc
)
set_target_properties(w10clock PROPERTIES AUTOMOC ON)

set(CLOCK_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/w10clock.desktop")
file(WRITE "${CLOCK_DESKTOP}"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name=闹钟和时钟\n"
    "Name[en]=Alarms & Clock\n"
    "Comment=世界时钟/计时器/秒表/闹钟\n"
    "Exec=w10clock\n"
    "Icon=appointment-new\n"
    "Terminal=false\n"
    "Categories=Utility;Clock;\n"
    "StartupNotify=true\n")
install(FILES "${CLOCK_DESKTOP}" DESTINATION share/applications)
install(TARGETS w10clock RUNTIME DESTINATION bin)

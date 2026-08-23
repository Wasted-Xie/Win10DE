# Handoff: Win10DE — Linux 上的 Windows 10 风格桌面环境

> 生成于 9 轮目标推进之后。本文档供新会话/新 Agent 无缝接管，请先读本文档 + `README.md` + `docs/ARCHITECTURE.md`，再动代码。

---

## 1. 项目概况

| 项 | 值 |
|---|---|
| 目标 | 在 Linux 上**从零实现** Win10 风格桌面环境（MVP：窗口管理、任务栏/开始菜单、桌面、托盘、锁屏） |
| 技术栈 | C++20 + **wlroots 0.19.0**（compositor）+ **Qt 6 Widgets**（shell 客户端） |
| 形态 | 双进程：`w10compositor`（wlroots compositor）+ `w10shell`（Qt layer-shell 客户端） |
| 工作区 | `C:\Projects\Win10DE`（**Windows 机器**，无编译器、无 WSL 发行版、无 Docker——所有代码**从未编译验证**） |
| API 参考 | `third_party/wlroots/` = wlroots 0.19.0 完整源码（git tag `0.19.0`），所有 API 均对照它核实 |
| 语言规则 | 用户使用简体中文交流；工作区 AGENTS.md 要求：执行命令/删改文件前需用户确认 |

**最重要的事实：全部代码未编译。** 开发在 Windows 上无任何工具链。接手后的第一要务是配置 Linux 构建环境（WSL2 或真机），编译并修复错误，跑通 headless 冒烟测试。

---

## 2. 当前状态（按里程碑）

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0 | headless compositor 骨架：display/backend/renderer/allocator/scene 生命周期、headless 输出、帧循环、**截图验证**（scene 渲染 → `wlr_buffer_begin_data_ptr_access` 读回 → stb 写 PNG → 中心像素校验） | ✅ 编码完成 |
| M1 | 多后端（`WLR_BACKEND=headless/wayland/drm`）、`wl_compositor`+subcompositor、xdg-shell（View 类）、seat（指针/键盘/光标/焦点/窗口移动缩放/剪贴板）、输出布局 | ✅ 编码完成 |
| M2a | **SSD 标题栏**：xdg-decoration 强制服务端装饰、Win10 风格装饰树（深灰标题栏 + 最小化/最大化/关闭按钮）、标题栏拖动、按钮操作 | ✅ 编码完成 |
| M3 前置 | compositor 侧 layer-shell：LayerSurface 类、**层锚树**（z 序）、arrangeLayers（复用 wlr helper）、layer surface 输入命中、键盘焦点；foreign-toplevel 服务端（窗口列表协议）；可用区缓存（最大化避开任务栏） | ✅ 编码完成 |
| M3 shell | Qt 任务栏（bottom 层：开始按钮 + 窗口列表 + 时钟）+ 开始菜单（overlay 层：.desktop 扫描 + 磁贴网格 + 启动） | ✅ 编码完成 |
| M4 | 桌面（background 层）：壁纸（`--wallpaper` 或内置 Win10 渐变）、桌面图标（QFileSystemModel + 双击打开） | ✅ 编码完成 |
| M5 | 系统托盘：SNI 宿主（纯 Qt D-Bus `org.kde.StatusNotifierWatcher`，无 KF）+ 任务栏托盘区（图标/Activate/ContextMenu） | ✅ 编码完成 |
| M6 | 锁屏：`w10lock` 进程（**ext-session-lock-v1**，0.19 已移除旧 input-inhibitor）+ compositor 锁定/解锁 + wl_shm 渲染 + 任意键解锁 | ✅ 编码完成 |
| M2b | 标题栏文字渲染（cairo/pango）与按钮 hover | ⬜ 未开始 |
| M7 | 会话集成：`w10-session` 启动器（固定 socket + 退出联动 + autostart）、compositor `--socket`、锁屏触发（Win+L / D-Bus `org.w10de.Shell.Lock()`）、配置系统（`src/ipc/config.{h,cpp}`）、**XWayland**（`wlr_xwayland` lazy + XView：map/unmap/激活/关闭/最大化/最小化/configure、scene/seat 集成、DISPLAY 注入）；**未完项**：多工作区、XView 装饰与任务栏集成 | 🔶 部分完成 |
| M7 | 会话集成（launcher/autostart/配置）、XWayland、多工作区 | ⬜ 未开始 |
| M8 | 视觉打磨（圆角/阴影/动画/Aero Snap） | ⬜ 未开始 |

**验证状态**：三轮子代理静态审查全部完成（第 1/2 轮各 4 组并行 + 失败重试，第 3 轮最终验证 4 组并行 + 输入层重拉），三轮共发现 **92 个问题**（10 CRITICAL + 15 HIGH + 32 MEDIUM + 35 LOW），全部修复或标注（5 项为可接受风险未修：XWayland/xdg 命中 z 序不统一、output 热插拔无 destroy listener、窄窗口 w<46 三按钮重叠、多输出 lock surface 只覆盖首输出、wl_seat min(7) 版本约束）。但**从未真实编译**。

---

## 3. 架构要点（接管前必须理解）

### 3.1 进程与协议

```
w10compositor（wlroots 0.19，C++ 封装）
  ├─ 多后端：headless（默认，冒烟/CI）/ wayland（嵌套调试）/ drm（真机）
  ├─ wlr_scene 场景图（官方推荐路径，自动伤害重绘）
  ├─ 协议：xdg-shell v3、xdg-decoration（强制 SSD）、layer-shell v4、
  │         foreign-toplevel-management v3
  ├─ 层锚树：scene 根按 z 序挂 5 个锚
  │         background < bottom < view < top < overlay
  │         （窗口挂 viewAnchor；层表面 map 时 reparent 到对应锚）
  └─ seat：指针/键盘/焦点/拖动/装饰交互

w10shell（Qt 6 Widgets，layer-shell 客户端）
  ├─ 任务栏：bottom 层、全宽、48px 独占区（开始按钮/窗口列表/时钟）
  ├─ 开始菜单：overlay 层、左下锚定、弹出式（.desktop 应用磁贴）
  └─ 与 compositor 通信：layer-shell（显示）+ foreign-toplevel（窗口列表）
```

### 3.2 关键类（compositor，6 文件）

| 类 | 文件 | 职责 |
|---|---|---|
| `Compositor` | server.{h,cpp} | 生命周期（display/backend/renderer/allocator/scene/各协议）、层锚树、视图/层表面列表、arrangeLayers、可用区缓存、退出码 |
| `Output` | output.{h,cpp} | 输出配置（enable+custom mode+scale）、背景矩形（backgroundAnchor）、帧循环、截图验证（PNG+像素校验） |
| `View` | view.{h,cpp} | 一个 xdg-toplevel 窗口：scene 节点、SSD 装饰树、最大化/最小化/移动/关闭状态机、foreign-toplevel handle 同步 |
| `Seat` | seat.{h,cpp} | 指针/键盘/焦点/拖动状态机、标题栏按钮交互、layer surface 命中（surfaceAt 按 z 序）、剪贴板 |
| `LayerSurface` | layer_shell.{h,cpp} | 一个 layer surface：scene 节点、arrange（**复用 `wlr_scene_layer_surface_v1_configure` helper**）、命中检测 |
| `main.cpp` | — | 参数解析（--width/--height/--frames/--screenshot/--verbose）、strtol 严格校验 |

### 3.3 关键类（shell，10 文件）

| 类/文件 | 职责 |
|---|---|
| `TaskbarWindow` | 任务栏主窗口：开始按钮 + 窗口列表区 + 时钟；持有 ForeignToplevelManager |
| `StartButton` | Win10 扁平开始按钮（hover/pressed 高亮，发 `startMenuRequested`） |
| `Clock` | 时钟（HH:mm + 日期，QTimer 1s 刷新） |
| `TaskbarButton` | 窗口项按钮：标题显示、激活高亮（强调蓝）、点击 `activate()` |
| `ForeignToplevelManager/Handle` | foreign-toplevel **客户端自写绑定**（wayland-scanner 生成协议代码 + Qt 封装）：窗口列表模型/操作 |
| `StartMenu` | 开始菜单：磁贴网格（QListWidget IconMode）、单击启动（QProcess 分离式）、Esc 隐藏 |
| `appmodel.{h,cpp}` | .desktop 扫描解析（/usr/share/applications 等 3 目录、Name/Icon/Exec、NoDisplay 过滤） |
| `DesktopWindow` | 桌面（background 层）：壁纸（--wallpaper 或内置 Win10 渐变）、桌面图标（QFileSystemModel + 双击打开） |
| `SniWatcher` | SNI watcher D-Bus 服务（org.kde.StatusNotifierWatcher，纯 Qt，无 KF）；注册项跟踪 + 信号 |
| `TrayIcon` | 单个托盘图标：IconName/IconPixmap 读取、左键 Activate、右键 ContextMenu、PropertiesChanged 刷新 |
| `TrayArea` | 任务栏托盘区：收集全部 SNI item 显示图标（时钟左侧） |
| `theme/colors.h` | Win10 颜色常量（任务栏 #2D2D2D、强调蓝 #0078D7 等）+ kTaskbarHeight=48 |

### 3.4 数据流要点

- **窗口列表**：compositor 侧 `View::createForeignToplevel()`（构造时）创建 handle，`setActivated/setMaximized/setMinimized/title/app_id` 变化同步 → shell 侧 `ForeignToplevelManager` 收到 toplevel 事件 → `TaskbarButton` 显示；任务栏点击 → `handle->activate()`（**传 "seat0"**，wlr 按名匹配 seat）→ compositor `request_activate` → focus+raise+恢复最小化。
- **最大化**：`View::setMaximized` 用 `compositor.outputUsableSize()`（可用区 = 输出减任务栏独占区）；**map 前请求最大化**（启动即最大化）由 `handleMap` 首次定位后应用几何。
- **输入命中**：`Seat::surfaceAt` 按 z 序 overlay → top → 窗口内容 → bottom → background；窗口装饰区归合成器（按钮/拖动），层表面 `keyboard_interactive != NONE` 时转移键盘焦点。
- **层表面配置时机**：`new_surface` 时**不能** arrange（`initialized==false` 触发 wlroots assert）；首次 commit 的 `handleCommit` 才 arrange；`arrangeLayers` 循环必须跳过 `!initialized || !mapped` 的表面。

---

## 4. 关键决策与原因（防止后来者推翻）

| 决策 | 原因 |
|---|---|
| wlroots 0.19 + C++20 | 用户选定 Qt/C++；wlroots 生态成熟、layer-shell/foreign-toplevel 等协议现成；**锁定 0.19**（API 变动频繁，追 master 风险高） |
| Wayland 直接上（不 X11 过渡） | 用户明确选择；X11 会走回头路 |
| compositor 与 shell 进程分离 | 崩溃隔离、协议先行；UI 全部走 layer-shell |
| **层锚树**（5 锚按 z 序） | scene 后建者在上面；窗口 raise 只在 viewAnchor 内，不会盖过任务栏/开始菜单 |
| **复用 `wlr_scene_layer_surface_v1_configure`** | wlroots 自带完整几何计算（锚点/边距/独占区/configure），少造轮子；自写算法有协议偏差 |
| **layer-shell 绑定用 KDE layer-shell-qt** | Qt 6.5+ 原生 `QWaylandLayerShell` API 文档不足、不稳定；layer-shell-qt 是 Plasma 生产方案 |
| **foreign-toplevel 客户端自写**（wayland-scanner 生成） | Qt 原生 QWaylandForeignToplevelManager API 不稳；协议简单（v3），自写可控 |
| SSD 服务端装饰 | Win10 统一视觉；xdg-decoration 强制 SERVER_SIDE |
| wlr_scene 场景图 | wlroots 官方推荐，自动 damage 追踪 |
| 截图方案：`wlr_scene_output_build_state` → `begin_data_ptr_access` → stb PNG | 0.19 移除了 renderer `read_pixels` 与 `wlr_drm_format_create`；此路径不手动建 buffer |
| `W10DE_BUILD_SHELL` option（默认 ON） | 无 Qt 环境时可只构建 compositor（M0-M3 验证不受 Qt 依赖阻塞） |
| 所有 wlroots 头文件 `extern "C"` 包裹 + `WLR_USE_UNSTABLE` | wlroots 头文件无 extern "C" 保护；不稳定接口需宏 |
| 可用区缓存（usableAreas_） | 最大化窗口避开任务栏独占区（Win10 行为） |
| 锁屏用 **ext-session-lock-v1**（`wlr_session_lock_v1`） | **wlroots 0.19 移除了旧 `wlr_input_inhibit_v1`**（调研确认，0.19 只有 idle-inhibit/keyboard-shortcuts-inhibit）；ext-session-lock 是现代标准（swaylock 同款），合成器强制锁定（锁屏进程崩溃不自动解锁）；协议 XML 已 vendored 到 `protocols/` |
| 锁屏渲染用 **wl_shm + QPainter 离屏**（w10lock 独立进程） | 锁定时合成器隐藏所有其他客户端（含 shell），锁屏必须自渲染自输入；QGuiApplication 提供 display，QImage(Format_ARGB32) 字节序与 WL_SHM_FORMAT_ARGB8888 一致可直拷 |
| **上游依赖合规**：Qt / layer-shell-qt 仅**动态链接**；vendored（wlroots/stb/ext-session-lock.xml）原样保留版权声明；README 有"依赖与许可证"章节 | 遵守上游许可（LGPL 动态链接义务）；唯一 GPL 项 hwdata 为数据文件、D-Bus 为独立进程，均不构成链接；本项目可自由选协议 |
| **README 顶部免责声明**："Not affiliated with Microsoft"（中英双语） | "Windows" 为微软商标；仿 Win10 界面需声明独立非官方，避免误导/商标纠纷 |

---

## 5. 已探索但放弃的路径（避免重复尝试）

| 路径 | 放弃原因 |
|---|---|
| X11 (XCB) 先行 | 用户选 Wayland；X11 过渡会重写 |
| QtWaylandCompositor | 文档少、坑多，生态不如 wlroots |
| Qt 原生 `QWaylandLayerShell` | Qt 6.5+ 提供但 API 无公开文档、不稳定 |
| 自写 layer-shell 客户端绑定 | 与 Qt 渲染管线（QPA）集成困难，不可行 |
| Qt 原生 QWaylandForeignToplevelManager | API 不稳，协议简单故自写 |
| 自写 layer surface 几何算法 | wlroots 自带 helper 更符合协议 |
| 手动构造 `wlr_drm_format` 建 buffer | 0.19 移除 create API；改用 scene build_state 路径 |
| `wlr_scene_attach_output_layout` 后不显式 add_output | 多输出位置不同步（**已修复**：需 `wlr_scene_output_layout_add_output`） |

---

## 6. 文件清单与职责（全部）

```
CMakeLists.txt                      # 顶层：pkg 依赖（wlroots-0.19 回退 wlroots）、
                                    #   W10DE_BUILD_SHELL option、Qt6 Widgets+WaylandClient、
                                    #   LayerShellQt
README.md                           # 状态、构建、冒烟验证、待验证项、依赖与许可证
DEPENDENCIES-LICENSES.md             # 上游依赖许可证清单（合规核实，10 项基础清单）
docs/ARCHITECTURE.md                # 架构设计文档（决策表/里程碑/风险）
docs/HANDOFF.md                     # 本文档
.gitignore

src/compositor/
  server.{h,cpp}                    # Compositor：生命周期/协议/层锚/arrange/可用区
  output.{h,cpp}                    # Output：配置/背景/帧循环/截图验证
  view.{h,cpp}                      # View：窗口 + SSD 装饰 + foreign-toplevel 同步
  xview.{h,cpp}                     # XView：XWayland 窗口（M7，无装饰基础版）
  seat.{h,cpp}                      # Seat：输入/焦点/拖动/装饰交互/层表面命中
  layer_shell.{h,cpp}               # LayerSurface：层表面管理/命中
  main.cpp                          # 参数解析
  CMakeLists.txt                    # WLR_USE_UNSTABLE、PkgConfig::WLROOTS/DRM/XKBCOMMON

src/shell/
  main.cpp                          # 入口：layer-shell 配置（桌面/任务栏/开始菜单）+ --wallpaper
  theme/colors.h                    # Win10 颜色常量
  desktop/desktopwindow.{h,cpp}     # 桌面：壁纸（内置渐变/指定图片）+ 图标列表（M4）
  taskbar/taskbarwindow.{h,cpp}     # 任务栏主窗口 + 窗口列表管理
  taskbar/startbutton.{h,cpp}       # 开始按钮
  taskbar/clock.{h,cpp}             # 时钟
  taskbar/taskbarbutton.{h,cpp}     # 窗口项按钮
  ipc/foreigntoplevel.{h,cpp}       # foreign-toplevel 客户端绑定（Qt 封装）
  startmenu/appmodel.{h,cpp}        # .desktop 扫描
  startmenu/startmenu.{h,cpp}       # 开始菜单
  tray/sniwatcher.{h,cpp}           # SNI watcher 宿主（D-Bus 服务，M5）
  tray/trayicon.{h,cpp}             # 单个托盘图标（属性读取/Activate/ContextMenu）
  tray/trayarea.{h,cpp}             # 任务栏托盘区（图标集合管理）
  CMakeLists.txt                    # wayland-scanner 生成协议代码、AUTOMOC、Qt 链接

src/lock/
  main.cpp                          # w10lock 入口：QGuiApplication + QPainter 离屏时钟 + 任意键解锁
  lockclient.{h,cpp}                # ext-session-lock 客户端绑定（wayland-scanner 生成）+ wl_shm 渲染
  CMakeLists.txt                    # wayland-scanner 生成 session-lock 协议代码

protocols/
  ext-session-lock-v1.xml           # 锁屏协议（wayland-protocols staging，已 vendored）

src/session/
  w10-session                       # 会话启动器（bash）：compositor→socket 就绪→shell；退出联动
  w10de.conf.example                # 配置示例（安装到 share/w10de）

src/ipc/
  config.{h,cpp}                    # 无依赖 INI 解析器（compositor/shell 共用，M7 配置系统）

third_party/
  wlroots/                          # wlroots 0.19.0 完整源码（API 参考，勿改）
  stb/stb_image_write.h             # PNG 输出（compositor 截图用）
```

---

## 7. 已知问题与待验证项（按优先级）

### 7.1 编译验证（最高优先）
1. **全部代码未编译**。预期错误：`[static 4]` C99 数组参数语法在 C++ 的接受度（wlroots 0.19 被 wayfire 等 C++ 项目使用，大概率按扩展接受，需确认）、Qt/layer-shell-qt API 细节。
2. **LayerShellQt CMake 集成**：`find_package(LayerShellQt REQUIRED)` 包名与 target 名可能为 `LayerShellQt6`/`LayerShellQt::LayerShellQt`（实现时按发行版确认）。
3. **layer-shell-qt 配置时序**：部分版本需 `show → 配置 → hide → show` 序列（代码已注释标注）。
4. **枚举名**：`LayerShellQt::Window::Layer/Anchors/KeyboardInteractivity*` 具体枚举名待编译确认。

### 7.2 行为待验证
- headless 冒烟：`w10compositor --frames 5 --screenshot /tmp/x.png` 应输出 `pixel verification passed`（背景 #0078D7 纯色 1920x1080）。
- 嵌套验证（wayland backend + weston/WSLg）：开窗口、拖动、标题栏按钮、任务栏窗口列表、开始菜单。

### 7.3 已知简化（M3 阶段刻意为之）
- 开始菜单无搜索框、无固定磁贴（仅 .desktop 列表网格）。
- 单键盘设备（多键盘热插拔被忽略）、无触摸/数位板。
- foreign-toplevel `activate()` 硬编码 seat 名 "seat0"。
- fullscreen 按最大化处理；无工作区；无 XWayland。
- SNI 托盘：`IconPixmap` 的 QDBusArgument 提取与 ARGB32 字节序（小端假设）待真机验证；`IconName` 主题查找失败时图标可能不显示；中键 SecondaryActivate 未连接。
- 锁屏（M6）：任意键解锁（无密码验证）；单锁屏 surface（首个输出）；`wl_shm` 客户端渲染（shm_open/mmap/QImage 字节序）待真机验证；`wl_seat` 的 capabilities 事件已处理（键盘绑定），但 seat name 事件忽略；锁屏进程崩溃时合成器自动解锁（ext-session-lock 语义，属预期）。

---

## 8. 构建与测试指引

```bash
# 依赖（Debian/Ubuntu）：libwlroots-dev(0.19, 可能需 sid/源码)、wlr-protocols、
#   libdrm-dev、libwayland-dev、libxkbcommon-dev、qt6-base-dev、liblayer-shell-qt6-dev、
#   wayland-scanner、ninja、cmake、g++
# 构建
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
# headless 冒烟
./build/src/compositor/w10compositor --frames 5 --screenshot /tmp/w10de-m0.png
# 嵌套运行（交互验证）
WLR_BACKEND=wayland ./build/src/compositor/w10compositor --frames 0
# 同一 WAYLAND_DISPLAY 下运行 w10shell 验证任务栏/开始菜单
./build/src/shell/w10shell
# 快捷键：Win+Q 关闭窗口 / Win+M 最小化 / Win+F 最大化 / Win+Esc 退出
```

---

## 9. 下一步计划（按优先级）

1. **配置构建环境并编译**（最大优先）：WSL2 装发行版（需用户确认）或真机；安装依赖（wlroots 0.19 可能需源码编译——`third_party/wlroots` 已备好）；修复编译错误。
2. **headless 冒烟**：截图验证 + 像素校验通过（M0 验收标准）。
3. **嵌套验证 M1-M3**：开窗口/标题栏/任务栏/开始菜单交互。
4. **M7 收尾**：多工作区；XView 补齐（SSD 装饰、foreign-toplevel 任务栏集成、拖动）。
5. **M2b**：标题栏文字（cairo/pango → `wlr_texture_from_pixels` 路径已确认存在；注意 0.19 **无 `wlr_buffer_from_texture`**，scene 显示需另找 buffer 来源，实现时调研）。
6. **M8 视觉打磨**：圆角/阴影/动画/Aero Snap；锁屏接入密码验证（PAM）。

---

## 10. 交接注意事项

- **用户偏好**：简体中文交流；AGENTS.md 要求执行命令/文件变更前经用户确认（交接后继续遵守）；回答结构化、准确性优先、不伪造数据。
- **goal 状态**：`goal-eed9bce7-0358-4504-a2ed-cf597ebe73fd` 处于 **paused**（用户暂停等待交接）。接手续跑时用 `update_goal action=resume` 恢复（需用户指示继续）。
- **wlroots 头文件无 extern "C" 保护**：C++ 引用必须手动包裹；不稳定接口需要 `WLR_USE_UNSTABLE`。
- **先读**：本文档 → `docs/ARCHITECTURE.md` → `README.md` → 相关源码，再改代码。
- **不要**重新核对已确认的 API（见第 4 节决策表与源码注释中的"已确认"标注）；新增 wlr_* 调用时对照 `third_party/wlroots/include/`。
- **维护规则（用户明确要求）**：**每次完成任务/里程碑时，更新 `README.md` 的同时必须同步更新本文档**（状态表、文件清单、决策、已知问题、下一步）。本文档不是一次性的——它随项目演进持续维护，任何"进行中"状态必须在每次交接时准确反映。若 README 有变更而本文档未同步，视为交接不完整。

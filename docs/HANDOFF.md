# Handoff: Win10DE — Linux 上的 Windows 10 风格桌面环境

> 生成于 9 轮目标推进之后。本文档供新会话/新 Agent 无缝接管，请先读本文档 + `README.md` + `docs/ARCHITECTURE.md`，再动代码。

---

## 1. 项目概况

| 项 | 值 |
|---|---|
| 目标 | 在 Linux 上**从零实现** Win10 风格桌面环境（MVP：窗口管理、任务栏/开始菜单、桌面、托盘、锁屏） |
| 技术栈 | C++20 + **wlroots 0.19.0**（compositor）+ **Qt 6 Widgets**（shell 客户端） |
| 形态 | 双进程：`w10compositor`（wlroots compositor）+ `w10shell`（Qt layer-shell 客户端） |
| 工作区 | `C:\Projects\Win10DE`（Windows 机器，开发用）；**WSL2 Arch（/root/win10de）为编译/验证环境**（2026-08 起，全部代码已在此编译验证） |
| API 参考 | `third_party/wlroots/` = wlroots 0.19.0 完整源码（git tag `0.19.0`），所有 API 均对照它核实 |
| 语言规则 | 用户使用简体中文交流；工作区 AGENTS.md 要求：执行命令/删改文件前需用户确认（审查流程豁免过） |

**开发流**：Windows 侧编辑 → 复制到 WSL（`/mnt/c/Projects/Win10DE` → `/root/win10de`）→ WSL 编译 + headless 截图/像素验证。WSL 仓库无 git，靠文件复制同步。

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
| M2b | 标题栏文字渲染（cairo/pango → 自实现 wlr_buffer → scene buffer）与按钮 hover 打磨 | ✅ 完成（2026-08 headless 验证通过） |
| M7 | 会话集成：`w10-session` 启动器（固定 socket + 退出联动 + autostart）、compositor `--socket`、锁屏触发（Win+L / D-Bus `org.w10de.Shell.Lock()`）、配置系统（`src/ipc/config.{h,cpp}`）、**XWayland**（`wlr_xwayland` lazy + XView：map/unmap/激活/关闭/最大化/最小化/configure、scene/seat 集成、DISPLAY 注入） | ✅ 完成 |
| M7 续 | **多工作区**（`View/XView::workspace_` 归属、`switchWorkspace`/`moveViewToWorkspace`、统一 `applyVisibility`、命中过滤）+ **XWayland SSD 装饰与任务栏集成**（XView 同款标题栏/按钮/文字/阴影 + foreign-toplevel handle + 拖动交互 + override-redirect 处理 + set_class→app_id） | ✅ 完成（多工作区 4 场景 headless 验证；XWayland 因 WSL 无 X11 仅静态审查+编译） |
| M8 | 视觉打磨：窗口阴影（自绘 ARGB 渐变 buffer）、Aero Snap（Win+←/→ 半屏 / ↑ 最大化 / ↓ 还原 + 平滑动画）、窗口移动动画；圆角遵循 Win10 直角设计（UI 元素 Qt 侧 2px 圆角） | ✅ 完成（阴影/Snap headless 验证通过） |
| 主题 | **主题功能 + 浅色模式 + 自定义通道**：`src/ipc/theme.{h,cpp}`（共享主题定义）—`[theme]` 段 `mode=dark/light` 预设 + 14 颜色键覆盖；compositor（标题栏/按钮/文字/背景）与 w10shell（任务栏/开始菜单/时钟）读同一配置 | ✅ 完成（深色回归/浅色/自定义三态验证通过） |
| 系统应用 | **通用接口框架**（`docs/SYSTEMAPPS.md`：独立二进制 + D-Bus 单实例激活 `org.w10de.Apps.<Name>`/Activate(s path)，`src/systemapps/appipc.{h,cpp}` 供后续应用复用）+ **w10explorer 文件资源管理器**（文件操作对标 Windows）+ **w10settings 设置中心**（KDE 风格，主题/壁纸/关于/开机自启） | ✅ 完成（explorer selftest 8 项 + settings 配置读写 selftest + 双应用 headless 渲染 + 单实例 D-Bus 激活验证通过） |
| 功能补全（goal cd47bf3e） | **第一批：Alt+Tab 切换器** ✅（`src/compositor/alttab.{h,cpp}`：scene 层 UI、候选=当前工作区可见窗口（xdg+XView）、强调色高亮、Seat 集成 Alt+Tab/Shift+Tab/Alt 释放、`--alttab-test` 帧钩子 headless 验证 PASS）；**全局搜索** ✅（开始菜单顶部搜索框：应用过滤 + 主目录文件搜索（QDirIterator 数量上限）、混合结果、文件 systemd 默认打开、磁贴区隐藏、聚焦即输入；渲染验证 PASS）；**通知中心** ✅（`org.freedesktop.Notifications` 标准 D-Bus 服务 + 右下角弹窗 360×100（5 秒自动隐藏、点击打开历史）+ 通知历史中心 380×480（Esc 关闭）；gdbus 触发 + headless 渲染验证 PASS：card 6314/文字 1214 像素）；**剪贴板历史** ✅（Win+V 语义：`ClipboardHistory` 监听系统剪贴板（文本/图片、连续去重、上限 20）+ overlay 历史面板 360px（点击写回剪贴板、Esc 关闭、空态占位）+ Win+V 快捷键（compositor fork dbus-send → `org.w10de.Clipboard.ToggleClipboardHistory`）；`--clipboard-selftest` 逻辑自测 + 面板渲染验证 PASS：card 33831/文字 73）；**终端 w10term** ✅（`src/systemapps/term/`：forkpty + **非阻塞 master fd**（阻塞 fd 会卡死 Qt 事件循环——gdb attach 实测）+ QSocketNotifier 读 + ANSI 子集解析（SGR 16 色/清屏/退格；光标移动忽略）+ 按键转发 + Ctrl+Shift+C/V 本地复制粘贴 + appipc 单实例；`--selftest`（pty 回读 + ANSI 提取单测）+ 渲染 PASS：文字 1282/背景 34354 像素 + 单实例 D-Bus 激活 PASS）——**第一批 5 项全部完成**；**第二批：显示设置** ✅（`src/compositor/dbus_service.{h,cpp}`：org.w10de.Compositor/Outputs——GetOutputs/GetModes/SetMode/SetScale/SetPosition，libdbus 共享连接（只 unref 不 close）+ dbus fd 挂 wl_event_loop + wlroots 0.19 state API 热应用；w10settings"显示"模块：输出/分辨率/缩放下拉 + 应用/刷新 + `--page display`；IPC 热应用验证 PASS：SetScale 200→GetOutputs 960x540、SetMode 1280x720→640x360；显示页渲染 PASS）→ **快捷键配置化** ✅（`src/ipc/shortcuts.{h,cpp}`：[shortcuts] 配置段解析 "win+q"/"ctrl+alt+l"（修饰键 win/ctrl/shift/alt + 键名 a-z/0-9/方向/F 键等）+ 14 动作绑定（close/maximize/minimize/snap×4/lock/quit/clipboard/workspace_1-4；Alt+Tab 语义特殊保持固定）；processKey 查表分发——`(mods & 0x4F) == b.mods` 修饰组合精确匹配（支持 ctrl/alt/shift，原 pureLogo 特化为通用）；dispatchShortcut 动作实现与原硬编码一致；`--shortcuts-dump` 打印生效绑定）→ 电源/音频/默认应用；第三批生态 | 按 KDE 差距分析优先级推进 |

**验证状态**：多轮子代理静态审查全部完成（前 3 轮共 92 问题全部修复或标注；M2b/M7续/M8 又 3 轮审查并修复，见 README「M2b / M7 续 / M8 开发与验证」节）。**2026-08 完成真实编译 + headless 冒烟 + 完整渲染验证**（WSL2 Arch：vendored wlroots 0.19 源码编译 + 各二进制构建成功；`--frames 5` 截图像素校验通过；compositor+w10shell 同跑验证桌面壁纸渐变 + 任务栏渲染成功）。**M2b/M7续/M8 专项验证**（2026-08）：标题栏白字/关闭钮像素、多工作区 4 场景、窄窗口文字清空、窗口阴影、Aero Snap 贴边全部 headless 截图/像素验证通过。XWayland 运行时验证待真机（WSL 无 X11）。

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
| `StartMenu` | 开始菜单：三列布局（侧栏/应用列表/磁贴）+ **顶部搜索框（应用过滤 + 主目录文件搜索）**、单击启动（QProcess 分离式）、Esc 隐藏 |
| `appmodel.{h,cpp}` | .desktop 扫描解析（/usr/share/applications 等 3 目录、Name/Icon/Exec、NoDisplay 过滤） |
| `ipc/notificationservice.{h,cpp}` | **通知 D-Bus 服务**（org.freedesktop.Notifications：Notify/CloseNotification/GetCapabilities/GetServerInformation，历史 ≤50 条，notificationReceived 信号） |
| `ipc/clipboardservice.{h,cpp}` | **剪贴板面板 D-Bus 服务**（org.w10de.Clipboard.ToggleClipboardHistory → toggleRequested 信号，随 /Shell 同服务注册 /Clipboard 对象） |
| `notification/notificationpopup.{h,cpp}` | **通知弹窗**（overlay 右下角固定 360×100：app/摘要/正文三行，paintEvent 自绘背景，点击发 clicked，showNotification 更新内容） |
| `notification/notificationcenter.{h,cpp}` | **通知历史中心**（overlay 380×480：历史倒序列表，refresh(history) 刷新，Esc 关闭） |
| `clipboard/clipboardhistory.{h,cpp}` | **剪贴板历史模型**（监听 QClipboard::dataChanged，文本/图片、与最近一条去重、上限 20，addText/addImage 供测试种子与写回路径） |
| `clipboard/clipboardpanel.{h,cpp}` | **剪贴板历史面板**（overlay 右缘 360px：文本单行预览/图片缩略图、点击 entryPicked、Esc 关闭、空态占位、高度随条目数自适应） |
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
| **项目协议：MIT**（根目录 LICENSE，版权人 Wasted-Xie，GitHub 仓库创建时添加） | 与全部上游（MIT/Public Domain/LGPL 动态链接）兼容；copyleft 无传染；社区零摩擦 |
| **vendored wlroots 头 C++ 补丁**（4 处，首编实测）：`wlr_scene.h` `[static 4]`→`[4]`×2（g++16 拒绝 C99 语法）、`xwayland.h` `class`→`class_`、`wlr_layer_shell_v1.h` `namespace`→`namespace_` + 生成协议头同步 | wlroots 头按 C 设计，C++ 关键字/语法不兼容；补丁仅改头不改实现，行为等价；**重新编译/安装 wlroots 时必须再次应用**（README 编译记录） |
| **`W10DE_CONTAINER_OF` 宏**（`src/compositor/util.h`）替代裸 `wl_container_of` | C++ 中 `auto* x = wl_container_of(l, x, m)` 自引用报 "use before deduction of auto"（首编实测 57 处）；宏用 `static_cast<Type*>(nullptr)` 提供显式类型 |
| **wlr 头 `extern "C"` 包裹**（6 个头文件） | wlroots 头无 C++ 保护，C++ 解析头需手动包裹（server.cpp 原本已包） |
| **XWayland 创建失败降级为警告**（不致命） | headless/WSL 无 X11 环境（`/tmp/.X11-unix` 只读挂载）下应继续运行；X11 客户端兼容缺失可接受 |
| **headless 帧循环：每帧 `wlr_output_schedule_frame`** | headless 后端 frame timer 仅在 enable commit 时续期，不显式调度渲染停在第二帧（首编实测） |
| **xdg-decoration 延迟 `set_mode`**（surface 未初始化时挂 commit 监听，单槽串行） | `wlr_xdg_toplevel_decoration_v1_set_mode` 无条件调 `schedule_configure`，surface 未初始化（客户端首 commit 前）断言崩溃——Qt 即此时序（渲染验证 gdb 定位） |
| **`arrangeLayers` 仅按 `!initialized` 过滤**（不按 mapped） | 未 map 表面需要首次 configure 才能 map；按 mapped 过滤会死锁（Qt 层表面永远等不到 configure，渲染验证实测） |
| **layer-shell-qt 时序：`winId() → get/配置 → show()`** | Qt 6.11 QPA 不允许 show 后切换 shell integration（"already has a shell integration"）；`useLayerShell()` 在 Qt 6.5+ 为废弃 no-op |
| **截图校验：内容多样性检测**（多色即通过，纯色校验背景） | M0 的中心==纯背景假设在有 shell 内容时误判（壁纸渐变覆盖中心，实测 center=#0073CD） |
| **wlroots 获取策略（跨发行版）**：`cmake/WlrootsCompat.cmake`——系统 0.19 头 C++ 兼容直接使用；头为上游原版则构建目录生成补丁副本以 -I 优先注入；缺失/版本不符自动 meson 编译 vendored；`-DW10DE_WLROOTS_STRATEGY=auto/system/vendored` | 上游 wlroots 头有 C99 `[static N]` 与 C++ 保留字（class/namespace），原版头在 C++ 下编译失败（g++16 实测）；策略闭环覆盖 Arch（0.19 包原版头/0.20）/Debian trixie（0.18）/Ubuntu（0.17）/Fedora 43（0.19.3） |
| **开始菜单 `margin.bottom=0`**（overlay 层） | overlay 层 bounds 是可用区（已排除任务栏独占区），再设底边距会双重避让——实测菜单与任务栏间 49px 空隙，改为 0 后 1px |
| **开始按钮 = 发行版 logo**（`/usr/share/pixmaps/archlinux-logo.svg`，缺失回退文字） | 系统发行版图标替代"开始"文字（Arch 蓝 #1793D1 实测渲染）；按钮 48×48 正方形（1:1 与任务栏同高）贴左（x=0 实测），图标保持原本 26×26 固定、按钮内居中 |
| **开始菜单 Win10 布局**：三列——左侧 48px 窄栏（#171717：☰ 汉堡展开/折叠 200px、底部功能区账户→设置/文档/图片→电源最底，电源弹关机/重启/睡眠菜单与侧栏等宽 MVP 占位）、应用列表列 240px（5×按钮宽）、磁贴区 288px（6×按钮宽），总宽 576 | 与 Win10 UI 对齐（渲染验证 x=48/576 分区边界精确、三列 74/129/197 色）；账户/设置动作与真实关机为后续里程碑（PAM/systemctl） |
| **电源/账户接线**：电源菜单执行 systemctl（poweroff/reboot/suspend）；账户按钮 → D-Bus org.w10de.Shell.Lock → w10lock（首次端到端验证：busctl 调用→w10lock→compositor session locked） | **LockService 需 Q_CLASSINFO("D-Bus Interface","org.w10de.Shell")**（默认接口名是类名，外部调用不到——实测）；w10lock 定位 PATH 优先 + /usr/local/bin 兜底 |
| **磁贴四种尺寸**（`TileButton`+`FlowLayout`，右键菜单自由设置）：小 48×48 / 中 **100×100**（默认）/ 大 **204×204** / 宽 **204×100**；磁贴区 **316px**（6 小磁贴+7×4 间隙，边缘 4px）、水平/行间距 4px、每行 3 个中磁贴（实测 100px、4px 间隙/边缘精确） | Win10 磁贴大小，4px 网格基准（中=2 小+1 间隙、大=4 小+3 间隙）；**FlowLayout 宽度取父 widget 实际宽度**（setGeometry rect 在 layer-shell 时序中不稳——真实运行验证）；尺寸变化触发重排 |
| **M2b 标题文字：cairo/pango → 自实现 `wlr_buffer`**（`wlr_buffer_init` + DATA_PTR access → `wlr_scene_buffer` 自动上传纹理） | 0.19 **无 `wlr_buffer_from_texture`**；scene buffer 渲染时自动上传，无需手动建 texture；cairo 1.18/pango 1.58 已在 WSL 环境；ARGB32 预乘与 DRM_FORMAT_ARGB8888 字节序一致 |
| **xdg-shell 初始 configure 由 compositor 主动回复**（首次 commit 的 `initial_commit` → `wlr_xdg_toplevel_set_size(0,0)`） | wlroots 0.19 `create_xdg_toplevel` **不自动调度初始 configure**（对照源码核实：schedule_configure 只在 set_* API 与 popup 路径）——不回复则客户端永久卡在等 configure、窗口永不 map（真实运行定位） |
| **标题文字/阴影自绘 buffer 的引用计数纪律**：`set_buffer`（scene lock+unlock）→ 旧引用 `wlr_buffer_drop`；析构先销毁 scene 节点再 drop；空标题/窄窗口 `set_buffer(NULL)` 清空 | 防泄漏/双重释放（子代理逐行核对 vendored buffer.c 引用模型）；文字节点 z 序在按钮之下（背景→文字→按钮，先创建者在下） |
| **M7 续 多工作区**：`workspace_` 归属 + 统一 `applyVisibility()`（mapped && !minimized && workspace==current）；`switchWorkspace` 刷新可见性+hover+焦点；`focusWorkspaceTop` 只更新焦点不重排 z 序；命中检测过滤非当前工作区 | Win10 虚拟桌面语义；可见性单一入口避免 set_enabled 分散；切换不重排堆叠（审查 #16） |
| **跨类型焦点统一**（xdg + XWayland）：`focusView/focusSurface/unfocusAll` 全路径同步失活另一类型窗口；XView `activate(true)` 先 focusSurface（统一失活）再激活自身 | 审查 #2：焦点/激活必须唯一，否则输入进入隐藏窗口或双激活 |
| **XWayland override-redirect 窗口不加装饰/任务栏条目**（X11 菜单、工具提示） | 审查 #8：此类窗口自由定位，无 WM 标题栏语义 |
| **M8 窗口阴影：自绘 ARGB8888 预乘渐变环**（切比雪夫距离线性衰减，内缘 α≈0.36→外缘 0；内部区域 α=0）挂在装饰树最底层 | Win10 窗口阴影观感；纯内存填充无额外依赖；内部必须 α=0（否则整窗被遮罩压暗——首版实测 bug） |
| **M8 Aero Snap**：Win+←/→ 半屏（保存 restore 几何、可还原）、Win+↑ 最大化、Win+↓ 还原；最大化与贴边互斥（进入一方先退出另一方） | Win10 Snap 语义；restore 几何复用最大化机制 |
| **M8 动画：帧插值 ease-out**（每帧 +0.12，`tickAnimations` 由输出帧循环驱动；拖动开始 `cancelAnimation`） | Snap/还原平滑移动；与用户操作无竞态（拖动即取消）；headless 用 `--snap-test` 验证最终位置 |
| **M8 审查修复**：snap→最大化保留 restore 几何 + 取消动画（不调 unsnap，防返回动画拉回/恢复点被半屏值覆盖）；maximized→snap 先拷出恢复目标再落回（resize 异步竞态）；XView OR 切换全量置 null 装饰节点（shadowNode_ 悬垂 UAF）；beginResize 补 cancelAnimation；快捷键纯 LOGO 组合；多输出动画仅第一输出推进；unmap/dissociate 取消动画 | 子代理审查 S1/S2 严重 + M1/M2 中等 + 12 轻微，全部修复后回归通过 |
| **主题系统**：`src/ipc/theme.{h,cpp}`（纯 C++ 共享定义：Theme 结构 + 深/浅预设 + `loadTheme(Config)` + `parseColor`）；compositor（`theme()` 访问器：标题栏/按钮/hover/标题文字/桌面背景/截图校验期望色）与 w10shell（`theme::loadFromConfig` + `colors.cpp` 全局主题，colors.h 常量改为访问器函数）读同一 `~/.config/w10de/config.ini` 的 `[theme]` 段 | mode=dark（默认，值不变）/light（浅灰任务栏标题栏 + 深字）/自定义键覆盖三态共用一套机制，双进程视觉一致；**自定义通道 = `[theme]` 段任意 `#RRGGBB` 键覆盖预设**（15 键含 menu_sidebar/accent_text，见 `w10de.conf.example`） |
| **主题审查修复**（AgentTeams t1-t3：compositor 核心/shell 接入/交叉一致性）：空配置回退深色预设；shell 加 `--config` 对齐 compositor（主题/壁纸路径不分叉）；新增 `accent_text` 键（激活高亮固定白字）；桌面图标区主题化；浅色菜单 #F0F0F0 与磁贴可辨 | 5 中等 + 若干轻微修复后四组验证（深色/浅色/自定义/完整渲染）全部 PASS |
| **依赖从源码编译（兼容性）**：`cmake/DepSource.cmake` 通用机制（系统 pkg-config 优先 → 缺失/版本不符时固定 URL 下载 + meson 编译到 `build/_deps/prefix` + PKG_CONFIG_PATH 注入 + stamp 增量 + 下载缓存）+ `cmake/DepsCompat.cmake` 依赖编排（wlroots 生态 15 项：wayland(-protocols)/libdrm/pixman/libxkbcommon/libdisplay-info/libliftoff/libseat/libevdev/libinput/hwdata/mesa/cairo/pango） | `W10DE_DEP_SOURCE=auto/always/never` + `W10DE_FORCE_SOURCE_DEPS` 验证（5 依赖 wayland-protocols/wayland/libdrm/pixman/libxkbcommon 全部源码构建成功并链接回归通过）；版本比较为 ≥（点分段数值）；下载优先系统 curl（CMake 内置 TLS 在 WSL 报 SSL connect error 实测）；**多源 + 延迟筛选 + 魔数校验**（URL 分号列表逐源测延迟 `--range 0-0` 选优；gitlab 对不存在 ref 返回 200+HTML，下载后校验 gzip/xz/bz2/tar/zip 魔数拒绝错误页）；原独立 release 域名（dri/cairographics/xkbcommon/mesa/gnome）在部分网络不可达，已统一改 gitlab archive（tag 带项目前缀）；GitHub 官方 mirror + gh-proxy 为 wayland/wayland-protocols/mesa/pango/hwdata 加镜像；**fdo 无国内镜像站**（TUNA/USTC/阿里实测）；libudev 假定系统；Qt6 源码编译留接口 |
| **通知系统**：`NotificationService`（`org.freedesktop.Notifications` 标准服务，Qt 槽=方法名：Notify/CloseNotification/GetCapabilities/GetServerInformation）+ `NotificationPopup`（overlay 右下角，**固定尺寸 360×100**，paintEvent 直接 fillRect 背景——layer-shell 窗口无系统背景，QSS 背景需 WA_StyledBackground 或自绘）+ `NotificationCenter`（历史 380×480，Esc 关闭）；弹窗 5 秒 QTimer 自动隐藏、点击打开中心 | 标准 D-Bus 通知协议（gdbus 可触发，busctl 对 `as`/`a{sv}` 空容器语法报 "Too many parameters"，用 gdbus `'[]' '{}'`）；**验证脚本教训：kill shell 必须在 wait compositor 之后**——截图前杀 shell 会销毁全部层表面（layerSurfaces_ 清空），渲染输出纯壁纸，曾误判为弹窗渲染失败 |
| **剪贴板历史（Win+V）**：`ClipboardHistory`（监听 QClipboard::dataChanged，文本/图片条目、**全表去重并移到顶部**、上限 20）+ `ClipboardPanel`（overlay 右缘 360px：文本预览/图片缩略图、点击写回剪贴板、Esc 关闭、空态占位、高度随条目数自适应）+ `ClipboardService`（`org.w10de.Clipboard.ToggleClipboardHistory`，随 /Shell 同服务注册 /Clipboard 对象）+ compositor Win+V 快捷键（fork **double-fork** 异步触发，防僵尸）+ **`wlr_data_device_manager_create`（M1 缺口补上：此前无管理器，客户端间剪贴板/拖放实际不通，剪贴板历史无从谈起）** + **面板 map 即获键盘焦点、unmap 补偿回顶层窗口**（layer-shell on_demand 需 compositor 显式 notify_enter；M2） | Qt D-Bus 槽名即方法名（首字母大写，busctl 大小写敏感）；`--clipboard-selftest`（headless 逻辑自测：去重/移顶/上限/图片/**QVariant 往返**）+ `--clipboard-seed`（渲染验证种子）+ `--clipboard-test <f>`（帧钩子触发面板，进程级单次）；**验证脚本教训：headless 下需显式启动 dbus-daemon（残留 socket 无 daemon 时 Qt 自 spawn 到随机地址，compositor fork 的 dbus-send 连不上）；触发前轮询 bus 确认 org.w10de.Shell 已注册**（shell 启动注册存在时序窗口）。子代理审查 S1（QVariant metatype）+ M1-3 + L4-L8 全部修复，L1（图片逐像素去重）/L2（dataChanged 同步拉取）/L3（面板与通知弹窗同右下角重叠）/L9（request_set_selection 无 serial 校验，既有）登记为已知简化 |
| **终端 w10term**：`TerminalPty`（forkpty + 子进程 exec shell -i + **master fd 设 O_NONBLOCK**——阻塞 fd 使 onReadable 循环 read 卡死 Qt 事件循环（QTimer/repaint 全停，gdb attach 主线程栈实测 read 阻塞于 pty master）→ QSocketNotifier 读 → outputReady）+ `TerminalEdit`（QPlainTextEdit 只读显示，ANSI 子集解析：OSC 标题吞到 BEL/SGR 16 色含亮色与 39/49/ESC[2J 清屏/退格/\r 忽略/光标移动忽略；keyPressEvent 拦截按键转发：Ctrl+字母→控制码、方向键/功能键→ESC 序列、Shift+符号用 e->text()、Alt 组合→ESC+字符、Ctrl+Shift+C/V 本地复制粘贴；**IME 提交转发**（inputMethodEvent））+ `TerminalPty::stop` 用 deleteLater 关 notifier（**Qt 禁止在 activated 槽栈内 setEnabled——gdb 实测崩溃**）| `--selftest`（offscreen：bash -c echo 经 pty 回读断言 + TerminalEdit ANSI 文本提取单测）+ 渲染验证（bash -i 提示符文字 628/背景 34426 像素）+ 单实例 D-Bus（org.w10de.Apps.Terminal）；**调试教训：stderr 重定向到文件时全缓冲，kill 丢日志导致误判"事件循环卡死"，main 开头 setvbuf(_IONBF) 无缓冲**。子代理审查 S1（write 部分写/EAGAIN 丢数据→pendingOut_+Write notifier）、S2（UTF-8 跨 chunk 截断乱码→增量解码）、S3（关窗留僵尸→SIGHUP+超时 waitpid+SIGKILL）、S4（EOF 后 notifier 忙循环 CPU100%→deleteLater+close）+ M1-M7（fcntl 错误处理/TERM 环境/亮色映射/Shift 符号与 Alt 组合/IME/退格缓冲交互/滚动条件）全部修复 |
| **显示设置（第二批）**：`CompositorDbus`（compositor 侧 D-Bus 服务 `org.w10de.Compositor`/Outputs——GetOutputs `a(siiiii)`/GetModes/SetMode/SetScale/SetPosition）+ w10settings"显示"模块（输出/分辨率/缩放下拉 + 应用/刷新，`--page display` 测试辅助，多输出按 currentIndex 操作 + 切换刷新）+ `Compositor::findOutputByName/outputs()` + 0.19 state API 热应用（wlr_output_state_set_custom_mode/set_scale + commit_state）+ **背景矩形随输出 commit 同步**（M1：SetMode 放大/SetPosition 移动后背景不露底） | **libdbus 集成要点**：`dbus_bus_get` 返回**共享连接**——**只 unref 不 close**（close 断言 abort，实测）+ 析构/失败路径 **unregister_object_path**（M4：vtable 悬垂）；dbus fd 挂 `wl_event_loop_add_fd`（单线程共存，read_write(0)+dispatch 循环）。**Qt D-Bus 教训**：`QDBusReply<QDBusArgument>` 与 `value<QDBusArgument>()` 直接迭代在 Qt6 **只读断言崩溃**（gdb 定位）——必须用自定义结构 + 流运算符 + `qdbus_cast`（OutputInfo/ModeInfo，Q_DECLARE_METATYPE 在全局）。**验证**：SetScale 200→960x540、SetMode 1280x720→640x360 热应用；显示页加载日志 `display page loaded 1 output(s) first=HEADLESS-1`；渲染 PASS。headless 无 modes → 常用分辨率回退 + "当前模式"项（L5）。审查 M1-M4 全部修复，L1-L6/L8 修复或登记，L9（outputs_ 无 destroy 监听，DRM 热拔插隐患）登记 |
| **快捷键配置化（第二批）**：`src/ipc/shortcuts.{h,cpp}`（ShortcutAction 14 动作 + parseShortcut("win+q"/"ctrl+alt+l") + loadShortcuts([shortcuts] 段，非法回退默认）+ seat.cpp processKey 查表分发（`(mods & 0xFD) == b.mods` 精确匹配修饰组合 + xkb_keysym_to_lower 支持 shift 组合，dispatchShortcut 动作实现与原硬编码一致）+ `--shortcuts-dump` 验证参数 | 修饰位用魔法数 0x40/0x04/0x01/0x08（wlr_keyboard_modifier LOGO/CTRL/SHIFT/ALT，ipc 层避免 wlroots 头依赖）；**0xFD = LOGO|CTRL|ALT|SHIFT|MOD2|MOD3|MOD5**（M1：原 0x4F 含 CAPS 且注释错误——0.19 get_modifiers 不含 locked 位所以 CAPS 无实际影响，显式修正防升级隐患）；**绑定冲突检测**（M2：同 (mods,sym) 告警 first-wins）；Alt+Tab 语义特殊（Alt 释放时序）保持固定不配置化；**验证**：默认 dump 与原硬编码逐项一致（13/14，Clipboard=win+v 为新增拦截——原转发客户端）、自定义（win+x/ctrl+alt+l/ctrl+f1）生效、非法（缺+、缺修饰键、未知键名、重复修饰键）回退默认、冲突告警、shift 组合解析；回归全 PASS。审查 M1/M2 + L1（shift 键符）/L4/L5/L6 修复，L2（alt+tab 配置被吞）/L8（锁定时快捷键生效，继承原版）文档登记 |
| **电源管理（第二批）**：`src/systemapps/settings/powerinfo.{h,cpp}`（sysfs 直读：`/sys/class/power_supply/BAT*`（type=Battery 过滤 + BAT 前缀，避免外设电池——M5）capacity/status/energy_now（charge_now 回退，**按来源标注 mWh/mAh 不强行换算**——M1）/`/sys/class/backlight/*`（max/brightness 读写 + **W_OK 可写探测**——M4））+ w10settings"电源"模块（电池状态：设备/电量%/充电状态/剩余能量 + 亮度滑块：**valueChanged+!isSliderDown 键盘可调**（M2）+ **缓存 maxBrightness**（M3）+ 无权限禁用滑块 + QSignalBlocker 防刷新写回） | **选型**：KDE 用 UPower D-Bus，sysfs 是内核直出接口（所有 Linux 都有，UPower 同源数据），无 D-Bus 依赖且 WSL 有虚拟 BAT1 可 headless 验证；**验证**：`--selftest`（BAT1 100% Full + backlight none）PASS、`--page power` 渲染 PASS（106072 像素，与外观/显示页像素数不同确认真实切换）。审查 M1-M5 全部修复，L1（percent -1 显示 --）/L9（selftest 放宽）修复，L2-L8/L10（status 映射不全/写后不回读/多背光选择）登记 |
| **音频控制（第二批）**：`src/systemapps/settings/audioinfo.{h,cpp}`（libpulse 客户端 pa_context API：连接（connect <0 立即失败 + 状态回调 FAILED/TERMINATED 双通道上报）+ get_sink_info_list（eol 判完成 + **查询序列号**——M4 快速刷新不混数据）+ set_sink_volume/mute（**pa_operation_unref 全收口**——S3 泄漏修复；未就绪挂起 pending）；pa_mainloop 由 QTimer 20ms 非阻塞迭代驱动（上限 8 次防呆）+ **内部连接超时 2.5s**（M1：挂起时断开允许重建）+ w10settings"音频"模块（sink 下拉 + 音量滑块 + 静音 + 刷新 + **可取消的 3 秒超时兜底**（L1）+ **切换 sink 更新显示**（L7）+ QSignalBlocker）+ paVolumeToPercent 静态（selftest） | **libpulse 关键语义**：READY 状态不得重复 connect（S1：BADSTATE 断言——修复后刷新/重进页面不再误报）；READY 时 refreshSinks 直接重发查询（S2：否则刷新是 no-op）；TERMINATED 与 FAILED 分开（M2：重建/断开不闪"不可用"）；音量 0% → PA_VOLUME_MUTED（M3：-20dB 近似仍有 10% 响度）+ 钳制 0-100（L8）。**验证**：`--selftest` 音量换算断言 PASS、`--page audio` 渲染 PASS（106070 像素）、无服务超时兜底显示不可用。审查 S1-S3 + M1-M4 + L1/L2/L3/L5/L6/L7/L8 全部修复，L4（pending 无上限）登记 |
| **默认应用设置（第二批收官）**：`src/systemapps/settings/defaultapps.{h,cpp}`（xdg 标准 mimeapps.list：loadMimeDefaults 读 [Default Applications] 段（**分号列表取首个**——L1）+ saveMimeDefaults 写（**QSaveFile 原子写**——M1、保留注释/其他段、**空 map 不写空段**——L3）+ listApplications（.desktop 扫描：**只解析 [Desktop Entry] 主段 + Name[lang] locale 匹配**——M6、**用户目录优先去重**——M7、XDG_DATA_HOME——M4）+ currentDefault/setDefault（**浏览器只设 http/https 不覆盖 text/html**——M5））+ w10settings"默认应用"模块（3 类别下拉 + 应用（**失败中断**——L7）+ 状态） | **xdg 语义**：写入位置 $XDG_CONFIG_HOME/mimeapps.list（spec 推荐 GUI 位置，QStandardPaths 尊重 XDG 变量——M4）；浏览器/邮件/文件管理器类别 mime 映射。**验证**：`--selftest` mimeapps 读写断言（保留注释/其他段/保序）PASS、`--page defaults` 渲染 PASS（106115 像素）。审查 M1/M4/M5/M6/M7 + L1/L3/L7 修复，M2（[Added Associations] 同步——严格 xdg 关联）、M3（多级 mimeapps 合并）、M8（[Removed Associations] 清空）、L2/L4/L5/L9/L10（段内注释保留/刷新/懒加载/NoDisplay 过滤/子目录）登记 |

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
CMakeLists.txt                      # 顶层：WlrootsCompat 集成、W10DE_BUILD_SHELL
                                    #   option、Qt6 Widgets+WaylandClient、LayerShellQt
cmake/WlrootsCompat.cmake           # wlroots 获取策略（系统/补丁注入/vendored 编译）
cmake/WlrootsPatchHeaders.cmake     # 系统 wlroots 头 C++ 补丁副本生成
cmake/WlrootsBuildVendored.cmake    # vendored wlroots 自动编译（meson+ninja）
cmake/DepSource.cmake               # 通用依赖策略：系统优先，缺失/版本不符时
                                    #   固定 URL 源码编译到 build/_deps/prefix
cmake/DepsCompat.cmake              # wlroots 生态依赖编排（15 项，版本/URL/meson 参数）
README.md                           # 状态、构建、冒烟验证、待验证项、依赖与许可证
DEPENDENCIES-LICENSES.md             # 上游依赖许可证清单（合规核实，10 项基础清单）
docs/ARCHITECTURE.md                # 架构设计文档（决策表/里程碑/风险）
docs/HANDOFF.md                     # 本文档
.gitignore

src/compositor/
  server.{h,cpp}                    # Compositor：生命周期/协议/层锚/arrange/可用区/多工作区/动画 tick
  output.{h,cpp}                    # Output：配置/背景/帧循环/截图验证/定时切工作区
  view.{h,cpp}                      # View：xdg 窗口 + SSD 装饰 + 标题文字 + 阴影 + Snap/动画 + ft 同步
  xview.{h,cpp}                     # XView：XWayland 窗口 + 同款装饰/任务栏/阴影/动画（M7 续）
  seat.{h,cpp}                      # Seat：输入/焦点/拖动/装饰交互/层表面命中/hover
  layer_shell.{h,cpp}               # LayerSurface：层表面管理/命中
  alttab.{h,cpp}                    # Alt+Tab 切换器（scene 层 UI + Seat 集成，第一批）
  dbus_service.{h,cpp}              # D-Bus 服务 org.w10de.Compositor（显示设置 IPC，第二批）
  titletext.{h,cpp}                 # M2b：cairo/pango 标题文字 → 自实现 wlr_buffer
  shadow.{h,cpp}                    # M8：窗口阴影渐变 buffer（自绘）
  util.h                            # W10DE_CONTAINER_OF 宏 + themeColorToFloat
  main.cpp                          # 参数解析（--width/--frames/--workspace/--switch-ws/--snap-test 等）
  CMakeLists.txt                    # WLR_USE_UNSTABLE、PkgConfig::WLROOTS/DRM/XKBCOMMON/WAYLAND_SERVER/CAIRO/PANGO

src/ipc/
  config.{h,cpp}                    # 无依赖 INI 解析器（compositor/shell 共用）
  theme.{h,cpp}                     # 主题定义（Theme 结构/深浅预设/loadTheme/parseColor，纯 C++）
  shortcuts.{h,cpp}                 # 快捷键配置（[shortcuts] 段解析/默认绑定，第二批）

src/shell/
  main.cpp                          # 入口：layer-shell 配置（桌面/任务栏/开始菜单）+ --wallpaper + 主题加载
  theme/colors.{h,cpp}              # 主题色访问器（从 [theme] 段加载的全局主题取色）
  desktop/desktopwindow.{h,cpp}     # 桌面：壁纸（内置渐变/指定图片）+ 图标列表（M4）
  taskbar/taskbarwindow.{h,cpp}     # 任务栏主窗口 + 窗口列表管理
  taskbar/startbutton.{h,cpp}       # 开始按钮
  taskbar/clock.{h,cpp}             # 时钟
  taskbar/taskbarbutton.{h,cpp}     # 窗口项按钮
  ipc/foreigntoplevel.{h,cpp}       # foreign-toplevel 客户端绑定（Qt 封装）
  ipc/notificationservice.{h,cpp}   # 通知 D-Bus 服务（org.freedesktop.Notifications，第一批）
  ipc/clipboardservice.{h,cpp}      # 剪贴板面板 D-Bus 服务（org.w10de.Clipboard，第一批）
  notification/notificationpopup.{h,cpp}   # 通知弹窗（overlay 右下角，固定 360×100）
  notification/notificationcenter.{h,cpp}  # 通知历史中心（overlay 380×480）
  clipboard/clipboardhistory.{h,cpp}       # 剪贴板历史模型（QClipboard 监听，第一批）
  clipboard/clipboardpanel.{h,cpp}         # 剪贴板历史面板（overlay 右缘 360px，第一批）
  startmenu/appmodel.{h,cpp}        # .desktop 扫描
  startmenu/startmenu.{h,cpp}       # 开始菜单（含搜索框）
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
  theme.{h,cpp}                     # 主题定义（Theme 结构/深浅预设/loadTheme/parseColor，纯 C++）

src/systemapps/
  appipc.{h,cpp}                    # 系统应用通用接口：D-Bus 单实例激活（org.w10de.Apps.*）
  explorer/                         # w10explorer 文件资源管理器（系统应用示例）
    main.cpp                        # 入口：单实例 + --selftest（文件操作 headless 自测）
    explorerwindow.{h,cpp}          # 主窗口（导航/地址栏/文件区/右键菜单/快捷键/状态栏）
    fileops.{h,cpp}                 # 文件操作（复制/剪切/粘贴/回收站/重命名/新建/大小）
  settings/                         # w10settings 设置中心（KDE System Settings 风格）
    main.cpp                        # 入口：单实例 + --selftest（配置/电源/音量/mimeapps headless 自测）+ --page
    settingswindow.{h,cpp}          # 主窗口（搜索/左侧分类/右侧模块：外观/系统/**显示**/**电源**/**音频**/**默认应用**）
    powerinfo.{h,cpp}               # 电源信息（sysfs 电池/背光，第二批）
    audioinfo.{h,cpp}               # 音频控制（libpulse 客户端，第二批）
    defaultapps.{h,cpp}             # 默认应用（xdg mimeapps.list，第二批收官）
  term/                             # w10term 终端（第一批，2026-08）
    main.cpp                        # 入口：单实例 + --selftest（pty 回读 + ANSI 提取单测）
    terminalpty.{h,cpp}             # PTY 封装（forkpty + 非阻塞 master + QSocketNotifier）
    termwindow.{h,cpp}              # 主窗口（TerminalEdit：ANSI 解析 + 按键转发）
  CMakeLists.txt                    # systemapps_appipc 静态库 + 各应用 + .desktop 生成

third_party/
  wlroots/                          # wlroots 0.19.0 完整源码（API 参考，勿改）
  stb/stb_image_write.h             # PNG 输出（compositor 截图用）
```

---

## 7. 已知问题与待验证项（按优先级）

### 7.1 编译验证（✅ 2026-08 已在 WSL2 Arch 完成首编，以下为结论）
1. ~~`[static 4]` C99 语法~~ ✅ **g++16 不接受**：vendored `wlr_scene.h` 补丁为 `color[4]`（见 README 编译记录）。
2. ~~LayerShellQt CMake target~~ ✅ Arch 为 `LayerShellQt::Interface`（CMake 已兼容两种名字）；`find_package(LayerShellQt)` 包名正确。
3. **layer-shell-qt 配置时序**（`show → 配置 → hide → show`）：**未实测**（headless 冒烟未运行 shell；嵌套验证时确认）。`useLayerShell()` 在 Qt≥6.5 已废弃（无害）。
4. ~~枚举名~~ ✅ `Anchors` 需显式构造（`Anchors(...)`），其余枚举名正确。

### 7.2 行为待验证
- ✅ headless 冒烟已通过（2026-08：`pixel verification passed`，#0078D7，退出码 0）。
- ✅ 锁屏链端到端（2026-08：busctl 调 org.w10de.Shell.Lock → w10lock 启动 → compositor `session locked`）；剩余：锁屏画面渲染与任意键解锁的真机确认。
- ✅ M2b 标题栏文字/按钮（2026-08：白字 385 像素 + 关闭钮红 1472 像素）、窄窗口清空。
- ✅ M7 续 多工作区 4 场景（默认可见/他区隐藏/切走隐藏/切回可见）。
- ✅ M8 阴影（阴影带变暗、内容区无污染）与 Aero Snap（贴左半屏 960×1048 at 0,0）。
- ✅ 主题三态（2026-08）：深色默认回归（白字 385/红钮 1472/任务栏 #2D2D2D）、浅色
  （任务栏/标题栏 #F3F3F3 + 深色文字 202 像素 + 无纯白残留）、自定义键覆盖
  （taskbar_bg=#123456、titlebar_bg=#654321 双进程生效）。锁屏 w10lock 未主题化
  （保持深色时钟，MVP 合理）。
- ✅ 第一批功能补全（2026-08）：Alt+Tab（--alttab-test 300 帧钩子：强调色高亮 8583/
  常态 5990 像素）、全局搜索（开始菜单搜索框渲染）、通知中心（gdbus 触发 + 弹窗
  card 6314/文字 1214 像素）、剪贴板历史（--clipboard-selftest PASS + 面板渲染
  card 33831/文字 73 像素）、**w10term**（--selftest：pty 回读 160B + ANSI 提取
  单测 PASS；渲染：bash -i 提示符文字 628/背景 34426 像素；单实例 D-Bus 激活
  PASS）。**第一批 5 项全部完成。**
- ✅ 第二批显示设置（2026-08）：compositor D-Bus 服务（org.w10de.Compositor/Outputs）
  GetOutputs/GetModes/SetMode/SetScale/SetPosition 全部工作（SetScale 200 →
  GetOutputs 960x540；SetMode 1280x720 → 640x360，热应用生效）；w10settings"显示"
  模块渲染 PASS（--page display，102158 像素）。
- ✅ 第二批快捷键配置化（2026-08）：--shortcuts-dump 验证——默认绑定与原硬编码
  一致（close=win+q 等 14 动作）；自定义 config（close=win+x/lock=ctrl+alt+l/
  move_left=win+a/workspace_1=ctrl+f1）解析生效；非法配置回退默认。
- ✅ 第二批电源管理（2026-08）：--selftest（WSL 虚拟 BAT1：`OK battery: BAT1 100%
  Full`、`OK backlight: none`）PASS；--page power 渲染 PASS（106072 像素）。
- ✅ 第二批音频控制（2026-08）：--selftest 音量换算断言（0→0/0x8000→50/0x10000→100）
  PASS；--page audio 渲染 PASS（106070 像素）；WSL 无 Pulse 服务 → 2.5s AudioInfo
  内部超时 + 3s UI 兜底显示不可用。审查 S1-S3（READY 重复 connect/刷新 no-op/
  operation 泄漏）+ M1-M4（连接超时/重建闪烁/0% 静音/查询序列号）+ L1-L8 修复。
- ✅ 第二批默认应用（2026-08）：--selftest mimeapps 读写断言（保留注释/其他段/保序）
  PASS；--page defaults 渲染 PASS（106115 像素）。审查 M1（QSaveFile 原子写）/M4（XDG
  变量）/M5（不覆盖 text/html）/M6（locale 名）/M7（去重）+ L1/L3/L7 修复。
- ✅ **第二批 5 项全部完成（2026-08）**：显示设置、快捷键配置化、电源管理、音频控制、
  默认应用设置——每项均编译 + headless 验证 + 子代理审查 + 文档同步。
- 🔶 剪贴板历史：**真实跨应用复制粘贴**依赖 compositor 的 wlr_data_device_manager
  （已补，M1 缺口）；WSL headless 下无真实复制来源，面板内容用 --clipboard-seed
  种子验证；**验证脚本须显式启动 dbus-daemon 并轮询 org.w10de.Shell 注册**（Qt
  自 spawn daemon 到随机地址会导致 compositor fork 的 dbus-send 连不上——实测竞态）。
- 🔶 **依赖 from-source 机制**（2026-08）：auto 模式全系统走通（15 依赖零下载零
  回归）；**强制源码验证完成**——wayland-protocols / wayland / libdrm / pixman /
  libxkbcommon 五依赖从源码完整构建（下载→解包→meson→install→PKG_CONFIG_PATH
  注入→项目链接→回归 PASS）；多源/延迟筛选/魔数校验实测有效（libxkbcommon 官方
  源 xkbcommon.org 当前可达）。**WSL 网络对 gitlab.freedesktop.org 间歇性
  connection reset**（当前环境经 Windows 主机 SOCKS 代理 172.21.192.1:10808
  可达，已配 WSL 代理脚本模板）；`W10DE_FORCE_SOURCE_DEPS` 可在任意环境重跑验证。
- ⬜ 嵌套验证（wayland backend + WSLg/weston）：开窗口、拖动、标题栏按钮、任务栏窗口列表、开始菜单交互、电源菜单 systemctl 实测（WSL 可测，勿真关机）。
- ⬜ XWayland：WSL 中 `/tmp/.X11-unix` 为只读挂载导致 wlr_xwayland 创建失败（已降级为警告）；**XView 装饰/拖动/override-redirect/set_class 未运行时验证**，真机验证 X11 客户端。
- ⬜ DRM 真机：headless 之外的后端需真机/KVM。

### 7.3 已知简化（M3 阶段刻意为之）
- 开始菜单无固定磁贴（仅 .desktop 列表网格；搜索框已实现，见第一批全局搜索）。
- 通知弹窗固定尺寸 360×100（内容超长截断，非动态高度）；通知中心仅会话内历史（不持久化）；无通知分组/勿扰模式。
- 剪贴板历史：无固定条目/清除全部（KDE Klipper 有）；图片条目无大小上限（大图占内存）；选择条目后写回剪贴板由用户按 Ctrl+V 粘贴（Win10 自动粘贴需 compositor 注入按键，未做）；面板宽度固定 360。
- **终端 w10term**：ANSI 光标移动/256 色/行内覆盖未实现（追加模式，全屏程序如 vim/top 显示异常）；无终端尺寸随窗口变化（winsize 固定 120×32）；IME 无候选框显示（提交转发可用）；shell 退出后窗口保留（不自动关闭）；粘贴多行不做换行转换；滚动条接近底部才自动跟随（上翻可回看历史）。
- **显示设置**：单输出为主（多输出下拉仅第一项可操作——SetMode/SetScale 用 outputNames_.first()）；无刷新率/旋转设置；headless 无 modes 列表回退常用分辨率（真机 DRM 用 GetModes）；无"应用后确认/回滚"对话框（改错分辨率立即生效）；设置不持久化（重启 compositor 后恢复默认，KDE 会写配置）。
- **快捷键配置化**：无设置 UI（编辑 config.ini 生效，KDE 有 System Settings UI）；Alt+Tab 固定不可配；绑定冲突时按动作枚举顺序优先（先定义者生效）；无重复绑定检测；caps lock 开启时修饰位含 MOD2（0x4F 不含 CAPS 位——CAPS 不影响匹配，但 capslock 状态变化可能产生未预期组合，真机验证）。
- **电源管理**：sysfs 直读（无 UPower D-Bus 集成——数据同源但真机 UPower 的百分比/时间计算更精细）；多电池只显示主电池（BAT* 第一个）；无系统托盘电源图标（KDE Plasma 托盘有）；无省电模式/合盖行为设置；背光写需 root/backlight 组权限（无权限时滑块禁用并提示）。
- **音频控制**：WSL 无服务时显示不可用（真机 pipewire-pulse 提供服务）；音量映射为 dB 近似（0%≈-20dB 非全静音——0% 时建议后续用 mute 语义）；仅控制默认/首个 sink 的音量与静音（无每应用音量、无设备切换后自动跟踪、无输入设备控制）；pa 连接超时兜底 3 秒（真机正常 <1 秒）。
- **默认应用**：只写 [Default Applications] 段（不维护 [Added Associations] 同步/多级 mimeapps 合并——严格 xdg 关联算法下部分应用可能不被识别为关联，真机验证）；无 NoDisplay 过滤（设置页列出全部 .desktop）；无子目录 .desktop 扫描；无页面刷新按钮（安装新应用后需重启设置）。
- 单键盘设备（多键盘热插拔被忽略）、无触摸/数位板。
- foreign-toplevel `activate()` 硬编码 seat 名 "seat0"。
- fullscreen 按最大化处理；窗口 Snap 无拖拽到屏幕边缘触发（仅快捷键，MVP）；无四分之一 Snap。
- XView 拖动期间每光标事件一次 `wlr_xwayland_surface_configure`（审查 #9 记录，未节流）；xdg/XView 命中优先级与 scene z 序分裂（审查 #3 记录，重叠窗口场景）。
- SNI 托盘：`IconPixmap` 的 QDBusArgument 提取与 ARGB32 字节序（小端假设）待真机验证；`IconName` 主题查找失败时图标可能不显示；中键 SecondaryActivate 未连接。
- 锁屏（M6）：任意键解锁（无密码验证）；单锁屏 surface（首个输出）；`wl_shm` 客户端渲染（shm_open/mmap/QImage 字节序）待真机验证；`wl_seat` 的 capabilities 事件已处理（键盘绑定），但 seat name 事件忽略；锁屏进程崩溃时合成器自动解锁（ext-session-lock 语义，属预期）。
- 窗口动画仅位置插值（Snap/还原）；尺寸变化（resize）仍即时请求、无动画。

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

1. **真机/嵌套验证**（需用户环境）：DRM 后端、XWayland 运行时（XView 装饰/拖动/override-redirect）、嵌套 wayland 后端（窗口/任务栏/开始菜单交互）、锁屏画面与任意键解锁。
2. **系统应用扩展**（可选）：w10explorer 补充（拖放细节/详情视图/回收站 UI/面包屑）；后续系统应用（记事本/设置/终端）按 SYSTEMAPPS.md 复用 appipc。
3. **交互打磨**（可选）：拖拽到屏幕边缘触发 Snap、XView 拖动 configure 节流、xdg/XView 统一命中 z 序（审查 #3/#9）。
4. **锁屏密码验证**（PAM）与电源菜单真实动作确认。
5. **提交/推送**：当前第一批 5 项（Alt+Tab/全局搜索/通知中心/剪贴板历史/终端 w10term）+ 第二批 5 项（显示设置/快捷键/电源/音频/默认应用）+ 第三批前 2 项（截图工具/系统监视器）+ 主题/from-source/explorer 改动在本地（最近提交 `dafcd55`），需用户授权后 commit + push。
6. **第二批（设置完备）**：显示设置 ✅、快捷键配置化 ✅、电源管理 ✅、音频控制 ✅、默认应用设置 ✅——**第二批 5 项全部完成**；**第三批（生态）5 项全部完成**：截图工具 ✅（wlr-screencopy v3 客户端；S1/M1/L1-L5 审查修复；纯壁纸+窗口验证）、系统监视器 ✅（/proc 数据源；审查 3 中等修复）、计算器 ✅（审查 M1-M3 修复；selftest 9 项）、壁纸幻灯片 ✅（slideshow_dir/interval；三连拍红→绿；**LayerShellQt paint 失效→hide/show**；审查 S1 优先级/M2/L 系修复）、网络/蓝牙 ✅（settings 网络+蓝牙模块：NetworkManager/Bluez D-Bus；**子代理审查 S1/S2 严重问题修复**：GetManagedObjects a{oa{sa{sv}}} 类型化注册（QMap<QDBusObjectPath,QMap<QString,QVariantMap>> + qDBusRegisterMetaType）、AddressData aa{sv} 双形态解组（QDBusArgument / QVariantList 兼容）、连接路径精确匹配（M1）、toggle 失败提示时序（M2）、errorText 透传（M3）；**D-Bus mock 端到端验证 PASS**：Qt 在 system bus 注册假服务，NET 连接+IP、BT 适配器+设备解析全过——测试工具 /tmp/w10de-dbtest.cpp（WSL 临时，不入库）；服务缺失降级渲染 PASS；setPowered 的 Properties.Set 签名已审查、mock 未覆盖，真机需验证）。

---

## 10. 交接注意事项

- **用户偏好**：简体中文交流；AGENTS.md 要求执行命令/文件变更前经用户确认（审查流程豁免过）；回答结构化、准确性优先、不伪造数据。
- **开发流**：Windows 编辑 → 复制文件到 WSL（`/root/win10de`）→ WSL 编译（`cmake --build build --target w10compositor`）→ headless 脚本验证。**WSL 无 git**，勿在 WSL 侧提交。
- **验证脚本**（Windows 侧 `%TEMP%`，经 `/mnt/c/Users/Administrator/AppData/Local/Temp/` 在 WSL 执行）：`w10de-title.sh`（标题栏白字/红钮）、`w10de-ws.sh`（多工作区 4 场景）、`w10de-narrow.sh`（窄窗口）、`w10de-shadow.sh`（阴影）、`w10de-snap.sh`（Snap）、`w10de-alttab.sh`（Alt+Tab）、`w10de-search-render.sh`（开始菜单搜索框）、`w10de-notify4.sh`（通知弹窗：gdbus 触发 + 像素校验；**注意 kill shell 必须在 wait compositor 之后**，否则截图时层表面已销毁、画面纯壁纸——此前多次误判渲染失败即此因）、`w10de-clipboard.sh`（剪贴板历史：selftest + 面板渲染；**headless 需显式启动 dbus-daemon 且触发前轮询 org.w10de.Shell 注册**）、`w10de-syncbuild.sh`（同步+编译）、`w10de-shot-dbg.sh`（截图工具：纯壁纸全蓝校验；**pkill 禁用 -f 匹配命令行**——会误杀外层 bash，用 pkill -x）、`w10de-shot-window.sh`（截图工具：设置窗口多色）、`w10de-monitor-render.sh`（监视器渲染）、`w10de-calc-render.sh`（计算器渲染）、`w10de-slideshow3.sh`（壁纸幻灯片三连拍红→绿；**interval=8s + hide/show 强制重绘**）、`w10de-netbt-render.sh`（网络/蓝牙页渲染）、`w10de-pngcheck.py`（PNG 采样分析）、`w10de-batch3-regress.sh`（第三批汇总回归）。
- **wlroots 头文件无 extern "C" 保护**：C++ 引用必须手动包裹；不稳定接口需要 `WLR_USE_UNSTABLE`。
- **先读**：本文档 → `docs/ARCHITECTURE.md` → `README.md` → 相关源码，再改代码。
- **KDE 差距分析**：`docs/KDE-GAP.md`（2026-08 生成）——按类别/优先级列出未实现功能
  （高优先：软件中心、进程管理器、Snap 布局选择器、锁屏密码 PAM、文件索引搜索），
  后续功能补全立项以此为基准。
- **不要**重新核对已确认的 API（见第 4 节决策表与源码注释中的"已确认"标注）；新增 wlr_* 调用时对照 `third_party/wlroots/include/`。
- **维护规则（用户明确要求）**：**每次完成任务/里程碑时，更新 `README.md` 的同时必须同步更新本文档**（状态表、文件清单、决策、已知问题、下一步）。本文档不是一次性的——它随项目演进持续维护，任何"进行中"状态必须在每次交接时准确反映。若 README 有变更而本文档未同步，视为交接不完整。

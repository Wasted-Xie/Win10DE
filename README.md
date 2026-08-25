# Win10DE

Linux 上的 Windows 10 风格桌面环境，从零实现。

> **Disclaimer**: Not affiliated with Microsoft. This is an independent,
> unofficial project inspired by the Windows 10 user interface. "Windows" and
> related names are trademarks of Microsoft Corporation.
> 本项目与微软无关，系受 Windows 10 界面启发的独立非官方项目。

- **技术栈**：C++20 + [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) 0.19 + Qt 6.5+
- **形态**：wlroots compositor（`w10compositor`）+ Qt Widgets shell（`w10shell`，layer-shell 客户端）+ 托盘/会话组件
- **功能**：窗口管理器、Win10 风格服务端装饰、任务栏与开始菜单、桌面壁纸/图标、系统托盘、锁屏

## 状态

**M0-M7 编码完成，WSL2（Arch）真实编译 + headless 冒烟 + 完整渲染验证通过**（2026-08：vendored wlroots 0.19 源码编译；`--frames 5` 截图像素校验 `pixel verification passed`；compositor+shell 同跑验证**桌面壁纸渐变 + 任务栏**渲染成功，238 色，底部 #2D2D2D 任务栏）。

- [x] 技术调研与架构设计
- [x] M0 项目骨架 + headless compositor 代码（未编译验证）
- [x] M1 核心 compositor 代码：多后端（headless/wayland/drm）、xdg-shell 窗口、
      seat 输入与焦点（指针/键盘/光标/窗口移动缩放）、输出布局（未编译验证）
- [x] M2a SSD 标题栏：xdg-decoration 强制服务端装饰、Win10 风格标题栏
      （深色背景 + 最小化/最大化/关闭按钮）、标题栏拖动、按钮操作（未编译验证）
- [x] M3 前置：compositor 的 layer-shell 支持——层表面管理（锚点/边距/独占区
      几何由 wlr_scene helper 计算）、层锚树 z 序（background < bottom < view <
      top < overlay）、背景/窗口/层表面分层挂载（未编译验证）
- [x] M3 前置：foreign-toplevel-management——任务栏窗口列表协议（View 创建
      handle、状态同步 title/activated/maximized/minimized、任务栏请求转发
      最大化/最小化/激活/关闭）（未编译验证）
- [x] M3 前置：layer surface 输入支持——指针命中（overlay/top 层优先于窗口，
      bottom/background 层在窗口下）、层表面键盘焦点（keyboard-interactive）、
      窗口最大化避开任务栏独占区（可用区缓存）（未编译验证）
- [x] M3 shell 客户端：任务栏（bottom 层、layer-shell-qt 绑定）——开始按钮、
      时钟、窗口列表（foreign-toplevel 客户端：wayland-scanner 生成绑定 +
      Qt 封装，窗口项按钮显示标题/激活高亮/点击激活）
- [x] M3 shell 客户端：开始菜单（overlay 层）——.desktop 应用扫描（系统+
      用户目录）、Win10 磁贴网格（图标+名称）、单击启动（QProcess 分离式）、
      Esc 隐藏、开始按钮切换（Qt 代码需 Qt 6 + layer-shell-qt 环境编译，未验证）
- [x] M4 桌面（background 层）：壁纸（--wallpaper 指定或内置 Win10 风格渐变）、
      桌面图标（QFileSystemModel 显示桌面目录、双击系统打开）——未编译验证
- [x] M5 系统托盘：SNI 宿主（D-Bus `org.kde.StatusNotifierWatcher` 纯 Qt 实现，
      无 KF 依赖）+ 任务栏托盘区（图标显示/点击 Activate/右键 ContextMenu）
      ——未编译验证
- [x] M6 锁屏：`w10lock` 进程（ext-session-lock-v1——0.19 已移除旧 input-inhibitor，
      改用现代锁屏协议）：compositor 侧锁定/解锁（隐藏普通内容锚、焦点限制）、
      wl_shm 渲染（QPainter 离屏时钟）、任意键解锁（MVP 不验密码）——未编译验证
- [x] M7 会话启动器：`w10-session` 脚本（compositor 固定 socket → 等待就绪 →
      w10shell；退出联动）+ `--socket` 参数 + CMake 安装规则——未编译验证
- [x] M7 会话集成（续）：锁屏触发（**Win+L** 快捷键 → compositor fork w10lock；
      D-Bus 服务 `org.w10de.Shell.Lock()`）、autostart（~/.config/autostart
      *.desktop Exec）——未编译验证
- [x] M7 会话集成（续）：配置系统——`~/.config/w10de/config.ini`（无依赖 INI
      解析器 `src/ipc/config.{h,cpp}`，compositor/shell 共用；[output] 尺寸 +
      [wallpaper] 路径，--config 参数）+ 示例配置——未编译验证
- [x] M7 会话集成（续）：**XWayland**（`wlr_xwayland` lazy 模式 + XView 窗口类：
      map/unmap/激活/关闭/最大化/最小化/configure、scene 集成、seat 命中、
      DISPLAY 环境注入）——未编译验证
- [x] M2b 标题栏文字渲染（cairo/pango）与 hover 打磨（2026-08 headless 验证通过）
- [x] M7 续：**多工作区**（窗口归属/切换/显示隐藏，Win+1..4；headless 4 场景验证通过）
- [x] M7 续：**XWayland SSD 装饰 + 任务栏集成**（XView 同款 Win10 标题栏 + foreign-toplevel handle；WSL 无 XWayland 未运行时验证）
- [x] M8 视觉打磨：窗口阴影（自绘渐变）、Aero Snap（Win+←/→/↑/↓ + 平滑动画）、窗口移动动画；圆角遵循 Win10 直角设计（UI 元素 Qt 侧 2px 圆角）（2026-08 验证通过）
- [x] **主题功能**：`[theme]` 配置段（compositor 与 w10shell 共用）——`mode=dark/light` 预设
      + 任意颜色键覆盖（自定义主题通道）；深色/浅色/自定义三态 headless 验证通过（2026-08）
- [x] **系统应用框架**（docs/SYSTEMAPPS.md）：独立二进制 + D-Bus 单实例激活通用接口
      （`org.w10de.Apps.<Name>` / Activate(s path)，`src/systemapps/appipc.{h,cpp}`，后续
      系统应用复用）+ **w10explorer 文件资源管理器**（文件操作对标 Windows：导航树/地址栏/
      前进后退/复制/剪切/粘贴/冲突命名/删除进回收站/重命名/新建文件夹/属性/快捷键/右键菜单/
      状态栏；`--selftest` 8 项自测 + headless 渲染 + 单实例 D-Bus 激活验证通过，2026-08）
- [x] **w10settings 设置中心**（参考 KDE System Settings：顶部搜索 + 左侧分类 + 右侧模块）：
      外观（主题深浅色/壁纸路径，写 config.ini）、系统（关于/开机自启开关）；Config 扩展
      写入（保留注释与顺序）；`--selftest` 配置读写 + headless 渲染 + 单实例验证通过（2026-08）
- [ ] **功能补全（goal cd47bf3e，对标 KDE 差距分析）**：第一批 Alt+Tab 切换器 ✅、全局搜索 ✅
      （开始菜单顶部搜索框：应用过滤 + 主目录文件搜索混合结果、文件系统默认打开、磁贴区隐藏；
      渲染验证 PASS）、**通知中心 ✅**（`org.freedesktop.Notifications` 标准 D-Bus 服务 + 右下角
      弹窗（360×100，5 秒自动隐藏、点击打开历史）+ 通知历史中心（380×480，Esc 关闭）；
      gdbus 触发 + headless 渲染验证 PASS）、**剪贴板历史 ✅**（Win+V 语义：`ClipboardHistory`
      监听系统剪贴板（文本/图片、去重、上限 20 条）+ overlay 历史面板（点击写回剪贴板、
      Esc 关闭）+ Win+V 快捷键（compositor → D-Bus → shell）→ **终端 w10term ✅**（复用
      systemapps 框架 + appipc 单实例 + PTY（forkpty + 非阻塞 master）+ ANSI 子集解析
      （SGR 16 色/清屏）+ 按键转发 + `--selftest` 逻辑自测 + headless 渲染 + 单实例
      D-Bus 激活验证 PASS）→ 第一批完成。**第二批显示设置 ✅**（compositor D-Bus 服务
      `org.w10de.Compositor`/Outputs：GetOutputs/GetModes/SetMode/SetScale/SetPosition，
      libdbus + wl_event_loop 集成 + wlroots 0.19 state API 热应用；w10settings"显示"模块：
      输出/分辨率/缩放下拉 + 应用/刷新，`--page display` headless 验证 PASS：IPC 热应用
      （SetScale 200→960x540）+ 渲染 PASS）→ **快捷键配置化 ✅**（`[shortcuts]` 配置段驱动
      Seat 快捷键：`src/ipc/shortcuts.{h,cpp}` 解析 "win+q"/"ctrl+alt+l" 格式 + 14 动作
      绑定（close/maximize/snap/lock/quit/clipboard/workspace_1-4；Alt+Tab 保持固定）；
      processKey 查表分发（修饰组合精确匹配，支持 ctrl/alt/shift 组合）；`--shortcuts-dump`
      验证：默认绑定与原硬编码一致、自定义（win+x/ctrl+f1）生效、非法回退默认）→ **电源管理
      ✅**（`src/systemapps/settings/powerinfo.{h,cpp}`：sysfs 直读电池（/sys/class/power_supply
      的 BAT*：capacity/status/energy_now，UPower 同源数据）与背光（max_brightness/brightness
      读写）；w10settings"电源"模块：电池状态（电量%/充电状态/剩余）+ 亮度滑块（写回）；
      `--selftest`（WSL 虚拟 BAT1：100% Full）PASS + `--page power` 渲染 PASS）→ **音频控制
      ✅**（`src/systemapps/settings/audioinfo.{h,cpp}`：libpulse 客户端（pa_context API，
      真机 pipewire-pulse 提供 Pulse 兼容服务；无服务时连接失败/超时兜底显示不可用）；
      w10settings"音频"模块：输出设备下拉 + 音量滑块 + 静音；`--selftest` 音量换算断言 +
      `--page audio` 渲染 PASS）→ **默认应用设置 ✅**（`src/systemapps/settings/defaultapps.{h,cpp}`：
      xdg 标准 mimeapps.list（[Default Applications] 段：浏览器 http/https/html、邮件 mailto、
      文件管理器 inode/directory），保留注释/其他段写入；.desktop 应用扫描；w10settings"默认
      应用"模块：3 类别下拉 + 应用；`--selftest` mimeapps 读写断言 + `--page defaults` 渲染
      PASS）——**第二批 5 项全部完成**；第三批生态：**截图工具 ✅**（`src/systemapps/screenshot/`：
       wlr-screencopy 协议客户端 w10screenshot——registry 绑定 wl_shm +
       zwlr_screencopy_manager_v1 + 首个 wl_output（bind v4 完整监听）；capture_output →
       buffer 事件（格式用事件值创建 wl_shm buffer）→ copy → ready → XRGB/ARGB→RGBA
       转换 + stbi 写 PNG；compositor 侧 server.cpp 注册
       `wlr_screencopy_manager_v1_create`；修复：像素转换循环漏写 dst 递增导致截图仅
       首列有内容；审查 S1/M1/L1-L5 修复（失败路径守卫/多输出选择/v3 buffer_done 时序/
       fd 泄漏/alpha 保留/超时兜底）；headless 验证 PASS：纯壁纸全蓝 + 设置窗口内容
       多色正确截取）→ **系统监视器 ✅**（`src/systemapps/monitor/`：Win10 任务管理器
       风格——CPU 使用率曲线（自绘 60 点滚动）+ 每核进度条 + 内存（含 swap）+ 磁盘
       读写/网络收发速率；数据源纯 /proc（/proc/stat、/proc/meminfo、/proc/net/dev、
       /proc/diskstats，增量速率计算）；`--selftest` PASS（WSL 24 核/内存 9.7%）+
       headless 渲染 PASS（深色面板 + 白字 + 进度条））→ **计算器 ✅**（`src/systemapps/calculator/`：
       Win10 标准计算器——深色主题、即时计算状态机（操作符先结算积压再记录）、
       % 双语义（无操作符=自身/100，有操作符=acc×v/100）、÷0 错误态、C/CE/±；
       `--selftest` 8 项断言全过 + headless 渲染 PASS（深色背景 + 数字键）；
       系统监视器经子代理审查修复 3 个中等问题（MemAvailable 回退条件、
       CPU 增量无符号下溢守卫、接口/磁盘对象缓存））→ **壁纸幻灯片 ✅**
       （`[wallpaper] slideshow_dir` + `slideshow_interval` 配置段：desktop 层按
       文件名排序定时轮换目录内图片；--wallpaper 优先、其次幻灯片、再单张
       path；headless 三连拍验证红→绿轮换 PASS；已知：LayerShellQt 增量 paint
       调度失效（update/repaint/requestUpdate 均不触发 surface 提交），轮换用
       hide/show 强制重绘，真机潜在闪烁已注释；子代理审查 S1 优先级/M2 溢出/
       L 系全修复）→ **网络/蓝牙集成 ✅**（设置中心"网络"模块：NetworkManager
       D-Bus 状态（连接状态/活动连接 Id/Type/IPv4）；"蓝牙"模块：Bluez 适配器
       电源开关 + 设备列表（Name/地址/连接状态）；服务缺失降级"服务不可用"
       （与音频模块同款）；**子代理审查 S1/S2 严重问题全修复**（GetManagedObjects
       a{oa{sa{sv}}} 类型化注册解组、AddressData aa{sv} 双形态解组、连接路径
       精确匹配、失败提示时序）+ **D-Bus mock 端到端验证 PASS**（Qt 注册假
       Bluez/NetworkManager 到 system bus：NET 连接+IP 192.168.1.100、
       BT 适配器+设备解析全过）；headless 渲染 PASS；真机需 NetworkManager/
       Bluez 验证真实连接）——**第三批 5 项全部完成**；后续对标 KDE 的差距
       清单见 [docs/KDE-GAP.md](docs/KDE-GAP.md)（按类别+优先级：软件中心/
       进程管理/Snap 布局/锁屏密码/文件索引为高优先）
- [x] 编译与冒烟验证（headless 运行 + 截图 + 像素校验，2026-08 Arch/WSL2 通过）
- [x] 完整渲染验证（compositor + w10shell 同跑：桌面壁纸渐变 + 任务栏渲染成功，2026-08）
- [ ] 真机/嵌套环境验证（DRM、XWayland 运行时、鼠标键盘实际交互）

详见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 目录结构

```
src/compositor/    wlroots 合成器（C++ 封装）
src/shell/         Qt Widgets UI（layer-shell 客户端，M3 起）
src/shell/desktop/ 桌面（壁纸 + 图标，background 层，M4）
src/tray/          系统托盘（SNI 宿主 + 任务栏托盘区，M5）
src/lock/          锁屏进程 w10lock（ext-session-lock-v1，M6）
src/session/       会话启动脚本 w10-session（M7）
src/tray/          系统托盘（SNI + XEmbed，M5）
src/ipc/           D-Bus 接口定义
src/session/       会话启动脚本与配置
protocols/         wlr-protocols XML（如需自生成绑定）
themes/            壁纸、图标资源
tools/             开发脚本（headless 测试、截图）
docs/              设计文档
```

## 构建（环境就绪后）

依赖清单见架构文档第 10 节（Debian/Ubuntu 与 Arch 包名），当前代码需要：
`wlroots 0.19`（含 wlr-protocols）、`libdrm`、`wayland`、`xkbcommon`、
Qt 6（shell 用，含 WaylandClient）+ layer-shell-qt。

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### 跨发行版兼容（wlroots 获取策略）

构建系统自动适配发行版差异（`cmake/WlrootsCompat.cmake`）：

| 场景 | 处理 |
|---|---|
| 系统 wlroots 0.19.x 且头 C++ 兼容 | 直接使用系统包 |
| 系统 wlroots 0.19.x 但头为上游原版（C99 `[static N]`、`class`/`namespace` 关键字） | **构建目录自动生成补丁头副本**并以 `-I` 优先注入（不改系统文件） |
| 系统 wlroots 缺失或版本 ≠ 0.19（Debian trixie 0.18 / Ubuntu 0.17 / Arch 0.20） | **自动用 meson 编译 vendored 源码**（`third_party/wlroots`，头已打补丁）到构建目录 |
| libdrm pkg-config 名 | `drm`（Debian 系）↔ `libdrm`（Arch）自动回退 |
| LayerShellQt target | `LayerShellQt::Interface` ↔ `LayerShellQt::LayerShellQt` 自动兼容 |

策略选项：`-DW10DE_WLROOTS_STRATEGY=auto`（默认）/ `system` / `vendored`。
vendored 编译需要 `meson` + `ninja` + wlroots 构建依赖。

### 从源码编译（无视发行版环境，`cmake/DepSource.cmake` + `DepsCompat.cmake`）

wlroots 生态的**全部库依赖**（wayland / wayland-protocols / libdrm / pixman /
libxkbcommon / libdisplay-info / libliftoff / libseat / libevdev / libinput /
hwdata / mesa(egl+glesv2+gbm，GL 用) / cairo / pango）实现 **系统优先、缺失或
版本不符时自动从固定版本源码编译** 的兜底机制：

- 每个依赖先 `pkg-config` 探测系统（版本须 ≥ 要求值），满足 → 直接用系统包；
- 不满足 → 从固定版本 tarball URL 下载 → `meson` 编译安装到
  `build/_deps/prefix` → 注入 `PKG_CONFIG_PATH`（后续依赖与项目均可见）；
- 增量：已构建依赖写 stamp，重复配置不重编；下载缓存于 `build/_deps/downloads`。

策略开关：

| 选项 | 语义 |
|---|---|
| `-DW10DE_DEP_SOURCE=auto`（默认） | 缺啥编啥（系统满足走系统） |
| `-DW10DE_DEP_SOURCE=always` | 全部依赖从源码编译（无视系统包） |
| `-DW10DE_DEP_SOURCE=never` | 缺则报错（不自动编译） |
| `-DW10DE_FORCE_SOURCE_DEPS=a;b;c` | auto 下强制指定依赖走源码（验证用） |
| `-DW10DE_DOWNLOAD_SELECT=fastest`（默认） | 下载前逐源测延迟，选最优源 |
| `-DW10DE_DOWNLOAD_SELECT=ordered` | 按 URL 列表顺序尝试 |
| `-DW10DE_DOWNLOAD_PROBE_MS=4000` | 源探测超时（毫秒） |

下载源（每个依赖的 URL 为分号分隔的多源列表，逐源尝试 + 延迟排序）：

- **gitlab.freedesktop.org** 官方（release/archive 两种，均已实测；原独立域名
  dri/cairographics/xkbcommon/mesa/download.gnome.org 在部分网络不可达，已全部
  改为 gitlab archive，tag 名带项目前缀如 `libdrm-2.4.123`/`pixman-0.43.4`）；
- **GitHub 官方 mirror 仓库**（[gitlab-freedesktop-mirrors](https://github.com/gitlab-freedesktop-mirrors)
  的 wayland/wayland-protocols、[mirror/mesa](https://github.com/mirror/mesa)、
  [GNOME/pango](https://github.com/GNOME/pango)）+ **gh-proxy.com 加速前缀**
  （国内可直连，已实测）——为 wayland/wayland-protocols/mesa/pango/hwdata 配置；
- **国内无 fdo release 专用镜像站**（TUNA/USTC/阿里实测均无）；
- **离线构建**：把 tarball 预置到 `build/_deps/downloads/` 即跳过下载。

下载健壮性：源探测用 `--range 0-0` 实测（gitlab 对不存在 ref 返回 200+HTML，
探测与下载后均做**压缩包魔数校验**（gzip/xz/bz2/tar/zip），HTML 错误页自动
拒绝并切换下一源。

说明：
- 源码编译需要 `meson` + `ninja` + `curl`（下载；CMake 内置 TLS 在部分环境
  证书校验失败，已实测 WSL/Arch 报 SSL connect error，故优先系统 curl）；
- `libudev`（libinput/mesa 的 udev 依赖）假定系统提供（主流发行版必有）；
- wlroots 的 renderers/backends/allocators 默认 auto：GL（egl/glesv2/gbm）或
  libseat 缺失时**自动降级**（pixman 渲染 + shm 分配器），headless 无 GL 也可用；
  DRM 真机需要 GL 时系统装 mesa 或 `-DW10DE_DEP_SOURCE=always` 编译 mesa
  （最小集：swrast、无 LLVM，约 20-40 分钟）；
- Qt6/layer-shell-qt 从源码编译（superbuild 扩展）为后续工作项；缺失时可用
  `-DW10DE_BUILD_SHELL=OFF` 仅构建 compositor。

### 各发行版依赖安装

```bash
# Arch Linux
sudo pacman -S wlroots0.19 libdrm libxkbcommon wayland qt6-base qt6-wayland \
    layer-shell-qt cmake ninja meson base-devel
# 注：Arch 的 wlroots0.19 头为上游原版，构建系统会自动打 C++ 补丁；
# 若仓库已升至 0.20，自动走 vendored 编译。

# Fedora 43+
sudo dnf install wlroots-devel layer-shell-qt-devel qt6-qtbase-devel \
    qt6-qtwayland-devel libdrm-devel libxkbcommon-devel wayland-devel \
    meson ninja-build cmake gcc-c++

# Debian 13 trixie / sid（wlroots 0.18，需 vendored 编译）
sudo apt install meson ninja-build cmake g++ pkg-config \
    libwlroots-dev libdrm-dev libxkbcommon-dev libwayland-dev \
    qt6-base-dev layer-shell-qt \
    libinput-dev libdisplay-info-dev libseat-dev libliftoff-dev \
    hwdata libudev-dev libegl-dev libgles2-dev libgbm-dev

# openSUSE Tumbleweed（包名以发行版仓库为准，vendor 策略可兜底）
sudo zypper install wlroots-devel layer-shell-qt-devel qt6-base-devel \
    qt6-wayland-devel libdrm-devel libxkbcommon-devel wayland-devel \
    meson ninja cmake gcc-c++
```

### M0/M1 冒烟验证

```bash
# M0：headless 运行 5 帧后截图并校验中心像素（应输出 pixel verification passed）
./build/src/compositor/w10compositor --frames 5 --screenshot /tmp/w10de-m0.png

# M1：嵌套运行（在另一个 Wayland compositor 中，如 WSLg/weston），
# 开窗口验证 xdg-shell 与输入：
WLR_BACKEND=wayland ./build/src/compositor/w10compositor --frames 0
# 另开终端：WAYLAND_DISPLAY=<上面的 socket> weston-terminal 之类
```

- headless 预期：正常退出（exit 0），日志含 `pixel verification passed`，
  生成 1920x1080 纯色 Win10 蓝（#0078D7）PNG。
- 快捷键：`Win+Q` 关闭焦点窗口，`Win+M` 最小化，`Win+F` 最大化，`Win+Esc` 退出，
  `Win+1..4` 切换工作区（M7 续），`Win+←/→` 左/右半屏贴边（Aero Snap，M8），
  `Win+↑` 最大化、`Win+↓` 还原（M8）。
- headless 验证参数：`--workspace <n>` 启动工作区、`--switch-ws <frame>:<ws>` 定时
  切换（可重复）、`--snap-test` 首窗口自动贴左半屏。
- 其他参数见 `--help`。

### 代码与 API 参考

`third_party/wlroots/` 为 wlroots 0.19.0 源码（git 克隆，tag `0.19.0`），
用作 API 对照（wlroots 头文件无 `extern "C"` 保护，C++ 引用需手动包裹）。

### 首次真实编译（2026-08，Arch/WSL2）—— 发现并修复

项目在 WSL2 Arch 上完成**首次真实编译 + headless 冒烟**（此前全部为静态审查，从未编译）。编译过程发现并修复：

- **wlroots 头 C++ 兼容补丁（vendored，3 头 + 1 生成头）**：
  - `wlr_scene.h`：`const float color[static 4]`（C99 语法，**g++16 不接受**，实测验证）→ `color[4]`（2 处）
  - `xwayland.h`：`char *class`（C++ 保留字）→ `class_`
  - `wlr_layer_shell_v1.h` + 生成的协议头：`char *namespace`（C++ 保留字）→ `namespace_`
  - 补丁同时应用到 `/usr/local` 安装头（重新编译/安装时需再次应用，见 HANDOFF）
- **`wl_container_of` + `auto` 自引用**（C++ 特有，57 处）：新增 `W10DE_CONTAINER_OF(ptr, Type, member)` 宏（`src/compositor/util.h`）
- **wlr 头缺 `extern "C"` 保护**：server.h/seat.h/view.h/xview.h/layer_shell.h/output.h 的 wlr include 全部手动包裹（server.cpp 原本已包）
- **layer-shell-qt API**：`setAnchors` 需显式 `Anchors(...)` 构造（普通枚举 `|` 得 int）；CMake target 为 `LayerShellQt::Interface`（非 `LayerShellQt::LayerShellQt`，兼容两者）
- **Qt 6.11 头布局**：qpa 头移入版本化目录 `QtGui/6.11.2/QtGui/qpa/`（环境符号链接处理，见 HANDOFF）；`nativeInterface` 为**非静态成员**（`qGuiApp->nativeInterface<...>()`）；`QDBusArgument::InvalidType` 已改名 `UnknownType`
- **wlroots 0.19 API 核实**：无 `wlr_scene_destroy`（用 `wlr_scene_node_destroy(&scene->tree.node)`）；无 `WLR_WARNING`（改用 `WLR_INFO`）；`wl_display_add_socket_auto` 返回**内部指针不可 free**（注释原假设 strdup 是错的，真实崩溃验证）；headless 后端无 vsync，须每帧 `wlr_output_schedule_frame`（否则渲染停在第二帧）
- **构建配置**：libdrm 的 pkg-config 名 Arch 为 `libdrm`（Debian 系为 `drm`，CMake 已回退）；`w10compositor` 显式链接 `wayland-server`（wlroots.pc 的 Requires.private 不传递）；shell include 补 `src/shell` 路径
- **运行时容错**：XWayland 创建失败降级为警告（WSL/headless 无 X11 环境不致命）
- **Qt API**：`QString::replace` 不支持 lambda 回调（sanitizeExec 改 globalMatch 手动拼接）；sniwatcher 补 `QDBusMessage` include 与 slot 声明

**冒烟结果**：`w10compositor --frames 5 --screenshot` → 1920x1080 PNG + `pixel verification passed (center = #0078D7)`，退出码 0。

**wlroots C++ 补丁策略**（跨发行版，`cmake/` 自动处理）：上游 wlroots 头含 C99 `[static N]` 与 C++ 保留字（`class`/`namespace`），原版头在 C++ 下编译失败（g++16 实测）。构建系统**编译原版源码**（C 编译头+实现自洽），安装/系统头由 `WlrootsPatchHeaders.cmake` 在**构建目录生成补丁副本**并以 `-I` 优先注入——不改系统文件、不影响其他项目。详见"跨发行版兼容"节。

**完整渲染验证**（compositor + w10shell 同跑，固定 socket）—— 又发现并修复：
- **xdg-decoration 时序**：`set_mode(SERVER_SIDE)` 在 surface 未初始化时触发 `wlr_xdg_surface_schedule_configure` 断言崩溃（gdb 定位；Qt 在首 commit 前请求 decoration）→ 未初始化则挂 commit 监听延迟设置（单槽串行，MVP 够用）
- **layer surface 死锁**：`arrangeLayers` 按 `mapped` 过滤导致**未 map 表面永远收不到首次 configure** 而无法 map（`wlr_layer_surface_v1_configure` 只 assert `initialized`）→ 仅按 `!initialized` 过滤
- **layer-shell-qt / Qt 6.11 时序**：`show()` 后再 `Window::get()` 报 "already has a shell integration"（QPA 不允许事后切换）→ 改 `winId() → get/配置 → show()`；`useLayerShell()` 在 Qt 6.5+ 为废弃 no-op
- **截图校验过时**：中心==纯背景色的 M0 假设在有 shell 内容时误判（壁纸渐变覆盖中心 #0073CD）→ 改为"内容多样性检测"（多色即通过，纯色才校验背景）
- **开始按钮发行版图标**：`/usr/share/pixmaps/archlinux-logo.svg`（Arch 品牌蓝 #1793D1 渲染确认），缺失回退"开始"文字；按钮 48×48 正方形（1:1、与任务栏同高）、`padding:0` 贴屏幕最左（按钮左缘 x=0 实测），**图标保持原本 26×26 大小固定不变**（按钮内居中）
- **开始菜单 Win10 布局重构**（渲染验证）：三列——左侧窄栏 48px（#171717 略深，顶部 ☰ 汉堡展开/折叠 200px、底部功能区：账户→设置/文档/图片→电源最底，电源弹关机/重启/睡眠菜单 MVP 占位）、**应用列表列 240px（5×开始按钮宽）**、**磁贴区 288px（6×开始按钮宽）**；总宽 576（x=48/576 分区边界实测精确）
- **磁贴四种尺寸**（`TileButton`，右键菜单自由设置，`FlowLayout` 流式排布）：小 48×48 / 中 **100×100** / 大 **204×204** / 宽 **204×100**（4px 网格基准：2 小+1 间隙 / 4 小+3 间隙）；磁贴区 **316px**（6 小磁贴 + 7×4px 间隙，左右边缘 4px）、水平与行间距均 **4px**；每行 3 个中磁贴（实测 100px 磁贴、4px 间隙/边缘精确）
- **FlowLayout 宽度健壮性**：布局宽度取父 widget 实际宽度（`setGeometry` 的 rect 在 layer-shell 显示时序中不稳定 100↔288，按 rect 排布会错误换行——真实运行验证）
- **开始菜单与任务栏对齐**：`margin.bottom` 从 kTaskbarHeight 改为 **0**——overlay 层 bounds 是可用区（已排除任务栏独占区），双重避让导致 49px 空隙（实测 49px→1px）
- 结果：桌面壁纸渐变 + 任务栏（#2D2D2D）渲染成功，238 色采样，`pixel verification passed (content rendered)`
- **电源/账户接线**（2026-08）：电源菜单（关机/重启/睡眠）执行 **systemctl**（poweroff/reboot/suspend，systemd 环境）；账户按钮 → D-Bus `org.w10de.Shell.Lock()` → w10lock 锁屏（**首次端到端验证**：busctl 调用 → w10lock 启动 → compositor `session locked`）
- **LockService D-Bus 接口修复**：需显式 `Q_CLASSINFO("D-Bus Interface", "org.w10de.Shell")`（默认接口名是类名，外部 dbus-send/busctl 调用不到——实测发现）；w10lock 定位增强（PATH 优先 + /usr/local/bin 兜底）

### M2b / M7 续 / M8 开发与验证（2026-08，headless）

- **M2b 标题栏文字**（`src/compositor/titletext.{h,cpp}`）：cairo/pango 渲染 ARGB32
  预乘像素 → 自实现 `wlr_buffer`（`wlr_buffer_init` + DATA_PTR access）→
  `wlr_scene_buffer` 自动上传纹理。白字 #FFFFFF、单行、超长省略（`pango_layout_set_width`
  触发 ellipsize）、垂直居中。CMake 链接 `PkgConfig::CAIRO/PANGO`。
  验证：标题栏 #2D2D2D + 白字 385 像素 + 关闭钮红 1472 像素（窗口 640×480 at 100,80）。
- **xdg-shell 初始 configure 修复**（关键协议 bug）：wlroots 0.19 的 `create_xdg_toplevel`
  **不自动调度初始 configure**——compositor 必须在 xdg_surface 首次 commit
  （`initial_commit`）后调用 `wlr_xdg_toplevel_set_size(0,0)`（tinywl 同款处理），
  否则客户端永久卡在等 configure、窗口永不 map（真实运行定位：窗口创建但无
  "view mapped"、截图纯蓝）。
- **M2b 审查修复**（子代理审查 4 中等项 + 若干轻微项）：窄窗口（textW≤0）时清空
  标题文字 buffer 防止覆盖按钮（`wlr_scene_buffer_set_buffer(node, NULL)`）；空标题/
  清空标题清旧文字；`titleText_` 为空时宽度变化也触发渲染；hover 感知 overlay/top
  层表面遮挡；标题栏装饰区不再 fallthrough 到底层（滚轮/右键不落桌面）；拖动结束
  补 hover 刷新；文字节点 z 序移至按钮之下（标题栏背景→文字→按钮）。窄窗口
  （100×100）验证：0 白字 + 关闭钮红可见。
- **M7 续 多工作区**：`View/XView::workspace_` 归属（创建时取当前工作区）、
  `Compositor::switchWorkspace/moveViewToWorkspace/moveXViewToWorkspace/focusWorkspaceTop`、
  统一可见性 `applyVisibility()`（mapped && !minimized && workspace==current）。
  命中检测（viewAt/surfaceAt）过滤非当前工作区窗口。headless 验证 4 场景
  （`--workspace`/`--switch-ws`）：默认可见 / 他区隐藏 / 切走隐藏 / 切回可见 全部 PASS。
- **M7 续 XWayland SSD + 任务栏**：XView 增加与 xdg View 同款装饰树（标题栏+按钮+
  标题文字+阴影）、foreign-toplevel handle（任务栏窗口列表）、标题栏拖动/按钮交互
  （Seat 双窗口类型统一 hover/命中）、`set_class` → app_id、override-redirect 窗口
  跳过装饰与任务栏（X11 菜单/提示）。WSL headless 无 XWayland，仅静态审查 + 编译验证。
- **M8 视觉打磨**：
  - **窗口阴影**（`src/compositor/shadow.{h,cpp}`）：自绘 ARGB8888 预乘渐变环
    （切比雪夫距离线性衰减，内缘 α≈0.36 → 外缘 0），挂在装饰树最底层（覆盖窗口外
    8px）。验证：窗口外侧阴影带显著暗于背景、内容区无污染。
  - **Aero Snap**：`Win+←/→` 左/右半屏（保存 restore 几何，可还原）、`Win+↑` 最大化、
    `Win+↓` 还原（最大化/贴边均恢复）。最大化与贴边互斥。验证（`--snap-test`）：
    窗口贴左半屏（960×1048 at 0,0）、右半屏无内容、标题栏在 (0,0)。
  - **动画**：Snap/还原平滑移动（帧插值 ease-out，每帧 0.12 推进；`tickAnimations`
    由输出帧循环驱动；拖动开始即取消动画）。
  - **圆角**：遵循 Win10 直角设计（窗口无圆角）；圆角用于 UI 元素——开始菜单磁贴
    等 Qt 侧已有 2px `border-radius`（`src/shell/startmenu/tilebutton.cpp`）。
- **M7 续审查修复**（子代理审查 2 严重 + 8 中等 + 若干轻微）：remap 到非当前工作区
  的窗口不再获得焦点/激活/置顶（焦点泄漏到隐藏窗口）；跨类型焦点统一（xdg 获焦时
  XWayland 窗口同步失活、反之亦然，`focusSurface/focusView/unfocusAll` 全路径）；
  `focusWorkspaceTop` 考虑 XView、不重排全局 z 序；`moveXViewToWorkspace` 清理
  焦点/激活残留；任务栏激活跨工作区窗口先切换桌面；XView map 置顶。
- **M8 审查修复**（子代理审查 2 严重 + 3 中等 + 若干轻微）：snap→最大化 时保留
  restore 几何并取消动画（原实现 unsnap 启动返回动画且消费恢复点，导致最大化
  窗口被拉回/还原尺寸错误）；maximized→snap 时恢复尺寸竞态（异步 resize 未 ack
  前 width() 是最大化值，改为拷出恢复目标再落回）；XWayland override-redirect
  中途切换后装饰节点指针全量置 null（shadowNode_ 悬垂导致 commit 时 UAF）；
  `beginResize` 补 cancelAnimation；快捷键仅纯 LOGO 组合（排除 Shift/Ctrl/Alt）；
  多输出下动画仅第一输出推进（速度不翻倍）；unmap/dissociate 取消动画；
  shadow.cpp 用 W10DE_CONTAINER_OF + nothrow 分配。全部修复后回归验证通过。
- **主题功能**（2026-08）：新增 `src/ipc/theme.{h,cpp}`（共享主题定义，无 Qt 依赖）
  与 `src/shell/theme/colors.cpp`（shell 侧加载）。`~/.config/w10de/config.ini` 的
  `[theme]` 段：`mode = dark|light` 预设 + 14 个颜色键覆盖（`taskbar_bg`/`menu_bg`/
  `menu_sidebar`/`titlebar_bg`/`button_bg`/`button_hover`/`close_bg`/`close_hover`/
  `text_primary`/`text_secondary`/`hover_bg`/`pressed_bg`/`accent`/`desktop_bg`，
  `#RRGGBB` 格式）。compositor（标题栏/按钮/hover/标题文字/桌面背景/截图校验）
  与 w10shell（任务栏/开始菜单/时钟/窗口列表/托盘）读同一配置，两进程视觉一致。
  验证：深色默认（回归白字 385/红钮 1472/任务栏 #2D2D2D）、浅色模式（任务栏与
  标题栏 #F3F3F3 + 深色文字，无纯白残留）、自定义键覆盖（`taskbar_bg=#123456`、
  `titlebar_bg=#654321` 双进程生效）全部 PASS。
- **主题审查修复**（AgentTeams 三成员审查：compositor 核心/shell 接入/交叉一致性，
  t1-t3）：无严重问题；修复 5 中等 + 若干轻微——compositor 空配置回退深色预设
  （原为全黑）；w10shell 增加 `--config` 参数与 compositor 对齐（自定义路径时
  主题/壁纸不分叉）；新增 `accent_text` 键（任务栏激活高亮文字固定白，浅色下
  黑字 on #0078D7 仅 3.87:1 对比不足）；桌面图标区主题化（文字/高亮跟随主题，
  原硬编码白色）；浅色预设菜单背景调浅 #F0F0F0（与磁贴 #E5E5E5 可辨）；注释/
  配置示例同步（menu_sidebar/accent_text 键、非法值回退语义、mode 语义）。
  修复后深色/浅色/自定义/完整渲染四组验证全部 PASS。

### 已知待验证项（编译时确认）

- ~~`const float color[static 4]` C++ 接受度~~ ✅ 已验证：g++16 不接受，vendored 补丁解决
- wlroots 0.19 与新版 libinput（Arch 1.31）的枚举兼容：`LIBINPUT_SWITCH_KEYPAD_SLIDE` 未处理被 `-Werror` 拦截 → 编译 wlroots 用 `-Dwerror=false`
- 嵌套运行（`WLR_BACKEND=wayland` 开窗口）与 DRM 真机验证：待 WSLg/真机
- 已执行三轮独立代码审查（前两轮各 4 个子代理并行 + 人工，第 3 轮最终验证
  4 组并行 + 输入层重拉）并修复，三轮共发现 **92 个问题**（10 CRITICAL +
  15 HIGH + 32 MEDIUM + 35 LOW），全部修复或标注（5 项为可接受风险未修）：
  - 编译错误：XKB 键符头文件缺失（`xkbcommon-keysyms.h`）、`wl_pointer_button_state`
    枚举类型不匹配、不存在的 `minimize()`/`request_close`、`wlr_cursor_*` 参数类型、
    `createForeignToplevel` 未调用（任务栏协议失效）、main.cpp lambda 捕获未声明
    变量（`i`）、`firstOutput()` 内联调用不完整类型 Output（移入 server.cpp）
  - 运行崩溃：`new_surface` 时 arrange 触发 wlroots assert、`arrangeLayers` 对未
    初始化/已 unmap 表面触发 assert、析构顺序、构造失败路径 listener、键盘热插拔
    UAF（含第 3 轮补 `wlr_seat_set_keyboard(seat_, nullptr)`）、重复 remove、
    拖动中窗口销毁悬垂、锁屏 listener 未摘除触发 wlroots assert
  - 逻辑错误：层锚 z 序、多输出 scene output 位置、最大化恢复几何（含 map 前请求
    最大化）、最小化窗口激活恢复、raiseView 真正置顶（先内容后装饰）、resize 左/
    上边与最小尺寸联动、窄窗口按钮、remap 位置、serial 校验、幽灵 release、双重
    frame 事件、strtol 溢出
  - 第 3 轮（最终验证，25 项）补充修复：拖动结束后首次点击被吞（press 转发前
    补 enter）、层表面 commit 修改 layer 时 scene 节点重挂锚（否则 z 序与命中
    不一致）、`handleLockDestroy` 禁用锁屏场景节点、`handleLockUnlock` 摘除
    lock surface 监听、output 构造失败路径销毁 `sceneOutput_`、析构补
    `wlr_xwayland_destroy`、锁客户端 unlock 分支/双 buffer/size_t 溢出、
    `--help` 补 `--config`、QModelIndex 前向声明、NameOwnerChanged 监听等

## 依赖与许可证（上游合规）

遵守所有上游开源项目的许可协议：

- **Qt 6 / layer-shell-qt 仅动态链接**（LGPL 义务：不静态链接、不修改上游源码、保留许可声明）；
- `third_party/` 的 vendored 源码（wlroots、stb）与 `protocols/` 的协议 XML
  **原样保留上游版权声明**；
- 全部依赖无传染性 copyleft **库**链接，本项目采用 **MIT 协议**（见根目录
  [LICENSE](LICENSE)），与全部上游依赖完全兼容。

| 上游项目 | 用途 | 许可证 | 使用方式 |
|---|---|---|---|
| [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) 0.19.0 | compositor 核心（vendored） | MIT | 源码编译 |
| [stb_image_write](https://github.com/nothings/stb) 1.16 | 截图 PNG 输出（vendored） | Public Domain | 头文件内联 |
| [wayland-protocols](https://gitlab.freedesktop.org/wayland/wayland-protocols) `ext-session-lock-v1` | 锁屏协议（vendored XML） | MIT | wayland-scanner 生成 |
| [wayland](https://gitlab.freedesktop.org/wayland/wayland) | 协议库 + wayland-scanner | MIT | 动态链接 |
| [libdrm](https://gitlab.freedesktop.org/mesa/drm) | DRM 格式头 / 库 | MIT | 动态链接 |
| [libxkbcommon](https://github.com/xkbcommon/libxkbcommon) | 键盘 xkb 解析 | MIT/X11 | 动态链接 |
| [Qt 6](https://www.qt.io/)（Widgets / WaylandClient / DBus） | shell UI、锁屏渲染 | LGPL-3.0（或 GPL-3.0/商业） | **动态链接** |
| [layer-shell-qt](https://invent.kde.org/plasma/layer-shell-qt)（KDE） | layer-shell 客户端绑定 | LGPL-2.1-only OR LGPL-3.0-only OR KDE-Accepted-LGPL | **动态链接** |
| [XWayland](https://www.x.org/)（X.Org Server） | X11 应用兼容 | MIT/X11 | 运行时进程 |
| [D-Bus](https://www.freedesktop.org/wiki/Software/dbus/)（dbus-daemon） | 系统服务总线（QtDBus 调用） | AFL-2.1 OR GPL-2.0-or-later | 运行时进程（非链接） |

wlroots 构建期依赖（间接，随发行版安装）：pixman / libinput / libdisplay-info /
libliftoff / libseat / wlr-protocols / Mesa（EGL·GLESv2·GBM）— **MIT**；
libudev（systemd）— **LGPL-2.1+**；hwdata（pnp.ids 数据，仅 DRM 后端运行时
读取）— **GPL-2.0-or-later**（数据文件，无传染）。

> 详细核实证据与合规清单见 [DEPENDENCIES-LICENSES.md](DEPENDENCIES-LICENSES.md)。

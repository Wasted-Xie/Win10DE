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

**M1 + M2a + M3 前置（layer-shell）编码完成，待 Linux 构建环境验证**（当前工作区为 Windows，无法编译）。

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
- [ ] 编译与冒烟验证（headless 运行 + 截图 + 像素校验）
- [ ] M7 续：多工作区；XWayland 装饰/任务栏集成（M8）
- [ ] M2b 标题栏文字渲染（cairo/pango）与交互打磨
- [ ] M7 会话集成 / XWayland / 多工作区
- [ ] M8 视觉打磨（圆角、阴影、动画、Aero Snap）

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
`wlroots 0.19`（含 wlr-protocols）、`libdrm`、`wayland`、`xkbcommon`。

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
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
- 快捷键：`Win+Q` 关闭焦点窗口，`Win+M` 最小化，`Win+F` 最大化，`Win+Esc` 退出。
- 其他参数见 `--help`。

### 代码与 API 参考

`third_party/wlroots/` 为 wlroots 0.19.0 源码（git 克隆，tag `0.19.0`），
用作 API 对照（wlroots 头文件无 `extern "C"` 保护，C++ 引用需手动包裹）。

### 已知待验证项（编译时确认）

- `wlr_scene_rect_create` 的 `const float color[static 4]` 是 C99 数组参数语法，
  C++ 标准不支持；wlroots 0.19 已被 wayfire 等 C++ 项目使用，多数编译器按扩展接受，
  但需在真实 Linux 环境编译确认。
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

# Win10DE 架构设计文档

> 状态：设计阶段（v0.1）｜构建环境待定（先设计后补环境）
> 目标：在 Linux 上从零实现一个 Win10 风格桌面环境

---

## 1. 项目概述

Win10DE 是一个运行于 Linux 的 Windows 10 风格桌面环境，从零实现，技术栈为 **C++20 + wlroots 0.19 + Qt 6.5+**。

功能范围（MVP 及演进）：
- 窗口管理器（移动 / 缩放 / 最小化 / 最大化 / 关闭 / 聚焦）
- Win10 风格服务端窗口装饰（标题栏、控制按钮）
- 任务栏 + 开始菜单（磁贴 + 应用列表）
- 桌面壁纸与桌面图标
- 系统托盘（StatusNotifierItem + 传统 XEmbed 兼容）
- 锁屏

产品形态对标：labwc（轻量 wlroots compositor）+ Plasma 式 shell，但 UI 全部自研为 Win10 视觉。

---

## 2. 总体架构

```
┌──────────────────────────────────────────────────────────────┐
│  w10-session（会话启动器脚本）                                │
│  设置环境变量 → 启动 seatd/logind 权限 → 启动 compositor      │
├──────────────────────────────────────────────────────────────┤
│  w10compositor 进程（wlroots C API + C++ 封装）               │
│  ├─ 后端 backend：DRM（真机） / headless（无头测试）          │
│  │                 / wayland（嵌套于 WSLg 或其他 compositor） │
│  ├─ 渲染：wlr_scene 场景图（wlr_renderer / EGL）              │
│  ├─ 视图：xdg-toplevel / xwayland / layer-shell surface       │
│  ├─ SSD：服务端装饰（Win10 风格标题栏）                       │
│  ├─ 工作区（虚拟桌面）管理与窗口布局                          │
│  ├─ seat：键盘 / 指针 / 触摸 / 焦点 / 拖放                    │
│  └─ IPC：D-Bus 服务（w10.manager）                            │
├──────────────────────────────────────────────────────────────┤
│  w10shell 进程（Qt Widgets，layer-shell 客户端）              │
│  ├─ 任务栏：开始按钮 / 运行中窗口 / 固定应用 / 托盘区 / 时钟  │
│  ├─ 开始菜单：Win10 磁贴 + 应用列表                           │
│  ├─ 桌面：壁纸（background layer）+ 图标（QListView）          │
│  └─ 锁屏：独占层 + input-inhibitor                            │
├──────────────────────────────────────────────────────────────┤
│  托盘：w10tray（StatusNotifierItem D-Bus 服务端 + XEmbed）    │
└──────────────────────────────────────────────────────────────┘
```

设计原则：
1. **compositor 与 shell 分离**：compositor 只做窗口管理与合成；一切 UI 走 layer-shell 客户端。二者通过 Wayland 协议 + D-Bus 通信，进程崩溃互不影响。
2. **协议先行**：优先使用 wlr-protocols 提供的现成协议（layer-shell / foreign-toplevel / input-inhibitor / server-decoration），少自造轮子。
3. **可测试性**：headless backend + 截图断言作为持续集成手段；wayland backend 用于嵌套开发。

---

## 3. 关键技术决策

| 决策点 | 选择 | 理由 | 备选 |
|---|---|---|---|
| 合成器路线 | wlroots 0.19（C API，C++ 封装） | 生态成熟、backend/scene/seat 齐全，layer-shell 等协议现成 | QtWaylandCompositor（文档少、坑多）；自写 Wayland 协议栈（不可行） |
| 渲染 | wlr_scene 场景图 | wlroots 官方推荐路径，自动处理 surface 树与伤害重绘 | 手写 render pass（复杂、没必要） |
| shell UI 技术 | Qt 6 Widgets（layer-shell 客户端） | 用户指定 Qt/C++；Widgets 便于像素级还原 Win10 控件 | QML（性能好但风格定制成本高）；GTK（违背选型） |
| layer-shell 绑定 | **KDE layer-shell-qt**（`LayerShellQt::Window`） | Qt 6.5+ 原生 `QWaylandLayerShell` API 文档不足、不稳定；layer-shell-qt 是 Plasma 生产方案、API 稳定 | Qt 原生 QWaylandLayerShell（成熟后迁移）；自写绑定（与 Qt 渲染管线集成困难，不可行） |
| 窗口装饰 | 服务端装饰（SSD），Win10 风格 | 统一视觉；Qt 客户端可禁用 CSD | 客户端装饰（无法统一风格，弃） |
| 任务栏窗口列表 | wlr-foreign-toplevel-management 协议 | wlroots 官方提供，标准做法（swaybar 同款） | 自建 D-Bus 窗口清单（重复造轮子） |
| 锁屏 | layer-shell 独占层 + wlr-input-inhibitor | 协议层强制（其他输入被屏蔽），无需 hack | 仅靠 Qt 全屏置顶（可绕过，不安全） |
| 托盘 | StatusNotifierItem（D-Bus 主）+ 传统 XEmbed 兜底 | 现代 Linux 标准（KDE/GNOME 通用） | 仅 XEmbed（老应用兼容，需实现托盘宿主） |
| 跨进程通信 | D-Bus（应用启动、配置、会话事件） | 标准、可调试（busctl）、无耦合 | 自定义 unix socket（调试成本高） |
| X11 兼容 | XWayland | 大量应用仍为 X11，必须支持 | 无（不可接受） |
| wlroots 版本策略 | 锁定 0.19.x（git submodule 或发行版包） | wlroots API 变化频繁，锁版保证可复现构建 | 追 git master（风险高） |

---

## 4. 协议选型（wlr-protocols）

| 协议 | 用途 | 说明 |
|---|---|---|
| `wlr-layer-shell-unstable-v1` | 任务栏 / 桌面 / 锁屏 surface | 层：background / bottom / top / overlay；独占 overlay 用于锁屏 |
| `wlr-foreign-toplevel-management-unstable-v1` | 任务栏窗口列表、最大化/最小化/关闭操作 | compositor 侧由 wlroots 提供 |
| `wlr-input-inhibitor-unstable-v1` | 锁屏期间屏蔽全部输入 | 由 wlroots 提供 |
| `ext-server-decoration`（或 `wlr-server-decoration`） | 声明/协商服务端装饰 | 客户端可请求 SSD；MVP 可先忽略客户端请求直接统一 SSD |
| `wlr-virtual-keyboard` | 屏幕键盘（后期） | 暂列 |
| `wlr-output-management` | 显示器设置（后期 UI） | 暂列 |
| XWayland | X11 应用 | wlr_xwayland |

> 注：0.18 起 layer-shell 等协议从 wlroots 拆分为独立的 `wlr-protocols` 包，构建时需同时依赖。

---

## 5. 渲染与装饰

- **场景图**：`wlr_scene` 为根；每个 view 一个 `wlr_scene_tree`；输出按输出布局组织。
- **标题栏（SSD）**：Win10 风格（高度 ~32px，含图标、标题、最小化/最大化/关闭按钮）。
  - 方案 A（首选）：decoration 作为 scene 中 view 之上的合成节点，用 `wlr_renderer` 直接绘制（矩形 + 文本 + 图标），拖拽由 compositor 的 seat 处理。
  - 方案 B：把标题栏画成 view 内容的一部分（buffer 上合成），缺点是与客户端内容绑定、圆角阴影处理复杂。
  - 圆角/阴影：MVP 不做或做简单直角 + 1px 边框；Win10 的 4px 圆角与阴影留到 M8 视觉打磨（wlr_scene 的 shadow 支持有限，届时评估）。
- **伤害重绘**：信任 wlr_scene 自带的 damage 追踪，不自行管理。

---

## 6. 窗口管理模型

- **浮动式**（Win10 风格）：无自动平铺。窗口初次映射按层叠定位。
- **窗口状态**：normal / minimized / maximized / fullscreen；快照支持 Win10 式贴边分屏（Aero Snap，M7+）。
- **工作区**：MVP 单工作区；M7 增加多工作区（任务栏"任务视图"入口）。
- **焦点模型**：点击聚焦 + 标题栏交互；`wlr_seat` 统一键盘/指针焦点。

---

## 7. IPC 设计（D-Bus 服务 `org.w10de.Manager`）

供 shell / 外部工具调用：

| 接口 | 方向 | 示例 |
|---|---|---|
| `StartApp(desktop_file)` | → compositor | 开始菜单启动应用（compositor 调 xdg 执行或由 shell 直接 spawn） |
| 窗口列表 / 焦点事件 | compositor → shell | 由 foreign-toplevel 协议承担，D-Bus 不重复 |
| `SetWallpaper(path)` | → compositor/shell | 切换壁纸 |
| 配置读写（`org.w10de.Config`） | 双向 | INI/TOML，`~/.config/w10de/` |
| 会话动作（Lock / Logout / Suspend） | shell → 会话 | 锁屏由 shell 弹独占层 |

> 决策：窗口元数据一律走 Wayland 协议（foreign-toplevel），D-Bus 只管"应用级"操作，避免双份状态源。

---

## 8. 目录结构

```
Win10DE/
├── CMakeLists.txt              # 顶层构建（C++20）
├── README.md
├── docs/
│   └── ARCHITECTURE.md         # 本文档
├── src/
│   ├── compositor/             # wlroots compositor（C++ 封装）
│   │   ├── server.{h,cpp}      # display/backend/renderer/事件循环生命周期
│   │   ├── output.{h,cpp}      # 输出管理：模式/变换/布局
│   │   ├── view.{h,cpp}        # 视图抽象：xdg-toplevel / xwayland / layer
│   │   ├── decoration.{h,cpp}  # SSD 标题栏绘制与交互
│   │   ├── seat.{h,cpp}        # 输入、焦点、拖放
│   │   ├── workspace.{h,cpp}   # 工作区管理
│   │   └── main.cpp
│   ├── shell/                  # Qt Widgets UI（layer-shell 客户端）
│   │   ├── main.cpp
│   │   ├── taskbar/            # 任务栏：按钮/窗口列表/托盘区/时钟
│   │   ├── startmenu/          # 开始菜单：磁贴 + 应用列表
│   │   ├── desktop/            # 壁纸层 + 桌面图标
│   │   ├── lock/               # 锁屏
│   │   └── theme/              # Win10 主题：颜色/尺寸/字体常量 + 图标
│   ├── tray/                   # 托盘：SNI D-Bus 服务端 + XEmbed 宿主
│   ├── ipc/                    # D-Bus 接口定义（compositor/shell 共用）
│   └── session/                # 会话启动脚本、环境准备、autostart
├── protocols/                  # wlr-protocols XML（如需自生成绑定）
├── themes/                     # 壁纸、图标等资源
└── tools/                      # 开发脚本：headless 测试、截图比对
```

---

## 9. 里程碑（M0 起每步可运行、可验证）

| 里程碑 | 内容 | 验证方式 |
|---|---|---|
| **M0 骨架** | 目录/CMake/最小 compositor（headless 起 display、创建输出、可截图退出） | headless 运行 + `grim` 截图 |
| **M1 核心 compositor** | DRM/wayland/headless 三 backend；wlr_scene 渲染；xdg-shell 支持；seat 输入与焦点 | 嵌套运行开 Qt 测试窗；headless 截图 |
| **M2 窗口交互** | 移动/缩放/最小化/最大化/关闭；SSD 标题栏（Win10 风格） | 交互测试 + 截图 |
| **M3 shell：任务栏 + 开始菜单** | compositor 侧 layer-shell 支持已完成（层表面管理、层锚树 z 序）；Qt layer-shell 任务栏（foreign-toplevel 集成、时钟、托盘区）；开始菜单磁贴 | 嵌套运行手工验证 |
| **M4 桌面** | 壁纸层；桌面图标（QListView + 文件系统）；右键菜单 | 截图 + 交互 |
| **M5 系统托盘** | SNI 服务端 + XEmbed 宿主 | 运行 Qt 应用验证托盘图标 |
| **M6 锁屏** | 独占层 + input-inhibitor + 密码验证（PAM 或 MVP 简化） | 触发锁屏验证输入被屏蔽 |
| **M7 会话集成** | 会话启动器、autostart、配置系统、XWayland 完善、多工作区 | 完整登录会话冒烟 |
| **M8 视觉打磨** | 圆角/阴影、动画、Aero Snap、Win10 图标与细节 | 截图对比 Win10 |

> 当前进度：M0（headless compositor）+ M1（多后端/xdg-shell/seat）+ M2a（SSD 标题栏）
> + M3 前置（compositor layer-shell 支持）代码已编写，待构建环境就绪后编译验证；
> M3 shell 客户端（Qt layer-shell）未开始。

---

## 10. 构建与测试策略

### 依赖（目标发行版）

| 依赖 | Debian/Ubuntu | Arch |
|---|---|---|
| C++ 编译器 | g++ (>=12) | gcc |
| CMake/Ninja | cmake ninja-build | cmake ninja |
| wlroots 0.19 | libwlroots-dev（0.19 需 backports/sid） | wlroots |
| wlr-protocols | wlr-protocols | wlr-protocols |
| Qt 6 (Widgets/DBus) | qt6-base-dev libqt6svg6-dev | qt6-base |
| Wayland 客户端库 | libwayland-dev | wayland |
| xkbcommon | libxkbcommon-dev | libxkbcommon |
| XWayland | xwayland | xwayland |
| 调试工具 | grim slurp wayland-utils weston | grim slurp wayland-utils |

### 测试矩阵

| 模式 | backend | 用途 |
|---|---|---|
| headless | `WLR_BACKEND=headless` | CI、截图断言、无 GPU 冒烟 |
| 嵌套 | wayland backend（WSLg / weston 内） | 开发期交互验证，窗口显示在宿主桌面 |
| 真机 | DRM | 最终部署，需要 TTY 与 seatd/logind 权限 |

### WSL2 开发指引（待环境就绪后执行）
1. 安装发行版（Debian/Ubuntu 推荐，包齐全）。
2. 安装上述依赖（wlroots 0.19 可能需从 sid/源码编译）。
3. 构建：`cmake -B build -G Ninja && cmake --build build`。
4. headless 测试：`WLR_BACKEND=headless ./build/w10compositor` + 截图验证。
5. 嵌套测试：WSLg 提供 Wayland 环境，直接以 wayland backend 运行。

---

## 11. 风险与备选方案

| 风险 | 等级 | 缓解 / 备选 |
|---|---|---|
| Qt 的 layer-shell 支持（`QWaylandLayerShell`）API 不稳定或不足 | 中 | 备选：用 `wlr-layer-shell-unstable-v1.xml` 自生成绑定，在 Qt 客户端手动管理 surface；再备选：layer-shell-qt（KDE 库，但引入 KF 依赖） |
| wlroots 0.19 API 与文档不符（版本间破坏性变更） | 中 | 锁定版本 + submodule；以 wlroots 自带 example 为对照 |
| 无 GPU 的 headless 环境渲染验证受限 | 低 | wlr_renderer 支持软件渲染（pixman）；截图断言不依赖 GPU |
| 服务端装饰实现复杂度（文本/图标绘制在 compositor 侧） | 中 | 标题栏文字用 cairo/pango 或 Qt 离屏渲染成 texture；MVP 可先用简单字体渲染（wlr 无内置文本渲染，需引入 cairo+pango 或 stb_truetype） |
| 锁屏安全性（PAM 集成） | 低 | MVP 用轻量方式（不真正鉴权），M6 再评估 PAM |

> 需要决策的开放项：标题栏文本渲染库（cairo+pango 属主流选择，将作为默认）。

---

## 12. 参考

- wlroots 官方文档与示例：<https://gitlab.freedesktop.org/wlroots/wlroots>
- wlr-protocols：<https://gitlab.freedesktop.org/wlroots/wlr-protocols>
- wlroots 0.19.0 发布（[Debian 0.19 过渡追踪](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=1111148)、[wayfire 升级讨论](https://github.com/WayfireWM/wayfire/issues/2628)）
- layer-shell 协议在 Qt 侧支持（[layer-shell-qt 迁移到 Qt 6.5/6.6 多 shell](https://invent.kde.org/plasma/layer-shell-qt/-/merge_requests/25/diffs)、[layer shell 会议纪要](https://mail.kde.org/pipermail/plasma-devel/2023-March/122994.html)）

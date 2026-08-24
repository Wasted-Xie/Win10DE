# 系统应用通用接口（Win10DE System Apps）

Win10DE 的**系统应用**（文件资源管理器、未来的记事本/设置/终端等）遵循统一
接入约定，使启动器（开始菜单/桌面/会话）与开发可以一致地对待所有系统应用。

## 1. 形态

- 每个系统应用是**独立二进制**（如 `w10explorer`），Qt Widgets，作为普通
  xdg-toplevel 窗口客户端连接 compositor（非 layer-shell）。
- 统一命名：二进制 `w10<name>`（如 `w10explorer`）；D-Bus 服务
  `org.w10de.Apps.<Name>`（Name 首字母大写：`org.w10de.Apps.Explorer`）。
- 统一安装：`bin/` + `share/applications/w10<name>.desktop`（供开始菜单/
  桌面 appmodel 扫描启动）。

## 2. 通用接口（src/systemapps/appipc.{h,cpp}，Qt D-Bus）

所有系统应用复用同一封装，实现**单实例 + 激活既有窗口**语义：

- `w10de::app::tryActivateExisting(appName, path)`：
  目标服务已在运行 → 调其 `Activate(path)` 并返回 `true`（当前进程应退出）；
  否则返回 `false`（当前进程成为实例，继续初始化）。
- `w10de::app::registerService(appName, onActivate)`：
  以 `org.w10de.Apps.<AppName>` 注册 D-Bus 服务；被再次启动（tryActivateExisting）
  时回调 `onActivate(path)`——应用应把主窗口置前并导航到 `path`。
  注册失败（服务名被占）返回 `false`。

D-Bus 对象：`/App`；接口 `org.w10de.Apps.<AppName>`；方法
`Activate(s path)`（path 可为空 = 仅激活，不改变位置）。

CLI 约定：`w10<name> [arg]`——首个位置参数为应用特定入口
（explorer：起始路径），经 `tryActivateExisting(name, arg)` 单实例化。

## 3. 启动接入

- 开始菜单/桌面：应用安装 `share/applications/w10<name>.desktop`
  （`Exec=w10<name> %U`），由现有 appmodel 扫描启动；
- 会话 autostart 亦可直接 Exec；
- 未来可在 `org.w10de.Shell` 增加 `OpenApp(s app, s arg)` 总入口（shell 负责
  单实例激活或按 .desktop 启动），当前 MVP 由各应用自持单实例。

## 4. 新增系统应用的步骤

1. 新建 `src/systemapps/<name>/`（main.cpp + 主窗口类），链接
   `systemapps::appipc`（或直接编译 appipc.{h,cpp}）；
2. main：`tryActivateExisting` → `registerService` → 创建主窗口；
3. CMake：新 `add_executable(w10<name>)` + 安装 `.desktop`（模板见
   `src/systemapps/explorer/` 的 CMakeLists）；顶层 `W10DE_BUILD_SHELL` 下
   `add_subdirectory`；
4. 主题/颜色：窗口内 UI 使用 `theme/colors.h`（与 w10shell 同源，读同一
   `[theme]` 配置，保持深浅色一致）。

# Win10DE 未修复项清单（Known Unfixed Items）

> 本清单跟踪**尚未修复/尚未实现**的项目，与 `docs/KDE-GAP.md`（差距分析）、`docs/HANDOFF.md`（交接）、`README.md`（状态）配套。
> 维护规则：每完成一项功能补全，同步更新本文档（删除已修项、更新状态）。
> 最后更新：2026-08

---

## 1. 功能未实现（KDE-GAP 差距剩余）

### 1.1 中优先（常用生产力）

**全部 7 项完成 ✅**（输入设备设置、文本/PDF/图像查看器、回收站窗口、每应用音量、多显示器图形排列 GUI、窗口规则、桌面小部件）。

### 1.2 低优先（锦上添花，4 项）

| 项 | 对标 | 状态 |
|----|------|------|
| KWin 特效/脚本 | 最小化动画/桌面立方体/KWin Scripts | 部分完成（打开淡入 + 还原淡入 ✅）；**其余特效不做**：最小化淡出需延迟隐藏（fade 完成前窗口滞留，最小化→还原竞态，风险/收益低）；桌面立方体需 3D 变换（scene 无节点变换）；KWin Scripts 需 QJSEngine 脚本框架（工程量大） |
| 全局菜单 | KDE Global Menu（菜单栏上移） | **MVP 不做**——依赖客户端 DBusMenu（com.canonical.dbusmenu）导出 + appmenu 协议配合（Qt 应用默认不导出菜单），无客户端配合不可行；留待生态成熟 |
| KWallet | KDE 密码库 | **MVP 不做**——Win10DE 无密码存储消费方（无浏览器/邮件客户端集成）；D-Bus org.kde.KWallet 服务 + 加密存储工程量大，无实际需求 |
| 登录管理器 | SDDM 等价 | **不做**——系统层（显示管理器/会话选择），超出桌面环境范围（Win10DE 是会话内环境，非发行版登录栈） |

---

## 2. 已实现模块的已知简化 / 未修项

> 均为审查中记录的 L 级或"已知简化"（无 S/M 未修项）。级别：L=轻微/增强，R=记录在案（接受）。

### 2.1 w10viewer（文本/PDF/图像查看器）

| # | 问题 | 级别 | 状态 |
|---|------|------|------|
| V1 | 大文本（>50MB）全量 `readAll()` + `setPlainText`，明显卡顿 | L | ✅ 已修：>50MB 只读前缀 + 状态栏提示截断 |
| V2 | 文件对话框过滤器窄于类型白名单（缺 xml/yaml/csv/rs/go/tiff/xpm/ico） | L | ✅ 已修：过滤器与白名单对齐（全扩展名） |
| V3 | 未知类型时 `QMessageBox::warning` 在 D-Bus Activate 槽内 `exec()`，阻塞调用方 | L | ✅ 已修：QTimer::singleShot 延迟非模态 |
| V4 | `.desktop` MimeType 未声明全部支持格式（tiff/x-xpixmap/x-ico/csv/xml/yaml 等） | L | ✅ 已修：MimeType 与白名单对齐 |
| V5 | GBK/GB18030 编码文本不支持（仅 UTF-8/UTF-16/UTF-32 BOM） | R | ✅ 已修：UTF-8 替换字符回退 GB18030（Qt6Core5Compat QTextCodec） |

### 2.2 w10trash（回收站窗口）

| # | 问题 | 级别 | 状态 |
|---|------|------|------|
| T1 | broken symlink 无法被 `QDir::entryList` 枚举（Qt 实测），列表不可见、不可单独清理（empty 可清其 info） | L | ✅ 已修：list/empty 改用 `std::filesystem::directory_iterator`（可枚举 broken symlink）+ selftest 断言 |
| T2 | 跨设备恢复（EXDEV）失败无原因提示，窗口仅显示"失败" | L | ✅ 已修：TrashStore::lastError() 透传 errno（strerror），UI 显示原因 |
| T3 | 双击条目直接恢复无确认（Windows 语义是预览/打开） | L | ✅ 已修：双击改为"打开原始位置"（QDesktopServices 定位父目录）；恢复保留工具栏 |
| T4 | restore 的 uniqueRestoreName 检查与 rename 之间极小竞态（Unix rename 会覆盖） | R | 保留记录：桌面概率可忽略，代码注释已说明 |

### 2.3 文件索引（FileIndex）

| # | 问题 | 级别 | 状态 |
|---|------|------|------|
| F1 | 无增量更新（未接 QFileSystemWatcher），索引一次后新文件需重启 w10shell 才可搜 | L | ✅ 已修：QFileSystemWatcher 监听 rootDir + 顶层子目录，目录变化事件归并（150ms）+ 批量删除（contentWords 一次全表扫），增量入库即时反映；增量验证 PASS（新增入库/删除移除）。**已知限制**：只监听目录（文件 in-place 修改不触发，增删覆盖）；顶层子目录整体 rmdir 深层残留索引（注释已承认）；更深层目录未监听 |
| F2 | 内容索引有闸门（5000 文件/200 万词对/停用词），超限文件只入名称索引 | R | 保留记录：平衡内存的刻意设计 |

### 2.4 壁纸幻灯片

| # | 问题 | 级别 | 修复建议 |
|---|------|------|----------|
| W1 | LayerShellQt 下 Qt 增量 paint 调度失效（update/repaint/requestUpdate/resize 均不触发 surface 提交），轮换用 hide/show 强制重绘 | L | 真机可能闪烁；需 layer-shell Qt 渲染路径排查（上游问题） |

### 2.5 输入设备设置

| # | 问题 | 级别 | 修复建议 |
|---|------|------|----------|
| I1 | headless 无真实设备，libinput 热插拔应用路径（handleNewInput → applyPointerSettings）未真机验证 | 真机项 | 真机插拔鼠标/触摸板验证 |
| I2 | natural_scroll 同时影响鼠标滚轮（libinput 语义），与 Windows 仅触摸板反向的预期有差异 | R | 文档记录；如需细分需按设备类型过滤 |
| I3 | `[input]` 仅 libinput 后端可配置（wayland 嵌套/headless 降级为保存） | R | 文档记录 |

### 2.6 锁屏（w10lock + PAM）

| # | 问题 | 级别 | 修复建议 |
|---|------|------|----------|
| L1 | fail-closed：非 root 时 PAM 不可用 → 提示"验证服务不可用"且不提供任意键解锁，唯一出口为系统控制台/会话重启 | R | 真机建议 setuid root 安装 w10lock 或专用 /etc/pam.d/w10lock 服务 |

### 2.7 其他

| # | 模块 | 问题 | 级别 | 状态 |
|---|------|------|------|------|
| O1 | w10term | ANSI 追加模式无光标移动（简化），部分 TUI（vim/htop 全屏）显示异常 | L | ✅ 已修：CSI A/B/C/D 光标移动 + H/f 定位 + 备用屏（?1049h/l）——进入"定位模式"在光标处插入；TUI 最小支持（无滚动区）。**已知限制**：定位模式换行无 CR（光标留原列）；备用屏退出 clear() 丢主屏回显（vim 退出后终端空白）；参数化 CSI（[3A）忽略参数只移 1 格 |
| O2 | 默认应用 | 仅管理 3 类（浏览器/邮件/文件管理器），查看器/终端等类型未入设置页 | L | ✅ 已修：新增"查看器"类别（image/*、application/pdf、text/plain → w10viewer.desktop）；终端无标准 mime 未加 |
| O3 | 每应用音量 | 滑块拖动→音量变化为真机交互验证项；sink-input 无 Pulse 事件订阅，流增删需手动刷新 | 真机项 | 保留：真机验证；事件订阅可后加 |
| O3 | w10explorer | 删除进回收站仅主回收站，系统分区顶层回收站（/.Trash-<uid>）与跨设备删除不支持（QFile::rename EXDEV 不上报成功） | R | 保留：跨设备安全失败（不上报成功）为保守行为；.Trash-uid 需分区检测，MVP 不做 |
| O4 | 窗口 | Alt+Tab/任务栏对 Wayland 激活置前（activateWindow）受 xdg-activation token 限制，部分场景无法真置前 | R | 保留：需 compositor 实现 xdg-activation-v1 + shell 请求 token，工程量大 |

---

## 3. 待真机 / 嵌套环境验证（非代码缺陷）

- **DRM 后端**：headless 之外的真实显示器输出、热插拔
- **XWayland**：WSL 无 XWayland 运行时，X11 客户端未实际验证
- **输入设备**：真实鼠标/键盘/触摸板交互、热插拔（含输入设置热应用）
- **NetworkManager / Bluez**：当前 D-Bus mock 验证，真机需实际服务
- **PAM 锁屏**：setuid root 安装后密码验证链路
- **音频**：PipeWire 真实服务（当前 WSL 无 Pulse 兼容服务，显示不可用）

---

## 4. 与本清单配套的既有流程

每项功能补全完成后仍执行：**编译通过 + headless 验证 + 子代理代码审查 + README 与 docs/HANDOFF 同步**，并在本文档"2. 已实现模块的已知简化"中登记审查遗留项或标记已修。

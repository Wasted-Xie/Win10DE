# Win10DE 未修复项清单（Known Unfixed Items）

> 本清单跟踪**尚未修复/尚未实现**的项目，与 `docs/KDE-GAP.md`（差距分析）、`docs/HANDOFF.md`（交接）、`README.md`（状态）配套。
> 维护规则：每完成一项功能补全，同步更新本文档（删除已修项、更新状态）。
> 最后更新：2026-08

---

## 1. 功能未实现（KDE-GAP 差距剩余）

### 1.1 中优先（常用生产力，4 项）

| 项 | 对标 | 当前状态 | 建议切入点 |
|----|------|----------|-----------|
| 每应用音量 | KDE 每应用音量（pipewire 节点级） | 未开始。w10settings"音频"页仅全局输出/音量/静音（libpulse） | 用 PulseAudio 的 sink-input 枚举（libpulse `pa_context_get_sink_input_info_list`）列出各应用流 + 音量滑块；或走 PipeWire 原生 API |
| 多显示器图形排列 GUI | KDE System Settings 显示器图形拖拽 | 未开始。仅下拉选择输出/分辨率/缩放/位置（`org.w10de.Compositor` SetPosition 已可设坐标） | Qt 自绘显示器矩形缩略图 + 拖拽更新位置（复用 SetPosition）；多输出 headless 验证需 `--outputs` 多输出测试参数 |
| 窗口规则 | KWin Window Rules（按类/标题强制属性） | 未开始。compositor 无规则引擎 | `[window_rules]` 配置段（match: app_id/title + action: 无边框/置顶/固定工作区/初始几何）+ View 创建时应用；真机验证 |
| 桌面小部件 | Plasma 桌面部件 | 未开始。桌面仅图标列表（M4 desktopwindow） | 最轻量：桌面快捷方式小组件（时钟/系统信息），复用背景层 + LayerShellQt；复杂部件需拖放框架，优先级低 |

### 1.2 低优先（锦上添花，5 项）

| 项 | 对标 | 状态 |
|----|------|------|
| KWin 特效/脚本 | 最小化动画/桌面立方体/KWin Scripts | 未开始（已有窗口移动/贴靠动画） |
| Night Light | KDE 夜间色温 | 未开始（compositor gamma 控制 + 定时） |
| 全局菜单 | KDE Global Menu（菜单栏上移） | 未开始（需要 appmenu 协议 + 客户端配合） |
| KWallet | KDE 密码库 | 未开始（无加密存储需求，暂缓） |
| 登录管理器 | SDDM 等价 | 未开始（系统层，超出桌面环境范围） |

---

## 2. 已实现模块的已知简化 / 未修项

> 均为审查中记录的 L 级或"已知简化"（无 S/M 未修项）。级别：L=轻微/增强，R=记录在案（接受）。

### 2.1 w10viewer（文本/PDF/图像查看器）

| # | 问题 | 级别 | 修复建议 |
|---|------|------|----------|
| V1 | 大文本（>50MB）全量 `readAll()` + `setPlainText`，明显卡顿 | L | 分块读取/虚拟化（QPlainTextEdit 无内置虚拟化，需自定义 model 或限制打开大小） |
| V2 | 文件对话框过滤器窄于类型白名单（缺 xml/yaml/csv/rs/go/tiff/xpm/ico） | L | 过滤器与 `textExtensions()/imageExtensions()` 对齐；"所有文件"可绕过，非缺陷 |
| V3 | 未知类型时 `QMessageBox::warning` 在 D-Bus Activate 槽内 `exec()`，阻塞调用方 | L | `QTimer::singleShot(0, ...)` 延迟弹出或非模态 |
| V4 | `.desktop` MimeType 未声明全部支持格式（tiff/x-xpixmap/x-ico/csv/xml/yaml 等），文件管理器不关联这些文件 | L | 补 MimeType 声明 |
| V5 | GBK/GB18030 编码文本不支持（仅 UTF-8/UTF-16/UTF-32 BOM） | R | 真机中文 Windows 遗留文件场景；可选 QTextCodec 探测 |

### 2.2 w10trash（回收站窗口）

| # | 问题 | 级别 | 修复建议 |
|---|------|------|----------|
| T1 | broken symlink 无法被 `QDir::entryList` 枚举（Qt 实测），列表不可见、不可单独清理（empty 可清其 info） | L | `std::filesystem::directory_iterator` 枚举；头注释已记录 |
| T2 | 跨设备恢复（EXDEV）失败无原因提示，窗口仅显示"失败" | L | 错误信息透传（errno→中文） |
| T3 | 双击条目直接恢复无确认（Windows 语义是预览/打开） | L | 产品决策：改预览或加确认 |
| T4 | restore 的 uniqueRestoreName 检查与 rename 之间极小竞态（Unix rename 会覆盖） | R | 桌面概率可忽略，代码注释已说明 |

### 2.3 文件索引（FileIndex）

| # | 问题 | 级别 | 修复建议 |
|---|------|------|----------|
| F1 | 无增量更新（未接 QFileSystemWatcher），索引一次后新文件需重启 w10shell 才可搜 | L | QFileSystemWatcher 监听主目录增量入库 |
| F2 | 内容索引有闸门（5000 文件/200 万词对/停用词），超限文件只入名称索引 | R | 文档记录；平衡内存的刻意设计 |

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

| # | 模块 | 问题 | 级别 |
|---|------|------|------|
| O1 | w10term | ANSI 追加模式无光标移动（简化），部分 TUI（vim/htop 全屏）显示异常 | L |
| O2 | 默认应用 | 仅管理 3 类（浏览器/邮件/文件管理器），查看器/终端等类型未入设置页（w10viewer.desktop 的 MimeType 需手动或后续扩展） | L |
| O3 | w10explorer | 删除进回收站仅主回收站（~/.local/share/Trash），系统分区顶层回收站（/.Trash-<uid>）与跨设备删除不支持（QFile::rename EXDEV 不上报成功） | R |
| O4 | 窗口 | Alt+Tab/任务栏对 Wayland 激活置前（activateWindow）受 xdg-activation token 限制，部分场景无法真置前 | R |

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

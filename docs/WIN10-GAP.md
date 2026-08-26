# Win10DE 未完成 Windows 功能清单（Win10-Gap）

> 对照 Windows 10 自带系统应用/功能，记录 Win10DE 尚未实现或部分实现的项目。
> 配套：`docs/UNFIXED.md`（已实现模块的已知简化）、`docs/KDE-GAP.md`（KDE 生态差距）。
> 维护规则：每完成一项，更新本文档并同步 README/docs/HANDOFF。
> 最后更新：2026-08（G1 完成）

---

## 1. 进行中 / 已立项（按优先级）

| # | 功能 | 对标 Windows | 当前状态 | 立项目标 |
|---|------|-------------|----------|----------|
| G1 | **控制面板应用** | Windows 设置 vs 控制面板双入口 | ✅ **完成**（2026-08）：新建 `w10control`（传统"按类别"图标网格 + 模态对话框）+ `w10settings` 补全 3 页（Night Light/快捷键/窗口规则）。双入口覆盖同一功能全集（见下方功能矩阵），共享后端 config.ini + org.w10de.Compositor D-Bus | 新建独立"控制面板"应用（传统面板风格）+ 设置页补全——**两个入口都可更改全部功能** |
| G2 | **截图工具补全** | Win10 截图工具（区域/窗口/延时） | ✅ **完成**（2026-08）：`w10screenshot` 升级 Qt 应用——交互模式（全屏遮罩 + 工具条：全屏/区域拖选/窗口选择/延时 5 秒，Esc 取消）+ CLI（`--fullscreen`/`--region X,Y,W,H`/`--window MATCH`/`--delay N`/`--output`/`--out`）；compositor 新增 `GetViews` D-Bus（a(ssiiii) 窗口列表）；捕获核心提取 `capture.{h,cpp}`（区域裁剪/原子错误路径） | 区域截图（拖选）、窗口截图、延时截图 |
| G3 | **设备管理器** | Win10 设备管理器 | ✅ **完成**（2026-08）：新建 `w10devices`（左硬件树 8 类别 + 右属性表；sysfs/proc 数据源；稳定匹配键定位重名设备；虚拟盘过滤） | 硬件树：CPU/内存/磁盘/显卡/USB/网络（lsusb/lspci/sysfs 数据源）+ 设备详情 || G4 | **性能监视器补全** | Win10 资源监视器 | ✅ **完成**（2026-08）：`w10monitor` 性能页 4 图（CPU/内存/磁盘读写/网络收发，滚动 60 点，磁盘/网络双序列曲线）+ 磁盘/网络累计总量详情；进程页加每进程磁盘 IO（读/写 KB/s，/proc/pid/io 增量）；SysInfo 扩展历史/累计/每进程 IO | 历史记录（滚动图）、磁盘/网络详情、每进程明细 |
| G5 | **任务计划程序** | Win10 任务计划程序 | ✅ **完成**（2026-08）：新建 `w10tasks`（GUI 管理：新建/编辑/删除/启用禁用/立即运行 + `--daemon` 调度守护：每分钟检查 cron 风格配置并执行，D-Bus org.w10de.Tasks Reload；配置 ~/.config/w10de/tasks.ini） | cron/systemd timer 等价 GUI：创建/编辑/启停定时任务 |
| G6 | **日历** | Win10 任务栏时钟点开显示日历 | ✅ **完成**（2026-08）：任务栏时钟（Clock）点击 → 弹出月历（MonthCalendar：标题 ◀yyyy 年 M 月▶ + 星期表头（一~日，周一起始）+ 6×7 网格（当月白/非当月灰/今天蓝圆高亮/悬停浅灰）+ 翻月/今天/日期点击；Qt::Popup 点击外部关闭） | 任务栏时钟点击 → 弹出月历（与 Windows 一致：当月高亮/今天标记/翻月）；后续可接日历服务 |

## 1.1 G1 完成详情（控制面板 vs 设置功能矩阵）

双入口共享后端：`~/.config/w10de/config.ini` + `org.w10de.Compositor` D-Bus（/Outputs）+ info 查询类（sysfs/NetworkManager/Bluez/libpulse）。两边任一处修改，另一处立即可见/可改。

| 功能 | w10settings 入口 | w10control 入口 | 生效方式 |
|------|-----------------|-----------------|----------|
| 主题模式（深/浅） | 外观 | 外观和个性化 | 重启会话 |
| 壁纸 | 外观 | 外观和个性化 | 重启会话 |
| Night Light（色温/时间窗） | Night Light（新） | 外观和个性化 | **热应用**（D-Bus SetNightLight） |
| 显示（分辨率/缩放/排列） | 显示 | 硬件和声音·显示 | **热应用**（D-Bus） |
| 电源（电池/背光亮度） | 电源 | 系统和安全 | 即时（sysfs） |
| 音频（设备/音量/每应用） | 音频 | 硬件和声音·音频 | 即时（libpulse） |
| 蓝牙（开关/状态） | 蓝牙 | 硬件和声音·蓝牙 | 即时（Bluez D-Bus） |
| 输入设备（鼠标/触摸板/键盘） | 输入设备 | 硬件和声音·输入设备 | **热应用**（D-Bus SetInputSettings） |
| 默认应用（mimeapps.list） | 默认应用 | 程序 | 即时（xdg） |
| 网络状态 | 网络 | 网络和 Internet | 只读 |
| 开机自启 | 系统 | 系统和安全 | 即时 |
| 关于（版本/平台/主题） | 系统 | 系统和安全 | 只读 |
| 快捷键（[shortcuts]） | 快捷键（新） | 系统和安全·快捷键… | 重启会话 |
| 窗口规则（[window_rules]） | 窗口规则（新） | 系统和安全·窗口规则… | 重启会话 |
| 时钟/时区 | — | 时钟和区域 | 只读 |

新增 D-Bus：`SetNightLight(b enabled, i temperature, i startMinutes, i endMinutes)`
（写 config.ini + 热应用 gamma + 重建每分钟检查 timer；范围校验 1000-8000K、0-1439 分钟）。
新增共享组件：`src/systemapps/common/monitorarrangement.{h,cpp}`（显示器排列控件，双应用共用）。
`Config` 新增 `remove`/`sectionKeys`（窗口规则增删改）；`WindowRule` 新增 `name` 字段（UI 定位）。
`w10control` 主页 6 类别：系统和安全/外观和个性化/硬件和声音（QTabWidget 四子页）/网络和 Internet/程序/时钟和区域；
系统和安全内含"快捷键…/窗口规则…"入口（双入口全功能）。规则编辑对话框提取共享组件
`common/ruleeditdialog.{h,cpp}`（settings 与 control 共用）。

验证记录：w10settings/w10control selftest 全 PASS（Config remove/sectionKeys、窗口规则
name、规则序列化往返、快捷键解析、6 对话框 offscreen 构建）；headless 渲染像素验证 4/4
（control 主页类别图标网格 + settings Night Light/快捷键/窗口规则 3 新页）；SetNightLight
D-Bus 热应用验证通过（config 写入 + compositor 日志 `setNightLight: enabled=1 temp=4500K
window=21:00-06:00` + gamma 应用；start==end 被拒绝 InvalidArgs）。
**子代理审查 S1/S2 严重问题已修复**：S1 规则输入分隔符注入（名称/匹配值禁 `=` `;` `&`——
ruleInputError 校验，两入口 4 处）；S2 编辑 AND 双条件规则静默丢条件（ruleeditdialog 增加
"同时按 title/app_id 匹配"第二条件输入，两入口共用）；M1 Config::save 原子写（tmp+rename）；
M2 start==end 语义漂移（D-Bus/两 UI 均拒绝）；M3 geometry 解析严格化（逗号校验 + 尾随拒绝）；
M4 控制面板 Night Light 热应用返回检查；L10 快捷键/窗口规则对话框补关闭按钮。
**二次审查（精简）修复**：M1 追加键插入段头后（消除重复段头累积 + 归错段风险）、
openCategory 弃用 new+WA_DeleteOnClose+exec 组合、空匹配规则过滤、tmp 名带 pid、setNightLight
内复校验。

## 1.2 G2 完成详情（截图工具补全）

| 模式 | 用法 | 说明 |
|------|------|------|
| 交互 | `w10screenshot`（无参数） | 全屏遮罩 + 顶部工具条（全屏/区域/窗口/延时 5 秒/取消 Esc）；区域拖选虚线框 + 尺寸提示；窗口经 compositor GetViews 列表选择；延时倒计时 |
| 全屏 | `--fullscreen [--output NAME] [--delay N]` | 原行为保留（--output 精确匹配修复：registry bind 初始事件需第二次 roundtrip） |
| 区域 | `--region X,Y,W,H [--delay N]` | 捕获后裁剪（headless 验证用） |
| 窗口 | `--window MATCH [--delay N]` | 按 app_id/title 子串匹配（GetViews），含标题栏 32px 自动外扩 |
| 自测 | `--selftest` | cropRgba 裁剪/越界钳制、region 参数解析、保存路径格式 |

保存：`~/Pictures/Screenshots/w10shot-<yyyyMMdd-HHmmss>.png`（--out 覆盖）。
已知简化（记录）：区域坐标按单输出 + scale=100（多输出/缩放取首个输出，标题栏外扩
32px 常量）；交互遮罩窗口在 compositor 的 fullscreen=最大化语义下为最大化窗口，
选区经 widget 坐标映射输出坐标。

验证记录：selftest 3 项 PASS；headless 捕获 4/4（全屏 1920x1080、窗口 860x592 含
w10settings 内容像素、区域 400x300、延时后全屏）；交互遮罩渲染 PASS（工具条深色
18115px + 按钮文字）；`--output HEADLESS-1` 精确匹配修复验证；compositor fullscreen
未初始化断言修复（`pendingFullscreen_` 延迟到 map 应用——交互模式不再崩溃）。
**子代理审查修复（G2）**：S1 交互窗口栈对象+WA_DeleteOnClose UB → 堆分配；M1 cropRgba
int 溢出 → 64 位计算；M2 遮罩缺 WA_TranslucentBackground（全黑盖屏）→ 补透明背景；
M3 窗口模式捕获前漏 hide（遮罩入图）→ 补；M4 parseRegion 无 ERANGE/范围校验 → 补；
M5 pendingFullscreen_ 无失效机制（取消全屏后 map 仍错误最大化）→ 已初始化路径清除；
--delay 改 strtol 校验；验证脚本交互冒烟改 offscreen（**防经 WSLg 连 Windows 主屏幕**——
用户反馈修复）。

## 1.3 G3 完成详情（设备管理器）

| 类别 | 数据源 | 显示 |
|------|--------|------|
| 处理器 | /proc/cpuinfo（model name/Processor fallback，含 ARM） | 型号/物理封装/核心数/逻辑处理器/频率 |
| 内存 | /proc/meminfo MemTotal | 总容量（KB/MB/GB） |
| 磁盘驱动器 | /sys/block/*（过滤 loop/ram/zram/dm/md 虚拟盘） | 设备名/型号/容量/介质类型（HDD/SSD） |
| 显卡 | /sys/class/drm/card*/device（同卡去重） | vendor/device ID/驱动 |
| 网络适配器 | /sys/class/net/*（过滤 lo） | MAC/链路速率/驱动/状态 |
| USB 设备 | /sys/bus/usb/devices/* | 制造商/vendor/产品 ID/速率/总线地址 |
| PCI 设备 | /sys/bus/pci/devices/* | vendor:device/驱动/类别/PCI 地址 |
| 输入设备 | /proc/bus/input/devices | 名称/处理程序/物理路径 |

新应用 `w10devices`：左硬件树（8 类别 + 自绘图标 + 状态列）+ 右属性表；稳定匹配键
（key 字段）定位重名设备；无设备类别显示占位。selftest 6 项（CPU 型号/内存容量/磁盘
容量格式/GPU-USB-PCI-输入健壮性/8 类别结构）。验证：selftest PASS（WSL：i7-14650HX、
7.6GB、磁盘 4[过滤后]、pci 4、gpu/usb/input 0 降级正常）+ headless 渲染 PASS。
**子代理审查修复（G3）**：M1 ARM CPU 降级（model name 缺失 fallback Processor/
implementer+part）；M2 虚拟盘过滤（28 loop → 4 物理盘）；M3 重名设备按 key 定位 +
查找失败清空详情；M5 selftest 磁盘容量 KB 断言放宽；scanInput 改索引（Device* 易碎
设计加固）；USB fallback 名称残缺修复。

## 1.4 G4 完成详情（性能监视器补全）

| 补全项 | 实现 |
|--------|------|
| 历史滚动曲线 | 性能页 4 图：CPU（原）+ **内存使用率** + **磁盘读写**（双序列读蓝/写绿）+ **网络收发**（双序列收/发），均 60 点滚动；磁盘/网络 max 自适应历史峰值 |
| 磁盘/网络详情 | 详情文本扩展：速率 + **累计总量**（磁盘读/写、网络收/发的内核累计字节，KB/MB/GB 自适应）；SysInfo 新增 diskReadTotalBytes 等 4 个 getter |
| 每进程明细 | 进程页 6 列：PID/名称/CPU%/内存 + **IO 读/IO 写**（KB/s；/proc/pid/io 的 read_bytes/write_bytes 两次采样增量；无权限/内核线程显示 "-"）；prevProcIo_ 缓存随进程退出清理 |

验证记录：selftest 扩展（G4 历史缓冲 ≤60、累计总量、进程 IO 字段非负）PASS——WSL 实测
24 核、内存 10.6%、磁盘 sdd、网络 eth0；headless 渲染 PASS（性能页 4 图：窗口内蓝曲线
1311 采样（CPU/内存曲线 + 每核进度条）、绿双序列 36、图深底 75581、亮文字 64325）。
已知简化（记录）：每进程网络明细未做（Linux 无直接每进程网络计数器，需 eBPF/nethogs
类方案，超出 MVP）；磁盘曲线单位 MB/s、网络 KB/s（与任务栏数值一致）。
**子代理审查修复（G4）**：M1 每进程 IO 速率 dt 窗口失配（sample 重写 prevTimeMs_ 致 dt
毫秒级、IO 虚高数十倍）→ processList 独立维护 prevProcIoTimeMs_（钳制 ≥1）；L1 累计
总量读取失败清零 → 单调性守卫（0>=prev 不更新）；L2 缓存清理 O(P²) → 存活 pid 集合
O(P)；L4 图 y 轴峰值跳变"呼吸" → EMA 平滑（上升 0.6/下降 0.85）；L5 注释与实现统一。

## 1.5 G5 完成详情（任务计划程序）

| 能力 | 实现 |
|------|------|
| 创建/编辑 | `w10tasks` GUI：任务列表（名称/触发器/上次运行/结果/状态）+ 新建/编辑对话框（名称/命令 + 触发器模板：每分钟/每小时/每天/每周/每月/自定义 cron 字段） |
| 启停 | 启用/禁用开关（写配置）；删除（确认）；立即运行（shell 执行 + last_run/last_result 写回） |
| 调度执行 | `--daemon` 守护：每分钟 tick（QTimer），cron 风格匹配（minute/hour/day_of_month/month/day_of_week，-1 通配；dom+dow 任一匹配），命中且同分钟未跑过（ranKeys_ 上限 1024）→ `/bin/sh -c` 执行 + 写回 last_run/last_result；会话由 w10-session 拉起（待接入） |
| 配置 | `~/.config/w10de/tasks.ini`（[task:N] 段；保存保留注释） |
| D-Bus | `org.w10de.Tasks` /Tasks `Reload()`（GUI 保存后通知守护重读；registerObject 接口名重载固定，避免自动类名） |

验证记录：selftest 3 项（调度匹配表驱动：每天 10:30/每分钟/每周一 08:00/每月 1 日/
dow-dom 任一匹配；配置读写往返；触发器文本）PASS；**守护端到端 PASS**（每分钟任务 →
touch 证明文件 + last_run 写回 + last_result=OK，65 秒实测）；D-Bus Reload method return
PASS（含 Introspect 接口名验证）；GUI headless 渲染 PASS（窗口深底 15489 + 亮文字）。
已知简化（记录）：调度粒度分钟级（Win10 可到秒）；无"下次运行时间"预览与历史日志；
守护由 w10-session 拉起（G6 后接入会话脚本）；ranKeys_ 重启后同分钟不补跑（合理）。
**子代理审查修复（G5）**：S1 编辑对话框模板默认"每分钟"毁掉原调度 → inferTemplate 按
字段推断选中；S2 Custom 无条件置位 dom/dow/month（每周日意外触发）→ 星期下拉加"忽略"
项、日期/月份 spin 支持 0=忽略、Custom 只写显式字段；M1 name/command 换行转义 +
注释更正（全量重写不保留未识别键）；M2 triggerText 月份并入 dom/dow、hour-only 组合
修正（前缀去重）；M3 调度字段范围校验（越界记 stderr 回退）；M4 GUI 保存前合并守护
写回的 last_run/last_result（TOCTOU）；M5 selftest 经 W10DE_TASKS_CONFIG 隔离到临时
目录（不再碰真实配置）；L1 注册服务先于对象 + 失败返回；L2 Reload 立即 tick；
L4 补特殊字符/边界组合用例；L6 守护写回失败记日志；L7 未知参数告警。

## 1.6 G6 完成详情（日历）

| 能力 | 实现 |
|------|------|
| 弹出 | 任务栏时钟（Clock）左键点击 → `MonthCalendar` 弹窗（时钟上方居中；Qt::Popup 点击外部自动关闭 + WA_DeleteOnClose） |
| 月历视图 | 标题（◀ yyyy 年 M 月 ▶ 翻月）+ 星期表头（一 二 三 四 五 六 日，**周一起始**）+ 6×7 日期网格（42 格，跨月补足：当月白字/非当月灰字/**今天蓝圆高亮**/悬停浅灰底）+ 底部"今天"行（点击回当月） |
| 交互 | 翻月（◀▶）、点击日期切月（跨月点击自动换月）、今天行、悬停高亮 |
| 纯逻辑 | `calendarCells(year, month)`（周一起始 42 格）+ `daysInMonth`（闰年）——headless 可测 |

验证记录：`--calendar-selftest` PASS（月天数 28/29/31/30、42 格、2026-08-01 周六→索引 5、
首列全周一、连续 42 天）；`--calendar-render` 独立渲染 PASS（面板深底 17692、**今天蓝圆
3444**、文字 495——今天高亮/网格渲染）；w10shell 完整渲染回归无破坏（桌面小部件时钟区
877/白字 164 与改动前一致）。已知简化（记录）：无事件/农历（Win10 有节假日与农历，
MVP 仅日期）；弹窗为 xdg popup（真机 layer-shell 任务栏下坐标需实测）。
**子代理审查修复（G6）**：S1 Qt::Popup 标志名存实亡（普通顶层窗口永不关闭——每次点击
时钟泄漏一窗）→ Clock 弹出路径显式 setWindowFlags(Qt::Popup)；M2 点击当月日期无选中
反馈 → selected_ 成员（浅蓝实心圆，与今天实心蓝圆分离）+ 点击任意日期选中并高亮；
L1 面板宽对齐 259（7×37）；L2 网格行高 33 消空白；L3 标题热区与绘制统一 28px；
L4 补足格中的"今天"不画圆（非当月一律灰字）；L5 翻月后 hoverIndex_ 重置；L6 今天圆
改正圆（22×22 居中）；L7 daysInMonth/calendarCells 非法输入校验（selftest 补 %400 闰年
2000-02 与 offset=0 2026-06 用例）；L8 弹出位置屏幕边界钳制（screenAt + availableGeometry）。

## 2. 未立项（缺失功能，按价值排序）

| 功能 | 对标 | 说明 | 建议 |
|------|------|------|------|
| 画图 | Win10 画图 | 图像编辑器 | Qt QPainter 基础版（打开/绘制/保存） |
| 闹钟和时钟 | Win10 闹钟 | 世界时钟/计时器/秒表/闹钟 | 纯 Qt 小应用 |
| 录音机 | Win10 录音机 | 录音 + 播放 | libpulse record |
| 媒体播放器 | Groove/电影和电视 | 音视频播放 | mpv 后端或 QtMultimedia |
| 便笺 | Sticky Notes | 置顶便签 | QTextEdit 轻量实现 |
| 字符映射表 | charmap | 特殊字符插入 | 纯 Qt 表格 |
| 写字板 | 写字板 | 富文本 | QTextEdit 富文本模式 |
| 远程桌面 | 远程桌面连接 | RDP 客户端 | 封装 xfreerdp |
| 磁盘清理 | 磁盘清理 | 垃圾清理 | gio trash + 缓存扫描 |
| 磁盘管理 | 磁盘管理 | 分区查看 | udisks 只读 |
| 屏幕键盘 | 轻松使用 | 虚拟键盘 | Qt 实现 |
| 日历（完整） | 日历应用 | 月视图 + 事件 | G6 后扩展（需日历服务） |
| 邮件/人脉 | 邮件/人脉 | 需服务端 | MVP 难做（IMAP/Caldav） |

## 3. 已评估不做（超范围/生态不同）

| 功能 | 理由 |
|------|------|
| Edge 浏览器 | xdg-open 走系统默认浏览器 |
| OneNote / Xbox / 反馈中心 | 生态绑定，无对应 |
| Windows 安全中心 / 系统还原 | Linux 安全/备份模型不同 |
| 注册表编辑器 | dconf/gsettings 非等效 |
| 放大镜 / 讲述人 | 系统级辅助（Orca 等外部方案） |

---

## 4. 执行流程（每项）

**编译通过 + headless 验证（selftest/渲染/视情）+ 子代理代码审查 + README 与 docs/HANDOFF/WIN10-GAP 同步**；完成后本文档对应项标记 ✅。

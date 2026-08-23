# Win10DE — 上游依赖许可证清单（开源合规）

> 生成时间：2026-08-22 | 所有许可证均**逐一核实**（克隆上游仓库/读取本地 vendored 源码），非凭记忆
> 用途：开源前确认对上游的尊重与合规义务

## 一、依赖许可证清单（已核实）

| # | 依赖 | 用途 | 许可证 | 核实方式 |
|---|------|------|--------|----------|
| 1 | **wlroots 0.19.0**（commit 13a62a2） | compositor 核心（vendored 在 third_party/） | **MIT** | 本地 `third_party/wlroots/LICENSE`（Drew DeVault 等版权） |
| 2 | **stb_image_write 1.16** | 截图/图像输出（vendored） | **Public Domain** | 本地文件头声明 `public domain - http://nothings.org/stb` |
| 3 | **Qt 6.5+** | shell UI（Qt Widgets） | **LGPL-3.0**（或 GPL-3.0/商业，动态链接） | qtbase 仓库 `LICENSES/LGPL-3.0-only.txt` |
| 4 | **layer-shell-qt**（KDE） | 任务栏/锁屏 layer-shell 客户端 | **LGPL-2.1** | KDE invent 仓库 `LICENSES/LGPL-2.1-only.txt` |
| 5 | **libdrm** | DRM_FORMAT_* 头文件 | **MIT** | GitLab 克隆 `LICENSES/MIT.txt`（REUSE 规范） |
| 6 | **libxkbcommon** | 键盘输入（xkb） | **MIT** | GitHub raw `LICENSE` |
| 7 | **wayland**（客户端库） | Wayland 协议客户端 | **MIT** | GitLab 克隆 `COPYING`（Høgsberg/Intel/Collabora 版权） |
| 8 | **wayland-protocols** | 协议 XML | **MIT** | GitLab 克隆 `COPYING` |
| 9 | **pixman**（wlroots 构建期） | 像素操作 | **MIT** | GitLab 克隆 `COPYING`（Open Group 等版权） |
| 10 | **seatd**（wlroots 构建期） | seat 管理 | **MIT** | GitHub raw `LICENSE` |

> 说明：9/10 为 wlroots 的 subproject 构建依赖（`third_party/wlroots/subprojects/*.wrap`），项目间接使用，MIT 无传染性。

## 二、许可证义务分析

| 依赖 | 对你代码的约束 |
|------|----------------|
| wlroots / libdrm / libxkbcommon / wayland / wayland-protocols / pixman / seatd（均 MIT） | **无传染**。仅需保留上游版权声明（vendored 的 wlroots/stb 必须保留 LICENSE 文件） |
| stb（Public Domain） | 无任何义务 |
| Qt 6（LGPL-3.0） | 动态链接即可；需允许用户替换 Qt 库、提供反向工程手段（默认满足）；**不要静态链接** |
| layer-shell-qt（LGPL-2.1） | 同上，动态链接即可 |

**结论：只要保持 Qt / layer-shell-qt 动态链接，你的代码可选用任意开源协议。**

## 三、你代码的协议选择建议

| 选项 | 适用 |
|------|------|
| **MIT**（推荐） | 最宽松，与 wlroots 一致，社区零摩擦 |
| **Apache-2.0** | 宽松 + 专利授权，商业友好 |
| **GPL-3.0** | copyleft，禁止衍生闭源（与 LGPL 依赖兼容） |
| **LGPL-3.0** | 折中 |

## 四、开源前合规操作清单

1. [ ] 根目录添加你选择的 LICENSE 文件
2. [ ] 编写 `THIRD_PARTY_NOTICES.md`（引用本清单）
3. [ ] 确认 `third_party/wlroots/LICENSE`、`third_party/stb` 版权声明**原样保留**（vendored 必需）
4. [ ] 检查 src/ 是否有直接复制自 wlroots/Qt 的代码段（保留其文件头版权）
5. [ ] CMake 确认 Qt6 动态链接（默认即动态）
6. [ ] README 添加"依赖与许可证"章节
7. [ ] 若发布二进制，附带对应 LGPL 库的许可证文本与源码获取方式（Qt 组件）

## 五、核实证据存档

本清单对应的上游源码克隆保留于工作区 `lic_check/`（wayland / wayland-protocols / libdrm / pixman，均为 --depth 1 浅克隆），可复核。

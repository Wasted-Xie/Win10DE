// Win10DE 合成器核心（M1：多后端 + xdg-shell + seat）
//
// 里程碑 M1：DRM/wayland/headless 多后端；wlr_scene 渲染；xdg-shell 支持；
// seat 输入与焦点。视图（View）、输入（Seat）由本类管理。
#pragma once

// wlroots 头文件没有 extern "C" 保护，C++ 中必须手动包裹。
extern "C" {
#include <wayland-server-core.h>
extern "C" {
// wlroots headers lack extern "C" guards; required in C++.
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
}  // extern "C" (wlroots)
}

#include "ipc/config.h"
#include "ipc/shortcuts.h"
#include "ipc/windowrules.h"
#include "ipc/nightlight.h"
#include "ipc/theme.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace w10de {

// M0 运行参数（由 main.cpp 解析命令行填充）
struct CompositorOptions {
    int width = 1920;              // headless 输出宽度（其他后端忽略）
    int height = 1080;             // headless 输出高度（其他后端忽略）
    int outputs = 1;               // headless 输出数量（KDE-GAP 中优先 #5：
                                   // 多显示器排列 headless 验证用）
    int frames = 0;                // 渲染帧数后退出（0 = 无限运行；仅 headless 冒烟用）
    std::string screenshotPath;    // 截图输出路径，空则跳过截图验证
    std::string socketName;        // 固定 Wayland socket 名（会话启动用）；空则自动生成
    std::string configPath;        // 配置文件（~/.config/w10de/config.ini）；空则不读
    bool verbose = false;          // 详细日志
    // ---- 多工作区（M7 续）----
    int initialWorkspace = 0;      // 启动时的工作区（0..kWorkspaceCount-1）
    // headless 验证用：在渲染到指定帧时切换工作区（frame, workspace），
    // 按帧序应用；空则无自动切换。
    std::vector<std::pair<int, int>> workspaceSwitches;
    // M8 验证用：首个窗口 map 后自动贴左半屏（验证 Aero Snap 几何/动画）。
    bool snapTest = false;
    // Alt+Tab 验证用：渲染到指定帧时显示切换器（headless 截图验证 UI）。
    int alttabTestFrame = 0;
    // 剪贴板历史验证用：渲染到指定帧时触发 Win+V 面板
    // （headless 截图验证面板渲染；经 D-Bus 通知 shell）。
    int clipboardTestFrame = 0;
    // Snap 布局验证用：渲染到指定帧时显示布局选择器（KDE-GAP #3）。
    int snaplayoutTestFrame = 0;
    // 快捷键验证用：打印 [shortcuts] 生效绑定后退出（无需启动后端）。
    bool shortcutsDump = false;
    // 窗口规则验证用（KDE-GAP 中优先 #6）：打印 [window_rules] 解析结果后退出。
    bool windowRulesDump = false;
    // Night Light 算法验证用（低优先）：打印 <temp>K gamma 表采样后退出。
    int nightlightTestTemp = 0;
};

class Output;
class Seat;
class View;
class LayerSurface;
class XView;
class CompositorDbus;

// wlroots compositor 生命周期：display / backend / renderer / allocator /
// scene / xdg-shell / seat，以及视图列表管理。
class Compositor {
public:
    explicit Compositor(CompositorOptions opts);
    ~Compositor();

    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    // 初始化各子系统并启动 backend；失败返回 false。
    bool init();
    // 进入事件循环；返回进程退出码。
    int run();

    // ---- 供子组件访问 ----
    wl_display* display() const { return display_; }
    wlr_renderer* renderer() const { return renderer_; }
    wlr_allocator* allocator() const { return allocator_; }
    wlr_scene* scene() const { return scene_; }
    wlr_output_layout* outputLayout() const { return outputLayout_; }
    Seat* seat() const { return seat_.get(); }
    const CompositorOptions& options() const { return options_; }
    // 当前主题（init 时从配置 [theme] 段加载；窗口装饰/背景色用）。
    const Theme& theme() const { return theme_; }
    // 快捷键绑定（init 时从 [shortcuts] 段加载；Seat 查表分发）。
    const std::vector<ShortcutBinding>& shortcuts() const { return shortcuts_; }
    // 窗口规则（KDE-GAP 中优先 #6：init 时从 [window_rules] 段加载；
    // View::applyRules 在 map 时匹配）。
    const std::vector<w10de::ipc::WindowRule>& windowRules() const {
        return windowRules_;
    }
    // Night Light（低优先：夜间色温）——启动/定时/新输出时应用。
    // force=true：跳过状态去重（热插拔新输出必须拿到 gamma LUT）。
    void applyNightLight(bool force = false);
    bool nightLightActive() const { return nightLightActive_; }
    wl_event_source* nightLightTimer() const { return nightLightTimer_; }
    // 输出列表（显示设置 D-Bus 服务遍历用）。
    const std::vector<std::unique_ptr<Output>>& outputs() const { return outputs_; }
    // 按名字找输出（D-Bus SetMode/SetScale/SetPosition 用）；未找到返回 nullptr。
    wlr_output* findOutputByName(const std::string& name) const;

    // ---- 视图列表管理（z 序：末尾为最上层）----
    const std::vector<View*>& views() const { return views_; }
    void addView(View* view);
    void removeView(View* view);
    void raiseView(View* view);

    // ---- 多工作区（M7 续）----
    // 工作区数量（Win10 默认 4 个虚拟桌面）。
    static constexpr int kWorkspaceCount = 4;
    // 当前工作区（0..kWorkspaceCount-1）。
    int currentWorkspace() const { return currentWorkspace_; }
    // 切换工作区：更新全部 view/xview 可见性，焦点移到新工作区顶层窗口
    //（原焦点窗口被隐藏时）；无变化（同工作区）时忽略。
    void switchWorkspace(int workspace);
    // 窗口移动到指定工作区（任务栏/快捷键用）；归属变化时刷新可见性。
    void moveViewToWorkspace(View* view, int workspace);
    void moveXViewToWorkspace(XView* view, int workspace);
    // 当前工作区下窗口是否可见（View::applyVisibility 用）。
    bool viewVisible(const View* view) const;
    bool xviewVisible(const XView* view) const;
    // 工作区切换后：聚焦指定工作区最上层可见窗口（切换目标工作区用）。
    void focusWorkspaceTop(int workspace);
    // M8：推进全部窗口动画一帧（Output 帧循环调用）。
    void tickAnimations();

    // ---- XWayland 窗口列表（XView，与 xdg View 平级）----
    const std::vector<XView*>& xviews() const { return xviews_; }
    void addXView(XView* view);
    void removeXView(XView* view);
    void raiseXView(XView* view);

    // 第一个输出的有效分辨率（最大化等操作的目标尺寸）。
    bool outputSize(int* width, int* height) const;

    // 指定输出的可用区（扣除任务栏等独占区后的区域，最大化窗口用）。
    // 未排过层表面时回退到输出分辨率。
    bool outputUsableSize(wlr_output* output, int* width, int* height) const;

    // 第一个输出（layer surface 未指定输出时分配用）；无输出时返回 nullptr。
    // 注意：定义在 server.cpp（此处 Output 仅前向声明，内联实现会编译失败）。
    wlr_output* firstOutput() const;

    // 重排全部 layer surface：按输出与层序，background→bottom→top→overlay，
    // 独占区逐层递减可用区域。由 LayerSurface 的 map/commit/属性变化触发。
    void arrangeLayers();
    // LayerSurface 销毁时移除（destroy 回调中调用，见 LayerSurface）。
    void removeLayerSurface(LayerSurface* layerSurface);

    // 层表面列表（seat 命中检测按层遍历用）。
    const std::vector<LayerSurface*>& layerSurfaces() const { return layerSurfaces_; }

    // 层锚树：scene 根按 z 序挂 5 个锚（background < bottom < view < top <
    // overlay），窗口/装饰/层表面/背景分别挂载，保证跨层 z 序正确。
    wlr_scene_tree* backgroundAnchor() const { return backgroundAnchor_; }
    wlr_scene_tree* viewAnchor() const { return viewAnchor_; }
    wlr_scene_tree* layerAnchor(int layer) const;

    // foreign-toplevel manager：任务栏窗口列表协议（View 创建 handle 用）。
    wlr_foreign_toplevel_manager_v1* foreignToplevelManager() const {
        return foreignToplevelManager_;
    }

    // scene 输出布局关联（Output 构造时 add_output 用）。
    wlr_scene_output_layout* sceneOutputLayout() const { return sceneOutputLayout_; }

    // ---- 锁屏（ext-session-lock-v1，M6）----
    bool sessionLocked() const { return sessionLocked_; }
    // 当前锁屏 surface（锁定期间唯一接收输入的 surface）；未锁定时 nullptr。
    wlr_surface* lockSurface() const;
    // 锁定/解锁：隐藏/恢复普通内容（背景/窗口/层表面锚），清空焦点。
    void setSessionLocked(bool locked);
    // 启动锁屏进程（Win+L 快捷键触发；fork/exec w10lock）。
    void launchLockScreen();
    // Win+V：通知 shell 切换剪贴板历史面板（D-Bus org.w10de.Clipboard）。
    void toggleClipboardHistory();

    // 由输出帧逻辑设置最终退出码（如截图验证失败）。
    void setExitCode(int code) { exitCode_ = code; }

private:
    // 后端新增输出/输入/顶层窗口时的事件入口（静态 C 回调）。
    static void handleNewOutput(wl_listener* listener, void* data);
    static void handleNewInput(wl_listener* listener, void* data);
    static void handleNewToplevel(wl_listener* listener, void* data);
    static void handleNewDecoration(wl_listener* listener, void* data);
    static void handleDecorationCommit(wl_listener* listener, void* data);
    static void handleDecorationDestroy(wl_listener* listener, void* data);
    static void handleNewLayerSurface(wl_listener* listener, void* data);
    static void handleNewLock(wl_listener* listener, void* data);
    static void handleLockNewSurface(wl_listener* listener, void* data);
    static void handleLockSurfaceMap(wl_listener* listener, void* data);
    static void handleLockUnlock(wl_listener* listener, void* data);
    static void handleLockDestroy(wl_listener* listener, void* data);
    static void handleLockSurfaceDestroy(wl_listener* listener, void* data);
    static void handleNewXSurface(wl_listener* listener, void* data);
    static void handleXWaylandReady(wl_listener* listener, void* data);

    CompositorOptions options_;
    // 主题（[theme] 段；init 时从 configPath 加载，未配置为深色预设）。
    Theme theme_;
    wl_display* display_ = nullptr;
    wlr_backend* backend_ = nullptr;
    wlr_renderer* renderer_ = nullptr;
    wlr_allocator* allocator_ = nullptr;
    wlr_compositor* compositor_ = nullptr;  // wl_compositor 全局（XWayland 用）
    wlr_scene* scene_ = nullptr;
    wlr_output_layout* outputLayout_ = nullptr;
    // scene 输出布局关联（wlr_scene_attach_output_layout 的返回值；
    // 需显式 add_output 才能同步 scene output 与 layout 位置）。
    wlr_scene_output_layout* sceneOutputLayout_ = nullptr;
    // 层锚树（z 序：background < view < bottom < top < overlay）。
    wlr_scene_tree* backgroundAnchor_ = nullptr;
    wlr_scene_tree* viewAnchor_ = nullptr;
    wlr_scene_tree* bottomAnchor_ = nullptr;
    wlr_scene_tree* topAnchor_ = nullptr;
    wlr_scene_tree* overlayAnchor_ = nullptr;
    wlr_xdg_shell* xdgShell_ = nullptr;
    wlr_xdg_decoration_manager_v1* decorationManager_ = nullptr;
    wlr_layer_shell_v1* layerShell_ = nullptr;
    wlr_foreign_toplevel_manager_v1* foreignToplevelManager_ = nullptr;
    // ---- XWayland（M7）----
    wlr_xwayland* xwayland_ = nullptr;
    // ---- 锁屏（ext-session-lock-v1，M6）----
    wlr_session_lock_manager_v1* sessionLockManager_ = nullptr;
    wlr_session_lock_v1* sessionLock_ = nullptr;
    wlr_session_lock_surface_v1* sessionLockSurface_ = nullptr;
    wlr_scene_surface* sessionLockSceneSurface_ = nullptr;
    bool sessionLocked_ = false;
    std::unique_ptr<Seat> seat_;
    std::vector<std::unique_ptr<Output>> outputs_;
    // D-Bus 服务（org.w10de.Compositor：输出管理，第二批显示设置用）。
    std::unique_ptr<CompositorDbus> dbus_;
    // 快捷键绑定（[shortcuts] 配置段，第二批快捷键配置化）。
    std::vector<ShortcutBinding> shortcuts_;
    // 窗口规则（KDE-GAP 中优先 #6）。
    std::vector<w10de::ipc::WindowRule> windowRules_;
    // Night Light（低优先）：配置 + 当前生效状态 + 每分钟检查 timer。
    w10de::ipc::NightLightConfig nightLight_;
    bool nightLightActive_ = false;
    bool appliedOnce_ = false;  // 首次应用已完成（跳过无变化判断）
    wl_event_source* nightLightTimer_ = nullptr;
    std::vector<View*> views_;
    std::vector<XView*> xviews_;
    std::vector<LayerSurface*> layerSurfaces_;  // 所有权为自身（destroy 回调 delete this）
    // 当前工作区（M7 续：多工作区；0..kWorkspaceCount-1）。
    int currentWorkspace_ = 0;
    // 每输出可用区缓存（arrangeLayers 更新；窗口最大化避开任务栏等独占区）。
    std::unordered_map<wlr_output*, wlr_box> usableAreas_;

    wl_listener newOutputListener_ = {};
    wl_listener newInputListener_ = {};
    wl_listener newToplevelListener_ = {};
    wl_listener newDecorationListener_ = {};
    // xdg-decoration：set_mode 在 surface 未初始化时断言（schedule_configure），
    // 未初始化则挂 commit/destroy 监听延迟设置（单槽串行，MVP 够用）。
    wl_listener decorationCommitListener_ = {};
    wl_listener decorationDestroyListener_ = {};
    wlr_xdg_toplevel_decoration_v1* pendingDecoration_ = nullptr;
    wl_listener newLayerSurfaceListener_ = {};
    wl_listener newLockListener_ = {};
    wl_listener lockNewSurfaceListener_ = {};
    wl_listener lockSurfaceMapListener_ = {};
    wl_listener lockUnlockListener_ = {};
    wl_listener lockDestroyListener_ = {};
    wl_listener lockSurfaceDestroyListener_ = {};
    wl_listener newXSurfaceListener_ = {};
    wl_listener xwaylandReadyListener_ = {};
    int exitCode_ = 0;
};

}  // namespace w10de

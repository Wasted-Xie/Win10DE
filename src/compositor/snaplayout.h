// Snap 布局选择器（KDE-GAP 高优先 #3，对标 KWin 平铺辅助 / Win10 Snap 布局）。
//
// Win+Z 触发：屏幕中心显示 3×3 网格（9 个布局格，强调色描边），方向键
// 移动选择（当前格高亮），Enter 应用（聚焦窗口贴到对应区域），Esc 取消，
// 再按 Win+Z 取消。复用 Aero Snap 的恢复点/动画机制（View::snapToRect）。
// 交互为键盘驱动（MVP；鼠标点击未实现——审查 M2）。
//
// UI 用 wlr_scene 绘制在场景最顶层（与 Alt+Tab 同模式）。

#pragma once

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
}  // extern "C"

namespace w10de {

class Compositor;
class View;

class SnapLayoutSwitcher {
public:
    explicit SnapLayoutSwitcher(Compositor& compositor);
    ~SnapLayoutSwitcher();

    // 显示布局选择器（目标 = 当前聚焦 View；无聚焦/锁屏中返回 false）。
    bool show(View* target);
    // 方向键移动选择（dx/dy ∈ {-1,0,1}，钳制在 3×3 内）。
    void move(int dx, int dy);
    // 应用当前选择（贴到对应区域）并销毁 UI。
    void apply();
    // 取消（仅销毁 UI）。
    void hide();
    bool active() const { return active_; }

    // 审查 S1：目标窗口销毁时清理（避免 apply 解引用已释放 View）。
    void onViewDestroyed(View* view);

    // 当前选择格（headless 验证用）。
    int selCol() const { return selCol_; }
    int selRow() const { return selRow_; }

private:
    void buildUi();
    void updateHighlight();

    Compositor& compositor_;
    View* target_ = nullptr;
    bool active_ = false;
    int selCol_ = 0;  // 0..2
    int selRow_ = 0;
    wlr_scene_tree* tree_ = nullptr;
    wlr_scene_rect* highlight_ = nullptr;
};

}  // namespace w10de

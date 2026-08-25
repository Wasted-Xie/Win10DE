// Alt+Tab 窗口切换器（Win10 风格：水平列表 + 当前项高亮，松开 Alt 应用）。
//
// UI 用 wlr_scene 绘制在场景最顶层（scene 根末尾）：每个候选窗口一个
// 标题块（主题色背景 + cairo 标题文字），选中项高亮（强调色）。候选 =
// 当前工作区可见窗口（xdg View + XWayland XView）。
#pragma once

#include <string>
#include <vector>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
}  // extern "C"

namespace w10de {

class Compositor;

class AltTabSwitcher {
public:
    explicit AltTabSwitcher(Compositor& compositor);
    ~AltTabSwitcher();

    // 开始切换：收集候选并显示 UI；返回是否有效果（有候选）。
    bool show();
    void next();
    void prev();
    // 结束切换：应用选中窗口（聚焦 + 置顶）并销毁 UI。
    void hideAndApply();
    // 仅销毁 UI（Alt 未选中路径，如锁屏打断）。
    void hide();
    bool active() const { return active_; }

private:
    struct Entry {
        void* view;          // View* 或 XView*
        bool isXView;
        std::string title;
        wlr_scene_tree* tree = nullptr;
        wlr_scene_rect* bg = nullptr;
        wlr_scene_buffer* text = nullptr;
    };

    void buildUi();
    void updateHighlight();

    Compositor& compositor_;
    bool active_ = false;
    std::vector<Entry> entries_;
    int current_ = 0;
    wlr_scene_tree* tree_ = nullptr;
};

}  // namespace w10de

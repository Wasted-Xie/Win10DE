// snaplayout.cpp —— Snap 布局选择器实现。

#include "compositor/snaplayout.h"

#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
// 注意：wlr/util/log.h 的 _wlr_log 须为 C 链接——不在此直接 include
//（extern "C" 外会声明为 C++ 链接导致 undefined reference），
// 由 server.h 的 extern "C" 块提供。

#include "compositor/server.h"
#include "compositor/util.h"
#include "compositor/view.h"
namespace w10de {

namespace {
constexpr int kGrid = 3;         // 3×3
constexpr int kCellW = 120;      // 每个布局格显示尺寸
constexpr int kCellH = 80;
constexpr int kGap = 8;          // 格间距
constexpr int kBorder = 2;       // 描边宽
constexpr int kPadding = 24;     // 面板内边距
}  // namespace

SnapLayoutSwitcher::SnapLayoutSwitcher(Compositor& compositor)
    : compositor_(compositor) {}

SnapLayoutSwitcher::~SnapLayoutSwitcher() {
    hide();
}

bool SnapLayoutSwitcher::show(View* target) {
    // 审查 M4：锁屏中拒绝显示（避免面板浮在锁屏之上）。
    if (target == nullptr || active_ || compositor_.sessionLocked()) {
        return false;
    }
    target_ = target;
    active_ = true;
    selCol_ = 1;  // 默认中心格
    selRow_ = 1;
    buildUi();
    updateHighlight();
    wlr_log(WLR_INFO, "snap layout: shown for view");
    return true;
}

void SnapLayoutSwitcher::move(int dx, int dy) {
    if (!active_) {
        return;
    }
    selCol_ += dx;
    selRow_ += dy;
    if (selCol_ < 0) selCol_ = 0;
    if (selCol_ > kGrid - 1) selCol_ = kGrid - 1;
    if (selRow_ < 0) selRow_ = 0;
    if (selRow_ > kGrid - 1) selRow_ = kGrid - 1;
    updateHighlight();
}

void SnapLayoutSwitcher::apply() {
    if (!active_ || target_ == nullptr) {
        return;
    }
    // 计算选中格对应的输出区域（工作区可用区 ÷ 3×3）。
    int outW = 0, outH = 0;
    if (!compositor_.outputUsableSize(compositor_.firstOutput(), &outW, &outH)) {
        wlr_log(WLR_ERROR, "snap layout: no usable output size");
        hide();
        return;
    }
    // 审查 M5：前两列/行用整除宽，末列/末行吸收余数（避免 1-2px 留白）。
    const int cellW = outW / kGrid;
    const int lastCellW = outW - cellW * (kGrid - 1);
    const int cellH = outH / kGrid;
    const int lastCellH = outH - cellH * (kGrid - 1);
    const int w = (selCol_ == kGrid - 1) ? lastCellW : cellW;
    const int h = (selRow_ == kGrid - 1) ? lastCellH : cellH;
    const int x = selCol_ * cellW;
    const int y = selRow_ * cellH;
    // 内容区减标题栏高。
    int contentH = h - View::kTitleBarHeight;
    if (contentH < 1) {
        contentH = 1;
    }
    target_->snapToRect(x, y, w, contentH);
    wlr_log(WLR_INFO, "snap layout: applied cell (%d,%d) -> %dx%d at %d,%d",
            selCol_, selRow_, w, contentH, x, y);
    hide();
}

void SnapLayoutSwitcher::onViewDestroyed(View* view) {
    // 审查 S1：目标窗口销毁时清理（防 apply 解引用已释放对象）。
    if (view == target_) {
        hide();
    }
}

void SnapLayoutSwitcher::hide() {
    if (tree_ != nullptr) {
        wlr_scene_node_destroy(&tree_->node);
        tree_ = nullptr;
    }
    highlight_ = nullptr;
    target_ = nullptr;
    active_ = false;
}

void SnapLayoutSwitcher::buildUi() {
    if (tree_ != nullptr) {
        return;
    }
    // 挂到场景根末尾（最顶层，与 Alt+Tab 一致）。
    tree_ = wlr_scene_tree_create(&compositor_.scene()->tree);
    if (tree_ == nullptr) {
        return;
    }

    int outW = 0, outH = 0;
    compositor_.outputUsableSize(compositor_.firstOutput(), &outW, &outH);
    if (outW < 1 || outH < 1) {
        outW = 1920;
        outH = 1080;
    }

    // 面板背景（半透明深色；审查 L2：const 数组替代 C99 compound literal）。
    const int panelW = kGrid * kCellW + (kGrid - 1) * kGap + kPadding * 2;
    const int panelH = kGrid * kCellH + (kGrid - 1) * kGap + kPadding * 2;
    const int panelX = (outW - panelW) / 2;
    const int panelY = (outH - panelH) / 2;
    const float bgColor[4] = {0.06f, 0.06f, 0.07f, 0.92f};
    wlr_scene_rect* bg = wlr_scene_rect_create(tree_, panelW, panelH, bgColor);
    if (bg != nullptr) {
        wlr_scene_node_set_position(&bg->node, panelX, panelY);
    }

    // 9 个布局格（默认色描边）。
    const float gridColor[4] = {0.16f, 0.16f, 0.17f, 1.0f};
    const int startX = panelX + kPadding;
    const int startY = panelY + kPadding;
    for (int r = 0; r < kGrid; ++r) {
        for (int c = 0; c < kGrid; ++c) {
            const int cx = startX + c * (kCellW + kGap);
            const int cy = startY + r * (kCellH + kGap);
            // 外描边（格背景色 = 面板色略浅）。
            wlr_scene_rect* cell = wlr_scene_rect_create(
                tree_, kCellW, kCellH, gridColor);
            if (cell != nullptr) {
                wlr_scene_node_set_position(&cell->node, cx, cy);
            }
        }
    }
    wlr_log(WLR_DEBUG, "snap layout: panel %dx%d at (%d,%d)",
            panelW, panelH, panelX, panelY);
}

void SnapLayoutSwitcher::updateHighlight() {
    if (tree_ == nullptr) {
        return;
    }
    // 高亮格（强调蓝，稍大于格以形成边框效果）。
    if (highlight_ == nullptr) {
        const float hl[4] = {0.0f, 0.47f, 0.84f, 1.0f};  // #0078D7
        highlight_ = wlr_scene_rect_create(tree_, kCellW + kBorder * 2,
                                           kCellH + kBorder * 2, hl);
        if (highlight_ == nullptr) {
            return;
        }
    }
    int outW = 0, outH = 0;
    compositor_.outputUsableSize(compositor_.firstOutput(), &outW, &outH);
    if (outW < 1 || outH < 1) {
        outW = 1920;
        outH = 1080;
    }
    const int panelW = kGrid * kCellW + (kGrid - 1) * kGap + kPadding * 2;
    const int panelH = kGrid * kCellH + (kGrid - 1) * kGap + kPadding * 2;
    const int panelX = (outW - panelW) / 2;
    const int panelY = (outH - panelH) / 2;
    const int x = panelX + kPadding + selCol_ * (kCellW + kGap) - kBorder;
    const int y = panelY + kPadding + selRow_ * (kCellH + kGap) - kBorder;
    wlr_scene_node_set_position(&highlight_->node, x, y);
}

}  // namespace w10de

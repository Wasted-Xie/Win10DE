#include "compositor/view.h"
#include "compositor/util.h"

#include "compositor/seat.h"
#include "compositor/server.h"

namespace w10de {

View::View(Compositor& compositor, wlr_xdg_toplevel* toplevel)
    : compositor_(compositor), toplevel_(toplevel) {
    // 先初始化全部 listener：构造中途失败被 delete 时，析构对未 add 的
    // listener 执行 wl_list_remove 也安全（自指链表自摘除）。
    wl_list_init(&map_.link);
    wl_list_init(&unmap_.link);
    wl_list_init(&destroy_.link);
    wl_list_init(&commit_.link);
    wl_list_init(&requestMove_.link);
    wl_list_init(&requestResize_.link);
    wl_list_init(&requestMaximize_.link);
    wl_list_init(&requestMinimize_.link);
    wl_list_init(&requestFullscreen_.link);
    wl_list_init(&setTitle_.link);
    wl_list_init(&setAppId_.link);
    wl_list_init(&ftMaximize_.link);
    wl_list_init(&ftMinimize_.link);
    wl_list_init(&ftActivate_.link);
    wl_list_init(&ftFullscreen_.link);
    wl_list_init(&ftClose_.link);
    wl_list_init(&ftDestroy_.link);

    wlr_xdg_surface* xdgSurface = toplevel->base;
    wlr_surface* surface = xdgSurface->surface;

    // scene 节点：map 后内容可见（wlr_scene 内部跟踪 surface map 状态）。
    // 挂到 viewAnchor，保证 z 序位于 background/bottom 层之上。
    sceneTree_ = wlr_scene_xdg_surface_create(compositor.viewAnchor(), xdgSurface);
    if (sceneTree_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create scene xdg surface for toplevel");
        return;
    }

    // 监听 surface map/unmap（0.19 中 xdg_surface 不再自带 map/unmap 事件）。
    map_.notify = handleMap;
    wl_signal_add(&surface->events.map, &map_);
    unmap_.notify = handleUnmap;
    wl_signal_add(&surface->events.unmap, &unmap_);
    destroy_.notify = handleDestroy;
    wl_signal_add(&toplevel->events.destroy, &destroy_);
    // 内容尺寸变化时同步标题栏宽度。
    commit_.notify = handleCommit;
    wl_signal_add(&surface->events.commit, &commit_);

    // SSD 标题栏装饰（M2a）。
    createDecoration();
    // foreign-toplevel handle：任务栏窗口列表协议（M3 前置）。
    createForeignToplevel();

    requestMove_.notify = handleRequestMove;
    wl_signal_add(&toplevel->events.request_move, &requestMove_);
    requestResize_.notify = handleRequestResize;
    wl_signal_add(&toplevel->events.request_resize, &requestResize_);
    requestMaximize_.notify = handleRequestMaximize;
    wl_signal_add(&toplevel->events.request_maximize, &requestMaximize_);
    requestMinimize_.notify = handleRequestMinimize;
    wl_signal_add(&toplevel->events.request_minimize, &requestMinimize_);
    requestFullscreen_.notify = handleRequestFullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &requestFullscreen_);
    setTitle_.notify = handleSetTitle;
    wl_signal_add(&toplevel->events.set_title, &setTitle_);
    setAppId_.notify = handleSetAppId;
    wl_signal_add(&toplevel->events.set_app_id, &setAppId_);

    // 新窗口归属当前工作区（M7 续：多工作区）。
    workspace_ = compositor_.currentWorkspace();
    // M8 验证：--snap-test 时 map 后自动贴左半屏。
    snapOnMap_ = compositor_.options().snapTest;

    wlr_log(WLR_INFO, "new toplevel view created");
}

View::~View() {
    // 从合成器视图列表移除（若仍在其中）。
    compositor_.removeView(this);
    destroyForeignToplevel();
    // 装饰树是纯场景节点（sceneTree_ 由 wlr_scene_xdg_surface 内部管理，
    // 装饰树需手动销毁，否则每窗口泄漏 5 个节点）。
    if (decorationTree_ != nullptr) {
        wlr_scene_node_destroy(&decorationTree_->node);
    }
    // 标题文字 buffer（scene buffer 已随装饰树销毁，这里释放像素）。
    if (titleText_ != nullptr) {
        wlr_buffer_drop(&titleText_->base);
        titleText_ = nullptr;
    }
    // M8 阴影 buffer（scene buffer 已随装饰树销毁，这里释放像素）。
    if (shadow_ != nullptr) {
        wlr_buffer_drop(&shadow_->base);
        shadow_ = nullptr;
    }
    wl_list_remove(&map_.link);
    wl_list_remove(&unmap_.link);
    wl_list_remove(&destroy_.link);
    wl_list_remove(&commit_.link);
    wl_list_remove(&requestMove_.link);
    wl_list_remove(&requestResize_.link);
    wl_list_remove(&requestMaximize_.link);
    wl_list_remove(&requestMinimize_.link);
    wl_list_remove(&requestFullscreen_.link);
    wl_list_remove(&setTitle_.link);
    wl_list_remove(&setAppId_.link);
    wl_list_remove(&ftMaximize_.link);
    wl_list_remove(&ftMinimize_.link);
    wl_list_remove(&ftActivate_.link);
    wl_list_remove(&ftFullscreen_.link);
    wl_list_remove(&ftClose_.link);
    wl_list_remove(&ftDestroy_.link);
    wlr_log(WLR_INFO, "toplevel view destroyed");
}

int View::width() const {
    return toplevel_->base->geometry.width;
}

int View::height() const {
    return toplevel_->base->geometry.height;
}

bool View::contains(double lx, double ly) const {
    // 命中范围 = 标题栏装饰区（上方 32px）+ 内容区。
    if (ly >= y_ && ly < y_ + kTitleBarHeight && lx >= x_ && lx < x_ + width()) {
        return true;
    }
    return lx >= x_ && lx < x_ + width() &&
           ly >= y_ + kTitleBarHeight && ly < y_ + kTitleBarHeight + height();
}

DecorationArea View::decorationAt(double lx, double ly) const {
    if (!mapped_) {
        return DecorationArea::None;
    }
    const double dx = lx - x_;
    const double dy = ly - y_;
    if (dy < 0 || dy >= kTitleBarHeight || dx < 0 || dx >= width()) {
        return DecorationArea::None;
    }
    // 审查 M5（窗口规则）：borderless 无按钮——仅保留拖动区，
    // 避免点击无视觉按钮的位置触发关闭等破坏性动作。
    if (borderless_) {
        return DecorationArea::TitleBar;
    }
    // 按钮从右往左排列（Win10 布局）；窄窗口时按钮区收缩（不越界）。
    const int w = width();
    const int closeX = w - kButtonWidth > 0 ? w - kButtonWidth : 0;
    const int maxX = w - 2 * kButtonWidth > 0 ? w - 2 * kButtonWidth : 0;
    const int minX = w - 3 * kButtonWidth > 0 ? w - 3 * kButtonWidth : 0;
    if (dx >= closeX) return DecorationArea::CloseButton;
    if (dx >= maxX) return DecorationArea::MaxButton;
    if (dx >= minX) return DecorationArea::MinButton;
    return DecorationArea::TitleBar;
}

void View::setActivated(bool activated) {
    wlr_xdg_toplevel_set_activated(toplevel_, activated);
    if (ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_activated(ftHandle_, activated);
    }
}

void View::close() {
    wlr_xdg_toplevel_send_close(toplevel_);
}

void View::setMaximized(bool maximize) {
    // M8 互斥：最大化时先退出贴边。注意不能用 unsnap()——它启动返回动画
    // 且消费 restore 几何；这里只清贴边标志并取消动画，保留 snap 时保存的
    // 浮动几何作为恢复点（审查 S1）。
    if (maximize && snapEdge_ != SnapEdge::None) {
        cancelAnimation();
        snapEdge_ = SnapEdge::None;
    }
    if (maximized_ == maximize) {
        // 协议要求：每次 request_maximize/fullscreen 都必须响应 configure，
        // 即使状态未变（wlr_xdg_toplevel_set_maximized 内部 schedule_configure）。
        wlr_xdg_toplevel_set_maximized(toplevel_, maximize);
        return;
    }
    maximized_ = maximize;
    wlr_xdg_toplevel_set_maximized(toplevel_, maximize);
    if (ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_maximized(ftHandle_, maximized_);
    }

    // 未映射时（客户端 map 前请求最大化，常见于启动即最大化应用）只记录
    // 状态，几何由 handleMap 首次定位后应用；此时 geometry 还是 0，保存
    // 恢复几何会得到 (0,0,0,0)。
    if (!mapped_) {
        if (!maximize && hasRestoreGeometry_) {
            // 未映射时取消最大化：清理旧 restore 几何，避免下次最大化
            // 时恢复错误位置（M2a 审查 #13）。
            hasRestoreGeometry_ = false;
        }
        wlr_log(WLR_INFO, "view maximized=%d (applied on map)", maximized_);
        return;
    }

    if (maximized_) {
        // 保存恢复几何（仅首次进入最大化时）。
        if (!hasRestoreGeometry_) {
            setRestoreGeometry(x_, y_, width(), height());
        }
        moveTo(0, 0);
        // 最大化到可用区（扣除任务栏等独占区），而非整个输出。
        int outW = 0, outH = 0;
        if (compositor_.outputUsableSize(compositor_.firstOutput(), &outW, &outH)) {
            // 内容区高度 = 可用区高度 - 标题栏（最大化时标题栏仍可见）。
            int contentH = outH - kTitleBarHeight;
            if (contentH < 1) {
                contentH = 1;  // 极端小可用区时避免非法尺寸
            }
            resize(outW, contentH);
        }
    } else {
        // 恢复最大化前几何。
        if (hasRestoreGeometry_) {
            int rx = 0, ry = 0, rw = 0, rh = 0;
            restoreGeometry(&rx, &ry, &rw, &rh);
            moveTo(rx, ry);
            resize(rw, rh);
            hasRestoreGeometry_ = false;
        }
    }
    wlr_log(WLR_INFO, "view maximized=%d", maximized_);
}

void View::setRestoreGeometry(int x, int y, int w, int h) {
    restoreX_ = x;
    restoreY_ = y;
    restoreW_ = w;
    restoreH_ = h;
    hasRestoreGeometry_ = true;
}

// ---- M8 窗口动画（帧插值）----

void View::animateMoveTo(int x, int y) {
    if (!mapped_) {
        // 未映射：直接定位（map 后 handleMap 会设置初始位置）。
        moveTo(x, y);
        return;
    }
    animFromX_ = x_;
    animFromY_ = y_;
    animToX_ = x;
    animToY_ = y;
    animT_ = 0.0f;
    animActive_ = true;
}

void View::startFadeIn() {
    // KWin 特效（低优先）：打开淡入。sceneTree_（wlr_scene_xdg_surface）
    // 结构 = tree 下嵌套 surface_tree（wlr_scene_subsurface_tree_create），
    // 内容 buffer 在 surface_tree 的 children 里——需下探一层找 BUFFER。
    if (contentBuffer_ == nullptr && sceneTree_ != nullptr) {
        wlr_scene_node* child;
        wl_list_for_each(child, &sceneTree_->children, link) {
            if (child->type == WLR_SCENE_NODE_BUFFER) {
                contentBuffer_ = wlr_scene_buffer_from_node(child);
                break;
            }
            if (child->type == WLR_SCENE_NODE_TREE) {
                wlr_scene_tree* sub = wlr_scene_tree_from_node(child);
                wlr_scene_node* subChild;
                wl_list_for_each(subChild, &sub->children, link) {
                    if (subChild->type == WLR_SCENE_NODE_BUFFER) {
                        contentBuffer_ = wlr_scene_buffer_from_node(subChild);
                        break;
                    }
                }
                if (contentBuffer_ != nullptr) {
                    break;
                }
            }
        }
    }
    if (contentBuffer_ == nullptr) {
        return;  // 无内容节点（异常/窗口未映射内容）：不动画
    }
    fadeOpacity_ = 0.0f;
    fadeActive_ = true;
    wlr_scene_buffer_set_opacity(contentBuffer_, 0.0f);
    wlr_log(WLR_INFO, "view fade-in started (opacity 0 → 1)");
}

void View::tickAnimation() {
    // KWin 特效（低优先）：打开淡入推进（先于移动动画，独立状态）。
    if (fadeActive_) {
        fadeOpacity_ += 0.15f;
        if (fadeOpacity_ >= 1.0f) {
            fadeOpacity_ = 1.0f;
            fadeActive_ = false;
            wlr_log(WLR_DEBUG, "view fade-in done");
        }
        if (contentBuffer_ != nullptr) {
            wlr_scene_buffer_set_opacity(contentBuffer_, fadeOpacity_);
        }
    }
    if (!animActive_) {
        return;
    }
    // 每帧推进；缓动（ease-out）使开始快、收尾慢。
    constexpr float kStep = 0.12f;
    animT_ += kStep;
    if (animT_ >= 1.0f) {
        animT_ = 1.0f;
        moveTo(static_cast<int>(animToX_), static_cast<int>(animToY_));
        animActive_ = false;
        return;
    }
    // ease-out：1 - (1-t)^2。
    const float eased = 1.0f - (1.0f - animT_) * (1.0f - animT_);
    const int nx = static_cast<int>(animFromX_ + (animToX_ - animFromX_) * eased);
    const int ny = static_cast<int>(animFromY_ + (animToY_ - animFromY_) * eased);
    moveTo(nx, ny);
}

void View::cancelAnimation() {
    animActive_ = false;
}

// ---- Aero Snap（M8：Win+←/→ 半屏）----

void View::snapTo(SnapEdge edge) {
    if (!mapped_ || edge == SnapEdge::None) {
        return;
    }
    // 先取消最大化（避免与贴边布局态冲突）。setMaximized(false) 会把恢复
    // 几何消费掉，且 resize 是异步请求（取消后 width()/height() 仍是最大化
    // 尺寸）——先拷出恢复目标，取消后重新落回，保证 snap 的恢复点是真实
    // 浮动几何而非半屏/最大化尺寸（审查 M2）。
    int rx = 0, ry = 0, rw = 0, rh = 0;
    bool hadRestore = false;
    if (maximized_) {
        if (hasRestoreGeometry_) {
            restoreGeometry(&rx, &ry, &rw, &rh);
            hadRestore = true;
        }
        setMaximized(false);
        if (hadRestore) {
            setRestoreGeometry(rx, ry, rw, rh);
        }
    }
    // 保存当前浮动几何作为恢复点（若已贴边则覆盖为当前贴边几何：
    // 与 Win10 一致，连续按 ←/→ 时从"当前半屏"切换方向）。
    if (!hasRestoreGeometry_) {
        setRestoreGeometry(x_, y_, width(), height());
    }
    snapEdge_ = edge;

    int outW = 0, outH = 0;
    if (!compositor_.outputUsableSize(compositor_.firstOutput(), &outW, &outH)) {
        wlr_log(WLR_ERROR, "snap: no usable output size");
        unsnap();
        return;
    }
    // 半屏内容尺寸 = 可用区一半宽 × (可用高 - 标题栏)。
    int contentH = outH - kTitleBarHeight;
    if (contentH < 1) {
        contentH = 1;
    }
    const int halfW = outW / 2;
    const int newX = edge == SnapEdge::Left ? 0 : outW - halfW;
    // M8 动画：平滑移动到目标（resize 仍即时请求，客户端 configure 异步）。
    animateMoveTo(newX, 0);
    resize(halfW, contentH);
    wlr_log(WLR_INFO, "view snapped to %s (%dx%d at %d,0)",
            edge == SnapEdge::Left ? "left" : "right",
            halfW, contentH, newX);
}

void View::unsnap() {
    if (snapEdge_ == SnapEdge::None && !layoutSnapped_) {
        return;
    }
    snapEdge_ = SnapEdge::None;
    layoutSnapped_ = false;
    // 恢复 snap 前几何（最大化状态下由 setMaximized(false) 处理）。
    if (hasRestoreGeometry_) {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        restoreGeometry(&rx, &ry, &rw, &rh);
        // M8 动画：平滑回到原位置。
        animateMoveTo(rx, ry);
        resize(rw, rh);
        hasRestoreGeometry_ = false;
    }
    wlr_log(WLR_INFO, "view unsnapped");
}

// ---- Snap 布局选择器（KDE-GAP #3：任意矩形区域）----

void View::snapToRect(int x, int y, int w, int h) {
    if (!mapped_) {
        return;
    }
    // 同 snapTo：先取消最大化并保存恢复点（若尚无）。
    if (maximized_) {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        bool hadRestore = false;
        if (hasRestoreGeometry_) {
            restoreGeometry(&rx, &ry, &rw, &rh);
            hadRestore = true;
        }
        setMaximized(false);
        if (hadRestore) {
            setRestoreGeometry(rx, ry, rw, rh);
        }
    }
    if (!hasRestoreGeometry_) {
        setRestoreGeometry(x_, y_, width(), height());
    }
    // 布局贴边：标记 layoutSnapped_（Win+↓ 可还原），不设 snapEdge_。
    // 审查 L4：恢复点首次保存后不再覆盖（保持布局前的浮动几何）。
    layoutSnapped_ = true;
    animateMoveTo(x, y);
    resize(w, h);
    wlr_log(WLR_INFO, "view snapped to rect (%dx%d at %d,%d)", w, h, x, y);
}

void View::restoreGeometry(int* x, int* y, int* w, int* h) const {
    *x = restoreX_;
    *y = restoreY_;
    *w = restoreW_;
    *h = restoreH_;
}

void View::setMinimized(bool minimize) {
    minimized_ = minimize;
    // 可见性 = mapped && !minimized && 当前工作区（M7 续统一入口）。
    applyVisibility();
    // KWin 特效（低优先）：还原淡入——最小化是节点 enabled=false
    // （不触发 map 事件），还原时复用打开淡入机制（内容 0→1）。
    if (!minimized_ && compositor_.viewVisible(this)) {
        startFadeIn();
    }
    if (minimized_) {
        setActivated(false);
    }
    if (ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_minimized(ftHandle_, minimized_);
    }
    wlr_log(WLR_INFO, "view minimized=%d", minimized_);
}

// ---- 多工作区（M7 续）----

void View::setWorkspace(int workspace) {
    if (workspace < 0 || workspace >= Compositor::kWorkspaceCount) {
        return;
    }
    if (workspace_ == workspace) {
        return;
    }
    workspace_ = workspace;
    applyVisibility();
}

void View::applyVisibility() {
    // 可见 = 已映射 && 未最小化 && 属于当前工作区。
    const bool visible = mapped_ && !minimized_ &&
                         workspace_ == compositor_.currentWorkspace();
    if (decorationTree_ != nullptr) {
        wlr_scene_node_set_enabled(&decorationTree_->node, visible);
    }
    if (sceneTree_ != nullptr) {
        wlr_scene_node_set_enabled(&sceneTree_->node, visible);
    }
}

void View::moveTo(int x, int y) {
    x_ = x;
    y_ = y;
    // 装饰树在 (x, y)，内容区在其下方 kTitleBarHeight 处。
    if (decorationTree_ != nullptr) {
        wlr_scene_node_set_position(&decorationTree_->node, x_, y_);
    }
    wlr_scene_node_set_position(&sceneTree_->node, x_, y_ + kTitleBarHeight);
}

void View::resize(int width, int height) {
    // 仅请求；客户端 configure/ack 后几何才会变化。
    wlr_xdg_toplevel_set_size(toplevel_, width, height);
}

// ---- SSD 装饰 ----

void View::createDecoration() {
    // 装饰树挂在 viewAnchor（内容区上方，同窗口 z 序），位置由 moveTo 同步。
    decorationTree_ = wlr_scene_tree_create(compositor_.viewAnchor());
    if (decorationTree_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create decoration tree");
        return;
    }
    // Win10 标题栏（主题驱动：mode dark/light 或自定义键，见 ipc/theme.h）。
    const Theme& th = compositor_.theme();
    float titleBarColor[4], buttonColor[4], closeColor[4];
    themeColorToFloat(th.titlebarBg, titleBarColor);
    themeColorToFloat(th.buttonBg, buttonColor);
    themeColorToFloat(th.closeBg, closeColor);

    // z 序（decorationTree_ 子节点，后创建者在上）：
    //   1. 窗口阴影（最底，覆盖窗口外 8px）
    //   2. 标题栏背景
    //   3. 标题文字（背景之上，按钮之下）
    //   4. 三个按钮（最顶，文字不会盖按钮）
    // M8 阴影节点：位置固定 (-s, -s)（装饰树已在窗口坐标），尺寸随窗口。
    shadowNode_ = wlr_scene_buffer_create(decorationTree_, nullptr);
    if (shadowNode_ != nullptr) {
        wlr_scene_node_set_position(&shadowNode_->node,
                                    -kShadowSize, -kShadowSize);
    } else {
        wlr_log(WLR_ERROR, "failed to create shadow node");
    }
    titleBarRect_ = wlr_scene_rect_create(decorationTree_, 0, kTitleBarHeight, titleBarColor);
    // M2b 标题文字节点（buffer 可为空，renderTitle 时设置）。
    titleTextNode_ = wlr_scene_buffer_create(decorationTree_, nullptr);
    if (titleTextNode_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create title text node");
    }
    minButtonRect_ = wlr_scene_rect_create(decorationTree_, kButtonWidth, kTitleBarHeight, buttonColor);
    maxButtonRect_ = wlr_scene_rect_create(decorationTree_, kButtonWidth, kTitleBarHeight, buttonColor);
    closeButtonRect_ = wlr_scene_rect_create(decorationTree_, kButtonWidth, kTitleBarHeight, closeColor);
    // 审查 t1：rect 创建失败判空（与 xview.cpp 风格一致；失败仅缺该节点，
    // 其余路径判空安全）。
    if (titleBarRect_ == nullptr || minButtonRect_ == nullptr ||
            maxButtonRect_ == nullptr || closeButtonRect_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create decoration rects");
        return;
    }

    // 未映射时隐藏。
    wlr_scene_node_set_enabled(&decorationTree_->node, false);
}

void View::updateDecoration() {
    if (decorationTree_ == nullptr) {
        return;
    }
    const int w = width();
    // 标题栏背景铺满内容宽度。
    if (titleBarRect_ != nullptr) {
        wlr_scene_rect_set_size(titleBarRect_, w, kTitleBarHeight);
    }
    // 按钮从右往左：关闭(46) 最大化(46) 最小化(46)；窄窗口时 clamp 到 0。
    const int closeX = w - kButtonWidth > 0 ? w - kButtonWidth : 0;
    const int maxX = w - 2 * kButtonWidth > 0 ? w - 2 * kButtonWidth : 0;
    const int minX = w - 3 * kButtonWidth > 0 ? w - 3 * kButtonWidth : 0;
    if (closeButtonRect_ != nullptr) {
        wlr_scene_node_set_position(&closeButtonRect_->node, closeX, 0);
    }
    if (maxButtonRect_ != nullptr) {
        wlr_scene_node_set_position(&maxButtonRect_->node, maxX, 0);
    }
    if (minButtonRect_ != nullptr) {
        wlr_scene_node_set_position(&minButtonRect_->node, minX, 0);
    }
    // M2b 标题文字：左侧 padding 12，宽到最小化按钮左缘。
    // 文字区尺寸变化（含首次/缩到不可用）时重渲染；renderTitle 内部
    // 处理空标题与窄窗口（清空旧 buffer），避免旧文字残留。
    if (titleTextNode_ != nullptr) {
        const int textW = w - 3 * kButtonWidth - 24;
        wlr_scene_node_set_position(&titleTextNode_->node, 12, 0);
        const int oldTextW = titleText_ != nullptr
            ? titleText_->base.width : -1;
        if (textW != oldTextW) {
            renderTitle();
        }
    }
}

void View::renderTitle() {
    if (titleTextNode_ == nullptr || decorationTree_ == nullptr) {
        return;
    }
    const char* t = title();
    // 标题文字区域：宽 = 标题栏 - 3 按钮 - 左右 padding。
    const int textW = width() - 3 * kButtonWidth - 24;
    // 空标题 / 文字区不可用（过窄）：清空 scene buffer 并释放旧引用，
    // 防止旧文字残留（覆盖按钮或显示过期标题）。
    TitleTextBuffer* next = nullptr;
    if (textW > 0 && t != nullptr && *t != '\0') {
        // 标题文字颜色（主题驱动：深色主题白字、浅色主题深字）。
        const ThemeColor& tc = compositor_.theme().textPrimary;
        const float textColor[3] = {tc.r / 255.0f, tc.g / 255.0f, tc.b / 255.0f};
        next = renderTitleText(t, textW, kTitleBarHeight, textColor);
    }
    if (next == nullptr && titleText_ == nullptr) {
        return;  // 无旧文字可清，跳过（避免每帧重复 set_buffer(NULL)）
    }
    // 替换旧 buffer（set_buffer 内部 lock 新引用、unlock 旧引用；
    // 旧引用随后 drop 释放。传 NULL 时 scene 直接清空）。
    wlr_scene_buffer_set_buffer(titleTextNode_,
        next != nullptr ? &next->base : nullptr);
    if (titleText_ != nullptr) {
        wlr_buffer_drop(&titleText_->base);
    }
    titleText_ = next;
    wlr_scene_node_set_position(&titleTextNode_->node, 12, 0);
}

// M8：渲染/更新窗口阴影（尺寸变化时重绘；位置固定跟随装饰树）。
void View::updateShadow() {
    if (decorationTree_ == nullptr || shadowNode_ == nullptr) {
        return;
    }
    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) {
        return;
    }
    // 阴影覆盖整个窗口（标题栏 kTitleBarHeight + 内容区 h）。
    const int shadowW = w + 2 * kShadowSize;
    const int shadowH = kTitleBarHeight + h + 2 * kShadowSize;
    if (shadow_ != nullptr && shadow_->base.width == shadowW &&
            shadow_->base.height == shadowH) {
        return;  // 尺寸未变（位置由装饰树移动携带），无需重绘
    }
    ShadowBuffer* next = renderShadow(w, kTitleBarHeight + h, kShadowSize);
    if (next == nullptr) {
        return;
    }
    wlr_scene_buffer_set_buffer(shadowNode_, &next->base);
    if (shadow_ != nullptr) {
        wlr_buffer_drop(&shadow_->base);
    }
    shadow_ = next;
    wlr_scene_node_set_position(&shadowNode_->node, -kShadowSize, -kShadowSize);
}

void View::setHoverArea(DecorationArea area) {
    if (hoverArea_ == area || decorationTree_ == nullptr) {
        return;
    }
    hoverArea_ = area;
    // hover 打磨：按钮背景高亮（关闭更亮红）；颜色来自主题（自定义通道）。
    const Theme& th = compositor_.theme();
    float defaultButton[4], hoverButton[4], defaultClose[4], hoverClose[4];
    themeColorToFloat(th.buttonBg, defaultButton);
    themeColorToFloat(th.buttonHover, hoverButton);
    themeColorToFloat(th.closeBg, defaultClose);
    themeColorToFloat(th.closeHover, hoverClose);
    if (minButtonRect_ != nullptr) {
        wlr_scene_rect_set_color(minButtonRect_,
            area == DecorationArea::MinButton ? hoverButton : defaultButton);
    }
    if (maxButtonRect_ != nullptr) {
        wlr_scene_rect_set_color(maxButtonRect_,
            area == DecorationArea::MaxButton ? hoverButton : defaultButton);
    }
    if (closeButtonRect_ != nullptr) {
        wlr_scene_rect_set_color(closeButtonRect_,
            area == DecorationArea::CloseButton ? hoverClose : defaultClose);
    }
}

// ---- foreign-toplevel（任务栏窗口列表协议）----

void View::createForeignToplevel() {
    wlr_foreign_toplevel_manager_v1* manager = compositor_.foreignToplevelManager();
    if (manager == nullptr) {
        return;
    }
    ftHandle_ = wlr_foreign_toplevel_handle_v1_create(manager);
    if (ftHandle_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create foreign toplevel handle");
        return;
    }
    wlr_foreign_toplevel_handle_v1_set_title(ftHandle_,
        title() != nullptr ? title() : "");
    wlr_foreign_toplevel_handle_v1_set_app_id(ftHandle_,
        appId() != nullptr ? appId() : "");
    wlr_foreign_toplevel_handle_v1_set_activated(ftHandle_, false);
    if (wlr_output* output = compositor_.firstOutput(); output != nullptr) {
        wlr_foreign_toplevel_handle_v1_output_enter(ftHandle_, output);
    }

    ftMaximize_.notify = handleFtlMaximize;
    wl_signal_add(&ftHandle_->events.request_maximize, &ftMaximize_);
    ftMinimize_.notify = handleFtlMinimize;
    wl_signal_add(&ftHandle_->events.request_minimize, &ftMinimize_);
    ftActivate_.notify = handleFtlActivate;
    wl_signal_add(&ftHandle_->events.request_activate, &ftActivate_);
    ftFullscreen_.notify = handleFtlFullscreen;
    wl_signal_add(&ftHandle_->events.request_fullscreen, &ftFullscreen_);
    ftClose_.notify = handleFtlClose;
    wl_signal_add(&ftHandle_->events.request_close, &ftClose_);
    ftDestroy_.notify = handleFtlDestroy;
    wl_signal_add(&ftHandle_->events.destroy, &ftDestroy_);
}

void View::destroyForeignToplevel() {
    if (ftHandle_ == nullptr) {
        return;
    }
    // 先摘除监听再销毁（销毁会触发 destroy 信号，避免访问已释放链表）。
    // remove 后重新 init：析构体后续还会 remove 一遍，须保证链表有效。
    wl_list_remove(&ftMaximize_.link);
    wl_list_init(&ftMaximize_.link);
    wl_list_remove(&ftMinimize_.link);
    wl_list_init(&ftMinimize_.link);
    wl_list_remove(&ftActivate_.link);
    wl_list_init(&ftActivate_.link);
    wl_list_remove(&ftFullscreen_.link);
    wl_list_init(&ftFullscreen_.link);
    wl_list_remove(&ftClose_.link);
    wl_list_init(&ftClose_.link);
    wl_list_remove(&ftDestroy_.link);
    wl_list_init(&ftDestroy_.link);
    wlr_foreign_toplevel_handle_v1_destroy(ftHandle_);
    ftHandle_ = nullptr;
}

void View::handleFtlMaximize(wl_listener* listener, void* data) {
    auto* view = W10DE_CONTAINER_OF(listener, View, ftMaximize_);
    auto* event = static_cast<wlr_foreign_toplevel_handle_v1_maximized_event*>(data);
    view->setMaximized(event->maximized);
}

void View::handleFtlMinimize(wl_listener* listener, void* data) {
    auto* view = W10DE_CONTAINER_OF(listener, View, ftMinimize_);
    auto* event = static_cast<wlr_foreign_toplevel_handle_v1_minimized_event*>(data);
    view->setMinimized(event->minimized);
}

void View::handleFtlActivate(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, View, ftActivate_);
    // 任务栏点击窗口：恢复显示（若最小化）、聚焦并置顶（仅已映射窗口）。
    if (!view->mapped_) {
        return;
    }
    // 审查 #6：窗口在别的桌面时先切换过去（Win10 任务栏语义）。
    if (view->workspace() != view->compositor_.currentWorkspace()) {
        wlr_log(WLR_INFO, "ftl activate: switching to workspace %d",
                view->workspace());
        view->compositor_.switchWorkspace(view->workspace());
    }
    if (view->minimized_) {
        view->setMinimized(false);
    }
    view->compositor_.seat()->focusView(view);
    view->compositor_.raiseView(view);
}

void View::handleFtlFullscreen(wl_listener* listener, void* data) {
    auto* view = W10DE_CONTAINER_OF(listener, View, ftFullscreen_);
    auto* event = static_cast<wlr_foreign_toplevel_handle_v1_fullscreen_event*>(data);
    // M2a：fullscreen 暂按最大化处理。
    view->setMaximized(event->fullscreen);
}

void View::handleFtlClose(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, View, ftClose_);
    view->close();
}

void View::handleFtlDestroy(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, View, ftDestroy_);
    // handle 即将释放：摘除全部 ft 监听并重新初始化（析构可能再次 remove）。
    wl_list_remove(&view->ftMaximize_.link);
    wl_list_init(&view->ftMaximize_.link);
    wl_list_remove(&view->ftMinimize_.link);
    wl_list_init(&view->ftMinimize_.link);
    wl_list_remove(&view->ftActivate_.link);
    wl_list_init(&view->ftActivate_.link);
    wl_list_remove(&view->ftFullscreen_.link);
    wl_list_init(&view->ftFullscreen_.link);
    wl_list_remove(&view->ftClose_.link);
    wl_list_init(&view->ftClose_.link);
    wl_list_remove(&view->ftDestroy_.link);
    wl_list_init(&view->ftDestroy_.link);
    view->ftHandle_ = nullptr;
}

// ---- 事件回调 ----

void View::applyRules() {
    // KDE-GAP 中优先 #6：首条命中的 [window_rules] 规则生效
    //（KWin 语义简化：不合并多条规则）。
    const std::string appId = toplevel_->app_id != nullptr
        ? toplevel_->app_id : "";
    const std::string title = toplevel_->title != nullptr
        ? toplevel_->title : "";
    for (const auto& r : compositor_.windowRules()) {
        if (!r.matches(appId, title)) {
            continue;
        }
        wlr_log(WLR_INFO, "window rule applied (app_id='%s' title='%s'): "
                "ws=%d geom=%d,%d,%dx%d ontop=%d borderless=%d",
                appId.c_str(), title.c_str(), r.workspace,
                r.hasGeometry ? r.geomX : 0, r.hasGeometry ? r.geomY : 0,
                r.hasGeometry ? r.geomW : 0, r.hasGeometry ? r.geomH : 0,
                r.alwaysOnTop ? 1 : 0, r.borderless ? 1 : 0);
        if (r.workspace >= 0) {
            setWorkspace(r.workspace);
        }
        if (r.hasGeometry) {
            moveTo(r.geomX, r.geomY);
            resize(r.geomW, r.geomH);
            positionInitialized_ = true;  // 规则几何不再层叠
        }
        if (r.alwaysOnTop) {
            alwaysOnTop_ = true;
            // 审查 M4：内容置顶后装饰树需跟随置顶（否则
            // always_on_top + 非当前工作区场景装饰被内容盖住）。
            if (sceneTree_ != nullptr) {
                wlr_scene_node_raise_to_top(&sceneTree_->node);
                if (decorationTree_ != nullptr) {
                    wlr_scene_node_place_above(&decorationTree_->node,
                                               &sceneTree_->node);
                }
            }
        }
        if (r.borderless && !borderless_) {
            borderless_ = true;
            if (decorationTree_ != nullptr) {
                wlr_scene_node_destroy(&decorationTree_->node);
                decorationTree_ = nullptr;
            }
        }
        break;  // 首条命中生效
    }
}

void View::handleMap(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, View, map_);
    view->mapped_ = true;
    // remap（unmap 后再次 map）场景：复位最小化状态并恢复显示。
    if (view->minimized_) {
        view->setMinimized(false);
    }
    view->compositor_.addView(view);
    // 窗口规则（KDE-GAP 中优先 #6）：workspace/geometry/置顶/无边框。
    // 在初始位置与最大化处理之前应用，规则几何覆盖层叠放置。
    view->applyRules();
    // KWin 特效（低优先）：打开淡入（内容透明度 0→1）。
    view->startFadeIn();
    // 初始位置：仅首次 map 层叠放置，remap 保持原位置。
    if (!view->positionInitialized_) {
        static int cascade = 0;
        const int offset = 40;
        view->moveTo(100 + cascade * offset % 400, 80 + cascade * offset % 300);
        ++cascade;
        view->positionInitialized_ = true;
    }
    // map 前客户端已请求最大化（启动即最大化）：此时应用最大化几何，
    // 并保存当前（层叠后的）位置作为恢复几何。
    if (view->pendingFullscreen_) {
        // G2：map 前未初始化的 fullscreen 请求（surface 已 initialized，
        // setMaximized 安全）。
        view->pendingFullscreen_ = false;
        view->setMaximized(true);
    }
    if (view->maximized_) {
        int outW = 0, outH = 0;
        if (view->compositor_.outputUsableSize(
                view->compositor_.firstOutput(), &outW, &outH)) {
            if (!view->hasRestoreGeometry()) {
                view->setRestoreGeometry(view->x(), view->y(),
                                         view->width(), view->height());
            }
            view->moveTo(0, 0);
            int contentH = outH - View::kTitleBarHeight;
            if (contentH < 1) {
                contentH = 1;
            }
            view->resize(outW, contentH);
        }
    }
    // 显示装饰并同步尺寸（可见性按 工作区+最小化 统一判定，M7 续）。
    view->applyVisibility();
    view->renderTitle();  // M2b：map 时渲染标题文字
    view->updateShadow();  // M8：map 时渲染窗口阴影
    view->updateDecoration();
    // 审查 #1：窗口在非当前工作区（remap 场景）时不得获取焦点/激活/置顶，
    // 否则输入进入不可见窗口，破坏可见性不变量。
    if (!view->compositor_.viewVisible(view)) {
        wlr_log(WLR_INFO, "view mapped (hidden, workspace %d != current %d)",
                view->workspace(), view->compositor_.currentWorkspace());
        return;
    }
    view->setActivated(true);
    view->compositor_.seat()->focusView(view);
    // 置顶：map 顺序可能与创建顺序不同，确保新窗口在 z 序最上
    //（与 views_ 列表"末尾最上"的语义一致）。
    view->compositor_.raiseView(view);
    // M8 验证：--snap-test 自动贴左半屏（在焦点/置顶之后执行，几何可用）。
    if (view->snapOnMap_) {
        view->snapTo(SnapEdge::Left);
    }
    wlr_log(WLR_INFO, "view mapped: '%s' (%s) %dx%d at %d,%d",
            view->title() ? view->title() : "",
            view->appId() ? view->appId() : "",
            view->width(), view->height(), view->x(), view->y());
}

void View::handleUnmap(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, View, unmap_);
    view->mapped_ = false;
    view->cancelAnimation();  // 隐藏后动画不应继续空转（审查轻微项）
    view->applyVisibility();  // 统一显隐（工作区/最小化判定，M7 续）
    view->compositor_.removeView(view);
    // unmap 后 hover 高亮残留：重算（viewAt 不命中已 unmap 窗口，自动清除）。
    if (view->compositor_.seat() != nullptr) {
        view->compositor_.seat()->updateHover();
    }
    view->compositor_.seat()->unfocusView(view);
}

void View::handleCommit(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, View, commit_);
    // xdg-shell 协议要求：xdg_surface 首次 commit 后 compositor 必须回复
    // configure，客户端才能 attach buffer 并 map。尺寸传 0 表示由客户端自定
    // （tinywl 同款处理；缺失会导致客户端永远卡在等 configure）。
    if (view->toplevel_->base->initial_commit) {
        wlr_xdg_toplevel_set_size(view->toplevel_, 0, 0);
    }
    if (view->mapped_) {
        view->updateDecoration();
        view->updateShadow();  // M8：尺寸变化时重绘阴影
    }
}

void View::handleDestroy(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, View, destroy_);
    delete view;  // wlroots destroy 信号是最后一个事件，此后对象不再被引用。
}

void View::handleRequestMove(wl_listener* listener, void* data) {
    auto* view = W10DE_CONTAINER_OF(listener, View, requestMove_);
    auto* event = static_cast<wlr_xdg_toplevel_move_event*>(data);
    // 校验 serial 来自 seat 最近的输入事件，防伪造。
    if (view->mapped_ && event->seat != nullptr &&
            wlr_seat_client_validate_event_serial(event->seat, event->serial)) {
        view->compositor_.seat()->beginMove(view);
    }
}

void View::handleRequestResize(wl_listener* listener, void* data) {
    auto* view = W10DE_CONTAINER_OF(listener, View, requestResize_);
    auto* event = static_cast<wlr_xdg_toplevel_resize_event*>(data);
    if (view->mapped_ && event->seat != nullptr &&
            wlr_seat_client_validate_event_serial(event->seat, event->serial)) {
        view->compositor_.seat()->beginResize(view, event->edges);
    }
}

void View::handleRequestMaximize(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, View, requestMaximize_);
    // 事件不携带目标状态，以客户端请求的为准（避免无条件翻转失步）。
    view->setMaximized(view->toplevel()->requested.maximized);
}

void View::handleRequestMinimize(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, View, requestMinimize_);
    // 客户端 set_minimized 请求即请求最小化。
    view->setMinimized(true);
}

void View::handleRequestFullscreen(wl_listener* listener, void* /*data*/) {
    // M2a：fullscreen 暂按最大化处理，后续里程碑完善独立 fullscreen 状态。
    auto* view = W10DE_CONTAINER_OF(listener, View, requestFullscreen_);
    const bool fs = view->toplevel()->requested.fullscreen;
    wlr_log(WLR_INFO, "fullscreen request (M2a: treating as maximize)");
    // G2 审查：surface 未初始化（首次 commit 前）时 wlr_xdg_toplevel_
    // set_maximized 内部 schedule_configure 断言崩溃（Qt 客户端
    // showFullScreen 可能在首个 commit 前发 set_fullscreen）——记录待
    // map 时应用（与 xdg-decoration 延迟设置同款防护）。
    wlr_xdg_surface* xdg = view->toplevel()->base;
    if (xdg != nullptr && !xdg->initialized) {
        view->pendingFullscreen_ = fs;
        wlr_log(WLR_INFO, "fullscreen request before surface init: deferred to map");
        return;
    }
    // M5 审查（G2）：surface 已初始化路径清除 pending——覆盖"uninit 时
    // fullscreen(true) 记 pending，init 后 map 前客户端取消全屏"的残留
    //（否则 map 时仍消费旧 pending 错误最大化）。
    view->pendingFullscreen_ = false;
    view->setMaximized(fs);
}

void View::handleSetTitle(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, View, setTitle_);
    wlr_log(WLR_DEBUG, "toplevel title set: '%s'", view->title() ? view->title() : "");
    if (view->ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_title(view->ftHandle_,
            view->title() != nullptr ? view->title() : "");
    }
    // M2b：标题变化时刷新标题栏文字。
    view->renderTitle();
}

void View::handleSetAppId(wl_listener* listener, void* /*data*/) {
    auto* view = W10DE_CONTAINER_OF(listener, View, setAppId_);
    wlr_log(WLR_DEBUG, "toplevel app_id set: '%s'", view->appId() ? view->appId() : "");
    if (view->ftHandle_ != nullptr) {
        wlr_foreign_toplevel_handle_v1_set_app_id(view->ftHandle_,
            view->appId() != nullptr ? view->appId() : "");
    }
}

}  // namespace w10de

#include "compositor/alttab.h"

#include "compositor/util.h"

#include "compositor/server.h"
#include "compositor/seat.h"
#include "compositor/titletext.h"
#include "compositor/view.h"
#include "compositor/xview.h"

namespace w10de {

namespace {

// 切换器条目尺寸（标题块）。
constexpr int kEntryW = 220;
constexpr int kEntryH = 56;   // 标题条高度（MVP：无内容缩略图，仅标题）
constexpr int kGap = 14;
constexpr int kMaxEntries = 12;  // 超出截断（Win10 亦截断显示）

}  // namespace

AltTabSwitcher::AltTabSwitcher(Compositor& compositor)
    : compositor_(compositor) {}

AltTabSwitcher::~AltTabSwitcher() {
    hide();
}

bool AltTabSwitcher::show() {
    if (active_) {
        return true;
    }
    // 收集候选：当前工作区可见窗口（xdg 优先序 = z 序，XView 附加其后）。
    entries_.clear();
    for (View* v : compositor_.views()) {
        if (!v->mapped() || v->minimized() ||
                v->workspace() != compositor_.currentWorkspace()) {
            continue;
        }
        Entry e;
        e.view = v;
        e.isXView = false;
        e.title = v->title() != nullptr ? v->title() : "";
        entries_.push_back(std::move(e));
    }
    for (XView* x : compositor_.xviews()) {
        if (!x->mapped() || x->minimized() ||
                x->workspace() != compositor_.currentWorkspace()) {
            continue;
        }
        Entry e;
        e.view = x;
        e.isXView = true;
        e.title = x->title() != nullptr ? x->title() : "";
        entries_.push_back(std::move(e));
    }
    if (entries_.empty()) {
        return false;
    }
    // 初始选中：当前焦点窗口；否则最上层（z 序末位）。
    current_ = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (!entries_[i].isXView &&
                static_cast<View*>(entries_[i].view) == compositor_.seat()->focusedView()) {
            current_ = static_cast<int>(i);
            break;
        }
    }
    buildUi();
    active_ = true;
    return true;
}

void AltTabSwitcher::buildUi() {
    tree_ = wlr_scene_tree_create(&compositor_.scene()->tree);
    if (tree_ == nullptr) {
        return;
    }
    const size_t n = std::min(entries_.size(), static_cast<size_t>(kMaxEntries));
    const int totalW = static_cast<int>(n) * kEntryW + static_cast<int>(n - 1) * kGap;
    int outW = 0, outH = 0;
    compositor_.outputSize(&outW, &outH);
    const int x0 = outW > 0 ? (outW - totalW) / 2 : 0;
    const int y0 = outH > 0 ? outH / 2 - kEntryH / 2 : 0;

    const Theme& th = compositor_.theme();
    float normalColor[4];
    themeColorToFloat(th.titlebarBg, normalColor);
    for (size_t i = 0; i < n; ++i) {
        Entry& e = entries_[i];
        e.tree = wlr_scene_tree_create(tree_);
        if (e.tree == nullptr) {
            continue;
        }
        const int x = x0 + static_cast<int>(i) * (kEntryW + kGap);
        wlr_scene_node_set_position(&e.tree->node, x, y0);
        // 背景块（初始为正常色；选中高亮由 updateHighlight 切换）。
        e.bg = wlr_scene_rect_create(e.tree, kEntryW, kEntryH, normalColor);
        if (e.bg == nullptr) {
            continue;
        }
        // 标题文字（cairo，主题主文字色）。
        e.text = wlr_scene_buffer_create(e.tree, nullptr);
        if (e.text == nullptr) {
            continue;
        }
        const int pad = 10;
        const int textW = kEntryW - 2 * pad;
        if (textW > 0 && !e.title.empty()) {
            const ThemeColor& tc = th.textPrimary;
            const float color[3] = {tc.r / 255.0f, tc.g / 255.0f, tc.b / 255.0f};
            TitleTextBuffer* buf = renderTitleText(
                e.title.c_str(), textW, kEntryH, color);
            if (buf != nullptr) {
                wlr_scene_buffer_set_buffer(e.text, &buf->base);
                wlr_scene_node_set_position(&e.text->node, pad, 0);
                wlr_buffer_drop(&buf->base);  // scene 已 lock，释放初始引用
            }
        }
    }
    updateHighlight();
}

void AltTabSwitcher::updateHighlight() {
    const Theme& th = compositor_.theme();
    float normalColor[4], accentColor[4];
    themeColorToFloat(th.titlebarBg, normalColor);
    themeColorToFloat(th.accent, accentColor);
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].bg != nullptr) {
            wlr_scene_rect_set_color(entries_[i].bg,
                static_cast<int>(i) == current_ ? accentColor : normalColor);
        }
    }
}

void AltTabSwitcher::next() {
    if (!active_ || entries_.empty()) {
        return;
    }
    current_ = (current_ + 1) % static_cast<int>(entries_.size());
    updateHighlight();
}

void AltTabSwitcher::prev() {
    if (!active_ || entries_.empty()) {
        return;
    }
    current_ = (current_ - 1 + static_cast<int>(entries_.size()))
        % static_cast<int>(entries_.size());
    updateHighlight();
}

void AltTabSwitcher::hideAndApply() {
    if (!active_) {
        return;
    }
    const Entry& sel = entries_[current_];
    // 应用选择：聚焦 + 置顶。
    if (!sel.isXView) {
        auto* view = static_cast<View*>(sel.view);
        if (view->mapped()) {
            if (view->minimized()) {
                view->setMinimized(false);
            }
            compositor_.seat()->focusView(view);
            compositor_.raiseView(view);
        }
    } else {
        auto* xview = static_cast<XView*>(sel.view);
        if (xview->mapped()) {
            if (xview->minimized()) {
                xview->setMinimized(false);
            }
            xview->activate(true);
            compositor_.raiseXView(xview);
        }
    }
    hide();
    wlr_log(WLR_INFO, "alttab: selected '%s'", sel.title.c_str());
}

void AltTabSwitcher::hide() {
    active_ = false;
    if (tree_ != nullptr) {
        wlr_scene_node_destroy(&tree_->node);
        tree_ = nullptr;
    }
    entries_.clear();
    current_ = 0;
}

}  // namespace w10de

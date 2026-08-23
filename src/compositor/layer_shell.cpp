#include "compositor/layer_shell.h"

#include "compositor/server.h"

namespace w10de {

LayerSurface::LayerSurface(Compositor& compositor, wlr_layer_surface_v1* surface)
    : compositor_(compositor), layer_(surface) {
    wl_list_init(&destroy_.link);
    wl_list_init(&map_.link);
    wl_list_init(&unmap_.link);
    wl_list_init(&commit_.link);

    // 层表面必须绑定一个输出（协议允许 NULL，由合成器分配）。
    if (layer_->output == nullptr) {
        wlr_output* output = compositor.firstOutput();
        if (output == nullptr) {
            wlr_log(WLR_ERROR, "layer surface created with no output available");
            return;
        }
        layer_->output = output;
    }

    // scene 节点：map 后内容可见，位置由 arrange 设置。
    sceneLayer_ = wlr_scene_layer_surface_v1_create(&compositor.scene()->tree, layer_);
    if (sceneLayer_ == nullptr) {
        wlr_log(WLR_ERROR, "failed to create scene layer surface");
        return;
    }

    destroy_.notify = handleDestroy;
    wl_signal_add(&layer_->events.destroy, &destroy_);
    // 0.19 中 layer surface 无 map/unmap 事件，使用底层 wlr_surface 事件。
    map_.notify = handleMap;
    wl_signal_add(&layer_->surface->events.map, &map_);
    unmap_.notify = handleUnmap;
    wl_signal_add(&layer_->surface->events.unmap, &unmap_);
    commit_.notify = handleCommit;
    wl_signal_add(&layer_->surface->events.commit, &commit_);

    wlr_log(WLR_INFO, "new layer surface: namespace='%s' layer=%d",
            layer_->namespace ? layer_->namespace : "", layer_->pending.layer);
}

LayerSurface::~LayerSurface() {
    wl_list_remove(&destroy_.link);
    wl_list_remove(&map_.link);
    wl_list_remove(&unmap_.link);
    wl_list_remove(&commit_.link);
    // scene 节点由 scene 根销毁时连带；layer surface 由 wlroots 管理。
}

void LayerSurface::arrange(const wlr_box* fullArea, wlr_box* usableArea) {
    // 复用 wlroots 官方 helper：基于 current 状态计算几何（锚点/边距/独占区/
    // 期望尺寸）、设置 scene 节点位置、发送 configure 事件，并在映射且
    // 独占区 > 0 时更新 usable_area 供后续层使用。
    // 该 helper 的独占区语义（按锚点组合）比手写实现更符合协议。
    wlr_scene_layer_surface_v1_configure(sceneLayer_, fullArea, usableArea);
}

wlr_surface* LayerSurface::surfaceAt(double lx, double ly, double* sx, double* sy) const {
    if (sceneLayer_ == nullptr || !layer_->surface->mapped) {
        return nullptr;
    }
    const double localX = lx - sceneX();
    const double localY = ly - sceneY();
    if (localX < 0 || localY < 0) {
        return nullptr;
    }
    return wlr_layer_surface_v1_surface_at(layer_, localX, localY, sx, sy);
}

// ---- 事件回调 ----

void LayerSurface::handleDestroy(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, destroy_);
    self->compositor_.removeLayerSurface(self);
    delete self;  // wlroots destroy 信号是最后一个事件。
}

void LayerSurface::handleMap(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, map_);
    const char* ns = self->layer_->namespace != nullptr ? self->layer_->namespace : "";
    wlr_log(WLR_INFO, "layer surface mapped: '%s' layer=%d", ns,
            static_cast<int>(self->layer_->current.layer));
    // 挂到对应层锚：background/bottom 在窗口下，top/overlay 在窗口上。
    wlr_scene_node_reparent(&self->sceneLayer_->tree->node,
        self->compositor_.layerAnchor(
            static_cast<int>(self->layer_->current.layer)));
    // map 后实际尺寸确定，重排（可能影响独占区/其他层）。
    self->compositor_.arrangeLayers();
}

void LayerSurface::handleUnmap(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, unmap_);
    self->compositor_.arrangeLayers();
}

void LayerSurface::handleCommit(wl_listener* listener, void* /*data*/) {
    auto* self = wl_container_of(listener, self, commit_);
    // 客户端可通过 commit 修改 layer（如 top→overlay）：scene 节点必须
    // 重新挂锚，否则渲染 z 序与命中检测（按 current.layer）不一致
    // （wlr_scene_layer_surface_v1_configure 只设置位置/尺寸，不 reparent）。
    const int layer = static_cast<int>(self->layer_->current.layer);
    wlr_scene_tree* anchor = self->compositor_.layerAnchor(layer);
    if (self->sceneLayer_ != nullptr && anchor != nullptr &&
            self->sceneLayer_->tree->node.parent != &anchor->node) {
        wlr_scene_node_reparent(&self->sceneLayer_->tree->node, anchor);
    }
    // 属性（锚点/边距/独占区/期望尺寸）变化时重排。
    self->compositor_.arrangeLayers();
}

}  // namespace w10de

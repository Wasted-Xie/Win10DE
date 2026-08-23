// layer-shell 层表面管理（M3 前置：任务栏/桌面/锁屏等 Shell UI 的宿主）
//
// wlr-layer-shell 允许客户端（我们的 Qt Shell）在桌面上分层排列表面：
//   background（桌面壁纸）→ bottom（任务栏下）→ top（任务栏/开始菜单）→ overlay（锁屏）。
// 本类负责：创建 scene 节点、按锚点/边距/独占区计算几何并 configure、
// 响应客户端尺寸与属性变化。
#pragma once

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
}

namespace w10de {

class Compositor;

class LayerSurface {
public:
    LayerSurface(Compositor& compositor, wlr_layer_surface_v1* surface);
    ~LayerSurface();

    LayerSurface(const LayerSurface&) = delete;
    LayerSurface& operator=(const LayerSurface&) = delete;

    bool isValid() const { return sceneLayer_ != nullptr; }

    wlr_layer_surface_v1* layer() const { return layer_; }

    // 命中检测：layout 坐标 → 层表面内的实际 surface（含子树/popup）。
    // 命中返回 surface 与 surface-local 坐标；未命中返回 nullptr。
    wlr_surface* surfaceAt(double lx, double ly, double* sx, double* sy) const;

    // 场景节点位置（布局坐标，由 arrange 设置）。
    int sceneX() const { return sceneLayer_ != nullptr ? sceneLayer_->tree->node.x : 0; }
    int sceneY() const { return sceneLayer_ != nullptr ? sceneLayer_->tree->node.y : 0; }

    // 依据 pending 状态（锚点/边距/独占区/期望尺寸）计算几何并发送 configure。
    // fullArea：输出可用的全部区域；usableArea：扣除已占用独占区后剩余区域
    // （供后续层使用，本函数内按独占区更新）。
    void arrange(const wlr_box* fullArea, wlr_box* usableArea);

private:
    static void handleDestroy(wl_listener* l, void* data);
    static void handleMap(wl_listener* l, void* data);
    static void handleUnmap(wl_listener* l, void* data);
    static void handleCommit(wl_listener* l, void* data);

    Compositor& compositor_;
    wlr_layer_surface_v1* layer_ = nullptr;
    wlr_scene_layer_surface_v1* sceneLayer_ = nullptr;
    wl_listener destroy_ = {}, map_ = {}, unmap_ = {}, commit_ = {};
};

}  // namespace w10de

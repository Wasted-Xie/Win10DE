// CompositorDbus —— compositor 的 D-Bus 服务（org.w10de.Compositor）。
//
// 暴露输出管理接口（第二批显示设置用；设置应用 w10settings 经 QDBus 调用）：
//   GetOutputs()  → a(siiiii)  (name, width, height, scalePercent, x, y)
//   GetModes(s)   → a(ii)      输出支持的分辨率列表
//   SetMode(s, i, i)            设置分辨率（wlr_output_set_custom_mode）
//   SetScale(s, i)              设置缩放百分比（125 = 1.25x）
//   SetPosition(s, i, i)        设置输出位置（多屏排列）
//
// 集成：连接 session bus（DBUS_SESSION_BUS_ADDRESS），dbus fd 挂到
// wl_event_loop（wl_event_loop_add_fd），可读时 read_write + dispatch——
// 单线程内与 wl_display 事件循环共存。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <dbus/dbus.h>
#include <wayland-server-core.h>
}

namespace w10de {

class Compositor;

class CompositorDbus {
public:
    explicit CompositorDbus(Compositor& compositor);
    ~CompositorDbus();

    CompositorDbus(const CompositorDbus&) = delete;
    CompositorDbus& operator=(const CompositorDbus&) = delete;

    // 连接 bus + 注册服务/对象路径 + 挂 fd 到事件循环；失败返回 false。
    bool init();
    // dbus fd 可读回调（wl_event_loop fd 回调入口）。
    void onReadable();

    bool valid() const { return conn_ != nullptr; }

private:
    static DBusHandlerResult handleMessage(DBusConnection* conn,
                                           DBusMessage* message, void* userData);
    static int dbusFdCallback(int fd, uint32_t mask, void* data);

    // 方法分发：成功返回 true 并填充 reply；失败返回 false（reply 为空）。
    bool handleMethod(const char* method, DBusMessage* message,
                      DBusMessageIter* args, DBusMessage** reply);
    void sendReply(DBusMessage* request, DBusMessage* reply);

    // 输出信息收集（GetOutputs 用）。
    struct OutputInfo {
        std::string name;
        int width = 0;
        int height = 0;
        int scalePercent = 100;
        int x = 0;
        int y = 0;
    };
    std::vector<OutputInfo> collectOutputs() const;

    Compositor& compositor_;
    DBusConnection* conn_ = nullptr;
    wl_event_source* eventSource_ = nullptr;
};

}  // namespace w10de

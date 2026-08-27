#include "compositor/dbus_service.h"

#include "compositor/output.h"
#include "compositor/seat.h"
#include "compositor/server.h"
#include "compositor/util.h"
#include "compositor/view.h"
#include "compositor/xview.h"

#include <wlr/util/log.h>

#include <cerrno>
#include <cstring>

#include "ipc/inputsettings.h"
#include "ipc/nightlight.h"

namespace w10de {

namespace {

// 输出模式列表：headless/无 modes 时返回空（设置应用显示"当前模式"）。
// 从 wlr_output 的 modes 链表收集（wlr_output_mode 有 width/height）。
void collectModes(wlr_output* output, std::vector<std::pair<int, int>>* modes) {
    if (output == nullptr || modes == nullptr) {
        return;
    }
    wlr_output_mode* mode;
    wl_list_for_each(mode, &output->modes, link) {
        modes->emplace_back(mode->width, mode->height);
    }
}

// 参数迭代辅助：取一个 int32（失败返回 false）。
bool nextInt32(DBusMessageIter* it, int32_t* out) {
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_INT32) {
        return false;
    }
    dbus_message_iter_get_basic(it, out);
    dbus_message_iter_next(it);
    return true;
}

// 参数迭代辅助：取字符串（失败返回 false；返回指针指向消息内内存）。
bool nextString(DBusMessageIter* it, const char** out) {
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_STRING) {
        return false;
    }
    dbus_message_iter_get_basic(it, out);
    dbus_message_iter_next(it);
    return true;
}

// 参数迭代辅助：取 double（KDE-GAP 中优先：输入设备设置）。
bool nextDouble(DBusMessageIter* it, double* out) {
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_DOUBLE) {
        return false;
    }
    dbus_message_iter_get_basic(it, out);
    dbus_message_iter_next(it);
    return true;
}

// 参数迭代辅助：取布尔（DBUS_TYPE_BOOLEAN）。
bool nextBool(DBusMessageIter* it, bool* out) {
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_BOOLEAN) {
        return false;
    }
    dbus_bool_t v = FALSE;
    dbus_message_iter_get_basic(it, &v);
    *out = v != FALSE;
    dbus_message_iter_next(it);
    return true;
}

}  // namespace

CompositorDbus::CompositorDbus(Compositor& compositor) : compositor_(compositor) {}

CompositorDbus::~CompositorDbus() {
    if (eventSource_ != nullptr) {
        wl_event_source_remove(eventSource_);
        eventSource_ = nullptr;
    }
    if (conn_ != nullptr) {
        // 对象路径 vtable 引用 this：必须注销（审查 M4——降级路径析构后
        // 进程继续运行，客户端再调 /Outputs 会经 vtable 访问已析构对象）。
        dbus_connection_unregister_object_path(conn_, "/Outputs");
        // dbus_bus_get 返回**共享**连接：禁止 dbus_connection_close（libdbus
        // 断言 "Applications must not close shared connections"——实测 abort）。
        // 只 unref（连接由 libdbus 在进程退出时清理）。
        dbus_connection_unref(conn_);
        conn_ = nullptr;
    }
}

bool CompositorDbus::init() {
    DBusError err;
    dbus_error_init(&err);
    conn_ = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (conn_ == nullptr) {
        wlr_log(WLR_ERROR, "dbus: failed to open session bus: %s",
                err.message ? err.message : "unknown");
        dbus_error_free(&err);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(conn_, false);

    // 服务名（竞争失败仅警告：可能已有实例/其他持有者）。
    DBusError nameErr;
    dbus_error_init(&nameErr);
    const int ret = dbus_bus_request_name(conn_, "org.w10de.Compositor",
                                          DBUS_NAME_FLAG_DO_NOT_QUEUE, &nameErr);
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        wlr_log(WLR_ERROR, "dbus: request name org.w10de.Compositor: %s",
                nameErr.message ? nameErr.message : "not primary owner");
        dbus_error_free(&nameErr);
        dbus_connection_unref(conn_);
        conn_ = nullptr;
        return false;
    }
    dbus_error_free(&nameErr);

    // 对象路径 /Outputs：方法分发走 handleMessage。
    static DBusObjectPathVTable vtable = {nullptr, handleMessage, nullptr,
                                          nullptr, nullptr, nullptr};
    if (!dbus_connection_register_object_path(conn_, "/Outputs", &vtable, this)) {
        wlr_log(WLR_ERROR, "dbus: failed to register object path /Outputs");
        dbus_connection_unref(conn_);
        conn_ = nullptr;
        return false;
    }

    // dbus fd 挂到 wl_event_loop（单线程共存）。
    int dbusFd = -1;
    if (!dbus_connection_get_unix_fd(conn_, &dbusFd) || dbusFd < 0) {
        wlr_log(WLR_ERROR, "dbus: no unix fd available");
        dbus_connection_unregister_object_path(conn_, "/Outputs");
        dbus_connection_unref(conn_);
        conn_ = nullptr;
        return false;
    }
    wl_event_loop* loop = wl_display_get_event_loop(compositor_.display());
    if (loop == nullptr) {
        wlr_log(WLR_ERROR, "dbus: no display event loop");
        dbus_connection_unregister_object_path(conn_, "/Outputs");
        dbus_connection_unref(conn_);
        conn_ = nullptr;
        return false;
    }
    eventSource_ = wl_event_loop_add_fd(loop, dbusFd, WL_EVENT_READABLE,
                                        dbusFdCallback, this);
    if (eventSource_ == nullptr) {
        wlr_log(WLR_ERROR, "dbus: failed to add fd to event loop");
        dbus_connection_unregister_object_path(conn_, "/Outputs");
        dbus_connection_unref(conn_);
        conn_ = nullptr;
        return false;
    }
    wlr_log(WLR_INFO, "dbus: compositor service ready (org.w10de.Compositor, /Outputs)");
    return true;
}

void CompositorDbus::onReadable() {
    if (conn_ == nullptr) {
        return;
    }
    // 读取挂起输入并分发消息（同一线程，非阻塞）。
    if (!dbus_connection_read_write(conn_, 0)) {
        // 连接断开（bus 退出等）：清理。
        wlr_log(WLR_ERROR, "dbus: connection closed");
        if (eventSource_ != nullptr) {
            wl_event_source_remove(eventSource_);
            eventSource_ = nullptr;
        }
        dbus_connection_unref(conn_);
        conn_ = nullptr;
        return;
    }
    while (dbus_connection_dispatch(conn_) == DBUS_DISPATCH_DATA_REMAINS) {
        // 分派全部挂起消息（方法调用/信号）。
    }
}

// ---- 方法分发 ----

DBusHandlerResult CompositorDbus::handleMessage(DBusConnection* /*conn*/,
                                                DBusMessage* message,
                                                void* userData) {
    auto* self = static_cast<CompositorDbus*>(userData);
    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    const char* iface = dbus_message_get_interface(message);
    if (iface == nullptr || std::strcmp(iface, "org.w10de.Compositor") != 0) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    DBusMessageIter args;
    dbus_message_iter_init(message, &args);
    DBusMessage* reply = nullptr;
    const char* method = dbus_message_get_member(message);
    if (method == nullptr || !self->handleMethod(method, message, &args, &reply)) {
        // 未知方法或参数错误：返回 InvalidArgs（审查 L3——UNKNOWN_METHOD
        // 不精确；客户端查 isValid 即可，但协议语义应准确）。
        DBusError err;
        dbus_error_init(&err);
        dbus_set_error_const(&err, DBUS_ERROR_INVALID_ARGS,
                             "unknown method or bad arguments");
        reply = dbus_message_new_error(message, err.name, err.message);
        dbus_error_free(&err);
    }
    self->sendReply(message, reply);
    if (reply != nullptr) {
        dbus_message_unref(reply);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

void CompositorDbus::sendReply(DBusMessage* request, DBusMessage* reply) {
    if (reply == nullptr || conn_ == nullptr) {
        return;
    }
    dbus_connection_send(conn_, reply, nullptr);
    dbus_connection_flush(conn_);
}

bool CompositorDbus::handleMethod(const char* method, DBusMessage* message,
                                  DBusMessageIter* args, DBusMessage** reply) {
    if (std::strcmp(method, "GetOutputs") == 0) {
        const auto outputs = collectOutputs();
        *reply = dbus_message_new_method_return(message);
        DBusMessageIter root;
        dbus_message_iter_init_append(*reply, &root);
        DBusMessageIter arr;
        dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY,
                                         "(siiiii)", &arr);
        for (const OutputInfo& o : outputs) {
            DBusMessageIter st;
            dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, nullptr, &st);
            const char* name = o.name.c_str();
            int32_t w = o.width, h = o.height, sc = o.scalePercent, x = o.x, y = o.y;
            dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &name);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &w);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &h);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &sc);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &x);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &y);
            dbus_message_iter_close_container(&arr, &st);
        }
        dbus_message_iter_close_container(&root, &arr);
        return true;
    }
    if (std::strcmp(method, "GetViews") == 0) {
        // G2：窗口列表（截图工具 --window 用）：a(ssiiii) =
        // (app_id, title, x, y, w, h)。仅已映射窗口（跨全部工作区，
        // 由截图工具按需选择）。
        *reply = dbus_message_new_method_return(message);
        DBusMessageIter root;
        dbus_message_iter_init_append(*reply, &root);
        DBusMessageIter arr;
        dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY,
                                         "(ssiiii)", &arr);
        const auto appendView = [&arr](const char* appId, const char* title,
                                       int x, int y, int w, int h) {
            DBusMessageIter st;
            dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, nullptr,
                                             &st);
            const char* a = appId != nullptr ? appId : "";
            const char* t = title != nullptr ? title : "";
            int32_t xi = x, yi = y, wi = w, hi = h;
            dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &a);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &t);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &xi);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &yi);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &wi);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &hi);
            dbus_message_iter_close_container(&arr, &st);
        };
        for (const View* v : compositor_.views()) {
            if (!v->mapped()) {
                continue;
            }
            appendView(v->appId(), v->title(), v->x(), v->y(), v->width(),
                       v->height());
        }
        for (const XView* v : compositor_.xviews()) {
            if (!v->mapped()) {
                continue;
            }
            // XView 无 app_id 访问器（X11 class 处理见 xview.cpp）——
            // 统一给空串，匹配可用 title。
            appendView("", v->title(), v->x(), v->y(), v->width(),
                       v->height());
        }
        dbus_message_iter_close_container(&root, &arr);
        return true;
    }
    if (std::strcmp(method, "GetModes") == 0) {
        const char* name = nullptr;
        if (!nextString(args, &name)) {
            return false;
        }
        wlr_output* output = compositor_.findOutputByName(name);
        if (output == nullptr) {
            // 输出不存在：返回错误（审查 L8——客户端可区分"无输出"与
            // "无 modes"；返回 false 走 InvalidArgs 错误）。
            return false;
        }
        *reply = dbus_message_new_method_return(message);
        DBusMessageIter root;
        dbus_message_iter_init_append(*reply, &root);
        DBusMessageIter arr;
        dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "(ii)", &arr);
        std::vector<std::pair<int, int>> modes;
        collectModes(output, &modes);
        for (const auto& [w, h] : modes) {
            DBusMessageIter st;
            dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, nullptr, &st);
            int32_t wi = w, hi = h;
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &wi);
            dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &hi);
            dbus_message_iter_close_container(&arr, &st);
        }
        dbus_message_iter_close_container(&root, &arr);
        return true;
    }
    if (std::strcmp(method, "SetMode") == 0) {
        const char* name = nullptr;
        int32_t w = 0, h = 0;
        if (!nextString(args, &name) || !nextInt32(args, &w) || !nextInt32(args, &h) ||
                w <= 0 || h <= 0) {
            return false;
        }
        wlr_output* output = compositor_.findOutputByName(name);
        if (output == nullptr) {
            return false;
        }
        // 0.19 输出配置走 state API（wlr_output_set_custom_mode 已移除）。
        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_custom_mode(&state, w, h, 0);
        wlr_output_state_set_enabled(&state, true);
        const bool ok = wlr_output_commit_state(output, &state);
        wlr_output_state_finish(&state);
        if (!ok) {
            wlr_log(WLR_ERROR, "dbus: SetMode %dx%d commit failed", w, h);
            return false;
        }
        compositor_.arrangeLayers();
        wlr_log(WLR_INFO, "dbus: SetMode '%s' %dx%d", name, w, h);
        *reply = dbus_message_new_method_return(message);
        return true;
    }
    if (std::strcmp(method, "GetInputSettings") == 0) {
        // KDE-GAP 中优先：返回当前生效的输入设备设置。
        const auto s = compositor_.seat()->inputSettings();
        *reply = dbus_message_new_method_return(message);
        DBusMessageIter root;
        dbus_message_iter_init_append(*reply, &root);
        dbus_message_iter_append_basic(&root, DBUS_TYPE_DOUBLE, &s.pointerSpeed);
        dbus_bool_t natural = s.naturalScroll;
        dbus_bool_t left = s.leftHanded;
        dbus_bool_t tap = s.tapToClick;
        dbus_message_iter_append_basic(&root, DBUS_TYPE_BOOLEAN, &natural);
        dbus_message_iter_append_basic(&root, DBUS_TYPE_BOOLEAN, &left);
        dbus_message_iter_append_basic(&root, DBUS_TYPE_BOOLEAN, &tap);
        // 审查 L：DBUS_TYPE_INT32 须传 int32_t 局部变量（libdbus 按 4 字节读）。
        const int32_t rate = static_cast<int32_t>(s.repeatRate);
        const int32_t delay = static_cast<int32_t>(s.repeatDelay);
        dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &rate);
        dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &delay);
        return true;
    }
    if (std::strcmp(method, "SetInputSettings") == 0) {
        // KDE-GAP 中优先：热应用输入设备设置（参数
        // d:b:b:b:i:i = speed, natural, left, tap, repeat_rate, repeat_delay）。
        double speed = 0.0;
        bool natural = false, left = false, tap = false;
        int32_t rate = 25, delay = 600;
        if (!nextDouble(args, &speed) || !nextBool(args, &natural) ||
                !nextBool(args, &left) || !nextBool(args, &tap) ||
                !nextInt32(args, &rate) || !nextInt32(args, &delay) ||
                speed < -1.0 || speed > 1.0 || rate < 1 || rate > 100 ||
                delay < 100 || delay > 5000) {
            return false;
        }
        w10de::ipc::InputSettings s;
        s.pointerSpeed = speed;
        s.naturalScroll = natural;
        s.leftHanded = left;
        s.tapToClick = tap;
        s.repeatRate = rate;
        s.repeatDelay = delay;
        compositor_.seat()->applyInputSettings(s);
        wlr_log(WLR_INFO, "dbus: SetInputSettings speed=%.2f natural=%d left=%d "
                "tap=%d repeat=%d/%d", speed, natural ? 1 : 0, left ? 1 : 0,
                tap ? 1 : 0, rate, delay);
        *reply = dbus_message_new_method_return(message);
        return true;
    }
    if (std::strcmp(method, "SetScale") == 0) {
        const char* name = nullptr;
        int32_t scalePercent = 0;
        if (!nextString(args, &name) || !nextInt32(args, &scalePercent) ||
                scalePercent < 50 || scalePercent > 400) {
            return false;
        }
        wlr_output* output = compositor_.findOutputByName(name);
        if (output == nullptr) {
            return false;
        }
        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_scale(&state, scalePercent / 100.0f);
        const bool ok = wlr_output_commit_state(output, &state);
        wlr_output_state_finish(&state);
        if (!ok) {
            wlr_log(WLR_ERROR, "dbus: SetScale %d%% commit failed", scalePercent);
            return false;
        }
        compositor_.arrangeLayers();
        wlr_log(WLR_INFO, "dbus: SetScale '%s' %d%%", name, scalePercent);
        *reply = dbus_message_new_method_return(message);
        return true;
    }
    if (std::strcmp(method, "SetPosition") == 0) {
        const char* name = nullptr;
        int32_t x = 0, y = 0;
        if (!nextString(args, &name) || !nextInt32(args, &x) || !nextInt32(args, &y)) {
            return false;
        }
        wlr_output* output = compositor_.findOutputByName(name);
        if (output == nullptr) {
            return false;
        }
        wlr_output_layout_add(compositor_.outputLayout(), output, x, y);
        compositor_.arrangeLayers();
        wlr_log(WLR_INFO, "dbus: SetPosition '%s' (%d,%d)", name, x, y);
        *reply = dbus_message_new_method_return(message);
        return true;
    }
    if (std::strcmp(method, "SetNightLight") == 0) {
        // G1：Night Light 热应用（参数 b:i:i:i = enabled, temp, start, end）。
        bool enabled = false;
        int32_t temperature = 0, startMinutes = 0, endMinutes = 0;
        if (!nextBool(args, &enabled) || !nextInt32(args, &temperature) ||
                !nextInt32(args, &startMinutes) || !nextInt32(args, &endMinutes) ||
                temperature < 1000 || temperature > 8000 ||
                startMinutes < 0 || startMinutes > 1439 ||
                endMinutes < 0 || endMinutes > 1439 ||
                startMinutes == endMinutes) {
            // 审查 M2（G1）：start==end 拒绝——热应用 isNightActive 会恒真
            // （全天），而启动加载 loadNightLightConfig 对 start==end 回退
            // 默认 18:00-06:00，两者语义漂移。
            return false;
        }
        w10de::ipc::NightLightConfig cfg;
        cfg.enabled = enabled;
        cfg.temperature = temperature;
        cfg.startMinutes = startMinutes;
        cfg.endMinutes = endMinutes;
        if (!compositor_.setNightLight(cfg)) {
            return false;  // config 写盘失败 → 错误返回
        }
        *reply = dbus_message_new_method_return(message);
        return true;
    }
    if (std::strcmp(method, "InputKey") == 0) {
        // E8 屏幕键盘：虚拟键盘注入。InputKey(u keysym, b pressed)。
        if (dbus_message_iter_get_arg_type(args) != DBUS_TYPE_UINT32) {
            return false;
        }
        dbus_uint32_t keysym = 0;
        dbus_message_iter_get_basic(args, &keysym);
        dbus_message_iter_next(args);
        if (dbus_message_iter_get_arg_type(args) != DBUS_TYPE_BOOLEAN) {
            return false;
        }
        dbus_bool_t pressed = FALSE;
        dbus_message_iter_get_basic(args, &pressed);
        if (compositor_.seat() != nullptr) {
            compositor_.seat()->injectKey(keysym, pressed != FALSE);
        }
        *reply = dbus_message_new_method_return(message);
        return true;
    }
    return false;  // 未知方法
}

int CompositorDbus::dbusFdCallback(int /*fd*/, uint32_t /*mask*/, void* data) {
    static_cast<CompositorDbus*>(data)->onReadable();
    return 0;
}

std::vector<CompositorDbus::OutputInfo> CompositorDbus::collectOutputs() const {
    std::vector<OutputInfo> infos;
    for (const auto& out : compositor_.outputs()) {
        wlr_output* o = out->wlr();
        if (o == nullptr) {
            continue;
        }
        OutputInfo info;
        info.name = o->name != nullptr ? o->name : "";
        info.scalePercent = static_cast<int>(o->scale * 100.0f + 0.5f);
        // 统一返回有效分辨率（物理尺寸/scale，审查 M2：custom mode 与
        // 内置 mode 两分支语义一致，避免 DRM+scale≠100 时 UI 错位）。
        wlr_output_effective_resolution(o, &info.width, &info.height);
        // 布局位置（0.19 签名：void get_box(layout, output, dest)；
        // 输出不在布局时 box 为空）。
        wlr_box box{};
        wlr_output_layout_get_box(compositor_.outputLayout(), o, &box);
        if (box.width != 0 || box.height != 0) {
            info.x = box.x;
            info.y = box.y;
        }
        infos.push_back(std::move(info));
    }
    return infos;
}

}  // namespace w10de

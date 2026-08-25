#include "systemapps/settings/audioinfo.h"

#include <QTimer>
#include <QtDebug>

#include <pulse/pulseaudio.h>

namespace w10de::settings {

// ---- libpulse 异步状态（后台 mainloop 由 QTimer 驱动，单线程内迭代）----
struct AudioInfoImpl {
    pa_mainloop* mainloop = nullptr;
    pa_context* context = nullptr;
    bool connecting = false;
    bool reconnecting = false;  // 重建中：抑制 TERMINATED 回调（审查 M2）
    AudioInfo* owner = nullptr;  // 状态回调上报信号用
    QList<SinkInfo> sinks;
    bool sinkListPending = false;  // get_sink_info_list 进行中
    bool sinkListDone = false;     // eol 到达（列表完整）
    int sinkListSeq = 0;           // 查询序列号（审查 M4：旧查询数据不混入）
    int sinkListPendingSeq = -1;   // 当前发起查询的序列号
    QTimer* connectTimer = nullptr;  // 连接超时（审查 M1：挂起时允许重建）
    // 待执行的设置命令（连接就绪后按序执行）。
    struct PendingCmd {
        enum Type { Volume, Mute } type = Volume;
        QString name;
        int value = 0;
    };
    QList<PendingCmd> pending;
};

namespace {

// pa_volume（0..PA_VOLUME_NORM=0x10000）→ 0-100 百分比。
int volumeToPercent(pa_volume_t v) {
    return AudioInfo::paVolumeToPercent(static_cast<unsigned int>(v));
}

void onSinkInfo(pa_context* /*c*/, const pa_sink_info* info, int eol, void* user) {
    auto* self = static_cast<AudioInfoImpl*>(user);
    if (eol > 0) {
        // 列表结束；仅当这是最新发起的查询（审查 M4：旧查询的 eol 不
        // 触发完成，避免快速刷新时半截/混合列表）。
        if (self->sinkListPendingSeq == self->sinkListSeq) {
            self->sinkListDone = true;
        }
        return;
    }
    if (info == nullptr || self->sinkListPendingSeq != self->sinkListSeq) {
        return;  // 旧查询数据忽略
    }
    SinkInfo sink;
    sink.name = QString::fromUtf8(info->name);
    sink.description = QString::fromUtf8(info->description);
    sink.volumePercent = info->volume.channels > 0
        ? volumeToPercent(pa_cvolume_avg(&info->volume)) : 100;
    sink.muted = info->mute != 0;
    self->sinks.append(sink);
}

void onContextState(pa_context* c, void* user) {
    auto* self = static_cast<AudioInfoImpl*>(user);
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY: {
        self->connecting = false;
        if (self->connectTimer != nullptr) {
            self->connectTimer->stop();
        }
        // 连接就绪：执行挂起命令 + 初始查询。
        for (const auto& cmd : self->pending) {
            if (cmd.type == AudioInfoImpl::PendingCmd::Volume) {
                pa_cvolume cv;
                pa_cvolume_set(&cv, 1,
                    cmd.value <= 0 ? PA_VOLUME_MUTED
                                   : pa_sw_volume_from_dB(
                                         cmd.value * 20.0 / 100.0 - 20.0));
                pa_operation* op = pa_context_set_sink_volume_by_name(
                    c, cmd.name.toUtf8().constData(), &cv, nullptr, nullptr);
                if (op != nullptr) pa_operation_unref(op);
            } else {
                pa_operation* op = pa_context_set_sink_mute_by_name(
                    c, cmd.name.toUtf8().constData(), cmd.value, nullptr, nullptr);
                if (op != nullptr) pa_operation_unref(op);
            }
        }
        self->pending.clear();
        self->sinks.clear();
        self->sinkListSeq++;
        self->sinkListPendingSeq = self->sinkListSeq;
        self->sinkListPending = true;
        self->sinkListDone = false;
        pa_operation* op = pa_context_get_sink_info_list(c, onSinkInfo, self);
        if (op != nullptr) pa_operation_unref(op);
        break;
    }
    case PA_CONTEXT_FAILED: {
        self->connecting = false;
        if (self->connectTimer != nullptr) {
            self->connectTimer->stop();
        }
        // 异步连接失败（无 Pulse 服务）：上报不可用。
        if (self->owner != nullptr) {
            emit self->owner->connectionFailed(
                QStringLiteral("PulseAudio 服务不可用"));
        }
        break;
    }
    case PA_CONTEXT_TERMINATED: {
        self->connecting = false;
        if (self->connectTimer != nullptr) {
            self->connectTimer->stop();
        }
        // 正常断开（shutdown/重建）不报错（审查 M2）。
        if (self->owner != nullptr && !self->reconnecting) {
            emit self->owner->connectionFailed(
                QStringLiteral("PulseAudio 服务不可用"));
        }
        break;
    }
    default:
        break;
    }
}

}  // namespace

int AudioInfo::paVolumeToPercent(unsigned int paVolume) {
    if (paVolume >= PA_VOLUME_NORM) {
        return 100;
    }
    return static_cast<int>(paVolume * 100 / PA_VOLUME_NORM);
}

AudioInfo::AudioInfo(QObject* parent) : QObject(parent) {
    impl_ = new AudioInfoImpl;
    impl_->owner = this;
    impl_->mainloop = pa_mainloop_new();
    if (impl_->mainloop == nullptr) {
        qWarning("AudioInfo: pa_mainloop_new failed");
        delete impl_;
        impl_ = nullptr;
        return;
    }
    impl_->context = pa_context_new(pa_mainloop_get_api(impl_->mainloop), "w10settings");
    if (impl_->context == nullptr) {
        qWarning("AudioInfo: pa_context_new failed");
        pa_mainloop_free(impl_->mainloop);
        delete impl_;
        impl_ = nullptr;
        return;
    }
    pa_context_set_state_callback(impl_->context, onContextState, impl_);
}

AudioInfo::~AudioInfo() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

void AudioInfo::ensureContext() {
    if (impl_ == nullptr || impl_->context == nullptr) {
        return;
    }
    const pa_context_state_t st = pa_context_get_state(impl_->context);
    if (st == PA_CONTEXT_READY) {
        return;  // 审查 S1：READY 状态不得再次 connect（BADSTATE 断言）
    }
    if (impl_->connecting) {
        // 已连接中（含挂起）：等 connectTimer 超时（审查 M1）或状态回调。
        return;
    }
    if (st == PA_CONTEXT_TERMINATED || st == PA_CONTEXT_FAILED) {
        // 重建：抑制 TERMINATED 回调（审查 M2），检查 pa_context_new 失败
        //（审查 L6）。
        impl_->reconnecting = true;
        pa_context_disconnect(impl_->context);
        pa_context_unref(impl_->context);
        impl_->context = pa_context_new(pa_mainloop_get_api(impl_->mainloop),
                                        "w10settings");
        if (impl_->context == nullptr) {
            impl_->reconnecting = false;
            impl_->connecting = false;
            emit connectionFailed(QStringLiteral("PulseAudio 初始化失败"));
            return;
        }
        pa_context_set_state_callback(impl_->context, onContextState, impl_);
        impl_->reconnecting = false;
    }
    impl_->connecting = true;
    startConnectTimer();
    if (pa_context_connect(impl_->context, nullptr,
                           PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        impl_->connecting = false;
        available_ = false;
        emit connectionFailed(QStringLiteral("PulseAudio 服务不可用（无 %1）")
            .arg(QStringLiteral("/run/user/0/pulse/native")));
    }
}

void AudioInfo::startConnectTimer() {
    if (impl_->connectTimer == nullptr) {
        impl_->connectTimer = new QTimer(this);
        impl_->connectTimer->setSingleShot(true);
        impl_->connectTimer->setInterval(2500);  // 连接超时（审查 M1）
        connect(impl_->connectTimer, &QTimer::timeout, this, [this] {
            // 挂起（无服务的残留 socket）：断开并允许重建（状态回调
            // FAILED/TERMINATED 会触发 connectionFailed——UI 文案一致）。
            if (impl_ != nullptr && impl_->context != nullptr) {
                impl_->reconnecting = false;
                pa_context_disconnect(impl_->context);
            }
        });
    }
    impl_->connectTimer->start();
}

void AudioInfo::runMainloopOnce() {
    if (impl_ == nullptr || impl_->mainloop == nullptr) {
        return;
    }
    // 迭代一次（非阻塞）：pa_mainloop_prepare/poll/dispatch。
    // 上限 8 次（审查 L3：防某 fd 持续就绪阻塞 Qt 主线程）。
    int ret = 1;
    int budget = 8;
    while (ret > 0 && budget-- > 0) {
        ret = pa_mainloop_iterate(impl_->mainloop, 0, nullptr);
    }
    // 查询完成（最新查询的 eol 到达）：交还 sinks。
    if (impl_->sinkListPending &&
            impl_->sinkListPendingSeq == impl_->sinkListSeq &&
            impl_->sinkListDone) {
        impl_->sinkListPending = false;
        impl_->sinkListDone = false;
        available_ = true;
        const QList<SinkInfo> ready = impl_->sinks;
        impl_->sinks.clear();
        emit sinksReady(ready);
    }
}

void AudioInfo::refreshSinks() {
    if (impl_ == nullptr) {
        emit connectionFailed(QStringLiteral("音频后端初始化失败"));
        return;
    }
    impl_->sinks.clear();
    const pa_context_state_t st = pa_context_get_state(impl_->context);
    if (st == PA_CONTEXT_READY) {
        // 审查 S2：READY 时直接重新发起查询（否则"刷新"是 no-op）。
        impl_->sinkListSeq++;
        impl_->sinkListPendingSeq = impl_->sinkListSeq;
        impl_->sinkListPending = true;
        impl_->sinkListDone = false;
        pa_operation* op = pa_context_get_sink_info_list(
            impl_->context, onSinkInfo, impl_);
        if (op != nullptr) pa_operation_unref(op);
    } else {
        ensureContext();
    }
    // mainloop 驱动：每 20ms 迭代（连接 + 查询异步完成）。
    auto* timer = new QTimer(this);
    timer->setInterval(20);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        runMainloopOnce();
        // 查询完成或服务不可用时停止。
        const bool finished = !impl_->sinkListPending &&
            !impl_->connecting;
        const bool failed = impl_->context != nullptr &&
            pa_context_get_state(impl_->context) == PA_CONTEXT_FAILED;
        if (finished || failed) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

void AudioInfo::setVolume(const QString& name, int percent) {
    if (impl_ == nullptr) {
        return;
    }
    // 审查 L8：钳制 0-100。
    percent = qBound(0, percent, 100);
    if (pa_context_get_state(impl_->context) == PA_CONTEXT_READY) {
        pa_cvolume cv;
        // 审查 M3：0% → MUTED（非 -20dB 近似——10% 线性响度仍有声）。
        pa_cvolume_set(&cv, 1, percent <= 0 ? PA_VOLUME_MUTED
                                            : pa_sw_volume_from_dB(
                                                  percent * 20.0 / 100.0 - 20.0));
        pa_operation* op = pa_context_set_sink_volume_by_name(
            impl_->context, name.toUtf8().constData(), &cv, nullptr, nullptr);
        if (op != nullptr) pa_operation_unref(op);
    } else {
        AudioInfoImpl::PendingCmd cmd;
        cmd.type = AudioInfoImpl::PendingCmd::Volume;
        cmd.name = name;
        cmd.value = percent;
        impl_->pending.append(cmd);
        ensureContext();
    }
}

void AudioInfo::setMuted(const QString& name, bool muted) {
    if (impl_ == nullptr) {
        return;
    }
    if (pa_context_get_state(impl_->context) == PA_CONTEXT_READY) {
        pa_operation* op = pa_context_set_sink_mute_by_name(
            impl_->context, name.toUtf8().constData(),
            muted ? 1 : 0, nullptr, nullptr);
        if (op != nullptr) pa_operation_unref(op);
    } else {
        AudioInfoImpl::PendingCmd cmd;
        cmd.type = AudioInfoImpl::PendingCmd::Mute;
        cmd.name = name;
        cmd.value = muted ? 1 : 0;
        impl_->pending.append(cmd);
        ensureContext();
    }
}

void AudioInfo::shutdown() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->connectTimer != nullptr) {
        impl_->connectTimer->stop();
    }
    if (impl_->context != nullptr) {
        impl_->reconnecting = true;  // 正常断开不报错（审查 M2/L5）
        pa_context_disconnect(impl_->context);
        pa_context_unref(impl_->context);
        impl_->context = nullptr;
        impl_->reconnecting = false;
    }
    if (impl_->mainloop != nullptr) {
        pa_mainloop_free(impl_->mainloop);
        impl_->mainloop = nullptr;
    }
}

}  // namespace w10de::settings

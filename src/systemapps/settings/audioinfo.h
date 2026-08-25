// AudioInfo —— 音频设备/音量控制（第二批，PipeWire 音量/设备）。
//
// 用 PulseAudio 客户端库（libpulse，pa_context API）：真机 pipewire-pulse
// 提供 Pulse 兼容服务（KDE 同路径）；无服务时连接失败，UI 显示不可用。
// 异步模型：connect + 查询/设置在后台 mainloop 线程执行，结果经 Qt 信号
// 回传主线程。
#pragma once

#include <QObject>
#include <QStringList>

namespace w10de::settings {

struct SinkInfo {
    QString name;
    QString description;
    int volumePercent = 100;  // 0-100（PA_VOLUME_NORM=0x10000 归一化）
    bool muted = false;
};

// 应用音频流（sink-input，KDE-GAP 中优先 #4：每应用音量）。
struct AppStreamInfo {
    uint32_t index = 0;        // pulse sink-input index（设置音量用）
    QString name;              // 流名（media.name）
    QString application;       // application.name（进程名，可能为空）
    int volumePercent = 100;   // 0-100
    bool muted = false;
};

struct AudioInfoImpl;  // 定义在 audioinfo.cpp（PIMPL：libpulse 状态）

class AudioInfo : public QObject {
    Q_OBJECT
public:
    explicit AudioInfo(QObject* parent = nullptr);
    ~AudioInfo() override;

    bool available() const { return available_; }

    // pa_volume（0..PA_VOLUME_NORM=0x10000）→ 0-100 百分比（selftest 用）。
    static int paVolumeToPercent(unsigned int paVolume);

    // 开始异步查询 sink 列表；完成发 sinksReady（失败发 connectionFailed）。
    void refreshSinks();
    // 异步设置音量（0-100）/静音。
    void setVolume(const QString& name, int percent);
    void setMuted(const QString& name, bool muted);

    // ---- 每应用音量（KDE-GAP 中优先 #4）----
    // 异步查询应用音频流（sink-input）列表；完成发 appStreamsReady。
    void refreshAppStreams();
    // 按 sink-input index 设置应用音量（0-100）/静音。
    void setAppVolume(uint32_t index, int percent);
    void setAppMuted(uint32_t index, bool muted);

signals:
    void sinksReady(const QList<SinkInfo>& sinks);
    void appStreamsReady(const QList<AppStreamInfo>& streams);
    void connectionFailed(const QString& reason);

private:
    void ensureContext();
    void startConnectTimer();
    void runMainloopOnce();
    void shutdown();

    AudioInfoImpl* impl_ = nullptr;
    bool available_ = false;
};

}  // namespace w10de::settings

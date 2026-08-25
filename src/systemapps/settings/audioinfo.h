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

signals:
    void sinksReady(const QList<SinkInfo>& sinks);
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

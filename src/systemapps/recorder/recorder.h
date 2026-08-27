// RecorderEngine —— 录音/播放引擎（libpulse，可选拓展 E6 录音机）。
//
// 用 PulseAudio 客户端库（pa_context/pa_stream，同 settings/audioinfo 的
// 连接模式）：真机 pipewire-pulse 提供 Pulse 兼容服务；无服务时连接失败，
// UI 显示不可用。
// 录音：PA_STREAM_RECORD 从默认 source（或指定 device）捕获 S16LE 单声道
// 44100Hz 原始 PCM，停止时组装 WAV 落盘（buildWav）。
// 播放：解析 WAV（parseWav）定位 PCM 后 PA_STREAM_PLAYBACK 输出。
// 异步模型：pa_mainloop 由常驻 QTimer 周期 pump 驱动（单线程，同 audioinfo）。
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>

namespace w10de::recorder {

// ---- WAV 工具（静态可测，selftest 覆盖）----

// S16LE PCM → 完整 WAV 字节（44 字节 RIFF 头 + PCM）。
QByteArray buildWav(const QByteArray& pcmS16le, int sampleRate, int channels);

struct WavInfo {
    bool ok = false;         // 头合法（RIFF/WAVE/fmt/data 标记 + 大小一致）
    int sampleRate = 0;      // 采样率（Hz）
    int channels = 0;
    int pcmOffset = 0;       // PCM 数据在原始字节中的偏移
    int pcmBytes = 0;        // PCM 数据长度
};

// 解析完整 WAV 字节（校验头，定位 PCM 区）；非法输入 ok=false。
WavInfo parseWav(const QByteArray& wav);

// 毫秒 → "mm:ss"（≥1h 为 "h:mm:ss"）。
QString formatDuration(qint64 ms);

// 默认录音目录（Win10 一致：~/Documents/Sound recordings；环境变量
// W10DE_RECORDINGS_DIR 覆盖——测试隔离用）。
QString recordingsDir();

// 新录音文件名："录音 yyyy-MM-dd HH-mm-ss.wav"（毫秒防同秒撞名）。
QString newRecordingName(const QDateTime& when);

// 历史录音项（列表展示）。
struct RecordingItem {
    QString path;
    QString name;
    qint64 durationMs = 0;
    qint64 sizeBytes = 0;
    QDateTime modified;
};

// 扫描录音目录下 *.wav（parseWav 得时长）。
QList<RecordingItem> scanRecordings(const QString& dir);

struct RecorderEngineImpl;  // PIMPL：libpulse 状态（recorder.cpp）

class RecorderEngine : public QObject {
    Q_OBJECT
public:
    explicit RecorderEngine(QObject* parent = nullptr);
    ~RecorderEngine() override;

    bool isAvailable() const;    // pa 连接就绪
    bool isRecording() const;
    bool isPlaying() const;

    // 异步连接；就绪发 availableChanged(true)，失败发 error。
    void ensureContext();

    // 录音到 path（WAV 落盘）；device 为 pa source 名，空=默认 source。
    void startRecording(const QString& path, const QString& device = QString());
    // 停止并落盘 → recordingSaved。
    void stopRecording();

    // 播放完整 WAV 字节；结束/停止发 playbackFinished。
    void playWav(const QByteArray& wav);
    void stopPlayback();

signals:
    void availableChanged(bool available);
    void recordingStarted();
    void recordingSaved(const QString& path, qint64 durationMs, int pcmBytes);
    void levelChanged(int percent);   // 录音 RMS 电平 0-100
    void playbackStarted();
    void playbackFinished();
    void error(const QString& message);

private:
    void pump();
    void shutdown();
    RecorderEngineImpl* impl_ = nullptr;
};

}  // namespace w10de::recorder

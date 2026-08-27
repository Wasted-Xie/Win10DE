// RecorderEngine 实现（libpulse，可选拓展 E6 录音机）。
//
// pa_mainloop 单线程模型（同 settings/audioinfo）：常驻 QTimer 周期
// pa_mainloop_iterate(0) 非阻塞迭代，pa 回调内累积数据/状态，Qt 侧读取。
// 录音采样固定 S16LE 单声道 44100Hz（WSL 虚拟设备与真机兼容性最稳）。

#include "systemapps/recorder/recorder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QtDebug>
#include <QtEndian>
#include <cmath>

#include <pulse/pulseaudio.h>

namespace w10de::recorder {

namespace {

constexpr int kSampleRate = 44100;
constexpr int kChannels = 1;
constexpr int kBytesPerSample = 2;  // S16LE

// RMS 电平（0-100）：峰值约 2*32768 归一后 ×100。
int rmsLevel(const char* data, size_t nbytes) {
    const int n = static_cast<int>(nbytes / kBytesPerSample);
    if (n <= 0) return 0;
    double sum = 0.0;
    const qint16* s = reinterpret_cast<const qint16*>(data);
    for (int i = 0; i < n; ++i) {
        const double v = static_cast<double>(s[i]) / 32768.0;
        sum += v * v;
    }
    const double rms = std::sqrt(sum / n);
    return qBound(0, qRound(rms * 200.0), 100);
}

}  // namespace

// ---- WAV 工具 ----

QByteArray buildWav(const QByteArray& pcmS16le, int sampleRate, int channels) {
    const quint32 dataSize = static_cast<quint32>(pcmS16le.size());
    const quint32 byteRate = static_cast<quint32>(sampleRate * channels * 2);
    const quint16 blockAlign = static_cast<quint16>(channels * 2);
    QByteArray wav;
    wav.reserve(44 + dataSize);
    const auto put32 = [&wav](quint32 v) {
        wav.append(static_cast<char>(v & 0xFF));
        wav.append(static_cast<char>((v >> 8) & 0xFF));
        wav.append(static_cast<char>((v >> 16) & 0xFF));
        wav.append(static_cast<char>((v >> 24) & 0xFF));
    };
    const auto put16 = [&wav](quint16 v) {
        wav.append(static_cast<char>(v & 0xFF));
        wav.append(static_cast<char>((v >> 8) & 0xFF));
    };
    wav.append("RIFF", 4);
    put32(36 + dataSize);
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    put32(16);              // fmt 块长
    put16(1);               // PCM
    put16(static_cast<quint16>(channels));
    put32(static_cast<quint32>(sampleRate));
    put32(byteRate);
    put16(blockAlign);
    put16(16);              // 位深
    wav.append("data", 4);
    put32(dataSize);
    wav.append(pcmS16le);
    return wav;
}

WavInfo parseWav(const QByteArray& wav) {
    WavInfo info;
    if (wav.size() < 44 || wav.mid(0, 4) != "RIFF" || wav.mid(8, 4) != "WAVE") {
        return info;
    }
    // 遍历块，找 fmt + data（允许 data 前的任意块）。
    // 审查 M1：pos/len 用 qint64 累加，防畸形头的大 len 使 int 回绕
    // 造成死循环或越界访问。
    qint64 pos = 12;
    qint64 dataOffset = -1;
    while (pos + 8 <= wav.size()) {
        const QByteArray id = wav.mid(static_cast<int>(pos), 4);
        const quint32 len = static_cast<quint32>(
            (static_cast<unsigned char>(wav.at(static_cast<int>(pos) + 4)))
            | (static_cast<unsigned char>(wav.at(static_cast<int>(pos) + 5)) << 8)
            | (static_cast<unsigned char>(wav.at(static_cast<int>(pos) + 6)) << 16)
            | (static_cast<unsigned char>(wav.at(static_cast<int>(pos) + 7)) << 24));
        const qint64 body = pos + 8;
        if (id == "fmt " && len >= 16 && body + 16 <= wav.size()) {
            const quint16 audioFormat = static_cast<quint16>(
                static_cast<unsigned char>(wav.at(static_cast<int>(body)))
                | (static_cast<unsigned char>(wav.at(static_cast<int>(body) + 1)) << 8));
            if (audioFormat != 1) return info;  // 仅 PCM
            info.channels = static_cast<int>(
                static_cast<unsigned char>(wav.at(static_cast<int>(body) + 2))
                | (static_cast<unsigned char>(wav.at(static_cast<int>(body) + 3)) << 8));
            info.sampleRate = static_cast<int>(
                static_cast<unsigned char>(wav.at(static_cast<int>(body) + 4))
                | (static_cast<unsigned char>(wav.at(static_cast<int>(body) + 5)) << 8)
                | (static_cast<unsigned char>(wav.at(static_cast<int>(body) + 6)) << 16)
                | (static_cast<unsigned char>(wav.at(static_cast<int>(body) + 7)) << 24));
        } else if (id == "data") {
            dataOffset = body;
            info.pcmBytes = static_cast<int>(qMin<qint64>(len,
                static_cast<qint64>(wav.size()) - body));
        }
        pos = body + len + (len & 1);  // 块对齐到偶字节
    }
    if (dataOffset < 0) return info;
    info.pcmOffset = dataOffset;
    info.ok = info.channels > 0 && info.sampleRate > 0;
    return info;
}

QString formatDuration(qint64 ms) {
    const qint64 totalSec = qMax<qint64>(0, ms / 1000);
    const qint64 h = totalSec / 3600;
    const qint64 m = (totalSec % 3600) / 60;
    const qint64 s = totalSec % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3").arg(h)
            .arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

QString recordingsDir() {
    const QByteArray env = qgetenv("W10DE_RECORDINGS_DIR");
    if (!env.isEmpty()) {
        return QString::fromUtf8(env);
    }
    return QDir::homePath() + QStringLiteral("/Documents/Sound recordings");
}

QString newRecordingName(const QDateTime& when) {
    return QStringLiteral("录音 %1.wav").arg(
        when.toString(QStringLiteral("yyyy-MM-dd HH-mm-ss-zzz")));
}

QList<RecordingItem> scanRecordings(const QString& dir) {
    QList<RecordingItem> items;
    QDir d(dir);
    if (!d.exists()) return items;
    const QStringList files = d.entryList(
        {QStringLiteral("*.wav")}, QDir::Files, QDir::Name);
    for (const QString& f : files) {
        const QString path = d.absoluteFilePath(f);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        // 审查 S3：必须读全文件——parseWav 的 pcmBytes 由 data 块长度
        // 决定，只读头部会把时长截断成 ~0ms。
        const QByteArray wav = file.readAll();
        file.close();
        const WavInfo wi = parseWav(wav);
        if (!wi.ok) continue;
        RecordingItem item;
        item.path = path;
        item.name = QFileInfo(path).completeBaseName();
        item.durationMs = static_cast<qint64>(wi.pcmBytes)
            * 1000LL / qMax(1, wi.sampleRate * wi.channels * 2);
        item.sizeBytes = QFileInfo(path).size();
        item.modified = QFileInfo(path).lastModified();
        items.append(item);
    }
    return items;
}

// ---- RecorderEngine ----

struct RecorderEngineImpl {
    pa_mainloop* mainloop = nullptr;
    pa_context* context = nullptr;
    bool connecting = false;
    // 录音流状态。
    pa_stream* recordStream = nullptr;
    QByteArray recordBuffer;
    QString recordPath;
    qint64 recordStartMs = 0;
    double levelEma = 0.0;
    // 播放流状态。
    pa_stream* playStream = nullptr;
    QByteArray playWav;       // 完整 WAV（写后由 drain 完成确认释放）
    int playPcmOffset = 0;
    int playPcmBytes = 0;
    pa_operation* playDrainOp = nullptr;  // 审查 S2：drain 句柄（停止时取消）
    RecorderEngine* owner = nullptr;
    QTimer* pumpTimer = nullptr;
    QTimer* connectTimer = nullptr;
};

namespace {

void onDrain(pa_stream* ds, int success, void* user);  // 前置声明

void onRecordRead(pa_stream* s, size_t /*nbytes*/, void* user) {
    auto* self = static_cast<RecorderEngineImpl*>(user);
    while (pa_stream_readable_size(s) > 0) {
        const void* data = nullptr;
        size_t len = 0;
        if (pa_stream_peek(s, &data, &len) < 0) break;
        if (len == 0) break;
        if (data != nullptr) {
            self->recordBuffer.append(static_cast<const char*>(data),
                                      static_cast<int>(len));
            // RMS 电平（EMA 平滑，防抖动）。
            const int lvl = rmsLevel(static_cast<const char*>(data), len);
            self->levelEma = self->levelEma <= 0.0
                ? lvl : self->levelEma * 0.7 + lvl * 0.3;
            if (self->owner != nullptr) {
                emit self->owner->levelChanged(
                    qBound(0, qRound(self->levelEma), 100));
            }
        }
        if (pa_stream_drop(s) < 0) break;
    }
}

// 播放流状态回调：READY 后写入 PCM 并 drain；FAILED/TERMINATED（服务
// 重启/设备被移除等外部终结）释放流，防 impl 悬挂（审查 S1）。
// 审查 M2：所有清理先完成、指针归零后再 emit，槽内可安全启动新播放。
void onPlayState(pa_stream* s, void* user) {
    auto* self = static_cast<RecorderEngineImpl*>(user);
    switch (pa_stream_get_state(s)) {
    case PA_STREAM_READY: {
        const int pcmBytes = self->playPcmBytes;
        if (pcmBytes <= 0 || self->playPcmOffset < 0) {
            pa_stream_disconnect(s);
            return;
        }
        // 审查 M5：S16 帧 2 字节对齐（畸形 WAV 奇数长度丢弃末字节），
        // 并检查写入结果——失败则直接结束播放（drain 永不完成风险）。
        int aligned = pcmBytes & ~1;
        if (aligned <= 0) {
            pa_stream_disconnect(s);
            return;
        }
        const char* pcm = self->playWav.constData() + self->playPcmOffset;
        if (pa_stream_write(s, pcm, static_cast<size_t>(aligned),
                            nullptr, 0, PA_SEEK_RELATIVE) < 0) {
            self->playPcmBytes = 0;
            pa_stream_disconnect(s);
            return;
        }
        self->playPcmBytes = aligned;
        self->playDrainOp = pa_stream_drain(s, onDrain, user);
        break;
    }
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
        // 外部终结（pipewire-pulse 重启、设备移除等）。
        if (self->playStream == s) {
            if (self->playDrainOp != nullptr) {
                pa_operation_cancel(self->playDrainOp);
                pa_operation_unref(self->playDrainOp);
                self->playDrainOp = nullptr;
            }
            pa_stream_unref(s);
            self->playStream = nullptr;
            self->playWav.clear();
            self->playPcmOffset = 0;
            self->playPcmBytes = 0;
            if (self->owner != nullptr) {
                emit self->owner->playbackFinished();
            }
        }
        break;
    default:
        break;
    }
}

// 录音流状态回调：READY 才上报 recordingStarted（计时从流真正就绪起）；
// FAILED/TERMINATED 释放流防悬挂（审查 S1）并上报错误。
void onRecordStreamState(pa_stream* s, void* user) {
    auto* self = static_cast<RecorderEngineImpl*>(user);
    switch (pa_stream_get_state(s)) {
    case PA_STREAM_READY:
        if (self->owner != nullptr) {
            emit self->owner->recordingStarted();
        }
        break;
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
        if (self->recordStream == s) {
            pa_stream_unref(s);
            self->recordStream = nullptr;
            self->recordPath.clear();
            self->recordBuffer.clear();
            if (self->owner != nullptr) {
                emit self->owner->error(QStringLiteral("录音流中断"));
            }
        }
        break;
    default:
        break;
    }
}

// drain 完成回调（审查 S2）：disconnect 后 unref——此时流可能已被
// onPlayState(FAILED/TERMINATED) 释放，故先检查 playStream 是否仍指向本流；
// 清理顺序：指针归零 → emit，避免槽内新播放撞上旧状态。
void onDrain(pa_stream* ds, int /*success*/, void* user) {
    auto* self = static_cast<RecorderEngineImpl*>(user);
    self->playDrainOp = nullptr;  // 本 op 已完成
    if (self->playStream != ds) {
        return;  // 已被 stopPlayback/外部终结清理
    }
    pa_stream_disconnect(ds);
    pa_stream_unref(ds);
    self->playStream = nullptr;
    self->playWav.clear();
    self->playPcmOffset = 0;
    self->playPcmBytes = 0;
    if (self->owner != nullptr) {
        emit self->owner->playbackFinished();
    }
}

void onContextState(pa_context* c, void* user) {
    auto* self = static_cast<RecorderEngineImpl*>(user);
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY:
        self->connecting = false;
        if (self->connectTimer != nullptr) self->connectTimer->stop();
        if (self->owner != nullptr) emit self->owner->availableChanged(true);
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED: {
        self->connecting = false;
        if (self->connectTimer != nullptr) self->connectTimer->stop();
        // 审查 S1：服务终结后流也随之失效，统一释放防 impl 悬挂
        //（流的 state 回调可能不再触发）。
        if (self->recordStream != nullptr) {
            pa_stream_unref(self->recordStream);
            self->recordStream = nullptr;
            self->recordPath.clear();
            self->recordBuffer.clear();
        }
        if (self->playStream != nullptr) {
            if (self->playDrainOp != nullptr) {
                pa_operation_cancel(self->playDrainOp);
                pa_operation_unref(self->playDrainOp);
                self->playDrainOp = nullptr;
            }
            pa_stream_unref(self->playStream);
            self->playStream = nullptr;
            self->playWav.clear();
            self->playPcmOffset = 0;
            self->playPcmBytes = 0;
        }
        if (pa_context_get_state(c) == PA_CONTEXT_FAILED
                && self->owner != nullptr) {
            emit self->owner->error(QStringLiteral("PulseAudio 服务不可用"));
        }
        break;
    }
    default:
        break;
    }
}

}  // namespace

RecorderEngine::RecorderEngine(QObject* parent) : QObject(parent) {
    impl_ = new RecorderEngineImpl;
    impl_->owner = this;
    impl_->mainloop = pa_mainloop_new();
    if (impl_->mainloop == nullptr) {
        qWarning("RecorderEngine: pa_mainloop_new failed");
        delete impl_;
        impl_ = nullptr;
        return;
    }
    impl_->context = pa_context_new(pa_mainloop_get_api(impl_->mainloop),
                                    "w10recorder");
    if (impl_->context == nullptr) {
        qWarning("RecorderEngine: pa_context_new failed");
        pa_mainloop_free(impl_->mainloop);
        delete impl_;
        impl_ = nullptr;
        return;
    }
    pa_context_set_state_callback(impl_->context, onContextState, impl_);
    // 常驻 pump：20ms 周期迭代 mainloop（录音回调连续、连接/播放异步）。
    impl_->pumpTimer = new QTimer(this);
    impl_->pumpTimer->setInterval(20);
    connect(impl_->pumpTimer, &QTimer::timeout, this,
            [this] { pump(); });
    impl_->pumpTimer->start();
}

RecorderEngine::~RecorderEngine() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

void RecorderEngine::pump() {
    if (impl_ == nullptr || impl_->mainloop == nullptr) return;
    int ret = 1;
    int budget = 8;  // 防某 fd 持续就绪阻塞（同 audioinfo）
    while (ret > 0 && budget-- > 0) {
        ret = pa_mainloop_iterate(impl_->mainloop, 0, nullptr);
    }
}

bool RecorderEngine::isAvailable() const {
    return impl_ != nullptr && impl_->context != nullptr
        && pa_context_get_state(impl_->context) == PA_CONTEXT_READY;
}

bool RecorderEngine::isRecording() const {
    return impl_ != nullptr && impl_->recordStream != nullptr;
}

bool RecorderEngine::isPlaying() const {
    return impl_ != nullptr && impl_->playStream != nullptr;
}

void RecorderEngine::ensureContext() {
    if (impl_ == nullptr || impl_->context == nullptr) return;
    const pa_context_state_t st = pa_context_get_state(impl_->context);
    if (st == PA_CONTEXT_READY) return;  // 已就绪
    if (impl_->connecting) return;       // 连接中
    if (st == PA_CONTEXT_TERMINATED || st == PA_CONTEXT_FAILED) {
        // 重建。
        pa_context_disconnect(impl_->context);
        pa_context_unref(impl_->context);
        impl_->context = pa_context_new(pa_mainloop_get_api(impl_->mainloop),
                                        "w10recorder");
        if (impl_->context == nullptr) {
            impl_->connecting = false;
            emit error(QStringLiteral("PulseAudio 初始化失败"));
            return;
        }
        pa_context_set_state_callback(impl_->context, onContextState, impl_);
    }
    impl_->connecting = true;
    if (impl_->connectTimer == nullptr) {
        impl_->connectTimer = new QTimer(this);
        impl_->connectTimer->setSingleShot(true);
        impl_->connectTimer->setInterval(2500);  // 连接超时
        connect(impl_->connectTimer, &QTimer::timeout, this, [this] {
            if (impl_ != nullptr && impl_->context != nullptr) {
                pa_context_disconnect(impl_->context);
            }
        });
    }
    impl_->connectTimer->start();
    if (pa_context_connect(impl_->context, nullptr,
                           PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        impl_->connecting = false;
        emit error(QStringLiteral("PulseAudio 服务不可用"));
    }
}

void RecorderEngine::startRecording(const QString& path, const QString& device) {
    if (impl_ == nullptr || !isAvailable()) {
        ensureContext();
        emit error(QStringLiteral("音频服务未就绪"));
        return;
    }
    if (isRecording()) return;
    if (isPlaying()) stopPlayback();

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = kSampleRate;
    ss.channels = kChannels;
    pa_stream* stream = pa_stream_new(impl_->context, "w10recorder-record",
                                      &ss, nullptr);
    if (stream == nullptr) {
        emit error(QStringLiteral("创建录音流失败"));
        return;
    }
    pa_stream_set_read_callback(stream, onRecordRead, impl_);
    // 审查 S1：外部终结（服务重启/设备移除）时释放流防悬挂。
    pa_stream_set_state_callback(stream, onRecordStreamState, impl_);
    pa_buffer_attr attr;
    // 显式小 fragsize（≈116ms）+ ADJUST_LATENCY：确保回调及时拿到数据
    //（默认 fragsize 可达数秒，短录音可能一直读不到）。
    attr.fragsize = 10240;
    attr.maxlength = static_cast<uint32_t>(-1);
    attr.minreq = static_cast<uint32_t>(-1);
    attr.prebuf = static_cast<uint32_t>(-1);
    attr.tlength = static_cast<uint32_t>(-1);
    // 审查 S4：device 字节串须存活到 connect 调用返回（作用域变量）。
    const QByteArray devBytes = device.toUtf8();
    const char* dev = device.isEmpty() ? nullptr : devBytes.constData();
    if (pa_stream_connect_record(stream, dev, &attr,
                                 PA_STREAM_ADJUST_LATENCY) < 0) {
        pa_stream_unref(stream);
        emit error(QStringLiteral("连接录音设备失败"));
        return;
    }
    impl_->recordStream = stream;
    impl_->recordPath = path;
    impl_->recordBuffer.clear();
    impl_->levelEma = 0.0;
    impl_->recordStartMs = QDateTime::currentMSecsSinceEpoch();
    // recordingStarted 由 onRecordStreamState(READY) 发出（流真正就绪）。
}

void RecorderEngine::stopRecording() {
    if (impl_ == nullptr || impl_->recordStream == nullptr) return;
    // 同步断开（数据回调停止），随后落盘。
    pa_stream_disconnect(impl_->recordStream);
    pa_stream_unref(impl_->recordStream);
    impl_->recordStream = nullptr;
    const qint64 durationMs =
        QDateTime::currentMSecsSinceEpoch() - impl_->recordStartMs;
    const int pcmBytes = impl_->recordBuffer.size();
    const QByteArray wav = buildWav(impl_->recordBuffer, kSampleRate,
                                    kChannels);
    // 落盘（目录自动创建）。
    QFileInfo fi(impl_->recordPath);
    QDir().mkpath(fi.absolutePath());
    QFile f(impl_->recordPath);
    bool ok = f.open(QIODevice::WriteOnly) && f.write(wav) == wav.size();
    f.close();
    if (!ok) {
        emit error(QStringLiteral("保存录音失败：%1").arg(impl_->recordPath));
        impl_->recordBuffer.clear();
        return;
    }
    const QString saved = impl_->recordPath;
    impl_->recordPath.clear();
    impl_->recordBuffer.clear();
    emit recordingSaved(saved, durationMs, pcmBytes);
}

void RecorderEngine::playWav(const QByteArray& wav) {
    if (impl_ == nullptr || !isAvailable()) {
        ensureContext();
        emit error(QStringLiteral("音频服务未就绪"));
        return;
    }
    if (isPlaying()) stopPlayback();
    const WavInfo wi = parseWav(wav);
    if (!wi.ok || wi.pcmBytes <= 0) {
        emit error(QStringLiteral("无效的 WAV 文件"));
        return;
    }
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = static_cast<uint32_t>(wi.sampleRate);
    ss.channels = static_cast<uint8_t>(wi.channels);
    pa_stream* stream = pa_stream_new(impl_->context, "w10recorder-play",
                                      &ss, nullptr);
    if (stream == nullptr) {
        emit error(QStringLiteral("创建播放流失败"));
        return;
    }
    pa_stream_set_state_callback(stream, onPlayState, impl_);
    impl_->playStream = stream;
    impl_->playWav = wav;  // 存活到 drain 完成
    impl_->playPcmOffset = wi.pcmOffset;
    impl_->playPcmBytes = wi.pcmBytes;
    if (pa_stream_connect_playback(stream, nullptr, nullptr,
                                   PA_STREAM_NOFLAGS, nullptr, nullptr) < 0) {
        pa_stream_unref(stream);
        impl_->playStream = nullptr;
        impl_->playWav.clear();
        emit error(QStringLiteral("连接播放设备失败"));
        return;
    }
    emit playbackStarted();
}

void RecorderEngine::stopPlayback() {
    if (impl_ == nullptr || impl_->playStream == nullptr) return;
    // 审查 S2：先取消挂起的 drain（否则其回调可能在 stop 后触发，
    // 对已 unref 的流再操作导致崩溃）。
    if (impl_->playDrainOp != nullptr) {
        pa_operation_cancel(impl_->playDrainOp);
        pa_operation_unref(impl_->playDrainOp);
        impl_->playDrainOp = nullptr;
    }
    pa_stream_disconnect(impl_->playStream);
    pa_stream_unref(impl_->playStream);
    impl_->playStream = nullptr;
    impl_->playWav.clear();
    impl_->playPcmOffset = 0;
    impl_->playPcmBytes = 0;
    emit playbackFinished();
}

void RecorderEngine::shutdown() {
    if (impl_ == nullptr) return;
    if (impl_->connectTimer != nullptr) impl_->connectTimer->stop();
    if (impl_->recordStream != nullptr) {
        pa_stream_disconnect(impl_->recordStream);
        pa_stream_unref(impl_->recordStream);
        impl_->recordStream = nullptr;
        impl_->recordPath.clear();
        impl_->recordBuffer.clear();
    }
    if (impl_->playStream != nullptr) {
        // 同 stopPlayback：取消 drain 防停止后回调（审查 S2）。
        if (impl_->playDrainOp != nullptr) {
            pa_operation_cancel(impl_->playDrainOp);
            pa_operation_unref(impl_->playDrainOp);
            impl_->playDrainOp = nullptr;
        }
        pa_stream_disconnect(impl_->playStream);
        pa_stream_unref(impl_->playStream);
        impl_->playStream = nullptr;
        impl_->playWav.clear();
        impl_->playPcmOffset = 0;
        impl_->playPcmBytes = 0;
    }
    if (impl_->context != nullptr) {
        pa_context_disconnect(impl_->context);
        pa_context_unref(impl_->context);
        impl_->context = nullptr;
    }
    if (impl_->mainloop != nullptr) {
        pa_mainloop_free(impl_->mainloop);
        impl_->mainloop = nullptr;
    }
}

}  // namespace w10de::recorder

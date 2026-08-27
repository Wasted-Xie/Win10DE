// w10recorder —— 录音机入口（可选拓展 E6）。
//
// 用法：w10recorder [--selftest] [--render]。
// --selftest：WAV 工具函数自测（offscreen，无音频服务依赖）。
// --render：offscreen 渲染窗口到 PNG（验证布局用，无音频服务依赖）。

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <cstdio>  // setvbuf

#include "systemapps/appipc.h"
#include "systemapps/recorder/recorder.h"
#include "systemapps/recorder/recorderwindow.h"
#include "ipc/config.h"
#include "ipc/theme.h"
#include "theme/colors.h"

namespace {

int runSelfTest() {
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };
    using w10de::recorder::buildWav;
    using w10de::recorder::formatDuration;
    using w10de::recorder::parseWav;
    // 1) WAV 头字节级断言（44 字节 RIFF/WAVE/fmt/data）。
    {
        const QByteArray pcm(882, '\x00');  // 10ms @44100 mono
        const QByteArray wav = buildWav(pcm, 44100, 1);
        if (wav.size() != 44 + 882) return fail(QStringLiteral("WAV 总长错误"));
        if (wav.mid(0, 4) != "RIFF" || wav.mid(8, 4) != "WAVE"
                || wav.mid(12, 4) != "fmt " || wav.mid(36, 4) != "data") {
            return fail(QStringLiteral("WAV 块标记错误"));
        }
        auto le32 = [&wav](int off) {
            return static_cast<quint32>(
                static_cast<unsigned char>(wav.at(off))
                | (static_cast<unsigned char>(wav.at(off + 1)) << 8)
                | (static_cast<unsigned char>(wav.at(off + 2)) << 16)
                | (static_cast<unsigned char>(wav.at(off + 3)) << 24));
        };
        auto le16 = [&wav](int off) {
            return static_cast<quint16>(
                static_cast<unsigned char>(wav.at(off))
                | (static_cast<unsigned char>(wav.at(off + 1)) << 8));
        };
        if (le32(4) != 36 + 882) return fail(QStringLiteral("RIFF 大小错误"));
        if (le16(20) != 1 || le16(22) != 1 || le16(34) != 16) {
            return fail(QStringLiteral("fmt 字段错误"));
        }
        if (le32(24) != 44100 || le32(28) != 88200 || le16(32) != 2) {
            return fail(QStringLiteral("采样参数错误"));
        }
        if (le32(40) != 882) return fail(QStringLiteral("data 大小错误"));
        out << "OK wav-header\n";
    }
    // 2) buildWav/parseWav 往返（含立体声 22050）。
    {
        const QByteArray pcm(4096, '\x01');
        const QByteArray wav = buildWav(pcm, 22050, 2);
        const auto wi = parseWav(wav);
        if (!wi.ok) return fail(QStringLiteral("往返解析失败"));
        if (wi.sampleRate != 22050 || wi.channels != 2
                || wi.pcmOffset != 44 || wi.pcmBytes != 4096) {
            return fail(QStringLiteral("往返字段不一致"));
        }
        if (wav.mid(wi.pcmOffset, 8) != pcm.left(8)) {
            return fail(QStringLiteral("PCM 数据偏移错误"));
        }
        out << "OK wav-roundtrip\n";
    }
    // 3) parseWav 拒绝非法输入。
    {
        if (parseWav(QByteArrayLiteral("NOTWAV")).ok) {
            return fail(QStringLiteral("垃圾头未拒绝"));
        }
        if (parseWav(QByteArray(20, '\x00')).ok) {
            return fail(QStringLiteral("过短头未拒绝"));
        }
        // 无 data 块。
        QByteArray noData = buildWav(QByteArray(4, '\x00'), 8000, 1);
        noData.truncate(40);
        if (parseWav(noData).ok) return fail(QStringLiteral("缺 data 未拒绝"));
        out << "OK wav-reject\n";
    }
    // 4) formatDuration 边界。
    {
        if (formatDuration(0) != "00:00"
                || formatDuration(59999) != "00:59"
                || formatDuration(60000) != "01:00"
                || formatDuration(3599000) != "59:59"
                || formatDuration(3600000) != "1:00:00"
                || formatDuration(3661000) != "1:01:01") {
            return fail(QStringLiteral("时长格式化错误"));
        }
        out << "OK duration\n";
    }
    // 5) 录音文件名：合法扩展名 + 毫秒防同秒撞名。
    {
        const QString n1 = w10de::recorder::newRecordingName(
            QDateTime::fromString(QStringLiteral("2026-08-27 10:20:30.000"),
                                  QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        const QString n2 = w10de::recorder::newRecordingName(
            QDateTime::fromString(QStringLiteral("2026-08-27 10:20:30.001"),
                                  QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        if (!n1.endsWith(QStringLiteral(".wav")) || n1 == n2) {
            return fail(QStringLiteral("录音文件名错误"));
        }
        out << "OK filename\n";
    }
    out << "SELFTEST PASS\n";
    return 0;
}

int runRender(const QString& pngPath) {
    using w10de::recorder::RecorderWindow;
    RecorderWindow window;
    window.show();
    // 等引擎连接尝试（无服务时也不阻塞：窗口已渲染）。
    QApplication::processEvents();
    const QPixmap pm = window.grab();
    const bool ok = pm.save(pngPath);
    if (!ok) {
        std::fprintf(stderr, "render save failed\n");
        return 1;
    }
    std::printf("RENDER OK %s %dx%d state=%s\n", qPrintable(pngPath),
                pm.width(), pm.height(),
                qPrintable(window.stateText()));
    return 0;
}

// 端到端录音验证：连接 PulseAudio → 录 seconds 秒到 outPath（device 为 pa
// source 名，空=默认）→ 停止落盘 → 校验 WAV 头/长度 → 退出。
// 无音频服务时 error 信号触发退出（非零）。
int runRecordTest(const QString& outPath, int seconds, const QString& device) {
    using w10de::recorder::RecorderEngine;
    using w10de::recorder::parseWav;
    RecorderEngine engine;
    bool started = false;
    QObject::connect(&engine, &RecorderEngine::availableChanged,
                     &engine, [&](bool ok) {
        if (!ok) return;
        engine.startRecording(outPath, device);
    });
    QObject::connect(&engine, &RecorderEngine::recordingStarted,
                     &engine, [&] {
        started = true;
        std::printf("recording started (%d s)\n", seconds);
        // 录音时长后停止。
        QTimer::singleShot(seconds * 1000, &engine, [&] {
            engine.stopRecording();
        });
    });
    QObject::connect(&engine, &RecorderEngine::recordingSaved,
                     &engine, [&](const QString& path, qint64 ms, int pcm) {
        // 校验落盘文件。
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "RECORD TEST FAIL: cannot open %s\n",
                         qPrintable(path));
            QCoreApplication::exit(1);
            return;
        }
        const QByteArray wav = f.readAll();
        f.close();
        const auto wi = parseWav(wav);
        if (!wi.ok || wi.pcmBytes != pcm) {
            std::fprintf(stderr, "RECORD TEST FAIL: wav invalid\n");
            QCoreApplication::exit(1);
            return;
        }
        std::printf("RECORD TEST OK %s duration=%lldms pcm=%d\n",
                    qPrintable(path), ms, pcm);
        QCoreApplication::exit(0);
    });
    QObject::connect(&engine, &RecorderEngine::error, &engine, [&](const QString& m) {
        std::fprintf(stderr, "RECORD TEST FAIL: %s\n", qPrintable(m));
        QCoreApplication::exit(1);
    });
    // 总超时保护。
    QTimer::singleShot(15000, &engine, [&] {
        if (!started) {
            std::fprintf(stderr, "RECORD TEST FAIL: timeout (no service)\n");
            QCoreApplication::exit(1);
        }
    });
    engine.ensureContext();
    return QCoreApplication::exec() == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QString renderPath;
    QString recordOut;
    int recordSeconds = 0;
    QString recordDevice;
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            selftest = true;
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--render") == 0 && i + 1 < argc) {
            renderPath = QString::fromLocal8Bit(argv[++i]);
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--record-test") == 0
                   && i + 2 < argc) {
            recordOut = QString::fromLocal8Bit(argv[++i]);
            recordSeconds = QString::fromLocal8Bit(argv[++i]).toInt();
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                recordDevice = QString::fromLocal8Bit(argv[++i]);
            }
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--help") == 0) {
            std::printf("Usage: w10recorder [--selftest] [--render <png>]"
                        " [--record-test <out.wav> <seconds> [device]]\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10recorder: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (selftest) {
        QApplication app(argc, argv);
        return runSelfTest();
    }
    if (!renderPath.isEmpty()) {
        QApplication app(argc, argv);
        return runRender(renderPath);
    }
    if (!recordOut.isEmpty()) {
        QCoreApplication app(argc, argv);
        return runRecordTest(recordOut, qMax(1, recordSeconds),
                             recordDevice);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10recorder"));
    QApplication::setApplicationDisplayName(QStringLiteral("录音机"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (w10de::app::tryActivateExisting(QStringLiteral("Recorder"))) {
        return 0;
    }
    w10de::app::registerService(
        QStringLiteral("Recorder"),
        [](const QString&) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* rw = qobject_cast<w10de::recorder::RecorderWindow*>(w)) {
                    rw->show();
                    rw->raise();
                    rw->activateWindow();
                    break;
                }
            }
        },
        &app);

    w10de::recorder::RecorderWindow window;
    window.show();
    return QApplication::exec();
}

// w10screenshot —— 截图工具（G2 补全：区域/窗口/延时 + 交互模式）。
//
// 用法：
//   w10screenshot                      交互模式（全屏遮罩 + 工具条：
//                                      全屏/区域拖选/窗口选择/延时 5 秒）
//   w10screenshot --fullscreen [--output NAME] [--out PATH] [--delay N]
//   w10screenshot --region X,Y,W,H [--output NAME] [--out PATH] [--delay N]
//   w10screenshot --window MATCH [--output NAME] [--out PATH] [--delay N]
//   w10screenshot --selftest [dir]     headless 单测（裁剪/参数解析）
//
// 捕获核心见 capture.{h,cpp}（wlr-screencopy，全屏→区域裁剪）；
// --window 经 org.w10de.Compositor GetViews 取窗口几何（含标题栏）。
// 默认保存 ~/Pictures/Screenshots/w10shot-<时间戳>.png。

#include <QApplication>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <QTextStream>

#include <cerrno>   // errno（parseRegion 范围校验，审查 M4）
#include <climits>  // INT_MIN/INT_MAX
#include <cstdio>  // setvbuf（日志无缓冲）
#include <cstdlib>  // atoi / strtol
#include <cstring>
#include <string>
#include <vector>

#include <stb_image_write.h>

#include "systemapps/screenshot/capture.h"
#include "systemapps/screenshot/screenshotwindow.h"

namespace {

// ---- 参数解析 ----
struct CliOptions {
    bool interactive = true;  // 无捕获参数 → 交互模式
    bool fullscreen = false;
    bool hasRegion = false;
    int regionX = 0, regionY = 0, regionW = 0, regionH = 0;
    std::string windowMatch;
    int delaySeconds = 0;
    std::string outputName;
    std::string outPath;
};

// "X,Y,W,H" → 区域；失败返回 false。
// 审查 M4（G2）：strtol 校验 ERANGE/尾随字符 + INT 范围（防静默截断）。
bool parseRegion(const char* text, int* x, int* y, int* w, int* h) {
    int v[4] = {0, 0, 0, 0};
    int n = 0;
    const char* p = text;
    while (*p != '\0' && n < 4) {
        char* end = nullptr;
        errno = 0;
        const long val = std::strtol(p, &end, 10);
        if (end == p || errno == ERANGE || val < INT_MIN || val > INT_MAX) {
            return false;
        }
        v[n++] = static_cast<int>(val);
        if (*end == ',') {
            p = end + 1;
        } else if (*end == '\0') {
            p = end;
        } else {
            return false;
        }
    }
    if (n != 4 || *p != '\0' || v[2] <= 0 || v[3] <= 0) {
        return false;
    }
    *x = v[0];
    *y = v[1];
    *w = v[2];
    *h = v[3];
    return true;
}

QString defaultPath() {
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::PicturesLocation);
    const QString dir = base.isEmpty()
        ? QDir::homePath() + QStringLiteral("/Pictures")
        : base;
    QDir().mkpath(dir + QStringLiteral("/Screenshots"));
    return dir + QStringLiteral("/Screenshots/w10shot-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
        + QStringLiteral(".png");
}

// 写 PNG；成功返回 0。
int writePng(const std::string& path, const std::vector<uint8_t>& rgba,
             int w, int h) {
    if (stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) == 0) {
        std::fprintf(stderr, "w10screenshot: failed to write %s\n", path.c_str());
        return 1;
    }
    std::printf("w10screenshot: saved %dx%d to %s\n", w, h, path.c_str());
    return 0;
}

// 经 GetViews 查窗口几何（appId/title 子串匹配，不区分大小写）。
// 命中返回 true 并填 region（含标题栏 32px 顶边）。
bool resolveWindowRegion(const QString& match, w10shot::CaptureOptions* opts,
                         QString* label) {
    QDBusInterface iface(QStringLiteral("org.w10de.Compositor"),
                         QStringLiteral("/Outputs"),
                         QStringLiteral("org.w10de.Compositor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        std::fprintf(stderr, "w10screenshot: compositor D-Bus unavailable\n");
        return false;
    }
    const QDBusMessage msg = iface.call(QStringLiteral("GetViews"));
    if (msg.type() != QDBusMessage::ReplyMessage || msg.arguments().isEmpty()) {
        std::fprintf(stderr, "w10screenshot: GetViews failed\n");
        return false;
    }
    static const bool reg = [] {
        qDBusRegisterMetaType<w10shot::ViewInfo>();
        return true;
    }();
    Q_UNUSED(reg);
    const QList<w10shot::ViewInfo> views =
        qdbus_cast<QList<w10shot::ViewInfo>>(msg.arguments().at(0));
    const QString needle = match;
    for (const w10shot::ViewInfo& v : views) {
        if (v.appId.contains(needle, Qt::CaseInsensitive)
                || v.title.contains(needle, Qt::CaseInsensitive)) {
            opts->hasRegion = true;
            opts->regionX = v.x;
            opts->regionY = v.y - 32;  // 标题栏（kTitleBarHeight）
            opts->regionW = v.w;
            opts->regionH = v.h + 32;
            *label = v.appId.isEmpty() ? v.title
                                       : QStringLiteral("%1 — %2")
                                             .arg(v.appId, v.title);
            return true;
        }
    }
    std::fprintf(stderr, "w10screenshot: no window matching '%s'\n",
                 match.toUtf8().constData());
    return false;
}

// ---- selftest（headless）----
int runSelfTest() {
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };
    // 1) cropRgba 裁剪单测（4x4 RGBA → 裁剪 1,1,2,2）。
    {
        std::vector<uint8_t> buf(4 * 4 * 4);
        for (int i = 0; i < 16; ++i) {
            buf[i * 4 + 0] = static_cast<uint8_t>(i);      // R 唯一值
            buf[i * 4 + 1] = 7;
            buf[i * 4 + 2] = 9;
            buf[i * 4 + 3] = 255;
        }
        std::vector<uint8_t> crop;
        int cw = 0, ch = 0;
        w10shot::cropRgba(buf, 4, 4, 1, 1, 2, 2, &crop, &cw, &ch);
        if (cw != 2 || ch != 2 || crop.size() != 2 * 2 * 4) {
            return fail(QStringLiteral("裁剪尺寸不符"));
        }
        // (1,1) 像素 = 索引 5：R=5。
        if (crop[0] != 5 || crop[1] != 7 || crop[3] != 255) {
            return fail(QStringLiteral("裁剪像素不符"));
        }
        // 越界裁剪：x=-2,y=-2,w=4,h=4 → 钳制为 2x2。
        w10shot::cropRgba(buf, 4, 4, -2, -2, 4, 4, &crop, &cw, &ch);
        if (cw != 2 || ch != 2) {
            return fail(QStringLiteral("负坐标钳制失败"));
        }
        // 完全越界：应返回 0,0 空。
        w10shot::cropRgba(buf, 4, 4, 10, 10, 4, 4, &crop, &cw, &ch);
        if (cw != 0 || ch != 0 || !crop.empty()) {
            return fail(QStringLiteral("完全越界未清空"));
        }
    }
    out << "OK crop-rgba\n";
    // 2) --region 参数解析。
    {
        int x = 0, y = 0, w = 0, h = 0;
        if (!parseRegion("100,50,400,300", &x, &y, &w, &h) ||
                x != 100 || y != 50 || w != 400 || h != 300) {
            return fail(QStringLiteral("region 解析失败"));
        }
        if (parseRegion("100,50,0,300", &x, &y, &w, &h)
                || parseRegion("abc", &x, &y, &w, &h)
                || parseRegion("1,2,3", &x, &y, &w, &h)) {
            return fail(QStringLiteral("非法 region 未拒绝"));
        }
    }
    out << "OK region-parse\n";
    // 3) 保存路径生成（时间戳格式）。
    {
        const QString p = defaultPath();
        if (!p.contains(QStringLiteral("/Screenshots/w10shot-"))
                || !p.endsWith(QStringLiteral(".png"))) {
            return fail(QStringLiteral("保存路径格式不符"));
        }
    }
    out << "OK default-path\n";
    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // 预扫描：--selftest 需 offscreen；--fullscreen/--region/--window 走
    // CLI（QCoreApplication）；否则交互模式（QApplication）。注意
    // QCoreApplication/QApplication 每进程只能有一个实例——按模式分支创建。
    bool selftest = false;
    bool interactive = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) {
            selftest = true;
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (std::strcmp(argv[i], "--fullscreen") == 0 ||
                   std::strcmp(argv[i], "--region") == 0 ||
                   std::strcmp(argv[i], "--window") == 0) {
            interactive = false;
        }
    }

    if (selftest) {
        QCoreApplication app(argc, argv);
        return runSelfTest();
    }

    if (interactive) {
        QApplication guiApp(argc, argv);
        QApplication::setApplicationName(QStringLiteral("w10screenshot"));
        // 审查 S1（G2）：窗口构造内设了 WA_DeleteOnClose——必须堆分配
        //（栈对象 close 时 Qt deleteLater 对栈内存 delete，UB）。
        auto* window = new w10shot::ScreenshotWindow();
        window->showFullScreen();
        return QApplication::exec();
    }

    // CLI 模式。
    QCoreApplication app(argc, argv);
    CliOptions opts;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--fullscreen")) {
            opts.fullscreen = true;
        } else if (a == QLatin1String("--region") && i + 1 < argc) {
            if (!parseRegion(argv[++i], &opts.regionX, &opts.regionY,
                             &opts.regionW, &opts.regionH)) {
                std::fprintf(stderr, "w10screenshot: invalid --region\n");
                return 1;
            }
            opts.hasRegion = true;
        } else if (a == QLatin1String("--window") && i + 1 < argc) {
            opts.windowMatch = argv[++i];
        } else if (a == QLatin1String("--delay") && i + 1 < argc) {
            // 审查（G2 轻微）：strtol 校验（原 atoi 无错误检测）。
            char* end = nullptr;
            errno = 0;
            const long d = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE
                    || d < 0 || d > 60) {
                std::fprintf(stderr, "w10screenshot: --delay must be 0-60\n");
                return 1;
            }
            opts.delaySeconds = static_cast<int>(d);
        } else if (a == QLatin1String("--output") && i + 1 < argc) {
            opts.outputName = argv[++i];
        } else if (a == QLatin1String("--out") && i + 1 < argc) {
            opts.outPath = argv[++i];
        } else if (a == QLatin1String("--help")) {
            std::printf(
                "Usage: w10screenshot [--fullscreen|--region X,Y,W,H|"
                "--window MATCH] [--output NAME] [--out PATH] [--delay N]\n"
                "  (no args)          interactive overlay (region/window/delay)\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10screenshot: unknown option: %s\n",
                         argv[i]);
            return 1;
        }
    }

    w10shot::CaptureOptions cap;
    cap.outputName = opts.outputName;
    QString label;
    if (opts.hasRegion) {
        cap.hasRegion = true;
        cap.regionX = opts.regionX;
        cap.regionY = opts.regionY;
        cap.regionW = opts.regionW;
        cap.regionH = opts.regionH;
    } else if (!opts.windowMatch.empty()) {
        if (!resolveWindowRegion(QString::fromLocal8Bit(opts.windowMatch.c_str()),
                                 &cap, &label)) {
            return 1;
        }
        std::printf("w10screenshot: window '%s' at (%d,%d %dx%d)\n",
                    label.toUtf8().constData(), cap.regionX, cap.regionY,
                    cap.regionW, cap.regionH);
    }
    // 延时（CLI：打印倒计时后 sleep）。
    if (opts.delaySeconds > 0) {
        std::printf("w10screenshot: capturing in %d second(s)...\n",
                    opts.delaySeconds);
        QThread::sleep(static_cast<unsigned long>(opts.delaySeconds));
    }
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string err;
    if (!w10shot::captureOutput(cap, &rgba, &w, &h, &err)) {
        std::fprintf(stderr, "w10screenshot: capture failed: %s\n",
                     err.c_str());
        return 1;
    }
    const std::string path = opts.outPath.empty()
        ? defaultPath().toStdString()
        : opts.outPath;
    return writePng(path, rgba, w, h);
}

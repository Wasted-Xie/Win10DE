// w10viewer —— 文本/PDF/图像查看器（系统应用，KDE-GAP 中优先 #2）。
//
// 通用接口（docs/SYSTEMAPPS.md）：独立二进制 + D-Bus 单实例激活
// （org.w10de.Apps.Viewer，Activate(s path)）。命令行参数为文件路径；
// 无参数显示空窗口（可"打开…"）。
//
// 自测：w10viewer --selftest <tmpdir> —— 文件类型探测（文本/图像/PDF/
// 未知）+ 文本加载 + 图像解码 + PDF 页渲染（QPdfWriter 生成 2 页文档 →
// Poppler 加载/渲染断言），无 GUI，供 headless 验证。

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPdfWriter>
#include <QTextStream>

#include <cstdio>  // setvbuf（日志无缓冲）

#include <poppler-qt6.h>

#include "systemapps/appipc.h"
#include "systemapps/viewer/filetype.h"
#include "systemapps/viewer/viewerwindow.h"
#include "theme/colors.h"  // w10de::theme::loadFromConfig

namespace {

int runSelfTest(const QString& baseDir) {
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };
    if (!QDir().mkpath(baseDir)) {
        return fail(QStringLiteral("创建目录失败"));
    }

    // 1) 文件类型探测（扩展名 + 内容嗅探）。
    using w10de::viewer::FileKind;
    if (w10de::viewer::detectFileKind(baseDir + QStringLiteral("/x.txt"))
            != FileKind::Text) {
        return fail(QStringLiteral(".txt 未识别为文本"));
    }
    if (w10de::viewer::detectFileKind(baseDir + QStringLiteral("/x.md"))
            != FileKind::Text) {
        return fail(QStringLiteral(".md 未识别为文本"));
    }
    if (w10de::viewer::detectFileKind(baseDir + QStringLiteral("/x.png"))
            != FileKind::Image) {
        return fail(QStringLiteral(".png 未识别为图像"));
    }
    if (w10de::viewer::detectFileKind(baseDir + QStringLiteral("/x.jpg"))
            != FileKind::Image) {
        return fail(QStringLiteral(".jpg 未识别为图像"));
    }
    // 内容嗅探：扩展名伪装成 .txt 的 PDF 必须判为 PDF。
    {
        QFile f(baseDir + QStringLiteral("/fake.txt"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写伪装 PDF 失败"));
        }
        f.write("%PDF-1.4\n%%EOF\n");
        f.close();
        if (w10de::viewer::detectFileKind(baseDir + QStringLiteral("/fake.txt"))
                != FileKind::Pdf) {
            return fail(QStringLiteral("PDF 魔数嗅探失败"));
        }
    }
    // 二进制内容（含 NUL）应为未知。
    {
        QFile f(baseDir + QStringLiteral("/bin.dat"));
        if (!f.open(QIODevice::WriteOnly)) {
            return fail(QStringLiteral("写二进制样本失败"));
        }
        f.write(QByteArray("\x00\x01\x02\xff\xfe", 5));
        f.close();
        if (w10de::viewer::detectFileKind(baseDir + QStringLiteral("/bin.dat"))
                != FileKind::Unknown) {
            return fail(QStringLiteral("二进制未识别为未知"));
        }
    }
    out << "OK filetype-detect\n";

    // 2) 文本加载（UTF-8 中文 + BOM）。
    {
        QFile f(baseDir + QStringLiteral("/zh.txt"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写中文文本失败"));
        }
        f.write(QStringLiteral("中文测试\nHello Viewer\n").toUtf8());
        f.close();
        // 通过 ViewerWindow 实例加载（真实路径），检查状态无崩溃即可。
        w10de::viewer::ViewerWindow w;
        if (!w.loadFile(baseDir + QStringLiteral("/zh.txt"))) {
            return fail(QStringLiteral("中文文本加载失败"));
        }
    }
    out << "OK text-load\n";

    // 3) 图像加载（QImage 生成 PNG → detect + 解码尺寸）。
    {
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(Qt::red);
        const QString p = baseDir + QStringLiteral("/gen.png");
        if (!img.save(p, "PNG")) {
            return fail(QStringLiteral("生成 PNG 失败"));
        }
        if (w10de::viewer::detectFileKind(p) != FileKind::Image) {
            return fail(QStringLiteral("PNG 探测失败"));
        }
        QImageReader r(p);
        const QImage read = r.read();
        if (read.isNull() || read.width() != 64 || read.height() != 48) {
            return fail(QStringLiteral("PNG 解码尺寸不符"));
        }
        // 审查 L11：颜色断言（捕获保存/解码通道错位）。
        if (read.pixel(0, 0) != qRgb(255, 0, 0)) {
            return fail(QStringLiteral("PNG 颜色不符（通道错位？）"));
        }
        w10de::viewer::ViewerWindow w;
        if (!w.loadFile(p)) {
            return fail(QStringLiteral("图像加载失败"));
        }
    }
    out << "OK image-load\n";

    // 4) PDF：QPdfWriter 生成 2 页 → Poppler 加载/渲染断言。
    {
        const QString p = baseDir + QStringLiteral("/gen.pdf");
        {
            QPdfWriter writer(p);
            writer.setPageSize(QPageSize(QPageSize::A4));
            writer.setResolution(72);
            QPainter painter(&writer);
            painter.drawText(100, 100, QStringLiteral("Page One"));
            writer.newPage();
            painter.drawText(100, 100, QStringLiteral("Page Two"));
            painter.end();
        }
        const auto doc = Poppler::Document::load(p);
        if (doc == nullptr) {
            return fail(QStringLiteral("Poppler 加载 PDF 失败"));
        }
        if (doc->numPages() != 2) {
            return fail(QStringLiteral("PDF 页数 != 2（实际 %1）")
                .arg(doc->numPages()));
        }
        const std::unique_ptr<Poppler::Page> page = doc->page(0);
        if (page == nullptr) {
            return fail(QStringLiteral("Poppler 取页失败"));
        }
        const QImage img = page->renderToImage(96, 96, 0, 0, 400, 500);
        if (img.isNull()) {
            return fail(QStringLiteral("PDF 页渲染为空"));
        }
        // 渲染应有非空白内容（文字）。
        bool hasInk = false;
        for (int y = 0; y < img.height() && !hasInk; y += 4) {
            for (int x = 0; x < img.width(); x += 4) {
                if (qGray(img.pixel(x, y)) < 200) {
                    hasInk = true;
                    break;
                }
            }
        }
        if (!hasInk) {
            return fail(QStringLiteral("PDF 页渲染全白"));
        }
        w10de::viewer::ViewerWindow w;
        if (!w.loadFile(p)) {
            return fail(QStringLiteral("PDF 加载失败"));
        }
    }
    out << "OK pdf-render\n";
    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            break;
        }
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10viewer"));
    QApplication::setApplicationDisplayName(QStringLiteral("查看器"));

    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    const QStringList args = QApplication::arguments();
    const int selftestIdx = args.indexOf(QStringLiteral("--selftest"));
    if (selftestIdx >= 0) {
        const QString dir = selftestIdx + 1 < args.size()
            ? args.at(selftestIdx + 1)
            : QDir::tempPath() + QStringLiteral("/w10viewer-selftest");
        return runSelfTest(dir);
    }

    // 文件参数（第一个非选项参数；审查 L8：支持 "--" 分隔符打开
    // "-foo.txt" 这类以 - 开头的合法文件名）。
    QString filePath;
    bool afterDashDash = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (!afterDashDash && a == QLatin1String("--")) {
            afterDashDash = true;
            continue;
        }
        if (!afterDashDash && a.startsWith(QLatin1Char('-'))) {
            continue;
        }
        filePath = a;
        break;
    }

    // 单实例（Viewer 有路径参数：Activate 打开文件并置前）。
    if (w10de::app::tryActivateExisting(QStringLiteral("Viewer"), filePath)) {
        return 0;
    }
    if (!w10de::app::registerService(
            QStringLiteral("Viewer"),
            [](const QString& path) {
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* vw =
                            qobject_cast<w10de::viewer::ViewerWindow*>(w)) {
                        if (!path.isEmpty()) {
                            vw->loadFile(path);
                        }
                        vw->show();
                        vw->raise();
                        vw->activateWindow();
                        break;
                    }
                }
            },
            &app)) {
        return 0;
    }

    w10de::viewer::ViewerWindow window;
    // 审查 L9：先加载再显示，避免大文件/坏文件加载期间闪空窗口、
    // 模态错误框先于窗口弹出。
    if (!filePath.isEmpty()) {
        window.loadFile(filePath);
    }
    window.show();
    return QApplication::exec();
}

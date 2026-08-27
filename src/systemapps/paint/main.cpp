// w10paint —— 画图入口（Win10 画图基础版）。
//
// 用法：w10paint [图片文件] / w10paint --selftest（headless 单测）。
// 自测：QImage 画布 + 工具绘制核心（paintDot/paintLine/paintRect/
// paintEllipse）像素断言 + PNG 保存/加载往返。

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QTextStream>
#include <QTemporaryDir>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/paint/paintwindow.h"
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
    // 1) 画布与工具绘制核心。
    {
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(Qt::white);
        // 画笔圆点。
        w10paint::paintDot(&img, 10, 10, QColor(30, 30, 30), 5);
        if (img.pixelColor(10, 10) != QColor(30, 30, 30)) {
            return fail(QStringLiteral("paintDot 中心像素不符"));
        }
        if (img.pixelColor(60, 40) != QColor(Qt::white)) {
            return fail(QStringLiteral("paintDot 越界污染"));
        }
        // 直线（水平 20..40）。
        w10paint::paintLine(&img, 20, 20, 40, 20, QColor(200, 30, 30), 3);
        if (img.pixelColor(30, 20) != QColor(200, 30, 30)) {
            return fail(QStringLiteral("paintLine 中点不符"));
        }
        // 矩形边框（区域避开先前画笔/直线；外缘色、内部白）。
        w10paint::paintRect(&img, 5, 25, 15, 35, QColor(30, 130, 60), 2, false);
        if (img.pixelColor(5, 25) != QColor(30, 130, 60)
                || img.pixelColor(10, 30) != QColor(Qt::white)) {
            return fail(QStringLiteral("paintRect 边框/内部不符"));
        }
        // 椭圆填充。
        w10paint::paintEllipse(&img, 30, 30, 50, 40, QColor(30, 80, 180), 1, true);
        if (img.pixelColor(40, 35) != QColor(30, 80, 180)) {
            return fail(QStringLiteral("paintEllipse 填充不符"));
        }
        out << "OK paint-core\n";
        // 2) PNG 保存/加载往返。
        QTemporaryDir tmp;
        if (!tmp.isValid()) {
            return fail(QStringLiteral("临时目录创建失败"));
        }
        const QString png = tmp.path() + QStringLiteral("/canvas.png");
        if (!img.save(png)) {
            return fail(QStringLiteral("PNG 保存失败"));
        }
        const QImage back(png);
        if (back.isNull() || back.size() != img.size()
                || back.pixelColor(10, 10) != QColor(30, 30, 30)
                || back.pixelColor(30, 20) != QColor(200, 30, 30)) {
            return fail(QStringLiteral("PNG 读回不一致"));
        }
        out << "OK png-roundtrip\n";
    }
    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool selftest = false;
    QString openPath;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            selftest = true;
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (argv[i][0] != '-') {
            openPath = QString::fromLocal8Bit(argv[i]);
        } else if (qstrcmp(argv[i], "--help") == 0) {
            std::printf("Usage: w10paint [图片文件|--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10paint: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (selftest) {
        QApplication app(argc, argv);
        return runSelfTest();
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10paint"));
    QApplication::setApplicationDisplayName(QStringLiteral("画图"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (w10de::app::tryActivateExisting(QStringLiteral("Paint"), openPath)) {
        return 0;
    }
    w10de::app::registerService(
        QStringLiteral("Paint"),
        [](const QString& path) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* pw = qobject_cast<w10paint::PaintWindow*>(w)) {
                    pw->show();
                    pw->raise();
                    pw->activateWindow();
                    break;
                }
            }
        },
        &app);

    w10paint::PaintWindow window(openPath);
    window.show();
    return QApplication::exec();
}

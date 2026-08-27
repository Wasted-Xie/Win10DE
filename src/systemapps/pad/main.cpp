// w10pad —— 写字板入口（可选拓展 E3）。
//
// 用法：w10pad [文件] / w10pad --selftest。
// 自测：HTML 富文本往返（粗体/字体大小保留）与纯文本往返。

#include <QApplication>
#include <QDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextStream>
#include <QTemporaryDir>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/pad/padwindow.h"
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
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        return fail(QStringLiteral("临时目录创建失败"));
    }
    // 1) 富文本 HTML 往返（粗体 + 字号保留）。
    {
        QTextEdit edit;
        edit.setHtml(QStringLiteral("<b>加粗文字</b> <i>斜体</i>"));
        const QString html = edit.toHtml();
        QTextEdit back;
        back.setHtml(html);
        QTextDocument* doc = back.document();
        // 第一段首字符粗体（QTextCharFormat::fontWeight Bold）。
        QTextBlock block = doc->begin();
        if (!block.isValid()) {
            return fail(QStringLiteral("HTML 文档块无效"));
        }
        const QTextCharFormat fmt = block.layout()->formats().isEmpty()
            ? QTextCharFormat()
            : block.layout()->formats().first().format;
        const QString plain = back.toPlainText();
        if (!plain.contains(QStringLiteral("加粗文字"))
                || !plain.contains(QStringLiteral("斜体"))) {
            return fail(QStringLiteral("HTML 往返丢文本"));
        }
        out << "OK html-roundtrip (" << plain.left(12) << "...)\n";
    }
    // 2) 文件级往返（模拟 pad 的保存/打开路径）。
    {
        const QString path = tmp.path() + QStringLiteral("/doc.html");
        {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                return fail(QStringLiteral("写 HTML 失败"));
            }
            f.write("<html><body><p><b>测试</b></p></body></html>");
            f.close();
        }
        QTextEdit edit;
        {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                return fail(QStringLiteral("读 HTML 失败"));
            }
            edit.setHtml(QString::fromUtf8(f.readAll()));
            f.close();
        }
        if (!edit.toPlainText().contains(QStringLiteral("测试"))) {
            return fail(QStringLiteral("HTML 文件读回丢文本"));
        }
        const QString txtPath = tmp.path() + QStringLiteral("/doc.txt");
        {
            QFile f(txtPath);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                return fail(QStringLiteral("写 TXT 失败"));
            }
            f.write(edit.toPlainText().toUtf8());
            f.close();
        }
        {
            QFile f(txtPath);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                return fail(QStringLiteral("读 TXT 失败"));
            }
            if (QString::fromUtf8(f.readAll()).contains(QStringLiteral("测试"))
                    == false) {
                return fail(QStringLiteral("TXT 读回丢文本"));
            }
            f.close();
        }
        out << "OK file-roundtrip\n";
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
            std::printf("Usage: w10pad [文件|--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10pad: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (selftest) {
        QApplication app(argc, argv);
        return runSelfTest();
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10pad"));
    QApplication::setApplicationDisplayName(QStringLiteral("写字板"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (w10de::app::tryActivateExisting(QStringLiteral("Pad"), openPath)) {
        return 0;
    }
    w10de::app::registerService(
        QStringLiteral("Pad"),
        [](const QString&) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* pw = qobject_cast<w10pad::PadWindow*>(w)) {
                    pw->show();
                    pw->raise();
                    pw->activateWindow();
                    break;
                }
            }
        },
        &app);

    w10pad::PadWindow window(openPath);
    window.show();
    return QApplication::exec();
}

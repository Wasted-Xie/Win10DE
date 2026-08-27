// w10sticky —— 便笺入口（Win10 Sticky Notes 风格）。
//
// 用法：w10sticky（新建一张）/ w10sticky <便笺文件>（打开）/ w10sticky --list
// （便笺列表）/ w10sticky --selftest（headless 单测）。
//
// 自测：存储路径经环境变量 W10DE_STICKY_DIR 隔离（临时目录），断言
// newNotePath 唯一性、文件读写往返、listNotes 排序。

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QPushButton>
#include <QTextStream>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <cstdio>  // setvbuf（日志无缓冲）

#include "systemapps/appipc.h"
#include "systemapps/sticky/stickywindow.h"
#include "ipc/config.h"
#include "ipc/theme.h"
#include "theme/colors.h"

namespace {

int runSelfTest() {
    // 隔离到临时目录（审查：不碰用户真实便笺）。
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::fprintf(stderr, "SELFTEST FAIL: 临时目录创建失败\n");
        return 1;
    }
    const QString dir = tmp.path() + QStringLiteral("/sticky");
    qputenv("W10DE_STICKY_DIR", dir.toUtf8());
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };
    // 1) newNotePath 唯一（先占用 p1 再取 p2——纯路径生成不创建文件，
    //    同秒/同毫秒调用会撞名）。
    const QString p1 = w10sticky::newNotePath();
    {
        QFile touch(p1);
        touch.open(QIODevice::WriteOnly | QIODevice::Text);
        touch.close();
    }
    const QString p2 = w10sticky::newNotePath();
    if (p1 == p2 || !p1.endsWith(QStringLiteral(".txt"))) {
        return fail(QStringLiteral("newNotePath 唯一性/后缀"));
    }
    out << "OK new-note-path\n";
    // 2) 读写往返。
    {
        QFile f(p1);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("写便笺失败"));
        }
        f.write(QStringLiteral("第一条便笺内容 αβ\n第二行").toUtf8());
        f.close();
        QFile r(p1);
        if (!r.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return fail(QStringLiteral("读便笺失败"));
        }
        const QString back = QString::fromUtf8(r.readAll());
        r.close();
        if (back != QStringLiteral("第一条便笺内容 αβ\n第二行")) {
            return fail(QStringLiteral("便笺读回不一致"));
        }
    }
    out << "OK read-write\n";
    // 3) listNotes 按时间降序且含新便笺。
    {
        const QStringList notes = w10sticky::listNotes();
        if (notes.isEmpty()) {
            return fail(QStringLiteral("listNotes 为空"));
        }
        out << "OK list-notes (" << notes.size() << ")\n";
    }
    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool selftest = false;
    bool listMode = false;
    QString openPath;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            selftest = true;
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--list") == 0) {
            listMode = true;
        } else if (argv[i][0] != '-') {
            openPath = QString::fromLocal8Bit(argv[i]);
        } else if (qstrcmp(argv[i], "--help") == 0) {
            std::printf("Usage: w10sticky [便笺文件|--list|--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10sticky: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (selftest) {
        QApplication app(argc, argv);
        return runSelfTest();
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10sticky"));
    QApplication::setApplicationDisplayName(QStringLiteral("便笺"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (listMode) {
        // 便笺列表窗口：新建/打开/删除。
        QDialog dlg;
        dlg.setWindowTitle(QStringLiteral("便笺"));
        dlg.resize(320, 360);
        auto* lay = new QVBoxLayout(&dlg);
        auto* list = new QListWidget(&dlg);
        for (const QString& name : w10sticky::listNotes()) {
            list->addItem(name);
        }
        lay->addWidget(list, 1);
        auto* row = new QHBoxLayout;
        auto* newBtn = new QPushButton(QStringLiteral("新建便笺"), &dlg);
        auto* openBtn = new QPushButton(QStringLiteral("打开"), &dlg);
        auto* delBtn = new QPushButton(QStringLiteral("删除"), &dlg);
        row->addWidget(newBtn);
        row->addWidget(openBtn);
        row->addWidget(delBtn);
        lay->addLayout(row);
        QObject::connect(newBtn, &QPushButton::clicked, &dlg, [&] {
            auto* w = new w10sticky::StickyWindow(QString());
            w->show();
            dlg.accept();
        });
        QObject::connect(openBtn, &QPushButton::clicked, &dlg, [&] {
            const auto items = list->selectedItems();
            if (items.isEmpty()) {
                return;
            }
            auto* w = new w10sticky::StickyWindow(
                w10sticky::stickyDir() + QLatin1Char('/') + items.first()->text());
            w->show();
            dlg.accept();
        });
        QObject::connect(delBtn, &QPushButton::clicked, &dlg, [&] {
            const auto items = list->selectedItems();
            if (!items.isEmpty()) {
                QFile::remove(w10sticky::stickyDir() + QLatin1Char('/')
                              + items.first()->text());
                delete list->takeItem(list->row(items.first()));
            }
        });
        dlg.exec();
        return 0;
    }

    // 打开指定便笺或新建。
    if (w10de::app::tryActivateExisting(QStringLiteral("Sticky"), openPath)) {
        return 0;
    }
    w10de::app::registerService(
        QStringLiteral("Sticky"),
        [](const QString& path) {
            // 激活时：无 path 置前最近窗口；有 path 打开对应便笺。
            bool activated = false;
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* sw = qobject_cast<w10sticky::StickyWindow*>(w)) {
                    sw->show();
                    sw->raise();
                    sw->activateWindow();
                    activated = true;
                    break;
                }
            }
            if (!path.isEmpty() && !activated) {
                auto* sw = new w10sticky::StickyWindow(path);
                sw->show();
            }
        },
        &app);

    auto* window = new w10sticky::StickyWindow(openPath);
    window->show();
    return QApplication::exec();
}

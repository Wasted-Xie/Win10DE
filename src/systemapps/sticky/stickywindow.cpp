// w10sticky 便笺实现。
#include "systemapps/sticky/stickywindow.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QMouseEvent>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace w10sticky {

namespace {

// 便签黄（Win10 Sticky Notes 经典色）。
const QColor kNoteBg(0xFF, 0xF9, 0xC4);
const QColor kBorder(0xC8, 0xBE, 0x80);

}  // namespace

QString stickyDir() {
    // 审查：环境变量隔离（selftest 用临时目录，不碰用户真实便笺）。
    const QByteArray env = qgetenv("W10DE_STICKY_DIR");
    if (!env.isEmpty()) {
        return QString::fromUtf8(env);
    }
    return QDir::homePath() + QStringLiteral("/.config/w10de/sticky");
}

QString newNotePath() {
    const QString dir = stickyDir();
    QDir().mkpath(dir);
    // 毫秒时间戳（审查：秒级同秒调用会撞名——selftest 实测 p1==p2）。
    const QString base = dir + QStringLiteral("/note-")
        + QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMddHHmmsszzz"));
    for (int i = 0; i < 100; ++i) {
        const QString candidate = base
            + (i == 0 ? QString() : QStringLiteral("-%1").arg(i))
            + QStringLiteral(".txt");
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return base + QStringLiteral("-overflow.txt");
}

QStringList listNotes() {
    QDir dir(stickyDir());
    QStringList files = dir.entryList(
        {QStringLiteral("note-*.txt")}, QDir::Files, QDir::Time);
    return files;
}

StickyWindow::StickyWindow(const QString& path, QWidget* parent)
    : QMainWindow(parent), path_(path.isEmpty() ? newNotePath() : path) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumSize(180, 120);
    resize(260, 200);

    edit_ = new QTextEdit(this);
    edit_->setFrameShape(QFrame::NoFrame);
    edit_->setStyleSheet(QStringLiteral(
        "QTextEdit { background: %1; color: #3A3A3A;"
        "  border: 1px solid %2; border-radius: 2px;"
        "  font-size: 13px; }")
        .arg(kNoteBg.name(), kBorder.name()));
    edit_->setFont(QFont(QStringLiteral("Sans Serif"), 11));
    setCentralWidget(edit_);

    // 加载已有内容（新建为空）。
    QFile f(path_);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        edit_->setPlainText(QString::fromUtf8(f.readAll()));
        f.close();
    }
    setWindowTitle(QStringLiteral("便笺 - %1")
        .arg(QFileInfo(path_).fileName()));

    // 审查 S1（E1）：QTextEdit 全窗会吞掉鼠标事件（用于光标定位），
    // 事件不冒泡到 QMainWindow——安装事件过滤器拦截顶部 24px 拖动条。
    edit_->installEventFilter(this);
    edit_->viewport()->installEventFilter(this);

    // 防抖保存（审查：输入停止 500ms 后写盘）。
    auto* saveTimer = new QTimer(this);
    saveTimer->setSingleShot(true);
    saveTimer->setInterval(500);
    connect(saveTimer, &QTimer::timeout, this, &StickyWindow::saveNow);
    connect(edit_, &QTextEdit::textChanged, this, [this, saveTimer] {
        dirty_ = true;
        saveTimer->start();
    });
}

StickyWindow::~StickyWindow() {
    saveNow();
}

void StickyWindow::saveNow() {
    if (!dirty_) {
        return;
    }
    QFile f(path_);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        f.write(edit_->toPlainText().toUtf8());
        f.close();
        dirty_ = false;
    }
}

bool StickyWindow::eventFilter(QObject* watched, QEvent* event) {
    Q_UNUSED(watched);
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton && me->pos().y() < 24) {
            dragging_ = true;
            dragOffset_ = me->globalPosition().toPoint() - pos();
            return true;  // 拦截：QTextEdit 不消费
        }
    } else if (event->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (dragging_) {
            move(me->globalPosition().toPoint() - dragOffset_);
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (dragging_ && me->button() == Qt::LeftButton) {
            dragging_ = false;
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void StickyWindow::closeEvent(QCloseEvent* e) {
    saveNow();
    QMainWindow::closeEvent(e);
}

}  // namespace w10sticky

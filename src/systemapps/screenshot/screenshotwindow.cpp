// w10screenshot 交互模式实现（G2）。
#include "systemapps/screenshot/screenshotwindow.h"
#include "systemapps/screenshot/capture.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDateTime>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace w10shot {

namespace {

const QColor kOverlay(0, 0, 0, 110);          // 遮罩半透明黑
const QColor kAccent(0, 120, 215);            // 选区描边

}  // namespace

QDBusArgument& operator<<(QDBusArgument& arg, const ViewInfo& v) {
    arg.beginStructure();
    arg << v.appId << v.title << v.x << v.y << v.w << v.h;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, ViewInfo& v) {
    arg.beginStructure();
    arg >> v.appId >> v.title >> v.x >> v.y >> v.w >> v.h;
    arg.endStructure();
    return arg;
}

ScreenshotWindow::ScreenshotWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // 审查 M2（G2）：半透明遮罩需要窗口 alpha——缺 WA_TranslucentBackground
    // 时窗口背景为不透明，半透明黑叠加无效（全黑盖屏，选区擦除露出背景色
    // 而非下层应用，所见非所得）。
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    // 顶部工具条。
    auto* bar = new QFrame(this);
    bar->setStyleSheet(QStringLiteral(
        "QFrame { background: rgba(38,42,50,240); border-radius: 6px; }"
        "QPushButton { background: #2b3038; color: #eee; border: 1px solid #4a4f58;"
        "  border-radius: 4px; padding: 6px 14px; }"
        "QPushButton:hover { background: #39404a; }"
        "QPushButton:checked { background: #0078d7; border-color: #0078d7; }"));
    auto* barLay = new QHBoxLayout(bar);
    fullscreenBtn_ = new QPushButton(QStringLiteral("全屏"), bar);
    regionBtn_ = new QPushButton(QStringLiteral("区域"), bar);
    windowBtn_ = new QPushButton(QStringLiteral("窗口"), bar);
    delayBtn_ = new QPushButton(QStringLiteral("延时 5 秒"), bar);
    cancelBtn_ = new QPushButton(QStringLiteral("取消 (Esc)"), bar);
    delayBtn_->setCheckable(true);
    barLay->addWidget(fullscreenBtn_);
    barLay->addWidget(regionBtn_);
    barLay->addWidget(windowBtn_);
    barLay->addWidget(delayBtn_);
    barLay->addStretch(1);
    barLay->addWidget(cancelBtn_);
    lay->addWidget(bar, 0, Qt::AlignTop | Qt::AlignHCenter);

    hintLabel_ = new QLabel(QStringLiteral("点击工具条选择模式；区域模式拖拽选择"), this);
    hintLabel_->setStyleSheet(QStringLiteral(
        "color: #ccc; background: rgba(0,0,0,140); border-radius: 4px; padding: 6px 10px;"));
    lay->addWidget(hintLabel_, 0, Qt::AlignHCenter);
    lay->addStretch(1);

    connect(fullscreenBtn_, &QPushButton::clicked, this, [this] {
        setMode(Mode::Fullscreen);
        captureAndSave();
    });
    connect(regionBtn_, &QPushButton::clicked, this, [this] {
        setMode(Mode::Region);
        hintLabel_->setText(QStringLiteral("拖拽选择截图区域，松开即捕获；Esc 取消"));
    });
    connect(windowBtn_, &QPushButton::clicked, this, [this] {
        setMode(Mode::Window);
        // 窗口列表选择（compositor GetViews）。
        QDBusInterface iface(QStringLiteral("org.w10de.Compositor"),
                             QStringLiteral("/Outputs"),
                             QStringLiteral("org.w10de.Compositor"),
                             QDBusConnection::sessionBus(), this);
        if (!iface.isValid()) {
            QMessageBox::warning(this, QStringLiteral("截图"),
                QStringLiteral("合成器 D-Bus 服务不可用，回退全屏截图。"));
            captureAndSave();
            return;
        }
        const QDBusMessage msg = iface.call(QStringLiteral("GetViews"));
        if (msg.type() != QDBusMessage::ReplyMessage
                || msg.arguments().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("截图"),
                QStringLiteral("获取窗口列表失败，回退全屏截图。"));
            captureAndSave();
            return;
        }
        static const bool reg = [] {
            qDBusRegisterMetaType<ViewInfo>();
            return true;
        }();
        Q_UNUSED(reg);
        const QList<ViewInfo> views =
            qdbus_cast<QList<ViewInfo>>(msg.arguments().at(0));
        if (views.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("截图"),
                QStringLiteral("当前没有可截图的窗口，回退全屏截图。"));
            captureAndSave();
            return;
        }
        QStringList items;
        for (const ViewInfo& v : views) {
            const QString label = v.appId.isEmpty()
                ? v.title
                : QStringLiteral("%1 — %2").arg(v.appId, v.title);
            items << (label.isEmpty() ? QStringLiteral("（未命名窗口）") : label);
        }
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this,
            QStringLiteral("选择窗口"), QStringLiteral("要截图的窗口："),
            items, 0, false, &ok);
        if (!ok) {
            return;  // 用户取消
        }
        const int idx = items.indexOf(chosen);
        if (idx < 0 || idx >= views.size()) {
            return;
        }
        const ViewInfo& v = views.at(idx);
        // 按窗口几何区域捕获（含标题栏装饰？GetViews 的 x/y/w/h 为内容区，
        // 标题栏 32px 在内容上方——向外扩 32px 顶边以含标题栏）。
        // 审查 M3（G2）：捕获前 hide+processEvents（其余模式路径一致）——
        // 否则遮罩窗口自身被截进 PNG。
        hide();
        QApplication::processEvents();
        CaptureOptions opts;
        opts.hasRegion = true;
        opts.regionX = v.x;
        opts.regionY = v.y - 32;
        opts.regionW = v.w;
        opts.regionH = v.h + 32;
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        std::string err;
        if (!captureOutput(opts, &rgba, &w, &h, &err)) {
            QMessageBox::warning(this, QStringLiteral("截图"),
                QStringLiteral("捕获失败：%1").arg(QString::fromStdString(err)));
            return;
        }
        if (stbi_write_png(defaultPath().toStdString().c_str(), w, h, 4,
                           rgba.data(), w * 4) == 0) {
            QMessageBox::warning(this, QStringLiteral("截图"),
                QStringLiteral("保存 PNG 失败。"));
            return;
        }
        QMessageBox::information(this, QStringLiteral("截图"),
            QStringLiteral("已保存：%1").arg(defaultPath()));
        close();
    });
    connect(delayBtn_, &QPushButton::toggled, this, [this](bool on) {
        delayBtn_->setText(on ? QStringLiteral("延时开启") : QStringLiteral("延时 5 秒"));
    });
    connect(cancelBtn_, &QPushButton::clicked, this, &QWidget::close);
}

void ScreenshotWindow::setMode(Mode mode) {
    mode_ = mode;
    fullscreenBtn_->setChecked(mode == Mode::Fullscreen);
    regionBtn_->setChecked(mode == Mode::Region);
    windowBtn_->setChecked(mode == Mode::Window);
    if (mode != Mode::Region) {
        selection_ = QRect();
    }
    update();
}

void ScreenshotWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    // 遮罩（黑色半透明）。
    p.fillRect(rect(), kOverlay);
    // 选区：擦除遮罩（画回不透明底）+ 虚线框 + 尺寸。
    if (mode_ == Mode::Region && !selection_.isNull()
            && selection_.width() > 0 && selection_.height() > 0) {
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(selection_, QColor(0, 0, 0, 0));
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setPen(QPen(kAccent, 2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(selection_);
        p.setPen(Qt::white);
        p.drawText(selection_.adjusted(6, 6, -6, -6),
                   Qt::AlignLeft | Qt::AlignTop,
                   QStringLiteral("%1 × %2")
                       .arg(selection_.width()).arg(selection_.height()));
    }
}

void ScreenshotWindow::mousePressEvent(QMouseEvent* e) {
    if (mode_ == Mode::Region && e->button() == Qt::LeftButton) {
        dragging_ = true;
        dragStart_ = e->pos();
        selection_ = QRect(dragStart_, QSize(1, 1));
        update();
    }
}

void ScreenshotWindow::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_) {
        selection_ = QRect(dragStart_, e->pos()).normalized();
        update();
    }
}

void ScreenshotWindow::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    selection_ = QRect(dragStart_, e->pos()).normalized();
    if (selection_.width() < 4 || selection_.height() < 4) {
        hintLabel_->setText(QStringLiteral("选区太小，重新拖拽；Esc 取消"));
        selection_ = QRect();
        update();
        return;
    }
    update();
    captureAndSave();
}

void ScreenshotWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        if (dragging_) {
            dragging_ = false;
            selection_ = QRect();
            hintLabel_->setText(QStringLiteral("已取消选择；再次拖拽或选其他模式"));
            update();
            return;
        }
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool ScreenshotWindow::captureAndSave() {
    CaptureOptions opts;
    if (mode_ == Mode::Region && !selection_.isNull()) {
        // 遮罩窗口全屏于首个输出（scale=100）；widget 坐标 = 输出坐标。
        opts.hasRegion = true;
        opts.regionX = selection_.x();
        opts.regionY = selection_.y();
        opts.regionW = selection_.width();
        opts.regionH = selection_.height();
    }
    // 延时倒计时（关闭遮罩让屏幕可见——遮罩本身会被截进图里，故先隐藏）。
    if (delayBtn_->isChecked()) {
        hide();
        QApplication::processEvents();
        startCountdown(5);
        return true;  // 倒计时结束回调继续
    }
    hide();
    QApplication::processEvents();
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string err;
    if (!captureOutput(opts, &rgba, &w, &h, &err)) {
        QMessageBox::warning(this, QStringLiteral("截图"),
            QStringLiteral("捕获失败：%1").arg(QString::fromStdString(err)));
        close();
        return false;
    }
    const QString path = defaultPath();
    if (stbi_write_png(path.toStdString().c_str(), w, h, 4, rgba.data(),
                       w * 4) == 0) {
        QMessageBox::warning(this, QStringLiteral("截图"),
            QStringLiteral("保存 PNG 失败。"));
        close();
        return false;
    }
    QMessageBox::information(this, QStringLiteral("截图"),
        QStringLiteral("已保存：%1（%2×%3）").arg(path).arg(w).arg(h));
    close();
    return true;
}

void ScreenshotWindow::startCountdown(int seconds) {
    countdownSeconds_ = seconds;
    if (countdownTimer_ == nullptr) {
        countdownTimer_ = new QTimer(this);
        connect(countdownTimer_, &QTimer::timeout,
                this, &ScreenshotWindow::onCountdownTick);
    }
    show();
    raise();
    hintLabel_->setText(QStringLiteral("%1 秒后截图…（Esc 取消）")
        .arg(countdownSeconds_));
    countdownTimer_->start(1000);
}

void ScreenshotWindow::onCountdownTick() {
    --countdownSeconds_;
    if (countdownSeconds_ <= 0) {
        countdownTimer_->stop();
        hintLabel_->setText(QStringLiteral("正在截图…"));
        // 倒计时结束后真正捕获（此时遮罩已隐藏——startCountdown 前已 hide；
        // 但 show() 又显示了。故捕获前再 hide）。
        hide();
        QApplication::processEvents();
        CaptureOptions opts;
        if (mode_ == Mode::Region && !selection_.isNull()) {
            opts.hasRegion = true;
            opts.regionX = selection_.x();
            opts.regionY = selection_.y();
            opts.regionW = selection_.width();
            opts.regionH = selection_.height();
        }
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        std::string err;
        if (!captureOutput(opts, &rgba, &w, &h, &err)) {
            QMessageBox::warning(this, QStringLiteral("截图"),
                QStringLiteral("捕获失败：%1").arg(QString::fromStdString(err)));
            close();
            return;
        }
        const QString path = defaultPath();
        if (stbi_write_png(path.toStdString().c_str(), w, h, 4, rgba.data(),
                           w * 4) == 0) {
            QMessageBox::warning(this, QStringLiteral("截图"),
                QStringLiteral("保存 PNG 失败。"));
            close();
            return;
        }
        QMessageBox::information(this, QStringLiteral("截图"),
            QStringLiteral("已保存：%1（%2×%3）").arg(path).arg(w).arg(h));
        close();
        return;
    }
    hintLabel_->setText(QStringLiteral("%1 秒后截图…（Esc 取消）")
        .arg(countdownSeconds_));
}

QString ScreenshotWindow::defaultPath() const {
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

}  // namespace w10shot

Q_DECLARE_METATYPE(w10shot::ViewInfo)

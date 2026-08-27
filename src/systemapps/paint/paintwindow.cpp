// w10paint 画图实现。
#include "systemapps/paint/paintwindow.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "theme/colors.h"

namespace w10paint {

void paintDot(QImage* img, int x, int y, const QColor& color, int penSize) {
    if (img == nullptr || img->isNull()) {
        return;
    }
    QPainter p(img);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const int d = qMax(1, penSize);
    p.drawEllipse(QPointF(x, y), d / 2.0, d / 2.0);
}

void paintLine(QImage* img, int x0, int y0, int x1, int y1,
               const QColor& color, int penSize) {
    QPainter p(img);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(color, qMax(1, penSize), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(x0, y0, x1, y1);
}

void paintRect(QImage* img, int x0, int y0, int x1, int y1,
               const QColor& color, int penSize, bool fill) {
    QPainter p(img);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRect r = QRect(QPoint(x0, y0), QPoint(x1, y1)).normalized();
    if (fill) {
        p.fillRect(r, color);
    } else {
        p.setPen(QPen(color, qMax(1, penSize)));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
    }
}

void paintEllipse(QImage* img, int x0, int y0, int x1, int y1,
                  const QColor& color, int penSize, bool fill) {
    QPainter p(img);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRect r = QRect(QPoint(x0, y0), QPoint(x1, y1)).normalized();
    if (fill) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(r);
    } else {
        p.setPen(QPen(color, qMax(1, penSize)));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r);
    }
}

// ---- 画布（自绘 QImage）----

class CanvasWidget : public QWidget {
public:
    explicit CanvasWidget(PaintWindow* owner, QWidget* parent = nullptr)
        : QWidget(parent), owner_(owner) {
        setMinimumSize(400, 300);
        setMouseTracking(true);
    }

    void setImage(QImage* img) {
        image_ = img;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xF0, 0xF0, 0xF0));
        if (image_ == nullptr || image_->isNull()) {
            p.setPen(QColor(0x88, 0x88, 0x88));
            p.drawText(rect(), Qt::AlignCenter,
                       QStringLiteral("空白画布（选择工具开始绘制）"));
            return;
        }
        p.drawImage(0, 0, *image_);
    }
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    PaintWindow* owner_;
    QImage* image_ = nullptr;
    bool drawing_ = false;
    QPoint lastPos_;
    QPoint startPos_;
};

namespace {

// 画布坐标 → 图像坐标（画布缩放 1:1，直接取 pos；越界钳制由 QPainter 内部
// 裁剪，无需额外处理）。
QPoint canvasPos(const QMouseEvent* e, const QImage* img) {
    QPoint pos = e->pos();
    if (img != nullptr) {
        pos.setX(qBound(0, pos.x(), img->width() - 1));
        pos.setY(qBound(0, pos.y(), img->height() - 1));
    }
    return pos;
}

}  // namespace

void CanvasWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && image_ != nullptr
            && !image_->isNull()) {
        drawing_ = true;
        lastPos_ = canvasPos(e, image_);
        startPos_ = lastPos_;
        if (owner_->tool() == w10paint::Tool::Pen
                || owner_->tool() == w10paint::Tool::Eraser) {
            paintDot(image_, lastPos_.x(), lastPos_.y(),
                     owner_->tool() == w10paint::Tool::Eraser
                         ? QColor(Qt::white) : owner_->color(),
                     owner_->penSize());
            update();
        }
    }
    QWidget::mousePressEvent(e);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!drawing_ || image_ == nullptr) {
        return;
    }
    const QPoint pos = canvasPos(e, image_);
    switch (owner_->tool()) {
    case w10paint::Tool::Pen:
    case w10paint::Tool::Eraser: {
        const QColor c = owner_->tool() == w10paint::Tool::Eraser
            ? QColor(Qt::white) : owner_->color();
        // 线段插值（快速移动不丢点）。
        w10paint::paintLine(image_, lastPos_.x(), lastPos_.y(),
                            pos.x(), pos.y(), c, owner_->penSize());
        lastPos_ = pos;
        update();
        break;
    }
    default:
        break;  // 形状工具：预览在释放时绘制（MVP 不做实时预览）
    }
    QWidget::mouseMoveEvent(e);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (!drawing_ || image_ == nullptr) {
        return;
    }
    drawing_ = false;
    const QPoint pos = canvasPos(e, image_);
    const bool shapeTool = owner_->tool() == w10paint::Tool::Line
        || owner_->tool() == w10paint::Tool::Rect
        || owner_->tool() == w10paint::Tool::Ellipse;
    switch (owner_->tool()) {
    case w10paint::Tool::Line:
        w10paint::paintLine(image_, startPos_.x(), startPos_.y(),
                            pos.x(), pos.y(), owner_->color(),
                            owner_->penSize());
        break;
    case w10paint::Tool::Rect:
        w10paint::paintRect(image_, startPos_.x(), startPos_.y(),
                            pos.x(), pos.y(), owner_->color(),
                            owner_->penSize(), false);
        break;
    case w10paint::Tool::Ellipse:
        w10paint::paintEllipse(image_, startPos_.x(), startPos_.y(),
                               pos.x(), pos.y(), owner_->color(),
                               owner_->penSize(), false);
        break;
    default:
        break;
    }
    update();
    // 审查（E2）：形状工具按下未移动（0 尺寸）不置脏。
    if (!(shapeTool && pos == startPos_)) {
        owner_->markChanged();
    }
    QWidget::mouseReleaseEvent(e);
}

// ---- PaintWindow ----

PaintWindow::PaintWindow(const QString& openPath, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("画图"));
    resize(720, 520);
    buildUi();

    if (!openPath.isEmpty()) {
        if (!openImage(openPath)) {
            QMessageBox::warning(this, QStringLiteral("画图"),
                QStringLiteral("无法打开：%1").arg(openPath));
            newCanvas();
        }
    } else {
        newCanvas();
    }
    refreshCanvas();
}

void PaintWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(6, 6, 6, 6);

    // 工具栏。
    auto* bar = new QHBoxLayout;
    auto* newBtn = new QPushButton(QStringLiteral("新建"), central);
    auto* openBtn = new QPushButton(QStringLiteral("打开…"), central);
    auto* saveBtn = new QPushButton(QStringLiteral("保存"), central);
    bar->addWidget(newBtn);
    bar->addWidget(openBtn);
    bar->addWidget(saveBtn);
    bar->addSpacing(12);
    toolCombo_ = new QComboBox(central);
    toolCombo_->addItem(QStringLiteral("画笔"), static_cast<int>(Tool::Pen));
    toolCombo_->addItem(QStringLiteral("橡皮"), static_cast<int>(Tool::Eraser));
    toolCombo_->addItem(QStringLiteral("直线"), static_cast<int>(Tool::Line));
    toolCombo_->addItem(QStringLiteral("矩形"), static_cast<int>(Tool::Rect));
    toolCombo_->addItem(QStringLiteral("椭圆"), static_cast<int>(Tool::Ellipse));
    bar->addWidget(toolCombo_);
    // 预置色板。
    const QList<QColor> palette = {QColor(30, 30, 30), QColor(200, 30, 30),
                                   QColor(30, 130, 60), QColor(30, 80, 180),
                                   QColor(220, 160, 30), QColor(150, 60, 160),
                                   QColor(Qt::white)};
    for (const QColor& c : palette) {
        auto* swatch = new QToolButton(central);
        swatch->setFixedSize(22, 22);
        swatch->setStyleSheet(QStringLiteral(
            "QToolButton { background: %1; border: 1px solid #999;"
            " border-radius: 2px; }").arg(c.name()));
        swatch->setCursor(Qt::PointingHandCursor);
        bar->addWidget(swatch);
        QObject::connect(swatch, &QToolButton::clicked, this,
                         [this, c] { color_ = c; });
    }
    bar->addSpacing(8);
    bar->addWidget(new QLabel(QStringLiteral("粗细"), central));
    penSpin_ = new QSpinBox(central);
    penSpin_->setRange(1, 24);
    penSpin_->setValue(penSize_);
    bar->addWidget(penSpin_);
    bar->addStretch(1);
    sizeLabel_ = new QLabel(central);
    bar->addWidget(sizeLabel_);
    root->addLayout(bar);

    canvas_ = new CanvasWidget(this, central);
    root->addWidget(canvas_, 1);
    setCentralWidget(central);

    QObject::connect(newBtn, &QPushButton::clicked, this,
                     &PaintWindow::newCanvas);
    QObject::connect(openBtn, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("打开图片"), QDir::homePath(),
            QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)"));
        if (!file.isEmpty()) {
            if (!openImage(file)) {
                QMessageBox::warning(this, QStringLiteral("画图"),
                    QStringLiteral("无法打开：%1").arg(file));
            } else {
                refreshCanvas();
            }
        }
    });
    QObject::connect(saveBtn, &QPushButton::clicked, this,
                     &PaintWindow::saveImage);
    QObject::connect(toolCombo_, &QComboBox::currentIndexChanged, this,
                     [this](int index) {
        tool_ = static_cast<Tool>(toolCombo_->itemData(index).toInt());
    });
    QObject::connect(penSpin_, &QSpinBox::valueChanged, this,
                     [this](int v) { penSize_ = v; });
}

void PaintWindow::newCanvas() {
    image_ = QImage(720, 480, QImage::Format_RGB32);
    image_.fill(Qt::white);
    currentPath_.clear();
    imageChanged_ = false;
    if (canvas_ != nullptr) {
        canvas_->setImage(&image_);
    }
    updateTitle();
    refreshCanvas();
}

bool PaintWindow::openImage(const QString& path) {
    QImage img(path);
    if (img.isNull()) {
        return false;
    }
    image_ = img.convertToFormat(QImage::Format_RGB32);
    currentPath_ = path;
    imageChanged_ = false;
    if (canvas_ != nullptr) {
        canvas_->setImage(&image_);
    }
    updateTitle();
    return true;
}

void PaintWindow::saveImage() {
    if (currentPath_.isEmpty()) {
        currentPath_ = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存图片"), QDir::homePath(),
            QStringLiteral("PNG 图片 (*.png)"));
        if (currentPath_.isEmpty()) {
            return;
        }
        // 审查（E2）：无后缀才补 .png（有后缀原样保存，QImage::save 按
        // 后缀推断格式；大小写不敏感不再重复追加）。
        const QString suffix = QFileInfo(currentPath_).suffix().toLower();
        if (suffix.isEmpty()) {
            currentPath_ += QStringLiteral(".png");
        }
    }
    if (image_.save(currentPath_)) {
        imageChanged_ = false;
        updateTitle();
        statusBar()->showMessage(
            QStringLiteral("已保存：%1").arg(currentPath_), 3000);
    } else {
        QMessageBox::warning(this, QStringLiteral("画图"),
            QStringLiteral("保存失败：%1").arg(currentPath_));
    }
}

void PaintWindow::updateTitle() {
    const QString base = currentPath_.isEmpty()
        ? QStringLiteral("画图")
        : QStringLiteral("画图 - %1")
              .arg(QFileInfo(currentPath_).fileName());
    setWindowTitle(imageChanged_ ? base + QStringLiteral(" *") : base);
}

void PaintWindow::markChanged() {
    imageChanged_ = true;
    updateTitle();
}

void PaintWindow::refreshCanvas() {
    if (canvas_ == nullptr) {
        return;
    }
    canvas_->setImage(&image_);
    if (sizeLabel_ != nullptr) {
        sizeLabel_->setText(QStringLiteral("%1 × %2")
            .arg(image_.width()).arg(image_.height()));
    }
}

}  // namespace w10paint

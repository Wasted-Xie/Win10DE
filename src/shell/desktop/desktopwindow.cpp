#include "desktop/desktopwindow.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileSystemModel>
#include <QLinearGradient>
#include <QListView>
#include <QPainter>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QUrl>

namespace w10de {

namespace {

// 桌面目录（~/.local/share 或主目录兜底）。
QString desktopDirectory() {
    const QString desktop =
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return desktop.isEmpty() ? QDir::homePath() : desktop;
}

}  // namespace

DesktopWindow::DesktopWindow(QWidget* parent) : QWidget(parent) {
    // 桌面图标（左上角浮动区域，透明背景；绝对定位覆盖在壁纸上）。
    iconList_ = new QListView(this);
    iconList_->setViewMode(QListView::IconMode);
    iconList_->setIconSize(QSize(40, 40));
    iconList_->setGridSize(QSize(90, 90));
    iconList_->setResizeMode(QListView::Adjust);
    iconList_->setMovement(QListView::Static);
    iconList_->setWordWrap(true);
    iconList_->setStyleSheet(QStringLiteral(
        "QListView {"
        "  background: transparent;"
        "  border: none;"
        "  color: white;"
        "  font-size: 11px;"
        "}"
        "QListView::item { background: transparent; padding: 2px; }"
        "QListView::item:hover { background: rgba(255,255,255,0.12); }"
        "QListView::item:selected { background: rgba(255,255,255,0.2); }"));
    iconList_->setGeometry(0, 0, 300, 200);

    // 桌面目录图标（只读浏览）。
    iconModel_ = new QFileSystemModel(this);
    iconModel_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    const QString dir = desktopDirectory();
    iconModel_->setRootPath(dir);
    iconList_->setModel(iconModel_);
    iconList_->setRootIndex(iconModel_->index(dir));

    connect(iconList_, &QListView::doubleClicked,
            this, &DesktopWindow::openItem);

    setWallpaper(QString());
}

void DesktopWindow::setWallpaper(const QString& path) {
    QImage source;
    if (!path.isEmpty()) {
        source.load(path);
    }
    if (source.isNull()) {
        source = renderDefaultWallpaper(QSize(1920, 1080));  // 默认渐变（高分辨率源）
    }
    wallpaperSource_ = source;
    updateWallpaperScaled();
}

void DesktopWindow::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x00, 0x5A, 0x9E));  // 兜底底色
    if (!wallpaperScaled_.isNull()) {
        // 原尺寸绘制（已按窗口比例缩放铺满），多余部分由窗口裁剪——
        // 不能 drawImage(rect(), ...) 二次拉伸（比例不一致时会变形）。
        painter.drawImage(rect().topLeft(), wallpaperScaled_);
    }
}

void DesktopWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateWallpaperScaled();
}

void DesktopWindow::updateWallpaperScaled() {
    if (wallpaperSource_.isNull() || size().isEmpty()) {
        return;
    }
    // 按窗口尺寸缩放（保持宽高比并铺满裁剪）。
    wallpaperScaled_ = wallpaperSource_.scaled(
        size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    update();
}

QImage DesktopWindow::renderDefaultWallpaper(const QSize& size) const {
    // Win10 风格蓝色渐变（无壁纸文件时的默认视觉）。
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, size.width(), size.height());
    gradient.setColorAt(0.0, QColor(0x00, 0x5A, 0x9E));
    gradient.setColorAt(0.6, QColor(0x00, 0x78, 0xD7));
    gradient.setColorAt(1.0, QColor(0x10, 0x36, 0x5A));
    painter.fillRect(image.rect(), gradient);
    return image;
}

void DesktopWindow::openItem(const QModelIndex& index) {
    const QString path = iconModel_->filePath(index);
    if (!path.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

}  // namespace w10de

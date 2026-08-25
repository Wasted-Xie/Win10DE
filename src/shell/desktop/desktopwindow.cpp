#include "desktop/desktopwindow.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QLinearGradient>
#include <QListView>
#include <QPainter>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "theme/colors.h"

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
    // 图标文字/高亮主题化（深色白字、浅色深字 + 深色高亮，审查 t2）。
    const QColor fg = w10de::theme::kTextPrimary();
    const QColor hv = w10de::theme::kHoverBackground();
    const QColor ps = w10de::theme::kPressedBackground();
    iconList_->setStyleSheet(QStringLiteral(
        "QListView {"
        "  background: transparent;"
        "  border: none;"
        "  color: %1;"
        "  font-size: 11px;"
        "}"
        "QListView::item { background: transparent; padding: 2px; }"
        "QListView::item:hover { background: rgba(%2,%3,%4,0.12); }"
        "QListView::item:selected { background: rgba(%5,%6,%7,0.2); }")
        .arg(fg.name())
        .arg(hv.red()).arg(hv.green()).arg(hv.blue())
        .arg(ps.red()).arg(ps.green()).arg(ps.blue()));
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
    // 停止幻灯片定时器（手动设壁纸时终止轮换）；不清 slideshowFiles_——
    // advanceSlideshow 内部调用 setWallpaper 后需保留列表并自行重启定时器
    //（实测：清空列表导致轮换只发生一次）。
    if (slideshowTimer_ != nullptr) {
        slideshowTimer_->stop();
    }

    QImage source;
    if (!path.isEmpty()) {
        source.load(path);
        if (source.isNull()) {
            // 审查 L1：损坏/格式不支持（如缺 webp 插件）时记录，便于排障。
            qWarning("wp: failed to load wallpaper '%s', falling back",
                     qPrintable(path));
        }
    }
    if (source.isNull()) {
        source = renderDefaultWallpaper(QSize(1920, 1080));  // 默认渐变（高分辨率源）
    }
    wallpaperSource_ = source;
    updateWallpaperScaled();
}

void DesktopWindow::setSlideshow(const QString& dir, int intervalSeconds) {
    if (slideshowTimer_ == nullptr) {
        slideshowTimer_ = new QTimer(this);
        slideshowTimer_->setTimerType(Qt::CoarseTimer);
        connect(slideshowTimer_, &QTimer::timeout,
                this, &DesktopWindow::advanceSlideshow);
    }
    slideshowTimer_->stop();
    slideshowFiles_.clear();
    slideshowIndex_ = 0;

    if (dir.isEmpty() || intervalSeconds <= 0) {
        setWallpaper(QString());
        return;
    }

    // 收集目录内图片（QImage 可解码的常见格式），按文件名排序轮换。
    // 审查 L2：CaseInsensitive——否则 .PNG/.JPG 大写后缀不匹配。
    const QStringList filters = {
        QStringLiteral("*.png"), QStringLiteral("*.jpg"),
        QStringLiteral("*.jpeg"), QStringLiteral("*.bmp"),
        QStringLiteral("*.webp"), QStringLiteral("*.gif"),
    };
    const QFileInfoList entries =
        QDir(dir).entryInfoList(filters,
                                QDir::Files | QDir::Readable,
                                QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fi : entries) {
        slideshowFiles_ << fi.absoluteFilePath();
    }
    if (slideshowFiles_.isEmpty()) {
        qWarning("wp: slideshow dir '%s' has no images, falling back to default",
                 qPrintable(dir));
        setWallpaper(QString());  // 无图片回退默认渐变
        return;
    }

    setWallpaper(slideshowFiles_.front());
    // 审查 M2：interval 溢出 clamp（interval*1000 超过 int 上限时
    // QTimer::start 行为未定义；配置来自外部文件，防御处理）。
    const int ms = (intervalSeconds >= 2147483)
        ? 2147483000
        : (intervalSeconds <= 0 ? 0 : intervalSeconds * 1000);
    slideshowTimer_->start(ms);
}

void DesktopWindow::advanceSlideshow() {
    if (slideshowFiles_.isEmpty()) {
        return;
    }
    // 审查 L5：窗口不可见（锁屏/无输出）时跳过轮换，避免无用加载与
    // hide/show（QTimer 非单次，超时后自动续下一周期）。
    if (!isVisible()) {
        return;
    }
    slideshowIndex_ = (slideshowIndex_ + 1) % slideshowFiles_.size();
    setWallpaper(slideshowFiles_[slideshowIndex_]);
    // LayerShellQt 下 Qt 的增量 paint 调度失效（实测 update()/repaint()/
    // requestUpdate()/强制 resize 均不触发 paintEvent 与 surface 提交，
    // 画面停留在上一张壁纸）；hide/show 强制完整 map → paint → flush →
    // commit 链路。依赖 LayerShellQt 在 surface 重建时重放 layer 配置
    // （scope/layer/anchors，审查 M1——与项目内其他 hide/show 复用机制
    // 一致；真机建议验证）。真机上每轮换闪黑一次（背景层短暂消失，
    // interval 越短越明显，审查 L6）；headless 验证通过。
    hide();
    show();
    // setWallpaper 会 stop 定时器；恢复轮换。
    if (slideshowTimer_ != nullptr && !slideshowFiles_.isEmpty()) {
        slideshowTimer_->start();
    }
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
        qDebug("wp: scaled skipped (null src or empty size)");
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

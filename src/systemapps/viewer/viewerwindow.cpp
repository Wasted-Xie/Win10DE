// viewerwindow.cpp —— 查看器主窗口实现。

#include "systemapps/viewer/viewerwindow.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextCodec>  // GB18030 探测（Qt6Core5Compat；审查 V5）
#include <QTextStream>
#include <QTimer>      // 非模态错误提示延迟（审查 V3）
#include <QToolBar>

#include <algorithm>  // std::clamp/std::min/std::max

#include <poppler-qt6.h>

#include "systemapps/viewer/filetype.h"
#include "theme/colors.h"

namespace w10de::viewer {

namespace {

// 缩放档位（审查 L4：实际为 0.25 线性步进，0.25..4.0）。
constexpr double kMinScale = 0.25;
constexpr double kMaxScale = 4.0;
// 渲染/缩放尺寸上限（审查 M2/M3：防恶意超大 PDF 页/图像爆内存）。
constexpr int kMaxRenderDim = 8192;
// 大文本保护（审查 V1）：超过该字节数只读前缀，避免全量 readAll 卡顿。
constexpr qint64 kMaxTextBytes = 50 * 1024 * 1024;

double nextScaleStep(double current, bool up) {
    if (up) {
        return std::min(kMaxScale, current + 0.25);
    }
    return std::max(kMinScale, current - 0.25);
}

}  // namespace

ViewerWindow::ViewerWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    const QColor bg = theme::kStartMenuBackground();
    const QColor fg = theme::kTextPrimary();
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background: %1; color: %2; }"
        "QToolBar { background: %3; border: none; padding: 2px; }"
        "QToolButton { color: %2; padding: 4px 8px; border-radius: 2px; }"
        "QToolButton:hover { background: %4; }"
        "QScrollArea { background: %1; }"
        "QPlainTextEdit { background: %1; color: %2;"
        "  border: none; font-family: monospace; }")
        .arg(bg.name(), fg.name(), theme::kTaskbarBackground().name(),
             theme::kHoverBackground().name()));
    resize(900, 640);
    setWindowTitle(QStringLiteral("查看器"));
    statusBar()->showMessage(QStringLiteral("打开文件开始查看"));
}

ViewerWindow::~ViewerWindow() = default;

void ViewerWindow::buildUi() {
    toolbar_ = addToolBar(QStringLiteral("工具栏"));
    toolbar_->setMovable(false);

    openAction_ = toolbar_->addAction(QStringLiteral("打开…"));
    connect(openAction_, &QAction::triggered, this, &ViewerWindow::openFileDialog);
    toolbar_->addSeparator();
    zoomOutAction_ = toolbar_->addAction(QStringLiteral("缩小"));
    zoomInAction_ = toolbar_->addAction(QStringLiteral("放大"));
    fitAction_ = toolbar_->addAction(QStringLiteral("适配窗口"));
    toolbar_->addSeparator();
    prevAction_ = toolbar_->addAction(QStringLiteral("上一页"));
    nextAction_ = toolbar_->addAction(QStringLiteral("下一页"));
    pageLabel_ = new QLabel(toolbar_);
    toolbar_->addWidget(pageLabel_);

    connect(zoomOutAction_, &QAction::triggered, this, &ViewerWindow::zoomOut);
    connect(zoomInAction_, &QAction::triggered, this, &ViewerWindow::zoomIn);
    connect(fitAction_, &QAction::triggered, this, &ViewerWindow::zoomFit);
    connect(prevAction_, &QAction::triggered, this, &ViewerWindow::prevPage);
    connect(nextAction_, &QAction::triggered, this, &ViewerWindow::nextPage);

    stack_ = new QStackedWidget(this);

    textView_ = new QPlainTextEdit(stack_);
    textView_->setReadOnly(true);
    stack_->addWidget(textView_);

    imageScroll_ = new QScrollArea(stack_);
    imageScroll_->setWidgetResizable(true);
    imageLabel_ = new QLabel(imageScroll_);
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageScroll_->setWidget(imageLabel_);
    stack_->addWidget(imageScroll_);

    pdfScroll_ = new QScrollArea(stack_);
    pdfScroll_->setWidgetResizable(true);
    pdfLabel_ = new QLabel(pdfScroll_);
    pdfLabel_->setAlignment(Qt::AlignCenter);
    pdfScroll_->setWidget(pdfLabel_);
    stack_->addWidget(pdfScroll_);

    setCentralWidget(stack_);

    // 初始禁用（无文件时）。
    zoomInAction_->setEnabled(false);
    zoomOutAction_->setEnabled(false);
    fitAction_->setEnabled(false);
    prevAction_->setEnabled(false);
    nextAction_->setEnabled(false);
}

bool ViewerWindow::loadFile(const QString& path) {
    const FileKind kind = detectFileKind(path);
    switch (kind) {
    case FileKind::Text:
        if (!loadText(path)) {
            return false;
        }
        break;
    case FileKind::Image:
        if (!loadImage(path)) {
            return false;
        }
        break;
    case FileKind::Pdf:
        if (!loadPdf(path)) {
            return false;
        }
        break;
    case FileKind::Unknown:
        setStatus(QStringLiteral("不支持的文件类型：%1").arg(path));
        // 审查 V3：D-Bus Activate 槽内不可 exec() 阻塞调用方——
        // 延迟到事件循环下一拍非模态弹出。
        QTimer::singleShot(0, this, [this, path] {
            QMessageBox::warning(this, QStringLiteral("查看器"),
                QStringLiteral("不支持的文件类型：\n%1").arg(path));
        });
        return false;
    }
    currentPath_ = path;  // 预留（后续"重新加载/最近文件"用；审查 L6）。
    setWindowTitle(QStringLiteral("%1 - 查看器")
        .arg(QFileInfo(path).fileName()));
    return true;
}

bool ViewerWindow::loadText(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("无法打开文件：%1").arg(path));
        return false;
    }
    // 审查 V1：大文本（>50MB）只读前缀，避免全量 readAll + setPlainText
    // 卡顿；状态栏提示截断。
    const qint64 size = f.size();
    const bool truncated = size > kMaxTextBytes;
    const QByteArray raw = truncated ? f.read(kMaxTextBytes) : f.readAll();
    // 编码：BOM 优先（审查 M4：UTF-32 LE/BE BOM 会误匹配 UTF-16 的
    // FF FE / FE FF 前缀，必须先判 4 字节 BOM）；无 BOM 按 UTF-8，
    // 出现替换字符时回退 GB18030（审查 V5：中文 Windows 遗留文件，
    // Qt6 经 Qt6Core5Compat 的 QTextCodec）。
    QString text;
    if (raw.startsWith("\xFF\xFE\x00\x00")) {
        // UTF-32 LE（4 字节 BOM）。
        text = QString::fromUcs4(
            reinterpret_cast<const char32_t*>(raw.constData() + 4),
            (raw.size() - 4) / 4);
    } else if (raw.startsWith("\x00\x00\xFE\xFF")) {
        // UTF-32 BE：逐 4 字节交换为 LE 后转。
        QByteArray le = raw.mid(4);
        for (int i = 0; i + 3 < le.size(); i += 4) {
            std::swap(le[i], le[i + 3]);
            std::swap(le[i + 1], le[i + 2]);
        }
        text = QString::fromUcs4(
            reinterpret_cast<const char32_t*>(le.constData()), le.size() / 4);
    } else if (raw.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(raw.constData() + 3, raw.size() - 3);
    } else if (raw.startsWith("\xFF\xFE")) {
        text = QString::fromUtf16(
            reinterpret_cast<const char16_t*>(raw.constData() + 2),
            (raw.size() - 2) / 2);
    } else if (raw.startsWith("\xFE\xFF")) {
        // UTF-16 BE：字节交换后转。
        QByteArray le = raw.mid(2);
        for (int i = 0; i + 1 < le.size(); i += 2) {
            std::swap(le[i], le[i + 1]);
        }
        text = QString::fromUtf16(
            reinterpret_cast<const char16_t*>(le.constData()), le.size() / 2);
    } else {
        text = QString::fromUtf8(raw);
        if (text.contains(QChar::ReplacementCharacter)) {
            // 非法 UTF-8：回退 GB18030（GBK 超集，中文 Windows 文件）。
            if (QTextCodec* codec = QTextCodec::codecForName("GB18030")) {
                text = codec->toUnicode(raw);
            }
        }
    }
    textView_->setPlainText(text);
    stack_->setCurrentWidget(textView_);
    zoomInAction_->setEnabled(false);
    zoomOutAction_->setEnabled(false);
    fitAction_->setEnabled(false);
    prevAction_->setEnabled(false);
    nextAction_->setEnabled(false);
    pageLabel_->clear();
    setStatus(truncated
        ? QStringLiteral("文本：%1 字符（文件过大，仅显示前 %2 MB）")
              .arg(text.size()).arg(kMaxTextBytes / (1024 * 1024))
        : QStringLiteral("文本：%1 字符").arg(text.size()));
    return true;
}

bool ViewerWindow::loadImage(const QString& path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull()) {
        setStatus(QStringLiteral("图像解码失败：%1").arg(path));
        return false;
    }
    image_ = img;
    scale_ = 1.0;
    imageLabel_->setPixmap(QPixmap::fromImage(image_));
    stack_->setCurrentWidget(imageScroll_);
    zoomInAction_->setEnabled(true);
    zoomOutAction_->setEnabled(true);
    fitAction_->setEnabled(true);
    prevAction_->setEnabled(false);
    nextAction_->setEnabled(false);
    pageLabel_->clear();
    setStatus(QStringLiteral("图像：%1 × %2 像素")
        .arg(img.width()).arg(img.height()));
    return true;
}

bool ViewerWindow::loadPdf(const QString& path) {
    // 审查 M1：先本地加载验证，成功后再替换成员——避免"正在查看 PDF 时
    // 打开坏文件"把旧文档销毁、UI 停在无文档状态。
    auto doc = Poppler::Document::load(path);
    if (doc == nullptr || doc->isLocked()) {
        setStatus(QStringLiteral("PDF 打开失败（可能已加密）：%1").arg(path));
        return false;
    }
    doc->setRenderHint(Poppler::Document::Antialiasing);
    doc->setRenderHint(Poppler::Document::TextAntialiasing);
    // 审查 L7：空页 PDF 显式拒绝（renderPdfPage 会静默停在空白）。
    if (doc->numPages() <= 0) {
        setStatus(QStringLiteral("PDF 无页面：%1").arg(path));
        return false;
    }
    pdfDoc_ = std::move(doc);
    pdfPages_ = pdfDoc_->numPages();
    pdfPage_ = 0;
    scale_ = 1.0;
    stack_->setCurrentWidget(pdfScroll_);
    zoomInAction_->setEnabled(true);
    zoomOutAction_->setEnabled(true);
    fitAction_->setEnabled(true);
    renderPdfPage();
    return true;
}

void ViewerWindow::renderPdfPage() {
    if (pdfDoc_ == nullptr || pdfPage_ < 0 || pdfPage_ >= pdfPages_) {
        return;
    }
    const std::unique_ptr<Poppler::Page> page = pdfDoc_->page(pdfPage_);
    if (page == nullptr) {
        return;
    }
    const QSizeF sizePt = page->pageSizeF();  // 单位：点（72dpi）
    const double dpi = 96.0 * scale_;
    int w = qRound(sizePt.width() * dpi / 72.0);
    int h = qRound(sizePt.height() * dpi / 72.0);
    // 审查 M2：防恶意超大 PDF 页在 4x 缩放下渲染出 GB 级位图——
    // 单边超过 kMaxRenderDim 时等比例降分辨率（保持整页可见）。
    const int maxSide = std::max(w, h);
    if (maxSide > kMaxRenderDim) {
        const double k = static_cast<double>(kMaxRenderDim) / maxSide;
        w = qRound(w * k);
        h = qRound(h * k);
    }
    pdfImage_ = page->renderToImage(dpi, dpi, 0, 0, w, h);
    if (pdfImage_.isNull()) {
        setStatus(QStringLiteral("PDF 页渲染失败"));
        return;
    }
    pdfLabel_->setPixmap(QPixmap::fromImage(pdfImage_));
    pageLabel_->setText(QStringLiteral("  第 %1 / %2 页（%3%）")
        .arg(pdfPage_ + 1).arg(pdfPages_)
        .arg(qRound(scale_ * 100)));
    updatePdfActions();
    setStatus(QStringLiteral("PDF：第 %1 / %2 页")
        .arg(pdfPage_ + 1).arg(pdfPages_));
}

void ViewerWindow::updatePdfActions() {
    prevAction_->setEnabled(pdfPage_ > 0);
    nextAction_->setEnabled(pdfPage_ + 1 < pdfPages_);
}

void ViewerWindow::applyImageZoom() {
    QSize target = image_.size() * scale_;
    // 审查 M3：超大图（如 10000×10000 全景）×4x 可达 GB 级位图——
    // clamp 单边到 kMaxRenderDim（等比例缩小，显示保真度降级可接受）。
    const int maxSide = std::max(target.width(), target.height());
    if (maxSide > kMaxRenderDim) {
        const double k = static_cast<double>(kMaxRenderDim) / maxSide;
        target = QSize(qRound(target.width() * k),
                       qRound(target.height() * k));
    }
    imageLabel_->setPixmap(QPixmap::fromImage(
        image_.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    setStatus(QStringLiteral("图像缩放：%1%").arg(qRound(scale_ * 100)));
}

void ViewerWindow::zoomIn() {
    if (stack_->currentWidget() == textView_) {
        return;
    }
    scale_ = nextScaleStep(scale_, true);
    if (stack_->currentWidget() == pdfScroll_) {
        renderPdfPage();
    } else {
        applyImageZoom();
    }
}

void ViewerWindow::zoomOut() {
    if (stack_->currentWidget() == textView_) {
        return;
    }
    scale_ = nextScaleStep(scale_, false);
    if (stack_->currentWidget() == pdfScroll_) {
        renderPdfPage();
    } else {
        applyImageZoom();
    }
}

void ViewerWindow::zoomFit() {
    if (stack_->currentWidget() == textView_) {
        return;
    }
    QScrollArea* area = (stack_->currentWidget() == pdfScroll_)
        ? pdfScroll_ : imageScroll_;
    const QSize view = area->viewport()->size() - QSize(8, 8);
    const QSize content = (stack_->currentWidget() == pdfScroll_)
        ? pdfImage_.size() : image_.size();
    if (view.isEmpty() || content.isEmpty()) {
        return;
    }
    scale_ = std::min(static_cast<double>(view.width()) / content.width(),
                      static_cast<double>(view.height()) / content.height());
    scale_ = std::clamp(scale_, kMinScale, kMaxScale);
    if (stack_->currentWidget() == pdfScroll_) {
        renderPdfPage();
    } else {
        applyImageZoom();
    }
}

void ViewerWindow::prevPage() {
    if (pdfDoc_ != nullptr && pdfPage_ > 0) {
        --pdfPage_;
        renderPdfPage();
    }
}

void ViewerWindow::nextPage() {
    if (pdfDoc_ != nullptr && pdfPage_ + 1 < pdfPages_) {
        ++pdfPage_;
        renderPdfPage();
    }
}

void ViewerWindow::openFileDialog() {
    // 审查 V2：过滤器与 filetype 白名单对齐（text/image 全扩展名）。
    const QString path = QFileDialog::getOpenFileName(this,
        QStringLiteral("打开文件"), QDir::homePath(),
        QStringLiteral("文本 (*.txt *.md *.markdown *.log *.ini *.conf *.cfg "
                       "*.json *.xml *.yaml *.yml *.toml *.csv *.tsv "
                       "*.c *.h *.cpp *.hpp *.cc *.cxx *.py *.js *.ts "
                       "*.sh *.bash *.zsh *.java *.rs *.go *.sql *.html *.htm "
                       "*.css *.license *.readme *.diff *.patch);;"
                       "PDF 文档 (*.pdf);;"
                       "图像 (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.svg "
                       "*.svgz *.xpm *.ico *.tif *.tiff);;"
                       "所有文件 (*)"));
    if (!path.isEmpty()) {
        loadFile(path);
    }
}

void ViewerWindow::setStatus(const QString& text) {
    statusBar()->showMessage(text);
}

}  // namespace w10de::viewer

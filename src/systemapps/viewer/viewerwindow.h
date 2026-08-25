// viewerwindow —— 查看器主窗口（文本/PDF/图像，KDE-GAP 中优先 #2）。
//
// 三渲染引擎：QPlainTextEdit（文本只读）、QLabel+QImageReader（图像，
// 含 svg——QtSvg 插件）、Poppler::Document（PDF，poppler-qt6，Okular
// 同款后端）。缩放仅对图像/PDF 生效；PDF 支持上一页/下一页 + 适配窗口。

#pragma once

#include <QMainWindow>

#include <memory>

class QAction;
class QImage;
class QLabel;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;
class QToolBar;

namespace Poppler {
class Document;
}

namespace w10de::viewer {

class ViewerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ViewerWindow(QWidget* parent = nullptr);
    ~ViewerWindow() override;

    // 打开文件（已存在则切换；失败返回 false 并在状态栏提示）。
    bool loadFile(const QString& path);

private slots:
    void openFileDialog();
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void prevPage();
    void nextPage();

private:
    void buildUi();
    bool loadText(const QString& path);
    bool loadImage(const QString& path);
    bool loadPdf(const QString& path);
    void renderPdfPage();
    void updatePdfActions();
    void setStatus(const QString& text);
    // 按当前 scale_ 缩放图像显示（审查 M3：目标尺寸 clamp 上限防内存爆）。
    void applyImageZoom();
    // 当前缩放（图像/PDF；文本恒 1）。
    double scale_ = 1.0;

    QToolBar* toolbar_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QPlainTextEdit* textView_ = nullptr;
    QScrollArea* imageScroll_ = nullptr;
    QLabel* imageLabel_ = nullptr;
    QScrollArea* pdfScroll_ = nullptr;
    QLabel* pdfLabel_ = nullptr;

    QAction* openAction_ = nullptr;
    QAction* zoomInAction_ = nullptr;
    QAction* zoomOutAction_ = nullptr;
    QAction* fitAction_ = nullptr;
    QAction* prevAction_ = nullptr;
    QAction* nextAction_ = nullptr;
    QLabel* pageLabel_ = nullptr;

    QString currentPath_;
    // 图像原始位图（缩放显示用）。
    QImage image_;
    // PDF 状态（QImage 缓存当前页，避免翻页重复渲染）。
    std::unique_ptr<Poppler::Document> pdfDoc_;
    QImage pdfImage_;
    int pdfPage_ = 0;
    int pdfPages_ = 0;
};

}  // namespace w10de::viewer

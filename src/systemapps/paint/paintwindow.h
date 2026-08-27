// w10paint —— 画图（Win10 画图基础版：QPainter 打开/绘制/保存 PNG）。
//
// 画布 QImage + 工具（画笔/橡皮/直线/矩形/椭圆）+ 颜色/粗细 + 新建/打开/
// 保存。绘制核心（paintDot/paintShape）为静态可测函数（selftest 断言像素）。
#pragma once

#include <QColor>
#include <QImage>
#include <QMainWindow>
#include <QString>

class QComboBox;
class QLabel;
class QSpinBox;
class QWidget;

namespace w10paint {

class CanvasWidget;  // 自绘画布（cpp 中定义）

enum class Tool { Pen, Eraser, Line, Rect, Ellipse };

// 笔刷点（圆点填充；selftest 与画布共用）。
void paintDot(QImage* img, int x, int y, const QColor& color, int penSize);
// 形状绘制（line/rect/ellipse；rect 与 ellipse 为边框绘制）。返回更新后的
// 图像（内部用 QPainter）。
void paintLine(QImage* img, int x0, int y0, int x1, int y1,
               const QColor& color, int penSize);
void paintRect(QImage* img, int x0, int y0, int x1, int y1,
               const QColor& color, int penSize, bool fill);
void paintEllipse(QImage* img, int x0, int y0, int x1, int y1,
                  const QColor& color, int penSize, bool fill);

class PaintWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit PaintWindow(const QString& openPath = QString(),
                         QWidget* parent = nullptr);
    // 画布访问器（CanvasWidget 绘制用）。
    Tool tool() const { return tool_; }
    QColor color() const { return color_; }
    int penSize() const { return penSize_; }
    // 画布完成一笔后标记（标题/保存提示）。
    void markChanged();

private:
    void buildUi();
    void newCanvas();
    bool openImage(const QString& path);
    void saveImage();
    void refreshCanvas();
    // 审查（E2）：按 imageChanged_ 统一渲染标题（保存/新建/打开后星号
    // 正确消失）。
    void updateTitle();

    w10paint::CanvasWidget* canvas_ = nullptr;
    QImage image_;
    QString currentPath_;
    bool imageChanged_ = false;

    Tool tool_ = Tool::Pen;
    QColor color_ = QColor(30, 30, 30);
    int penSize_ = 3;

    QComboBox* toolCombo_ = nullptr;
    QSpinBox* penSpin_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
};

}  // namespace w10paint

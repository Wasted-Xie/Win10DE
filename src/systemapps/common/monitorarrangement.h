// 显示器排列控件（KDE-GAP 中优先 #5：图形化排列 GUI）。
//
// 由 w10settings 与 w10control（控制面板）共享：自绘显示器矩形（按逻辑
// 尺寸比例）+ 拖拽移动。坐标映射用**基准包围盒**（setOutputs 时固定），
// 拖拽不重算——避免显示器移动导致整体缩放/偏移跳变。
#pragma once

#include <QDBusArgument>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

namespace w10de::common {

// GetOutputs 返回 a(siiiii)：(name, width, height, scalePercent, x, y)。
// 用 Qt 标准 qdbus_cast 方式解析（直接迭代 QDBusArgument 在 Qt6 有
// read-only 限制——gdb 定位 operator>> 断言崩溃）。
struct OutputInfo {
    QString name;
    int w = 0;
    int h = 0;
    int scale = 100;
    int x = 0;
    int y = 0;
};
QDBusArgument& operator<<(QDBusArgument& arg, const OutputInfo& o);
const QDBusArgument& operator>>(const QDBusArgument& arg, OutputInfo& o);

// GetModes 返回 a(ii)：(width, height)。
struct ModeInfo {
    int w = 0;
    int h = 0;
};
QDBusArgument& operator<<(QDBusArgument& arg, const ModeInfo& m);
const QDBusArgument& operator>>(const QDBusArgument& arg, ModeInfo& m);

class MonitorArrangementWidget : public QWidget {
public:
    explicit MonitorArrangementWidget(QWidget* parent = nullptr);

    // 设置输出列表（显示页刷新后调用）；重置拖拽与基准。
    // 审查 M4：GetOutputs 的 w/h 是物理分辨率、x/y 是逻辑坐标——
    // scale≠100 时混用会致矩形比例失真，此处统一换算为逻辑尺寸
    // （显示与拖拽一致；positions() 的 x/y 不受影响）。
    void setOutputs(const QList<OutputInfo>& outputs);

    // 当前各输出位置（name → (x,y)，含未拖拽的原始值）。
    QList<OutputInfo> positions() const { return outputs_; }
    bool hasChanges() const { return changed_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    // 审查 M5：窗口拉伸后重算基准映射（排列图跟随缩放/居中）。
    void resizeEvent(QResizeEvent*) override;

private:
    // 计算基准映射（包围盒 → widget 可用区，保持比例居中）。
    void rebuildBase();
    // 输出逻辑坐标 → widget 像素（用基准映射）。
    QRect mapToWidget(const OutputInfo& o) const;

    QList<OutputInfo> outputs_;
    int dragIndex_ = -1;
    int dragOffsetX_ = 0, dragOffsetY_ = 0;  // 按下点与矩形左上偏移（像素）
    bool changed_ = false;
    // 基准映射参数。
    int baseW_ = 1, baseH_ = 1;
    double baseScale_ = 1.0;
    int baseOx_ = 0, baseOy_ = 0;
};

}  // namespace w10de::common

// Q_DECLARE_METATYPE 必须在全局命名空间（qdbus_cast 自定义结构用）。
Q_DECLARE_METATYPE(w10de::common::OutputInfo)
Q_DECLARE_METATYPE(w10de::common::ModeInfo)

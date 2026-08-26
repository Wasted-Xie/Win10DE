// 显示器排列控件实现（w10settings/w10control 共享，见头文件注释）。
#include "systemapps/common/monitorarrangement.h"

#include <QDBusArgument>
#include <QMouseEvent>
#include <QPainter>
#include <QRect>

#include <algorithm>  // std::min/std::max（排列控件基准映射）

namespace w10de::common {

QDBusArgument& operator<<(QDBusArgument& arg, const OutputInfo& o) {
    arg.beginStructure();
    arg << o.name << o.w << o.h << o.scale << o.x << o.y;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, OutputInfo& o) {
    arg.beginStructure();
    arg >> o.name >> o.w >> o.h >> o.scale >> o.x >> o.y;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const ModeInfo& m) {
    arg.beginStructure();
    arg << m.w << m.h;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, ModeInfo& m) {
    arg.beginStructure();
    arg >> m.w >> m.h;
    arg.endStructure();
    return arg;
}

MonitorArrangementWidget::MonitorArrangementWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(180);
}

void MonitorArrangementWidget::resizeEvent(QResizeEvent*) {
    rebuildBase();
    update();
}

void MonitorArrangementWidget::setOutputs(const QList<OutputInfo>& outputs) {
    outputs_ = outputs;
    for (OutputInfo& o : outputs_) {
        if (o.scale > 0 && o.scale != 100) {
            o.w = qRound(o.w * 100.0 / o.scale);
            o.h = qRound(o.h * 100.0 / o.scale);
        }
    }
    dragIndex_ = -1;
    changed_ = false;  // 应用后回读刷新会重置"已编辑"标记
    rebuildBase();
    update();
}

void MonitorArrangementWidget::rebuildBase() {
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (const OutputInfo& o : outputs_) {
        minX = std::min(minX, o.x);
        minY = std::min(minY, o.y);
        maxX = std::max(maxX, o.x + o.w);
        maxY = std::max(maxY, o.y + o.h);
    }
    baseW_ = maxX - minX;
    baseH_ = maxY - minY;
    const QSize avail = size() - QSize(48, 48);
    // 审查：widget 未布局时 size() 可能为 0 → 下限防除零/负缩放。
    baseScale_ = (baseW_ > 0 && baseH_ > 0 && avail.width() > 0
                  && avail.height() > 0)
        ? std::max(0.01,
            std::min(static_cast<double>(avail.width()) / baseW_,
                     static_cast<double>(avail.height()) / baseH_))
        : 1.0;
    baseOx_ = (width() - baseW_ * baseScale_) / 2 - minX * baseScale_;
    baseOy_ = (height() - baseH_ * baseScale_) / 2 - minY * baseScale_;
}

QRect MonitorArrangementWidget::mapToWidget(const OutputInfo& o) const {
    return QRect(qRound(baseOx_ + o.x * baseScale_),
                 qRound(baseOy_ + o.y * baseScale_),
                 qRound(o.w * baseScale_), qRound(o.h * baseScale_));
}

void MonitorArrangementWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 24, 30));
    if (outputs_.isEmpty()) {
        p.setPen(QColor(150, 155, 165));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("无输出（compositor 未连接）"));
        return;
    }
    for (int i = 0; i < outputs_.size(); ++i) {
        const OutputInfo& o = outputs_.at(i);
        const QRect r = mapToWidget(o);
        const bool selected = (i == dragIndex_);
        p.setPen(QPen(selected ? QColor(0, 120, 215)
                               : QColor(90, 95, 105), 2));
        p.setBrush(QColor(45, 50, 60));
        p.drawRect(r.adjusted(0, 0, -1, -1));
        p.setPen(QColor(235, 235, 235));
        p.drawText(r.adjusted(6, 6, -6, -6),
                   Qt::AlignLeft | Qt::AlignTop,
                   QStringLiteral("%1\n%2×%3").arg(o.name).arg(o.w).arg(o.h));
    }
    p.setPen(QColor(150, 155, 165));
    p.drawText(rect().adjusted(8, 8, -8, -8),
               Qt::AlignBottom | Qt::AlignLeft,
               QStringLiteral("拖拽显示器调整排列，然后点击【应用排列】按钮。"));
}

void MonitorArrangementWidget::mousePressEvent(QMouseEvent* e) {
    for (int i = outputs_.size() - 1; i >= 0; --i) {
        if (mapToWidget(outputs_.at(i))
                .adjusted(-4, -4, 4, 4).contains(e->pos())) {
            dragIndex_ = i;
            const QRect r = mapToWidget(outputs_.at(i));
            dragOffsetX_ = e->pos().x() - r.x();
            dragOffsetY_ = e->pos().y() - r.y();
            update();
            return;
        }
    }
    dragIndex_ = -1;
}

void MonitorArrangementWidget::mouseMoveEvent(QMouseEvent* e) {
    if (dragIndex_ < 0 || dragIndex_ >= outputs_.size()) {
        return;
    }
    OutputInfo& o = outputs_[dragIndex_];
    const QRect r0 = mapToWidget(o);  // 基准映射，不随拖拽漂移
    // 像素位移 → 逻辑位移；网格对齐用 qRound（对称四舍五入，
    // C++ 整除向零截断对负值不对称）。
    const int dxPx = e->pos().x() - (r0.x() + dragOffsetX_);
    const int dyPx = e->pos().y() - (r0.y() + dragOffsetY_);
    o.x = qRound((o.x + qRound(dxPx / baseScale_)) / 10.0) * 10;
    o.y = qRound((o.y + qRound(dyPx / baseScale_)) / 10.0) * 10;
    changed_ = true;
    update();
}

void MonitorArrangementWidget::mouseReleaseEvent(QMouseEvent*) {
    if (dragIndex_ >= 0) {
        dragIndex_ = -1;
        update();
    }
}

}  // namespace w10de::common

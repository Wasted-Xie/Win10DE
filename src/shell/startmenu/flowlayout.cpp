#include "startmenu/flowlayout.h"

#include <QWidget>

namespace w10de {

FlowLayout::FlowLayout(QWidget* parent, int hSpace, int vSpace)
    : QLayout(parent), hSpace_(hSpace), vSpace_(vSpace) {
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout() {
    // 清空时删除仍由布局持有的 item。
    QLayoutItem* item = nullptr;
    while ((item = takeAt(0)) != nullptr) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem* item) {
    items_.append(item);
}

int FlowLayout::count() const {
    return items_.size();
}

QLayoutItem* FlowLayout::itemAt(int index) const {
    return items_.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index) {
    if (index < 0 || index >= items_.size()) {
        return nullptr;
    }
    return items_.takeAt(index);
}

Qt::Orientations FlowLayout::expandingDirections() const {
    return {};
}

bool FlowLayout::hasHeightForWidth() const {
    return true;
}

int FlowLayout::heightForWidth(int width) const {
    return doLayout(QRect(0, 0, width, 0), true);
}

QSize FlowLayout::sizeHint() const {
    return minimumSize();
}

QSize FlowLayout::minimumSize() const {
    QSize size;
    for (const QLayoutItem* item : items_) {
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins m = contentsMargins();
    size += QSize(m.left() + m.right(), m.top() + m.bottom());
    return size;
}

void FlowLayout::setGeometry(const QRect& rect) {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const {
    const QMargins m = contentsMargins();
    // 布局宽度以父 widget 实际宽度为准：layer-shell/QScrollArea 场景下
    // 传入的 rect 宽度在显示时序中不稳定（实测 100→288 反复），按 rect
    // 排布会导致磁贴错误换行（每行 1 个）。
    const int availWidth =
        parentWidget() != nullptr ? parentWidget()->width() : rect.width();
    int left = rect.left() + m.left();
    int top = rect.top() + m.top();
    int right = availWidth + rect.left() - m.right() - 1;
    int x = left;
    int y = top;
    int lineHeight = 0;

    for (QLayoutItem* item : items_) {
        const QSize hint = item->sizeHint();
        const int w = qMax(hint.width(), item->minimumSize().width());
        const int h = qMax(hint.height(), item->minimumSize().height());

        // 超宽则换行（防止单个大磁贴死循环：item 比整行还宽时单独一行）。
        if (w > right - left) {
            item->setGeometry(QRect(left, y, right - left, h));
            y += h + vSpace_;
            continue;
        }

        if (x + w > right + 1) {
            x = left;
            y += lineHeight + vSpace_;
            lineHeight = 0;
        }

        if (!testOnly) {
            item->setGeometry(QRect(QPoint(x, y), QSize(w, h)));
        }
        x += w + hSpace_;
        lineHeight = qMax(lineHeight, h);
    }
    return y + lineHeight - rect.top() + m.bottom();
}

}  // namespace w10de

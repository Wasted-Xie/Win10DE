// 流式布局（Win10 磁贴流）：子项按行排布，放不下自动换行。
// 自实现（未复制 Qt 示例代码）：按宽度换行，支持 heightForWidth。
#pragma once

#include <QLayout>
#include <QList>

class QLayoutItem;

namespace w10de {

class FlowLayout : public QLayout {
    Q_OBJECT
public:
    explicit FlowLayout(QWidget* parent = nullptr,
                        int hSpace = 8, int vSpace = 8);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    void setGeometry(const QRect& rect) override;

private:
    // 计算逐行几何；testOnly 时仅返回所需高度不应用几何。
    int doLayout(const QRect& rect, bool testOnly) const;

    QList<QLayoutItem*> items_;
    int hSpace_ = 8;
    int vSpace_ = 8;
};

}  // namespace w10de

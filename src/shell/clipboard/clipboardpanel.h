// 剪贴板历史面板（Win10 Win+V 浮窗）：overlay 右缘、任务栏上方。
//
// - 列表显示历史条目：文本（单行预览截断）/图片（缩略图）。
// - 点击条目 → entryPicked（写回系统剪贴板并隐藏，由 main 接线）。
// - Esc 关闭；空历史显示占位文案。
// 渲染方式与通知弹窗一致：paintEvent 直接绘制背景（layer-shell 窗口
// 无系统背景）。
#pragma once

#include <QListWidget>
#include <QWidget>

namespace w10de {

struct ClipboardEntry;

class ClipboardPanel : public QWidget {
    Q_OBJECT
public:
    explicit ClipboardPanel(QWidget* parent = nullptr);

    // 刷新内容（保留滚动位置尽量不变：先记录当前项再重建）。
    void setHistory(const QList<ClipboardEntry>& entries);

    // 显示：刷新 + show + raise（Win+V 语义：打开即最新内容）。
    void showPanel();

signals:
    // 用户选中一条历史 → 写回剪贴板（main 接线）。
    void entryPicked(const ClipboardEntry& entry);

protected:
    void paintEvent(QPaintEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QListWidget* list_ = nullptr;
};

}  // namespace w10de

// w10sticky —— 便笺（Win10 Sticky Notes 风格：置顶便签）。
//
// 便笺 = 无边框半透明置顶小窗 + QTextEdit 全窗编辑；文本变化防抖保存到
// ~/.config/w10de/sticky/note-<时间戳>.txt（每张一文件）。
// 启动：w10sticky（新建一张）/ w10sticky <文件>（打开指定）/ w10sticky --list
// （便笺列表：新建/打开/删除）。
#pragma once

#include <QMainWindow>

class QObject;
class QEvent;
class QMouseEvent;
class QTextEdit;

namespace w10sticky {

// 便笺存储路径（~/.config/w10de/sticky/）。
QString stickyDir();
// 新建便笺文件路径（note-yyyyMMddHHmmsszzz.txt；已存在则加序号）。
QString newNotePath();
// 便笺文件列表（按修改时间降序）。
QStringList listNotes();

class StickyWindow : public QMainWindow {
    Q_OBJECT
public:
    // path 为空 = 新建便笺（新文件）。
    explicit StickyWindow(const QString& path, QWidget* parent = nullptr);
    ~StickyWindow() override;

protected:
    // 审查 S1（E1）：QTextEdit 全窗时吞掉鼠标事件，无边框窗口无法拖动——
    // 事件过滤器拦截顶部 24px 拖动条区域。
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* e) override;

private:
    void saveNow();

    QString path_;
    QTextEdit* edit_ = nullptr;
    QPoint dragOffset_;
    bool dragging_ = false;
    bool dirty_ = false;
};

}  // namespace w10sticky

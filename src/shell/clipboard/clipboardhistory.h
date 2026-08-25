// 剪贴板历史（Win10 Win+V 语义）：监听系统剪贴板，记录最近复制内容。
//
// 设计要点：
// - 监听 QApplication::clipboard()->dataChanged()（Wayland 下经
//   wl_data_device 的 set_selection 通知；compositor 需创建
//   wlr_data_device_manager——见 server.cpp）。
// - 条目：文本（text/plain）或图片（image/*）；与最近一条相同则去重
//   （连续复制同内容不产生重复条目，Win10 语义）。
// - 上限 kMaxEntries 条，超出丢弃最旧（Win10 默认上限 25，取 20）。
// - 提供 addText/addImage 供内部（面板写回剪贴板时触发 dataChanged
//   再去重跳过）与测试种子复用。
#pragma once

#include <QImage>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

namespace w10de {

struct ClipboardEntry {
    bool isImage = false;
    QString text;     // 文本条目内容（isImage=false 时有效）
    QImage image;     // 图片条目（isImage=true 时有效）
    qint64 timeMs = 0;  // 记录时刻（毫秒时间戳）
};

class ClipboardHistory : public QObject {
    Q_OBJECT
public:
    explicit ClipboardHistory(QObject* parent = nullptr);

    // 直接追加（去重 + 裁剪）；供测试种子与写回路径复用。
    void addText(const QString& text);
    void addImage(const QImage& image);

    const QList<ClipboardEntry>& entries() const { return entries_; }
    bool isEmpty() const { return entries_.isEmpty(); }
    int count() const { return entries_.size(); }
    int maxEntries() const { return kMaxEntries; }

signals:
    // 历史内容变化（新条目/裁剪/清空）。
    void historyChanged();

private:
    void onClipboardChanged();
    void pushEntry(ClipboardEntry e);

    QList<ClipboardEntry> entries_;
    static constexpr int kMaxEntries = 20;
};

}  // namespace w10de

// 条目放入 QVariant（面板 QListWidgetItem::setData 用）需注册 metatype；
// 宏必须在全局命名空间（其展开为 QMetaTypeId 模板特化）。
Q_DECLARE_METATYPE(w10de::ClipboardEntry)

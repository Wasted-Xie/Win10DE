#include "shell/clipboard/clipboardhistory.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QMimeData>
#include <QVariant>

namespace w10de {

ClipboardHistory::ClipboardHistory(QObject* parent) : QObject(parent) {
    // 系统剪贴板变化 → 记录历史。QClipboard::dataChanged 在本进程
    // setText/setImage 时同样触发（写回路径由去重吸收）。
    connect(QApplication::clipboard(), &QClipboard::dataChanged,
            this, &ClipboardHistory::onClipboardChanged);
}

void ClipboardHistory::addText(const QString& text) {
    if (text.isEmpty()) {
        return;
    }
    ClipboardEntry e;
    e.isImage = false;
    e.text = text;
    e.timeMs = QDateTime::currentMSecsSinceEpoch();
    pushEntry(std::move(e));
}

void ClipboardHistory::addImage(const QImage& image) {
    if (image.isNull()) {
        return;
    }
    ClipboardEntry e;
    e.isImage = true;
    e.image = image;
    e.timeMs = QDateTime::currentMSecsSinceEpoch();
    pushEntry(std::move(e));
}

void ClipboardHistory::onClipboardChanged() {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (mime == nullptr) {
        return;
    }
    // 文本优先（text/plain 是最常见复制形态）；否则图片。
    const QString text = mime->text();
    if (!text.isEmpty()) {
        addText(text);
        return;
    }
    if (mime->hasImage()) {
        addImage(qvariant_cast<QImage>(mime->imageData()));
    }
}

void ClipboardHistory::pushEntry(ClipboardEntry e) {
    // 去重：全表查找相同内容——命中则移到顶部并更新时间戳（写回历史
    // 条目、复制同内容均不产生重复；Win10 语义）。只与最近一条比较会
    // 漏掉"写回非最近条目"场景（审查 M1）。
    for (int i = 0; i < entries_.size(); ++i) {
        const ClipboardEntry& other = entries_.at(i);
        const bool same = other.isImage == e.isImage &&
            (e.isImage ? other.image == e.image : other.text == e.text);
        if (same) {
            entries_.removeAt(i);
            entries_.prepend(std::move(e));
            emit historyChanged();
            return;
        }
    }
    entries_.prepend(std::move(e));
    // 裁剪到上限（丢弃最旧；一次只可能超 1 条）。
    if (entries_.size() > kMaxEntries) {
        entries_.removeLast();
    }
    emit historyChanged();
}

}  // namespace w10de

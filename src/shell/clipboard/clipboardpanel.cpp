#include "shell/clipboard/clipboardpanel.h"

#include "shell/clipboard/clipboardhistory.h"
#include "theme/colors.h"

#include <QFrame>
#include <QIcon>
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QVBoxLayout>
#include <QVariant>

namespace w10de {

namespace {
constexpr int kPanelWidth = 360;
constexpr int kPanelMaxHeight = 480;
constexpr int kRowHeight = 40;
}  // namespace

ClipboardPanel::ClipboardPanel(QWidget* parent) : QWidget(parent) {
    setFixedWidth(kPanelWidth);
    setMinimumHeight(56);
    list_ = new QListWidget(this);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setUniformItemSizes(true);
    list_->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; color: %1; font-size: 12px;"
        "  border: none; }"
        "QListWidget::item { padding: 4px 8px; }"
        "QListWidget::item:selected { background: %2; color: %1; }")
        .arg(theme::kTextPrimary().name(), theme::kHoverBackground().name()));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(list_);

    // 单击与 Enter/双击统一处理：取条目 → 写回 + 关闭（L7：避免双连接
    // 重复代码；双击时 itemClicked+itemActivated 各发一次 entryPicked，
    // 写回幂等无害）。
    auto activateItem = [this](QListWidgetItem* item) {
        const QVariant v = item->data(Qt::UserRole);
        if (v.canConvert<ClipboardEntry>()) {
            emit entryPicked(v.value<ClipboardEntry>());
        }
        hide();
    };
    connect(list_, &QListWidget::itemClicked, this, activateItem);
    connect(list_, &QListWidget::itemActivated, this, activateItem);
}

void ClipboardPanel::setHistory(const QList<ClipboardEntry>& entries) {
    // 尽量恢复选择（面板显示期间历史变化时；用行号而非文本，图片条目
    // 文本相同会错位——L4）。
    const int prevRow = list_->currentRow();
    list_->clear();
    if (entries.isEmpty()) {
        auto* placeholder = new QListWidgetItem(QStringLiteral("（暂无剪贴板历史）"));
        placeholder->setFlags(placeholder->flags() & ~Qt::ItemIsEnabled);
        placeholder->setForeground(theme::kTextSecondary());
        list_->addItem(placeholder);
    } else {
        for (const ClipboardEntry& e : entries) {
            QListWidgetItem* item = nullptr;
            if (e.isImage) {
                const QPixmap thumb = QPixmap::fromImage(e.image)
                    .scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                item = new QListWidgetItem(QIcon(thumb), QStringLiteral("（图片）"));
            } else {
                QString preview = e.text;
                preview.replace(QLatin1Char('\n'), QLatin1Char(' '));
                if (preview.size() > 40) {
                    preview = preview.left(40) + QStringLiteral("…");
                }
                item = new QListWidgetItem(preview);
            }
            item->setData(Qt::UserRole, QVariant::fromValue(e));
            item->setToolTip(e.isImage ? QStringLiteral("图片剪贴板条目")
                                       : e.text);
            list_->addItem(item);
        }
    }
    if (prevRow >= 0 && prevRow < list_->count()) {
        list_->setCurrentRow(prevRow);
    }
    // 高度自适应条目数（上限 kPanelMaxHeight）。
    const int rows = qMax(1, list_->count());
    const int h = qMin(kPanelMaxHeight, 56 + rows * kRowHeight);
    setFixedHeight(h);
}

void ClipboardPanel::showPanel() {
    show();
    raise();
    list_->setFocus();
    // 默认选中最近一条（Enter 直接粘贴）；空态占位符不可选（L6）。
    if (list_->count() > 0 && (list_->item(0)->flags() & Qt::ItemIsEnabled)) {
        list_->setCurrentRow(0);
    }
}

void ClipboardPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme::kStartMenuBackground());
    p.setPen(theme::kHoverBackground());
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

void ClipboardPanel::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        hide();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

}  // namespace w10de

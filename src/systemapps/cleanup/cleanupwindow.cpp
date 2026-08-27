// CleanupWindow 实现（可选拓展 E7 磁盘清理）。
//
// 深色风格（与其他系统应用一致）：列表项 = 复选框 + 名称 + 右对齐大小；
// 勾选变化实时更新"预计释放"合计；清理前确认对话框。

#include "systemapps/cleanup/cleanupwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace w10de::cleanup {

namespace {
constexpr int kItemMargin = 10;
}  // namespace

CleanupWindow::CleanupWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("磁盘清理"));
    setFixedSize(520, 520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 18);
    root->setSpacing(8);

    titleLabel_ = new QLabel(QStringLiteral("磁盘清理"), this);
    titleLabel_->setStyleSheet(QStringLiteral(
        "color:#FFFFFF; font-size:20px; font-weight:bold;"));
    root->addWidget(titleLabel_);

    auto* sub = new QLabel(
        QStringLiteral("选择要清理的项目（仅删除缓存与回收站内容，不触碰用户文件）"),
        this);
    sub->setWordWrap(true);
    sub->setStyleSheet(QStringLiteral("color:#9AA0A6; font-size:13px;"));
    root->addWidget(sub);

    // 类别列表。
    list_ = new QListWidget(this);
    list_->setStyleSheet(QStringLiteral(
        "QListWidget{background:#262626;color:#E0E0E0;border:1px solid #3A3A3A;"
        "border-radius:6px;font-size:14px;}"
        "QListWidget::item{padding:%1px;}"
        "QListWidget::item:selected{background:#3D6FB4;}")
            .arg(kItemMargin));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(list_, &QListWidget::itemChanged,
            this, &CleanupWindow::onItemChanged);
    connect(list_, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem* cur, QListWidgetItem*) {
        if (cur == nullptr) return;
        const int row = list_->row(cur);
        if (row >= 0 && row < items_.size()) {
            detailLabel_->setText(items_[row].detail);
            detailLabel_->setVisible(true);
        }
    });
    root->addWidget(list_, 1);

    detailLabel_ = new QLabel(this);
    detailLabel_->setWordWrap(true);
    detailLabel_->setStyleSheet(
        QStringLiteral("color:#9AA0A6; font-size:12px;"));
    detailLabel_->hide();
    root->addWidget(detailLabel_);

    // 状态/失败提示（审查 L4：独立于详情 label，避免互相覆盖）。
    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#9AA0A6; font-size:12px;"));
    statusLabel_->hide();
    root->addWidget(statusLabel_);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setStyleSheet(
        QStringLiteral("color:#FFFFFF; font-size:14px;"));
    root->addWidget(summaryLabel_);

    // 底部按钮。
    cleanButton_ = new QPushButton(QStringLiteral("清理所选项目"), this);
    auto* cancelButton = new QPushButton(QStringLiteral("取消"), this);
    auto* rescanButton = new QPushButton(QStringLiteral("重新扫描"), this);
    for (QPushButton* b : {cleanButton_, cancelButton, rescanButton}) {
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(QStringLiteral(
            "QPushButton{background:#3D3D3D;color:#E0E0E0;border:none;"
            "border-radius:4px;padding:8px 16px;}"
            "QPushButton:hover{background:#4A4A4A;}"
            "QPushButton:disabled{color:#777;}"));
    }
    cleanButton_->setStyleSheet(QStringLiteral(
        "QPushButton{background:#C42B1C;color:#FFFFFF;border:none;"
        "border-radius:4px;padding:8px 16px;}"
        "QPushButton:hover{background:#D0382A;}"
        "QPushButton:disabled{background:#5A3A36;color:#999;}"));
    connect(cleanButton_, &QPushButton::clicked,
            this, &CleanupWindow::onClean);
    connect(cancelButton, &QPushButton::clicked, this, &QWidget::close);
    connect(rescanButton, &QPushButton::clicked,
            this, &CleanupWindow::onRescan);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(rescanButton);
    btnRow->addStretch();
    btnRow->addWidget(cancelButton);
    btnRow->addWidget(cleanButton_);
    root->addLayout(btnRow);

    setStyleSheet(QStringLiteral("QWidget{background:#2D2D2D;}"));
    rescan();
}

int CleanupWindow::itemCount() const {
    return list_->count();
}

qint64 CleanupWindow::selectedBytes() const {
    qint64 total = 0;
    for (int i = 0; i < list_->count(); ++i) {
        const auto* item = list_->item(i);
        if (item->checkState() == Qt::Checked && i < items_.size()) {
            total += items_[i].sizeBytes;
        }
    }
    return total;
}

void CleanupWindow::onRescan() {
    rescan();
}

void CleanupWindow::onClean() {
    QList<int> rows;
    qint64 total = 0;
    for (int i = 0; i < list_->count(); ++i) {
        // 审查 L3：与 selectedBytes 一致的越界防御。
        if (list_->item(i)->checkState() == Qt::Checked
                && i < items_.size()) {
            rows.append(i);
            total += items_[i].sizeBytes;
        }
    }
    if (rows.isEmpty()) {
        setStatus(QStringLiteral("请先勾选要清理的项目"), false);
        return;
    }
    const auto ret = QMessageBox::question(
        this, QStringLiteral("确认清理"),
        QStringLiteral("将删除所选项目，预计释放 %1。\n"
                       "缓存清理后应用可能首次启动变慢；回收站项目将永久删除。\n"
                       "继续吗？")
            .arg(formatSize(total)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    // 审查 L8：清理期间禁用按钮 + 忙碌光标（同步删除大缓存时窗口冻结）。
    cleanButton_->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    qint64 freed = 0;
    bool allOk = true;
    QString failMsg;
    for (int row : rows) {
        const qint64 r = scanner_.clean(items_[row]);
        if (r < 0) {
            allOk = false;
            failMsg = QStringLiteral("清理失败：%1").arg(scanner_.lastError());
        } else {
            freed += r;
        }
    }
    QApplication::restoreOverrideCursor();
    rescan();
    // 审查 S1：失败反馈不能被 rescan 的清空吞掉——弹窗兜底 + 状态保留。
    if (!allOk) {
        setStatus(failMsg, false);
        QMessageBox::warning(this, QStringLiteral("清理未完成"),
                             failMsg + QStringLiteral("\n已释放 %1。")
                                          .arg(formatSize(freed)));
    } else {
        setStatus(QStringLiteral("清理完成，已释放 %1。")
                      .arg(formatSize(freed)), true);
        QMessageBox::information(
            this, QStringLiteral("清理完成"),
            QStringLiteral("已释放 %1 空间。").arg(formatSize(freed)));
    }
}

void CleanupWindow::onItemChanged() {
    updateSummary();
    // 审查 L9：usercache ⊇ thumbnails——勾选用户缓存时缩略图不可再勾
    //（避免"预计释放"虚高；缩略图内容随用户缓存一并清理）。
    const bool cacheChecked = [this] {
        for (int i = 0; i < list_->count() && i < items_.size(); ++i) {
            if (items_[i].id == QStringLiteral("usercache")) {
                return list_->item(i)->checkState() == Qt::Checked;
            }
        }
        return false;
    }();
    for (int i = 0; i < list_->count() && i < items_.size(); ++i) {
        if (items_[i].id != QStringLiteral("thumbnails")) continue;
        auto* item = list_->item(i);
        if (cacheChecked) {
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            if (item->checkState() == Qt::Checked) {
                item->setCheckState(Qt::Unchecked);
            }
        } else if (items_[i].exists && items_[i].sizeBytes > 0) {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        }
    }
    updateSummary();
}

void CleanupWindow::rescan() {
    items_ = scanner_.scan();
    list_->clear();
    for (int i = 0; i < items_.size(); ++i) {
        const CleanupItem& it = items_[i];
        auto* item = new QListWidgetItem(list_);
        item->setText(QStringLiteral("  %1        %2")
                          .arg(it.label, formatSize(it.sizeBytes)));
        item->setData(Qt::UserRole, i);
        if (it.exists && it.sizeBytes > 0) {
            item->setCheckState(it.id == QStringLiteral("trash")
                                    ? Qt::Checked : Qt::Unchecked);
        } else {
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            item->setText(QStringLiteral("  %1        %2（%3）")
                              .arg(it.label, QStringLiteral("0 B"),
                                   it.cleanable ? QStringLiteral("无内容")
                                                : QStringLiteral("系统管理")));
        }
        if (!it.cleanable) {
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        }
        item->setToolTip(it.detail);
    }
    if (list_->count() > 0) {
        list_->setCurrentRow(0);
    }
    updateSummary();
    setStatus(QString(), true);
}

void CleanupWindow::updateSummary() {
    const qint64 total = selectedBytes();
    summaryLabel_->setText(total > 0
        ? QStringLiteral("预计释放空间：%1").arg(formatSize(total))
        : QStringLiteral("未选择任何项目"));
    cleanButton_->setEnabled(total > 0);
}

void CleanupWindow::setStatus(const QString& text, bool ok) {
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(ok
        ? QStringLiteral("color:#9AA0A6; font-size:12px;")
        : QStringLiteral("color:#E57373; font-size:12px;"));
    statusLabel_->setVisible(!text.isEmpty());
}

}  // namespace w10de::cleanup

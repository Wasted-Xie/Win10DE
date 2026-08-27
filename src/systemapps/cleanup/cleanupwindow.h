// CleanupWindow —— 磁盘清理主窗口（可选拓展 E7，Win10 磁盘清理风格）。
//
// 布局：标题 + 说明 → 类别列表（复选框 + 名称 + 大小）→ 选中项详情 →
// 底部（预计释放 + 清理所选项目/取消）。清理前确认，执行后刷新。

#pragma once

#include <QWidget>

#include "systemapps/cleanup/cleanupscanner.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace w10de::cleanup {

class CleanupWindow : public QWidget {
    Q_OBJECT
public:
    explicit CleanupWindow(QWidget* parent = nullptr);

    // 供验证：勾选合计（字节）。
    qint64 selectedBytes() const;
    // 供验证：列表行数。
    int itemCount() const;

private slots:
    void onRescan();
    void onClean();
    void onItemChanged();

private:
    void rescan();
    void updateSummary();
    void setStatus(const QString& text, bool ok);

    CleanupScanner scanner_;
    QList<CleanupItem> items_;
    QLabel* titleLabel_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* detailLabel_ = nullptr;  // 选中项详情（审查 L4：与状态分离）
    QLabel* statusLabel_ = nullptr;  // 状态/失败提示
    QLabel* summaryLabel_ = nullptr;
    QPushButton* cleanButton_ = nullptr;
};

}  // namespace w10de::cleanup

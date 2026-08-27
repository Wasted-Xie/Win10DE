// OskWindow —— 屏幕键盘主窗口（可选拓展 E8，Win10 屏幕键盘风格）。
//
// 无边框置顶；键网格由 layout() 数据驱动；点击字符键发送 press+release，
// 修饰键（Shift/Ctrl/Alt）按下保持、再按释放；Shift 切换大写层。

#pragma once

#include <QSet>
#include <QWidget>

#include "systemapps/osk/osk.h"

class QPushButton;
class QLabel;

namespace w10de::osk {

class OskWindow : public QWidget {
    Q_OBJECT
public:
    explicit OskWindow(QWidget* parent = nullptr);

    // 供验证：当前键网格行数。
    int rowCount() const;
    // 供验证：Shift 是否按住。
    bool shiftPressed() const { return shiftPressed_; }

private:
    void onKeyClicked(const KeyDef& def);
    void updateShiftLayers();
    void setStatus(const QString& text, bool ok);

    QList<QList<KeyDef>> rows_;
    QList<QList<QPushButton*>> rowButtons_;  // 每行按钮（与 rows_ 行列对齐）
    QLabel* statusLabel_ = nullptr;
    bool shiftPressed_ = false;
    bool capsLocked_ = false;
    QSet<uint32_t> heldMods_;  // 当前按住的修饰键 keysym（Shift/Ctrl/Alt）
};

}  // namespace w10de::osk

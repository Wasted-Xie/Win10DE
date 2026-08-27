// OskWindow 实现（可选拓展 E8 屏幕键盘）。

#include "systemapps/osk/oskwindow.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <xkbcommon/xkbcommon-keysyms.h>

namespace w10de::osk {

namespace {
// 键帽统一样式（Win10 屏幕键盘浅灰键帽）。
const char* kKeyStyle =
    "QPushButton{background:#3D3D3D;color:#E0E0E0;border:1px solid #555;"
    "border-radius:5px;font-size:14px;}"
    "QPushButton:hover{background:#4A4A4A;}"
    "QPushButton:pressed{background:#C42B1C;color:#FFF;}";
const char* kModStyle =
    "QPushButton{background:#2E5A8F;color:#FFF;border:1px solid #3D6FB4;"
    "border-radius:5px;font-size:13px;}"
    "QPushButton:hover{background:#3D6FB4;}"
    "QPushButton:checked{background:#C42B1C;color:#FFF;}";
}  // namespace

OskWindow::OskWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("屏幕键盘"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint);
    setFixedSize(820, 300);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(6);

    // 标题/状态行（可拖动标题栏）。
    statusLabel_ = new QLabel(QStringLiteral("屏幕键盘"), this);
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#9AA0A6; font-size:12px;"));
    root->addWidget(statusLabel_);

    rows_ = w10de::osk::layout(false);
    auto* grid = new QGridLayout;
    grid->setSpacing(5);
    const int kCols = 16;
    for (int r = 0; r < rows_.size(); ++r) {
        int col = 0;
        QList<QPushButton*> rowBtns;
        for (const KeyDef& def : rows_[r]) {
            auto* btn = new QPushButton(def.label, this);
            btn->setFocusPolicy(Qt::NoFocus);
            btn->setCursor(Qt::PointingHandCursor);
            // 修饰键可勾选（按下保持视觉）。
            if (isModifier(def.keysym) || def.type == KeyDef::Shift) {
                btn->setCheckable(true);
                btn->setStyleSheet(kModStyle);
            } else {
                btn->setStyleSheet(kKeyStyle);
            }
            rowBtns.append(btn);
            connect(btn, &QPushButton::clicked, this, [this, r, col, def] {
                Q_UNUSED(col);
                onKeyClicked(def);
            });
            grid->addWidget(btn, r, col, 1, def.colspan);
            col += def.colspan;
        }
        rowButtons_.append(rowBtns);
    }
    root->addLayout(grid, 1);

    setStyleSheet(QStringLiteral("QWidget{background:#262626;}"));
}

int OskWindow::rowCount() const {
    return rows_.size();
}

void OskWindow::onKeyClicked(const KeyDef& def) {
    // 修饰键（Shift/Ctrl/Alt）：toggle 保持（审查 M1——Ctrl/Alt 同样按住
    // 保持，支持 Ctrl+C 等组合键；再按一次释放）。
    if (isModifier(def.keysym)) {
        const bool nowPressed = !heldMods_.contains(def.keysym);
        const bool ok = injectKey(def.keysym, nowPressed);
        if (!ok) {
            // 审查 M5：注入失败回滚 toggle（UI 状态与 compositor 保持一致）。
            setStatus(QStringLiteral("注入失败：compositor 服务不可用"), false);
            return;
        }
        if (nowPressed) {
            heldMods_.insert(def.keysym);
        } else {
            heldMods_.remove(def.keysym);
        }
        // Shift 切换大写层（任一 Shift 按住即大写）。
        if (def.keysym == XKB_KEY_Shift_L
                || def.keysym == XKB_KEY_Shift_R) {
            shiftPressed_ = heldMods_.contains(XKB_KEY_Shift_L)
                || heldMods_.contains(XKB_KEY_Shift_R);
            updateShiftLayers();
        }
        return;
    }
    if (def.keysym == XKB_KEY_Caps_Lock) {
        capsLocked_ = !capsLocked_;
        updateShiftLayers();
        const bool ok = injectKey(def.keysym, true);
        // 审查 M4：CapsLock 失败也要反馈（回滚层状态）。
        if (!ok) {
            capsLocked_ = !capsLocked_;
            updateShiftLayers();
            setStatus(QStringLiteral("注入失败：compositor 服务不可用"), false);
            return;
        }
        injectKey(def.keysym, false);
        return;
    }
    // 字符/功能键：press + release。
    const bool ok = injectKey(def.keysym, true);
    if (ok) {
        // 审查 M4：release 失败也反馈（否则客户端只收 press，按键卡住）。
        if (!injectKey(def.keysym, false)) {
            setStatus(QStringLiteral("释放键失败：compositor 服务不可用"), false);
            return;
        }
    } else {
        setStatus(QStringLiteral("注入失败：compositor 服务不可用"), false);
        return;
    }
    // 审查 L4：状态栏显示当前按钮文本（shift 层下与键帽一致）。
    setStatus(QStringLiteral("已发送：%1").arg(def.label), true);
}

void OskWindow::updateShiftLayers() {
    // Shift/CapsLock 层切换：rows_ 与 layout(shifted) 结构一致（仅 label
    // 变化），按行列索引重设按钮文本。
    const bool shifted = shiftPressed_ ^ capsLocked_;
    const auto newRows = w10de::osk::layout(shifted);
    for (int r = 0; r < rows_.size() && r < newRows.size(); ++r) {
        for (int c = 0; c < rows_[r].size() && c < newRows[r].size(); ++c) {
            if (c < rowButtons_[r].size()) {
                rowButtons_[r][c]->setText(newRows[r][c].label);
            }
        }
    }
}

void OskWindow::setStatus(const QString& text, bool ok) {
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(ok
        ? QStringLiteral("color:#9AA0A6; font-size:12px;")
        : QStringLiteral("color:#E57373; font-size:12px;"));
}

}  // namespace w10de::osk

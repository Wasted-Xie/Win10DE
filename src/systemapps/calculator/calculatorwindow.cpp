// calculatorwindow.cpp —— 计算器 UI 与逻辑实现。

#include "systemapps/calculator/calculatorwindow.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <QKeyEvent>

#include <cctype>
#include <cmath>

namespace w10de::calc {

namespace {
constexpr double kEpsilon = 1e-12;  // 仅用于浮点比较；除零判定用真零（审查 M1）

// 显示格式化：整数不带小数；浮点最多 12 位有效数字，去尾 0。
QString formatNumber(double v) {
    if (std::isnan(v) || std::isinf(v)) {
        return QStringLiteral("无法计算");
    }
    if (std::fabs(v) < kEpsilon) {
        return QStringLiteral("0");
    }
    QString s = QString::number(v, 'g', 12);
    if (s.contains('e')) {
        // 科学计数法保持原样（大数/小数）。
        return s;
    }
    if (s.contains('.')) {
        while (s.endsWith('0')) {
            s.chop(1);
        }
        if (s.endsWith('.')) {
            s.chop(1);
        }
    }
    return s;
}
}  // namespace

CalculatorWindow::CalculatorWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("计算器"));
    setFixedSize(320, 420);

    auto* central = new QWidget(this);
    central->setStyleSheet("background:#1E1E1E;");
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    display_ = new QLabel(QStringLiteral("0"), central);
    display_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    display_->setStyleSheet(
        "color:#FFFFFF; font-size:42px; font-weight:600; "
        "padding:8px 6px; background:transparent;");
    display_->setMinimumHeight(64);
    root->addWidget(display_);

    auto* grid = new QGridLayout;
    grid->setSpacing(6);

    // 功能键行：C CE % ÷
    const char* kFuncStyle =
        "QPushButton { background:#323232; color:#FFFFFF; font-size:18px; "
        "border:none; border-radius:4px; }"
        "QPushButton:hover { background:#3C3C3C; }"
        "QPushButton:pressed { background:#464646; }";
    const char* kOpStyle =
        "QPushButton { background:#0078D7; color:#FFFFFF; font-size:18px; "
        "border:none; border-radius:4px; }"
        "QPushButton:hover { background:#1A86DA; }"
        "QPushButton:pressed { background:#0068C0; }";
    const char* kNumStyle =
        "QPushButton { background:#3C3C3C; color:#FFFFFF; font-size:18px; "
        "border:none; border-radius:4px; }"
        "QPushButton:hover { background:#484848; }"
        "QPushButton:pressed { background:#565656; }";

    struct Key { const char* text; int row; int col; const char* style; };
    const Key keys[] = {
        {"C", 0, 0, kFuncStyle}, {"CE", 0, 1, kFuncStyle},
        {"%", 0, 2, kFuncStyle}, {"÷", 0, 3, kOpStyle},
        {"7", 1, 0, kNumStyle},  {"8", 1, 1, kNumStyle},
        {"9", 1, 2, kNumStyle},  {"×", 1, 3, kOpStyle},
        {"4", 2, 0, kNumStyle},  {"5", 2, 1, kNumStyle},
        {"6", 2, 2, kNumStyle},  {"−", 2, 3, kOpStyle},
        {"1", 3, 0, kNumStyle},  {"2", 3, 1, kNumStyle},
        {"3", 3, 2, kNumStyle},  {"+", 3, 3, kOpStyle},
        {"±", 4, 0, kFuncStyle}, {"0", 4, 1, kNumStyle},
        {".", 4, 2, kNumStyle},  {"=", 4, 3, kOpStyle},
    };
    for (const auto& k : keys) {
        auto* btn = new QPushButton(QString::fromUtf8(k.text), central);
        btn->setStyleSheet(QLatin1String(k.style));
        btn->setFixedHeight(56);
        connect(btn, &QPushButton::clicked, this, [this, text = QString::fromUtf8(k.text)] {
            pressKey(this, text);
        });
        grid->addWidget(btn, k.row, k.col);
    }
    root->addLayout(grid);
    setCentralWidget(central);
}

bool CalculatorWindow::pressKey(CalculatorWindow* w, const QString& key) {
    if (key == QStringLiteral("C")) {
        w->handleClear();
    } else if (key == QStringLiteral("CE")) {
        w->handleClearEntry();
    } else if (key == QStringLiteral("±")) {
        w->handleNegate();
    } else if (key == QStringLiteral("%")) {
        w->handlePercent();
    } else if (key == QStringLiteral("=")) {
        w->handleEquals();
    } else if (key == QStringLiteral("+") || key == QStringLiteral("−") ||
               key == QStringLiteral("×") || key == QStringLiteral("÷")) {
        w->handleOperator(key);
    } else if (key.size() == 1 &&
               (key[0].isDigit() || key == QStringLiteral("."))) {
        w->handleDigit(key);
    } else {
        return false;
    }
    return true;
}

QString CalculatorWindow::displayText() const {
    return display_ != nullptr ? display_->text() : QString();
}

void CalculatorWindow::handleDigit(const QString& digit) {
    if (error_) {
        // 审查 M3：错误态输入新数字 = 重新开始计算（清累计与积压操作符，
        // 与 Win10 行为一致——否则 7÷0 后输 2 按 = 会继续旧表达式得 3.5）。
        input_.clear();
        acc_ = 0.0;
        pendingOp_.clear();
        error_ = false;
        fresh_ = true;
    }
    if (fresh_) {
        input_.clear();
        fresh_ = false;
    }
    if (digit == QStringLiteral(".")) {
        if (input_.contains('.')) {
            return;  // 防重复小数点
        }
        if (input_.isEmpty()) {
            input_ = QStringLiteral("0.");
            updateDisplay();
            return;
        }
        input_ += '.';
        updateDisplay();
        return;
    }
    // 数字：限制显示长度（避免溢出显示）。
    if (input_.size() >= 16) {
        return;
    }
    input_ += digit;
    updateDisplay();
}

void CalculatorWindow::handleOperator(const QString& op) {
    if (error_) {
        return;
    }
    if (!fresh_) {
        // 结算当前输入到累计值。
        const double v = input_.toDouble();
        if (pendingOp_.isEmpty()) {
            acc_ = v;
        } else if (pendingOp_ == QStringLiteral("+")) {
            acc_ += v;
        } else if (pendingOp_ == QStringLiteral("−")) {
            acc_ -= v;
        } else if (pendingOp_ == QStringLiteral("×")) {
            acc_ *= v;
        } else if (pendingOp_ == QStringLiteral("÷")) {
            // 审查 M1：除零判定用真零——toDouble("0"/"0."/小数 0) 精确得
            // 0.0；kEpsilon 会误伤 1e-14 等合法小除数。
            if (v == 0.0) {
                display_->setText(QStringLiteral("无法除以零"));
                error_ = true;
                return;
            }
            acc_ /= v;
        }
    }
    pendingOp_ = op;
    fresh_ = true;
    updateDisplay();
}

void CalculatorWindow::handleEquals() {
    if (error_) {
        return;
    }
    if (fresh_) {
        // 连续 = 无操作。
        return;
    }
    const double v = input_.toDouble();
    if (!pendingOp_.isEmpty()) {
        if (pendingOp_ == QStringLiteral("+")) {
            acc_ += v;
        } else if (pendingOp_ == QStringLiteral("−")) {
            acc_ -= v;
        } else if (pendingOp_ == QStringLiteral("×")) {
            acc_ *= v;
        } else if (pendingOp_ == QStringLiteral("÷")) {
            // 审查 M1：除零判定用真零（同 handleOperator）。
            if (v == 0.0) {
                display_->setText(QStringLiteral("无法除以零"));
                error_ = true;
                return;
            }
            acc_ /= v;
        }
    } else {
        acc_ = v;
    }
    input_ = formatNumber(acc_);
    pendingOp_.clear();
    fresh_ = true;
    updateDisplay();
}

void CalculatorWindow::handleClear() {
    acc_ = 0.0;
    pendingOp_.clear();
    input_.clear();
    fresh_ = true;
    error_ = false;
    updateDisplay();
}

void CalculatorWindow::handleClearEntry() {
    input_.clear();
    fresh_ = true;
    updateDisplay();
}

void CalculatorWindow::handleNegate() {
    if (error_) {
        return;
    }
    if (fresh_) {
        // 对累计值取反。
        acc_ = -acc_;
        input_ = formatNumber(acc_);
        updateDisplay();
        return;
    }
    if (input_.startsWith('-')) {
        input_.remove(0, 1);
    } else if (!input_.isEmpty() && input_ != QStringLiteral("0")) {
        input_.prepend('-');
    }
    updateDisplay();
}

void CalculatorWindow::handlePercent() {
    if (error_) {
        return;
    }
    // Win10 语义：无操作符时 % = 自身/100（10% → 0.1）；
    // 有操作符时 % = acc * 当前值 / 100（100 + 10% → 100 的 10% = 10，
    // 再按 = 得 110）。不置 fresh_：结果仍是"待结算输入"，= 正常结算。
    const double v = input_.toDouble();
    const double result = pendingOp_.isEmpty() ? v / 100.0 : acc_ * v / 100.0;
    input_ = formatNumber(result);
    updateDisplay();
}

void CalculatorWindow::updateDisplay() {
    if (error_) {
        return;  // 错误文本已直接设置
    }
    if (fresh_ && !pendingOp_.isEmpty()) {
        display_->setText(formatNumber(acc_));
        return;
    }
    if (input_.isEmpty()) {
        display_->setText(QStringLiteral("0"));
        return;
    }
    display_->setText(input_);
}

// ---- 键盘输入（审查 M2：Win10 计算器支持键盘）----
void CalculatorWindow::keyPressEvent(QKeyEvent* e) {
    QString mapped;
    const int k = e->key();
    if (k >= Qt::Key_0 && k <= Qt::Key_9) {
        // 主键盘与小键盘数字的 key() 均为 Key_0..9（KeypadModifier 在
        // modifiers() 中），统一映射。
        mapped = QString::number(k - Qt::Key_0);
    } else if (k == Qt::Key_Period) {
        mapped = QStringLiteral(".");
    } else if (k == Qt::Key_Plus) {
        mapped = QStringLiteral("+");
    } else if (k == Qt::Key_Minus) {
        mapped = QStringLiteral("−");
    } else if (k == Qt::Key_Asterisk) {
        mapped = QStringLiteral("×");
    } else if (k == Qt::Key_Slash) {
        mapped = QStringLiteral("÷");
    } else if (k == Qt::Key_Return || k == Qt::Key_Enter ||
               k == Qt::Key_Equal) {
        mapped = QStringLiteral("=");
    } else if (k == Qt::Key_Escape) {
        mapped = QStringLiteral("C");
    } else if (k == Qt::Key_Backspace) {
        mapped = QStringLiteral("CE");
    } else if (k == Qt::Key_Percent) {
        mapped = QStringLiteral("%");
    } else if (k == Qt::Key_F9) {
        mapped = QStringLiteral("±");
    }
    if (!mapped.isEmpty() && pressKey(this, mapped)) {
        e->accept();
        return;
    }
    QMainWindow::keyPressEvent(e);
}

}  // namespace w10de::calc

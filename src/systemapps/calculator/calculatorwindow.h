// CalculatorWindow —— Win10 标准计算器（深色主题）。
//
// 语义（Win10 标准模式即时计算）：
//   - 数字/小数点输入追加显示；操作符先结算已积压表达式再记录新操作符；
//   - = 结算；C 全清；CE 清当前输入；± 取反；% 按 "acc * display / 100"。
//   - 除零显示 "无法除以零"，输入新数字后恢复。
// UI：深色（#1E1E1E 背景 + #323232 数字键 + #0078D7 运算符/等号），
// 显示区右对齐大字，按钮网格（4 列）。

#pragma once

#include <QMainWindow>
#include <QString>

class QLabel;

namespace w10de::calc {

class CalculatorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit CalculatorWindow(QWidget* parent = nullptr);

    // 纯逻辑接口（--selftest 用）：按一个键（digit/op/= 等）。
    // 返回 true 表示已处理。
    static bool pressKey(CalculatorWindow* w, const QString& key);

    // 当前显示文本（--selftest 断言用；审查 L1——替代 findChildren<QLabel>）。
    QString displayText() const;

protected:
    void keyPressEvent(QKeyEvent* e) override;  // 键盘输入（审查 M2）

private:
    void handleDigit(const QString& digit);
    void handleOperator(const QString& op);
    void handleEquals();
    void handleClear();
    void handleClearEntry();
    void handleNegate();
    void handlePercent();
    void updateDisplay();
    void appendButton(const QString& text, const char* style,
                      const char* slotName);

    QLabel* display_ = nullptr;

    // 计算状态。
    double acc_ = 0.0;          // 累计值
    QString pendingOp_;         // 待应用操作符（+ - * /）
    QString input_;             // 当前输入文本（含小数点/负号）
    bool fresh_ = true;         // 刚结算/刚按操作符：新输入替换显示
    bool error_ = false;        // 除零错误态
};

}  // namespace w10de::calc

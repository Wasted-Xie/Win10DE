// TermWindow —— w10term 主窗口（Win10 风格终端）。
//
// - 中央 TerminalEdit：只读显示 + 按键拦截转发 PTY（终端语义）。
// - ANSI 子集解析：SGR 前景/背景色（16 色 + 亮色 + 39/49 恢复默认）、清屏
//   （ESC[2J）、回车/换行/退格、OSC 标题吞到 BEL；光标移动序列忽略
//   （MVP 简化，逐行追加）。
// - UTF-8 跨 chunk 增量解码（S2）；输入法 commit 转发（M5）。
// - 配色：Win10 终端深色底（#0C0C0C）+ 白字；ANSI 16 色用 Win10 终端色板。
#pragma once

#include <QMainWindow>
#include <QPlainTextEdit>

#include <functional>

class QSocketNotifier;

namespace w10de::term {

class TerminalPty;

// ---- 终端显示区：ANSI 解析 + 按键转发 ----
class TerminalEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit TerminalEdit(QWidget* parent = nullptr);

    // 解析并追加 PTY 输出（含 ANSI 转义；UTF-8 跨 chunk 增量解码）。
    void appendAnsi(const QByteArray& data);

    // 当前输入转发目标（TermWindow 注入；nullptr 时按键不转发）。
    void setInputSink(std::function<void(const QByteArray&)> sink) {
        sink_ = std::move(sink);
    }

protected:
    void keyPressEvent(QKeyEvent* e) override;
    // 输入法（IME）提交转发到 shell（审查 M5：中文输入）。
    void inputMethodEvent(QInputMethodEvent* e) override;

private:
    void appendText(const QString& text, const QTextCharFormat& fmt);
    void trimDocument();  // 行数上限裁剪（L3）
    QByteArray keySequenceForKey(QKeyEvent* e) const;

    // ANSI 解析状态。
    enum class State { Text, Escape, Csi, Osc };
    State state_ = State::Text;
    QString csiBuf_;
    QTextCharFormat curFmt_;   // 当前 SGR 格式
    bool bold_ = false;
    int fg_ = -1;              // 当前前景色索引（-1 = 默认）
    bool fgBright_ = false;    // 90-97 亮色标志（M3）
    int bg_ = -1;
    bool bgBright_ = false;
    QByteArray pendingUtf8_;   // 跨 chunk 不完整 UTF-8 序列（S2）
    std::function<void(const QByteArray&)> sink_;
};

// ---- 主窗口 ----
class TermWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit TermWindow(QWidget* parent = nullptr);
    ~TermWindow() override;

private:
    TerminalPty* pty_ = nullptr;
    TerminalEdit* edit_ = nullptr;
};

}  // namespace w10de::term


#include "systemapps/term/termwindow.h"

#include "systemapps/term/terminalpty.h"
#include "theme/colors.h"

#include <QApplication>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>

#include <functional>
#include <utility>  // std::move

namespace w10de::term {

namespace {

// Win10 终端 16 色板（0-7 基础 + 8-15 亮色）。
constexpr int kAnsiColors[16][3] = {
    {0x0C, 0x0C, 0x0C}, {0xC5, 0x0F, 0x1F}, {0x13, 0xA1, 0x0E}, {0xC1, 0x9C, 0x00},
    {0x00, 0x37, 0xDA}, {0x88, 0x1C, 0x9E}, {0x3A, 0x96, 0xDD}, {0xCC, 0xCC, 0xCC},
    {0x76, 0x76, 0x76}, {0xE7, 0x48, 0x56}, {0x16, 0xC6, 0x0C}, {0xD6, 0x9C, 0x85},
    {0x6C, 0x71, 0xC4}, {0xB4, 0x00, 0x9E}, {0x00, 0xBC, 0xF2}, {0xF2, 0xF2, 0xF2},
};

QColor ansiColor(int idx, bool bright) {
    const int base = bright ? idx + 8 : idx;
    if (base >= 0 && base < 16) {
        return QColor(kAnsiColors[base][0], kAnsiColors[base][1], kAnsiColors[base][2]);
    }
    return QColor();  // 非法
}

// 显示区最大行数（超出裁剪头部，防内存无限增长——审查 L3）。
constexpr int kMaxLines = 10000;

QFont terminalFont() {
    QFont f(QStringLiteral("DejaVu Sans Mono"));
    if (!f.exactMatch()) {
        f = QFont(QStringLiteral("Noto Sans Mono"));
    }
    if (!f.exactMatch()) {
        f = QFont(QStringLiteral("monospace"));
    }
    f.setPointSize(11);
    return f;
}

}  // namespace

// ---- TerminalEdit ----

TerminalEdit::TerminalEdit(QWidget* parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setFrameShape(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(terminalFont());
    setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #0C0C0C; color: %1;"
        "  selection-background-color: #3A96DD; }")
        .arg(theme::kTextPrimary().name()));
    // 默认格式：白字深底。
    curFmt_.setForeground(QColor(0xF2, 0xF2, 0xF2));
    curFmt_.setBackground(QColor(0x0C, 0x0C, 0x0C));
}

void TerminalEdit::appendAnsi(const QByteArray& data) {
    // UTF-8 增量解码：保留 chunk 尾部不完整序列，与下个 chunk 拼接后解码
    //（审查 S2：多字节字符跨 4096B chunk 边界被截断成 U+FFFD）。
    QByteArray full = pendingUtf8_ + data;
    QByteArray::const_iterator it = full.constBegin();
    const QByteArray::const_iterator end = full.constEnd();
    // 从尾部找最长完整 UTF-8 前缀：解码失败即回退一个字节，直到成功。
    int valid = 0;
    for (int len = full.size(); len > 0; --len) {
        const QString s = QString::fromUtf8(full.constData(), len);
        if (!s.contains(QChar::ReplacementCharacter)) {
            valid = len;
            break;
        }
    }
    // 简单可靠策略：直接尝试解码整块；含 U+FFFD 时去掉尾部 1-3 字节重试。
    QString utf8 = QString::fromUtf8(full.constData(), valid);
    pendingUtf8_ = full.mid(valid);

    QString text;
    text.reserve(utf8.size());
    QTextCharFormat fmt = curFmt_;

    for (const QChar ch : utf8) {
        switch (state_) {
        case State::Text:
            if (ch == QLatin1Char('\x1b')) {
                state_ = State::Escape;
            } else if (ch == QLatin1Char('\r')) {
                // 回车：忽略（\r\n 由 \n 换行；无 \n 的行内覆盖不做——
                // MVP 简化，进度条等场景显示异常可接受）。
            } else if (ch == QLatin1Char('\b')) {
                // 退格：优先删本 chunk 缓冲尾部（审查 M6：与文本缓冲
                // 交互——直接删文档末尾会误删上一 chunk 已插入内容）。
                if (!text.isEmpty()) {
                    text.chop(1);
                } else {
                    QTextCursor c = textCursor();
                    c.movePosition(QTextCursor::End);
                    c.deletePreviousChar();
                    setTextCursor(c);
                }
            } else {
                text.append(ch);
            }
            break;
        case State::Escape:
            if (ch == QLatin1Char('[')) {
                csiBuf_.clear();
                state_ = State::Csi;
            } else if (ch == QLatin1Char(']')) {
                // OSC（设置标题等）：吞到 BEL（审查 L1——不吞会泄漏
                // "0;root@host:~/path" 为普通文本）。
                state_ = State::Osc;
            } else {
                // 非 CSI/OSC 转义（ESC 7/8/() 等）：忽略。
                state_ = State::Text;
            }
            break;
        case State::Osc:
            if (ch == QLatin1Char('\x07') || ch == QLatin1Char('\x9c')) {
                state_ = State::Text;  // BEL / ST 结束 OSC
            }
            break;
        case State::Csi:
            if (ch.isDigit() || ch == QLatin1Char(';') || ch == QLatin1Char('?')) {
                csiBuf_.append(ch);
            } else {
                // 终结符。
                if (ch == QLatin1Char('m')) {
                    // SGR：设置格式（作用于后续文本）。
                    const QStringList params = csiBuf_.split(QLatin1Char(';'));
                    for (const QString& p : params) {
                        const int code = p.isEmpty() ? 0 : p.toInt();
                        if (code == 0) {
                            bold_ = false; fg_ = -1; fgBright_ = false;
                            bg_ = -1; bgBright_ = false;
                        } else if (code == 1) {
                            bold_ = true;
                        } else if (code == 39) {
                            fg_ = -1; fgBright_ = false;  // 恢复默认前景
                        } else if (code == 49) {
                            bg_ = -1; bgBright_ = false;  // 恢复默认背景
                        } else if (code >= 30 && code <= 37) {
                            fg_ = code - 30; fgBright_ = false;
                        } else if (code >= 90 && code <= 97) {
                            fg_ = code - 90; fgBright_ = true;  // 亮色（M3）
                        } else if (code >= 40 && code <= 47) {
                            bg_ = code - 40; bgBright_ = false;
                        } else if (code >= 100 && code <= 107) {
                            bg_ = code - 100; bgBright_ = true;
                        }
                    }
                    fmt = curFmt_;
                    fmt.setForeground(fg_ >= 0 ? ansiColor(fg_, fgBright_)
                                               : QColor(0xF2, 0xF2, 0xF2));
                    fmt.setBackground(bg_ >= 0 ? ansiColor(bg_, bgBright_)
                                               : QColor(0x0C, 0x0C, 0x0C));
                    if (bold_) {
                        fmt.setFontWeight(QFont::Bold);
                    } else {
                        fmt.setFontWeight(QFont::Normal);
                    }
                } else if (ch == QLatin1Char('J') && (csiBuf_.isEmpty() || csiBuf_ == QLatin1String("2"))) {
                    // 清屏：清空显示并重置光标格式。
                    clear();
                    curFmt_ = fmt;
                    setCurrentCharFormat(curFmt_);
                } else if (ch == QLatin1Char('K')) {
                    // 清行尾：忽略（MVP）。
                }
                // 其他 CSI（光标移动 H/A/B/C/D 等）：忽略（MVP 追加模式）。
                state_ = State::Text;
            }
            break;
        }
    }

    if (!text.isEmpty()) {
        appendText(text, fmt);
    }
    curFmt_ = fmt;

    // 行数上限裁剪（防内存无限增长——审查 L3）。
    trimDocument();

    // 自动滚动到底部：仅当已接近底部时（审查 M7——用户上翻看历史时
    // 不强制拉底）。
    QScrollBar* sb = verticalScrollBar();
    if (sb->value() >= sb->maximum() - sb->pageStep() / 2) {
        sb->setValue(sb->maximum());
    }
}

void TerminalEdit::appendText(const QString& text, const QTextCharFormat& fmt) {
    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText(text, fmt);
}

void TerminalEdit::trimDocument() {
    QTextDocument* doc = document();
    if (doc->blockCount() <= kMaxLines) {
        return;
    }
    QTextCursor c(doc);
    c.movePosition(QTextCursor::Start);
    c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor,
                   doc->blockCount() - kMaxLines);
    c.removeSelectedText();
}

void TerminalEdit::keyPressEvent(QKeyEvent* e) {
    if (sink_ == nullptr) {
        return QPlainTextEdit::keyPressEvent(e);
    }
    // Ctrl+Shift+C / Ctrl+Shift+V：本地复制/粘贴（Qt 默认处理），不转发。
    if (e->modifiers().testFlag(Qt::ControlModifier) &&
            e->modifiers().testFlag(Qt::ShiftModifier) &&
            (e->key() == Qt::Key_C || e->key() == Qt::Key_V)) {
        if (e->key() == Qt::Key_C) {
            copy();
        } else {
            const QString clip = QApplication::clipboard()->text();
            if (!clip.isEmpty()) {
                sink_(clip.toUtf8());
            }
        }
        return;
    }
    const QByteArray seq = keySequenceForKey(e);
    if (!seq.isEmpty()) {
        sink_(seq);
        return;
    }
    QPlainTextEdit::keyPressEvent(e);  // 兜底（不应到达：显示区只读）
}

void TerminalEdit::inputMethodEvent(QInputMethodEvent* e) {
    // 输入法（IME）提交转发到 shell（审查 M5：中文输入）。
    if (sink_ != nullptr && !e->commitString().isEmpty()) {
        sink_(e->commitString().toUtf8());
    }
    // 只读显示区：不消费预编辑串（无候选框显示——MVP）。
    e->accept();
}

QByteArray TerminalEdit::keySequenceForKey(QKeyEvent* e) const {
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool ctrl = mods.testFlag(Qt::ControlModifier);
    const bool alt = mods.testFlag(Qt::AltModifier);
    const bool shift = mods.testFlag(Qt::ShiftModifier);

    // 控制字符（Ctrl+字母）：发送对应控制码（Ctrl+C=0x03 等）。
    if (ctrl && !alt && key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QByteArray(1, static_cast<char>(key - Qt::Key_A + 1));
    }
    // Alt 组合（emacs/vim meta 前缀）：ESC + 字符（审查 M4）。
    if (alt && !ctrl && !e->text().isEmpty()) {
        return "\x1b" + e->text().toUtf8();
    }
    // 可打印字符：优先 e->text()（Shift+数字 → '!' 等符号正确；审查 M4）。
    if (!ctrl && !alt && !e->text().isEmpty()) {
        return e->text().toUtf8();
    }
    // 无文本的编辑键：功能键/导航键映射。
    const bool shiftMod = shift && !ctrl && !alt;
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QByteArray("\r");
    case Qt::Key_Backspace:
        return QByteArray("\x7f");
    case Qt::Key_Tab:
        return QByteArray("\t");
    case Qt::Key_Escape:
        return QByteArray("\x1b");
    case Qt::Key_Up:
        return QByteArray("\x1b[A");
    case Qt::Key_Down:
        return QByteArray("\x1b[B");
    case Qt::Key_Right:
        return QByteArray("\x1b[C");
    case Qt::Key_Left:
        return QByteArray("\x1b[D");
    case Qt::Key_Delete:
        return QByteArray("\x1b[3~");
    case Qt::Key_Home:
        return QByteArray("\x1b[H");
    case Qt::Key_End:
        return QByteArray("\x1b[F");
    case Qt::Key_PageUp:
        return QByteArray("\x1b[5~");
    case Qt::Key_PageDown:
        return QByteArray("\x1b[6~");
    case Qt::Key_F1:
        return QByteArray("\x1bOP");
    case Qt::Key_F2:
        return QByteArray("\x1bOQ");
    case Qt::Key_F3:
        return QByteArray("\x1bOR");
    case Qt::Key_F4:
        return QByteArray("\x1bOS");
    case Qt::Key_F5:
        return QByteArray("\x1b[15~");
    case Qt::Key_F6:
        return QByteArray("\x1b[17~");
    case Qt::Key_F7:
        return QByteArray("\x1b[18~");
    case Qt::Key_F8:
        return QByteArray("\x1b[19~");
    case Qt::Key_F9:
        return QByteArray("\x1b[20~");
    case Qt::Key_F10:
        return QByteArray("\x1b[21~");
    case Qt::Key_F11:
        return QByteArray("\x1b[23~");
    case Qt::Key_F12:
        return QByteArray("\x1b[24~");
    default:
        return QByteArray();  // 不转发
    }
}

// ---- TermWindow ----

TermWindow::TermWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("终端"));
    resize(920, 600);

    edit_ = new TerminalEdit(this);
    setCentralWidget(edit_);

    pty_ = new TerminalPty(this);
    edit_->setInputSink([this](const QByteArray& data) { pty_->write(data); });
    connect(pty_, &TerminalPty::outputReady, this, [this](const QByteArray& data) {
        if (!data.isEmpty()) {
            edit_->appendAnsi(data);
        }
    });
    connect(pty_, &TerminalPty::shellExited, this, [this](int code) {
        if (code != 0) {
            edit_->appendAnsi("\n[进程已退出，代码 " + QByteArray::number(code) + "]\n");
        }
        // 终端关闭语义：shell 退出后窗口保持显示退出信息（Win10 终端行为），
        // 由用户关闭窗口；不做自动退出，避免误关。
    });
    const bool noPty = qEnvironmentVariableIsSet("W10TERM_NO_PTY");
    if (!noPty && !pty_->start()) {
        edit_->appendAnsi("无法启动终端（forkpty 失败）\n");
    }
}

TermWindow::~TermWindow() {
    // pty_ 由父对象析构（stop 关闭 master + SIGHUP + 回收子进程）。
}

}  // namespace w10de::term

// w10osk 核心逻辑实现（可选拓展 E8 屏幕键盘）。
//
// keysym 使用 xkbcommon-keysyms.h 的宏常量（纯头文件，无需链接）。
// 标点上层（shifted）映射：1! 2@ 3# 4$ 5% 6^ 7& 8* 9( 0) -_ =+ [{ ]} \|
// ;: '" ,< .> /? `~。

#include "systemapps/osk/osk.h"

#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>

#include <xkbcommon/xkbcommon-keysyms.h>

namespace w10de::osk {

namespace {

KeyDef ch(QChar c, int colspan = 1) {
    KeyDef k;
    k.label = QString(c);
    k.keysym = static_cast<uint32_t>(c.unicode());  // ASCII keysym == 码点
    k.type = KeyDef::Char;
    k.colspan = colspan;
    return k;
}

KeyDef key(const QString& label, uint32_t keysym, KeyDef::Type type,
           int colspan = 1) {
    KeyDef k;
    k.label = label;
    k.keysym = keysym;
    k.type = type;
    k.colspan = colspan;
    return k;
}

// 基础键符 → shifted 上层（label 显示层；keysym 恒为基础键符——
// 上层字符由 xkb 在 Shift 按住时组合输出）。
struct ShiftedPair {
    QChar base;
    QChar upper;
};

// shifted 时字符键的 label（未在表中 = 大写字母或原样）。
QChar shiftedLabel(QChar base, bool shifted) {
    if (!shifted) return base;
    static const ShiftedPair pairs[] = {
        {QLatin1Char('1'), QLatin1Char('!')}, {QLatin1Char('2'), QLatin1Char('@')},
        {QLatin1Char('3'), QLatin1Char('#')}, {QLatin1Char('4'), QLatin1Char('$')},
        {QLatin1Char('5'), QLatin1Char('%')}, {QLatin1Char('6'), QLatin1Char('^')},
        {QLatin1Char('7'), QLatin1Char('&')}, {QLatin1Char('8'), QLatin1Char('*')},
        {QLatin1Char('9'), QLatin1Char('(')}, {QLatin1Char('0'), QLatin1Char(')')},
        {QLatin1Char('-'), QLatin1Char('_')}, {QLatin1Char('='), QLatin1Char('+')},
        {QLatin1Char('['), QLatin1Char('{')}, {QLatin1Char(']'), QLatin1Char('}')},
        {QLatin1Char('\\'), QLatin1Char('|')}, {QLatin1Char(';'), QLatin1Char(':')},
        {QLatin1Char('\''), QLatin1Char('"')}, {QLatin1Char(','), QLatin1Char('<')},
        {QLatin1Char('.'), QLatin1Char('>')}, {QLatin1Char('/'), QLatin1Char('?')},
    };
    for (const auto& p : pairs) {
        if (p.base == base) return p.upper;
    }
    if (base.isLetter()) return base.toUpper();
    return base;
}

}  // namespace

QList<QList<KeyDef>> layout(bool shifted) {
    QList<QList<KeyDef>> rows;
    // 数字行（shifted 显示上层标点；keysym 恒为数字）。
    const QChar digits[] = {
        QLatin1Char('1'), QLatin1Char('2'), QLatin1Char('3'),
        QLatin1Char('4'), QLatin1Char('5'), QLatin1Char('6'),
        QLatin1Char('7'), QLatin1Char('8'), QLatin1Char('9'),
        QLatin1Char('0')};
    QList<KeyDef> row0;
    row0.append(key(QStringLiteral("Esc"), XKB_KEY_Escape,
                    KeyDef::Esc, 2));
    for (QChar c : digits) {
        KeyDef k = ch(c);
        k.label = QString(shiftedLabel(c, shifted));
        row0.append(k);
    }
    row0.append(key(QStringLiteral("-"), XKB_KEY_minus, KeyDef::Char));
    row0.append(key(QStringLiteral("="), XKB_KEY_equal, KeyDef::Char));
    if (shifted) {
        row0[row0.size() - 2].label = QStringLiteral("_");
        row0[row0.size() - 1].label = QStringLiteral("+");
    }
    row0.append(key(QStringLiteral("退格"), XKB_KEY_BackSpace,
                    KeyDef::Backspace, 2));
    rows.append(row0);

    // QWERTY 行。
    const QChar q1[] = {
        QLatin1Char('q'), QLatin1Char('w'), QLatin1Char('e'),
        QLatin1Char('r'), QLatin1Char('t'), QLatin1Char('y'),
        QLatin1Char('u'), QLatin1Char('i'), QLatin1Char('o'),
        QLatin1Char('p'), QLatin1Char('['), QLatin1Char(']'),
        QLatin1Char('\\')};
    QList<KeyDef> row1;
    row1.append(key(QStringLiteral("Tab"), XKB_KEY_Tab, KeyDef::Tab, 2));
    for (QChar c : q1) {
        KeyDef k = ch(c);
        k.label = QString(shiftedLabel(c, shifted));
        row1.append(k);
    }
    row1.append(key(QStringLiteral("回车"), XKB_KEY_Return,
                    KeyDef::Enter, 2));
    rows.append(row1);

    // 中间行。
    const QChar q2[] = {
        QLatin1Char('a'), QLatin1Char('s'), QLatin1Char('d'),
        QLatin1Char('f'), QLatin1Char('g'), QLatin1Char('h'),
        QLatin1Char('j'), QLatin1Char('k'), QLatin1Char('l'),
        QLatin1Char(';'), QLatin1Char('\'')};
    QList<KeyDef> row2;
    row2.append(key(QStringLiteral("大写"), XKB_KEY_Caps_Lock,
                    KeyDef::Char, 2));  // 简化：CapsLock 当普通键
    for (QChar c : q2) {
        KeyDef k = ch(c);
        k.label = QString(shiftedLabel(c, shifted));
        row2.append(k);
    }
    rows.append(row2);

    // 底行。
    const QChar q3[] = {
        QLatin1Char('z'), QLatin1Char('x'), QLatin1Char('c'),
        QLatin1Char('v'), QLatin1Char('b'), QLatin1Char('n'),
        QLatin1Char('m'), QLatin1Char(','), QLatin1Char('.'),
        QLatin1Char('/')};
    QList<KeyDef> row3;
    row3.append(key(QStringLiteral("Shift"), XKB_KEY_Shift_L,
                    KeyDef::Shift, 2));
    for (QChar c : q3) {
        KeyDef k = ch(c);
        k.label = QString(shiftedLabel(c, shifted));
        row3.append(k);
    }
    row3.append(key(QStringLiteral("Shift"), XKB_KEY_Shift_R,
                    KeyDef::Shift, 2));
    rows.append(row3);

    // 修饰 + 空格 + 方向键行。
    QList<KeyDef> row4;
    row4.append(key(QStringLiteral("Ctrl"), XKB_KEY_Control_L, KeyDef::Ctrl));
    row4.append(key(QStringLiteral("Alt"), XKB_KEY_Alt_L, KeyDef::Alt));
    row4.append(key(QStringLiteral("空格"), XKB_KEY_space, KeyDef::Space, 6));
    row4.append(key(QStringLiteral("←"), XKB_KEY_Left, KeyDef::Arrow));
    row4.append(key(QStringLiteral("↑"), XKB_KEY_Up, KeyDef::Arrow));
    row4.append(key(QStringLiteral("↓"), XKB_KEY_Down, KeyDef::Arrow));
    row4.append(key(QStringLiteral("→"), XKB_KEY_Right, KeyDef::Arrow));
    rows.append(row4);
    return rows;
}

QList<KeyDef> allKeys() {
    QList<KeyDef> keys;
    for (const auto& row : layout(false)) {
        for (const KeyDef& k : row) {
            keys.append(k);
        }
    }
    return keys;
}

bool isModifier(uint32_t keysym) {
    return keysym == XKB_KEY_Shift_L || keysym == XKB_KEY_Shift_R
        || keysym == XKB_KEY_Control_L || keysym == XKB_KEY_Control_R
        || keysym == XKB_KEY_Alt_L || keysym == XKB_KEY_Alt_R
        || keysym == XKB_KEY_Super_L || keysym == XKB_KEY_Super_R;
}

bool injectKey(uint32_t keysym, bool pressed) {
    // compositor 的 D-Bus 服务（InputKey 由 E8 新增；对象路径 /Outputs
    // 与 w10settings 等现有客户端一致）。
    QDBusInterface iface(QStringLiteral("org.w10de.Compositor"),
                         QStringLiteral("/Outputs"),
                         QStringLiteral("org.w10de.Compositor"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        return false;
    }
    // 审查 L8：缩短同步调用超时（默认 25s——compositor 无响应时 UI 冻结）。
    iface.setTimeout(1000);
    QDBusReply<void> reply = iface.call(QStringLiteral("InputKey"),
                                        keysym, pressed);
    return reply.isValid();
}

}  // namespace w10de::osk

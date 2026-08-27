// w10osk —— 屏幕键盘核心逻辑（可选拓展 E8）。
//
// 键布局为可测静态数据（selftest 覆盖）；按键经 D-Bus 调用 compositor 的
// InputKey(keysym, pressed) 注入（compositor 侧 Seat::injectKey 反查
// keycode 并走 wlr_seat 键盘通知）。
#pragma once

#include <QList>
#include <QString>
#include <cstdint>

namespace w10de::osk {

struct KeyDef {
    enum Type {
        Char,       // 字符键（label 显示，keysym 注入）
        Shift,      // Shift（可切换保持）
        Ctrl,
        Alt,
        Super,
        Backspace,
        Enter,
        Space,
        Tab,
        Esc,
        Arrow,      // 方向键（label 含箭头符号）
    };
    QString label;
    uint32_t keysym = 0;
    int colspan = 1;   // 网格列宽
    Type type = Char;
};

// 完整键盘布局（5 行；shifted=true 时字符键切大写/标点上层）。
// 每行是 KeyDef 列表（列数 14）。
QList<QList<KeyDef>> layout(bool shifted);

// 全部键（去重收集，selftest 校验覆盖度）。
QList<KeyDef> allKeys();

// keysym 是否为修饰键（Shift/Ctrl/Alt/Super——按下保持、再按释放）。
bool isModifier(uint32_t keysym);

// 经 D-Bus 注入按键（org.w10de.Compositor.InputKey）。返回调用是否成功。
bool injectKey(uint32_t keysym, bool pressed);

}  // namespace w10de::osk

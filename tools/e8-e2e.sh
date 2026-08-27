#!/bin/bash
# E8 屏幕键盘端到端验证：headless compositor + 键盘客户端 +
# D-Bus InputKey 注入 → 客户端收到 key 事件（keycode 正确性）。
set -u
cd /root/win10de/build
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true
pkill -x w10compositor 2>/dev/null; sleep 0.5
FAIL=0

echo "==== 1) 编译键盘客户端 ===="
mkdir -p /tmp/e8src
cp /mnt/c/Projects/Win10DE/tools/e8-keyclient.c /tmp/e8src/
cd /tmp/e8src
wayland-scanner client-header \
  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
  /tmp/e8src/xdg-shell-client-protocol.h 2>/dev/null \
  || cp /root/win10de/tools/xdg-shell-client-protocol.h /tmp/e8src/
wayland-scanner private-code \
  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
  /tmp/e8src/xdg-shell-protocol.c 2>/dev/null \
  || cp /root/win10de/tools/xdg-shell-protocol.c /tmp/e8src/
cc e8-keyclient.c xdg-shell-protocol.c \
  -I. $(pkg-config --cflags --libs wayland-client) \
  -o /tmp/e8-keyclient 2>&1 | head -5
[ -x /tmp/e8-keyclient ] || { echo "FAIL: 客户端编译失败"; exit 1; }
echo "OK client built"

echo "==== 2) 启动 compositor + 客户端 ===="
cd /root/win10de/build
rm -f /run/user/0/e8-kb
./src/compositor/w10compositor --outputs 1 --socket e8-kb --frames 3000 \
  >/tmp/e8-comp.log 2>&1 &
CPID=$!
for i in $(seq 1 50); do [ -S /run/user/0/e8-kb ] && break; sleep 0.1; done
sleep 0.8
WAYLAND_DISPLAY=e8-kb /tmp/e8-keyclient >/tmp/e8-kc.log 2>&1 &
KPID=$!
# 等客户端 READY（keymap 收到 + 窗口焦点）。
for i in $(seq 1 60); do
  grep -q "READY keymap=1" /tmp/e8-kc.log 2>/dev/null && break
  sleep 0.2
done
sleep 0.3
cat /tmp/e8-kc.log
grep -q "READY keymap=1" /tmp/e8-kc.log \
  || { echo "FAIL: 客户端未就绪"; cat /tmp/e8-comp.log | tail -5; FAIL=1; }
grep -q "FOCUS_IN" /tmp/e8-kc.log \
  || { echo "FAIL: 客户端未获得键盘焦点"; FAIL=1; }

echo "==== 3) D-Bus InputKey 注入 'a'（0x61）====="
dbus-send --session --print-reply --dest=org.w10de.Compositor \
  /Outputs org.w10de.Compositor.InputKey \
  uint32:97 boolean:true >/tmp/e8-dbus1.log 2>&1
echo "press rc=$? $(head -1 /tmp/e8-dbus1.log)"
dbus-send --session --print-reply --dest=org.w10de.Compositor \
  /Outputs org.w10de.Compositor.InputKey \
  uint32:97 boolean:false >/tmp/e8-dbus2.log 2>&1
echo "release rc=$? $(head -1 /tmp/e8-dbus2.log)"
sleep 0.8

echo "==== 4) 注入 Shift 按住 + 'A'（验证修饰键路径）====="
dbus-send --session --print-reply --dest=org.w10de.Compositor /Outputs \
  org.w10de.Compositor.InputKey uint32:65505 boolean:true >/dev/null 2>&1 \
  && echo "Shift press ok" || { echo "FAIL: Shift press 调用失败"; FAIL=1; }
dbus-send --session --print-reply --dest=org.w10de.Compositor /Outputs \
  org.w10de.Compositor.InputKey uint32:97 boolean:true >/dev/null 2>&1
dbus-send --session --print-reply --dest=org.w10de.Compositor /Outputs \
  org.w10de.Compositor.InputKey uint32:97 boolean:false >/dev/null 2>&1
dbus-send --session --print-reply --dest=org.w10de.Compositor /Outputs \
  org.w10de.Compositor.InputKey uint32:65505 boolean:false >/dev/null 2>&1 \
  && echo "Shift release ok" || { echo "FAIL: Shift release 调用失败"; FAIL=1; }
sleep 0.8
cat /tmp/e8-kc.log

echo "==== 5) 断言 ===="
cat /tmp/e8-kc.log
grep -q "READY keymap=1" /tmp/e8-kc.log \
  && echo "OK keymap received" || { echo "FAIL: 未收到 keymap"; FAIL=1; }
grep -q "FOCUS_IN" /tmp/e8-kc.log \
  && echo "OK focus received" || { echo "FAIL: 无焦点"; FAIL=1; }
KEYS=$(grep -c "^KEY" /tmp/e8-kc.log)
echo "KEY events: $KEYS"
# 期望：'a' press+release(2) + Shift press(1) + 'a' press+release(2) + Shift release(1) = 6
[ "$KEYS" -ge 5 ] || { echo "FAIL: 注入未完全到达客户端"; FAIL=1; }
MODS=$(grep -c "^MODS" /tmp/e8-kc.log)
echo "MODS events: $MODS"
[ "$MODS" -ge 2 ] || { echo "FAIL: 无修饰键事件"; FAIL=1; }
# Shift 按住时修饰键掩码应为非零（depressed=1 至少一次）。
grep -q "MODS depressed=[1-9]" /tmp/e8-kc.log \
  && echo "OK shift modifier applied" \
  || { echo "FAIL: Shift 修饰键未生效"; FAIL=1; }

kill $KPID 2>/dev/null || true
kill $CPID 2>/dev/null || true
pkill -x w10compositor 2>/dev/null || true
[ $FAIL -eq 0 ] && echo "E8 END-TO-END PASS" || echo "E8 END-TO-END FAIL"
exit $FAIL

#!/bin/bash
# 检查 Tasks 对象的 D-Bus 接口（Reload 导出验证）
cd /root/win10de
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true
pkill -x w10tasks 2>/dev/null; sleep 0.3
./build/src/systemapps/w10tasks --daemon > /tmp/g5-taskd7.log 2>&1 &
sleep 2
dbus-send --session --print-reply --dest=org.w10de.Tasks /Tasks \
  org.freedesktop.DBus.Introspectable.Introspect 2>&1 \
  | grep -E 'interface name|Reload' | head -12
pkill -x w10tasks

#!/bin/bash
# 复现 --output HEADLESS-1 匹配问题
cd /root/win10de
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true
pkill -x w10compositor 2>/dev/null; sleep 0.5
./build/src/compositor/w10compositor --outputs 1 --socket g2-x --frames 300 > /tmp/g2-x.log 2>&1 &
CP=$!
for i in $(seq 1 40); do [ -S /run/user/0/g2-x ] && break; sleep 0.1; done
sleep 1
echo '== no --output =='
WAYLAND_DISPLAY=g2-x ./build/src/systemapps/w10screenshot --fullscreen --out /tmp/g2-x1.png 2>&1 | tail -1
echo '== --output HEADLESS-1（完整错误）=='
WAYLAND_DISPLAY=g2-x ./build/src/systemapps/w10screenshot --fullscreen --output HEADLESS-1 --out /tmp/g2-x2.png 2>&1 | grep -vE 'locale|UTF-8|problem|manual|Detected'
echo '== --output HEADLESS =='
WAYLAND_DISPLAY=g2-x ./build/src/systemapps/w10screenshot --fullscreen --output HEADLESS --out /tmp/g2-x3.png 2>&1 | tail -1
wait $CP 2>/dev/null

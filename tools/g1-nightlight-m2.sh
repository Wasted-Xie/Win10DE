#!/bin/bash
# G1 M2 验证：SetNightLight start==end 应被 D-Bus 拒绝；正常值成功。
cd /root/win10de
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true
pkill -x w10compositor 2>/dev/null; sleep 0.5
rm -f /root/.config/w10de/config.ini
./build/src/compositor/w10compositor --outputs 1 --socket g1-nl2 --frames 200 \
  > /tmp/g1-nl2.log 2>&1 &
CP=$!
for i in $(seq 1 40); do [ -S /run/user/0/g1-nl2 ] && break; sleep 0.1; done
sleep 0.5
echo '== start==end (应拒绝) =='
dbus-send --session --print-reply --dest=org.w10de.Compositor /Outputs \
  org.w10de.Compositor.SetNightLight boolean:true int32:4500 int32:1200 int32:1200 2>&1 | head -3
echo '== 正常值 (应成功) =='
dbus-send --session --print-reply --dest=org.w10de.Compositor /Outputs \
  org.w10de.Compositor.SetNightLight boolean:true int32:5000 int32:1200 int32:360 2>&1 | head -2
wait $CP 2>/dev/null
echo '== config =='
awk '/\[night_light\]/{f=1} f{print}' /root/.config/w10de/config.ini 2>/dev/null

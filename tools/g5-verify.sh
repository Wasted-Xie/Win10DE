#!/bin/bash
# G5 验证：w10tasks 渲染 + 守护端到端（每分钟任务执行 + last_run 更新）。
cd /root/win10de
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true
pkill -x w10tasks 2>/dev/null; pkill -x w10compositor 2>/dev/null; sleep 1

echo "==== 1) selftest ===="
./build/src/systemapps/w10tasks --selftest 2>&1 | grep -E 'OK|PASS|FAIL'

echo "==== 2) 守护端到端（每分钟任务）===="
# 备份原任务配置并写入每分钟任务（touch 证明文件）。
CFG=~/.config/w10de/tasks.ini
[ -f $CFG ] && cp $CFG $CFG.g5bak
PROOF=/tmp/taskd-proof.txt
rm -f $PROOF
cat > $CFG <<'EOF'
[task:1]
name = 守护证明
command = touch /tmp/taskd-proof.txt
minute = -1
hour = -1
day_of_month = -1
month = -1
day_of_week = -1
enabled = 1
EOF
./build/src/systemapps/w10tasks --daemon > /tmp/g5-taskd.log 2>&1 &
TD=$!
sleep 3
echo "== D-Bus Reload 调用 =="
dbus-send --session --print-reply --dest=org.w10de.Tasks /Tasks \
  org.w10de.Tasks.Reload 2>&1 | head -2
echo "== 等待 65 秒（守护 tick 60s + 余量）=="
sleep 65
kill $TD 2>/dev/null || true
if [ -f $PROOF ]; then
  echo "守护已执行任务（touch 证明文件存在）"
else
  echo "FAIL: 守护未执行任务"
fi
echo "== tasks.ini last_run/last_result =="
grep -E 'last_' $CFG || echo "(无 last_ 字段——守护未写回)"
[ -f $CFG.g5bak ] && mv $CFG.g5bak $CFG

echo "==== 3) GUI 渲染 ===="
rm -f /tmp/g5-tasks.png
./build/src/compositor/w10compositor --outputs 1 --socket g5-tasks --frames 400 \
  --screenshot /tmp/g5-tasks.png > /tmp/g5-tasks-comp.log 2>&1 &
CPID=$!
for i in $(seq 1 50); do [ -S /run/user/0/g5-tasks ] && break; sleep 0.1; done
sleep 1
WAYLAND_DISPLAY=g5-tasks ./build/src/systemapps/w10tasks > /tmp/g5-tasks-app.log 2>&1 &
APID=$!
BEST=0
for i in $(seq 1 30); do
  sleep 0.5
  [ -f /tmp/g5-tasks.png ] || continue
  S=$(stat -c %s /tmp/g5-tasks.png 2>/dev/null || echo 0)
  if [ "$S" -gt "$BEST" ]; then cp /tmp/g5-tasks.png /tmp/g5-tasks-best.png; BEST=$S; fi
done
wait $CPID || true
kill $APID 2>/dev/null || true

python3 - <<'PYEOF'
import zlib, struct

def decode(path):
    data = open(path, 'rb').read()
    pos = 8; idat = b''; width = height = 0
    while pos < len(data):
        ln = struct.unpack('>I', data[pos:pos+4])[0]
        typ = data[pos+4:pos+8]
        chunk = data[pos+8:pos+8+ln]
        if typ == b'IHDR': width, height = struct.unpack('>II', chunk[:8])
        elif typ == b'IDAT': idat += chunk
        pos += 12 + ln
    raw = zlib.decompress(idat)
    bpp = 4; stride = width * bpp
    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
        if pa <= pb and pa <= pc: return a
        if pb <= pc: return b
        return c
    out = bytearray(); prev = bytearray(stride); p = 0
    for y in range(height):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p+stride]); p += stride
        for x in range(stride):
            a = line[x-bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x-bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 0xff
            elif f == 2: line[x] = (line[x] + b) & 0xff
            elif f == 3: line[x] = (line[x] + (a+b)//2) & 0xff
            elif f == 4: line[x] = (line[x] + paeth(a, b, c)) & 0xff
        out += line; prev = line
    return width, height, out

w, h, out = decode('/tmp/g5-tasks-best.png')
dark = bright = 0
wx0, wy0, wx1, wy1 = 100, 80, 920, 560
for y in range(0, h, 2):
    for x in range(0, w, 2):
        i = (y*w + x)*4
        r, g, b = out[i], out[i+1], out[i+2]
        if wx0 <= x <= wx1 and wy0 <= y <= wy1:
            if 25 < r < 35 and 25 < g < 35 and 25 < b < 35: dark += 1
        if min(r, g, b) > 200: bright += 1
print('任务计划 %dx%d: 窗口深底=%d 亮文字=%d' % (w, h, dark, bright))
print('  OK' if dark > 2000 and bright > 300 else '  FAIL')
PYEOF
grep -avE 'locale|UTF-8|propagate' /tmp/g5-tasks-app.log | head -2

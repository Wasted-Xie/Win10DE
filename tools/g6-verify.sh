#!/bin/bash
# G6 验证：月历逻辑 selftest + 独立渲染（今天高亮/网格）。
cd /root/win10de
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true
pkill -x w10shell 2>/dev/null; pkill -x w10compositor 2>/dev/null; sleep 1

echo "==== 1) 月历逻辑 selftest ===="
./build/src/shell/w10shell --calendar-selftest 2>&1 | grep -E 'PASS|FAIL'

echo "==== 2) 月历独立渲染 ===="
rm -f /tmp/g6-cal.png
./build/src/compositor/w10compositor --outputs 1 --socket g6-cal --frames 400 \
  --screenshot /tmp/g6-cal.png > /tmp/g6-cal-comp.log 2>&1 &
CPID=$!
for i in $(seq 1 50); do [ -S /run/user/0/g6-cal ] && break; sleep 0.1; done
sleep 1
WAYLAND_DISPLAY=g6-cal ./build/src/shell/w10shell --calendar-render \
  > /tmp/g6-cal-app.log 2>&1 &
APID=$!
BEST=0
for i in $(seq 1 30); do
  sleep 0.5
  [ -f /tmp/g6-cal.png ] || continue
  S=$(stat -c %s /tmp/g6-cal.png 2>/dev/null || echo 0)
  if [ "$S" -gt "$BEST" ]; then cp /tmp/g6-cal.png /tmp/g6-cal-best.png; BEST=$S; fi
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

w, h, out = decode('/tmp/g6-cal-best.png')
# 月历 260x300 @ (100,80) 附近：面板深底 #262b33 + 今天蓝圆 (0,120,215)
# + 亮文字 + 底部今天行
panel = accent = bright = 0
wx0, wy0, wx1, wy1 = 100, 80, 400, 420
for y in range(0, h, 2):
    for x in range(0, w, 2):
        i = (y*w + x)*4
        r, g, b = out[i], out[i+1], out[i+2]
        if wx0 <= x <= wx1 and wy0 <= y <= wy1:
            if 33 < r < 45 and 38 < g < 50 and 45 < b < 60: panel += 1
            if r < 15 and 100 < g < 140 and 195 < b < 235: accent += 1
        if min(r, g, b) > 120: bright += 1   # kText 232 / kDim 138 均计入
print('月历 %dx%d: 面板深底=%d 今天蓝圆=%d 文字=%d' % (w, h, panel, accent, bright))
ok = panel > 500 and accent > 20 and bright > 300
print('  OK（月历网格 + 今天高亮渲染）' if ok else '  FAIL')
PYEOF
grep -avE 'locale|UTF-8|propagate' /tmp/g6-cal-app.log | head -2

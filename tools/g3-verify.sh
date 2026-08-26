#!/bin/bash
# G3 验证：w10devices 设备管理器 selftest + headless 渲染。
cd /root/win10de
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true
pkill -x w10devices 2>/dev/null; pkill -x w10compositor 2>/dev/null; sleep 1

echo "==== selftest ===="
./build/src/systemapps/w10devices --selftest 2>&1 | grep -E 'OK|PASS|FAIL'

echo "==== 渲染 ===="
rm -f /tmp/g3-dev.png
./build/src/compositor/w10compositor --outputs 1 --socket g3-dev --frames 400 \
  --screenshot /tmp/g3-dev.png > /tmp/g3-dev-comp.log 2>&1 &
CPID=$!
for i in $(seq 1 50); do [ -S /run/user/0/g3-dev ] && break; sleep 0.1; done
sleep 1
WAYLAND_DISPLAY=g3-dev ./build/src/systemapps/w10devices > /tmp/g3-dev-app.log 2>&1 &
APID=$!
BEST=0
for i in $(seq 1 30); do
  sleep 0.5
  [ -f /tmp/g3-dev.png ] || continue
  S=$(stat -c %s /tmp/g3-dev.png 2>/dev/null || echo 0)
  if [ "$S" -gt "$BEST" ]; then cp /tmp/g3-dev.png /tmp/g3-dev-best.png; BEST=$S; fi
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

w, h, out = decode('/tmp/g3-dev-best.png')
# 窗口 820x520 @ (100,80)：树区（#1e1e1e 背景 + 图标蓝）+ 详情表 + 亮文字
blue = bright = dark = 0
for y in range(0, h, 2):
    for x in range(0, w, 2):
        i = (y*w + x)*4
        r, g, b = out[i], out[i+1], out[i+2]
        if 60 < r < 130 and 100 < g < 170 and 160 < b < 235: blue += 1
        if min(r, g, b) > 200: bright += 1
        if 25 < r < 40 and 25 < g < 40 and 25 < b < 40: dark += 1
print('设备管理器 %dx%d: 图标蓝=%d 亮文字=%d 窗口深底=%d' % (w, h, blue, bright, dark))
print('  OK' if blue > 10 and bright > 500 and dark > 3000 else '  FAIL')
PYEOF
echo "== 应用日志 =="
grep -avE 'locale|UTF-8|propagate' /tmp/g3-dev-app.log | head -2

#!/bin/bash
# G2 交互模式渲染验证：compositor 下 w10screenshot（无参数）遮罩窗口。
cd /root/win10de
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true
pkill -x w10screenshot 2>/dev/null; pkill -x w10compositor 2>/dev/null; sleep 1
rm -f /tmp/g2-ui.png
./build/src/compositor/w10compositor --outputs 1 --socket g2-ui --frames 400 \
  --screenshot /tmp/g2-ui.png > /tmp/g2-ui-comp.log 2>&1 &
CPID=$!
for i in $(seq 1 50); do [ -S /run/user/0/g2-ui ] && break; sleep 0.1; done
sleep 1
WAYLAND_DISPLAY=g2-ui ./build/src/systemapps/w10screenshot > /tmp/g2-ui-app.log 2>&1 &
APID=$!
BEST=0
for i in $(seq 1 30); do
  sleep 0.5
  [ -f /tmp/g2-ui.png ] || continue
  S=$(stat -c %s /tmp/g2-ui.png 2>/dev/null || echo 0)
  if [ "$S" -gt "$BEST" ]; then cp /tmp/g2-ui.png /tmp/g2-ui-best.png; BEST=$S; fi
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

w, h, out = decode('/tmp/g2-ui-best.png')
# 遮罩：全屏蓝壁纸(0,120,215)被半透明黑覆盖 → 变暗（约 0,105,188）
# 工具条：深色面板 (#262a32 附近) + 按钮亮文字 + 提示条
toolbar_dark = bright = dim_blue = 0
for y in range(0, h, 2):
    for x in range(0, w, 2):
        i = (y*w + x)*4
        r, g, b = out[i], out[i+1], out[i+2]
        if 30 < r < 55 and 34 < g < 60 and 40 < b < 70: toolbar_dark += 1
        if min(r, g, b) > 200: bright += 1
        if 0 < r < 30 and 90 < g < 130 and 160 < b < 215: dim_blue += 1
print('交互窗口 %dx%d: 工具条深色=%d 亮文字=%d 遮罩暗蓝=%d' % (w, h, toolbar_dark, bright, dim_blue))
print('  OK' if toolbar_dark > 500 and bright > 100 else '  FAIL: 交互遮罩渲染不足')
PYEOF
grep -avE 'locale|UTF-8|propagate' /tmp/g2-ui-app.log | head -2

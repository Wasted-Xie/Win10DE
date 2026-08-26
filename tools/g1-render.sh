#!/bin/bash
# G1 验证：w10control 主页渲染 + w10settings 新 3 页渲染 + SetNightLight D-Bus。
set -u
cd /root/win10de
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true

pkill -x w10control 2>/dev/null; pkill -x w10settings 2>/dev/null; pkill -x w10compositor 2>/dev/null; sleep 1

# ============ 1) w10control 主页渲染 ============
rm -f /tmp/g1-ctrl*.png
./build/src/compositor/w10compositor --outputs 1 --socket g1-ctrl --frames 400 \
  --screenshot /tmp/g1-ctrl.png > /tmp/g1-ctrl-comp.log 2>&1 &
CPID=$!
for i in $(seq 1 50); do [ -S /run/user/0/g1-ctrl ] && break; sleep 0.1; done
sleep 1
WAYLAND_DISPLAY=g1-ctrl ./build/src/systemapps/w10control > /tmp/g1-ctrl-app.log 2>&1 &
APID=$!
BEST=0
for i in $(seq 1 30); do
  sleep 0.5
  [ -f /tmp/g1-ctrl.png ] || continue
  S=$(stat -c %s /tmp/g1-ctrl.png 2>/dev/null || echo 0)
  if [ "$S" -gt "$BEST" ]; then cp /tmp/g1-ctrl.png /tmp/g1-ctrl-best.png; BEST=$S; fi
done
wait $CPID || true
kill $APID 2>/dev/null || true

# ============ 2) w10settings 新 3 页渲染 ============
for PAGE in nightlight shortcuts rules; do
  ./build/src/compositor/w10compositor --outputs 1 --socket g1-set-$PAGE --frames 400 \
    --screenshot /tmp/g1-set-$PAGE.png > /tmp/g1-set-$PAGE-comp.log 2>&1 &
  CP=$!
  for i in $(seq 1 50); do [ -S /run/user/0/g1-set-$PAGE ] && break; sleep 0.1; done
  sleep 1
  WAYLAND_DISPLAY=g1-set-$PAGE ./build/src/systemapps/w10settings --page $PAGE \
    > /tmp/g1-set-$PAGE-app.log 2>&1 &
  AP=$!
  BEST=0
  for i in $(seq 1 30); do
    sleep 0.5
    [ -f /tmp/g1-set-$PAGE.png ] || continue
    S=$(stat -c %s /tmp/g1-set-$PAGE.png 2>/dev/null || echo 0)
    if [ "$S" -gt "$BEST" ]; then cp /tmp/g1-set-$PAGE.png /tmp/g1-set-$PAGE-best.png; BEST=$S; fi
  done
  wait $CP || true
  kill $AP 2>/dev/null || true
done

# ============ 3) SetNightLight D-Bus 热应用 ============
rm -f /root/.config/w10de/config.ini /tmp/g1-nl.png
./build/src/compositor/w10compositor --outputs 1 --socket g1-nl --frames 300 \
  --screenshot /tmp/g1-nl.png > /tmp/g1-nl-comp.log 2>&1 &
CP=$!
for i in $(seq 1 50); do [ -S /run/user/0/g1-nl ] && break; sleep 0.1; done
sleep 1
dbus-send --session --print-reply --dest=org.w10de.Compositor /Outputs \
  org.w10de.Compositor.SetNightLight boolean:true int32:4500 int32:1260 int32:360 \
  > /tmp/g1-nl-dbus.log 2>&1
sleep 1
wait $CP || true
echo "== SetNightLight dbus reply =="
cat /tmp/g1-nl-dbus.log
echo "== config.ini [night_light] =="
awk '/\[night_light\]/{f=1} f{print} /^\[/&&!/\[night_light\]/{f=0}' /root/.config/w10de/config.ini 2>/dev/null
echo "== compositor night light log =="
grep -a 'night light\|setNightLight' /tmp/g1-nl-comp.log | tail -4

echo ""
echo "== 渲染验证（PNG 像素）=="
python3 - <<'PYEOF'
import zlib, struct, glob

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

def stats(path):
    w, h, out = decode(path)
    blue = yellow = bright = dark_bg = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            i = (y*w + x)*4
            r, g, b = out[i], out[i+1], out[i+2]
            if 60 < r < 130 and 100 < g < 170 and 160 < b < 235: blue += 1
            elif 200 < r < 250 and 150 < g < 210 and 60 < b < 130: yellow += 1
            if min(r, g, b) > 200: bright += 1
            # 窗口背景 #1e1e1e（(30,30,30)，壁纸蓝 (0,120,215) 除外）
            if 25 < r < 40 and 25 < g < 40 and 25 < b < 40: dark_bg += 1
    return w, h, blue, yellow, bright, dark_bg

w, h, blue, yellow, bright, dark = stats('/tmp/g1-ctrl-best.png')
print('control 主页 %dx%d: 蓝图标像素=%d 黄图标像素=%d 亮文字=%d 窗口深底=%d' % (w, h, blue, yellow, bright, dark))
print('  (蓝图标>400 且 窗口深底>1000 = 类别图标网格+深色主页渲染)' if blue > 400 and dark > 1000 else '  FAIL: 主页像素不足')

for page in ('nightlight', 'shortcuts', 'rules'):
    w, h, blue, yellow, bright, dark = stats('/tmp/g1-set-%s-best.png' % page)
    print('settings %-10s %dx%d: 亮文字=%d 深底=%d' % (page, w, h, bright, dark))
    print('  (亮文字>650 = 页面内容渲染)' if bright > 650 else '  FAIL: 页面像素不足')
PYEOF
echo "== 应用日志 =="
grep -avE 'locale|UTF-8|propagateSizeHints' /tmp/g1-ctrl-app.log | head -3
grep -avE 'locale|UTF-8|propagateSizeHints' /tmp/g1-set-rules-app.log | head -3

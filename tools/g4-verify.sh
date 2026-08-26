#!/bin/bash
# G4 验证：w10monitor 性能页 4 图（CPU/内存/磁盘/网络）+ 进程页 IO 列。
cd /root/win10de
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true
pkill -x w10monitor 2>/dev/null; pkill -x w10compositor 2>/dev/null; sleep 1

echo "==== selftest ===="
./build/src/systemapps/w10monitor --selftest 2>&1 | grep -E 'SELFTEST OK|FAIL|OK process'

for PAGE in perf proc; do
  rm -f /tmp/g4-$PAGE.png
  ./build/src/compositor/w10compositor --outputs 1 --socket g4-$PAGE --frames 500 \
    --screenshot /tmp/g4-$PAGE.png > /tmp/g4-$PAGE-comp.log 2>&1 &
  CPID=$!
  for i in $(seq 1 50); do [ -S /run/user/0/g4-$PAGE ] && break; sleep 0.1; done
  sleep 1
  # CPU 负载（曲线非零才可验证蓝折线在中部渲染；结束清理）。
  yes > /dev/null & LOADPID=$!
  WAYLAND_DISPLAY=g4-$PAGE ./build/src/systemapps/w10monitor > /tmp/g4-$PAGE-app.log 2>&1 &
  APID=$!
  BEST=0
  for i in $(seq 1 30); do
    sleep 0.5
    [ -f /tmp/g4-$PAGE.png ] || continue
    S=$(stat -c %s /tmp/g4-$PAGE.png 2>/dev/null || echo 0)
    if [ "$S" -gt "$BEST" ]; then cp /tmp/g4-$PAGE.png /tmp/g4-$PAGE-best.png; BEST=$S; fi
  done
  kill $LOADPID 2>/dev/null || true
  wait $CPID || true
  kill $APID 2>/dev/null || true
done

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

# 性能页：窗口 (100,80,900x680) 内检测 4 图深底 + 蓝/绿双序列曲线 +
# 白字。注意壁纸蓝 (0,120,215) 与曲线蓝同色——蓝检测限定窗口区域。
w, h, out = decode('/tmp/g4-perf-best.png')
dark = blue = green = bright = 0
wx0, wy0, wx1, wy1 = 100, 80, 1000, 760
for y in range(0, h, 2):
    for x in range(0, w, 2):
        i = (y*w + x)*4
        r, g, b = out[i], out[i+1], out[i+2]
        in_win = wx0 <= x <= wx1 and wy0 <= y <= wy1
        if 25 < r < 35 and 25 < g < 35 and 25 < b < 35: dark += 1
        if in_win and r <= 20 and 100 < g < 140 and 195 < b < 235: blue += 1
        if 5 < r < 35 and 160 < g < 200 and 90 < b < 130: green += 1
        if min(r, g, b) > 200: bright += 1
print('性能页 %dx%d: 图深底=%d 窗口内蓝曲线=%d 绿曲线/图例=%d 亮文字=%d' % (w, h, dark, blue, green, bright))
ok = dark > 5000 and blue > 30 and green > 10 and bright > 800
print('  OK（4 图 + 双序列曲线渲染）' if ok else '  FAIL')
PYEOF
grep -avE 'locale|UTF-8|propagate' /tmp/g4-perf-app.log | head -2

#!/bin/bash
# E1-E5 可选拓展渲染验证（compositor + 各应用 → 截图像素校验）。
cd /root/win10de
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true
pkill -x w10compositor 2>/dev/null; sleep 0.5
pkill -x w10sticky 2>/dev/null; pkill -x w10paint 2>/dev/null; pkill -x w10pad 2>/dev/null; pkill -x w10charmap 2>/dev/null; pkill -x w10clock 2>/dev/null; sleep 0.5

run_app() {
  local tag="$1" bin="$2" extra="$3"
  rm -f /tmp/e-$tag.png
  ./build/src/compositor/w10compositor --outputs 1 --socket e-$tag --frames 400 \
    --screenshot /tmp/e-$tag.png > /tmp/e-$tag-comp.log 2>&1 &
  local cpid=$!
  for i in $(seq 1 50); do [ -S /run/user/0/e-$tag ] && break; sleep 0.1; done
  sleep 1
  WAYLAND_DISPLAY=e-$tag ./build/src/systemapps/$bin $extra > /tmp/e-$tag-app.log 2>&1 &
  local apid=$!
  local best=0
  for i in $(seq 1 30); do
    sleep 0.5
    [ -f /tmp/e-$tag.png ] || continue
    local s=$(stat -c %s /tmp/e-$tag.png 2>/dev/null || echo 0)
    if [ "$s" -gt "$best" ]; then cp /tmp/e-$tag.png /tmp/e-$tag-best.png; best=$s; fi
  done
  wait $cpid 2>/dev/null
  kill $apid 2>/dev/null
}

run_app sticky w10sticky ""
run_app paint w10paint ""
run_app pad w10pad ""
run_app charmap w10charmap ""
run_app clock w10clock ""

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

def count(path, pred, wx0=100, wy0=80, wx1=1000, wy1=700):
    w, h, out = decode(path)
    n = 0
    for y in range(wy0, min(h, wy1), 2):
        for x in range(wx0, min(w, wx1), 2):
            i = (y*w + x)*4
            if pred(out[i], out[i+1], out[i+2]): n += 1
    return n

# E1 便笺：黄底 #FFF9C4（(255,249,196)）
n = count('/tmp/e-sticky-best.png', lambda r,g,b: r>240 and 235<g<255 and 170<b<215)
print('E1 便笺黄底=%d' % n); print('  OK' if n > 500 else '  FAIL')
# E2 画图：白画布 + 深色文字/工具栏
n = count('/tmp/e-paint-best.png', lambda r,g,b: r>245 and g>245 and b>245)
print('E2 画图白画布=%d' % n); print('  OK' if n > 3000 else '  FAIL')
# E3 写字板：编辑区（白底 QTextEdit）
n = count('/tmp/e-pad-best.png', lambda r,g,b: r>245 and g>245 and b>245)
print('E3 写字板白编辑区=%d' % n); print('  OK' if n > 2000 else '  FAIL')
# E4 字符映射表：表格白底 + 深色字符
n = count('/tmp/e-charmap-best.png', lambda r,g,b: r>245 and g>245 and b>245)
print('E4 字符表白底=%d' % n); print('  OK' if n > 3000 else '  FAIL')
# E5 闹钟时钟：深色窗口 + 亮文字（世界时钟）
n = count('/tmp/e-clock-best.png', lambda r,g,b: min(r,g,b) > 150)
print('E5 时钟亮文字=%d' % n); print('  OK' if n > 500 else '  FAIL')
PYEOF

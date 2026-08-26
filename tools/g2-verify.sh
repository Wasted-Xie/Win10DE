#!/bin/bash
# G2 验证：截图工具补全（区域/窗口/延时/交互构建 + 捕获核心回归）。
cd /root/win10de
export XDG_RUNTIME_DIR=/run/user/0
mkdir -p /run/user/0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus
pkill -x dbus-daemon 2>/dev/null; sleep 0.3
rm -f /run/user/0/bus
dbus-daemon --session --address=unix:path=/run/user/0/bus --fork 2>/dev/null || true
pkill -x w10screenshot 2>/dev/null; pkill -x w10settings 2>/dev/null; pkill -x w10compositor 2>/dev/null; sleep 1

echo "==== 1) selftest ===="
./build/src/systemapps/w10screenshot --selftest 2>&1 | grep -E 'OK|PASS|FAIL'

echo "==== 2) 全屏捕获回归 ===="
./build/src/compositor/w10compositor --outputs 1 --socket g2-full --frames 600 \
  > /tmp/g2-full-comp.log 2>&1 &
CP=$!
for i in $(seq 1 50); do [ -S /run/user/0/g2-full ] && break; sleep 0.1; done
sleep 1
WAYLAND_DISPLAY=g2-full ./build/src/systemapps/w10screenshot --fullscreen \
  --output HEADLESS-1 --out /tmp/g2-full.png 2>&1
echo "== 窗口捕获（w10settings 应用窗口）=="
WAYLAND_DISPLAY=g2-full ./build/src/systemapps/w10settings --page display \
  > /tmp/g2-set.log 2>&1 &
SP=$!
sleep 2
WAYLAND_DISPLAY=g2-full ./build/src/systemapps/w10screenshot --window w10settings \
  --out /tmp/g2-window.png 2>&1
echo "== 区域捕获 =="
WAYLAND_DISPLAY=g2-full ./build/src/systemapps/w10screenshot --region 100,80,400,300 \
  --out /tmp/g2-region.png 2>&1
echo "== 延时捕获（--delay 1）=="
WAYLAND_DISPLAY=g2-full ./build/src/systemapps/w10screenshot --delay 1 --fullscreen \
  --out /tmp/g2-delay.png 2>&1
kill $SP 2>/dev/null
wait $CP 2>/dev/null || true

echo "==== 3) PNG 尺寸/内容校验 ===="
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

def non_bg(path):
    w, h, out = decode(path)
    # 统计非壁纸色（蓝渐变底 0,120,215 附近）像素：窗口内容应含深色/亮色
    dark = bright = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            i = (y*w + x)*4
            r, g, b = out[i], out[i+1], out[i+2]
            if r < 60 and g < 60 and b < 70: dark += 1
            if min(r, g, b) > 200: bright += 1
    return w, h, dark, bright

w, h, _, _ = non_bg('/tmp/g2-full.png')
print('全屏: %dx%d (期望 1920x1080)' % (w, h))
print('  OK' if w == 1920 and h == 1080 else '  FAIL')

w, h, dark, bright = non_bg('/tmp/g2-window.png')
print('窗口: %dx%d 深色=%d 亮文字=%d' % (w, h, dark, bright))
print('  OK (含窗口内容)' if dark > 500 and bright > 200 else '  FAIL: 窗口捕获内容不足')

w, h, _, _ = non_bg('/tmp/g2-region.png')
print('区域: %dx%d (期望 400x300)' % (w, h))
print('  OK' if w == 400 and h == 300 else '  FAIL')

w, h, _, _ = non_bg('/tmp/g2-delay.png')
print('延时: %dx%d (期望 1920x1080)' % (w, h))
print('  OK' if w == 1920 and h == 1080 else '  FAIL')
PYEOF
echo "== 交互模式构建冒烟（offscreen，禁止连 WSLg/Windows 主屏幕）=="
# 审查（用户反馈）：裸调用会经 WSLg 连 Windows 主屏幕 Wayland，遮罩窗口
# 弹出到用户桌面——必须显式 QT_QPA_PLATFORM=offscreen（纯离屏，不连显示）。
timeout 2 env QT_QPA_PLATFORM=offscreen ./build/src/systemapps/w10screenshot 2>&1 | head -2
echo "(timeout 退出码 $? = 交互窗口保持运行即正常)" 

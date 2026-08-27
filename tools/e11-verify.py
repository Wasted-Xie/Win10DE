#!/usr/bin/env python3
# E11 日历渲染验证：深色背景 + 月历网格（多行日期按钮）+ 事件面板。
import sys
import zlib
import struct


def decode(path):
    data = open(path, 'rb').read()
    pos = 8
    idat = b''
    w = h = 0
    bitdepth = ct = 0
    while pos < len(data):
        ln = struct.unpack('>I', data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, bitdepth, ct = struct.unpack('>IIBB', chunk[:10])
        elif typ == b'IDAT':
            idat += chunk
        pos += 12 + ln
    raw = zlib.decompress(idat)
    bpp = 4 if ct == 6 else (3 if ct == 2 else 1)
    stride = w * bpp

    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        return a if pa <= pb and pa <= pc else (b if pb <= pc else c)

    out = bytearray()
    prev = bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                line[x] = (line[x] + paeth(a, b, c)) & 255
        out += line
        prev = line
    return w, h, bytes(out), bpp


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/e11-render.png'
    w, h, px, bpp = decode(path)

    def rgb(x, y):
        o = (y * w + x) * bpp
        return px[o], px[o + 1], px[o + 2]

    dark = today_green = 0
    total = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            r, g, b = rgb(x, y)
            total += 1
            if r < 70 and g < 70 and b < 70:
                dark += 1
            # 今天描边 #3DDC84（61,220,132）。
            if 40 <= r <= 90 and 190 <= g <= 245 and 100 <= b <= 160:
                today_green += 1
    print('size=%dx%d dark_ratio=%.2f today_green=%d' % (w, h, dark / total,
                                                         today_green))
    ok = True
    if dark / total < 0.5:
        print('FAIL: 深色背景占比过低')
        ok = False
    if today_green < 5:
        print('FAIL: 未找到今天日期描边')
        ok = False
    print('RENDER PASS' if ok else 'RENDER FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())

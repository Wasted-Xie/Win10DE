#!/usr/bin/env python3
# E6 录音机渲染验证：解码 PNG 并断言布局像素（深色背景/白色大圆按钮/红点）。
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
    print('dbg: %dx%d depth=%d ct=%d rawlen=%d expect=%d'
          % (w, h, bitdepth, ct, len(raw), h * (1 + stride)))

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
    return w, h, bytes(out)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/e6-render.png'
    w, h, px = decode(path)

    def rgb(x, y):
        o = (y * w + x) * 3
        return px[o], px[o + 1], px[o + 2]

    # 背景深色（四角 + 边缘采样）。
    dark = 0
    total = 0
    for y in range(0, h, 8):
        for x in range(0, w, 8):
            r, g, b = rgb(x, y)
            total += 1
            if r < 70 and g < 70 and b < 70:
                dark += 1
    # 白色大圆按钮（空闲态白底）。
    whites = 0
    for y in range(0, h, 3):
        for x in range(0, w, 3):
            r, g, b = rgb(x, y)
            if r > 235 and g > 235 and b > 235:
                whites += 1
    # 红点（按钮中心文本 ●，空闲态应为红色）。
    reds = 0
    for y in range(0, h, 3):
        for x in range(0, w, 3):
            r, g, b = rgb(x, y)
            if r > 120 and g < 80 and b < 80:
                reds += 1
    print('size=%dx%d dark_ratio=%.2f white_px=%d red_px=%d'
          % (w, h, dark / total, whites, reds))
    ok = True
    if dark / total < 0.5:
        print('FAIL: 深色背景占比过低')
        ok = False
    if whites < 200:  # 110px 圆按钮，3px 采样应有数百白色点
        print('FAIL: 未找到白色大圆按钮')
        ok = False
    if reds < 5:  # 空闲态红点应存在
        print('FAIL: 未找到按钮红点')
        ok = False
    print('RENDER PASS' if ok else 'RENDER FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())

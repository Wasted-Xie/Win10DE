#!/usr/bin/env python3
# E8 屏幕键盘渲染验证：深色背景 + 键帽网格（灰键帽行 + 蓝色修饰键）。
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
    path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/e8-render.png'
    w, h, px, bpp = decode(path)

    def rgb(x, y):
        o = (y * w + x) * bpp
        return px[o], px[o + 1], px[o + 2]

    dark = keycap = blue = 0
    total = 0
    for y in range(0, h, 3):
        for x in range(0, w, 3):
            r, g, b = rgb(x, y)
            total += 1
            if r < 70 and g < 70 and b < 70:
                dark += 1
            # 灰键帽 #3D3D3D（61,61,61）。
            if 50 <= r <= 75 and 50 <= g <= 75 and 50 <= b <= 75:
                keycap += 1
            # 蓝色修饰键 #2E5A8F（46,90,143）。
            if 30 <= r <= 70 and 75 <= g <= 110 and 125 <= b <= 160:
                blue += 1
    print('size=%dx%d dark=%d keycap=%d blue_mods=%d'
          % (w, h, dark, keycap, blue))
    ok = True
    if keycap < 300:  # 5 行键帽网格（3px 采样应有大量灰键帽像素）
        print('FAIL: 键帽网格过少')
        ok = False
    if blue < 20:  # Ctrl/Alt/Shift 蓝色修饰键
        print('FAIL: 未找到蓝色修饰键')
        ok = False
    print('RENDER PASS' if ok else 'RENDER FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())

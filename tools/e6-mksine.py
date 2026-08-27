#!/usr/bin/env python3
# 生成 1s 440Hz 正弦波（S16LE 44100 mono）到 /tmp/e6-sine.raw。
import math
import struct

n = 44100
out = bytearray()
for i in range(n):
    v = int(12000 * math.sin(2 * math.pi * 440 * i / 44100))
    out += struct.pack('<h', v)
with open('/tmp/e6-sine.raw', 'wb') as f:
    f.write(bytes(out))
print('sine bytes:', len(out))

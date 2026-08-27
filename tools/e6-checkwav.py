#!/usr/bin/env python3
# 校验录音 WAV：头标记 + PCM 长度 + 非全零（正弦波能量）。
import sys


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/e6-rec.wav'
    data = open(path, 'rb').read()
    ok = True
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        ok = False
        print('FAIL: RIFF/WAVE 标记')
    if len(data) < 44:
        ok = False
        print('FAIL: 过短')
    if ok:
        pcm = data[44:]
        if len(pcm) < 44100 * 2:  # ≥1s（允许 pacat prebuf 时序损失）
            ok = False
            print('FAIL: 时长不足', len(pcm))
        nonzero = sum(1 for b in pcm if b != 0)
        print('wav size=%d pcm=%d nonzero_bytes=%d' % (len(data), len(pcm),
                                                       nonzero))
        if nonzero < 1000:
            ok = False
            print('FAIL: PCM 全零/近零（未录到输入）')
    print('WAV CHECK PASS' if ok else 'WAV CHECK FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())

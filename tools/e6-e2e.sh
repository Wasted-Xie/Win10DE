#!/bin/bash
# E6 录音机端到端验证：真 PulseAudio 服务（system + 匿名认证配置）+
# pipe-source 正弦波 → 录音 → 校验 WAV 落盘（头正确 + PCM 非全零 + 时长）。
set -u
cd /root/win10de/build
export PULSE_SERVER=unix:/var/run/pulse/native
BIN=./src/systemapps/w10recorder
FAIL=0

echo "==== 0) 前置检查 ===="
[ -x "$BIN" ] || { echo "FAIL: w10recorder 未构建"; exit 1; }
which pulseaudio pactl >/dev/null || { echo "FAIL: 无 pulseaudio/pactl"; exit 1; }

echo "==== 1) 生成 440Hz 正弦波（1s，S16LE 44100 mono）===="
python3 /mnt/c/Projects/Win10DE/tools/e6-mksine.py

echo "==== 2) 启动 PulseAudio（system + 匿名认证配置）===="
pkill -x pulseaudio 2>/dev/null; sleep 0.5
rm -f /var/run/pulse/pid
id pulse >/dev/null 2>&1 || useradd -r -s /usr/bin/nologin pulse
mkdir -p /var/run/pulse
cp /mnt/c/Projects/Win10DE/tools/e6-pulse-test.pa /tmp/e6-pa.pa
pulseaudio --daemonize=yes --system --exit-idle-time=-1 --disallow-exit=1 \
  --disallow-module-loading=1 -n -F /tmp/e6-pa.pa >/tmp/e6-pulse.log 2>&1
for i in $(seq 1 20); do [ -S /var/run/pulse/native ] && break; sleep 0.2; done
if [ ! -S /var/run/pulse/native ]; then
  echo "FAIL: pulse socket 未出现"; cat /tmp/e6-pulse.log; exit 1
fi
sleep 0.5
echo "OK pulse running"

echo "==== 3) 正弦波播放到 testnull（后台循环）===="
(while true; do cat /tmp/e6-sine.raw; done | \
  pacat --playback --device=testnull --format=s16le --rate=44100 \
        --channels=1 >/tmp/e6-pacat.log 2>&1) &
PACAT_PID=$!
sleep 0.5
kill -0 $PACAT_PID 2>/dev/null && echo "OK pacat playing" \
  || { echo "FAIL: pacat 未运行"; cat /tmp/e6-pacat.log; FAIL=1; }

echo "==== 4) 录音 3s（w10recorder --record-test，源=testnull.monitor）===="
rm -f /tmp/e6-rec.wav
timeout 30 "$BIN" --record-test /tmp/e6-rec.wav 3 testnull.monitor \
  >/tmp/e6-rt.log 2>&1
RC=$?
echo "record-test exit=$RC"
grep -E "RECORD TEST|recording" /tmp/e6-rt.log
if [ $RC -ne 0 ]; then cat /tmp/e6-rt.log; FAIL=1; fi

echo "==== 5) 校验 WAV ===="
if [ -f /tmp/e6-rec.wav ]; then
  python3 /mnt/c/Projects/Win10DE/tools/e6-checkwav.py /tmp/e6-rec.wav || FAIL=1
else
  echo "FAIL: 未生成录音文件"; FAIL=1
fi

echo "==== 6) 清理 ===="
kill $PACAT_PID 2>/dev/null || true
pkill -x pulseaudio 2>/dev/null || true

[ $FAIL -eq 0 ] && echo "E6 END-TO-END PASS" || echo "E6 END-TO-END FAIL"
exit $FAIL

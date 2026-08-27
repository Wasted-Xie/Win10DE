#!/bin/bash
# E6-E11 最终验证汇总。
cd /root/win10de/build
FAIL=0
run() {
    local name="$1"; shift
    echo "==== $name ===="
    "$@" >/tmp/final-$name.log 2>&1
    local rc=$?
    if [ $rc -eq 0 ]; then
        grep -E "SELFTEST PASS|RENDER OK" /tmp/final-$name.log | head -2
        echo "  -> PASS"
    else
        echo "  -> FAIL rc=$rc"; tail -5 /tmp/final-$name.log; FAIL=1
    fi
}
run recorder-selftest ./src/systemapps/w10recorder --selftest
run recorder-render ./src/systemapps/w10recorder --render /tmp/f-e6.png
run cleanup-selftest ./src/systemapps/w10cleanup --selftest
run cleanup-render ./src/systemapps/w10cleanup --render /tmp/f-e7.png
run osk-selftest ./src/systemapps/w10osk --selftest
run osk-render ./src/systemapps/w10osk --render /tmp/f-e8.png
run rdp-selftest ./src/systemapps/w10rdp --selftest
run rdp-render ./src/systemapps/w10rdp --render /tmp/f-e9.png
run disks-selftest ./src/systemapps/w10disks --selftest
run disks-render ./src/systemapps/w10disks --render /tmp/f-e10.png
run calendar-selftest ./src/systemapps/w10calendar --selftest
run calendar-render ./src/systemapps/w10calendar --render /tmp/f-e11.png
[ $FAIL -eq 0 ] && echo "ALL PASS" || echo "HAS FAILURES"
exit $FAIL

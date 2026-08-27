#!/bin/bash
# E1-E5 可选拓展 selftest 汇总
cd /root/win10de/build
for app in w10sticky w10paint w10pad w10charmap w10clock; do
  echo "== $app =="
  ./src/systemapps/$app --selftest 2>&1 | grep -E 'OK|PASS|FAIL'
done

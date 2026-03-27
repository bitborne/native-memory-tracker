#!/bin/bash
adb connect 192.168.122.22:6520
adb root
adb push app/build/intermediates/cxx/Debug/53655p1s/obj/x86_64/pfn_helper /data/local/tmp/
adb shell chmod +x /data/local/tmp/pfn_helper
adb shell chmod 666 /sys/kernel/mm/page_idle/bitmap
adb shell setenforce 0
adb shell pkill -9 pfn

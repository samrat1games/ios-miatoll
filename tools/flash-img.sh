#!/bin/bash
# Flash an existing boot image, reboot, wait for the phone to come back to fastboot by itself (sweetbl=N in its
# boot-args), then pull the persistent log from recovery.   tools/flash-img.sh IMG LOG.txt
set -uo pipefail
cd "$(dirname "$0")/.."
img=$1; log=$2
if adb devices | grep -q "recovery\|device$"; then adb reboot bootloader; sleep 12; fi
for i in $(seq 1 20); do fastboot devices | grep -q fastboot && break; sleep 3; done
fastboot flash boot "$img" 2>&1 | tail -1
fastboot reboot >/dev/null 2>&1
echo "rebooted at $(date +%s)"
for i in $(seq 1 20); do fastboot devices | grep -q fastboot || break; sleep 2; done
for i in $(seq 1 120); do fastboot devices | grep -q fastboot && break; sleep 5; done
fastboot devices | grep -q fastboot || { echo "phone did not return to fastboot" >&2; exit 2; }
fastboot reboot recovery >/dev/null 2>&1
for i in $(seq 1 40); do adb devices | grep -q recovery && break; sleep 3; done
for i in $(seq 1 20); do adb shell "ls /dev/block/by-name/userdata" >/dev/null 2>&1 && break; sleep 3; done
tools/sweet-log-pull.sh "$log" 2>&1 | tail -1

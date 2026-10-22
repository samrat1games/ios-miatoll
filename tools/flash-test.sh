#!/bin/bash
# Flash the kernel built by CI (xnu/out) with extra boot-args, reboot, wait until the phone returns to fastboot
# on its own (boot-arg sweetbl=N), then pull the persistent log from recovery.
#   tools/flash-test.sh NAME "extra boot-args" LOG.txt
set -euo pipefail
cd "$(dirname "$0")/.."
name=$1; extra=${2:-}; log=${3:-logs/last.txt}
B="debug=0xc serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_hz=60 sweetfb_noconsole=1 sweetusb=2 $extra"
python3 tools/mkpayload.py xnu/out/kernel.development.sweet shim/out/miatoll.adt "shim/out/payload-$name.bin" --cmdline "$B" >/dev/null
(cd shim && ./build.sh "../shim/out/payload-$name.bin" >/dev/null 2>&1 && cp out/shim-boot.img "out/shim-boot-$name.img")
fastboot flash boot "shim/out/shim-boot-$name.img" 2>&1 | tail -1
t0=$(date +%s)
fastboot reboot >/dev/null 2>&1
echo "rebooted at $t0"
for i in $(seq 1 120); do fastboot devices | grep -q fastboot && break; sleep 5; done
fastboot devices | grep -q fastboot || { echo "phone did not return to fastboot" >&2; exit 2; }
fastboot reboot recovery >/dev/null 2>&1
for i in $(seq 1 40); do adb devices | grep -q recovery && break; sleep 3; done
sleep 5
tools/sweet-log-pull.sh "$log" 2>&1 | tail -1

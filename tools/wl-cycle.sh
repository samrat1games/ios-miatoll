#!/bin/bash
# One Wi-Fi test cycle: wait for the newest xnu CI run, build a sweetmodem=1 boot image, fastboot boot it (temporary,
# nothing is flashed), wait for the WLAN firmware outcome and write the interesting log lines to logs/wl-last.txt.
#   tools/wl-cycle.sh        (phone in fastboot, tools/sweet-bridge-macos.sh running)
set -e
cd "$(dirname "$0")/.."
# the run of the current commit (the newest run may still be an older one right after a push)
head=$(git rev-parse HEAD)
rid=${WL_RID:-}
for i in $(seq 1 60); do [ -n "$rid" ] && break
	rid=$(gh run list --workflow xnu.yml --commit "$head" -L 1 --json databaseId -q '.[0].databaseId' 2>/dev/null)
	[ -n "$rid" ] && break
	sleep 10
done
[ -n "$rid" ] || { echo "no xnu run for $head"; exit 1; }
echo "xnu run $rid"
sed -e 's/sweetmodem=0/sweetmodem=1/' -e "s/^rid=.*/rid=$rid/" tools/ci-boot.sh > tools/.wl-boot.sh
rm -rf /tmp/kdl
rm -f shim/out/shim-boot-wl.img
EXTRA_ARGS="${EXTRA_ARGS:-gpu=1 gpu_with_modem=1 adreno_pause=0}" bash tools/.wl-boot.sh wl >/dev/null; [ -s shim/out/shim-boot-wl.img ] && [ -s /tmp/kdl/kernel.development.sweet ] || { rm -f tools/.wl-boot.sh; echo "build of the boot image failed"; exit 1; }
rm -f tools/.wl-boot.sh
if ! fastboot devices | grep -q fastboot; then
	echo "phone not in fastboot: asking iOS to restart into the bootloader (UDP 5141)"
	printf bootloader | nc -u -w1 192.168.7.2 5141 || true
	for i in $(seq 1 24); do fastboot devices | grep -q fastboot && break; sleep 5; done
	fastboot devices | grep -q fastboot || { echo "phone did not reach fastboot: put it there by hand (Power+VolDown)"; exit 1; }
fi
start=$(($(wc -l < logs/bridge.log) + 1))
fastboot boot shim/out/shim-boot-wl.img
echo "booted, waiting for the phone's log..."
for i in $(seq 1 30); do
	[ "$(wc -l < logs/bridge.log)" -ge "$start" ] && break
	sleep 10
done
if [ "$(wc -l < logs/bridge.log)" -lt "$start" ]; then
	echo "NO LOG after 5 min: is tools/sweet-bridge-macos.sh running and does it see the phone?"
	exit 1
fi
echo "log is coming, waiting for the WLAN firmware (up to 15 min)..."
for i in $(seq 1 90); do
	tail -n +$start logs/bridge.log | grep -a -q -E 'FATAL, crash|FW_READY|no FW_READY|HTC READY' && break
	sleep 10
done
sleep 30
tail -n +$start logs/bridge.log | grep -a -E 'adreno: summary|THE GPU|FATAL|wlanmdsp.mbn (done|stalled|ACK)|RESUME_TX|64K chunk|asked again|unexpected packet|OACK|raw RRQ|confirm_rx|non-request|SWEET (WLFW|WLCE|WLSMMU)|intent for|refused' |
	grep -v 'done, 0 bytes' | cut -c1-200 | awk '!seen[$0]++' | tail -60 > logs/wl-last.txt
cat logs/wl-last.txt

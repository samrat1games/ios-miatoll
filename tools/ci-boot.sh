#!/bin/bash
# Wait for the latest xnu CI run, download its kernel, build shim-boot-NAME.img and (with --flash) flash it.
#   tools/ci-boot.sh NAME [--flash]    (phone in fastboot for --flash)
set -euo pipefail
cd "$(dirname "$0")/.."
name=$1
sleep 20
rid=$(gh run list -L 1 --json databaseId -q '.[0].databaseId')
until [ "$(gh run view "$rid" --json status -q .status)" = completed ]; do sleep 15; done
[ "$(gh run view "$rid" --json conclusion -q .conclusion)" = success ] || { echo "CI run $rid failed" >&2; exit 1; }
rm -rf /tmp/kdl
for i in 1 2 3; do gh run download "$rid" -n xnu-kernel -D /tmp/kdl && break; sleep 5; done
cp /tmp/kdl/kernel.development.sweet xnu/out/
B="serial=3 cpus=8 keepsyms=1 -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_hz=60 sweetfb_noconsole=1 sweetusb=2 sweetmodem=0 sweetrtc=${SWEETRTC:-1786732036} ${EXTRA_ARGS:-}"
python3 tools/mkpayload.py xnu/out/kernel.development.sweet shim/out/miatoll.adt "shim/out/payload-$name.bin" --cmdline "$B" >/dev/null
(cd shim && ./build.sh "../shim/out/payload-$name.bin" >/dev/null 2>&1 && cp out/shim-boot.img "out/shim-boot-$name.img")
echo "built shim/out/shim-boot-$name.img"
if [ "${2:-}" = --flash ]; then
	fastboot flash boot "shim/out/shim-boot-$name.img" 2>&1 | tail -1
	fastboot reboot 2>&1 | tail -1
fi

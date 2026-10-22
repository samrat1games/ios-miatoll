#!/bin/bash
# A copy of the live iOS root WITHOUT anything personal (the modem's EFS copies hold the phone's IMEI and
# calibration), ready to be handed to testers. Needs sudo (the image is mounted with ownership).
#   tools/make-shareable-root.sh [SRC.img] [OUT.img]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
src=${1:-"$here/../research/ipsw-x/ios16-live.img"}
out=${2:-"$here/../research/ipsw-x/ios16-root-shared.img"}
[[ ! -e $out ]] || { echo "refusing to overwrite $out" >&2; exit 1; }
cp -c "$src" "$out"
sudo -v
mp=$(mktemp -d /tmp/shareroot.XXXXXX)
dev=$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "$out" 2>/dev/null | awk '{print $1; exit}')
trap 'diskutil unmount "$mp" >/dev/null 2>&1 || true; hdiutil detach "$dev" >/dev/null 2>&1 || true' EXIT
diskutil mount -mountOptions owners -mountPoint "$mp" "$dev" >/dev/null
# the author's phone data: EFS copies (IMEI, calibration), logs, crash reports, caches of the author's boots
sudo rm -rf "$mp/private/var/miatoll/efs"
sudo rm -rf "$mp/private/var/log"/* "$mp/private/var/mobile/Library/Logs"/* "$mp/private/var/mobile/Library/Caches"/* 2>/dev/null || true
sudo rm -rf "$mp/private/var/root/Library/Caches"/* 2>/dev/null || true
sudo rm -rf "$mp/.fseventsd" "$mp/.Spotlight-V100" "$mp/.Trashes" 2>/dev/null || true
diskutil unmount "$mp" >/dev/null
fsck_hfs -f -n "${dev/disk/rdisk}"
hdiutil detach "$dev" >/dev/null 2>&1; trap - EXIT
echo "OK: $out (no personal data from the author's phone)"

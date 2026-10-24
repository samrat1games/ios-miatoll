#!/bin/bash
# A copy of the live iOS root WITHOUT the author's phone data (the modem's EFS copies hold the phone's IMEI and
# calibration), ready to be handed to testers. Caches and the rest stay: without them SpringBoard crash-loops
# (clonefile errors). The image is mounted without ownership: deleting a directory needs none, nothing else changes.
#   tools/make-shareable-root.sh [SRC.img] [OUT.img]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
src=${1:-"$here/../research/ipsw-x/ios16-live.img"}
out=${2:-"$here/../research/ipsw-x/ios16-root-shared.img"}
[[ ! -e $out ]] || { echo "refusing to overwrite $out" >&2; exit 1; }
cp -c "$src" "$out"
mp=$(mktemp -d /tmp/shareroot.XXXXXX)
dev=$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "$out" 2>/dev/null | awk '{print $1; exit}')
trap 'diskutil unmount "$mp" >/dev/null 2>&1 || true; hdiutil detach "$dev" >/dev/null 2>&1 || true' EXIT
diskutil mount -mountPoint "$mp" "$dev" >/dev/null
rm -rf "$mp/private/var/miatoll/efs"
ls "$mp/private/var/miatoll" || true
diskutil unmount "$mp" >/dev/null
fsck_hfs -f -n "${dev/disk/rdisk}"
hdiutil detach "$dev" >/dev/null 2>&1; trap - EXIT
echo "OK: $out"

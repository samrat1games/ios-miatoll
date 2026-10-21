#!/bin/bash
# Mount the pulled phone image, apply tools/miatoll-live-tweaks.py, check it. Run as yourself (not sudo).
#   tools/tweak-live-macos.sh [IMAGE]       default research/ipsw-x/ios16-live.img
set -euo pipefail
img=${1:-"$(dirname "$0")/../research/ipsw-x/ios16-live.img"}
sudo -v
mp=$(mktemp -d /tmp/iosroot.XXXXXX)
dev=$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "$img" 2>/dev/null | awk '{print $1; exit}')
trap 'diskutil unmount "$mp" >/dev/null 2>&1 || true; hdiutil detach "$dev" >/dev/null 2>&1 || true' EXIT
diskutil mount -mountOptions owners -mountPoint "$mp" "$dev" >/dev/null
mount | grep -q "$mp.*noowners" && { echo "mounted without ownership: aborting" >&2; exit 1; }
sudo python3 "$(dirname "$0")/miatoll-live-tweaks.py" "$mp"
sudo rm -rf "$mp/.fseventsd" "$mp/.Spotlight-V100" "$mp/.Trashes" 2>/dev/null || true
diskutil unmount "$mp" >/dev/null
fsck_hfs -f -n "${dev/disk/rdisk}"
hdiutil detach "$dev" >/dev/null 2>&1; trap - EXIT
echo "OK: $img tweaked"

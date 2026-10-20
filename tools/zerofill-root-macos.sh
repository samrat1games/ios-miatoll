#!/bin/bash
# Zero the free space inside the root image so img2simg can skip it (run as yourself, not sudo).
#   tools/zerofill-root-macos.sh [IMAGE]
set -euo pipefail
img=${1:-"$(dirname "$0")/../research/ipsw-x/ios16-root.img"}
sudo -v
mp=$(mktemp -d /tmp/iosroot.XXXXXX)
hdiutil attach -owners on -nobrowse -imagekey diskimage-class=CRawDiskImage -mountpoint "$mp" "$img" >/dev/null
dev=$(diskutil info "$mp" | awk '/Device Node/{print $3}')
trap 'diskutil unmount "$mp" >/dev/null 2>&1 || true; hdiutil detach "$dev" >/dev/null 2>&1 || true' EXIT
sudo dd if=/dev/zero of="$mp/.ZERO" bs=1m 2>&1 | tail -1 || true   # ends with "No space left on device"
sudo sync
sudo rm -f "$mp/.ZERO"
sudo rm -rf "$mp/.fseventsd" "$mp/.Spotlight-V100" "$mp/.Trashes" 2>/dev/null || true
df -h "$mp" | tail -1
diskutil unmount "$mp" >/dev/null
fsck_hfs -f -n "${dev/disk/rdisk}"
hdiutil detach "$dev" >/dev/null 2>&1; trap - EXIT
echo "OK: free space zeroed"

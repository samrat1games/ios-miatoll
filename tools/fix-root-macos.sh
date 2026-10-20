#!/bin/bash
# Decompress the iOS root image in place (see tools/decompress-root.py).
#   sudo tools/fix-root-macos.sh [IMAGE]     (default research/ipsw-x/ios16-root.img)
set -euo pipefail
img=${1:-"$(dirname "$0")/../research/ipsw-x/ios16-root.img"}
mp=$(mktemp -d /tmp/iosroot.XXXXXX)
hdiutil attach -owners on -nobrowse -imagekey diskimage-class=CRawDiskImage -mountpoint "$mp" "$img" >/dev/null
dev=$(diskutil info "$mp" | awk '/Device Node/{print $3}')
trap 'diskutil unmount "$mp" >/dev/null 2>&1 || true; hdiutil detach "$dev" >/dev/null 2>&1 || true' EXIT
python3 "$(dirname "$0")/decompress-root.py" "$mp"
rm -rf "$mp/.fseventsd" "$mp/.Spotlight-V100" "$mp/.Trashes" 2>/dev/null || true
df -h "$mp" | tail -1
diskutil unmount "$mp" >/dev/null
fsck_hfs -f -n "${dev/disk/rdisk}"
hdiutil detach "$dev" >/dev/null 2>&1; trap - EXIT
echo "OK: root image decompressed and checked"

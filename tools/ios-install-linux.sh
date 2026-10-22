#!/bin/bash
# Linux: from the IPSW to a flashable iOS root, and the flashing. Needs: apfs-fuse, hfsprogs (mkfs.hfsplus),
# rsync, fastboot, a kernel with hfsplus, sudo; ~40 GB of free space.
#   tools/ios-install-linux.sh iPhone10,3_16.7.16_20H392_Restore.ipsw ios-miatoll-boot-v0.1.img [WORKDIR]
set -euo pipefail
ipsw=${1:?usage: ios-install-linux.sh IPSW BOOT.img [WORKDIR]}
boot=${2:?usage: ios-install-linux.sh IPSW BOOT.img [WORKDIR]}
work=${3:-./ios-work}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$work"
for f in 087-86698-044.dmg 087-86593-047.dmg 087-86715-047.dmg; do
	[[ -e $work/$f ]] || unzip -o -j "$ipsw" "$f" -d "$work" >/dev/null || { echo "$f is not in the IPSW: it must be iPhone X (iPhone10,3) 16.7.16" >&2; exit 1; }
done
[[ -e $work/ios16-root.img ]] || "$here/mkiosroot.sh" "$work/087-86698-044.dmg" "$work/087-86593-047.dmg" "$work/087-86715-047.dmg" "$work/ios16-root.img"
[[ -e $work/ios16-root.simg ]] || python3 "$here/img2simg.py" "$work/ios16-root.img" "$work/ios16-root.simg"
read -r -p "The phone in fastboot mode (Power + Vol-). Android's userdata will be ERASED. Type yes to flash: " a
[[ $a == yes ]] || exit 1
fastboot flash userdata "$work/ios16-root.simg"
fastboot flash boot "$boot"
fastboot reboot

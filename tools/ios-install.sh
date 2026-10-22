#!/bin/bash
# One command from the IPSW to the flashable iOS root: unpack the three disk images of iPhone X 16.7.16 (20H392),
# build the root (needs sudo, ~14 GB, a while), make a fastboot image.
#   tools/ios-install.sh iPhone10,3_16.7.16_20H392_Restore.ipsw [WORKDIR]
# then, with the phone in fastboot:   fastboot flash userdata WORKDIR/ios16-root.simg
set -euo pipefail
ipsw=${1:?usage: ios-install.sh IPSW [WORKDIR]}
work=${2:-./ios-work}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$work"
for f in 087-86698-044.dmg 087-86593-047.dmg 087-86715-047.dmg; do
	[[ -e $work/$f ]] || unzip -o -j "$ipsw" "$f" -d "$work" >/dev/null || { echo "$f is not in the IPSW: it must be iPhone X (iPhone10,3) 16.7.16" >&2; exit 1; }
done
[[ -e $work/ios16-root.img ]] || "$here/mkiosroot-macos.sh" "$work/087-86698-044.dmg" "$work/087-86593-047.dmg" "$work/087-86715-047.dmg" "$work/ios16-root.img"
[[ -e $work/ios16-root.simg ]] || python3 "$here/img2simg.py" "$work/ios16-root.img" "$work/ios16-root.simg"
echo "Ready. Put the phone in fastboot, then:  fastboot flash userdata $work/ios16-root.simg"
echo "(this erases Android's userdata)"

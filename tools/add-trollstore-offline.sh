#!/bin/bash
# Put TrollStore Persistence Helper into a COPY of ios16-root-shared.img and cut it into flashable parts.
#   sudo tools/add-trollstore-offline.sh
# Does not touch the original image. Output: dist/raw-ts/ios16-root.part01..08.simg (flash with fastboot -S 512M flash userdata).
set -euo pipefail
cd "$(dirname "$0")/.."
src=research/ipsw-x/ios16-root-shared.img
dst=research/ipsw-x/ios16-root-ts.img
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
rm -f "$dst"
cp -c "$src" "$dst"                     # APFS clone: no extra space until written
dev=$(hdiutil attach -nobrowse -noverify -imagekey diskimage-class=CRawDiskImage "$dst" | awk 'NR==1{print $1}')
vol=$(diskutil info "$dev" | awk -F': *' '/Mount Point/{print $2}')
trap 'hdiutil detach "$dev" >/dev/null 2>&1 || true' EXIT
diskutil enableOwnership "$dev" >/dev/null 2>&1 || true
mount -u -o owners "$vol" 2>/dev/null || true
python3 - "$vol" <<'EOF'
import importlib.util, os, sys
spec = importlib.util.spec_from_file_location('lt', 'tools/miatoll-live-tweaks.py')
lt = importlib.util.module_from_spec(spec)
sys.argv = ['x', sys.argv[1]]
spec.loader.exec_module(lt)
lt.trollstore(sys.argv[1], os.path.abspath('apps/trollstorehelper.deb'))
EOF
sync
hdiutil detach "$dev"; trap - EXIT
mkdir -p dist/raw-ts
python3 tools/simg_split.py "$dst" dist/raw-ts 1800
chown -R "${SUDO_USER:-root}" dist/raw-ts
echo "ready: dist/raw-ts"

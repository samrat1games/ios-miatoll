#!/bin/bash
# Copy unpacked .app bundles into /Applications of a mounted iOS root, as system apps.
#
#   add-apps.sh MOUNTED_ROOT [PAYLOAD_DIR]        (default PAYLOAD_DIR: apps/stage/Payload)
#
# Unpack an .ipa first:  unzip -q App.ipa -d apps/stage   (apps/ is not in git).
# There is no installer on this port and AMFI/sandbox are permissive, so a
# bundle only has to exist under /Applications: lsd registers it at boot.
# Needs arm64 (not arm64e) iOS 16 binaries. Run as root for the right owner.
set -euo pipefail
root=${1:?usage: add-apps.sh MOUNTED_ROOT [PAYLOAD_DIR]}
src=${2:-"$(dirname "$0")/../apps/stage/Payload"}
[[ -d $root/Applications ]] || { echo "no $root/Applications: not an iOS root" >&2; exit 1; }
shopt -s nullglob
apps=("$src"/*.app)
[[ ${#apps[@]} -gt 0 ]] || { echo "no .app in $src" >&2; exit 1; }
for a in "${apps[@]}"; do
	name=$(basename "$a")
	exe=$(plutil -extract CFBundleExecutable raw "$a/Info.plist")
	arch=$(lipo -archs "$a/$exe" 2>/dev/null || file -b "$a/$exe")
	case $arch in *arm64e*) echo "warning: $name is arm64e; this port has no PAC" >&2;; esac
	echo "installing $name ($arch) -> /Applications"
	rm -rf "${root:?}/Applications/$name"
	ditto "$a" "$root/Applications/$name"
	if [[ $EUID -eq 0 ]]; then
		chown -R 0:0 "$root/Applications/$name"
	else
		echo "warning: not root, owner left as $(id -un)" >&2
	fi
	chmod -R u+rwX,go+rX,go-w "$root/Applications/$name"
	chmod 755 "$root/Applications/$name/$exe"
done

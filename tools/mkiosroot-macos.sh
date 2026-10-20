#!/bin/bash
# macOS port of mkiosroot.sh: build the SWEET iOS root as a raw, case-sensitive,
# journaled HFS+ image from the three IPSW disk images.
#
#   mkiosroot-macos.sh OS.dmg SYSTEMOS.dmg APPOS.dmg OUT.img [SIZE_GB]   (default 16)
#
# macOS reads the iOS APFS volumes (and their compression) natively, so there is
# no apfs-fuse. Differences from the Linux script:
#   * newfs_hfs -s -J on a plain file = raw volume with no partition map;
#   * hdiutil attach -owners on, so uid/gid/modes are honoured (needs sudo);
#   * ditto (hard links, owners, modes, symlinks kept; no HFS compression, so
#     XNU's HFS sees plain data);
#   * Spotlight/FSEvents/Trash metadata is removed before the volume is detached.
# Mechanics (format, owners, fsck) tested on a 64 MB image; the copy steps are
# NOT yet run against a real IPSW.
set -euo pipefail

[[ $# -ge 4 ]] || { sed -n '2,6p' "$0"; exit 2; }
os=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
sysos=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
appos=$(cd "$(dirname "$3")" && pwd)/$(basename "$3")
out=$4 size_gb=${5:-16}
[[ ! -e $out ]] || { echo "refusing to overwrite $out" >&2; exit 1; }
sudo -v

work=$(mktemp -d /tmp/mkiosroot.XXXXXX)
disks=()
cleanup() {
	for d in ${disks[@]+"${disks[@]}"}; do hdiutil detach "$d" -force >/dev/null 2>&1 || true; done
	rmdir "$work"/* "$work" 2>/dev/null || true
}
trap cleanup EXIT

# Attach a UDIF or raw APFS image read-only; print the mount point.
attach() {
	local img=$1 name=$2 mp="$work/$2"
	mkdir "$mp"
	local line
	line=$(hdiutil attach -readonly -nobrowse -noverify -mountpoint "$mp" "$img" 2>/dev/null | tail -1) ||
	line=$(hdiutil attach -readonly -nobrowse -noverify -imagekey diskimage-class=CRawDiskImage -mountpoint "$mp" "$img" | tail -1)
	disks+=("$mp")
	[[ -d $mp/System || -d $mp/root/System ]] || echo "warning: $mp has no System dir" >&2
	echo "$mp"
}
os_mp=$(attach "$os" os)
sys_mp=$(attach "$sysos" sysos)
app_mp=$(attach "$appos" appos)
# the Linux script copies <volume>/root; on macOS the volume itself is the root
rootof() { if [[ -d $1/root/System ]]; then echo "$1/root"; else echo "$1"; fi; }

truncate -s "${size_gb}G" "$out"
# newfs_hfs needs a device node, not a file: format via a temporary attach.
dev=$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "$out" 2>/dev/null | awk '{print $1; exit}')
newfs_hfs -s -J -v iOSRoot "${dev/disk/rdisk}" >/dev/null
hdiutil detach "$dev" >/dev/null 2>&1
root="$work/root"; mkdir "$root"
hdiutil attach -owners on -nobrowse -imagekey diskimage-class=CRawDiskImage -mountpoint "$root" "$out" >/dev/null 2>&1
rootdev=$(diskutil info "$root" | awk '/Device Node/{print $3}')
disks+=("$root")
mdutil -i off "$root" >/dev/null 2>&1 || true
sudo touch "$root/.metadata_never_index"

copy() {
	echo "copying $1 -> ${2#"$root"}"
	sudo mkdir -p "$2"
	sudo ditto "$1" "$2"
}
copy "$(rootof "$os_mp")" "$root"
copy "$(rootof "$sys_mp")" "$root/private/preboot/Cryptexes/OS"
copy "$(rootof "$app_mp")" "$root/private/preboot/Cryptexes/App"

# Shared cache where dyld looks at ignition level 1: hard links.
dsc=private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld
sudo mkdir -p "$root/System/Library/Caches/com.apple.dyld"
for f in "$root/$dsc"/*; do sudo ln "$f" "$root/System/Library/Caches/com.apple.dyld/${f##*/}"; done

# Hard-link every cryptex file with no counterpart in the OS root.
for c in OS App; do
	(cd "$root/private/preboot/Cryptexes/$c" && sudo find . ! -type d ! -path './System/Library/Caches/com.apple.dyld/*') |
	while IFS= read -r f; do
		f=${f#./}
		if ! sudo test -e "$root/$f" && ! sudo test -L "$root/$f"; then
			sudo mkdir -p "$root/$(dirname "$f")"
			src="$root/private/preboot/Cryptexes/$c/$f"
			if sudo test -L "$src"; then sudo cp -P "$src" "$root/$f"; else sudo ln "$src" "$root/$f"; fi
			echo "linked $c:/$f"
		fi
	done
done

# logd stores (owner _logd 272), system keybag: identical to the Linux script.
db="$root/private/var/db"
sudo install -d -o 272 -g 272 -m 0751 "$db/diagnostics"
sudo install -d -o 272 -g 272 -m 0755 "$db/uuidtext" "$db/diagnostics/timesync"
for d in diagnostics uuidtext diagnostics/timesync; do sudo install -o 272 -g 272 -m 0644 /dev/null "$db/$d/.migrated"; done
sudo install -d -m 0700 -o 0 -g 0 "$root/private/var/keybags"
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
	'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	'<plist version="1.0"><dict><key>KeyBagKeys</key><data>U1dFRVQ=</data></dict></plist>' |
	sudo install -m 0600 -o 0 -g 0 /dev/stdin "$root/private/var/keybags/systembag.kb"

if [[ -d "$(dirname "$0")/../apps/stage/Payload" ]]; then
	sudo "$(dirname "$0")/add-apps.sh" "$root"
fi
echo "now run: sudo python3 tools/sweet-root-tweaks.py $root   (then press Enter here to finish)"
read -r _
sudo rm -rf "$root/.fseventsd" "$root/.Spotlight-V100" "$root/.Trashes" "$root/.DS_Store" 2>/dev/null || true
df -h "$root"
diskutil unmount "$root" >/dev/null
fsck_hfs -f -n "${rootdev/disk/rdisk}"
hdiutil detach "$rootdev" >/dev/null 2>&1
echo "done: $out  (flash with: fastboot -S 512M flash userdata $out)"

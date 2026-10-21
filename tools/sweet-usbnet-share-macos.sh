#!/bin/bash
# Share this Mac's internet with the phone over the USB Ethernet (192.168.7.0/24): IP forwarding + pf NAT.
#   sudo tools/sweet-usbnet-share-macos.sh [on|off]        (on by default; temporary, undone by "off" or reboot)
set -eu
wan=$(route -n get default 2>/dev/null | awk '/interface:/{print $2}')
case "${1:-on}" in
on)
	[ -n "$wan" ] || { echo "no default route on this Mac" >&2; exit 1; }
	sysctl -w net.inet.ip.forwarding=1 >/dev/null
	printf 'nat on %s from 192.168.7.0/24 to any -> (%s)\npass all\n' "$wan" "$wan" | pfctl -q -f - -e 2>&1 | grep -v "pf already enabled" || true
	echo "sharing: 192.168.7.0/24 -> $wan (pf NAT on)";;
off)
	sysctl -w net.inet.ip.forwarding=0 >/dev/null
	pfctl -q -d 2>&1 | true
	echo "sharing off";;
esac

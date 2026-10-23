#!/bin/bash
# Run ONCE as root and leave it running:   sudo tools/sweet-bridge-macos.sh [LOGFILE]
# Keeps the phone's USB network up across reboots (192.168.7.1 on the CDC-ECM interface with MAC
# 02:00:5e:77:ee:01), restarts the UDP log listener each time the phone reappears, appends the kernel
# log to LOGFILE (default logs/bridge.log; each connect is marked with "==== phone up"), and switches
# the internet sharing for the phone on. Everything else needs no root: commands to the phone
# (kernel patch 0101) are plain UDP to 192.168.7.2:5141:
#   printf ping | nc -u -w1 192.168.7.2 5141     ping | log | bootloader | reboot
set -u
cd "$(dirname "$0")/.."
mac=02:00:5e:77:ee:01
log=${1:-logs/bridge.log}
mkdir -p "$(dirname "$log")"
[ -x tools/sweet-usbnet-share-macos.sh ] && tools/sweet-usbnet-share-macos.sh on >/dev/null 2>&1
nc_pid=
cleanup() { [ -n "$nc_pid" ] && kill "$nc_pid" 2>/dev/null; exit 0; }
trap cleanup INT TERM
echo "bridge: waiting for the phone ($mac), log -> $log" >&2
while :; do
	ifn=$(ifconfig -a | awk -v m="$mac" '/^[a-z0-9]+:/{i=$1; sub(":","",i)} tolower($1)=="ether" && tolower($2)==m {print i; exit}')
	if [ -n "$ifn" ]; then
		cur=$(ifconfig "$ifn" | awk '$1=="inet"{print $2; exit}')
		if [ "$cur" != 192.168.7.1 ]; then
			ifconfig "$ifn" inet 192.168.7.1 netmask 255.255.255.0 up
			[ -n "$nc_pid" ] && kill "$nc_pid" 2>/dev/null
			echo "==== phone up $(date '+%F %T') ($ifn)" >> "$log"
			nc -ul 5140 >> "$log" 2>/dev/null &
			nc_pid=$!
			echo "bridge: $ifn up" >&2
		fi
	fi
	sleep 1
done

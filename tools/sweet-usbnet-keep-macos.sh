#!/bin/bash
# Like sweet-usbnet-macos.sh, but survives phone reboots: macOS drops 192.168.7.1 whenever the interface re-enumerates,
# so it is given back every second. Run once per session; the live log (udp/5140) goes to LOGFILE.
#   sudo tools/sweet-usbnet-keep-macos.sh LOGFILE
set -u
mac=02:00:5e:77:ee:01
log=${1:-/dev/null}
nc -ul 5140 | tee -a "$log" >/dev/null &
trap 'kill 0' EXIT
while :; do
	ifn=$(ifconfig -a | awk -v m="$mac" '/^[a-z0-9]+:/{i=$1; sub(":","",i)} tolower($1)=="ether" && tolower($2)==m {print i; exit}')
	if [ -n "$ifn" ] && ! ifconfig "$ifn" | grep -q "inet 192.168.7.1 "; then
		ifconfig "$ifn" inet 192.168.7.1 netmask 255.255.255.0 up && echo "$ifn up" >&2
	fi
	sleep 1
done

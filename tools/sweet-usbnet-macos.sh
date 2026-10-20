#!/bin/bash
# macOS host side of the phone's USB Ethernet (kernel sweetusb=2, patches 0075-0077).
#   sudo tools/sweet-usbnet-macos.sh [LOGFILE]
# Finds the CDC-ECM interface with MAC 02:00:5e:77:ee:01, gives it 192.168.7.1/24
# (phone: 192.168.7.2) and prints the live kernel console from UDP 5140.
set -eu
mac=02:00:5e:77:ee:01
echo "waiting for the phone's USB Ethernet ($mac)..." >&2
while :; do
	ifn=$(ifconfig -a | awk -v m="$mac" '/^[a-z0-9]+:/{i=$1; sub(":","",i)} tolower($1)=="ether" && tolower($2)==m {print i; exit}')
	[ -n "$ifn" ] && break
	sleep 1
done
ifconfig "$ifn" inet 192.168.7.1 netmask 255.255.255.0 up
echo "$ifn up: 192.168.7.1 <-> phone 192.168.7.2; live log on udp/5140" >&2
if [ -n "${1:-}" ]; then exec nc -ul 5140 | tee -a "$1"; else exec nc -ul 5140; fi

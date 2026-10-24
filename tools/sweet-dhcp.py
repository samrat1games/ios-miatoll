#!/usr/bin/env python3
"""Minimal DHCP server for the phone's USB network: answers DISCOVER/REQUEST on the CDC-ECM interface with
192.168.7.2, router 192.168.7.1 and public DNS, so iOS keeps a default route through the Mac (pf NAT from
tools/sweet-usbnet-share-macos.sh). Without it iOS gives up on DHCP, drops the 192.168.7.1 route and falls back to
169.254.x.x: Safari stays white.

    sudo tools/sweet-dhcp.py [IFACE]        (default: the interface that holds 192.168.7.1)
"""
import socket
import struct
import subprocess
import sys

SERVER, CLIENT, MASK = '192.168.7.1', '192.168.7.2', '255.255.255.0'
DNS = ['1.1.1.1', '8.8.8.8']
LEASE = 24 * 3600
IP_BOUND_IF = 25    # macOS: send and receive on this interface only


def iface_with_server_ip():
    out = subprocess.run(['ifconfig'], capture_output=True, text=True).stdout
    cur = None
    for line in out.splitlines():
        if line and not line[0].isspace():
            cur = line.split(':')[0]
        elif f'inet {SERVER} ' in line:
            return cur
    sys.exit(f'no interface has {SERVER}: is tools/sweet-bridge-macos.sh running?')


def reply(req, msgtype):
    xid, flags, chaddr = req[4:8], req[10:12], req[28:44]
    hdr = struct.pack('!BBBB', 2, 1, 6, 0) + xid + b'\0\0' + flags + bytes(4) + socket.inet_aton(CLIENT) + \
        socket.inet_aton(SERVER) + bytes(4) + chaddr + bytes(192)
    opts = bytes([53, 1, msgtype]) + bytes([54, 4]) + socket.inet_aton(SERVER) + \
        bytes([51, 4]) + struct.pack('!I', LEASE) + bytes([1, 4]) + socket.inet_aton(MASK) + \
        bytes([3, 4]) + socket.inet_aton(SERVER) + bytes([6, 4 * len(DNS)]) + b''.join(socket.inet_aton(d) for d in DNS) + \
        bytes([255])
    return hdr + b'\x63\x82\x53\x63' + opts


def option(pkt, code):
    i = 240
    while i < len(pkt) and pkt[i] != 255:
        if pkt[i] == 0:
            i += 1
            continue
        if pkt[i] == code:
            return pkt[i + 2:i + 2 + pkt[i + 1]]
        i += 2 + pkt[i + 1]
    return None


def main():
    ifname = sys.argv[1] if len(sys.argv) > 1 else iface_with_server_ip()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.setsockopt(socket.IPPROTO_IP, IP_BOUND_IF, socket.if_nametoindex(ifname))
    s.bind(('0.0.0.0', 67))
    mac = subprocess.run(['ifconfig', ifname], capture_output=True, text=True).stdout.split('ether ')[1].split()[0]
    own = bytes.fromhex(mac.replace(':', ''))
    print(f'DHCP on {ifname}: {CLIENT}/24, router {SERVER}, dns {" ".join(DNS)}', flush=True)
    while True:
        pkt, _ = s.recvfrom(2048)
        if len(pkt) < 241 or pkt[0] != 1 or pkt[236:240] != b'\x63\x82\x53\x63':
            continue
        t = option(pkt, 53)
        if not t or t[0] not in (1, 3):     # DISCOVER, REQUEST
            continue
        if pkt[28:34] == own:               # the Mac's own interface asks too: never hand it the phone's address
            continue
        answer = 2 if t[0] == 1 else 5      # OFFER, ACK
        s.sendto(reply(pkt, answer), ('255.255.255.255', 68))
        print(('OFFER' if answer == 2 else 'ACK') + f' {CLIENT} to {pkt[28:34].hex(":")}', flush=True)


if __name__ == '__main__':
    main()

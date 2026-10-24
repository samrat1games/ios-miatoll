#!/usr/bin/env python3
"""Copy files onto the phone over the USB network into /private/var/miatoll/<name> (kernel patch 0116, UDP 5141).

    tools/sweet-put.py FILE [FILE ...]          e.g. tools/sweet-put.py backup/modemst1.img backup/fsg.img

Every 1400-byte chunk is acknowledged ("ok <end>") and resent on a timeout.
"""
import os
import socket
import sys

PHONE = ('192.168.7.2', 5141)
CHUNK = 1400


def put(path):
    name = os.path.basename(path)
    data = open(path, 'rb').read()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(1.0)
    off = 0
    while True:
        part = data[off:off + CHUNK]
        pkt = f'fput {name} {off}\n'.encode() + part
        for attempt in range(10):
            s.sendto(pkt, PHONE)
            try:
                reply = s.recvfrom(256)[0].decode(errors='replace').strip()
            except socket.timeout:
                continue
            if reply == f'ok {off + len(part)}':
                break
            if reply.startswith('err'):
                sys.exit(f'{name} @{off}: {reply}')
        else:
            sys.exit(f'{name} @{off}: no answer from the phone')
        off += len(part)
        if off >= len(data):
            break
        if off % (CHUNK * 500) == 0:
            print(f'  {name}: {off * 100 // len(data)}%', flush=True)
    print(f'{name}: {len(data)} bytes -> /private/var/miatoll/{name}')


for p in sys.argv[1:] or sys.exit(__doc__):
    put(p)

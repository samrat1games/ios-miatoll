#!/usr/bin/env python3
"""Write stdin to a sparse file, skipping all-zero 1 MiB chunks (APFS keeps them as holes).
  adb exec-out 'dd if=/dev/block/by-name/userdata bs=4M count=4096' | pull-sparse.py OUT.img"""
import os, sys
out = sys.argv[1]
fd = os.open(out, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o644)
CH = 1 << 20
zero = bytes(CH)
inp = sys.stdin.buffer
pos = 0
while True:
    data = inp.read(CH)
    if not data:
        break
    while len(data) < CH:          # pipes return short reads
        more = inp.read(CH - len(data))
        if not more:
            break
        data += more
    if data != zero[:len(data)]:
        os.pwrite(fd, data, pos)
    pos += len(data)
    if (pos >> 20) % 1024 == 0:
        print(f'{pos >> 20} MiB', file=sys.stderr, flush=True)
os.ftruncate(fd, pos)
os.close(fd)
print(f'done: {pos} bytes', file=sys.stderr)

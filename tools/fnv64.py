#!/usr/bin/env python3
"""FNV-1a 64 of files, same as the kernel's "SWEET MODEM: MODEM.Bnn ... fnv" lines (patch 0103).
  tools/fnv64.py research/modem/fs/IMAGE/MODEM.B00 ...   (files are hashed whole; the kernel hashes the first filesz bytes)"""
import sys
for p in sys.argv[1:]:
    h = 0xcbf29ce484222325
    for b in open(p, 'rb').read():
        h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    print(p, hex(h))

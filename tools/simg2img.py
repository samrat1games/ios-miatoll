#!/usr/bin/env python3
"""Android sparse image -> raw image.  Usage: simg2img.py IN.simg OUT.img [--consume]

--consume punches each source range out of IN (APFS F_PUNCHHOLE) right after it was written, so IN and OUT
never need disk space at the same time (IN is destroyed)."""
import fcntl, os, struct, sys

src, dst = sys.argv[1:3]
consume = '--consume' in sys.argv
f = open(src, 'rb')
g = open(src, 'r+b') if consume else None
magic, maj, minr, fh, ch, blk, total, nchunks, crc = struct.unpack('<IHHHHIIII', f.read(28))
assert magic == 0xED26FF3A
f.seek(fh)
out = os.open(dst, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o644)
pos = 0
for _ in range(nchunks):
    cpos = f.tell()
    t, _, n, tot = struct.unpack('<HHII', f.read(12))
    size = n * blk
    if t == 0xCAC1:
        left, off = size, pos
        while left:
            b = f.read(min(left, 8 << 20))
            os.pwrite(out, b, off)
            off += len(b); left -= len(b)
        if consume:
            os.fsync(out)
            lo = (cpos + 12 + 4095) // 4096 * 4096          # F_PUNCHHOLE needs 4 KiB alignment
            hi = (cpos + 12 + size) // 4096 * 4096
            if hi > lo:
                fcntl.fcntl(g.fileno(), 99, struct.pack('<IIqq', 0, 0, lo, hi - lo))
    elif t == 0xCAC2:
        v = f.read(4)
        if v != b'\0\0\0\0':
            os.pwrite(out, v * (size // 4), pos)
    elif t == 0xCAC3:
        pass
    elif t == 0xCAC4:
        f.read(4)
    pos += size
os.ftruncate(out, total * blk)
os.close(out)
print(f'{dst}: {total * blk} bytes')

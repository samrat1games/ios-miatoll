#!/usr/bin/env python3
"""Raw image -> Android sparse image (RAW + DONT_CARE chunks).  Usage: img2simg.py IN OUT [--dry]

Stand-in for AOSP img2simg (not available on macOS). Zero 4 KiB blocks become DONT_CARE."""
import struct, sys

BLK = 4096
MAXRAW = 64 << 20   # keep chunks modest for fastboot's resparse


def main(src, dst, dry):
    size = __import__('os').path.getsize(src)
    assert size % BLK == 0
    total = size // BLK
    chunks = []          # (type, nblocks, offset)
    f = open(src, 'rb')
    zero = bytes(BLK)
    blk = 0
    nz = 0
    cur = None
    while blk < total:
        data = f.read(BLK * 1024)
        for i in range(0, len(data), BLK):
            z = data[i:i + BLK] == zero
            t = 0xCAC3 if z else 0xCAC1
            if cur and cur[0] == t and not (t == 0xCAC1 and cur[1] * BLK >= MAXRAW):
                cur[1] += 1
            else:
                cur = [t, 1, blk * BLK]
                chunks.append(cur)
            blk += 1
            nz += 0 if z else 1
    print(f'{size} bytes, {nz} data blocks ({nz * BLK / 2**30:.2f} GiB), {len(chunks)} chunks')
    if dry:
        return
    f.seek(0)
    out = open(dst, 'wb')
    out.write(struct.pack('<IHHHHIIII', 0xED26FF3A, 1, 0, 28, 12, BLK, total, len(chunks), 0))
    for t, n, off in chunks:
        if t == 0xCAC1:
            out.write(struct.pack('<HHII', t, 0, n, 12 + n * BLK))
            f.seek(off)
            out.write(f.read(n * BLK))
        else:
            out.write(struct.pack('<HHII', t, 0, n, 12))
    out.close()


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], '--dry' in sys.argv)

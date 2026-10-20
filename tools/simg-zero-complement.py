#!/usr/bin/env python3
"""Sparse "complement": zero-FILL every block range the given sparse image leaves DONT_CARE, skip the rest.

  simg-zero-complement.py IMAGE.simg OUT.simg

Flash OUT before IMAGE after an earlier flash of a different image: DONT_CARE keeps old data, and e.g.
the HFS+ journal replay would re-apply stale transactions. Reads headers only (seeks over RAW data)."""
import struct, sys

RAW, FILL, DONTCARE = 0xCAC1, 0xCAC2, 0xCAC3
src, dst = sys.argv[1:3]
f = open(src, 'rb')
magic, maj, minr, fh, ch, blk, total, nchunks, crc = struct.unpack('<IHHHHIIII', f.read(28))
assert magic == 0xED26FF3A and fh == 28 and ch == 12
f.seek(fh)
out = []   # (type, blocks)
for _ in range(nchunks):
    t, _, n, tot = struct.unpack('<HHII', f.read(12))
    f.seek(tot - 12, 1)
    nt = DONTCARE if t == RAW else FILL if t == DONTCARE else None
    assert nt, hex(t)
    if out and out[-1][0] == nt:
        out[-1][1] += n
    else:
        out.append([nt, n])
with open(dst, 'wb') as g:
    g.write(struct.pack('<IHHHHIIII', magic, maj, minr, fh, ch, blk, total, len(out), 0))
    for t, n in out:
        if t == FILL:
            g.write(struct.pack('<HHII', FILL, 0, n, 16) + b'\0\0\0\0')
        else:
            g.write(struct.pack('<HHII', DONTCARE, 0, n, 12))
fill = sum(n for t, n in out if t == FILL)
print(f'{len(out)} chunks, {fill} blocks ({fill * blk / 2**30:.2f} GiB) zero-filled')

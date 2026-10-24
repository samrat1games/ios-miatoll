#!/usr/bin/env python3
"""Raw image -> several Android sparse images, each at most PART bytes, flashed one after another:

  simg_split.py IN.img OUTDIR [PART_MB]        ->  OUTDIR/ios16-root.part01.simg, part02, ...
  fastboot flash userdata ios16-root.part01.simg ; fastboot flash userdata ios16-root.part02.simg ; ...

Every part describes the whole partition (DONT_CARE before and after its own data), so each flash only writes
its own part and leaves the rest alone. Zero 4 KiB blocks are DONT_CARE as in tools/img2simg.py.
  simg_split.py --verify OUTDIR IN.img     checks that the parts, put back in order, are IN.img (a hash of both)"""
import glob
import hashlib
import os
import struct
import sys

BLK = 4096
MAXRAW = 64 << 20
RAW, DONT = 0xCAC1, 0xCAC3


def scan(src):
    size = os.path.getsize(src)
    assert size % BLK == 0
    total = size // BLK
    chunks, cur, blk = [], None, 0
    zero = bytes(BLK)
    with open(src, 'rb') as f:
        while blk < total:
            data = f.read(BLK * 1024)
            for i in range(0, len(data), BLK):
                t = DONT if data[i:i + BLK] == zero else RAW
                if cur and cur[0] == t and not (t == RAW and cur[1] * BLK >= MAXRAW):
                    cur[1] += 1
                else:
                    cur = [t, 1, blk]
                    chunks.append(cur)
                blk += 1
    return total, chunks


def split(src, outdir, part_mb):
    os.makedirs(outdir, exist_ok=True)
    total, chunks = scan(src)
    limit = part_mb << 20
    parts, cur, used = [], [], 0
    for c in chunks:
        need = c[1] * BLK if c[0] == RAW else 0
        if cur and used + need > limit:
            parts.append(cur)
            cur, used = [], 0
        cur.append(c)
        used += need
    if cur:
        parts.append(cur)
    print('%d blocks, %d chunks -> %d parts' % (total, len(chunks), len(parts)), flush=True)
    with open(src, 'rb') as f:
        for n, part in enumerate(parts, 1):
            first, last = part[0][2], part[-1][2] + part[-1][1]
            seq = []
            if first:
                seq.append((DONT, first, None))
            seq += [(t, k, b) for t, k, b in part]
            if last < total:
                seq.append((DONT, total - last, None))
            name = os.path.join(outdir, 'ios16-root.part%02d.simg' % n)
            with open(name, 'wb') as out:
                out.write(struct.pack('<IHHHHIIII', 0xED26FF3A, 1, 0, 28, 12, BLK, total, len(seq), 0))
                for t, k, b in seq:
                    if t == RAW:
                        out.write(struct.pack('<HHII', t, 0, k, 12 + k * BLK))
                        f.seek(b * BLK)
                        left = k * BLK
                        while left:
                            piece = f.read(min(left, 8 << 20))
                            out.write(piece)
                            left -= len(piece)
                    else:
                        out.write(struct.pack('<HHII', t, 0, k, 12))
            print(name, os.path.getsize(name), flush=True)


def verify(outdir, src):
    """Hash the image rebuilt from the parts in order (later parts only fill what earlier ones left) against src."""
    names = sorted(glob.glob(os.path.join(outdir, 'ios16-root.part*.simg')))
    size = os.path.getsize(src)
    total = size // BLK
    covered = bytearray(total)       # 1 where a part wrote the block
    h_parts = {}
    for name in names:
        with open(name, 'rb') as f:
            hdr = struct.unpack('<IHHHHIIII', f.read(28))
            assert hdr[0] == 0xED26FF3A and hdr[6] == total, 'bad header in ' + name
            blk = 0
            for _ in range(hdr[7]):
                t, _r, k, tsz = struct.unpack('<HHII', f.read(12))
                if t == RAW:
                    for i in range(k):
                        data = f.read(BLK)
                        assert covered[blk + i] == 0, 'block written twice'
                        covered[blk + i] = 1
                        h_parts[blk + i] = hashlib.md5(data).digest()
                    blk += k
                elif t == DONT:
                    blk += k
                else:
                    raise SystemExit('unknown chunk %x' % t)
            assert blk == total, 'chunks do not add up in ' + name
    bad = 0
    zero = bytes(BLK)
    with open(src, 'rb') as f:
        for blk in range(total):
            data = f.read(BLK)
            if data == zero:
                if covered[blk]:
                    bad += 1
            elif h_parts.get(blk) != hashlib.md5(data).digest():
                bad += 1
    print('verify: %d parts, %d differing blocks' % (len(names), bad))
    return bad == 0


if __name__ == '__main__':
    if sys.argv[1] == '--verify':
        sys.exit(0 if verify(sys.argv[2], sys.argv[3]) else 1)
    split(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 1800)

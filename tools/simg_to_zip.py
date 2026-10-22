#!/usr/bin/env python3
"""Raw root image -> ZIP holding an Android sparse image (ios16-root.simg), written in one go.

  simg_to_zip.py IN.img OUT.zip

The sparse image is never kept on the disk by itself: its bytes go straight into the zip (ZIP64, deflate),
so only the zip (a few GB) needs room. Windows' own `tar -xf` opens it. Zero 4 KiB blocks become DONT_CARE
as in tools/img2simg.py."""
import os
import struct
import sys
import zipfile

BLK = 4096
MAXRAW = 64 << 20


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
                z = data[i:i + BLK] == zero
                t = 0xCAC3 if z else 0xCAC1
                if cur and cur[0] == t and not (t == 0xCAC1 and cur[1] * BLK >= MAXRAW):
                    cur[1] += 1
                else:
                    cur = [t, 1, blk * BLK]
                    chunks.append(cur)
                blk += 1
    return total, chunks


def main(src, dst):
    total, chunks = scan(src)
    data_blocks = sum(n for t, n, _ in chunks if t == 0xCAC1)
    print('%d blocks, %d data (%.2f GiB), %d chunks' % (total, data_blocks, data_blocks * BLK / 2**30, len(chunks)), flush=True)
    done = 0
    with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED, compresslevel=3, allowZip64=True) as z, \
            z.open('ios16-root.simg', 'w', force_zip64=True) as out, open(src, 'rb') as f:
        out.write(struct.pack('<IHHHHIIII', 0xED26FF3A, 1, 0, 28, 12, BLK, total, len(chunks), 0))
        for t, n, off in chunks:
            if t == 0xCAC1:
                out.write(struct.pack('<HHII', t, 0, n, 12 + n * BLK))
                f.seek(off)
                left = n * BLK
                while left:
                    piece = f.read(min(left, 8 << 20))
                    out.write(piece)
                    left -= len(piece)
                done += n
                if done % (1 << 16) < n:
                    print('%.0f%%' % (100.0 * done / max(1, data_blocks)), flush=True)
            else:
                out.write(struct.pack('<HHII', t, 0, n, 12))
    print('written', dst, os.path.getsize(dst))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

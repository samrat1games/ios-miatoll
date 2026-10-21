#!/usr/bin/env python3
"""Pull only the USED part of the HFS+ volume on the phone's userdata (via adb, recovery root).

  pull-used.py OUT.img [--size 17179869184]

Reads the volume header + allocation bitmap, finds the highest allocated block, and copies
[0, last_used] plus the last 1 MiB (alternate volume header). The rest of OUT.img stays a hole
(zeros): safe because those blocks are free and are never written back (diff-flash only sends
blocks that differ from the pulled copy)."""
import os, struct, subprocess, sys

DEV = '/dev/block/by-name/userdata'


def dd(skip_bytes, count_bytes):
    bs = 4096
    assert skip_bytes % bs == 0
    cmd = f'dd if={DEV} bs={bs} skip={skip_bytes // bs} count={(count_bytes + bs - 1) // bs} 2>/dev/null | zstd -1 -T8 -c'
    z = subprocess.run(['adb', 'exec-out', cmd], capture_output=True, check=True).stdout   # compressed on the phone (USB is the bottleneck)
    return subprocess.run(['zstd', '-d', '-c'], input=z, capture_output=True, check=True).stdout[:count_bytes]


def main():
    out = sys.argv[1]
    size = 16 << 30
    vh = dd(0, 4096)[1024:1024 + 512]
    assert vh[:2] == b'HX', 'not HFSX'
    block = struct.unpack('>I', vh[40:44])[0]
    total = struct.unpack('>I', vh[44:48])[0]
    free = struct.unpack('>I', vh[48:52])[0]
    ext = [struct.unpack('>II', vh[128 + 8 * i:136 + 8 * i]) for i in range(8)]
    print(f'block {block}, total {total}, free {free}', flush=True)
    bitmap = b''
    for start, cnt in ext:
        if cnt:
            bitmap += dd(start * block, cnt * block)
    CHB = (16 << 20) // block                  # blocks per chunk
    nchunks = (total + CHB - 1) // CHB
    used = []
    for c in range(nchunks):
        lo, hi = c * CHB, min(total, (c + 1) * CHB)
        seg = bitmap[lo // 8:(hi + 7) // 8]
        used.append(any(seg))
    need = sum(used) * (16 << 20)
    print(f'chunks with allocated blocks: {sum(used)}/{nchunks} -> read {need / 2**30:.2f} GiB of {total * block / 2**30:.2f}', flush=True)
    fd = os.open(out, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o644)
    os.ftruncate(fd, total * block)
    zero = bytes(1 << 20)
    done = 0
    for c in range(nchunks):
        if not used[c]:
            continue
        pos = c * CHB * block
        data = dd(pos, min(CHB * block, total * block - pos))
        for off in range(0, len(data), 1 << 20):
            piece = data[off:off + (1 << 20)]
            if piece != zero[:len(piece)]:
                os.pwrite(fd, piece, pos + off)
        done += 1
        if done % 32 == 0:
            print(f'{done * 16} MiB', flush=True)
    tail = 1 << 20
    os.pwrite(fd, dd(total * block - tail, tail), total * block - tail)
    os.close(fd)
    print('done:', total * block, 'bytes', flush=True)


main()

#!/usr/bin/env python3
"""List/extract files from a FAT16 image (4 KiB sectors ok): fat16-ls.py IMG [OUTDIR]"""
import struct, sys, os
d = open(sys.argv[1], 'rb').read()
bps, spc, rsv, nfat, rootn = struct.unpack_from('<HBHBH', d, 11)
spf = struct.unpack_from('<H', d, 22)[0]
fat = d[rsv*bps:(rsv+spf)*bps]
root = (rsv + nfat*spf)*bps
data = root + (rootn*32 + bps-1)//bps*bps
cl = bps*spc
def chain(c):
    out = b''
    while 2 <= c < 0xfff8:
        o = data + (c-2)*cl
        out += d[o:o+cl]
        c = struct.unpack_from('<H', fat, c*2)[0]
    return out
def entries(buf):
    lfn = ''
    for i in range(0, len(buf), 32):
        e = buf[i:i+32]
        if e[0] == 0: break
        if e[0] == 0xe5: lfn = ''; continue
        if e[11] == 0xf:
            part = (e[1:11]+e[14:26]+e[28:32]).decode('utf-16le').split('\0')[0]
            lfn = part + lfn; continue
        name = lfn or (e[:8].decode().strip() + ('.' + e[8:11].decode().strip() if e[8:11].strip() else ''))
        lfn = ''
        c = struct.unpack_from('<H', e, 26)[0]; sz = struct.unpack_from('<I', e, 28)[0]
        yield name, e[11], c, sz
out = sys.argv[2] if len(sys.argv) > 2 else None
def walk(buf, path):
    for name, attr, c, sz in entries(buf):
        if attr & 8 or name in ('.', '..'): continue
        full = path + name
        if attr & 0x10:
            walk(chain(c), full + '/')
            continue
        print(f'{sz:10d} {full}')
        if out:
            fp = os.path.join(out, full)
            os.makedirs(os.path.dirname(fp), exist_ok=True)
            open(fp, 'wb').write(chain(c)[:sz])
walk(d[root:root+rootn*32], '')

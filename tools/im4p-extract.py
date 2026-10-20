#!/usr/bin/env python3
"""Extract (and LZFSE-decompress) the payload of an unencrypted IM4P.  Usage: im4p-extract.py IN.im4p OUT

Replaces `ipsw img4 im4p extract` for DeviceTree.d22ap.im4p (needs `pip install pyliblzfse`)."""
import sys


def der(b, o):
    tag = b[o]; n = b[o + 1]; o += 2
    if n & 0x80:
        k = n & 0x7f; n = int.from_bytes(b[o:o + k], 'big'); o += k
    return tag, o, n


def main(src, dst):
    b = open(src, 'rb').read()
    tag, o, n = der(b, 0)            # SEQUENCE
    end = o + n
    items = []
    while o < end:
        t, s, l = der(b, o)
        items.append((t, b[s:s + l]))
        o = s + l
    assert items[0][1] == b'IM4P', 'not an IM4P'
    data = next(v for t, v in items[3:] if t == 0x04)   # OCTET STRING payload
    if data[:4] in (b'bvx2', b'bvxn', b'bvx1', b'bvx-'):
        import liblzfse
        data = liblzfse.decompress(data)
    open(dst, 'wb').write(data)
    print(f'{src}: type={items[1][1].decode()} -> {len(data)} bytes')


if __name__ == '__main__':
    main(*sys.argv[1:3])

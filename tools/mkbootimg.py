#!/usr/bin/env python3
"""Minimal Android boot.img v2 writer, option-compatible with AOSP mkbootimg for what
shim/build.sh uses (header_version 2 only). Verified by tools/unpack_bootv2.py."""
import argparse, hashlib, struct

def pad(b, page):
    return b + b'\0' * (-len(b) % page)

def os_ver(v, patch):
    a, b, c = (int(x) for x in v.split('.'))
    y, m = (int(x) for x in patch.split('-'))
    return (((a << 14) | (b << 7) | c) << 11) | ((y - 2000) << 4) | m

ap = argparse.ArgumentParser()
ap.add_argument('--header_version', type=int, default=2)
ap.add_argument('--kernel', required=True)
ap.add_argument('--ramdisk', required=True)
ap.add_argument('--dtb', required=True)
ap.add_argument('--base', type=lambda x: int(x, 0), default=0x10000000)
ap.add_argument('--kernel_offset', type=lambda x: int(x, 0), default=0x8000)
ap.add_argument('--ramdisk_offset', type=lambda x: int(x, 0), default=0x1000000)
ap.add_argument('--second_offset', type=lambda x: int(x, 0), default=0xf00000)
ap.add_argument('--tags_offset', type=lambda x: int(x, 0), default=0x100)
ap.add_argument('--dtb_offset', type=lambda x: int(x, 0), default=0x1f00000)
ap.add_argument('--pagesize', type=lambda x: int(x, 0), default=2048)
ap.add_argument('--os_version', default='0.0.0')
ap.add_argument('--os_patch_level', default='2000-01')
ap.add_argument('--cmdline', default='')
ap.add_argument('-o', '--output', required=True)
a = ap.parse_args()
assert a.header_version == 2
k, r, d = (open(f, 'rb').read() for f in (a.kernel, a.ramdisk, a.dtb))
cmd = a.cmdline.encode()
assert len(cmd) < 512 + 1024
h = hashlib.sha1()
for blob in (k, r, b''):
    h.update(blob); h.update(struct.pack('<I', len(blob)))
h.update(struct.pack('<I', 0)); h.update(d)
ident = h.digest().ljust(32, b'\0')
hdr = b'ANDROID!' + struct.pack('<10I', len(k), a.base + a.kernel_offset, len(r), a.base + a.ramdisk_offset,
                                0, a.base + a.second_offset, a.base + a.tags_offset, a.pagesize, 2,
                                os_ver(a.os_version, a.os_patch_level))
hdr += b'\0' * 16 + cmd[:512].ljust(512, b'\0') + ident + cmd[512:].ljust(1024, b'\0')
hdr += struct.pack('<IQI', 0, 0, 1660) + struct.pack('<IQ', len(d), a.base + a.dtb_offset)
assert len(hdr) == 1660, len(hdr)
p = a.pagesize
open(a.output, 'wb').write(pad(hdr, p) + pad(k, p) + pad(r, p) + pad(d, p))

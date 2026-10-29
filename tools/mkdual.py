#!/usr/bin/env python3
"""Pack an Android boot.img (kernel + ramdisk) behind the xnu payload of the menu image: dual boot without a second partition.

    tools/mkdual.py ANDROID_BOOT.img PAYLOAD OUT_RAMDISK OUT_CMDLINE OUT_DTB

The shim (shim/loader.c android_boot) finds the Android part by a 64-byte trailer at the very end of the ramdisk, copies the kernel
and the ramdisk to free RAM and enters the kernel with the bootloader's own device tree. The device tree and the kernel command line
come from the Android image (the bootloader builds /chosen from the boot image header), so the menu image takes them over:
OUT_CMDLINE and OUT_DTB are what shim/build.sh gives to mkbootimg.
"""
import struct
import sys
import zlib

PAGE = 4096


def up(n, a):
    return (n + a - 1) // a * a


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    src, payload_path, out_rd, out_cmd, out_dtb = sys.argv[1:]
    d = open(src, 'rb').read()
    if d[:8] != b'ANDROID!':
        sys.exit('not an Android boot image (no ANDROID! magic)')
    ksz, _, rsz, _, ssz, _, _, page, ver = struct.unpack('<9I', d[8:44])
    if ver > 2:
        sys.exit(f'boot image header v{ver}: only v0-v2 (kernel, ramdisk and dtb inside one image) are supported')
    cmd = d[64:64 + 512].split(b'\0')[0] + d[608:608 + 1024].split(b'\0')[0]
    dtb_sz = struct.unpack('<I', d[1648:1652])[0] if ver >= 2 else 0
    off = page
    kernel = d[off:off + ksz]
    off += up(ksz, page)
    ramdisk = d[off:off + rsz]
    off += up(rsz, page) + up(ssz, page)
    if ver >= 1:
        off += up(struct.unpack('<I', d[1632:1636])[0], page)
    dtb = d[off:off + dtb_sz] if dtb_sz else b''
    if kernel[:2] == b'\x1f\x8b':                     # Image.gz (the device tree may be appended to it)
        kernel = zlib.decompressobj(31).decompress(kernel)
    if kernel[56:60] != b'ARM\x64':
        sys.exit('the kernel is not a plain arm64 Image (no ARM\\x64 magic at 56)')
    text_off, image_size, flags = struct.unpack('<QQQ', kernel[8:32])
    if not dtb:
        sys.exit('the Android image has no device tree (header v2 dtb): the bootloader would not get one for it')
    payload = open(payload_path, 'rb').read()
    out = bytearray(payload)
    out += b'\0' * (up(len(out), PAGE) - len(out))
    k_off = len(out)
    out += kernel
    out += b'\0' * (up(len(out), PAGE) - len(out))
    r_off = len(out)
    out += ramdisk
    out += b'\0' * (up(len(out) + 64, 64) - 64 - len(out))
    out += b'SWEETLNX' + struct.pack('<4Q', k_off, len(kernel), r_off, len(ramdisk)) + b'\0' * 24
    assert len(out) % 64 == 0
    open(out_rd, 'wb').write(out)
    open(out_cmd, 'w').write(cmd.decode())
    open(out_dtb, 'wb').write(dtb)
    print(f'mkdual: kernel {len(kernel)} B (text_offset {text_off:#x}, image_size {image_size}, flags {flags:#x}), '
          f'ramdisk {len(ramdisk)} B, dtb {len(dtb)} B, cmdline {len(cmd)} chars -> ramdisk {len(out)} B')


main()

#!/usr/bin/env python3
"""Send IPAs to the phone over the USB network: serves them from the Mac at http://192.168.7.1:8080/<n>.ipa
for MiatollTools -> "Скачать и установить" (it downloads, unzips into /Applications and registers the app).

    tools/ipa-serve.py [--no-check] APP.ipa [MORE.ipa ...]     (--no-check: data bundles like MiatollEFS.ipa)

Needs the USB network up (tools/sweet-bridge-macos.sh). Listens on 192.168.7.1 only, never on other networks.
Each IPA is checked first: iOS 16 runs only 64-bit (arm64) code, and an App Store binary still encrypted with
FairPlay (cryptid 1) only runs on the device of the Apple ID that bought it; such IPAs are refused.
"""
import http.server
import os
import socketserver
import struct
import sys
import zipfile

HOST, PORT = '192.168.7.1', 8080

MH_MAGIC_64, MH_MAGIC, FAT_MAGIC = 0xfeedfacf, 0xfeedface, 0xcafebabe
LC_ENCRYPTION_INFO, LC_ENCRYPTION_INFO_64 = 0x21, 0x2c
CPU_TYPE_ARM64 = 0x0100000c


def slices(data):
    """(cputype, offset) of every Mach-O in a thin or fat binary"""
    magic = struct.unpack_from('>I', data, 0)[0]
    if magic == FAT_MAGIC:
        n = struct.unpack_from('>I', data, 4)[0]
        return [struct.unpack_from('>II', data, 8 + 20 * i)[0:1] + (struct.unpack_from('>I', data, 8 + 20 * i + 8)[0],)
                for i in range(n)]
    return [(struct.unpack_from('<i', data, 4)[0] & 0xffffffff, 0)]


def cryptid(data, off):
    magic = struct.unpack_from('<I', data, off)[0]
    hdr = 32 if magic == MH_MAGIC_64 else 28
    ncmds = struct.unpack_from('<I', data, off + 16)[0]
    p = off + hdr
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, p)
        if cmd in (LC_ENCRYPTION_INFO, LC_ENCRYPTION_INFO_64):
            return struct.unpack_from('<I', data, p + 16)[0]
        p += size
    return 0


def check(ipa):
    """None if the IPA can run here, otherwise the reason it cannot"""
    with zipfile.ZipFile(ipa) as z:
        plists = [n for n in z.namelist() if n.count('/') == 2 and n.startswith('Payload/') and n.endswith('.app/Info.plist')]
        if not plists:
            return 'no Payload/*.app inside'
        import plistlib
        info = plistlib.loads(z.read(plists[0]))
        app = plists[0][:-len('Info.plist')]
        exe = z.read(app + info['CFBundleExecutable'])
    arm64 = [off for cpu, off in slices(exe) if cpu == CPU_TYPE_ARM64]
    if not arm64:
        return 'no 64-bit (arm64) code: iOS 11 and later do not run 32-bit apps'
    if cryptid(exe, arm64[0]):
        return 'encrypted by the App Store (FairPlay, cryptid 1): only runs on the buyer\'s device'
    return None


def main():
    ipas = [a for a in sys.argv[1:] if a != '--no-check']
    skip_check = '--no-check' in sys.argv[1:]
    if not ipas:
        sys.exit(__doc__)
    served = {}
    for ipa in ipas:
        why = None if skip_check else check(ipa)
        if why:
            print(f'skip {os.path.basename(ipa)}: {why}')
            continue
        served[f'/{len(served) + 1}.ipa'] = os.path.abspath(ipa)
    if not served:
        sys.exit('nothing to serve')

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            path = served.get(self.path)
            if not path:
                self.send_error(404)
                return
            size = os.path.getsize(path)
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(size))
            self.end_headers()
            with open(path, 'rb') as f:
                while chunk := f.read(1 << 20):
                    self.wfile.write(chunk)
            print(f'sent {os.path.basename(path)} ({size} bytes) to {self.client_address[0]}')

    socketserver.TCPServer.allow_reuse_address = True
    try:
        srv = socketserver.ThreadingTCPServer((HOST, PORT), Handler)
    except OSError as e:
        sys.exit(f'cannot listen on {HOST}:{PORT} ({e}): is the USB network up (tools/sweet-bridge-macos.sh)?')
    print('On the phone: MiatollTools -> IPA, type the address, "Скачать и установить":')
    for url, path in served.items():
        print(f'  http://{HOST}:{PORT}{url}   {os.path.basename(path)}')
    print('Ctrl-C stops.')
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()

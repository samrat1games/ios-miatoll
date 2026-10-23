#!/usr/bin/env python3
"""Pack the modem EFS copies (backup/modemst1.img, modemst2.img, fsg.img, fsc.img) into MiatollEFS.ipa: a hidden
app bundle that MiatollTools installs to /Applications/MiatollEFS.app, where the kernel's rmtfs server (patch 0113)
reads and writes them. The real modem partitions stay untouched.

    tools/mk-efs-ipa.py [OUT.ipa]        then: tools/ipa-serve.py --no-check OUT.ipa
"""
import os
import plistlib
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'dist', 'MiatollEFS.ipa')
exe = os.path.join(HERE, '..', 'apps', 'MiatollTools', 'build', 'MiatollTools.app', 'MiatollTools')
app = 'Payload/MiatollEFS.app/'
info = {
    'CFBundleIdentifier': 'org.miatoll.efs', 'CFBundleName': 'MiatollEFS', 'CFBundleExecutable': 'MiatollEFS',
    'CFBundlePackageType': 'APPL', 'CFBundleVersion': '1', 'CFBundleShortVersionString': '1',
    'CFBundleSupportedPlatforms': ['iPhoneOS'], 'MinimumOSVersion': '16.0',
    'SBAppTags': ['hidden'],    # no home-screen icon: the bundle only carries the files
}
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr(app + 'Info.plist', plistlib.dumps(info, fmt=plistlib.FMT_BINARY))
    zi = zipfile.ZipInfo(app + 'MiatollEFS')
    zi.external_attr = 0o755 << 16
    z.writestr(zi, open(exe, 'rb').read(), zipfile.ZIP_DEFLATED)
    for n in ('modemst1.img', 'modemst2.img', 'fsg.img', 'fsc.img'):
        z.write(os.path.join(HERE, '..', 'backup', n), app + n)
print(out, os.path.getsize(out), 'bytes')

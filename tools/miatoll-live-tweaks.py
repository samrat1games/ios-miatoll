#!/usr/bin/env python3
"""Edits for the root image PULLED BACK from the phone after its first boot (containers now exist).

  sudo miatoll-live-tweaks.py MOUNTED_ROOT        (mounted with owners)

 1. mobileactivationd data ark (needs the container the first boot creates)
 2. ByHost com.apple.migration.<IOPlatformUUID>.plist: CurrentHost prefs need it, else datamigrator
    re-runs on every boot. UUID = XNU generatePlatformUUID(unique-chip-id, chip-id) from tools/mkadt.py
 3. USB internet: /usr/libexec/sweet-netup + a launchd job (tools/netup)
Idempotent."""
import hashlib, importlib.util, os, plistlib, shutil, struct, sys, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
spec = importlib.util.spec_from_file_location('swt', os.path.join(HERE, 'sweet-root-tweaks.py'))
swt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(swt)
import board

MOBILE = swt.MOBILE


def platform_uuid():
    B = board.load('miatoll', quiet=True)
    ecid = int.from_bytes(B['ecid_tag'].upper().encode()[:7].ljust(7, b'\0'), 'big') << 8 | 1
    space = bytes([0xA6, 0xDD, 0x4C, 0xCB, 0xB5, 0xE8, 0x4A, 0xF5, 0xAC, 0xDD, 0xB6, 0xDC, 0x6A, 0x05, 0x42, 0xB8])
    d = bytearray(hashlib.sha1(space + struct.pack('<Q', ecid) + struct.pack('<I', 0x8015)).digest()[:16])
    d[6] = (d[6] & 0x0F) | 0x50
    d[8] = (d[8] & 0x3F) | 0x80
    return str(uuid.UUID(bytes=bytes(d))).upper()


def byhost(root):
    u = platform_uuid()
    d = os.path.join(root, 'private/var/mobile/Library/Preferences/ByHost')
    os.makedirs(d, exist_ok=True)
    os.chown(d, MOBILE, MOBILE)
    os.chmod(d, 0o755)
    for name in (u, u.lower()):
        swt.save(os.path.join(d, f'com.apple.migration.{name}.plist'), {'LastSystemVersion': swt.BUILD},
                 MOBILE, MOBILE, 0o600)
    print('ByHost migration prefs for', u)


def netup(root):
    src = os.path.join(HERE, 'netup', 'sweet-netup')
    if not os.path.exists(src):
        print('warning: tools/netup/sweet-netup missing (run tools/netup/build.sh); skipped')
        return
    dst = os.path.join(root, 'usr/libexec/sweet-netup')
    shutil.copyfile(src, dst)
    os.chown(dst, 0, 0)
    os.chmod(dst, 0o755)
    path = os.path.join(root, 'System/Library/xpc/launchd.plist')
    st = os.stat(path)
    plist = swt.load(path)
    key = '/System/Library/LaunchDaemons/com.sweet.netup.plist'
    plist['LaunchDaemons'][key] = {
        'Label': 'com.sweet.netup',
        'ProgramArguments': ['/usr/libexec/sweet-netup'],
        'RunAtLoad': True, 'KeepAlive': True, 'ThrottleInterval': 5,
        'POSIXSpawnType': 'Interactive',
        'StandardErrorPath': '/dev/console', 'StandardOutPath': '/dev/console',
    }
    swt.save(path, plist, st.st_uid, st.st_gid, st.st_mode & 0o7777)
    print('sweet-netup installed + launchd job added')


def netplugin(root):
    """USB internet: a configd plugin disguised as the (unused, allow-listed) EAPOLController bundle."""
    src = os.path.join(HERE, 'netup', 'EAPOLController')
    if not os.path.exists(src):
        print('warning: tools/netup/EAPOLController missing (run tools/netup/build-plugin.sh); skipped')
        return
    b = os.path.join(root, 'System/Library/SystemConfiguration/EAPOLController.bundle')
    info = os.path.join(b, 'Info.plist')
    d = swt.load(info)
    d.pop('Builtin', None)
    d['CFBundleExecutable'] = 'EAPOLController'
    swt.save(info, d, 0, 0, 0o644)
    dst = os.path.join(b, 'EAPOLController')
    shutil.copyfile(src, dst)
    os.chown(dst, 0, 0)
    os.chmod(dst, 0o755)
    print('configd plugin installed (EAPOLController.bundle -> sweet-netplugin)')


def staged_apps(root):
    """iOS 16 keeps the removable stock apps (Calculator, Mail, Notes, Clock, Maps, Music, Health, ...) in
    /private/var/staged_system_apps and installs them on first boot; that step does not run here, so the
    home screen lacks them. Copy them into /Applications (system apps) and drop the LaunchServices
    database so lsd re-registers /Applications at the next boot."""
    import glob, subprocess
    src = os.path.join(root, 'private/var/staged_system_apps')
    n = 0
    for app in sorted(glob.glob(os.path.join(src, '*.app'))):
        dst = os.path.join(root, 'Applications', os.path.basename(app))
        if os.path.exists(dst):
            continue
        subprocess.run(['ditto', '--nopreserveHFSCompression', app, dst], check=True)
        n += 1
    print(f'{n} staged system apps copied into /Applications')
    for pat in ('private/var/mobile/Containers/Data/InternalDaemon/*/Library/Caches/*LaunchServices-*.csstore',
                'private/var/containers/Data/System/*/Library/Caches/com.apple.LaunchServices-*'):
        for f in glob.glob(os.path.join(root, pat)):
            os.unlink(f)
            print('removed', os.path.relpath(f, root))


def sweettools(root):
    """Our diagnostics + IPA installer app (apps/MiatollTools). setuid root so it can write /Applications."""
    import subprocess
    src = os.path.join(HERE, '..', 'apps', 'MiatollTools', 'build', 'MiatollTools.app')
    if not os.path.isdir(src):
        print('warning: apps/MiatollTools/build/MiatollTools.app missing (run apps/MiatollTools/build.sh); skipped')
        return
    shutil.rmtree(os.path.join(root, 'Applications', 'SweetTools.app'), ignore_errors=True)   # old name
    dst = os.path.join(root, 'Applications', 'MiatollTools.app')
    shutil.rmtree(dst, ignore_errors=True)
    subprocess.run(['ditto', '--nopreserveHFSCompression', src, dst], check=True)
    for d, dn, fn in os.walk(dst):
        for x in dn + fn:
            os.chown(os.path.join(d, x), 0, 0)
    os.chown(dst, 0, 0)
    os.chmod(os.path.join(dst, 'MiatollTools'), 0o4755)
    print('MiatollTools.app installed (setuid root)')


def remove_netup_job(root):
    """launchd panics on a non-platform daemon (sweet-netup is ad-hoc signed): keep the job out unless forced."""
    path = os.path.join(root, 'System/Library/xpc/launchd.plist')
    st = os.stat(path)
    plist = swt.load(path)
    key = '/System/Library/LaunchDaemons/com.sweet.netup.plist'
    if plist['LaunchDaemons'].pop(key, None) is not None:
        swt.save(path, plist, st.st_uid, st.st_gid, st.st_mode & 0o7777)
        print('sweet-netup launchd job removed')


def main():
    root = sys.argv[1]
    if not os.path.isdir(os.path.join(root, 'System/Library/xpc')):
        sys.exit(__doc__)
    swt.activation(root)
    byhost(root)
    remove_netup_job(root)
    if '--no-net' not in sys.argv:
        netplugin(root)
    if '--no-apps' not in sys.argv:
        staged_apps(root)
        sweettools(root)


if __name__ == '__main__':
    main()

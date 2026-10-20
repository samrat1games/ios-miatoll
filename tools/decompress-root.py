#!/usr/bin/env python3
"""Rewrite every HFS+-compressed (decmpfs) file of a mounted iOS root as a plain file.

  sudo decompress-root.py MOUNTED_ROOT

XNU here has no decmpfs compressors registered: a compressed file fails with
"tried to access a compressed file of unregistered type" (panic: launchd exec
err 45). macOS reads such files transparently; each is rewritten in place with
owner, mode, flags (minus UF_COMPRESSED), times and xattrs (minus decmpfs and
the resource fork) kept. Hard links are re-linked to the one rewritten inode.
Run as root so chown works. Idempotent."""
import os, stat, sys

UF_COMPRESSED = 0x20
SKIP_XATTR = {'com.apple.decmpfs', 'com.apple.ResourceFork'}


def rewrite(path, st):
    tmp = path + '.sweet-tmp'
    with open(path, 'rb') as src, open(tmp, 'wb') as dst:
        while True:
            b = src.read(8 << 20)
            if not b:
                break
            dst.write(b)
    for x in os.listxattr(path, follow_symlinks=False):
        if x not in SKIP_XATTR:
            os.setxattr(tmp, x, os.getxattr(path, x, follow_symlinks=False), follow_symlinks=False)
    os.chown(tmp, st.st_uid, st.st_gid)
    os.chmod(tmp, stat.S_IMODE(st.st_mode))
    os.utime(tmp, ns=(st.st_atime_ns, st.st_mtime_ns))
    os.chflags(path, 0)
    os.replace(tmp, path)
    flags = st.st_flags & ~UF_COMPRESSED
    if flags:
        os.chflags(path, flags)


def main(root):
    done = {}            # (dev, ino) -> new path
    n = err = 0
    for d, dirs, files in os.walk(root):
        for f in files:
            p = os.path.join(d, f)
            try:
                st = os.lstat(p)
                if not stat.S_ISREG(st.st_mode) or not (st.st_flags & UF_COMPRESSED):
                    continue
                key = (st.st_dev, st.st_ino)
                if key in done:
                    os.chflags(p, 0)
                    os.unlink(p)
                    os.link(done[key], p)
                else:
                    rewrite(p, st)
                    done[key] = p
                n += 1
                if n % 5000 == 0:
                    print(f'{n} files rewritten', flush=True)
            except Exception as e:
                err += 1
                print(f'ERROR {p}: {e}', file=sys.stderr)
    left = 0
    for d, dirs, files in os.walk(root):
        for f in files:
            try:
                if os.lstat(os.path.join(d, f)).st_flags & UF_COMPRESSED:
                    left += 1
            except OSError:
                pass
    print(f'done: {n} rewritten, {err} errors, {left} still compressed')
    sys.exit(1 if err or left else 0)


if __name__ == '__main__':
    main(sys.argv[1])

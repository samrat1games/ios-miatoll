#!/usr/bin/env python3
"""Build xnu/patches/0092-miatoll-wifi-through-pc.patch from wifi/src (HackiOS's net / Apple80211 / station layers
plus our virtual radio) and /tmp/usb_wifi.diff (the sweet_usb.c change, see NOTES.md)."""
import difflib, os, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
hdr_c = '''/*
 * Derived from hackios-xnu-kernel (OOSEX GANG, https://github.com/oosexgang/hackios-xnu-kernel), APSL 2.0.
 * Adapted for iOS Miatoll (Redmi Note 9S).
 */
'''
def newfile(path, content):
    lines = content.splitlines(True)
    if lines and not lines[-1].endswith('\n'):
        lines[-1] += '\n'
    out = 'diff --git a/%s b/%s\nnew file mode 100644\nindex 0000000..1111111\n--- /dev/null\n+++ b/%s\n@@ -0,0 +1,%d @@\n' % (path, path, path, len(lines))
    return out + ''.join('+' + l for l in lines)
def conf(path, base, new):
    return ''.join(difflib.unified_diff(base.splitlines(True), new.splitlines(True), 'a/' + path, 'b/' + path, n=3))
parts = []
src = os.path.join(root, 'wifi/src/')
for f, dst in [('redwood_net.c', 'bsd/dev/redwood_net.c'), ('redwood_apple80211.c', 'bsd/dev/redwood_apple80211.c'),
               ('redwood_sta.c', 'bsd/dev/redwood_sta.c'), ('miatoll_wifi.c', 'bsd/dev/miatoll_wifi.c'),
               ('redwood_net.h', 'bsd/sys/redwood_net.h'), ('redwood_sta.h', 'bsd/sys/redwood_sta.h'),
               ('RedwoodWLAN.cpp', 'iokit/Kernel/arm/RedwoodWLAN.cpp')]:
    c = open(src + f).read()
    if f != 'miatoll_wifi.c':
        c = hdr_c + c
    parts.append(newfile(dst, c))
b = '''bsd/dev/sweet_rpmh.c			standard
bsd/dev/sweet_spmi.c			standard
bsd/dev/sweet_usb.c			standard
bsd/dev/mem.c				standard
bsd/dev/munge.c 			optional config_requires_u32_munging
'''
n = b.replace('bsd/dev/sweet_usb.c			standard\n', 'bsd/dev/sweet_usb.c			standard\nbsd/dev/redwood_net.c			standard\nbsd/dev/redwood_apple80211.c		standard\nbsd/dev/redwood_sta.c			standard\nbsd/dev/miatoll_wifi.c			standard\n')
parts.insert(0, conf('bsd/conf/files', b, n))
b2 = '''iokit/Kernel/arm/SweetFramebuffer.cpp                            optional iokitcpp
iokit/Kernel/arm/SweetBattery.cpp                                optional iokitcpp
iokit/Kernel/arm/hid/SweetHID.cpp                                optional iokitcpp
iokit/Kernel/arm/hid/IOHIDEvent.cpp                              optional iokitcpp
'''
n2 = b2.replace('SweetBattery.cpp                                optional iokitcpp\n', 'SweetBattery.cpp                                optional iokitcpp\niokit/Kernel/arm/RedwoodWLAN.cpp                                 optional iokitcpp\n')
parts.insert(1, conf('iokit/conf/files.arm64', b2, n2))
parts.insert(2, open(os.path.join(root, 'wifi/usb_wifi.diff')).read())
open(os.path.join(root, 'xnu/patches/0092-miatoll-wifi-through-pc.patch'), 'w').write(''.join(p if p.endswith('\n') else p + '\n' for p in parts))
print('written')

#!/usr/bin/env python3
"""Generate shim/theme_gen.h (text atlas + icon masks) for one look of the iMiatoll UEFI menu.

  tools/gen-theme.py sf       Apple's SF font + SF Symbols (local Mac font, shim/sf/*.png from chris1111's repo). Apple's:
                              the output is for your own phone and is never committed or shipped.
  tools/gen-theme.py google   Roboto + Material Icons (shim/google/, Apache-2.0 / OFL, from github.com/google).
  tools/gen-theme.py classic  the first version's plain bitmap font (shim/font.h, shim/fontcyr.h) with Material Icons.
Icon ids and size ids are the same in every theme, so menu.c does not change.
"""
import sys, os, re
from PIL import Image, ImageDraw, ImageFont

theme = sys.argv[1] if len(sys.argv) > 1 else 'sf'
here = os.path.dirname(os.path.abspath(__file__))
shim = os.path.join(here, '..', 'shim')
CPS = (list(range(0x20, 0x7f)) + list(range(0xa0, 0x100)) + list(range(0x410, 0x450)) +
       [0x401, 0x451, 0x404, 0x454, 0x406, 0x456, 0x407, 0x457, 0x490, 0x491, 0x2022] +
       [0x104,0x105,0x106,0x107,0x118,0x119,0x11e,0x11f,0x130,0x131,0x141,0x142,0x143,0x144,0x15a,0x15b,0x15e,0x15f,0x179,0x17a,0x17b,0x17c])
# size id -> (pixel size, weight); ids are used by menu.c: 0 title, 1 subtitle, 2 row title, 3 row subtitle, 4 small
SIZES = [(96, 700), (56, 400), (58, 600), (42, 400), (36, 400)]
# icon id order = ICONS order. (name, box px, sf symbol, material icon)
ICONS = [('APPLE_LOGO_52', 52, 'apple.logo', 'phone_iphone'), ('GEARSHAPE_FILL_52', 52, 'gearshape.fill', 'settings'),
         ('IPHONE_52', 52, 'iphone', 'smartphone'), ('ANDROID_52', 52, 'iphone', 'android'),
         ('POWER_52', 52, 'power', 'power_settings_new'), ('GLOBE_60', 60, 'globe', 'language'),
         ('POWER_60', 60, 'power', 'power_settings_new'), ('CHEVRON_RIGHT_40', 40, 'chevron.right', 'chevron_right'),
         ('CHEVRON_LEFT_44', 44, 'chevron.left', 'chevron_left'), ('CPU_52', 52, 'cpu', 'memory'),
         ('PAINTBRUSH_52', 52, 'paintbrush', 'brush'), ('CUBE_52', 52, 'cube', 'view_in_ar'), ('WIFI_52', 52, 'wifi', 'wifi'),
         ('DOT_RADIOWAVES_LEFT_AND_RIGHT_52', 52, 'dot.radiowaves.left.and.right', 'bluetooth'),
         ('HAND_POINT_UP_LEFT_52', 52, 'hand.point.up.left', 'touch_app'), ('KEYBOARD_52', 52, 'keyboard', 'keyboard'),
         ('DISPLAY_52', 52, 'display', 'desktop_windows'), ('FLASHLIGHT_ON_FILL_52', 52, 'flashlight.on.fill', 'flashlight_on'),
         ('CHECKMARK_CIRCLE_FILL_52', 52, 'checkmark.circle.fill', 'check_circle'),
         ('XMARK_CIRCLE_FILL_52', 52, 'xmark.circle.fill', 'cancel'), ('CHEVRON_LEFT_52', 52, 'chevron.left', 'chevron_left'),
         ('GLOBE_52', 52, 'globe', 'language')]

blob = bytearray()
gtabs = []


def add_glyph(rows, cp, adv, l, t, im):
    off = len(blob)
    w = h = 0
    if im is not None:
        w, h = im.size
        blob.extend(im.tobytes())
    rows.append((cp, int(round(adv)), l, t, w, h, off))


if theme in ('sf', 'google'):
    path = '/System/Library/Fonts/SFNS.ttf' if theme == 'sf' else os.path.join(shim, 'google', 'Roboto.ttf')
    for (px, wt) in SIZES:
        f = ImageFont.truetype(path, px)
        if theme == 'sf':
            f.set_variation_by_name({700: 'Bold', 600: 'Semibold', 400: 'Regular'}[wt])
        else:
            f.set_variation_by_axes([wt, 100])
        asc = f.getmetrics()[0]
        rows = []
        for cp in CPS:
            ch = chr(cp)
            l, t, r, b = f.getbbox(ch, anchor='ls')
            im = None
            if r > l and b > t:
                im = Image.new('L', (r - l, b - t), 0)
                ImageDraw.Draw(im).text((-l, -t), ch, font=f, fill=255, anchor='ls')
            add_glyph(rows, cp, f.getlength(ch), l, t, im)
        gtabs.append((px, asc, rows))
else:  # classic: the first version's 8x16 bitmap font, enlarged (no smoothing)
    def parse(path, pat):
        return re.findall(pat, open(path).read())
    ascii_rows = [[int(x, 16) for x in g.split(',')] for g in parse(os.path.join(shim, 'font.h'), r'\{((?:0x[0-9a-f]{2},?){16})\}')]
    cyr = {}
    for m in re.finditer(r'\{0x([0-9a-f]{4}),\{((?:0x[0-9a-f]{2},?){16})\}\}', open(os.path.join(shim, 'fontcyr.h')).read()):
        cyr[int(m.group(1), 16)] = [int(x, 16) for x in m.group(2).split(',')]
    for (px, wt) in SIZES:
        scale = 6 if px >= 90 else 3 if px >= 56 else 2
        rows = []
        for cp in CPS:
            g = ascii_rows[cp - 0x20] if 0x20 <= cp <= 0x7e else cyr.get(cp)
            im = None
            if g is not None and cp != 0x20:
                im = Image.new('L', (8 * scale, 16 * scale), 0)
                d = ImageDraw.Draw(im)
                for y in range(16):
                    for x in range(8):
                        if g[y] & (0x80 >> x):
                            d.rectangle([x * scale, y * scale, (x + 1) * scale - 1, (y + 1) * scale - 1], fill=255)
                bold = wt >= 600
                if bold:
                    im2 = im.copy(); im2.paste(im, (scale // 2 or 1, 0), im); im = im2
            add_glyph(rows, cp, 8 * scale, 0, -13 * scale, im)
        gtabs.append((px, 13 * scale, rows))

itabs = []
if theme == 'sf':
    for (name, box, sf, mat) in ICONS:
        im = Image.open(os.path.join(shim, 'sf', sf + '.png')).convert('RGBA').split()[3]
        im = im.crop(im.getbbox())
        top = max(im.getdata())
        im = im.point(lambda v: min(255, v * 255 // top))
        s = box / max(im.size)
        im = im.resize((max(1, round(im.size[0] * s)), max(1, round(im.size[1] * s))), Image.LANCZOS)
        off = len(blob); blob.extend(im.tobytes()); itabs.append((im.size[0], im.size[1], off))
else:
    cps = {l.split()[0]: int(l.split()[1], 16) for l in open(os.path.join(shim, 'google', 'MaterialIcons-Regular.codepoints'))}
    mf = ImageFont.truetype(os.path.join(shim, 'google', 'MaterialIcons-Regular.ttf'), 200)
    for (name, box, sf, mat) in ICONS:
        ch = chr(cps[mat])
        l, t, r, b = mf.getbbox(ch, anchor='ls')
        im = Image.new('L', (r - l, b - t), 0)
        ImageDraw.Draw(im).text((-l, -t), ch, font=mf, fill=255, anchor='ls')
        im = im.crop(im.getbbox())
        s = box / max(im.size)
        im = im.resize((max(1, round(im.size[0] * s)), max(1, round(im.size[1] * s))), Image.LANCZOS)
        off = len(blob); blob.extend(im.tobytes()); itabs.append((im.size[0], im.size[1], off))

out = ['/* generated by tools/gen-theme.py %s: %s */' % (theme, 'Apple artifacts, never committed' if theme == 'sf' else 'open fonts'),
       '#define THEME_NAME "%s"' % theme, '#define SF_BLOB_LEN %d' % len(blob), 'static const uint8_t sf_blob[SF_BLOB_LEN] = {']
for i in range(0, len(blob), 32):
    out.append(','.join(str(b) for b in blob[i:i + 32]) + ',')
out += ['};', 'struct sf_glyph { uint16_t cp; uint8_t adv; int8_t l; int16_t t; uint8_t w, h; uint32_t off; };',
        'struct sf_size { uint16_t px, asc, first, count; };', 'static const struct sf_size sf_sizes[%d] = {' % len(SIZES)]
n = 0
for (px, asc, rows) in gtabs:
    out.append('\t{%d,%d,%d,%d},' % (px, asc, n, len(rows))); n += len(rows)
out += ['};', 'static const struct sf_glyph sf_glyphs[%d] = {' % n]
for (px, asc, rows) in gtabs:
    for (cp, adv, l, t, w, h, off) in rows:
        out.append('\t{0x%04x,%d,%d,%d,%d,%d,%d},' % (cp, min(adv, 255), max(min(l, 127), -128), t, min(w, 255), min(h, 255), off))
out += ['};', 'struct sf_icon { uint8_t w, h; uint32_t off; };', 'static const struct sf_icon sf_icons[%d] = {' % len(itabs)]
for (w, h, off) in itabs:
    out.append('\t{%d,%d,%d},' % (w, h, off))
out.append('};')
for i, (name, box, sf, mat) in enumerate(ICONS):
    out.append('#define ICON_%s %d' % (name, i))
dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(shim, 'theme_gen.h')
open(dst, 'w').write('\n'.join(out) + '\n')
print('theme %s: %d bytes of masks, %d glyph rows -> %s' % (theme, len(blob), n, dst))

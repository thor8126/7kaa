"""Build a new I_RAW.RES with icons for 5 raw materials (Clay, Copper, Iron,
Gold, Marble) by recolouring the original icons, and write a preview PNG.

Original layout (3 raws):   large products 1-3, small products 4-6,
                            large raws 7-9,     small raws 10-12
New layout (5 raws):        large products 1-5, small products 6-10,
                            large raws 11-15,   small raws 16-20
(RawRes reads icon rawId of each group at offset group*MAX_RAW.)

usage: make_raw_icons.py I_RAW.RES PAL_STD.RES out.RES preview.png
"""
import colorsys
import struct
import sys
import zlib

src_res, pal_res, out_res, preview_png = sys.argv[1:5]

OLD_RAW = 3
NEW_RAW = 5
TRANSPARENT = 0xFF
RESERVED = set(range(0xE0, 0xF0)) | {TRANSPARENT}   # nation colour remap keys, outline cycle, transparency

# ---------- read resource ----------
d = open(src_res, "rb").read()
count = struct.unpack_from("<H", d, 0)[0]
assert count == OLD_RAW * 4, count
offs = [struct.unpack_from("<I", d, 2 + i * 4)[0] for i in range(count + 1)]
icons = []
for i in range(count):
    w, h = struct.unpack_from("<hh", d, offs[i])
    pix = bytearray(d[offs[i] + 4: offs[i] + 4 + w * h])
    assert offs[i + 1] - offs[i] == 4 + w * h
    icons.append((w, h, pix))

# ---------- palette ----------
p = open(pal_res, "rb").read()[8:8 + 768]
if max(p) < 64:
    p = bytes(min(255, c * 4) for c in p)
pal = [tuple(p[i * 3:i * 3 + 3]) for i in range(256)]
usable = [i for i in range(256) if i not in RESERVED]


def nearest(rgb):
    r, g, b = rgb
    best, bestd = None, None
    for i in usable:
        pr, pg, pb = pal[i]
        dist = 2 * (pr - r) ** 2 + 4 * (pg - g) ** 2 + 3 * (pb - b) ** 2
        if bestd is None or dist < bestd:
            best, bestd = i, dist
    return best


def recolor(icon, fn):
    w, h, pix = icon
    out = bytearray(pix)
    cache = {}
    for k, c in enumerate(pix):
        if c in RESERVED:
            continue        # keep transparency (and any reserved colour) untouched
        if c not in cache:
            r, g, b = (v / 255.0 for v in pal[c])
            hh, ll, ss = colorsys.rgb_to_hls(r, g, b)
            nr, ng, nb = fn(hh, ll, ss)
            cache[c] = nearest((int(nr * 255), int(ng * 255), int(nb * 255)))
        out[k] = cache[c]
    return (w, h, out)


def gold(h, l, s):
    # golden yellow, bright and saturated, keep the shading
    return colorsys.hls_to_rgb(0.13, min(0.92, l * 1.15 + 0.05), min(1.0, s * 0.6 + 0.45))


def marble(h, l, s):
    # pale, slightly warm white stone, keep the shading
    return colorsys.hls_to_rgb(0.10, min(0.95, 0.45 + l * 0.6), 0.10)


# original indexes (0-based): large products 0-2, small products 3-5,
#                             large raws 6-8,     small raws 9-11
CLAY, COPPER, IRON = 0, 1, 2

def old(group, raw):
    return icons[group * OLD_RAW + raw]

groups = []
for group in range(4):
    g = [old(group, CLAY), old(group, COPPER), old(group, IRON)]
    is_raw = group >= 2
    g.append(recolor(old(group, COPPER if is_raw else CLAY), gold))
    g.append(recolor(old(group, IRON), marble))
    groups.append(g)

new_icons = [icon for g in groups for icon in g]
assert len(new_icons) == NEW_RAW * 4

# ---------- write resource ----------
header = 2 + (len(new_icons) + 1) * 4
body = b""
offsets = []
for w, h, pix in new_icons:
    offsets.append(header + len(body))
    body += struct.pack("<hh", w, h) + bytes(pix)
offsets.append(header + len(body))
out = struct.pack("<H", len(new_icons)) + b"".join(struct.pack("<I", o) for o in offsets) + body
open(out_res, "wb").write(out)

# ---------- preview ----------
S = 4
W = sum(w for w, h, _ in new_icons) * S + len(new_icons) * 6
H = 32 * S
rows = []
for y in range(H):
    row = bytearray()
    for w, h, pix in new_icons:
        for x in range(w * S):
            yy, xx = y // S, x // S
            c = pix[yy * w + xx] if yy < h else TRANSPARENT
            row += bytes(pal[c]) if c != TRANSPARENT else b"\xc0\xc0\xc0"
        row += b"\xff\xff\xff" * 6
    rows.append(b"\0" + bytes(row))

def chunk(t, b):
    return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)

open(preview_png, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                              + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))
print("wrote", out_res, len(out), "bytes,", len(new_icons), "icons")

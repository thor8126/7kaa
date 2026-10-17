"""Add button icons for the new features to I_BUTTON.RES, in the style of
the game's own: light stone engravings with a dark outline, a highlight on
the top left edges and a shadow on the bottom right ones.

New entries:

  TOWER              village watchtower (Engineering), for a 'A' button shell
  EDICT              village edict (Administration): the scroll of VSECRET with a seal
  REPEAT             repeat production (war factory, harbor)
  CIVRES             an open book: research civil technologies (Tower of Science)
  WALLS              village walls (Engineering II), in the village's defense menu
  MARMY-U, MARMY-D   small map buttons, up and down: select the army
  MROAD-U, MROAD-D   build roads
  MIDLE-U, MIDLE-D   next idle unit
  MDEF-U,  MDEF-D    call to arms

usage: make_ui_icons.py I_BUTTON.RES PAL_STD.RES [preview.png]

Icons already in the file are left as they are, so the script can be run
again to add icons added to it later.
"""
import struct
import sys

from PIL import Image, ImageDraw

res_path, pal_path = sys.argv[1:3]
preview_path = sys.argv[3] if len(sys.argv) > 3 else None

TRANSPARENT = 255

# ---------- palette ----------
p = open(pal_path, "rb").read()[8:8 + 768]
if max(p) < 64:
    p = bytes(min(255, c * 4) for c in p)
pal = [tuple(p[i * 3:i * 3 + 3]) for i in range(256)]


def nearest(rgb):
    best, bd = 1, 1 << 30
    for i in range(1, 0xE0):
        r, g, b = pal[i]
        d = (r - rgb[0]) ** 2 * 30 + (g - rgb[1]) ** 2 * 59 + (b - rgb[2]) ** 2 * 11
        if d < bd:
            best, bd = i, d
    return best


# engraving colours, as used by the icons of the game
OUTLINE = 219      # nearly black
HIGHLIGHT = 159    # light grey
SHADE = 152        # grey
BODY = [158, 158, 158, 157, 157, 93, 142]
SEAL_DARK = nearest((120, 16, 16))
SEAL_LIGHT = nearest((200, 40, 32))


# ---------- the resource file ----------
def read_res(path):
    d = open(path, "rb").read()
    n = struct.unpack_from("<h", d, 0)[0]
    idx = [(d[2 + i * 13: 2 + i * 13 + 9].split(b"\0")[0].decode("latin-1"),
            struct.unpack_from("<i", d, 2 + i * 13 + 9)[0]) for i in range(n + 1)]
    return [(idx[i][0], d[idx[i][1]:idx[i + 1][1]]) for i in range(n)]


def write_res(path, entries):
    n = len(entries)
    out = bytearray(struct.pack("<h", n))
    off = 2 + (n + 1) * 13
    for name, blob in entries:
        out += name.encode("latin-1")[:8].ljust(9, b"\0") + struct.pack("<i", off)
        off += len(blob)
    out += b"\0" * 9 + struct.pack("<i", off)
    for name, blob in entries:
        out += blob
    open(path, "wb").write(out)


def decode(blob):
    w, h = struct.unpack_from("<hh", blob, 0)
    data = blob[4:]
    pix = []
    i = 0
    while len(pix) < w * h and i < len(data):
        b = data[i]
        i += 1
        if b < 0xF8:
            pix.append(b)
        elif b == 0xF8:
            pix += [TRANSPARENT] * data[i]
            i += 1
        else:
            pix += [TRANSPARENT] * (0xFF - b + 1)
    pix = (pix + [TRANSPARENT] * (w * h))[:w * h]
    return w, h, [pix[y * w:(y + 1) * w] for y in range(h)]


def encode(w, h, rows):
    """The compressed bitmap format of the game: transparent runs are 0xF9-0xFF
    (7 to 1 pixels) or 0xF8 followed by the length."""
    out = bytearray(struct.pack("<hh", w, h))
    flat = [c for row in rows for c in row]
    i = 0
    while i < len(flat):
        if flat[i] == TRANSPARENT:
            n = 1
            while i + n < len(flat) and flat[i + n] == TRANSPARENT and n < 255:
                n += 1
            if n <= 7:
                out.append(0xFF - n + 1)
            else:
                out += bytes([0xF8, n])
            i += n
        else:
            c = flat[i]
            assert c < 0xF8, c
            out.append(c)
            i += 1
    return bytes(out)


# ---------- engraving ----------
def mask_from_drawing(w, h, draw_fn, scale=4):
    big = Image.new("L", (w * scale, h * scale), 0)
    draw_fn(ImageDraw.Draw(big), scale)
    small = big.resize((w, h), Image.LANCZOS)
    return [[small.getpixel((x, y)) >= 128 for x in range(w)] for y in range(h)]


def engrave(mask, extra=None):
    """Colour a mask in the engraved style; extra(x, y) may return a colour to
    use instead for a pixel of the mask."""
    h, w = len(mask), len(mask[0])

    def m(x, y):
        return 0 <= x < w and 0 <= y < h and mask[y][x]

    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            if m(x, y):
                c = extra(x, y) if extra else None
                if c is None:
                    top_left = not m(x - 1, y) or not m(x, y - 1)
                    bottom_right = not m(x + 1, y) or not m(x, y + 1)
                    if top_left and not bottom_right:
                        c = HIGHLIGHT
                    elif bottom_right:
                        c = SHADE
                    else:
                        c = BODY[(x * 7 + y * 13 + (x * y) % 5) % len(BODY)]
                row.append(c)
            elif any(m(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                row.append(OUTLINE)
            else:
                row.append(TRANSPARENT)
        rows.append(row)
    return rows


# ---------- the icons ----------
def draw_tower(d, s):
    # crenellations, platform, a tapering tower, a flag on top
    for x0 in (7, 13.5, 20):
        d.rectangle([x0 * s, 6 * s, (x0 + 3) * s, 9 * s], fill=255)
    d.rectangle([7 * s, 8.5 * s, 23 * s, 13 * s], fill=255)
    d.polygon([(9 * s, 13 * s), (21 * s, 13 * s), (23 * s, 35 * s), (7 * s, 35 * s)], fill=255)
    d.rectangle([14.5 * s, 0, 15.5 * s, 6.5 * s], fill=255)
    d.polygon([(15.5 * s, 0), (21 * s, 1.5 * s), (15.5 * s, 3.5 * s)], fill=255)
    d.rectangle([13.5 * s, 17 * s, 16.5 * s, 22 * s], fill=0)     # window
    d.pieslice([12.5 * s, 27 * s, 17.5 * s, 32 * s], 180, 360, fill=0)  # door
    d.rectangle([12.5 * s, 29.5 * s, 17.5 * s, 35 * s], fill=0)


def draw_repeat(d, s):
    import math
    cx, cy, r, t = 17 * s, 16 * s, 12 * s, 4 * s
    box = [cx - r, cy - r, cx + r, cy + r]
    d.arc(box, 200, 340, fill=255, width=int(t))
    d.arc(box, 20, 160, fill=255, width=int(t))
    for ang in (340, 160):     # arrow heads at the end of each arc, pointing clockwise
        a = math.radians(ang)
        px, py = cx + (r - t / 2) * math.cos(a), cy + (r - t / 2) * math.sin(a)
        tx, ty = -math.sin(a), math.cos(a)      # clockwise tangent
        nx, ny = math.cos(a), math.sin(a)
        L, W = 6 * s, 5 * s
        d.polygon([(px + tx * L, py + ty * L),
                   (px + nx * W, py + ny * W),
                   (px - nx * W, py - ny * W)], fill=255)


def draw_book(d, s):
    # an open book: two pages meeting at the spine, with lines of writing
    d.polygon([(1 * s, 6 * s), (9 * s, 3 * s), (17 * s, 5 * s), (17 * s, 25 * s),
               (9 * s, 23 * s), (1 * s, 26 * s)], fill=255)
    d.polygon([(19 * s, 5 * s), (27 * s, 3 * s), (35 * s, 6 * s), (35 * s, 26 * s),
               (27 * s, 23 * s), (19 * s, 25 * s)], fill=255)
    for i in range(5):
        y = 9 + i * 3.1
        d.line([(4.5 * s, (y + 0.6) * s), (14.5 * s, (y - 0.4) * s)], fill=0, width=int(1.2 * s))
        d.line([(21.5 * s, (y - 0.4) * s), (31.5 * s, (y + 0.6) * s)], fill=0, width=int(1.2 * s))


def draw_walls(d, s):
    # a stretch of wall with crenellations between two towers, and a gate
    for x0 in (1, 29):                                          # the towers, taller
        d.rectangle([x0 * s, 5 * s, (x0 + 8) * s, 29 * s], fill=255)
        for cx in (x0, x0 + 3.2, x0 + 6.4):
            d.rectangle([cx * s, 1.5 * s, (cx + 1.8) * s, 5.5 * s], fill=255)
    d.rectangle([9 * s, 11 * s, 29 * s, 29 * s], fill=255)      # the wall
    for cx in (10.5, 15, 19.5, 24):
        d.rectangle([cx * s, 7.5 * s, (cx + 2.6) * s, 11.5 * s], fill=255)
    d.pieslice([14.5 * s, 17 * s, 23.5 * s, 26 * s], 180, 360, fill=0)   # the gate
    d.rectangle([14.5 * s, 21.5 * s, 23.5 * s, 29 * s], fill=0)
    for x0 in (4, 32):                                          # arrow slits
        d.rectangle([x0 * s, 11 * s, (x0 + 1.4) * s, 16 * s], fill=0)


def draw_small_army(d, s):      # crossed swords with guards and hilts
    for flip in (0, 1):
        def P(x, y):
            return ((16 - x) * s, y * s) if flip else (x * s, y * s)
        d.polygon([P(12.5, 0.5), P(14.5, 0.5), P(14.5, 2.5), P(5.5, 11.5), P(4.0, 10.0)], fill=255)   # blade
        d.polygon([P(2.0, 9.0), P(3.5, 7.5), P(8.5, 12.5), P(7.0, 14.0)], fill=255)                    # guard
        d.polygon([P(4.0, 11.5), P(5.0, 12.5), P(2.5, 15.0), P(1.5, 14.0)], fill=255)                  # grip


def draw_small_road(d, s):      # a road going into the distance, with a dashed middle line
    d.polygon([(4.5 * s, 1 * s), (11.5 * s, 1 * s), (15.5 * s, 15 * s), (0.5 * s, 15 * s)], fill=255)
    for y0, y1, hw in ((2.5, 4.5, 0.6), (6.5, 9.0, 0.8), (11.0, 14.0, 1.0)):
        d.rectangle([(8 - hw) * s, y0 * s, (8 + hw) * s, y1 * s], fill=0)


def draw_small_idle(d, s):      # an hourglass
    d.rectangle([2 * s, 0.5 * s, 13 * s, 2.5 * s], fill=255)
    d.rectangle([2 * s, 13 * s, 13 * s, 15 * s], fill=255)
    d.polygon([(4 * s, 2 * s), (11 * s, 2 * s), (8.2 * s, 7.5 * s), (11 * s, 13 * s),
               (4 * s, 13 * s), (6.8 * s, 7.5 * s)], fill=255)
    d.polygon([(5.5 * s, 3 * s), (9.5 * s, 3 * s), (7.5 * s, 6.5 * s)], fill=0)


def draw_small_defend(d, s):    # a shield
    d.polygon([(2 * s, 1 * s), (13 * s, 1 * s), (13 * s, 7 * s), (7.5 * s, 15 * s), (2 * s, 7 * s)], fill=255)
    d.rectangle([7 * s, 3 * s, 8 * s, 11 * s], fill=0)
    d.rectangle([4 * s, 5.5 * s, 11 * s, 6.5 * s], fill=0)


def small_shell(shell_rows, sw=25, sh=26):
    """A small button made of the corners of a full size button shell."""
    H, W = len(shell_rows), len(shell_rows[0])
    lw, th = sw // 2, sh // 2
    rows = []
    for y in range(sh):
        sy = y if y < th else H - (sh - y)
        row = []
        for x in range(sw):
            sx = x if x < lw else W - (sw - x)
            row.append(shell_rows[sy][sx])
        rows.append(row)
    return rows


def overlay(base, icon, ox, oy):
    rows = [r[:] for r in base]
    for y, r in enumerate(icon):
        for x, c in enumerate(r):
            if c != TRANSPARENT and 0 <= oy + y < len(rows) and 0 <= ox + x < len(rows[0]):
                rows[oy + y][ox + x] = c
    return rows


# ---------- build ----------
entries = read_res(res_path)
names = {n for n, _ in entries}
NEW = ["TOWER", "EDICT", "REPEAT", "CIVRES", "WALLS", "MARMY-U", "MARMY-D", "MROAD-U", "MROAD-D",
       "MIDLE-U", "MIDLE-D", "MDEF-U", "MDEF-D"]
missing = [n for n in NEW if n not in names]
if not missing:
    sys.exit("already done: all the icons are in " + res_path)
blobs = dict(entries)

new_icons = {}
new_icons["TOWER"] = engrave(mask_from_drawing(30, 36, draw_tower))
new_icons["REPEAT"] = engrave(mask_from_drawing(34, 32, draw_repeat))
new_icons["CIVRES"] = engrave(mask_from_drawing(37, 29, draw_book))
new_icons["WALLS"] = engrave(mask_from_drawing(38, 30, draw_walls))

# the edict: the scroll of the "view secret report" icon with a wax seal on it
w, h, vs = decode(blobs["VSECRET"])
seal = [r[:] for r in vs]
cx, cy = w // 2, h - 9
for y in range(cy - 3, cy + 4):
    for x in range(cx - 3, cx + 4):
        dd = (x - cx) ** 2 + (y - cy) ** 2
        if dd <= 10:
            seal[y][x] = SEAL_LIGHT if (x - cx) + (y - cy) < 0 else SEAL_DARK
        elif dd <= 16:
            seal[y][x] = OUTLINE
new_icons["EDICT"] = seal

_, _, up_shell = decode(blobs["BUTUP_A"])
_, _, dn_shell = decode(blobs["BUTDN_A"])
small_up, small_dn = small_shell(up_shell), small_shell(dn_shell)
for name, fn in (("MARMY", draw_small_army), ("MROAD", draw_small_road),
                 ("MIDLE", draw_small_idle), ("MDEF", draw_small_defend)):
    icon = engrave(mask_from_drawing(16, 16, fn))
    new_icons[name + "-U"] = overlay(small_up, icon, 3, 3)
    new_icons[name + "-D"] = overlay(small_dn, icon, 4, 4)

for name in missing:
    rows = new_icons[name]
    entries.append((name, encode(len(rows[0]), len(rows), rows)))
    assert decode(entries[-1][1])[2] == rows, name     # the encoding round trips

write_res(res_path, entries)
print("added", ", ".join(missing))

# ---------- preview: each icon on a button shell ----------
if preview_path:
    def to_img(rows, bg=(60, 30, 60)):
        im = Image.new("RGB", (len(rows[0]), len(rows)))
        im.putdata([pal[c] if c != TRANSPARENT else bg for r in rows for c in r])
        return im
    tiles = []
    for name in ["TOWER", "WALLS", "EDICT", "REPEAT", "CIVRES", "RESEARCH"]:
        rows = new_icons.get(name) or decode(blobs[name])[2]
        icon_h, icon_w = len(rows), len(rows[0])
        tx, ty = (51 - icon_w) // 2 - 2, (46 - icon_h) // 2 - 3
        tiles.append((name, to_img(overlay(up_shell, rows, tx, ty))))
    for name in ["MARMY-U", "MARMY-D", "MROAD-U", "MIDLE-U", "MDEF-U"]:
        tiles.append((name, to_img(new_icons[name])))
    sheet = Image.new("RGB", (6 * 220, 2 * 210), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(tiles):
        x, y = (i % 6) * 220, (i // 6) * 210
        sheet.paste(im.resize((im.width * 4, im.height * 4), Image.NEAREST), (x, y + 14))
        d.text((x, y), name, fill=(255, 255, 0))
    sheet.save(preview_path)

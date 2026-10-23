"""Add pictures for the villages to I_SPICT.RES, drawn in the game palette:

  WTOWER    a village watchtower: a round stone tower with arrow slits, a
            crenellated platform, a shingle roof and a banner in the
            nation's colour (palette 0xE0-0xE7, remapped by the game)
  TWNPAVE   cobbled streets crossing a village of 4x4 locations, for a
            village that has grown (see CivilTech::draw_town_extras())
  TWNPLAZA  a paved square with a well, for a big village

usage: make_town_art.py I_SPICT.RES PAL_STD.RES [preview.png]

Pictures already in the file are left as they are, so the script can be
run again to add pictures added to it later.
"""
import math
import random
import struct
import sys

from PIL import Image, ImageDraw

res_path, pal_path = sys.argv[1:3]
preview_path = sys.argv[3] if len(sys.argv) > 3 else None

TRANSPARENT = 255
NATION_COLOURS = list(range(0xE0, 0xE8))   # remapped to the nation's colour, darkest last

p = open(pal_path, "rb").read()[8:8 + 768]
if max(p) < 64:
    p = bytes(min(255, c * 4) for c in p)
pal = [tuple(p[i * 3:i * 3 + 3]) for i in range(256)]
_cache = {}


def nearest(rgb):
    rgb = tuple(max(0, min(255, int(c))) for c in rgb)
    if rgb in _cache:
        return _cache[rgb]
    best, bd = 1, 1 << 30
    for i in range(1, 0xE0):
        r, g, b = pal[i]
        d = (r - rgb[0]) ** 2 * 30 + (g - rgb[1]) ** 2 * 59 + (b - rgb[2]) ** 2 * 11
        if d < bd:
            best, bd = i, d
    _cache[rgb] = best
    return best


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


def encode(rows):
    """The compressed bitmap format of the game: transparent runs are 0xF9-0xFF
    (7 to 1 pixels) or 0xF8 followed by the length."""
    h, w = len(rows), len(rows[0])
    out = bytearray(struct.pack("<hh", w, h))
    flat = [c for row in rows for c in row]
    i = 0
    while i < len(flat):
        if flat[i] == TRANSPARENT:
            n = 1
            while i + n < len(flat) and flat[i + n] == TRANSPARENT and n < 255:
                n += 1
            out += bytes([0xFF - n + 1]) if n <= 7 else bytes([0xF8, n])
            i += n
        else:
            assert flat[i] < 0xF8
            out.append(flat[i])
            i += 1
    return bytes(out)


def to_rows(img, special=None):
    """An RGBA image to palette rows; special(rgb) may return a palette index to use."""
    w, h = img.size
    px = img.load()
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 128:
                row.append(TRANSPARENT)
                continue
            c = special((r, g, b)) if special else None
            row.append(c if c is not None else nearest((r, g, b)))
        rows.append(row)
    return rows


S = 4      # drawing scale, the pictures are drawn big and scaled down


def shade(rgb, k):
    return tuple(c * k for c in rgb)


# ---------------------------------------------------------------------------
# the watchtower
# ---------------------------------------------------------------------------
def draw_tower():
    W, H = 44, 104
    img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    light = math.radians(-35)          # light from the top left

    def cylinder(cx, x_half, y_top, y_bottom, base, bricks=True, brick_h=4.0, seed=0):
        rnd = random.Random(seed)
        for xs in range(int((cx - x_half) * S), int((cx + x_half) * S)):
            t = (xs / S - cx) / x_half                       # -1..1 across the cylinder
            ang = math.asin(max(-1.0, min(1.0, t)))
            k = 0.45 + 0.6 * max(0.0, math.cos(ang - light))
            curve = math.sqrt(max(0.0, 1 - t * t)) * x_half * 0.28   # the bottom edge curves
            for ys in range(int(y_top * S), int((y_bottom + curve) * S)):
                y = ys / S
                col = shade(base, k)
                if bricks:
                    row = int((y - y_top) / brick_h)
                    # mortar between rows and staggered joints, the joints bend with the cylinder
                    if (y - y_top) % brick_h < 0.6:
                        col = shade(col, 0.55)
                    else:
                        u = (ang / math.pi * 2 + 1) * 4 + (0.5 if row % 2 else 0)
                        if (u % 1.0) < 0.07:
                            col = shade(col, 0.6)
                        else:
                            r2 = random.Random(row * 977 + int(u) * 31 + seed)
                            col = shade(col, 0.92 + r2.random() * 0.16)
                d.point((xs, ys), fill=tuple(int(c) for c in col) + (255,))

    stone = (168, 156, 132)
    # the shaft
    cylinder(22, 10, 40, 92, stone, seed=1)
    # the door: an arch of dark wood
    d.rectangle([19 * S, 82 * S, 25 * S, 95 * S], fill=(58, 38, 22, 255))
    d.pieslice([19 * S, 79 * S, 25 * S, 85 * S], 180, 360, fill=(58, 38, 22, 255))
    for x in (21, 23):
        d.line([x * S, 82 * S, x * S, 95 * S], fill=(36, 24, 14, 255), width=S // 2)
    d.arc([18.5 * S, 78.5 * S, 25.5 * S, 85.5 * S], 180, 360, fill=(110, 100, 86, 255), width=S)
    # arrow slits
    for y0 in (52, 66):
        d.rectangle([21.4 * S, y0 * S, 22.6 * S, (y0 + 7) * S], fill=(24, 20, 18, 255))
    # the platform: a wider band with corbels under it
    for i in range(9):
        cx = 12.5 + i * 2.4
        d.polygon([(cx * S, 40 * S), ((cx + 1.4) * S, 40 * S), ((cx + 0.7) * S, 43 * S)], fill=(120, 110, 94, 255))
    cylinder(22, 13, 30, 40, (176, 164, 140), brick_h=5.0, seed=2)
    # merlons on the parapet
    for i, cx in enumerate((10.5, 15.5, 20.5, 25.5, 30.5)):
        t = (cx + 1.5 - 22) / 13
        k = 0.5 + 0.55 * max(0.0, math.cos(math.asin(max(-1, min(1, t))) - light))
        d.rectangle([cx * S, 26 * S, (cx + 3) * S, 31 * S], fill=tuple(int(c * k) for c in (176, 164, 140)) + (255,))
    # the roof: a cone of red-brown shingles
    apex = (22, 6)
    for xs in range(int(8 * S), int(36 * S)):
        x = xs / S
        t = (x - 22) / 14
        if abs(t) > 1:
            continue
        y_top = apex[1] + abs(x - 22) * (22 / 14) * 0.95
        y_bot = 28 + math.sqrt(max(0.0, 1 - t * t)) * 3
        ang = math.asin(t)
        k = 0.45 + 0.6 * max(0.0, math.cos(ang - light))
        for ys in range(int(y_top * S), int(y_bot * S)):
            y = ys / S
            band = int((y - apex[1]) / 2.6)
            col = shade((150, 70, 44), k)
            if (y - apex[1]) % 2.6 < 0.5:
                col = shade(col, 0.6)
            elif ((ang / math.pi * 2 + 1) * 6 + (0.5 if band % 2 else 0)) % 1.0 < 0.08:
                col = shade(col, 0.7)
            d.point((xs, ys), fill=tuple(int(c) for c in col) + (255,))
    # the pole and the banner (nation colour, marked with pure magenta shades for the remap)
    d.rectangle([21.6 * S, 0 * S, 22.4 * S, 8 * S], fill=(70, 50, 30, 255))
    d.polygon([(22.4 * S, 0.5 * S), (31 * S, 2.5 * S), (22.4 * S, 5 * S)], fill=(255, 0, 255, 255))
    d.line([(22.4 * S, 4.6 * S), (31 * S, 2.6 * S)], fill=(160, 0, 160, 255), width=S)
    small = img.resize((W, H), Image.LANCZOS)

    def banner(rgb):
        r, g, b = rgb
        if r > 120 and b > 120 and g < 90:            # the banner: nation colour, by brightness
            return NATION_COLOURS[1] if r > 200 else NATION_COLOURS[4]
        return None
    return to_rows(small, banner)


# ---------------------------------------------------------------------------
# cobbles
# ---------------------------------------------------------------------------
def cobble_layer(w, h, mask_fn, seed):
    """Cobbles where mask_fn(x, y) is true, on a dark mortar."""
    rnd = random.Random(seed)
    img = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    mortar = (78, 68, 56)
    for ys in range(0, h * S, S):
        for xs in range(0, w * S, S):
            if mask_fn(xs / S, ys / S):
                d.rectangle([xs, ys, xs + S - 1, ys + S - 1], fill=mortar + (255,))
    y = 0.0
    row = 0
    while y < h:
        x = -2.0 + (1.6 if row % 2 else 0)
        while x < w:
            sw = 2.6 + rnd.random() * 1.2
            sh = 2.0 + rnd.random() * 0.5
            cx, cy = x + sw / 2, y + sh / 2
            if mask_fn(cx, cy) and rnd.random() > 0.04:
                base = rnd.choice([(150, 142, 128), (138, 130, 116), (160, 150, 132), (128, 122, 112), (146, 132, 112)])
                k = 0.9 + rnd.random() * 0.2
                col = tuple(int(c * k) for c in base)
                d.rounded_rectangle([x * S + 1, y * S + 1, (x + sw) * S - 2, (y + sh) * S - 2], radius=S // 1.5, fill=col + (255,))
                d.line([(x * S + 2, y * S + 2), ((x + sw) * S - 3, y * S + 2)], fill=tuple(min(255, int(c * 1.18)) for c in col) + (255,))
            x += sw
        y += 2.3
        row += 1
    return img


def draw_pave():
    W = H = 128
    rnd = random.Random(7)
    edge = {}

    def street(x, y):
        # two streets crossing, a little uneven, fading out at the ends
        cx, cy = 64 + math.sin(y / 17) * 2, 66 + math.sin(x / 19) * 2
        horiz = abs(y - cy) < 7 and 4 < x < 124
        vert = abs(x - cx) < 6 and 4 < y < 124
        if not (horiz or vert):
            return False
        end = min(x, 128 - x) if horiz and not vert else min(y, 128 - y) if vert and not horiz else 99
        key = (int(x) // 3, int(y) // 3)
        if key not in edge:
            edge[key] = rnd.random()
        return end > 14 or edge[key] < (end - 4) / 10
    img = cobble_layer(W, H, street, 11).resize((W, H), Image.LANCZOS)
    return to_rows(img)


def draw_plaza():
    W, H = 60, 50
    cx, cy = 30, 25

    def square(x, y):
        return ((x - cx) / 29) ** 2 + ((y - cy) / 24) ** 2 < 1
    img = cobble_layer(W, H, square, 23)
    d = ImageDraw.Draw(img)
    # the well: a stone ring, dark water, two posts, a beam and a bucket
    d.ellipse([(cx - 8) * S, (cy - 4) * S, (cx + 8) * S, (cy + 7) * S], fill=(108, 100, 88, 255))
    d.ellipse([(cx - 8) * S, (cy - 6) * S, (cx + 8) * S, (cy + 4) * S], fill=(176, 168, 150, 255))
    d.ellipse([(cx - 5.5) * S, (cy - 4.2) * S, (cx + 5.5) * S, (cy + 2.4) * S], fill=(30, 46, 60, 255))
    d.ellipse([(cx - 4) * S, (cy - 3) * S, (cx + 1) * S, (cy + 0) * S], fill=(56, 84, 104, 255))
    for px in (cx - 7, cx + 6):
        d.rectangle([px * S, (cy - 17) * S, (px + 1.2) * S, (cy + 1) * S], fill=(96, 64, 36, 255))
    d.rectangle([(cx - 7.5) * S, (cy - 17.5) * S, (cx + 7.5) * S, (cy - 15.5) * S], fill=(120, 82, 46, 255))
    d.line([(cx - 0.5) * S, (cy - 15.5) * S, (cx - 0.5) * S, (cy - 9) * S], fill=(60, 50, 40, 255), width=S // 2)
    d.rectangle([(cx - 2) * S, (cy - 9) * S, (cx + 1) * S, (cy - 6) * S], fill=(110, 76, 44, 255))
    img = img.resize((W, H), Image.LANCZOS)
    return to_rows(img)


NEW = [("WTOWER", draw_tower), ("TWNPAVE", draw_pave), ("TWNPLAZA", draw_plaza)]

entries = read_res(res_path)
names = {n for n, _ in entries}
missing = [(n, f) for n, f in NEW if n not in names]
if not missing:
    sys.exit("already done: all the pictures are in " + res_path)

made = {}
for name, fn in missing:
    rows = fn()
    made[name] = rows
    entries.append((name, encode(rows)))
write_res(res_path, entries)
print("added", ", ".join(n for n, _ in missing))

if preview_path:
    tiles = []
    for name, rows in made.items():
        im = Image.new("RGB", (len(rows[0]), len(rows)), (70, 110, 60))
        im.putdata([(70, 110, 60) if c == TRANSPARENT else (200, 40, 40) if c >= 0xE0 else pal[c] for r in rows for c in r])
        tiles.append(im.resize((im.width * 3, im.height * 3), Image.NEAREST))
    sheet = Image.new("RGB", (sum(t.width for t in tiles) + 20 * len(tiles), max(t.height for t in tiles)), (20, 20, 20))
    x = 0
    for t in tiles:
        sheet.paste(t, (x, 0))
        x += t.width + 20
    sheet.save(preview_path)

"""Give the Supply Wagon its own sprite: an ox pulling a two-wheeled cart
under a canvas cover, with a flag in the nation's colour.

The cart is drawn from a small 3D model: the parts are sampled as surface
points, lit from the north-west, seen from the game's view angle and
reduced to the game palette, with a shadow (palette 0, SHADOW_CODE) and a
selection outline (0xF2/0xF3, OUTLINE_CODE/OUTLINE_SHADOW_CODE). The
flag uses the nation colour codes 0xE0-0xE3, remapped by the game.

Frames, written to OXCART.SPR:
  moving    5 directions (south, south-east, east, north-east, north) x 8
            frames: the ox walks and the wheels turn. The game mirrors the
            other 3 directions (Sprite::need_mirror()).
  dying     6 frames: the cart tips over and the ox goes down.

Records appended to the tables in STD.SET: SFRAME, SACTION and SPRITE
(OXCART, cloned from the Caravan's). The Supply Wagon's UNIT record is
pointed at the new sprite and at new icons, appended to I_UNITLI.RES and
I_UNITSI.RES.

usage: make_ox_cart.py STD.SET SPRITE_DIR I_UNITLI.RES I_UNITSI.RES PAL_STD.RES [preview.png]

The script refuses to run twice on the same files.
"""
import math
import os
import struct
import sys

import numpy as np

std_path, sprite_dir, large_path, small_path, pal_path = sys.argv[1:6]
preview_path = sys.argv[6] if len(sys.argv) > 6 else None

SPRITE_NAME = "OXCART"
WAGON_NAME = "Supply Wagon"
CARAVAN_SPRITE = "CARAVAN"

TRANSPARENT = 255
SHADOW = 0x00                 # SHADOW_CODE
OUTLINE = 0xF2                # OUTLINE_CODE, shown when the unit is selected
OUTLINE_SHADOW = 0xF3         # OUTLINE_SHADOW_CODE, outline over the shadow
FLAG_SHADES = [0xE0, 0xE1, 0xE2, 0xE3]     # the nation's colour, lightest first

SS = 4                        # supersampling
ELEVATION = math.radians(45)  # the view angle of the game
LIGHT = np.array([-0.50, -0.40, 0.90])
LIGHT /= np.linalg.norm(LIGHT)
MOVE_FRAMES = 8
DIE_FRAMES = 6
WALK_STEP = 2.2               # how far the cart moves in a frame, for turning the wheels

# ---------- palette ----------
p = open(pal_path, "rb").read()[8:8 + 768]
if max(p) < 64:
    p = bytes(min(255, c * 4) for c in p)
pal = np.array([tuple(p[i * 3:i * 3 + 3]) for i in range(256)], dtype=np.int32)
_cache = {}


def nearest(rgb):
    key = tuple(int(max(0, min(255, c))) for c in rgb)
    if key not in _cache:
        d = (pal[1:0xE0] - np.array(key)) ** 2 @ np.array([30, 59, 11])
        _cache[key] = 1 + int(np.argmin(d))
    return _cache[key]


# ---------- model: surface samples with normals and materials ----------
class Model:
    def __init__(self):
        self.pos, self.nrm, self.col = [], [], []

    def add(self, pos, nrm, col):
        self.pos.append(pos)
        self.nrm.append(nrm / np.linalg.norm(nrm, axis=1, keepdims=True))
        self.col.append(col)

    def arrays(self):
        return np.concatenate(self.pos), np.concatenate(self.nrm), np.concatenate(self.col)


STEP = 1.0 / (SS * 1.6)       # sample spacing in pixels


def noise(n, amount, seed):
    rng = np.random.default_rng(seed)
    return 1.0 + (rng.random(n) - 0.5) * 2 * amount


def colour_array(rgb, n, grain, seed):
    """Colours with a little grain; a negative red marks the flag, its lightness kept in green."""
    c = np.tile(np.array(rgb, dtype=float), (n, 1))
    return c * noise(n, grain, seed)[:, None] if rgb[0] >= 0 else c


def ellipsoid(m, centre, radii, rgb, grain=0.06, seed=0, rot=None):
    rmax = max(radii)
    nt = max(8, int(math.pi * rmax / STEP))
    npf = max(8, int(2 * math.pi * rmax / STEP))
    t, f = np.meshgrid(np.linspace(0, math.pi, nt), np.linspace(0, 2 * math.pi, npf, endpoint=False))
    t, f = t.ravel(), f.ravel()
    u = np.stack([np.sin(t) * np.cos(f), np.sin(t) * np.sin(f), np.cos(t)], axis=1)
    pos = u * radii
    nrm = u / radii
    if rot is not None:
        pos, nrm = pos @ rot.T, nrm @ rot.T
    m.add(pos + centre, nrm, colour_array(rgb, len(pos), grain, seed))


def frame_of(axis):
    axis = axis / np.linalg.norm(axis)
    other = np.array([0, 0, 1.0]) if abs(axis[2]) < 0.9 else np.array([1.0, 0, 0])
    a = np.cross(axis, other)
    a /= np.linalg.norm(a)
    return axis, a, np.cross(axis, a)


def cylinder(m, p1, p2, radius, rgb, grain=0.05, seed=0, caps=True):
    p1, p2 = np.array(p1, float), np.array(p2, float)
    axis, a, b = frame_of(p2 - p1)
    length = np.linalg.norm(p2 - p1)
    nl = max(2, int(length / STEP) + 1)
    na = max(6, int(2 * math.pi * radius / STEP))
    s, f = np.meshgrid(np.linspace(0, length, nl), np.linspace(0, 2 * math.pi, na, endpoint=False))
    s, f = s.ravel(), f.ravel()
    radial = np.cos(f)[:, None] * a + np.sin(f)[:, None] * b
    pos = p1 + s[:, None] * axis + radius * radial
    m.add(pos, radial, colour_array(rgb, len(pos), grain, seed))
    if caps:
        nr = max(2, int(radius / STEP))
        r, f = np.meshgrid(np.linspace(0, radius, nr), np.linspace(0, 2 * math.pi, na, endpoint=False))
        r, f = r.ravel(), f.ravel()
        disc = (np.cos(f) * r)[:, None] * a + (np.sin(f) * r)[:, None] * b
        for end, n in ((p1, -axis), (p2, axis)):
            m.add(end + disc, np.tile(n, (len(disc), 1)), colour_array(rgb, len(disc), grain, seed + 1))


def box(m, centre, size, rgb, grain=0.07, seed=0, rot=None):
    centre, size = np.array(centre, float), np.array(size, float)
    for axis in range(3):
        u_ax, v_ax = [i for i in range(3) if i != axis]
        nu = max(2, int(size[u_ax] / STEP) + 1)
        nv = max(2, int(size[v_ax] / STEP) + 1)
        uu, vv = np.meshgrid(np.linspace(-size[u_ax] / 2, size[u_ax] / 2, nu),
                             np.linspace(-size[v_ax] / 2, size[v_ax] / 2, nv))
        for sign in (-1, 1):
            pos = np.zeros((uu.size, 3))
            pos[:, u_ax], pos[:, v_ax] = uu.ravel(), vv.ravel()
            pos[:, axis] = sign * size[axis] / 2
            nrm = np.zeros_like(pos)
            nrm[:, axis] = sign
            if rot is not None:
                pos, nrm = pos @ rot.T, nrm @ rot.T
            m.add(pos + centre, nrm, colour_array(rgb, len(pos), grain, seed + axis * 2 + (sign > 0)))


def wheel(m, centre, radius, thickness, turn, rgb_rim, rgb_spoke, seed=0, rot=None):
    """A spoked wheel turning about the y axis; the gaps between the spokes are left empty."""
    centre = np.array(centre, float)
    nr = max(4, int(radius / STEP))
    na = max(12, int(2 * math.pi * radius / STEP))
    r, f = np.meshgrid(np.linspace(0, radius, nr), np.linspace(0, 2 * math.pi, na, endpoint=False))
    r, f = r.ravel(), f.ravel()
    spoke_dist = np.abs(((f - turn) % (math.pi / 3)) - math.pi / 6)      # 6 spokes
    rim = r > radius - 1.3
    hub = r < 1.5
    spoke = (np.abs(np.sin(math.pi / 6 - spoke_dist)) * r < 0.55)
    keep = rim | hub | spoke
    r, f, rim = r[keep], f[keep], rim[keep]
    col_rim = colour_array(rgb_rim, len(r), 0.06, seed)
    col_spoke = colour_array(rgb_spoke, len(r), 0.06, seed + 1)
    col = np.where(rim[:, None], col_rim, col_spoke)
    for side in (-1, 1):
        pos = np.stack([np.cos(f) * r, np.full_like(r, side * thickness / 2), np.sin(f) * r], axis=1)
        nrm = np.tile([0.0, side, 0.0], (len(pos), 1))
        if rot is not None:
            pos, nrm = pos @ rot.T, nrm @ rot.T
        m.add(pos + centre, nrm, col)
    nt = max(2, int(thickness / STEP) + 1)
    f2, y2 = np.meshgrid(np.linspace(0, 2 * math.pi, na, endpoint=False), np.linspace(-thickness / 2, thickness / 2, nt))
    f2, y2 = f2.ravel(), y2.ravel()
    radial = np.stack([np.cos(f2), np.zeros_like(f2), np.sin(f2)], axis=1)
    pos = radial * radius + np.stack([np.zeros_like(y2), y2, np.zeros_like(y2)], axis=1)
    nrm = radial
    if rot is not None:
        pos, nrm = pos @ rot.T, nrm @ rot.T
    m.add(pos + centre, nrm, colour_array((70, 52, 36), len(pos), 0.05, seed + 2))   # the iron tyre


def canvas_cover(m, x1, x2, centre_y, centre_z, radius, rgb, seed=0, rot=None, pivot=None):
    """A half cylinder of canvas along x over the cart, darker over its hoops, closed at the ends."""
    nl = max(2, int((x2 - x1) / STEP) + 1)
    na = max(8, int(math.pi * radius / STEP))
    x, f = np.meshgrid(np.linspace(x1, x2, nl), np.linspace(0, math.pi, na))
    x, f = x.ravel(), f.ravel()
    radial = np.stack([np.zeros_like(f), np.cos(f), np.sin(f)], axis=1)
    pos = np.stack([x, centre_y + radius * np.cos(f), centre_z + radius * np.sin(f) * 0.85], axis=1)
    col = colour_array(rgb, len(pos), 0.05, seed)
    hoop = (np.abs(((x - x1) % 5.0) - 2.5) > 2.0)
    col[hoop] *= 0.8
    folds = 1.0 + 0.06 * np.sin(f * 9 + x * 0.7)
    col *= folds[:, None]
    parts = [(pos, radial, col)]
    nr = max(2, int(radius / STEP))
    rr, ff = np.meshgrid(np.linspace(0, radius, nr), np.linspace(0, math.pi, na))
    rr, ff = rr.ravel(), ff.ravel()
    for xe, sign in ((x1, -1), (x2, 1)):
        pe = np.stack([np.full_like(rr, xe), centre_y + rr * np.cos(ff), centre_z + rr * np.sin(ff) * 0.85], axis=1)
        parts.append((pe, np.tile([sign, 0.0, 0.0], (len(pe), 1)), colour_array(rgb, len(pe), 0.05, seed + 3) * 0.9))
    for pos, nrm, col in parts:
        if rot is not None:
            pos, nrm = (pos - pivot) @ rot.T + pivot, nrm @ rot.T
        m.add(pos, nrm, col)


def flag(m, pole_top, length, height, frame, seed=0):
    """A flag trailing back from the pole, waving; marked as the nation's colour."""
    nl = max(2, int(length / STEP) + 1)
    nh = max(2, int(height / STEP) + 1)
    s, h = np.meshgrid(np.linspace(0, length, nl), np.linspace(0, height, nh))
    s, h = s.ravel(), h.ravel()
    wave = 0.9 * np.sin(s * 0.9 - frame * math.pi / 4) * (s / length)
    pos = np.stack([pole_top[0] - s, pole_top[1] + wave, pole_top[2] - h], axis=1)
    slope = 0.9 * 0.9 * np.cos(s * 0.9 - frame * math.pi / 4) * (s / length)
    for side in (-1, 1):
        nrm = np.stack([side * slope, np.full_like(s, side), np.zeros_like(s)], axis=1)
        m.add(pos, nrm, np.tile([-1.0, 0, 0], (len(pos), 1)))


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


OX_BROWN = (132, 88, 54)
OX_DARK = (84, 56, 36)
HORN = (226, 214, 182)
WOOD = (128, 88, 50)
WOOD_DARK = (92, 62, 36)
CANVAS = (214, 200, 156)
SHIFT = -4.5                   # the model is centred between the ox's nose and the back of the cart


def build(frame, dying=0.0):
    """The ox cart at a frame of its walk, or tipping over when dying is above 0."""
    m = Model()
    phase = 2 * math.pi * frame / MOVE_FRAMES
    walking = dying == 0.0
    bob = 0.4 * abs(math.sin(phase)) if walking else 0.0

    #------ the ox ------#
    sink = 6.0 * dying
    body_z = 12.0 + bob - sink
    ellipsoid(m, (14, 0, body_z), (10, 5.4, 5.8), OX_BROWN, 0.08, 1)
    ellipsoid(m, (19.5, 0, body_z + 2.8), (5, 5, 4.2), OX_BROWN, 0.08, 2)      # the shoulders
    head_drop = 4.0 * dying
    ellipsoid(m, (27, 0, body_z - 0.5 - head_drop), (4.4, 3.1, 3.4), OX_BROWN, 0.08, 3)
    ellipsoid(m, (30.4, 0, body_z - 1.8 - head_drop), (2.0, 2.4, 2.1), OX_DARK, 0.06, 4)
    for side in (-1, 1):
        cylinder(m, (26.4, side * 2.3, body_z + 2.2 - head_drop), (27.4, side * 5.6, body_z + 4.6 - head_drop), 0.75, HORN, 0.04, 5)
        ellipsoid(m, (24.5, side * 3.3, body_z + 1.4 - head_drop), (1.0, 1.2, 0.6), OX_DARK, 0.05, 6)    # the ears
    cylinder(m, (4.2, 0, body_z + 3), (3.4, 0.4 * math.sin(phase), body_z - 5 + sink * 0.5), 0.55, OX_DARK, 0.04, 7)
    for (hx, side, ph) in ((19, 1, 0.0), (19, -1, math.pi), (8.5, 1, math.pi), (8.5, -1, 0.0)):
        hip = np.array([hx, side * 3.0, body_z - 3.0])
        if walking:
            swing = 3.0 * math.sin(phase + ph)
            lift = max(0.0, 1.4 * math.cos(phase + ph))
            foot = np.array([hx + swing, side * 3.2, lift])
        else:
            foot = np.array([hx + side * 2.0 * dying, side * (3.2 + 2.5 * dying), 0.0])
        knee_len = 0.62
        knee = hip + (foot - hip) * knee_len
        cylinder(m, hip, knee, 1.75, OX_BROWN, 0.07, 8)
        cylinder(m, knee, foot + np.array([0, 0, 1.2]), 1.35, OX_BROWN, 0.07, 9)
        cylinder(m, foot + np.array([0, 0, 1.2]), foot, 1.4, (52, 40, 30), 0.04, 10)      # the hoof

    #------ the yoke and the pole ------#
    box(m, (22.5, 0, body_z + 5.6 - sink * 0.2), (2.0, 12.0, 1.6), WOOD_DARK, 0.06, 11)
    cylinder(m, (-2.5, 0, 9.0), (22.0, 0, body_z + 4.6 - sink * 0.3), 0.85, WOOD_DARK, 0.05, 12)

    #------ the cart ------#
    roll = -math.radians(62) * dying           # tips over to its right
    pivot = np.array([-12.0, -9.5, 0.0])
    rot = rot_x(roll) if dying else None

    def cart_box(centre, size, rgb, seed):
        c = np.array(centre, float)
        if rot is not None:
            c = (c - pivot) @ rot.T + pivot
        box(m, c, size, rgb, 0.08, seed, rot)

    cart_box((-12, 0, 9.2), (21, 15, 1.8), WOOD, 13)
    for side in (-1, 1):
        cart_box((-12, side * 7.2, 11.6), (21, 0.9, 3.0), WOOD, 14 + (side > 0))
    for x in (-1.9, -22.1):
        cart_box((x, 0, 11.6), (0.9, 15, 3.0), WOOD_DARK, 16)
    canvas_cover(m, -21.6, -3.2, 0.0, 12.0, 7.0, CANVAS, 17, rot, pivot)

    turn = frame * WALK_STEP / 7.0 if walking else 0.0
    for side in (-1, 1):
        c = np.array([-12.0, side * 8.6, 7.0])
        if rot is not None:
            c = (c - pivot) @ rot.T + pivot
        wheel(m, c, 7.0, 1.2, turn + side * 0.3, WOOD_DARK, WOOD, 20 + (side > 0), rot)

    #------ the nation's flag ------#
    top = np.array([-3.0, 6.4, 27.0])
    base = np.array([-3.0, 6.4, 11.0])
    if rot is not None:
        top, base = (top - pivot) @ rot.T + pivot, (base - pivot) @ rot.T + pivot
    cylinder(m, base, top, 0.5, WOOD_DARK, 0.04, 30)
    if dying < 0.5:
        flag(m, top - np.array([0.5, 0, 0.3]), 8.0, 4.6, frame, 31)

    pos, nrm, col = m.arrays()
    pos[:, 0] += SHIFT
    return pos, nrm, col


# ---------- rendering ----------
DIR_HEADINGS = [(0, 1), (1, 1), (1, 0), (1, -1), (0, -1)]     # south, south-east, east, north-east, north


def render(pos, nrm, col, heading, scale=1.0, flag_rgb=None):
    """Returns (rows of palette codes, origin x, origin y) for the model heading the given way."""
    hx, hy = heading
    n = math.hypot(hx, hy)
    hx, hy = hx / n, hy / n
    lx, ly = hy, -hx                                            # the ox's left side
    R = np.array([[hx, lx, 0], [hy, ly, 0], [0, 0, 1]])         # model -> ground (x east, y south, z up)
    P = pos @ R.T * scale
    N = nrm @ R.T

    se, ce = math.sin(ELEVATION), math.cos(ELEVATION)
    view = np.array([0, ce, se])
    sx = P[:, 0]
    sy = P[:, 1] * se - P[:, 2] * ce
    depth = P @ view

    # the shadow on the ground, cast away from the light
    gx = P[:, 0] - LIGHT[0] * P[:, 2] / LIGHT[2]
    gy = P[:, 1] - LIGHT[1] * P[:, 2] / LIGHT[2]
    shx, shy = gx, gy * se

    x0 = math.floor(min(sx.min(), shx.min())) - 2
    y0 = math.floor(min(sy.min(), shy.min())) - 2
    w = math.ceil(max(sx.max(), shx.max())) - x0 + 3
    h = math.ceil(max(sy.max(), shy.max())) - y0 + 3

    # lit colours; facing away from the viewer is drawn as seen from behind
    facing = np.sign(N @ view)
    facing[facing == 0] = 1
    Nv = N * facing[:, None]
    lam = np.clip(Nv @ LIGHT, 0, 1)
    shade = 0.42 + 0.72 * lam
    is_flag = col[:, 0] < 0
    rgb = np.clip(col * shade[:, None], 0, 255)
    if flag_rgb is not None:
        rgb[is_flag] = np.clip(np.array(flag_rgb, float) * shade[is_flag, None], 0, 255)

    W, H = w * SS, h * SS
    px = ((sx - x0) * SS).astype(int)
    py = ((sy - y0) * SS).astype(int)
    order = np.argsort(depth)
    idx = py[order] * W + px[order]
    zbuf = np.full(W * H, -1, dtype=np.int64)
    zbuf[idx] = order                                           # the nearest point wins
    hit = zbuf >= 0
    pt = zbuf[hit]

    cov = np.zeros(W * H)
    cov[hit] = 1
    acc = np.zeros((W * H, 3))
    acc[hit] = rgb[pt]
    flagacc = np.zeros(W * H)
    flagacc[hit] = is_flag[pt] & (flag_rgb is None)
    shadeacc = np.zeros(W * H)
    shadeacc[hit] = shade[pt]

    def down(a):
        return a.reshape(h, SS, w, SS, *a.shape[1:]).sum(axis=(1, 3))

    cov_d = down(cov)
    acc_d = down(acc)
    flag_d = down(flagacc)
    shade_d = down(shadeacc)

    sh = np.zeros(W * H)
    sidx = ((shy - y0) * SS).astype(int) * W + ((shx - x0) * SS).astype(int)
    sh[sidx] = 1
    sh_d = down(sh) >= SS * SS * 0.45

    solid = cov_d >= SS * SS * 0.4
    rows = np.full((h, w), TRANSPARENT, dtype=np.int32)
    for y in range(h):
        for x in range(w):
            if solid[y, x]:
                if flag_d[y, x] * 2 > cov_d[y, x]:
                    light = shade_d[y, x] / cov_d[y, x]
                    rows[y, x] = FLAG_SHADES[min(3, max(0, int((1.12 - light) * 4.2)))]
                else:
                    rows[y, x] = nearest(acc_d[y, x] / cov_d[y, x])
            elif sh_d[y, x]:
                rows[y, x] = SHADOW

    # the selection outline around the cart, on the ground or on its shadow
    out = rows.copy()
    for y in range(h):
        for x in range(w):
            if rows[y, x] in (TRANSPARENT, SHADOW):
                near = any(0 <= y + dy < h and 0 <= x + dx < w and solid[y + dy, x + dx]
                           for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                if near:
                    out[y, x] = OUTLINE if rows[y, x] == TRANSPARENT else OUTLINE_SHADOW

    # crop to what is drawn
    ys, xs = np.nonzero(out != TRANSPARENT)
    cy1, cy2, cx1, cx2 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    out = out[cy1:cy2, cx1:cx2]
    return out.tolist(), -x0 - cx1, -y0 - cy1


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


# ---------- resource files and tables ----------
def read_res(path):
    d = open(path, "rb").read()
    n = struct.unpack_from("<h", d, 0)[0]
    idx = [(d[2 + i * 13: 2 + i * 13 + 9].split(b"\0")[0].decode("latin-1"),
            struct.unpack_from("<i", d, 2 + i * 13 + 9)[0]) for i in range(n + 1)]
    return [[idx[i][0], bytearray(d[idx[i][1]:idx[i + 1][1]])] for i in range(n)]


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


class Table:
    def __init__(self, blob):
        self.d = blob
        self.rec_count, self.hdr_size, self.rec_size = struct.unpack_from("<IHH", blob, 4)
        self.fields = {}
        off, fpos = 32, 1
        while blob[off] != 0x0D:
            name = blob[off:off + 11].split(b"\0")[0].decode()
            flen = blob[off + 16]
            self.fields[name] = (fpos, flen, chr(blob[off + 11]))
            fpos += flen
            off += 32
        assert fpos == self.rec_size
        self.new = []
        self.changed = {}

    def record(self, i):        # 0-based
        if i in self.changed:
            return bytearray(self.changed[i])
        return bytearray(self.d[self.hdr_size + i * self.rec_size: self.hdr_size + (i + 1) * self.rec_size])

    def get(self, rec, name):
        fpos, flen, _ = self.fields[name]
        return bytes(rec[fpos:fpos + flen]).decode("latin-1").strip()

    def blank(self):
        return bytearray(b" " * self.rec_size)

    def put(self, rec, name, value):
        fpos, flen, ftype = self.fields[name]
        if isinstance(value, bytes) and len(value) == flen and name.endswith("PTR"):
            raw = value
        else:
            raw = (value if isinstance(value, bytes) else str(value).encode("ascii"))
            raw = raw.rjust(flen) if ftype == "N" else raw.ljust(flen)
        assert len(raw) == flen, (name, value)
        rec[fpos:fpos + flen] = raw

    def append(self, rec):
        self.new.append(bytes(rec))
        return self.rec_count + len(self.new)      # 1-based record no.

    def replace(self, i, rec):
        self.changed[i] = bytes(rec)

    def blob(self):
        end = self.hdr_size + self.rec_count * self.rec_size
        has_eof = len(self.d) > end and self.d[end] == 0x1A
        out = bytearray(self.d[:end])
        for i, rec in self.changed.items():
            out[self.hdr_size + i * self.rec_size: self.hdr_size + (i + 1) * self.rec_size] = rec
        out += b"".join(self.new) + (b"\x1a" if has_eof else b"")
        struct.pack_into("<I", out, 4, self.rec_count + len(self.new))
        return out


entries = read_res(std_path)
tables = {name: Table(blob) for name, blob in entries if name in ("SFRAME", "SACTION", "SPRITE", "UNIT")}

sprite_table = tables["SPRITE"]
caravan_sprite = None
for i in range(sprite_table.rec_count):
    rec = sprite_table.record(i)
    assert sprite_table.get(rec, "NAME") != SPRITE_NAME, "the ox cart has been added already"
    if sprite_table.get(rec, "NAME") == CARAVAN_SPRITE:
        caravan_sprite = rec
assert caravan_sprite is not None

unit_table = tables["UNIT"]
wagon_index = None
for i in range(unit_table.rec_count):
    if unit_table.get(unit_table.record(i), "NAME") == WAGON_NAME:
        wagon_index = i
assert wagon_index is not None, "no Supply Wagon in the unit table"

# ---------- the frames ----------
spr = bytearray()
frames = {}            # (group, frame no.) -> (offset in the .SPR, w, h, origin x, origin y)
sheet = []


def add_frame(key, rows, ox, oy):
    data = encode(rows)
    frames[key] = (len(spr), len(rows[0]), len(rows), ox, oy)
    spr.extend(struct.pack("<I", len(data)) + data)
    sheet.append(rows)


for group, heading in enumerate(DIR_HEADINGS):
    for k in range(MOVE_FRAMES):
        rows, ox, oy = render(*build(k), heading)
        add_frame((group, k), rows, ox, oy)
    print("direction", group, "done")

for k in range(DIE_FRAMES):
    rows, ox, oy = render(*build(0, dying=min(1.0, (k + 1) / (DIE_FRAMES - 1))), DIR_HEADINGS[2])
    add_frame(("D", k), rows, ox, oy)

open(os.path.join(sprite_dir, SPRITE_NAME + ".SPR"), "wb").write(spr)

# ---------- SFRAME: 8 directions; the west side ones are drawn mirrored, their origin mirrored too ----------
DIR_NAMES = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
DIR_GROUP = {0: 4, 1: 3, 2: 2, 3: 1, 4: 0, 5: 1, 6: 2, 7: 3}
ORIGIN_X, ORIGIN_Y = 16, 18             # where the middle of the cart stands in its location

sframe = tables["SFRAME"]


def add_sframe(action, dir_name, k, key, mirrored, filename):
    f_off, w, h, ox, oy = frames[key]
    if mirrored:
        ox = w - 1 - ox
    rec = sframe.blank()
    sframe.put(rec, "SPRITE", SPRITE_NAME)
    sframe.put(rec, "ACTION", action)
    sframe.put(rec, "DIR", dir_name)
    sframe.put(rec, "FRAME", k + 1)
    sframe.put(rec, "OFFSET_X", ORIGIN_X - ox)
    sframe.put(rec, "OFFSET_Y", ORIGIN_Y - oy)
    sframe.put(rec, "WIDTH", w)
    sframe.put(rec, "HEIGHT", h)
    sframe.put(rec, "FILENAME", filename)
    sframe.put(rec, "BITMAPPTR", struct.pack("<I", f_off))
    return sframe.append(rec)


first_frame_of_dir = {}
for dir_id in range(8):
    group = DIR_GROUP[dir_id]
    for k in range(MOVE_FRAMES):
        recno = add_sframe("M", DIR_NAMES[dir_id], k, (group, k), dir_id >= 5, "OX-M%d-%d" % (group + 1, k + 1))
        if k == 0:
            first_frame_of_dir[dir_id] = recno
first_die_frame = None
for k in range(DIE_FRAMES):
    recno = add_sframe("D", "E", k, ("D", k), False, "OX-D-%d" % (k + 1))
    first_die_frame = first_die_frame or recno

# ---------- SACTION: moving, standing (the first frame of the walk) and dying ----------
saction = tables["SACTION"]
first_action = None
for action, count in (("M", MOVE_FRAMES), ("S", 1)):
    for dir_id in range(8):
        rec = saction.blank()
        saction.put(rec, "SPRITE", SPRITE_NAME)
        saction.put(rec, "ACTION", action)
        saction.put(rec, "DIRID", dir_id)
        saction.put(rec, "FIRSTFRAME", first_frame_of_dir[dir_id])
        saction.put(rec, "FRAMECOUNT", count)
        recno = saction.append(rec)
        first_action = first_action or recno
rec = saction.blank()
saction.put(rec, "SPRITE", SPRITE_NAME)
saction.put(rec, "ACTION", "D")
saction.put(rec, "DIRID", 0)
saction.put(rec, "FIRSTFRAME", first_die_frame)
saction.put(rec, "FRAMECOUNT", DIE_FRAMES)
saction.append(rec)
action_count = 8 * 2 + 1

# ---------- SPRITE: cloned from the Caravan's ----------
rec = bytearray(caravan_sprite)
sprite_table.put(rec, "NAME", SPRITE_NAME)
sprite_table.put(rec, "FIRSTACT", first_action)
sprite_table.put(rec, "ACTCOUNT", action_count)
sprite_recno = sprite_table.append(rec)

# ---------- icons, drawn from the model ----------
def make_icon(res_path, template_offset, margin):
    d = bytearray(open(res_path, "rb").read())
    iw, ih = struct.unpack_from("<hh", d, template_offset + 4)
    background = d[template_offset + 8]          # the colour of the template's top left corner
    pos, nrm, col = build(2)
    best = None
    for scale in [s / 20 for s in range(40, 4, -1)]:
        rows, _, _ = render(pos, nrm, col, (1, 1), scale, flag_rgb=(196, 36, 28))
        if len(rows[0]) <= iw - margin * 2 and len(rows) <= ih - margin * 2:
            best = rows
            break
    rows = best
    rh, rw = len(rows), len(rows[0])
    ox, oy = (iw - rw) // 2, (ih - rh) // 2
    pix = bytearray([background] * (iw * ih))
    for y in range(rh):
        for x in range(rw):
            c = rows[y][x]
            if c not in (TRANSPARENT, OUTLINE, OUTLINE_SHADOW, SHADOW):
                pix[(oy + y) * iw + ox + x] = c
    new_offset = len(d)
    d += struct.pack("<i", 4 + iw * ih) + struct.pack("<hh", iw, ih) + pix
    open(res_path, "wb").write(d)
    return new_offset


def ptr(rec, name):
    fpos, _, _ = unit_table.fields[name]
    return struct.unpack_from("<I", rec, fpos)[0]


wagon = unit_table.record(wagon_index)
large_offset = make_icon(large_path, ptr(wagon, "LICONPTR"), 2)
small_offset = make_icon(small_path, ptr(wagon, "SICONPTR"), 1)

# ---------- the Supply Wagon uses the new sprite and icons ----------
unit_table.put(wagon, "SPRITE", SPRITE_NAME)
unit_table.put(wagon, "SPRITE_ID", sprite_recno)
unit_table.put(wagon, "LICONNAME", "L_OXCART")
unit_table.put(wagon, "LICONPTR", struct.pack("<I", large_offset))
unit_table.put(wagon, "SICONNAME", "S_OXCART")
unit_table.put(wagon, "SICONPTR", struct.pack("<I", small_offset))
unit_table.replace(wagon_index, wagon)

for entry in entries:
    if entry[0] in tables:
        entry[1] = tables[entry[0]].blob()
write_res(std_path, entries)

print("sprite", sprite_recno, "actions from", first_action, "frames", len(frames), "unit", wagon_index + 1,
      "icons at", large_offset, small_offset)

# ---------- a preview sheet: the shadow grey, the outline blue, the flag red ----------
if preview_path:
    from PIL import Image
    show = {SHADOW: (60, 60, 60), OUTLINE: (90, 120, 255), OUTLINE_SHADOW: (60, 80, 200),
            0xE0: (230, 60, 50), 0xE1: (200, 40, 32), 0xE2: (160, 28, 22), 0xE3: (120, 20, 16)}
    cw = max(len(r[0]) for r in sheet) + 4
    chh = max(len(r) for r in sheet) + 4
    cols = MOVE_FRAMES
    img = Image.new("RGB", (cw * cols, chh * math.ceil(len(sheet) / cols)), (52, 92, 40))
    px = img.load()
    for n, rows in enumerate(sheet):
        bx, by = (n % cols) * cw + 2, (n // cols) * chh + 2
        for y, row in enumerate(rows):
            for x, c in enumerate(row):
                if c == TRANSPARENT:
                    continue
                px[bx + x, by + y] = show.get(c, tuple(int(v) for v in pal[c]))
    img = img.resize((img.width * 3, img.height * 3), Image.NEAREST)
    img.save(preview_path)

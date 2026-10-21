"""Add the Baatezu, a winged Fryhtan made from the unused BAATEZU.SPR.

BAATEZU.SPR has 40 frames: a wing beat of 8 frames for each of 5
directions (south, south-east, east, north-east, north). The game draws
the other 3 directions mirrored (see Sprite::need_mirror()), so the beat
serves for moving, standing, attacking and dying.

Records appended to the tables in STD.SET:
  SFRAME    8 directions x 8 frames
  SACTION   move, stop and attack for 8 directions, and dying
  SPRITE    BAATEZU
  UNITATTK  a claw attack
  UNIT      Baatezu, a monster cloned from the Pfith (GITH)
  MONSTER   Baatezu, level 8, living in lairs like the Pfith's

and a large and a small icon, drawn from a frame, appended to I_UNITLI.RES
and I_UNITSI.RES.

usage: make_baatezu.py STD.SET BAATEZU.SPR I_UNITLI.RES I_UNITSI.RES

The script refuses to run twice on the same files.
"""
import struct
import sys

std_path, spr_path, large_path, small_path = sys.argv[1:5]

SPRITE_NAME = b"BAATEZU"
TRANSPARENT = 255


# ---------- resource files: short count, (count+1) x (9-byte name, int32 offset), data ----------
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


# ---------- dBASE tables ----------
class Table:
    def __init__(self, blob):
        self.d = blob
        self.rec_count, self.hdr_size, self.rec_size = struct.unpack_from("<IHH", blob, 4)
        self.fields = {}
        off, pos = 32, 1
        while blob[off] != 0x0D:
            name = blob[off:off + 11].split(b"\0")[0].decode()
            flen = blob[off + 16]
            self.fields[name] = (pos, flen, chr(blob[off + 11]))
            pos += flen
            off += 32
        assert pos == self.rec_size
        self.new = []

    def record(self, i):        # 0-based
        return bytearray(self.d[self.hdr_size + i * self.rec_size: self.hdr_size + (i + 1) * self.rec_size])

    def get(self, rec, name):
        pos, flen, _ = self.fields[name]
        return bytes(rec[pos:pos + flen]).decode("latin-1").strip()

    def blank(self):
        return bytearray(b" " * self.rec_size)

    def put(self, rec, name, value):
        pos, flen, ftype = self.fields[name]
        if isinstance(value, bytes) and len(value) == flen and ftype == "C" and name.endswith("PTR"):
            raw = value
        else:
            raw = (value if isinstance(value, bytes) else str(value).encode("ascii"))
            raw = raw.rjust(flen) if ftype == "N" else raw.ljust(flen)
        assert len(raw) == flen, (name, value)
        rec[pos:pos + flen] = raw

    def append(self, rec):
        self.new.append(bytes(rec))
        return self.rec_count + len(self.new)      # 1-based record no.

    def blob(self):
        end = self.hdr_size + self.rec_count * self.rec_size
        has_eof = len(self.d) > end and self.d[end] == 0x1A
        out = bytearray(self.d[:end]) + b"".join(self.new) + (b"\x1a" if has_eof else b"")
        struct.pack_into("<I", out, 4, self.rec_count + len(self.new))
        return out


entries = read_res(std_path)
tables = {name: Table(blob) for name, blob in entries if name in ("SFRAME", "SACTION", "SPRITE", "UNITATTK", "UNIT", "MONSTER")}

sprite_table = tables["SPRITE"]
for i in range(sprite_table.rec_count):
    assert sprite_table.get(sprite_table.record(i), "NAME") != "BAATEZU", "the Baatezu has been added already"

# ---------- the frames of BAATEZU.SPR ----------
spr = open(spr_path, "rb").read()
frames = []
off = 0
while off + 8 <= len(spr):
    size = struct.unpack_from("<I", spr, off)[0]
    w, h = struct.unpack_from("<hh", spr, off + 4)
    frames.append((off, w, h))
    off += 4 + size
assert len(frames) == 40, len(frames)

# direction id (0 north, clockwise) -> group of 8 frames; west side directions are drawn mirrored
DIR_NAMES = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
DIR_GROUP = {0: 4, 1: 3, 2: 2, 3: 1, 4: 0, 5: 1, 6: 2, 7: 3}

sframe = tables["SFRAME"]
first_frame_of_dir = {}
for dir_id in range(8):
    group = DIR_GROUP[dir_id]
    for k in range(8):
        f_off, w, h = frames[group * 8 + k]
        rec = sframe.blank()
        sframe.put(rec, "SPRITE", SPRITE_NAME)
        sframe.put(rec, "ACTION", "M")
        sframe.put(rec, "DIR", DIR_NAMES[dir_id])
        sframe.put(rec, "FRAME", k + 1)
        sframe.put(rec, "OFFSET_X", 16 - w // 2)        # centred on the location
        sframe.put(rec, "OFFSET_Y", 24 - h)             # hovering, the feet a little above the ground
        sframe.put(rec, "WIDTH", w)
        sframe.put(rec, "HEIGHT", h)
        sframe.put(rec, "FILENAME", "BZ-M%d-%d" % (group + 1, k + 1))
        sframe.put(rec, "BITMAPPTR", struct.pack("<I", f_off))
        recno = sframe.append(rec)
        if k == 0:
            first_frame_of_dir[dir_id] = recno

# ---------- actions ----------
saction = tables["SACTION"]
first_action = None
for action in ("M", "S", "A1"):
    for dir_id in range(8):
        rec = saction.blank()
        saction.put(rec, "SPRITE", SPRITE_NAME)
        saction.put(rec, "ACTION", action)
        saction.put(rec, "DIRID", dir_id)
        saction.put(rec, "FIRSTFRAME", first_frame_of_dir[dir_id])
        saction.put(rec, "FRAMECOUNT", 8)
        recno = saction.append(rec)
        if first_action is None:
            first_action = recno
rec = saction.blank()
saction.put(rec, "SPRITE", SPRITE_NAME)
saction.put(rec, "ACTION", "D")
saction.put(rec, "DIRID", 0)
saction.put(rec, "FIRSTFRAME", first_frame_of_dir[4])     # dying: a last beat facing south
saction.put(rec, "FRAMECOUNT", 8)
saction.append(rec)
action_count = 8 * 3 + 1

# ---------- sprite ----------
rec = sprite_table.blank()
for name, value in (("NAME", SPRITE_NAME), ("TYPE", "U"), ("TURN_RES", 8), ("LOC_WIDTH", 1), ("LOC_HEIGHT", 1),
                    ("SPEED", 5), ("FRAMESTEP", 1), ("RAIN_SLOW", 0), ("SNOW_SLOW", 0), ("LIGHTN_DMG", 30),
                    ("FIRSTACT", first_action), ("ACTCOUNT", action_count)):
    sprite_table.put(rec, name, value)
sprite_recno = sprite_table.append(rec)

# ---------- attack: cloned from the Pfith's first attack ----------
unit_table = tables["UNIT"]
GITH_UNIT_RECNO = 29
gith = unit_table.record(GITH_UNIT_RECNO - 1)
assert unit_table.get(gith, "SPRITE") == "GITH"

attk = tables["UNITATTK"]
gith_attk = attk.record(int(unit_table.get(gith, "FIRSTATTK")) - 1)
rec = bytearray(gith_attk)
attk.put(rec, "SPRITE", SPRITE_NAME)
attk.put(rec, "ATTK_ID", 1)
attk.put(rec, "ATTK_DELAY", 3)
attk.put(rec, "BASIC_DMG", 9)
attk.put(rec, "PIERCE_DMG", 6)
attk.put(rec, "OUT_FRAME", 5)
attk.put(rec, "NEXT_ATKID", "")
attk_recno = attk.append(rec)

# ---------- icons, drawn from a frame with the wings spread ----------
def decode_frame(f_off):
    size = struct.unpack_from("<I", spr, f_off)[0]
    w, h = struct.unpack_from("<hh", spr, f_off + 4)
    data = spr[f_off + 8: f_off + 4 + size]
    pix, i = [], 0
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
    return w, h, pix


def make_icon(res_path, template_offset, frame_no, margin):
    d = bytearray(open(res_path, "rb").read())
    size = struct.unpack_from("<i", d, template_offset)[0]
    iw, ih = struct.unpack_from("<hh", d, template_offset + 4)
    background = d[template_offset + 8]          # the colour of the template's top left corner
    fw, fh, fpix = decode_frame(frames[frame_no][0])
    scale = min((iw - margin * 2) / fw, (ih - margin * 2) / fh)
    sw, sh = max(1, int(fw * scale)), max(1, int(fh * scale))
    ox, oy = (iw - sw) // 2, (ih - sh) // 2
    pix = bytearray([background] * (iw * ih))
    for y in range(sh):
        for x in range(sw):
            c = fpix[int(y / scale) * fw + int(x / scale)]
            if c != TRANSPARENT:
                pix[(oy + y) * iw + ox + x] = c
    new_offset = len(d)
    d += struct.pack("<i", 4 + iw * ih) + struct.pack("<hh", iw, ih) + pix
    open(res_path, "wb").write(d)
    return new_offset


def ptr(rec, name):
    pos, flen, _ = unit_table.fields[name]
    return struct.unpack_from("<I", rec, pos)[0]


large_offset = make_icon(large_path, ptr(gith, "LICONPTR"), 4, 1)
small_offset = make_icon(small_path, ptr(gith, "SICONPTR"), 4, 0)

# ---------- unit: cloned from the Pfith ----------
rec = bytearray(gith)
unit_table.put(rec, "NAME", "Baatezu")
unit_table.put(rec, "SPRITE", SPRITE_NAME)
unit_table.put(rec, "VIS_RANGE", 6)
unit_table.put(rec, "HIT_PT", 380)
unit_table.put(rec, "ARMOR", 6)
unit_table.put(rec, "LICONNAME", "M_BAATEZ")
unit_table.put(rec, "LICONPTR", struct.pack("<I", large_offset))
unit_table.put(rec, "SICONNAME", "S_BAATEZ")
unit_table.put(rec, "SICONPTR", struct.pack("<I", small_offset))
unit_table.put(rec, "SPRITE_ID", sprite_recno)
unit_table.put(rec, "FIRSTATTK", attk_recno)
unit_table.put(rec, "ATTKCOUNT", 1)
unit_recno = unit_table.append(rec)

# ---------- monster ----------
monster = tables["MONSTER"]
rec = monster.blank()
monster.put(rec, "UNIT", SPRITE_NAME)
monster.put(rec, "NAME", "Baatezu")
monster.put(rec, "LEVEL", "8")
monster.put(rec, "FIRMBLDCOD", "GITH")          # it lives in lairs like the Pfith's
monster.put(rec, "UNITID", unit_recno)
monster.append(rec)

for entry in entries:
    if entry[0] in tables:
        entry[1] = tables[entry[0]].blob()
write_res(std_path, entries)

print("sprite", sprite_recno, "actions from", first_action, "attack", attk_recno, "unit", unit_recno,
      "icons at", large_offset, small_offset)

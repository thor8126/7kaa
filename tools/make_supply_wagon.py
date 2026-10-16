"""Add the Supply Wagon unit: a UNIT.DBF record cloned from the Caravan's,
and grey copies of the Caravan's large and small icons appended to
I_UNITLI.RES and I_UNITSI.RES.

The wagon uses the Caravan's sprite, drawn in grey by the game (see
wagon_remap_table() in OUNITDRW.CPP).

usage: make_supply_wagon.py UNIT.DBF I_UNITLI.RES I_UNITSI.RES PAL_STD.RES

UNIT.DBF is the unit table extracted from STD.SET with tools/deresx;
rebuild STD.SET with tools/libresx afterwards. The script refuses to run
twice on the same files.
"""
import struct
import sys

unit_dbf, large_res, small_res, pal_res = sys.argv[1:5]

CARAVAN_RECNO = 8                 # 1-based record no. of the Caravan in UNIT.DBF
WAGON_NAME = "Supply Wagon"
RESERVED = set(range(0xE0, 0x100))   # nation colour remap keys, outline, shadow, transparency

# ---------- palette ----------
p = open(pal_res, "rb").read()[8:8 + 768]
if max(p) < 64:
    p = bytes(min(255, c * 4) for c in p)
pal = [tuple(p[i * 3:i * 3 + 3]) for i in range(256)]
greys = [i for i in range(256) if i not in RESERVED and pal[i][0] == pal[i][1] == pal[i][2]]


def grey_of(c):
    if c in RESERVED:
        return c
    r, g, b = pal[c]
    lum = min(255, (r * 30 + g * 59 + b * 11) // 100 * 115 // 100)
    return min(greys, key=lambda i: abs(pal[i][0] - lum))


# ---------- icons ----------
def append_grey_icon(res_path, offset):
    d = bytearray(open(res_path, "rb").read())
    size = struct.unpack_from("<i", d, offset)[0]
    w, h = struct.unpack_from("<hh", d, offset + 4)
    assert size == 4 + w * h, (res_path, size, w, h)
    icon = d[offset + 4: offset + 4 + size]
    pix = bytes(grey_of(c) for c in icon[4:])
    new_offset = len(d)
    d += struct.pack("<i", size) + icon[:4] + pix
    open(res_path, "wb").write(d)
    return new_offset


# ---------- unit table ----------
d = bytearray(open(unit_dbf, "rb").read())
rec_count, hdr_size, rec_size = struct.unpack_from("<IHH", d, 4)

fields = {}
off, pos = 32, 1
while d[off] != 0x0D:
    name = d[off:off + 11].split(b"\0")[0].decode()
    flen = d[off + 16]
    fields[name] = (pos, flen, chr(d[off + 11]))
    pos += flen
    off += 32
assert pos == rec_size

for i in range(rec_count):
    rec = d[hdr_size + i * rec_size: hdr_size + (i + 1) * rec_size]
    assert rec[1:1 + len(WAGON_NAME)] != WAGON_NAME.encode(), "the Supply Wagon has been added already"

rec = bytearray(d[hdr_size + (CARAVAN_RECNO - 1) * rec_size: hdr_size + CARAVAN_RECNO * rec_size])


def get_ptr(name):
    pos, flen, _ = fields[name]
    return struct.unpack_from("<I", rec, pos)[0]


def put(name, value):
    pos, flen, ftype = fields[name]
    if isinstance(value, bytes):
        raw = value
    else:
        raw = str(value).encode("ascii")
        raw = raw.rjust(flen) if ftype == "N" else raw.ljust(flen)
    assert len(raw) == flen, (name, value)
    rec[pos:pos + flen] = raw


large_offset = append_grey_icon(large_res, get_ptr("LICONPTR"))
small_offset = append_grey_icon(small_res, get_ptr("SICONPTR"))

put("NAME", WAGON_NAME)
put("CLASS", "WEAPON")
put("ALL_KNOW", "1")              # no research needed
put("VIS_RANGE", 5)
put("SHEALTH", 40)
put("HIT_PT", 120)
put("ARMOR", 3)
put("BUILDDAYS", 40)
put("YEARCOST", 30)
put("WEAPONPOW", 1)
put("LICONPTR", struct.pack("<I", large_offset))
put("SICONPTR", struct.pack("<I", small_offset))
put("FIRSTATTK", "")              # it can't attack
put("ATTKCOUNT", "")

records_end = hdr_size + rec_count * rec_size
has_eof = len(d) > records_end and d[records_end] == 0x1A
d = d[:records_end] + rec + (b"\x1a" if has_eof else b"")
struct.pack_into("<I", d, 4, rec_count + 1)
open(unit_dbf, "wb").write(d)

print("unit id", rec_count + 1, "large icon at", large_offset, "small icon at", small_offset)

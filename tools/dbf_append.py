"""Append records to a dBASE III table (as used in 7kaa's STD.SET).

usage: dbf_append.py table.dbf "field1|field2" ["field1|field2" ...]
"""
import struct
import sys

path = sys.argv[1]
data = bytearray(open(path, "rb").read())

rec_count, hdr_size, rec_size = struct.unpack_from("<IHH", data, 4)

# field descriptors: 32 bytes each, from offset 32, ended by 0x0D
fields = []
off = 32
while data[off] != 0x0D:
    name = data[off:off+11].split(b"\0")[0].decode()
    ftype = chr(data[off+11])
    flen = data[off+16]
    fields.append((name, ftype, flen))
    off += 32

assert 1 + sum(f[2] for f in fields) == rec_size, "record size mismatch"

records_end = hdr_size + rec_count * rec_size
has_eof = len(data) > records_end and data[records_end] == 0x1A

new_records = b""
for arg in sys.argv[2:]:
    values = arg.split("|")
    assert len(values) == len(fields), (values, fields)
    rec = b" "
    for (name, ftype, flen), value in zip(fields, values):
        raw = value.encode("ascii")
        assert len(raw) <= flen, (name, value)
        if ftype == "N":
            rec += raw.rjust(flen)
        else:
            rec += raw.ljust(flen)
    new_records += rec

out = data[:records_end] + new_records + (b"\x1a" if has_eof else b"") + data[records_end + (1 if has_eof else 0):]
struct.pack_into("<I", out, 4, rec_count + len(sys.argv) - 2)
open(path, "wb").write(out)
print("fields:", fields, "records:", rec_count, "->", rec_count + len(sys.argv) - 2)

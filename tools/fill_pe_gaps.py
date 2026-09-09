"""Fill virtual gaps in the native test harness's PE section table.

The .fixture section reserves the game's preferred address before Windows can
put a heap there. MinGW leaves a gap before it; the Windows PE loader requires
that gap to be described by a section. No executable game file is modified.
"""
import struct
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
pe = struct.unpack_from('<I', data, 0x3c)[0]
count = struct.unpack_from('<H', data, pe+6)[0]
opt = pe+24
table = opt+struct.unpack_from('<H', data, pe+20)[0]
alignment = struct.unpack_from('<I', data, opt+32)[0]
headers = struct.unpack_from('<I', data, opt+60)[0]
sections = [bytes(data[table+i*40:table+(i+1)*40]) for i in range(count)]
sections.sort(key=lambda s: struct.unpack_from('<I', s, 12)[0])
out = []
end = (headers+alignment-1)//alignment*alignment
for s in sections:
    virtual_size, va, raw_size = struct.unpack_from('<III', s, 8)
    assert va >= end
    if va > end:
        pad = bytearray(40)
        pad[:8] = b'.gap\0\0\0\0'
        struct.pack_into('<II', pad, 8, va-end, end)
        struct.pack_into('<I', pad, 36, 0xc0000080)  # read/write uninitialized data
        out.append(pad)
    out.append(s)
    end = (va+max(virtual_size, raw_size)+alignment-1)//alignment*alignment
assert table+len(out)*40 <= headers
struct.pack_into('<H', data, pe+6, len(out))
data[table:table+len(out)*40] = b''.join(out)
path.write_bytes(data)
print(f'Harness sections: {count} -> {len(out)} (virtual gaps reserved)')

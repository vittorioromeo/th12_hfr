"""Check the frozen profile against a user-supplied file, without running the game."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import os
import tempfile
import pefile

root = Path(__file__).resolve().parent.parent
exe = Path(sys.argv[1]).resolve()
data = exe.read_bytes()
source = (root / "src/games/th06nc.c").read_text()
expected_hash = re.search(r'\.sha256 = "([0-9a-f]{64})"', source)[1]
assert hashlib.sha256(data).hexdigest() == expected_hash, "Unsupported executable fingerprint"
pe = pefile.PE(data=data)
assert pe.FILE_HEADER.Machine == 0x8664 and pe.OPTIONAL_HEADER.SizeOfImage == 0xC6B000
table = source.split("th06nc_signatures[] = {", 1)[1].split("};", 1)[0]
count = 0
for rva, size, values in re.findall(r"\{(0x[0-9a-f]+), (\d+), \{([^}]+)\}\}", table):
    frozen = bytes(int(x.strip(), 16) for x in values.split(","))
    assert len(frozen) == int(size)
    assert pe.get_data(int(rva, 16), len(frozen)) == frozen, f"Signature mismatch at {rva}"
    count += 1
assert count >= 14
# Every dimming rule names a draw callback; a callback the game never enters is a rule that
# silently does nothing, so require each to be a real function entry (it has a .pdata record).
# A pool is arithmetic on live memory, so require the whole array to lie inside the image.
import struct
directory = next(d for d in pe.OPTIONAL_HEADER.DATA_DIRECTORY
                 if d.name == "IMAGE_DIRECTORY_ENTRY_EXCEPTION")
image = pe.get_memory_mapped_image()
entries = {struct.unpack("<III", image[o:o + 12])[0]
           for o in range(directory.VirtualAddress,
                          directory.VirtualAddress + directory.Size, 12)}
rules = re.findall(r"\{(0x[0-9a-f]+), (DIM_\w+)\}", source)
assert rules, "no dimming rules"
for rva, category in rules:
    assert int(rva, 16) in entries, f"dim rule {rva} ({category}) is not a function entry"
pools = re.findall(r"\{(0x[0-9a-f]+), (0x[0-9a-f]+), (\d+), (DIM_\w+)\}", source)
for rva, stride, entries, category in pools:
    last = int(rva, 16) + int(stride, 16) * int(entries)
    assert last <= pe.OPTIONAL_HEADER.SizeOfImage, f"dim pool {rva} ({category}) leaves the image"

helpers = [root / "build/touhou_hfr.exe", root / "build/touhou_hfr64.exe"]
# The launchers are Windows binaries; away from Windows, run them the way the rest of the
# suite does so this check is not silently skipped on a development machine.
runner = [] if os.name == "nt" else [os.environ.get("RUN64", "wine")]
def check(helper, argument):
    return subprocess.run(runner + [str(helper), "--check", str(argument)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
for helper in helpers:
    assert check(helper, exe) == 0
    assert check(helper, root / "README.md") == 2
with tempfile.TemporaryDirectory(prefix="hfr-profile-test-") as directory:
    altered = Path(directory) / "th06nc.exe"
    mutation = bytearray(data)
    mutation[pe.get_offset_from_rva(0x45D82)] ^= 1
    altered.write_bytes(mutation)
    for helper in helpers:
        assert check(helper, altered) == 2
assert hashlib.sha256(exe.read_bytes()).hexdigest() == expected_hash
print(f"PASS: {count} x64 signatures, {len(rules)} dim rules, {len(pools)} dim pools, "
      f"both launcher checks, modified/non-PE rejection, original unchanged")

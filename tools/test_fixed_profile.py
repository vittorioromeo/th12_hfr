"""Check the frozen profile against a user-supplied file, without running the game."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys
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
helpers = [root / "build/touhou_hfr.exe", root / "build/touhou_hfr64.exe"]
for helper in helpers:
    assert subprocess.run([str(helper), "--check", str(exe)]).returncode == 0
    assert subprocess.run([str(helper), "--check", str(root / "README.md")]).returncode == 2
with tempfile.TemporaryDirectory(prefix="hfr-profile-test-") as directory:
    altered = Path(directory) / "th06nc.exe"
    mutation = bytearray(data)
    mutation[pe.get_offset_from_rva(0x45D82)] ^= 1
    altered.write_bytes(mutation)
    for helper in helpers:
        assert subprocess.run([str(helper), "--check", str(altered)]).returncode == 2
assert hashlib.sha256(exe.read_bytes()).hexdigest() == expected_hash
print(f"PASS: {count} x64 signatures, both launcher checks, modified/non-PE rejection, original unchanged")

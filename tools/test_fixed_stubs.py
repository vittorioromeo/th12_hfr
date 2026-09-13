"""Execute the actual emitted AMD64 relay bytes in Unicorn (no game process)."""
import json
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_RAX, UC_X86_REG_RBX, UC_X86_REG_RDI, UC_X86_REG_RSI,
    UC_X86_REG_R13, UC_X86_REG_RIP, UC_X86_REG_RSP,
    UC_X86_REG_XMM6, UC_X86_REG_XMM7,
)

plan = json.load(open(sys.argv[1], encoding="utf-8"))
base, relay = plan["base"], plan["relay"]

def machine():
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    pages = set()
    def put(address, data):
        for page in range(address & ~4095, (address + len(data) + 4095) & ~4095, 4096):
            if page not in pages:
                uc.mem_map(page, 4096)
                pages.add(page)
        uc.mem_write(address, data)
    put(relay, bytes.fromhex(plan["relay_hex"]))
    put(plan["factor"], bytes(8))       # the writable data page, separate from the code page
    put(plan["ran"], bytes(8))
    for p in plan["patches"]:
        put(base + p["rva"], bytes.fromhex(p["hex"]))
    for rva in (0x45e91, 0x45d97, 0x3c5a1, 0x3c71d):
        put(base + rva, b"\x90")
    put(base + 0x509690, struct.pack("<I", 123))
    put(0x100000, bytes(4096))
    uc.reg_write(UC_X86_REG_RSP, 0x100800)
    return uc, put

for minor in (False, True):
    uc, _ = machine()
    uc.reg_write(UC_X86_REG_RAX, 0x100 if minor else 0)
    uc.reg_write(UC_X86_REG_RBX, 999)
    uc.reg_write(UC_X86_REG_RDI, 999)
    uc.reg_write(UC_X86_REG_RSI, 0x1234)
    uc.reg_write(UC_X86_REG_R13, 0)
    target = base + (0x45e91 if minor else 0x45d97)
    uc.emu_start(base + 0x45d8b, target, count=30)
    assert uc.reg_read(UC_X86_REG_RIP) == target
    assert uc.reg_read(UC_X86_REG_RBX) == (999 if minor else 123)
    assert uc.reg_read(UC_X86_REG_RDI) == (999 if minor else 0)
    assert uc.reg_read(UC_X86_REG_RSI) == (0x1234 if minor else 0x1200)
    assert uc.reg_read(UC_X86_REG_RSP) == 0x100800

for minor in (False, True):
    uc, put = machine()
    # The fifth 16-byte leaf relay is fixed wait; replace only its compiled C
    # destination with a mock return, leaving every emitted call/jump byte intact.
    code = bytes.fromhex(plan["relay_hex"])
    callback = struct.unpack_from("<Q", code, 4 * 16 + 6)[0]
    put(callback, b"\xc3")
    def callback_result(u, address, size, unused):
        if address == callback:
            assert u.reg_read(UC_X86_REG_RSP) == 0x1007F8
            u.reg_write(UC_X86_REG_RAX, int(minor))
    uc.hook_add(UC_HOOK_CODE, callback_result)
    target = base + (0x3c71d if minor else 0x3c5a1)
    uc.emu_start(base + 0x3c45c, target, count=30)
    assert uc.reg_read(UC_X86_REG_RIP) == target
    assert uc.reg_read(UC_X86_REG_RSP) == 0x100800

print("PASS: emitted x64 post-update and wait relays, both branches, registers and stack balance")

# The player movement relay: the two per-frame multiplies gain the sub-tick factor, the
# facing store and the resume are unchanged, and it must clobber nothing.
PLAYER_MOTION, PLAYER_RESUME = 0x69388, 0x693a0
SCALE_X, SCALE_Y, FACING_Y = 0x7710, 0x7714, 0x78a0
for factor, expect_flag in ((1.0, 1), (0.0, 1)):
    uc, put = machine()
    player = 0x200000
    put(player, bytes(0x8000))
    uc.mem_write(player + SCALE_X, struct.pack("<f", 3.0))
    uc.mem_write(player + SCALE_Y, struct.pack("<f", 5.0))
    # The factor and the "site ran" byte live on the separate writable data page, whose
    # addresses the runtime reports; machine() maps that page because it follows the relay.
    uc.mem_write(plan["factor"], struct.pack("<f", factor))
    uc.mem_write(plan["ran"], b"\x00")
    uc.reg_write(UC_X86_REG_RDI, player)
    uc.reg_write(UC_X86_REG_RBX, 0xbeef)
    uc.reg_write(UC_X86_REG_RSI, 0xcafe)
    for register, value in ((UC_X86_REG_XMM6, 2.0), (UC_X86_REG_XMM7, 7.0)):
        uc.reg_write(register, struct.unpack("<I", struct.pack("<f", value))[0])
    uc.emu_start(base + PLAYER_MOTION, base + PLAYER_RESUME, count=30)
    assert uc.reg_read(UC_X86_REG_RIP) == base + PLAYER_RESUME
    x = struct.unpack("<f", struct.pack("<I", uc.reg_read(UC_X86_REG_XMM6) & 0xFFFFFFFF))[0]
    y = struct.unpack("<f", struct.pack("<I", uc.reg_read(UC_X86_REG_XMM7) & 0xFFFFFFFF))[0]
    assert x == 2.0 * 3.0 * factor, (x, factor)
    assert y == 7.0 * 5.0 * factor, (y, factor)
    # The facing value the animation triggers consume is stored before either multiply.
    assert struct.unpack("<f", uc.mem_read(player + FACING_Y, 4))[0] == 7.0
    assert uc.mem_read(plan["ran"], 1)[0] == expect_flag
    assert uc.reg_read(UC_X86_REG_RDI) == player
    assert uc.reg_read(UC_X86_REG_RBX) == 0xbeef and uc.reg_read(UC_X86_REG_RSI) == 0xcafe
    assert uc.reg_read(UC_X86_REG_RSP) == 0x100800

print("PASS: emitted player movement relay scales the step, keeps facing, records the site and clobbers nothing")

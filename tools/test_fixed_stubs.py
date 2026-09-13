"""Execute the actual emitted AMD64 relay bytes in Unicorn (no game process)."""
import json
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_RAX, UC_X86_REG_RBX, UC_X86_REG_RDI, UC_X86_REG_RSI,
    UC_X86_REG_R13, UC_X86_REG_RIP, UC_X86_REG_RSP,
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

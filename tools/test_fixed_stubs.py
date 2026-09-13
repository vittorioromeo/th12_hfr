"""Execute the actual emitted AMD64 relay bytes in Unicorn (no game process)."""
import json
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_RAX, UC_X86_REG_RBX, UC_X86_REG_RDI, UC_X86_REG_RSI,
    UC_X86_REG_R13, UC_X86_REG_RIP, UC_X86_REG_RSP,
    UC_X86_REG_XMM6, UC_X86_REG_XMM7,
    UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R12, UC_X86_REG_R15, UC_X86_REG_EFLAGS,
    UC_X86_REG_XMM1, UC_X86_REG_XMM2,
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

# The sub-stepped projectile relays. Each gate must run its relocated block and resume when
# the pass flag is clear, and jump past it without the side effect when it is set.
MINOR, DT, RAN = plan["proj_minor"], plan["proj_dt"], plan["proj_ran"]
BULLET = 0x300000
POS, VEL, DESC = 0x30, 0x08, 0x150
TIMER_PREV, TIMER_CUR, STATE, OFFSCREEN = 0x28, 0x2c, 0x44, 0x604

def projectile_machine(minor):
    uc, put = machine()
    put(BULLET, bytes(0x900))
    put(plan["proj_minor"] & ~4095, bytes(4096))
    put(base + 0x30cdbc, struct.pack("<f", 1.5707964))   # the constant the laser setup reloads
    uc.mem_write(MINOR, bytes([minor]))
    uc.reg_write(UC_X86_REG_RBX, BULLET)
    return uc, put

# --- motion: position advances by velocity scaled by the sub-step's length -----------------
for dt in (1.0, 0.25, 0.0):
    uc, _ = projectile_machine(0)
    uc.mem_write(DT, struct.pack("<f", dt))
    start = (1.0, 2.0, 3.0); vel = (0.5, -0.25, 0.125)
    for i, v in enumerate(start): uc.mem_write(BULLET + POS + 4 * i, struct.pack("<f", v))
    for i, v in enumerate(vel):   uc.mem_write(BULLET + VEL + 4 * i, struct.pack("<f", v))
    uc.mem_write(BULLET + DESC, struct.pack("<Q", 0xdead0000))
    for r, v in ((UC_X86_REG_RSI, 0x1111), (UC_X86_REG_RDI, 0x2222), (UC_X86_REG_R13, 0x3333)):
        uc.reg_write(r, v)
    uc.emu_start(base + 0x1102c, base + 0x11060, count=60)
    assert uc.reg_read(UC_X86_REG_RIP) == base + 0x11060
    got = [struct.unpack("<f", uc.mem_read(BULLET + POS + 4 * i, 4))[0] for i in range(3)]
    want = [start[i] + vel[i] * dt for i in range(3)]
    assert got == want, (dt, got, want)
    # the cull that follows reads the descriptor pointer and the new x/y from xmm2/xmm1
    assert uc.reg_read(UC_X86_REG_RAX) == 0xdead0000
    assert struct.unpack("<f", struct.pack("<I", uc.reg_read(UC_X86_REG_XMM2) & 0xFFFFFFFF))[0] == want[0]
    assert struct.unpack("<f", struct.pack("<I", uc.reg_read(UC_X86_REG_XMM1) & 0xFFFFFFFF))[0] == want[1]
    assert uc.reg_read(UC_X86_REG_RBX) == BULLET
    assert uc.reg_read(UC_X86_REG_RSI) == 0x1111 and uc.reg_read(UC_X86_REG_RDI) == 0x2222
    assert uc.reg_read(UC_X86_REG_RSP) == 0x100800

# at dt = 1 the rewritten block must equal the original adds bit for bit, for awkward values
for px, vx in ((16777216.0, 1.0), (0.1, 0.2), (-3.4e18, 5.6e18), (1e-40, 1e-40)):
    uc, _ = projectile_machine(0)
    uc.mem_write(DT, struct.pack("<f", 1.0))
    uc.mem_write(BULLET + POS, struct.pack("<f", px)); uc.mem_write(BULLET + VEL, struct.pack("<f", vx))
    uc.emu_start(base + 0x1102c, base + 0x11060, count=60)
    got = struct.unpack("<f", uc.mem_read(BULLET + POS, 4))[0]
    want = struct.unpack("<f", struct.pack("<f", struct.unpack("<f", struct.pack("<f", px))[0]
                                           + struct.unpack("<f", struct.pack("<f", vx))[0]))[0]
    assert got == want, (px, vx, got, want)

# --- the state switch: skipped on a sub-step pass, and its flags preserved otherwise -------
for minor in (0, 1):
    uc, _ = projectile_machine(minor)
    uc.mem_write(BULLET + STATE, struct.pack("<H", 1))
    uc.reg_write(UC_X86_REG_R12, 1)
    target = base + (0x11021 if minor else 0x1098b)
    uc.emu_start(base + 0x10982, target, count=30)
    assert uc.reg_read(UC_X86_REG_RIP) == target
    if not minor:
        # the `je` at the resume reads ZF from `sub ecx,r12d`: state 1 minus 1 is zero
        assert uc.reg_read(UC_X86_REG_RDX) == 1
        assert uc.reg_read(UC_X86_REG_RCX) == 0
        assert uc.reg_read(UC_X86_REG_EFLAGS) & 0x40, "ZF must survive to the switch"
    assert uc.reg_read(UC_X86_REG_RSP) == 0x100800

# --- the off-screen counter, the per-bullet timer and the manager's frame counter ----------
for minor in (0, 1):
    uc, _ = projectile_machine(minor)
    uc.mem_write(BULLET + OFFSCREEN, struct.pack("<H", 7))
    uc.emu_start(base + 0x11298, base + 0x1129f, count=30)
    assert struct.unpack("<H", uc.mem_read(BULLET + OFFSCREEN, 2))[0] == (7 if minor else 8)

for minor in (0, 1):
    uc, _ = projectile_machine(minor)
    uc.mem_write(BULLET + TIMER_PREV, struct.pack("<i", 11))
    uc.mem_write(BULLET + TIMER_CUR, struct.pack("<i", 12))
    uc.emu_start(base + 0x11562, base + 0x1156d, count=30)
    prev = struct.unpack("<i", uc.mem_read(BULLET + TIMER_PREV, 4))[0]
    cur = struct.unpack("<i", uc.mem_read(BULLET + TIMER_CUR, 4))[0]
    assert (prev, cur) == ((11, 12) if minor else (12, 13)), (minor, prev, cur)

MANAGER = 0x400000
for minor in (0, 1):
    uc, put = projectile_machine(minor)
    put(MANAGER, bytes(0x100))
    uc.reg_write(UC_X86_REG_R15, MANAGER)
    uc.mem_write(MANAGER + 4, struct.pack("<i", 41))
    uc.reg_write(UC_X86_REG_RAX, 41)
    uc.emu_start(base + 0x11796, base + 0x1179f, count=30)
    epoch = struct.unpack("<i", uc.mem_read(MANAGER, 4))[0]
    nxt = struct.unpack("<i", uc.mem_read(MANAGER + 4, 4))[0]
    assert (epoch, nxt) == ((0, 41) if minor else (41, 42)), (minor, epoch, nxt)
    # only a full pass records that projectiles were updated at all
    assert uc.mem_read(RAN, 1)[0] == (0 if minor else 1)

# --- a laser's head grows by its speed scaled the same way ---------------------------------
LASER, HEAD, SPEED = 0x300200, 0x0, 0x258
for dt in (1.0, 0.25):
    uc, _ = projectile_machine(0)
    uc.mem_write(DT, struct.pack("<f", dt))
    uc.reg_write(UC_X86_REG_RBX, LASER)
    uc.mem_write(LASER + HEAD, struct.pack("<f", 40.0))
    uc.mem_write(LASER + SPEED, struct.pack("<f", 6.0))
    uc.emu_start(base + 0x1161d, base + 0x11629, count=30)
    assert uc.reg_read(UC_X86_REG_RIP) == base + 0x11629
    got = struct.unpack("<f", struct.pack("<I", uc.reg_read(UC_X86_REG_XMM1) & 0xFFFFFFFF))[0]
    assert got == 40.0 + 6.0 * dt, (dt, got)
    assert struct.unpack("<f", uc.mem_read(LASER + HEAD, 4))[0] == 40.0   # stored later, not here
    assert uc.reg_read(UC_X86_REG_RSP) == 0x100800

# --- a laser's own timer and animation step only on a full pass ----------------------------
for minor in (0, 1):
    uc, _ = projectile_machine(minor)
    uc.reg_write(UC_X86_REG_RBX, LASER)
    uc.mem_write(LASER - 8, struct.pack("<i", 5))
    target = base + (0x1172f if minor else 0x1171b)
    uc.emu_start(base + 0x11714, target, count=30)
    assert uc.reg_read(UC_X86_REG_RIP) == target
    if not minor:
        # the relocated pair leaves the timer in eax and the VM pointer in rdx for the
        # call that follows; the call itself is left in place at its original address
        assert uc.reg_read(UC_X86_REG_RAX) == 5
        assert uc.reg_read(UC_X86_REG_RDX) == LASER + 0xc
    assert uc.reg_read(UC_X86_REG_RSP) == 0x100800

print("PASS: emitted projectile relays scale motion, and gate the switch, counters, timers and lasers")

"""TH20 adapter machine-code regression tests. Requires Unicorn.
Usage: python tools/test_th20_stubs.py build/tests/th20.exe

The executable is relocatable; the harness maps it at its preferred base, which is what these
addresses are. The hooks that are C functions (the timer jumps, the damage gate, the option
display) are not emulated here: they are exercised by the record/playback runs in
docs/games/TH20_DEVNOTES.md.
"""
from x86_test_support import *

def xmm0():
    return struct.unpack('<f', bytes(u.reg_read(UC_X86_REG_XMM0).to_bytes(16, 'little')[:4]))[0]

# 0x4f748b: Player::update's switch on the life state. Alive (1) is dispatched on every tick;
# every other state only on the frame's first, and otherwise control goes to the timers' tail.
TABLE = 0x4f83fc
alive, spawning = ri(TABLE + 4), ri(TABLE + 0)
for major in (0, 1):
    for state, target in ((1, alive), (0, spawning), (2, ri(TABLE + 8)), (4, ri(TABLE + 16)), (6, ri(TABLE + 24))):
        reset(major)
        u.reg_write(UC_X86_REG_ECX, state)
        want = target if (major or state == 1) else 0x4f8093
        assert run(0x4f748b, *{alive, spawning, ri(TABLE + 8), ri(TABLE + 16), ri(TABLE + 24), 0x4f8093}) == want, (major, state)
        assert u.reg_read(UC_X86_REG_ECX) == state and u.reg_read(UC_X86_REG_ESP) == STACK
print('PASS: TH20 player states other than alive run on the boundary tick only')

# The per-call integer countdowns: taken on the boundary tick, skipped whole on the others.
def countdown(start, skip, base_disp, fields, major):
    reset(major)
    ebp = u.reg_read(UC_X86_REG_EBP); obj = ARENA + 0x4000
    wi(ebp + base_disp & 0xffffffff, obj)
    for off, v in fields: wi(obj + off, v)
    return run(start, skip), [ri(obj + off) for off, _ in fields]
for major in (0, 1):
    pc, (a, b) = countdown(0x48620c, 0x48623c, -4, ((0x18, 5), (0x30, 7)), major)
    assert (a, b) == ((4, 6) if major else (5, 7)), (major, a, b)
    for start, skip in ((0x4d6eb0, 0x4d6ed1), (0x4d737d, 0x4d739e), (0x4d799d, 0x4d79be)):
        pc, (c,) = countdown(start, skip, -0x14, ((0x6cc, 9),), major)
        assert c == (8 if major else 9), (hex(start), major, c)
    pc, (f, _) = countdown(0x4fa762, 0x4fa784, -4, ((0x20e8, 3), (0x14, 2)), major)   # flag bit 1: the counter is live
    assert f == (4 if major else 3), (major, f)
print('PASS: TH20 bullet, laser and focus countdowns count once a frame')

# 0x485cd1: a bullet in its spawn state moves by velocity / 2.0 a call; the divisor becomes
# 2.0 / factor, and exactly 2.0 with no sub-stepping.
for factor in (1.0, 0.5, 0.25, 1.0 / 6.0):
    reset(); wf(meta['factor'], factor)
    run(0x485cd1, 0x485cd9)
    assert abs(xmm0() - 2.0 / factor) < 1e-5 and (factor != 1.0 or xmm0() == 2.0), (factor, xmm0())
print('PASS: TH20 spawn-state bullets cover the same distance per frame')

# 0x504547 / 0x504566: a player shot's per-call turn and acceleration, scaled by the factor.
for site, stop, disp, reg in ((0x504547, 0x50454c, 0x18, UC_X86_REG_EAX), (0x504566, 0x50456b, 0x20, UC_X86_REG_EDX)):
    for factor in (1.0, 0.25):
        reset(); wf(meta['factor'], factor)
        d = u.reg_read(reg); wf(d + disp, 0.75)
        run(site, stop)
        assert abs(xmm0() - 0.75 * factor) < 1e-6, (hex(site), factor, xmm0())
print('PASS: TH20 shot turn and acceleration are per frame, not per tick')

# 0x4ffaa2 / 0x4ffab6: Player::move truncates a float step into the fixed-point position. The
# fraction is carried, so six sixths of a step move her as far as one whole step.
PL = ARENA + 0x8000
for site, stop, disp, axis in ((0x4ffaa2, 0x4ffaaa, 0x20c4, 0), (0x4ffab6, 0x4ffabe, 0x20c8, 1)):
    for step in (576.0, -576.0, 384.5, 100.0):
        reset(); wf(meta['factor'], 1.0 / 6.0)
        total = 0
        for _ in range(6):
            u.reg_write(UC_X86_REG_ECX, PL); wf(PL + disp, step / 6.0)
            run(site, stop)
            v = u.reg_read(UC_X86_REG_EDX); total += v - (1 << 32) if v & 0x80000000 else v
        # never more than the one unit still held in the residual
        assert abs(total - step) <= 1.0 and abs(total + rf(meta['residual'] + 4 * axis) - step) < 1e-2, (hex(site), step, total)
    reset(); wf(meta['factor'], 1.0)
    u.reg_write(UC_X86_REG_ECX, PL); wf(PL + disp, 3.75); run(site, stop)
    assert u.reg_read(UC_X86_REG_EDX) == 3 and rf(meta['residual'] + 4 * axis) == 0.0, 'stock stepping truncates as the game does'
print('PASS: TH20 player movement carries its truncation across sub-steps')

# The stone fields (weapon*.cpp): countdown and bullet-eating both belong to the boundary tick.
for start, skip in ((0x53645d, 0x536527), (0x53674f, 0x5367b8), (0x53790d, 0x5379d7), (0x537bdf, 0x537c48)):
    reset(0)
    ebp = u.reg_read(UC_X86_REG_EBP); obj = ARENA + 0x4000; wi(ebp - 0x50, obj); wi(obj + 0x58, 0)
    assert run(start, skip, start + 7) == skip, hex(start)
    reset(1); wi(u.reg_read(UC_X86_REG_EBP) - 0x50, obj)
    assert run(start, skip, start + 7) == start + 7, hex(start)
print('PASS: TH20 stone fields act on the boundary tick only')

# 0x4850a2: the grazed bullet's tint and shake, behind a switch. On, the game's own block is
# entered with EAX on the in-range timer. Off, control reaches the end of the routine with the
# sprite untouched, after exactly the block's two draws from the generator at 0x5ba4c4
# (Rng::float(1.0), 0x4298b0 -- it takes a lock, so the calls are answered here, not emulated).
RNG, RNG_FLOAT = 0x5ba4c4, 0x4298b0
def graze(on):
    reset(); wi(meta['th20_graze_bullets'], on)
    ebp = u.reg_read(UC_X86_REG_EBP); b = ARENA + 0x6000; wi(ebp - 0x60, b)
    draws = 0; pc = run(0x4850a2, 0x4850aa, 0x4851ef, RNG_FLOAT)
    while pc == RNG_FLOAT:
        esp = u.reg_read(UC_X86_REG_ESP)
        assert u.reg_read(UC_X86_REG_ECX) == RNG and rf(esp + 4) == 1.0
        ret = ri(esp); u.reg_write(UC_X86_REG_ESP, esp + 8); fpush(0.5); draws += 1   # ret 4, a float in st0
        pc = run(ret, 0x4850aa, 0x4851ef, RNG_FLOAT)
    return pc, draws, u.reg_read(UC_X86_REG_EAX), u.reg_read(UC_X86_REG_ESP)
pc, draws, eax, esp = graze(1)
assert pc == 0x4850aa and draws == 0 and eax == ARENA + 0x6000 + 0x4b0 and esp == STACK
pc, draws, eax, esp = graze(0)
assert pc == 0x4851ef and draws == 2 and esp == STACK
u.mem_write(BOOT, b'\xdb\xe3'); u.emu_start(BOOT, BOOT + 2)   # and the x87 stack is empty again (fninit for the next test)
print('PASS: TH20 grazed-bullet tint and shake follow their switch, with the same two random draws')

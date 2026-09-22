"""TH18 adapter machine-code regression tests. Requires Unicorn.
Usage: python tools/test_th18_stubs.py build/tests/th18.exe

The emitted stubs, run on the harness's copy of the executable. The C-side hooks (the replay
wrappers, the catch-up tick) are exercised by the demo-trace runs in docs/games/TH18_DEVNOTES.md.
"""
from x86_test_support import *

SPEED, RATE_TABLE = 0x4ccbf0, 0x4b35c0
map_region(SPEED, 8)
assert ri(RATE_TABLE) == SPEED, 'rate table entry 0 must be the game speed'

def xmm(reg):
    return struct.unpack('<f', bytes(u.reg_read(reg).to_bytes(16, 'little')[:4]))[0]
def set_xmm(reg, value):
    u.reg_write(reg, int.from_bytes(struct.pack('<f', value) + bytes(12), 'little'))

# 0x45bec3: Player::update's switch on the life state. Alive (1) is dispatched on every tick;
# every other state only on the frame's first, and otherwise control goes to the tail.
TABLE, TAIL = 0x45ca8c, 0x45c3d9
targets = [ri(TABLE + 4 * i) for i in range(5)]
for major in (0, 1):
    for state in range(5):
        reset(major)
        u.reg_write(UC_X86_REG_EAX, state)
        want = targets[state] if (major or state == 1) else TAIL
        assert run(0x45bec3, *set(targets) | {TAIL}) == want, (major, state)
        assert u.reg_read(UC_X86_REG_EAX) == state and u.reg_read(UC_X86_REG_ESP) == STACK
print('PASS: TH18 player states other than alive run on the boundary tick only')

# 0x45b6f7: the movement truncations, xmm1 -> ecx (x) and xmm2 -> edx (y). At factor 1.0 the
# original instructions; sub-stepped, the truncation residual is carried so that four quarter
# steps add up to the whole step.
reset(); wf(meta['factor'], 1.0)
set_xmm(UC_X86_REG_XMM1, 2.75); set_xmm(UC_X86_REG_XMM2, -3.5)
run(0x45b6f7, 0x45b6ff)
assert u.reg_read(UC_X86_REG_ECX) == 2 and u.reg_read(UC_X86_REG_EDX) == (-3 & 0xffffffff)
assert rf(meta['residual']) == 0.0 and rf(meta['residual'] + 4) == 0.0, 'no residual at stock speed'
reset(); wf(meta['factor'], 0.25)
tx = ty = 0
for i in range(4):
    set_xmm(UC_X86_REG_XMM1, 5.0 * 0.25); set_xmm(UC_X86_REG_XMM2, -7.0 * 0.25)
    set_xmm(UC_X86_REG_XMM0, 123.0); set_xmm(UC_X86_REG_XMM4, 456.0)
    run(0x45b6f7, 0x45b6ff)
    tx += struct.unpack('<i', struct.pack('<I', u.reg_read(UC_X86_REG_ECX)))[0]
    ty += struct.unpack('<i', struct.pack('<I', u.reg_read(UC_X86_REG_EDX)))[0]
    assert xmm(UC_X86_REG_XMM0) == 123.0 and xmm(UC_X86_REG_XMM4) == 456.0, 'the borrowed registers must be given back'
    assert xmm(UC_X86_REG_XMM1) == 1.25 and xmm(UC_X86_REG_XMM2) == -1.75, 'the sources are left alone'
    assert u.reg_read(UC_X86_REG_ESP) == STACK
assert (tx, ty) == (5, -7), (tx, ty)
print('PASS: TH18 movement carries the truncation residual across the ticks')

# 0x45c63a: the invincibility blink asks whether the state timer's float moved across this
# tick's Player call.
for moved in (0, 1):
    reset()
    wf(meta['ptf_prev'], 10.0); wf(meta['ptf_cur'], 10.5 if moved else 10.0)
    pl = u.reg_read(UC_X86_REG_EDI); wi(pl + 0x638, 11); wi(pl + 0x634, 10)
    pc = run(0x45c63a, 0x45c648, 0x45c674)
    assert pc == (0x45c648 if moved else 0x45c674), (moved, hex(pc))
    if moved: assert u.reg_read(UC_X86_REG_EAX) == 11
print('PASS: TH18 invincibility blink follows the state timer float')

# 0x45b8e4 / 0x45b89d: the focus counter and the position history shift, once a frame, gated
# on the player's state timer (prev +0x634, integer +0x638).
for changed in (0, 1):
    reset()
    pl = u.reg_read(UC_X86_REG_EDI); wi(pl + 0x634, 4); wi(pl + 0x638, 5 if changed else 4); wi(pl + 0x477e8, 9)
    run(0x45b8e4, 0x45b8ea)
    assert ri(pl + 0x477e8) == (10 if changed else 9), (changed, ri(pl + 0x477e8))
    reset()
    pl = u.reg_read(UC_X86_REG_EDI); wi(pl + 0x634, 4); wi(pl + 0x638, 5 if changed else 4)
    pc = run(0x45b89d, 0x45b8a8, 0x45b8c3)
    assert pc == (0x45b8a8 if changed else 0x45b8c3), (changed, hex(pc))
    if changed: assert u.reg_read(UC_X86_REG_EDX) == 0x20 and u.reg_read(UC_X86_REG_ECX) == pl + 0x47904
print('PASS: TH18 focus counter and position history move once a frame')

# 0x45bd2f: the options' approach runs on frame boundaries only; the counter is EBX.
for major in (0, 1):
    for count in (0x10, 0x1e):
        reset(major); u.reg_write(UC_X86_REG_EBX, count)
        pc = run(0x45bd2f, 0x45bd38, 0x45bde1)
        assert pc == (0x45bd38 if (major and count >= 0x1e) else 0x45bde1), (major, count, hex(pc))
print('PASS: TH18 option approach on the boundary tick only')

# 0x45bd0b / 0x45ee20: the option and shot-object callbacks, once a frame.
for major in (0, 1):
    for present in (0, 1):
        reset(major); o = u.reg_read(UC_X86_REG_ESI); wi(o + 0x88, 0x11223344 if present else 0)
        pc = run(0x45bd0b, 0x45bd15, 0x45bd1f)
        assert pc == (0x45bd15 if (major and present) else 0x45bd1f), ('option', major, present, hex(pc))
        if pc == 0x45bd15: assert u.reg_read(UC_X86_REG_EAX) == 0x11223344
        reset(major); w = u.reg_read(UC_X86_REG_EDI); wi(w + 0xd8, 0x55667788 if present else 0)
        pc = run(0x45ee20, 0x45ee2a, 0x45ee37)
        assert pc == (0x45ee2a if (major and present) else 0x45ee37), ('shot object', major, present, hex(pc))
print('PASS: TH18 shot-type callbacks run on the boundary tick only')

# 0x45c402: the shot loop's head. An inactive entry is skipped; on a minor tick so is one fired
# this frame (age counter still 0).
for major in (0, 1):
    for active, age in ((0, 0), (1, 0), (1, -3)):
        reset(major); e = u.reg_read(UC_X86_REG_ECX); wi(e, active); wi(e + 0x88, age)
        pc = run(0x45c402, 0x45c40b, 0x45c510)
        want = 0x45c40b if (active and (major or age != 0)) else 0x45c510
        assert pc == want, (major, active, age, hex(pc))
print('PASS: TH18 shots fired this frame wait for the next boundary tick')

# 0x45c41b / 0x45c43c: the shot entry's two per-frame rates, scaled by the factor.
for factor in (1.0, 0.25):
    reset(); wf(meta['factor'], factor)
    t = u.reg_read(UC_X86_REG_EDI)
    wf(t - 0x60, 0.5); wf(t - 0x64, 3.0); wf(t - 0x58, -0.25); wf(t - 0x5c, 1.0)
    run(0x45c41b, 0x45c427)
    assert abs(xmm(UC_X86_REG_XMM0) - (3.0 + 0.5 * factor)) < 1e-6 and u.reg_read(UC_X86_REG_ECX) == 0
    run(0x45c43c, 0x45c446)
    assert abs(xmm(UC_X86_REG_XMM0) - (1.0 - 0.25 * factor)) < 1e-6
print('PASS: TH18 shot rates are per frame, not per tick')

# 0x45c4a5: the shot's hit-cadence timer counts down a whole frame on the boundary tick and not
# at all on the others, with prev set to the integer either way and xmm4 loaded on both paths.
for major in (0, 1):
    reset(major); wf(SPEED, 0.25)
    t = u.reg_read(UC_X86_REG_EDI)
    wi(t - 8, 9); wi(t - 4, 7); wf(t, 7.0); wi(t + 4, 0)
    u.reg_write(UC_X86_REG_ECX, SPEED)
    run(0x45c4a5, 0x45c4f7)
    assert ri(t - 8) == 7, 'prev must be set to the integer on every tick'
    assert xmm(UC_X86_REG_XMM0) == (6.0 if major else 7.0), (major, xmm(UC_X86_REG_XMM0))
    assert xmm(UC_X86_REG_XMM4) == rf(0x4b9174)
print('PASS: TH18 shot cadence counts down a whole frame on the boundary tick only')

# 0x45c4ff: the shot's age counter, once a frame; the integer store and the flag test around it
# are kept.
for major in (0, 1):
    reset(major); t = u.reg_read(UC_X86_REG_EDI); wi(t + 0x20, -5); u.reg_write(UC_X86_REG_EAX, 7)
    run(0x45c4ff, 0x45c507)
    assert ri(t - 4) == 7 and ri(t + 0x20) == ((-6 if major else -5) & 0xffffffff), (major, ri(t + 0x20))
    assert u.reg_read(UC_X86_REG_EFLAGS) & 0x40 == 0, 'test eax,eax with eax=7: ZF clear'
print('PASS: TH18 shot age counts once a frame')

# 0x45f144: player shots against enemies, gated on the state timer float having moved.
for moved in (0, 1):
    reset(); wf(meta['ptf_prev'], 3.0); wf(meta['ptf_cur'], 3.25 if moved else 3.0)
    pc = run(0x45f144, 0x45f14c, 0x45f153)
    assert pc == (0x45f153 if moved else 0x45f14c), (moved, hex(pc))
print('PASS: TH18 shot-versus-enemy guard follows the state timer float')

# 0x452c10, inside timer_rewind (0x452bf0): the rate it looks up is replaced by &g_logical, so
# the shot cycle rewinds by a whole 14 at any sub-step factor.
for factor, logical in ((0.25, 1.0), (1.0, 1.0), (1.0 / 6.0, 1.0), (0.25, 0.5)):
    reset()
    wf(meta['factor'], factor); wf(meta['logical'], logical); wf(SPEED, factor)
    timer = u.reg_read(UC_X86_REG_ESI)
    wi(timer + 0x00, 13); wi(timer + 0x04, 14); wf(timer + 0x08, 14.0); wi(timer + 0x0c, 0)
    u.reg_write(UC_X86_REG_ECX, timer)
    u.reg_write(UC_X86_REG_ESP, STACK - 8)
    wi(STACK - 8, BOOT + 128); wi(STACK - 4, 14)
    run(0x452bf0, BOOT + 128)
    want = 14.0 - 14.0 * logical
    assert abs(rf(timer + 8) - want) < 1e-5, f'factor {factor}: rewound to {rf(timer + 8)}, wanted {want}'
    assert ri(timer + 4) == int(want)
    assert ri(timer + 0) == 14, 'the rewind must leave prev on the value it came from'
    assert u.reg_read(UC_X86_REG_ESP) == STACK
print('PASS: TH18 shot cycle rewinds by a whole cycle at any sub-step factor')

# 0x45f068: the shot object's timer (EDI = its float) advances one whole frame on the boundary
# tick and not at all on the others, prev set to the integer either way.
for major in (0, 1):
    reset(major); wf(SPEED, 0.25)
    t = u.reg_read(UC_X86_REG_EDI)
    wi(t - 8, 3); wi(t - 4, 5); wf(t, 5.0); wi(t + 4, 0)
    u.reg_write(UC_X86_REG_ECX, SPEED)
    run(0x45f068, 0x45f0aa)
    assert ri(t - 8) == 5, 'prev must be set to the integer on every tick'
    assert ri(t - 4) == (6 if major else 5) and rf(t) == (6.0 if major else 5.0), (major, ri(t - 4), rf(t))
print('PASS: TH18 shot object timer advances a whole frame on the boundary tick only')

# 0x45e6c9: the muzzle's motion step at a shot's creation is made with the logical speed in
# the speed global, which is put back afterwards. The callee is replaced by one that returns
# what it saw in the speed.
u.mem_write(0x402bf0, b'\xa1' + struct.pack('<I', SPEED) + b'\xc3')   # mov eax,[speed]; ret
for factor, logical in ((0.25, 1.0), (0.5, 0.5), (1.0, 1.0)):
    reset(); wf(meta['factor'], factor); wf(meta['logical'], logical); wf(SPEED, factor * logical)
    run(0x45e6c9, 0x45e6ce)
    assert u.reg_read(UC_X86_REG_EAX) == struct.unpack('<I', struct.pack('<f', logical))[0], (factor, logical)
    assert rf(SPEED) == factor * logical, 'the sub-step speed must be restored'
    assert u.reg_read(UC_X86_REG_ESP) == STACK
print('PASS: TH18 shot creation steps the muzzle by a whole frame')

# 0x446819: the graze slow-down factor recovers by a constant per pass with no speed multiply;
# scaled by the sub-step factor it recovers at the same rate per frame.
ITEMS = 0x21000000
map_region(ITEMS + 0xe6bb14, 16)
step = rf(0x4b90d8)
assert step > 0
for factor in (1.0, 0.25, 1.0 / 6.0):
    reset(); wf(meta['factor'], factor)
    u.reg_write(UC_X86_REG_ECX, ITEMS); set_xmm(UC_X86_REG_XMM0, 0.3)
    run(0x446819, 0x446829)
    got = rf(ITEMS + 0xe6bb14)
    assert abs(got - (0.3 + step * factor)) < 1e-6, f'factor {factor}: {got}'
print('PASS: TH18 graze slow-down recovers by its constant per frame, not per tick')

# 0x471a9e: the window loop's inlined frame path is bypassed without touching a register or
# the flags; the latency compare's last byte is rewritten; the runner's entry is the jump to the
# shared runner and its ending is still the game's own return.
reset(); before = [u.reg_read(r) for r in regs]; flags = u.reg_read(UC_X86_REG_EFLAGS)
run(0x471a9e, 0x471c37)
assert before == [u.reg_read(r) for r in regs] and u.reg_read(UC_X86_REG_EFLAGS) == flags
assert u.reg_read(UC_X86_REG_ESP) == STACK
assert bytes(u.mem_read(0x4730be, 7)) == bytes.fromhex('80 3d 11 d0 4c 00 7f')
assert u.mem_read(0x4012e0, 1) == b'\xe9'
assert bytes(u.mem_read(0x4013f5, 1)) == b'\xc3'
print('PASS: TH18 frame paths routed; the runner ends on the game\'s own return')

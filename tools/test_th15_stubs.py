"""TH15 adapter machine-code regression tests. Requires Unicorn.
Usage: python tools/test_th15_stubs.py build/tests/th15.exe
"""
from x86_test_support import *

SPEED, RATE_TABLE = 0x4e73e8, 0x4ca620
map_region(SPEED, 8)
assert ri(RATE_TABLE) == SPEED, 'rate table entry 0 must be the game speed'

# 0x44b88f, inside timer_rewind (0x44b870): the rate it looks up is replaced by &g_logical, so
# the shot cycle rewinds by a whole 14 at any sub-step factor. Its only callers are the two in
# the shot cycle driver.
for factor, logical in ((0.25, 1.0), (1.0, 1.0), (1.0 / 6.0, 1.0), (0.25, 0.5)):
    reset()
    wf(meta['factor'], factor); wf(meta['logical'], logical); wf(SPEED, factor)
    timer = u.reg_read(UC_X86_REG_ESI)
    wi(timer + 0x00, 13); wi(timer + 0x04, 14); wf(timer + 0x08, 14.0); wi(timer + 0x0c, 0)
    u.reg_write(UC_X86_REG_ECX, timer)
    u.reg_write(UC_X86_REG_ESP, STACK - 8)
    wi(STACK - 8, BOOT + 128); wi(STACK - 4, 14)
    run(0x44b870, BOOT + 128)
    want = 14.0 - 14.0 * logical
    assert abs(rf(timer + 8) - want) < 1e-5, f'factor {factor}: rewound to {rf(timer + 8)}, wanted {want}'
    assert ri(timer + 4) == int(want)
    assert ri(timer + 0) == 14, 'the rewind must leave prev on the value it came from'
    assert u.reg_read(UC_X86_REG_ESP) == STACK
print('PASS: TH15 shot cycle rewinds by a whole cycle at any sub-step factor')

# 0x459156: the weapon timer advances one whole frame on the boundary tick and not at all on the
# others, with prev set to the integer either way. Stop after the integer is stored, before the
# stack-cookie check.
for major in (0, 1):
    reset(major)
    wf(SPEED, 0.25)
    obj = u.reg_read(UC_X86_REG_EDI)
    wi(obj + 0x0c, 3); wi(obj + 0x10, 5); wf(obj + 0x14, 5.0); wi(obj + 0x18, 0)
    u.reg_write(UC_X86_REG_ECX, SPEED)
    run(0x459156, 0x4591a3)
    assert ri(obj + 0x0c) == 5, 'prev must be set to the integer on every tick'
    assert ri(obj + 0x10) == (6 if major else 5), f'major={major}: integer {ri(obj + 0x10)}'
    assert rf(obj + 0x14) == (6.0 if major else 5.0)
print('PASS: TH15 weapon timer advances a whole frame on the boundary tick only')

# 0x454ec4: the shot cadence countdown, same shape, counting down. Timer at edi-8.
for major in (0, 1):
    reset(major)
    wf(SPEED, 0.25)
    t = u.reg_read(UC_X86_REG_EDI)
    wi(t - 8, 9); wi(t - 4, 7); wf(t, 7.0); wi(t + 4, 0)
    u.reg_write(UC_X86_REG_ECX, SPEED)
    run(0x454ec4, 0x454f09)
    assert ri(t - 8) == 7, 'prev must be set to the integer on every tick'
    got = struct.unpack('<f', bytes(u.reg_read(UC_X86_REG_XMM0).to_bytes(16, 'little')[:4]))[0]
    assert got == (6.0 if major else 7.0), f'major={major}: {got}'
print('PASS: TH15 shot cadence counts down a whole frame on the boundary tick only')

# 0x44017b: the graze slow-down factor recovers by a constant per pass with no speed multiply.
# Scaled by the sub-step factor it recovers at the same rate per frame.
ITEMS = 0x21000000
map_region(ITEMS + 0xe5def0, 16)
step = rf(0x4cfdf0)
assert step > 0
for factor in (1.0, 0.25, 1.0 / 6.0):
    reset()
    wf(meta['factor'], factor)
    u.reg_write(UC_X86_REG_EBX, ITEMS)
    wf(ITEMS + 0xe5def0, 0.3)
    run(0x440162, 0x44018b)
    got = rf(ITEMS + 0xe5def0)
    assert abs(got - (0.3 + step * factor)) < 1e-6, f'factor {factor}: {got}'
print('PASS: TH15 graze slow-down recovers by its constant per frame, not per tick')

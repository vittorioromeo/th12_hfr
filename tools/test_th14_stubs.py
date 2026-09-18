"""TH14 adapter machine-code regression tests. Requires Unicorn.
Usage: python tools/test_th14_stubs.py build/tests/th14.exe
"""
from x86_test_support import *

# 0x45101a: the shot cycle's rewind. `timer_rewind` at 0x414420 multiplies the amount it
# subtracts by the timer's rate, so sub-stepped it rewound the 14-step firing cycle by 14/6
# and Reimu stopped firing while the button was held. The stub swaps the rate pointer for
# &g_logical and puts it back, so the cycle rewinds by a whole 14 at any sub-step factor.
for factor, logical in ((0.25, 1.0), (1.0, 1.0), (1.0 / 6.0, 1.0), (0.25, 0.5)):
    reset()
    wf(meta['factor'], factor); wf(meta['logical'], logical)
    timer = u.reg_read(UC_X86_REG_ESI)
    wi(timer + 0x00, 13)            # prev
    wi(timer + 0x04, 14)            # int
    wf(timer + 0x08, 14.0)          # float
    wi(timer + 0x0c, meta['factor'])   # the game speed, as timer_init leaves it
    u.reg_write(UC_X86_REG_ECX, timer)
    assert run(0x45101a, 0x451021) == 0x451021
    assert rf(timer + 8) == 14.0 - 14.0 * logical, (
        f'factor {factor}: rewound to {rf(timer + 8)}, wanted {14.0 - 14.0 * logical}')
    assert ri(timer + 4) == int(14.0 - 14.0 * logical)
    assert ri(timer + 0) == 14, 'the rewind must leave prev on the value it came from'
    assert ri(timer + 0xc) == meta['factor'], 'the rate pointer must be put back'
    assert u.reg_read(UC_X86_REG_ESP) == STACK, 'the stub must not leak its saved rate pointer'
print('PASS: TH14 shot cycle rewinds by a whole cycle at any sub-step factor')
# And the reason the stub has to exist: call the game's own timer_rewind with a sub-stepped rate
# and it subtracts 14/6, not 14. If this ever stops holding, the stub above is dead weight.
reset()
wf(meta['factor'], 1.0 / 6.0)
timer = u.reg_read(UC_X86_REG_ESI)
wi(timer + 0x00, 13); wi(timer + 0x04, 14); wf(timer + 0x08, 14.0); wi(timer + 0x0c, meta['factor'])
u.reg_write(UC_X86_REG_ECX, timer)
u.reg_write(UC_X86_REG_ESP, STACK - 8)
wi(STACK - 8, BOOT + 128)   # return address
wi(STACK - 4, 14)           # the argument timer_rewind reads at [ebp+8]
run(0x414420, BOOT + 128)
assert abs(rf(timer + 8) - (14.0 - 14.0 / 6.0)) < 1e-4, rf(timer + 8)
print('PASS: TH14 timer_rewind does scale by the rate, which is what the stub is for')


# 0x45131b: the weapon timer advances one whole frame on the boundary tick and not at all on
# the others, leaving prev on the integer so the game's "did it change" guard reads unchanged.
# Stop at 0x45136b, after both stores and before the stack-cookie check the fixture cannot run.
for major in (0, 1):
    reset(major)
    obj = u.reg_read(UC_X86_REG_ESI)
    wi(obj + 0x18, 3); wi(obj + 0x1c, 5); wf(obj + 0x20, 5.0); wi(obj + 0x24, meta['factor'])
    u.reg_write(UC_X86_REG_EBP, STACK - 0x40)
    run(0x45131b, 0x45136b)
    assert ri(obj + 0x18) == 5, 'prev must be set to the integer on every tick'
    assert ri(obj + 0x1c) == (6 if major else 5), f'major={major}: integer {ri(obj + 0x1c)}'
    assert rf(obj + 0x20) == (6.0 if major else 5.0)
print('PASS: TH14 weapon timer advances a whole frame on the boundary tick only')

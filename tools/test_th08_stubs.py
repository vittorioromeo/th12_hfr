"""TH08 adapter machine-code regression tests. Requires Unicorn.

TH08's stubs differ from the TH10-14 adapters' in one respect that matters here: several of
them call back into C (the chain walker's helpers live in the DLL, not in emitted code), and
the harness dumps the stubs and the patched image but not the DLL. A call that leaves both is
answered by a page of `ret`, which is what a cdecl callee that did nothing looks like to its
caller; what is checked is everything around the call -- the branch taken, the argument
pushed, the displaced instructions, the stack and the registers.
"""
from x86_test_support import *
from unicorn import UC_HOOK_MEM_FETCH_UNMAPPED

called = []


def fetch_unmapped(uc, access, address, size, value, unused):
    page = address & ~4095
    uc.mem_map(page, 4096)
    uc.mem_write(page, b'\xc3' * 4096)
    mapped.add(page)
    return True


def note_calls(uc, address, size, unused):
    # a C callee: outside the game image, the stub buffer and the test arena
    stub = meta['stub_base']
    if not (0x400000 <= address < 0x18dc000 or stub <= address < stub + 0x10000 or ARENA <= address < ARENA + 0x100000):
        if not called or called[-1][0] != address:
            called.append((address, ri(uc.reg_read(UC_X86_REG_ESP) + 4)))


u.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED, fetch_unmapped)
u.hook_add(UC_HOOK_CODE, note_calls)

# ---- the per-call counters: five straight-line blocks gated to the frame tick.
gates = [
    (0x43147b, 21, 0x431490),   # bullet +0xda8, the off-screen grace
    (0x431524, 24, 0x43153c),   # bullet +0xdba, up
    (0x431578, 24, 0x431590),   # ... and down
    (0x432112, 21, 0x432127),   # manager +0x6ba53c
    (0x432137, 21, 0x43214c),   # manager +0x6ba54c
]
for start, n, skip in gates:
    for major in (0, 1):
        reset(major)
        frame = u.reg_read(UC_X86_REG_EBP)
        obj = ARENA + 0x4000                       # [ebp-0x20] the bullet, [ebp-0x6c] the manager
        map_region(obj + 0x6ba000, 0x1000)
        wi(frame - 0x20, obj); wi(frame - 0x6c, obj)
        wi(obj + 0xda8, 7); wi(obj + 0xdb8, 0x00050001)   # +0xdba is the high word: 5
        wi(obj + 0x6ba53c, 7); wi(obj + 0x6ba54c, 7)
        before = [u.reg_read(r) for r in regs]; flags = u.reg_read(UC_X86_REG_EFLAGS)
        pc = run(start, start + n, skip)
        # `skip` is start+n for every one of these: the block is gated, not branched around
        assert pc == skip and u.reg_read(UC_X86_REG_ESP) == STACK
        values = (ri(obj + 0xda8), ri(obj + 0xdb8) >> 16, ri(obj + 0x6ba53c), ri(obj + 0x6ba54c))
        if major:
            assert values != (7, 5, 7, 7), (hex(start), values)      # the block ran
        else:
            assert values == (7, 5, 7, 7), (hex(start), values)      # and here it did not
            assert before == [u.reg_read(r) for r in regs]
            assert flags == u.reg_read(UC_X86_REG_EFLAGS)
print('PASS: 10 counter-gate paths; the skipped path keeps flags, registers and stack')

# ---- the laser's graze-every-20-frames flag: only on the tick its timer reached the frame.
for current, previous, want in ((40, 39, 1), (40, 40, 0), (41, 40, 0), (41, 41, 0)):
    reset()
    frame = u.reg_read(UC_X86_REG_EBP); laser = ARENA + 0x5000
    wi(frame - 0x28, laser); wi(laser + 0x588, previous); wi(laser + 0x590, current)
    u.reg_write(UC_X86_REG_EAX, current)             # what the timer read returned
    run(0x431f0c, 0x431f19)
    assert u.reg_read(UC_X86_REG_EDX) == want, (current, previous, u.reg_read(UC_X86_REG_EDX))
    assert u.reg_read(UC_X86_REG_ESP) == STACK
print('PASS: laser graze interval fires once per frame, on the tick the timer arrived')

# ---- "animation finished" is withheld between frame ticks. ExecuteScript itself is replaced
# by `mov eax,1; ret 4` so that only the stub is under test.
u.mem_write(0x45ea00, b'\xb8\x01\x00\x00\x00\xc2\x04\x00')
for site in (0x4317f3, 0x431904, 0x431a16, 0x431ad8):
    for major in (0, 1):
        reset(major)
        u.reg_write(UC_X86_REG_ESP, STACK - 4); wi(STACK - 4, ARENA + 0x6000)   # the VM argument, already pushed
        run(site, site + 5)
        assert u.reg_read(UC_X86_REG_EAX) == major
        assert u.reg_read(UC_X86_REG_ESP) == STACK                              # the callee's ret 4 honoured
print('PASS: a spawn or death animation finishing is acted on at the frame tick only')

# ---- the live state's behaviour block.
reset(0)
run(0x431322, 0x43146f, 0x42ffc0)
assert u.reg_read(UC_X86_REG_EIP) == 0x43146f and not called
for major in (1,):
    reset(major); called.clear()
    frame = u.reg_read(UC_X86_REG_EBP); bullet = ARENA + 0x7000
    wi(frame - 0x20, bullet)
    run(0x431322, 0x42ffc0)
    assert len(called) == 1 and called[0][1] == bullet         # th08_behaviours_enter(bullet)
    assert u.reg_read(UC_X86_REG_ECX) == bullet                 # then the displaced `mov ecx,[ebp-0x20]; call`
    ret = ri(u.reg_read(UC_X86_REG_ESP))
    assert meta['stub_base'] <= ret < meta['stub_base'] + 0x10000
    u.reg_write(UC_X86_REG_ESP, u.reg_read(UC_X86_REG_ESP) + 4)  # return from the scheduler
    run(ret, 0x43132a)
    assert u.reg_read(UC_X86_REG_ESP) == STACK
for grace, zero in ((0, 1), (3, 0)):
    reset(); called.clear()
    frame = u.reg_read(UC_X86_REG_EBP); bullet = ARENA + 0x7000
    wi(frame - 0x20, bullet); wi(bullet + 0xda8, grace)
    run(0x43146f, 0x431479)
    assert len(called) == 1 and called[0][1] == bullet         # th08_behaviours_leave(bullet)
    assert u.reg_read(UC_X86_REG_EDX) == bullet and u.reg_read(UC_X86_REG_ESP) == STACK
    assert ((u.reg_read(UC_X86_REG_EFLAGS) >> 6) & 1) == zero   # the displaced cmp's ZF reaches the je
print('PASS: behaviours are skipped between frames and bracketed by enter/leave on them')

# ---- the player's movement reads its own factor, not the engine's multiplier.
for site in (0x44ba6a, 0x44ba7c):
    code = bytes(u.mem_read(site, 6))
    assert code[:2] == b'\xd8\x0d' and struct.unpack('<I', code[2:])[0] != 0x17ce8e0
print('PASS: both player movement multiplies read the sliced factor')

"""TH10 native clock/FPS regression. Requires Unicorn; uses the installed fixture.

Execute the game's real FPS calculation and both clock backends with deterministic
Win32 clock imports. Restore the original watchdog branch for the negative control.
No game is launched, and no device or Windows timer setting is changed.
"""
from x86_test_support import *
import math

FPS = ARENA + 0x8000
CTX = 0x4924f0
FREQUENCY = 10_000_000
QPC_ORIGIN = 100_000_000
UPTIME_SECONDS = 3600
CLOCK_RESULT = BOOT + 0x200
SHIMS = BOOT + 0x400


def wd(addr, value):
    u.mem_write(addr, struct.pack('<d', value))


def rd(addr):
    return struct.unpack('<d', u.mem_read(addr, 8))[0]


# Native clock imports: critical sections, QPC, multimedia timer APIs.
imports = {
    0x4660b0: 'enter', 0x4660b4: 'leave', 0x4660fc: 'qpc',
    0x466278: 'begin', 0x466274: 'end', 0x466270: 'time',
}
shims = {}
for index, (iat, name) in enumerate(imports.items()):
    address = SHIMS + index * 16
    wi(iat, address)
    shims[address] = name
    u.mem_write(address, b'\xc3' if name == 'time' else b'\xc2\x04\x00')

wall = 0.0


def clock_import(uc, address, size, unused):
    name = shims.get(address)
    if name == 'qpc':
        out = ri(u.reg_read(UC_X86_REG_ESP) + 4)
        u.mem_write(out, struct.pack('<Q', QPC_ORIGIN + round(wall * FREQUENCY)))
        u.reg_write(UC_X86_REG_EAX, 1)
    elif name == 'time':
        u.reg_write(UC_X86_REG_EAX, round((UPTIME_SECONDS + wall) * 1000))


u.hook_add(UC_HOOK_CODE, clock_import, begin=SHIMS, end=SHIMS + 0x100)


# Separate fixed entry points avoid self-modifying code in the per-frame loop.
entries = {}
for index, address in enumerate((0x439540, 0x4134b0)):
    entry = BOOT + 0x500 + index * 0x20
    code = b'\xe8' + struct.pack('<i', address - (entry + 5))
    if index == 0:
        code += b'\xdd\x1d' + struct.pack('<I', CLOCK_RESULT)
    u.mem_write(entry, code)
    entries[address] = (entry, entry + len(code))


def native_call(address, clock=False):
    # A real CALL is necessary: the FPS function realigns ESP before saving EBP.
    u.reg_write(UC_X86_REG_ESP, STACK)
    u.reg_write(UC_X86_REG_ESI, FPS)
    run(entries[address][0], entries[address][1])
    assert u.reg_read(UC_X86_REG_ESP) == STACK
    assert u.reg_read(UC_X86_REG_FPTAG) == 0xffff, (hex(address), wall, hex(u.reg_read(UC_X86_REG_FPTAG)))
    return rd(CLOCK_RESULT) if clock else None


installed_branch = bytes(u.mem_read(0x413508, 2))
assert installed_branch == b'\xeb\x5b', 'installed patch must bypass the clock watchdog'


def simulate(rate, patched, seconds=8):
    global wall
    reset()
    u.mem_write(0x413508, installed_branch if patched else b'\x75\x5b')
    # Unicorn caches translated code, so invalidate it when changing the branch.
    u.ctl_remove_cache(0x4134b0, 0x4135c8)
    u.mem_write(0x492508, struct.pack('<Q', FREQUENCY))
    u.mem_write(0x492510, struct.pack('<Q', QPC_ORIGIN))
    for addr in (0x492540, CTX + 0x38, CTX + 0x40, CTX + 0x48):
        wd(addr, 0)
    wi(0x477810, ARENA + 0x9000)
    wd(FPS + 0x14, 0)
    zero_fps = 0
    backwards = 0
    largest_jump = 0
    previous = 0
    for frame in range(1, rate * seconds + 1):
        wall = frame / rate
        now = native_call(0x439540, clock=True)
        if now < previous - 0.001:
            backwards += 1
        largest_jump = max(largest_jump, now - previous)
        previous = now
        native_call(0x4134b0)
        # The real draw callback increments this after calculating/displaying FPS.
        wi(FPS + 0x20, ri(FPS + 0x20) + 1)
        if wall > 3 and 0 <= rf(FPS + 0x34) < 0.05:
            zero_fps += 1
    return {
        'qpc_enabled': ri(0x492508) == FREQUENCY,
        'backwards': backwards,
        'largest_jump_s': largest_jump,
        'zero_fps_frames': zero_fps,
        'fps': rf(FPS + 0x34),
        'watchdog_count': ri(FPS + 0x1c),
        'accounting': (rd(FPS + 0x24), rd(FPS + 0x2c)),
    }


baseline = simulate(360, False)
assert not baseline['qpc_enabled'], baseline
assert baseline['backwards'] > 0, baseline
assert baseline['largest_jump_s'] > UPTIME_SECONDS - 60, baseline
assert baseline['zero_fps_frames'] > 0, baseline
print('PASS: unpatched 360 FPS reproduces clock fallback, jumps and 0.0 FPS:', baseline, flush=True)

for rate in (60, 64, 65, 66, 120, 144, 240, 360, 1000):
    result = simulate(rate, True)
    assert result['qpc_enabled'] and result['watchdog_count'] == 0, (rate, result)
    assert result['backwards'] == 0 and result['zero_fps_frames'] == 0, (rate, result)
    assert result['largest_jump_s'] < 1.01 / rate, (rate, result)
    assert math.isclose(result['fps'], rate, rel_tol=0.035), (rate, result)
    if rate == 60:
        original = simulate(rate, False)
        assert original == result, (original, result)
print('PASS: 60..1000 FPS keeps QPC and monotonic time; stock FPS/slowdown accounting unchanged')

# Execute the retained native deadline loop, without running the game's renderer.
# A watchdog clock jump asks it to add 1/60 second once per missed frame. One hour
# of machine uptime produces 216,000 iterations before any update or draw can run.
reset()
wd(CTX + 0x48, 2.0)
wd(CTX + 0x38, 2.0 + UPTIME_SECONDS)
u.reg_write(UC_X86_REG_EBP, CTX)
loop_iterations = 0


def count_loop(uc, address, size, unused):
    global loop_iterations
    loop_iterations += 1


counter = u.hook_add(UC_HOOK_CODE, count_loop, begin=0x4393d0, end=0x4393d0)
u.emu_start(0x4393d0, 0x4393e9, count=3_000_000)
u.hook_del(counter)
assert u.reg_read(UC_X86_REG_EIP) == 0x4393e9
assert abs(loop_iterations - 60 * UPTIME_SECONDS) <= 1, loop_iterations
assert u.reg_read(UC_X86_REG_FPTAG) == 0xffff
print(f'PASS: native pre-draw loop executes {loop_iterations:,} iterations for the clock jump')

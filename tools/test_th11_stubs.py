"""TH11 adapter machine-code regression tests. Requires Unicorn."""
from x86_test_support import *

# Both branches of every ordinary gate, with stack/register/flag preservation
# checked on the skipped path. Execute only the displaced prefix on the other.
gates=[
 (0x408cbc,5,0x408cd8,UC_X86_REG_EBX,0x464),
 (0x402bc5,10,0x40311b,UC_X86_REG_EBX,-1),
 (0x403121,6,0x403127,UC_X86_REG_EBX,-1),
 (0x425de0,6,0x425df1,UC_X86_REG_EBP,0x14),
 (0x426097,9,0x4260f6,UC_X86_REG_EBP,0x28),
 (0x427580,9,0x4275ec,UC_X86_REG_ESI,0x14),
 (0x4312c6,7,0x4313e6,UC_X86_REG_ESI,0x944),
 (0x431ab6,6,0x431af7,UC_X86_REG_ESI,0x944),
 (0x43065f,6,0x430665,UC_X86_REG_EBP,-1),
 (0x43078f,6,0x4307ab,UC_X86_REG_EBP,-1),
 (0x430b48,6,0x430b4e,UC_X86_REG_EBP,-1),
 (0x430b4e,6,0x430d91,UC_X86_REG_EBP,-1),
]
for start,n,skip,reg,timer in gates:
    for changed in (0,1):
        reset(changed)
        if timer>=0:
            obj=u.reg_read(reg);wi(obj+timer,5);wi(obj+timer+4,5+changed)
        before=[u.reg_read(r) for r in regs];flags=u.reg_read(UC_X86_REG_EFLAGS)
        pc=run(start,start+n,skip)
        assert pc==(start+n if changed else skip)
        assert u.reg_read(UC_X86_REG_ESP)==STACK
        if not changed:
            assert before==[u.reg_read(r) for r in regs]
            assert flags==u.reg_read(UC_X86_REG_EFLAGS)
print('PASS: 24 guard paths, including preserved flags/registers/stack')

for start,n,reg,timer in ((0x408070,7,UC_X86_REG_ECX,0x5c),(0x452420,9,UC_X86_REG_ECX,0x5c),
                           (0x434e30,6,UC_X86_REG_EDX,0),(0x4352a0,6,UC_X86_REG_EDX,0)):
    for changed in (0,1):
        reset();obj=u.reg_read(reg);wi(obj+timer,5);wi(obj+timer+4,5+changed)
        wi(STACK,BOOT+128)
        pc=run(start,start+n,BOOT+128)
        assert pc==(start+n if changed else BOOT+128)
        if not changed:
            assert u.reg_read(UC_X86_REG_EAX)==0 and u.reg_read(UC_X86_REG_ESP)==STACK+4
print('PASS: mesh and shot callbacks return only when their integer timer is unchanged')

# State-5 items: preserve all five pending x87 pops and signed expiry branch.
for major,count,target in ((0,0,0x423ec3),(1,0,0x4235c6),(1,2,0x423ec3)):
    reset(major);obj=u.reg_read(UC_X86_REG_EDI);wi(obj+0x474,count)
    for i in range(5):fpush(i+1)
    pc=run(0x4235af,0x423ec3,0x4235c6)
    assert pc==target and ri(obj+0x474)==(count-major)&0xffffffff
    assert u.reg_read(UC_X86_REG_FPTAG)==0xffff
print('PASS: item countdown expires once, with a balanced x87 stack')

reset();wi(STACK+0x2c,640);wi(STACK+0x10,-320)
run(0x43066e,0x4306ce)
assert ri(STACK+0x2c)==1280 and ri(STACK+0x10)==(-640)&0xffffffff
assert u.reg_read(UC_X86_REG_ESP)==STACK
print('PASS: Reimu C speed bonus applies on minor ticks without spawning extra particles')

reset();obj=u.reg_read(UC_X86_REG_EBX)
for i,value in enumerate((10,20,30,4,-8,12)):wf(obj+i*4,value)
run(0x45959c,0x4595b7)
assert [rf(obj+i*4) for i in range(3)]==[11,18,33]
assert u.reg_read(UC_X86_REG_FPTAG)==0xffff
for start,stop,reg in ((0x4315a5,0x4315ac,UC_X86_REG_EDI),(0x434562,0x434569,UC_X86_REG_ESI)):
    reset();obj=u.reg_read(reg);wf(obj+0x18,4);wf(obj+0x14,3)
    run(start,stop)
    # Execute displaced value's following store to inspect the x87 result.
    run(stop,stop+3);assert rf(obj+0x14)==4
    assert u.reg_read(UC_X86_REG_ESP)==STACK-4
for start,reg in ((0x4315af,UC_X86_REG_EDI),(0x43456c,UC_X86_REG_ESI)):
    reset();obj=u.reg_read(reg);wf(obj+0xc,4);wf(obj+0x10,3)
    run(start,start+6)
    u.mem_write(BOOT,b'\xd9\x1d'+struct.pack('<I',BOOT+64));ends=set();u.emu_start(BOOT,BOOT+6)
    assert rf(BOOT+64)==4
print('PASS: Cartesian motion, acceleration and angular increments scale by the sub-step')

# Exercise the actual game's ftol as well as our remainder-carry stub.
for sse in (0,1):
    for value in (3.75,-3.75,0.25,-0.25):
        reset();wi(0x4c9ccc,sse)
        total=0
        for _ in range(4):
            fpush(value);run(0x430722,0x430727)
            result=u.reg_read(UC_X86_REG_EAX);total+=result if result<2**31 else result-2**32
            assert u.reg_read(UC_X86_REG_ESP)==STACK
            assert u.reg_read(UC_X86_REG_FPTAG)==0xffff
        assert total==int(value*4),(sse,value,total)
        reset();wf(meta['factor'],1);wf(meta['residual'],0.75);wi(0x4c9ccc,sse)
        fpush(value);run(0x430722,0x430727)
        assert u.reg_read(UC_X86_REG_EAX)==int(value)&0xffffffff
print('PASS: positive/negative fixed-point residuals, SSE/x87 ftol paths, and stock truncation')

# Constant timer offsets must not be scaled by dt; this call uses ret 4.
reset();obj=u.reg_read(UC_X86_REG_ESI);wi(obj+4,17);wf(obj+8,17.25)
wf(STACK,-14);run(0x4343fc,0x434401)
assert rf(obj+8)==3.25 and ri(obj+4)==3 and ri(obj)==17
assert u.reg_read(UC_X86_REG_ESP)==STACK+4
print('PASS: shot-cycle constant subtraction and callee stack cleanup')

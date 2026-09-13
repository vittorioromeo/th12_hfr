"""TH12 adapter machine-code regression tests. Requires Unicorn."""
from x86_test_support import *

for start,n,skip,reg,prev,cur in (
    (0x409fdb,5,0x409ff7,UC_X86_REG_EBP,0x4e4,0x4e8),
    (0x4374fc,6,0x43753d,UC_X86_REG_EDI,0xa30,0xa34),
    (0x42979a,6,0x4297ab,UC_X86_REG_EDI,0x14,0x18),
    (0x42c90c,6,0x42c91d,UC_X86_REG_ESI,0x14,0x18),
    (0x42adef,6,0x42ae00,UC_X86_REG_EDI,0x14,0x18),
    (0x429a55,9,0x429a76,UC_X86_REG_EDI,0x28,0x2c),
    (0x42cbf7,9,0x42cc1d,UC_X86_REG_ESI,0x28,0x2c),
    (0x42b068,9,0x42b089,UC_X86_REG_EDI,0x14,0x18),
):
    for changed in (0,1):
        reset();obj=u.reg_read(reg);wi(obj+prev,5);wi(obj+cur,5+changed)
        assert run(start,start+n,skip)==(start+n if changed else skip)
        assert u.reg_read(UC_X86_REG_ESP)==STACK
for changed in (0,1):
    reset();obj=u.reg_read(UC_X86_REG_EDI);wi(obj+0xa30,3-changed);wi(obj+0xa34,3)
    assert run(0x436dd9,0x436de0,0x436e91)==(0x436de0 if changed else 0x436e91)
    reset();obj=u.reg_read(UC_X86_REG_EDI);wi(obj+0xa30,5);wi(obj+0xa34,5+changed);wi(obj+0xc418,10)
    run(0x4368f7,0x4368fd);assert ri(obj+0xc418)==10+changed
    reset(changed);obj=u.reg_read(UC_X86_REG_EBX);wi(obj+0x35dc,1)
    assert run(0x403145,0x4036dd,0x403155)==(0x403155 if changed else 0x4036dd)
    wi(obj+0x35d8,10);run(0x4036dd,0x4036e8);assert ri(obj+0x35d8)==10+changed
    reset(changed);obj=u.reg_read(UC_X86_REG_EDI);wi(obj+0x9c0,1)
    assert run(0x425c5c,0x425c63,0x426f53)==(0x425c63 if changed else 0x426f53)
    assert ri(obj+0x9c0)==1-changed
    reset();obj=u.reg_read(UC_X86_REG_ECX);wi(obj+0x68,5);wi(obj+0x6c,5+changed);wi(STACK,BOOT+128)
    assert run(0x45dcd0,0x45dcd9,BOOT+128)==(0x45dcd9 if changed else BOOT+128)
print('PASS: TH12 bullet/player/laser/stage/item/mesh gate branches and counters')

reset();obj=u.reg_read(UC_X86_REG_EBX)
for i,value in enumerate((10,20,30,4,-8,12)):wf(obj+i*4,value)
run(0x464dbc,0x464dd7)
assert [rf(obj+i*4) for i in range(3)]==[11,18,33]
assert u.reg_read(UC_X86_REG_FPTAG)==0xffff
for start,reg in ((0x436fe2,UC_X86_REG_ESI),(0x439b72,UC_X86_REG_EDI)):
    reset();obj=u.reg_read(reg);wf(obj+0x18,4);wf(obj+0x14,3)
    run(start,start+10);assert rf(obj+0x14)==4
    assert u.reg_read(UC_X86_REG_ESP)==STACK-4
reset();obj=u.reg_read(UC_X86_REG_ESI);dest=ARENA+0x8000
wf(obj-0x20,4);wf(obj-0x18,-8);wf(obj-0x1c,20);wi(STACK+0x10,dest);wf(dest,10)
run(0x437016,0x43702a);assert rf(dest)==11 and rf(obj-0x1c)==18
print('PASS: TH12 motion, shot acceleration and displacement scale correctly')

for sse in (0,1):
    for value in (3.75,-3.75,0.25,-0.25):
        for site in (0x4367ca,0x4367dd):
            reset();wi(0x4d52dc,sse);total=0
            for _ in range(4):
                fpush(value);run(site,site+5);result=u.reg_read(UC_X86_REG_EAX)
                total+=result if result<2**31 else result-2**32
                assert u.reg_read(UC_X86_REG_ESP)==STACK and u.reg_read(UC_X86_REG_FPTAG)==0xffff
            assert total==int(value*4)
            reset();wf(meta['factor'],1);wf(meta['residual'],0.75);wi(0x4d52dc,sse)
            fpush(value);run(site,site+5);assert u.reg_read(UC_X86_REG_EAX)==int(value)&0xffffffff
reset();obj=u.reg_read(UC_X86_REG_ESI);wi(obj+4,17);wf(obj+8,17.25);wf(STACK,-14)
run(0x439ac2,0x439ac7)
assert rf(obj+8)==3.25 and ri(obj+4)==3 and ri(obj)==17 and u.reg_read(UC_X86_REG_ESP)==STACK+4
print('PASS: TH12 residuals on both axes, native SSE/x87 truncation and constant Timer::add')

# LaserCurve (ESI = laser): the node ring shifts only when the laser's timer +0x28/+0x2c
# changed, and the head node moves by velocity * factor (factor is 0.25 in the fixture).
for changed in (0,1):
    reset();obj=u.reg_read(UC_X86_REG_ESI);ring=ARENA+0x9000
    wi(obj+0xf9c,ring);wi(obj+0x470,3);wi(obj+0x28,5);wi(obj+0x2c,5+changed)
    for i in range(3):
        for j in range(5):wf(ring+i*20+j*4,float(i*10+j))
    assert run(0x42c925,0x42c965)==0x42c965
    assert u.reg_read(UC_X86_REG_EDX)==ring and u.reg_read(UC_X86_REG_ESP)==STACK
    # node 2 takes node 1's values only when a whole frame passed; node 0 never shifts
    assert rf(ring+40)==(10.0 if changed else 20.0) and rf(ring+20)==(0.0 if changed else 10.0) and rf(ring)==0.0
    wf(obj+0x5c,8);wf(obj+0x60,-4);wf(obj+0x64,16);wf(ring,100);wf(ring+4,200);wf(ring+8,300)
    run(0x42c965,0x42c97e)
    assert [rf(ring+i*4) for i in range(3)]==[102,199,304] and u.reg_read(UC_X86_REG_FPTAG)==0xffff
# Player shot behaviours (EDX = shot): skipped, returning 0, unless the shot's timer +0/+4 changed.
for start,n in ((0x43a480,6),(0x43a810,8),(0x43aa50,9),(0x43ab60,6)):
    for changed in (0,1):
        reset();obj=u.reg_read(UC_X86_REG_EDX);wi(obj,7);wi(obj+4,7+changed);wi(STACK,BOOT+128)
        u.reg_write(UC_X86_REG_EAX,0x1234)
        assert run(start,start+n,BOOT+128)==(start+n if changed else BOOT+128)
        if not changed:assert u.reg_read(UC_X86_REG_EAX)==0 and u.reg_read(UC_X86_REG_ESP)==STACK+4
# The option laser grows by 28 * factor per call.
reset();obj=u.reg_read(UC_X86_REG_ESI);wf(obj+0x6c,100);fpush(100)
run(0x43a750,0x43a759);assert rf(obj+0x6c)==107 and u.reg_read(UC_X86_REG_FPTAG)==0xffff
print('PASS: TH12 curved laser ring/head, shot behaviour gates and option laser growth')

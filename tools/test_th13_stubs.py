"""TH13 adapter machine-code regression tests. Requires Unicorn."""
from x86_test_support import *

# Player shot behaviours (EDX = shot): skipped, returning 0, unless the shot's timer +0x18/+0x1c changed.
for start,n in ((0x446cb0,6),(0x447590,6),(0x447510,6)):
    for changed in (0,1):
        reset();obj=u.reg_read(UC_X86_REG_EDX);wi(obj+0x18,7);wi(obj+0x1c,7+changed);wi(STACK,BOOT+128)
        u.reg_write(UC_X86_REG_EAX,0x1234)
        assert run(start,start+n,BOOT+128)==(start+n if changed else BOOT+128)
        if not changed:assert u.reg_read(UC_X86_REG_EAX)==0 and u.reg_read(UC_X86_REG_ESP)==STACK+4
# 0x447590 / 0x447510 test the shot's state right after the copied prologue: the flags must survive the gate.
for start,cont in ((0x447590,0x447598),(0x447510,0x447518)):
    for state in (1,2):
        reset();obj=u.reg_read(UC_X86_REG_EDX);wi(obj+0x18,7);wi(obj+0x1c,8);wi(obj+0x70,state);wf(obj+0x44,10)
        end=run(start,cont,start+0x14)
        assert end==(cont if state==1 else start+0x14)
print('PASS: TH13 shot behaviour gates')

"""Execute TH18's emitted bridge with Unicorn; never execute the game or DLL."""
from x86_test_support import *
from unicorn import UC_HOOK_MEM_FETCH_UNMAPPED

seen=[]
def c_call(uc, access, address, size, value, unused):
    page=address & ~4095
    uc.mem_map(page,4096);mapped.add(page)
    # Stand-in for fastcall(manager, unused, vm, vertices): pop two stack args.
    uc.mem_write(address,b'\xb8\x78\x56\x34\x12\xc2\x08\x00')
    seen.append((uc.reg_read(UC_X86_REG_ECX),ri(uc.reg_read(UC_X86_REG_ESP)+4),ri(uc.reg_read(UC_X86_REG_ESP)+8)))
    return True
u.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED,c_call)
reset()
manager,vm,vertices=ARENA+0x2000,ARENA+0x3000,ARENA+0x4000
u.reg_write(UC_X86_REG_ECX,manager);u.reg_write(UC_X86_REG_EDI,vm);wi(STACK,vertices)
run(0x47e6b5,0x47e6ba)
assert seen==[(manager,vm,vertices)],seen
assert u.reg_read(UC_X86_REG_ESP)==STACK+4
assert u.reg_read(UC_X86_REG_EDI)==vm
assert u.reg_read(UC_X86_REG_EAX)==0x12345678
reset();before=[u.reg_read(r) for r in regs];flags=u.reg_read(UC_X86_REG_EFLAGS)
run(0x471a9e,0x471c37)
assert before==[u.reg_read(r) for r in regs]
assert u.reg_read(UC_X86_REG_EFLAGS)==flags
assert u.reg_read(UC_X86_REG_ESP)==STACK
assert bytes(u.mem_read(0x4730be,7))==bytes.fromhex('80 3d 11 d0 4c 00 7f')
# Native runner entry/return stay untouched: no replacement steals thprac's return hook.
assert bytes(u.mem_read(0x4012e0,4))==bytes.fromhex('55 8b ec 51')
assert bytes(u.mem_read(0x4013f5,1))==b'\xc3'
print('PASS: TH18 quad bridge arguments/stack/return; all latency modes routed; native runner preserved')

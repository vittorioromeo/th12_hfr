"""Emulate the actual x86 stubs emitted by test_th11.c. Requires unicorn.
Usage: python tools/test_th11_stubs.py build/native11
The .game/.stubs/.json files are local test artifacts, never release contents.
"""
import json
import struct
import sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

prefix=sys.argv[1]
meta=json.loads(Path(prefix+'.json').read_text())
u=Uc(UC_ARCH_X86,UC_MODE_32)
mapped=set()
def map_region(address,size):
    for page in range(address&~4095,(address+size+4095)&~4095,4096):
        if page not in mapped:
            u.mem_map(page,4096);mapped.add(page)
def wi(address,value):u.mem_write(address,struct.pack('<I',value&0xffffffff))
def ri(address):return struct.unpack('<I',u.mem_read(address,4))[0]
def wf(address,value):u.mem_write(address,struct.pack('<f',value))
def rf(address):return struct.unpack('<f',u.mem_read(address,4))[0]
for name,base in (('game',0x400000),('stubs',meta['stub_base'])):
    data=Path(prefix+'.'+name).read_bytes();map_region(base,len(data));u.mem_write(base,data)
for name,address in meta.items():
    if name!='stub_base':map_region(address,8)
ARENA=0x20000000;STACK=ARENA+0xf0000;BOOT=ARENA+0xe0000
map_region(ARENA,0x100000)
regs=[UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP]
def reset(major=0):
    u.mem_write(ARENA,bytes(0x10000));u.reg_write(UC_X86_REG_ESP,STACK)
    for i,reg in enumerate(regs):u.reg_write(reg,ARENA+0x1000+i*0x100)
    u.reg_write(UC_X86_REG_EFLAGS,0x247)
    wi(meta['major'],major);wf(meta['factor'],0.25);wf(meta['logical'],1)
    wf(meta['residual'],0);wf(meta['residual']+4,0)
    u.mem_write(BOOT,b'\xdb\xe3');u.emu_start(BOOT,BOOT+2)
def fpush(value):
    wf(BOOT+64,value);u.mem_write(BOOT,b'\xd9\x05'+struct.pack('<I',BOOT+64))
    u.emu_start(BOOT,BOOT+6)
ends=set()
def on_code(uc,address,size,unused):
    if address in ends:uc.emu_stop()
u.hook_add(UC_HOOK_CODE,on_code)
def run(start,*stop):
    global ends
    ends=set(stop);u.emu_start(start,0,count=2000)
    pc=u.reg_read(UC_X86_REG_EIP)
    assert pc in ends,f'Unexpected endpoint {pc:x}, wanted {[hex(x) for x in ends]}'
    return pc

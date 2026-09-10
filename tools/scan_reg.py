import pefile, capstone, sys
pe=pefile.PE('th12.exe')
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
code=text.get_data(); base=0x400000+text.VirtualAddress
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.detail=True
insns=list(md.disasm(code, base))
idx={i.address:n for n,i in enumerate(insns)}
targets={0x462380:'UPDATE',0x462420:'DRAW'}
for n,i in enumerate(insns):
    if i.mnemonic=='call' and i.op_str.startswith('0x') and int(i.op_str,16) in targets:
        kind=targets[int(i.op_str,16)]
        prio=None; func=None; fn=None
        for j in range(n-1, max(0,n-60), -1):
            p=insns[j]
            if prio is None and p.mnemonic=='mov' and p.op_str.startswith('ebx, 0x'): prio=int(p.op_str[5:],16)
            if prio is None and p.mnemonic=='mov' and p.op_str.startswith('ebx, '): prio=p.op_str
            if func is None and p.mnemonic=='mov' and '+ 8], 0x' in p.op_str: func=int(p.op_str.split(', ')[1],16)
            pass
        print(f"{i.address:08x} {kind:6s} prio={prio if isinstance(prio,str) else (hex(prio) if prio is not None else None):>10} func={hex(func) if func else None}")

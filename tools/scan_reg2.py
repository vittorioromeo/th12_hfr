# lists every UpdateFunc/DrawFunc registration in th12.exe with its priority and callback (see DEVNOTES.md 3.2)
import pefile, capstone, struct
pe=pefile.PE('th12.exe')
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
code=text.get_data(); base=0x400000+text.VirtualAddress
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
targets={0x462380:'UPDATE',0x462420:'DRAW'}
rows=[]
for off in range(len(code)-5):
    if code[off]==0xE8:
        rel=struct.unpack_from('<i',code,off+1)[0]
        tgt=base+off+5+rel
        if tgt in targets:
            # disassemble a window before
            start=max(0,off-80)
            best=None
            # try several start offsets to sync
            for s in range(start, off):
                ins=list(md.disasm(code[s:off+5], base+s))
                if ins and ins[-1].address==base+off:
                    best=ins; break
            prio=None; func=None
            if best:
                for p in reversed(best[:-1]):
                    if prio is None and p.mnemonic=='mov' and p.op_str.startswith('ebx, '): prio=p.op_str[5:]
                    if func is None and p.mnemonic=='mov' and '+ 8], 0x' in p.op_str: func=p.op_str.split(', ')[1]
            rows.append((targets[tgt], base+off, prio, func))
for k,a,p,f in sorted(rows, key=lambda r:(r[0], int(r[2],16) if r[2] and r[2].startswith('0x') else (int(r[2]) if r[2] and r[2].isdigit() else 9999))):
    print(f"{k:6s} at {a:08x} prio={p} func={f}")

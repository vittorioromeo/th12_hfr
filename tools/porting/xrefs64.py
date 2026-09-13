"""Find references to an address in an AMD64 PE by decoding from real function starts.

inspect_pe.py's `xrefs` decodes executable sections linearly from the section start. That
misaligns wherever data or padding sits between functions, so it both invents references
(TH06NC_DEVNOTES §17: three convincing "replay" functions that nothing calls) and misses
real ones. This decodes each .pdata function from its own BeginAddress, which is always an
instruction boundary, and reports whether each hit is a read or a write.

Usage: python3 xrefs64.py <exe> <rva> [<rva> ...]
"""
import sys
import pefile
import capstone

WRITTEN_FIRST = {
    "mov", "movss", "movsd", "movaps", "movups", "movd", "movq", "inc", "dec", "add", "sub",
    "or", "and", "xor", "bts", "btr", "sete", "setne", "imul", "sal", "shr", "adc", "sbb",
}

def main() -> int:
    pe = pefile.PE(sys.argv[1])
    base = pe.OPTIONAL_HEADER.ImageBase
    wanted = {int(a, 16) for a in sys.argv[2:]}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    funcs = sorted({(e.struct.BeginAddress, e.struct.EndAddress)
                    for e in pe.DIRECTORY_ENTRY_EXCEPTION})
    hits = 0
    for begin, end in funcs:
        if end <= begin or end - begin > 0x20000:
            continue
        try:
            code = pe.get_data(begin, end - begin)
        except Exception:
            continue
        for ins in md.disasm(code, base + begin):
            for index, op in enumerate(ins.operands):
                if op.type != capstone.x86.X86_OP_MEM or op.mem.base != capstone.x86.X86_REG_RIP:
                    continue
                target = (ins.address + ins.size + op.mem.disp) - base
                if target not in wanted:
                    continue
                kind = "write" if index == 0 and ins.mnemonic in WRITTEN_FIRST else "read"
                print(f"{target:#x} {kind:5} at {ins.address - base:#x} "
                      f"in function {begin:#x}: {ins.mnemonic} {ins.op_str}")
                hits += 1
    if not hits:
        print("no references from any .pdata function (the target may be reached only "
              "through a register, as a field of an object)")
    return 0

if __name__ == "__main__":
    sys.exit(main())

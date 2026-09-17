"""Match a known function in one Touhou build to its counterpart in another.

Usage: set PATHS, then `python tools/match_functions.py 0x470af0` to rank candidates.

Same source, different compiler build. Immediates and absolute displacements move, so they
are masked; mnemonics, operand shapes and small struct offsets are kept, because those are
what the source decides. Function starts are approximated by call targets, which is enough:
every function we care about is called from somewhere.
"""
import pefile, capstone, re, sys, collections

PATHS = {}   # tag -> executable path; set these to your own copies before use
_cache = {}

def load(tag):
    if tag in _cache: return _cache[tag]
    pe = pefile.PE(PATHS[tag], fast_load=True)
    d = pe.get_memory_mapped_image(); ib = pe.OPTIONAL_HEADER.ImageBase
    sec = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    lo, hi = sec.VirtualAddress, sec.VirtualAddress + sec.Misc_VirtualSize
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    # function starts: every direct call target inside .text
    starts = set()
    for a in range(lo, hi - 5):
        if d[a] == 0xe8:
            rel = int.from_bytes(d[a+1:a+5], 'little', signed=True)
            t = a + 5 + rel
            if lo <= t < hi: starts.add(t)
    _cache[tag] = (d, ib, lo, hi, md, starts)
    return _cache[tag]

NUM = re.compile(r'0x[0-9a-f]+')
def norm_op(s):
    # keep small structure offsets (they are source-level), mask anything large (addresses)
    def repl(m):
        v = int(m.group(0), 16)
        return m.group(0) if v < 0x1000 else '@'
    return NUM.sub(repl, s)

def shape(tag, addr, count=200, depth=0):
    """Normalised token sequence for a function.

    These update callbacks are thin thunks that end in a jmp to the real body, so a shape that
    stops at the first jmp compares wrappers with wrappers and says nothing -- three different
    TH14 callbacks scored 1.00 against TH13's Stage that way. Follow one tail jump."""
    d, ib, lo, hi, md, _ = load(tag)
    off = addr - ib
    out = []
    for i in md.disasm(d[off:off+count*8], addr):
        if i.mnemonic == 'jmp' and i.op_str.startswith('0x') and depth < 2:
            t = int(i.op_str, 16)
            if lo <= t - ib < hi and not (addr <= t < addr + len(out)*8 + 64):
                return out + shape(tag, t, count - len(out), depth + 1)
        out.append(i.mnemonic + ' ' + norm_op(i.op_str))
        if i.mnemonic == 'ret' and len(out) > 4: break
        if len(out) >= count: break
    return out

def score(a, b):
    """longest common subsequence ratio of two token lists"""
    import difflib
    return difflib.SequenceMatcher(None, a, b, autojunk=False).ratio()

def candidates(tag):
    d, ib, lo, hi, md, starts = load(tag)
    return sorted(ib + s for s in starts)

def best(src_tag, src_addr, dst_tag, topn=3, count=80):
    s = shape(src_tag, src_addr, count)
    if len(s) < 5: return []
    # cheap prefilter: same first mnemonic set
    scored = []
    for c in candidates(dst_tag):
        t = shape(dst_tag, c, count)
        if len(t) < 5: continue
        if abs(len(t) - len(s)) > max(12, len(s)*0.6): continue
        r = score(s, t)
        if r > 0.42: scored.append((r, c))
    scored.sort(reverse=True)
    return scored[:topn]

if __name__ == '__main__':
    for a in sys.argv[1:]:
        addr = int(a, 16)
        print("TH13 0x%06x ->" % addr, ", ".join("0x%06x (%.3f)" % (c, r) for r, c in best('13', addr, '14')))

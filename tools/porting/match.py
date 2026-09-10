"""Map TH12 addresses to TH13 by matching sequences of normalised instructions
(mnemonic + register operands, every number wildcarded)."""
import re, sys, pickle, os
def load_asm(path):
    seq=[]  # (addr, norm)
    for line in open(path,errors='replace'):
        mm=re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+?)\t(.*)',line)
        if not mm: continue
        a=int(mm.group(1),16); t=mm.group(3).strip()
        t=re.sub(r'\s+#.*','',t)               # objdump comments
        n=re.sub(r'0x[0-9a-f]+','N',t)
        n=re.sub(r'\b\d+\b','N',n)
        seq.append((a,n))
    return seq
cache='asm.pkl'
if os.path.exists(cache): S12,S13=pickle.load(open(cache,'rb'))
else:
    S12=load_asm('OLD.asm'); S13=load_asm('NEW.asm'); pickle.dump((S12,S13),open(cache,'wb'))
I12={a:i for i,(a,_) in enumerate(S12)}; I13={a:i for i,(a,_) in enumerate(S13)}
def key(seq,i,n): return tuple(x[1] for x in seq[i:i+n])
_idx={}
def index13(n):
    if n not in _idx:
        d={}
        for i in range(len(S13)-n): d.setdefault(key(S13,i,n),[]).append(S13[i][0])
        _idx[n]=d
    return _idx[n]
def find(a12, n=10, back=0):
    """match n instructions starting `back` instructions before a12"""
    if a12 not in I12: return None
    i=I12[a12]-back
    k=key(S12,i,n)
    hits=index13(n).get(k,[])
    return [h for h in hits], S12[i][0]
def best(a12, show=True):
    for n in (16,12,10,8,6,5,4):
        r=find(a12,n)
        if r and len(r[0])==1:
            if show: print(f'{a12:#x} -> {r[0][0]:#x}   (n={n})')
            return r[0][0]
    r=find(a12,6)
    if show: print(f'{a12:#x} -> ambiguous/none: {[hex(x) for x in (r[0] if r else [])][:8]}')
    return None
if __name__=='__main__':
    for a in sys.argv[1:]: best(int(a,16))

# --- reference-site matching: find where TH12 mentions F, match that site, read TH13's operand ---
import struct
RAW12=[x for x in S12]; 
def refs12(F):
    """indices of TH12 instructions that contain F as an immediate/disp or as a call/jmp target"""
    out=[]
    hexF=f'{F:x}'
    for i,(a,n) in enumerate(S12):
        pass
    return out
# faster: scan raw asm text once for the hex token
_txt12=None
def refs_text(F, path='OLD.asm'):
    global _txt12
    if _txt12 is None: _txt12=open(path,errors='replace').read().split('\n')
    tok=f'0x{F:x}'
    res=[]
    for line in _txt12:
        mm=re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+?)\t(.*)',line)
        if mm and tok in mm.group(3):
            res.append((int(mm.group(1),16), mm.group(3).strip()))
    return res
def operand13(a13):
    """the numeric operands of the TH13 instruction at a13 (addresses/immediates)"""
    if a13 not in I13: return []
    line=[l for l in open('NEW.asm',errors='replace') if l.startswith(f'  {a13:x}:\t')]
    if not line: return []
    return [int(x,16) for x in re.findall(r'0x([0-9a-f]+)',line[0].split('\t')[2])]
def via_ref(F, n=8, show=True):
    cands={}
    for a,t in refs_text(F):
        for back in (0,2,4):
            r=find(a,n,back)
            if r and len(r[0])==1:
                h=r[0][0]
                # the matched sequence starts `back` insns before a; step forward `back` insns in TH13
                i=I13[h]+back; a13=S13[i][0]
                ops=operand13(a13)
                # pick the operand playing the role of F: same position as F in the TH12 operand list
                ops12=[int(x,16) for x in re.findall(r'0x([0-9a-f]+)',t)]
                if F in ops12 and len(ops)==len(ops12):
                    v=ops[ops12.index(F)]; cands[v]=cands.get(v,0)+1
                break
    if show: print(f'{F:#x} via refs -> {[(hex(k),v) for k,v in sorted(cands.items(),key=lambda x:-x[1])]}')
    return cands

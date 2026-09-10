"""Match TH12 functions to TH13 by similarity of normalised decompiled bodies."""
import re, pickle, os, sys
def load(path):
    fns={}; cur=None; buf=[]
    for line in open(path,errors='replace'):
        m=re.match(r'// ==== FUNCTION (\S+) @ ([0-9a-f]+) size=(\d+)',line)
        if m:
            if cur: fns[cur]=(''.join(buf),size)
            cur=int(m.group(2),16); size=int(m.group(3)); buf=[]
        else: buf.append(line)
    if cur: fns[cur]=(''.join(buf),size)
    return fns
def norm(c):
    c=re.sub(r'//.*','',c)
    c=re.sub(r'\b(FUN|DAT|LAB|PTR|param|local|uVar|iVar|pcVar|puVar|piVar|fVar|dVar|cVar|bVar|sVar|uStack|iStack|auStack|pfVar|pdVar|ppuVar|ppiVar|pbVar|psVar|switchD|caseD|joined_r0x|thunk_FUN|_DAT)[_0-9a-fA-Fx]*','V',c)
    c=re.sub(r'0x[0-9a-fA-F]+','N',c); c=re.sub(r'\b\d+(\.\d+)?\b','N',c)
    toks=re.findall(r'[A-Za-z_]+|[^\sA-Za-z_]',c)
    return toks
def shingles(toks,k=4): return set(tuple(toks[i:i+k]) for i in range(max(1,len(toks)-k+1)))
cache='dmatch.pkl'
if os.path.exists(cache): F12,F13,S12s,S13s=pickle.load(open(cache,'rb'))
else:
    F12=load('OLD_decomp.c'); F13=load('NEW_decomp.c')
    S12s={a:shingles(norm(c)) for a,(c,s) in F12.items()}; S13s={a:shingles(norm(c)) for a,(c,s) in F13.items()}
    pickle.dump((F12,F13,S12s,S13s),open(cache,'wb'))
def best(a12, top=3, show=True):
    s=S12s.get(a12)
    if not s: 
        if show: print(f'{a12:#x}: not a function in TH12 decomp'); 
        return []
    res=[]
    for b,t in S13s.items():
        if not t: continue
        if len(t)<len(s)*0.5 or len(t)>len(s)*2: continue
        j=len(s&t)/len(s|t)
        if j>0.15: res.append((j,b))
    res.sort(reverse=True)
    if show: print(f'{a12:#x} -> '+', '.join(f'{b:#x} ({j:.2f})' for j,b in res[:top]))
    return res[:top]
if __name__=='__main__':
    for a in sys.argv[1:]: best(int(a,16))

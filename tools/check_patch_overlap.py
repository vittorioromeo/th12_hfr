#!/usr/bin/env python3
"""Does another patch write where this one does?

Touhou HFR and a translation patch such as thcrap can only share a game if they do not write
the same bytes -- and if neither one's verification covers a byte the other writes, because
this patch refuses to install into code it does not recognise.

This answers that mechanically rather than by reading two sets of addresses side by side.
It takes the patch plan the regression suite dumps (build/tests/<exe>.patches.json, which
lists every range this patch writes plus the frozen signatures and frame-loop sites it
checks) and thcrap's own game definitions, which carry the addresses and the code it writes,
and reports any range they have in common.

    ./test.sh th10.exe                       # writes build/tests/th10.exe.patches.json
    python3 tools/check_patch_overlap.py \
        <thcrap>/repos/nmlgc/base_tsa/global.js,.../th10.js,.../th10.v1.00a.js \
        build/tests/th10.exe.patches.json

thcrap's files are JSON5 and merge in the order given (global, then the game, then the
version), the same way thcrap resolves them. Sizes come from thcrap's own rules: a binhack
writes as many bytes as its "code" renders to, where a hex pair is one byte and a [reference]
or <option> is a pointer; a breakpoint writes its "cavesize". Addresses written "Rx1234" are
relative to the image base, "0x401234" absolute.

Entries under "init_stages" are listed separately rather than compared: those run before the
game's code exists in its final form -- on a Steam release they are what cracks the DRM
stub's integrity check and defers the rest until after it has decrypted -- so their addresses
are in the wrapper or in another module, not in the game.
"""
import json,re,sys,io,os

def load_json5(path):
    s=io.open(path,encoding='utf-8-sig').read()
    s=re.sub(r'/\*.*?\*/','',s,flags=re.S)
    s=re.sub(r'(^|\s)//[^\n]*','',s)
    s=re.sub(r',(\s*[}\]])',r'\1',s)          # trailing commas
    return json.loads(s)

def code_size(code):
    """thcrap code_string_calc_size: hex pairs are one byte, [expr]/<expr> are a pointer."""
    n=0; i=0
    while i<len(code):
        c=code[i]
        if c.isspace(): i+=1; continue
        if c in '[<':
            close=']' if c=='[' else '>'
            depth=1; i+=1
            while i<len(code) and depth:
                if code[i]==c: depth+=1
                elif code[i]==close: depth-=1
                i+=1
            n+=4; continue
        if c in '0123456789abcdefABCDEF':
            n+=1; i+=2; continue
        i+=1
    return n

def merge(dicts,key):
    out={}
    for d in dicts:
        for k,v in (d.get(key) or {}).items():
            out.setdefault(k,{}).update(v if isinstance(v,dict) else {'_':v})
    return out

def addrs(v):
    a=v.get('addr')
    if a is None: return []
    return a if isinstance(a,list) else [a]

def to_va(a,base=0x400000):
    if isinstance(a,int): return a
    a=a.strip()
    if a.lower().startswith('rx'): return base+int(a[2:],16)
    if a.lower().startswith('0x'): return int(a,16)
    return None

def collect(files,base=0x400000):
    ds=[load_json5(f) for f in files]
    ranges=[]
    for name,v in merge(ds,'binhacks').items():
        size=code_size(v['code']) if 'code' in v else None
        for a in addrs(v):
            va=to_va(a,base)
            if va is None or size is None: continue
            ranges.append((va,size,'binhack '+name))
    for name,v in merge(ds,'breakpoints').items():
        size=v.get('cavesize')
        if not isinstance(size,int): continue
        for a in addrs(v):
            va=to_va(a,base)
            if va is not None: ranges.append((va,size,'breakpoint '+name))
    # init_stages: stage 0 applies before decryption, listed separately
    stages=[]
    for d in ds:
        for i,st in enumerate(d.get('init_stages') or []):
            for key in ('binhacks','breakpoints'):
                for name,v in (st.get(key) or {}).items():
                    size=code_size(v['code']) if 'code' in v else v.get('cavesize')
                    for a in addrs(v):
                        va=to_va(a,base)
                        if va is not None and isinstance(size,int):
                            stages.append((i,va,size,'%s %s'%(key[:-1],name)))
    return ranges,stages

tc_files=sys.argv[1].split(',')
hfr=json.load(open(sys.argv[2]))
ranges,stages=collect(tc_files)
IMG=(0x400000,0x400000+0x200000)   # the game module; the rest are import/export redirections
ours=[(a,n) for a,n in hfr['patches'] if IMG[0]<=a<IMG[1]]
outside=[(a,n) for a,n in hfr['patches'] if not (IMG[0]<=a<IMG[1])]

print("game: %s"%hfr['game'])
print("HFR writes %d ranges into the game module (+%d import/export redirections elsewhere);"
      " thcrap writes %d"%(len(ours),len(outside),len(ranges)))
def lo(r): return r[0]
def hi(r): return r[0]+r[1]
clash=[]
for (a,n) in ours:
    for (b,m,what) in ranges:
        if a < b+m and b < a+n:
            clash.append((a,n,b,m,what))
print()
sigs=[(a,n) for a,n in hfr.get('signatures',[])]
confs=[(a,n) for a,n in hfr.get('conflicts',[])]
sclash=[(a,n,b,m,w) for (a,n) in sigs for (b,m,w) in ranges if a<b+m and b<a+n]
cclash=[(a,n,b,m,w) for (a,n) in confs for (b,m,w) in ranges if a<b+m and b<a+n]
print("HFR verifies %d frozen signatures and %d frame-loop conflict sites."%(len(sigs),len(confs)))
if sclash:
    print("SIGNATURE CLASHES (%d) -- identification/verification would refuse after thcrap has patched:"%len(sclash))
    for a,n,b,m,w in sclash: print("  signature %08x+%d  vs  thcrap %08x+%d  (%s)"%(a,n,b,m,w))
else:
    print("No frozen signature covers a byte thcrap writes.")
if cclash:
    print("CONFLICT-SITE CLASHES (%d) -- HFR would report 'another patch has modified this game':"%len(cclash))
    for a,n,b,m,w in cclash: print("  site %08x+%d  vs  thcrap %08x+%d  (%s)"%(a,n,b,m,w))
else:
    print("No frame-loop conflict site covers a byte thcrap writes.")
print()
if clash:
    print("OVERLAPS (%d):"%len(clash))
    for a,n,b,m,what in clash: print("  HFR %08x+%d  vs  thcrap %08x+%d  (%s)"%(a,n,b,m,what))
else:
    print("NO OVERLAP: no byte written by one patch is written by the other.")
print()
lo_o=min(a for a,_ in ours); hi_o=max(a+n for a,n in ours)
lo_t=min(r[0] for r in ranges); hi_t=max(r[0]+r[1] for r in ranges)
print("HFR    span %08x..%08x"%(lo_o,hi_o))
print("thcrap span %08x..%08x"%(lo_t,hi_t))
if stages:
    print("\ninit stages (applied before the game's code is decrypted):")
    for i,va,size,what in sorted(stages): print("  stage %d: %08x+%d  %s"%(i,va,size,what))

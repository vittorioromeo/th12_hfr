#!/usr/bin/env python3
import sys,re
import os
src=open(os.environ.get('DECOMP','decomp.c')).read()   # prints the decompiled function containing each address given (hex); DECOMP=path to ExportAll output
parts=re.split(r'(?m)^// ==== FUNCTION ', src)
funcs={}
order=[]
for p in parts[1:]:
    head,_,body=p.partition('\n')
    m=re.match(r'(\S+) @ (\w+) size=(\d+)',head)
    addr=int(m.group(2),16); funcs[addr]=(m.group(1),int(m.group(3)),body); order.append(addr)
order.sort()
def find(a):
    # function containing address a
    import bisect
    i=bisect.bisect_right(order,a)-1
    return order[i]
for arg in sys.argv[1:]:
    a=int(arg,16); f=find(a)
    name,size,body=funcs[f]
    print(f"// ==== {name} @ {f:08x} size={size} (asked {a:08x})")
    print(body)

"""Every markdown link and anchor in the tree, checked against the files and headings."""
import re,sys,unicodedata
from pathlib import Path
root=Path(__file__).resolve().parent.parent
docs=sorted(list(root.glob('*.md'))+list(root.glob('shaders/*.md'))+list(root.glob('tools/**/*.md')))
def anchors(p):
    out=set()
    for line in p.read_text(encoding='utf-8').splitlines():
        m=re.match(r'#{1,6}\s+(.*?)\s*$',line)
        if not m: continue
        t=m.group(1)
        t=re.sub(r'`|\*|_|~','',t)
        t=re.sub(r'\[([^\]]*)\]\([^)]*\)',r'\1',t)
        t=t.lower()
        t=''.join(c for c in t if c.isalnum() or c in ' -_')
        out.add(t.strip().replace(' ','-'))
    return out
bad=[]
for d in docs:
    text=d.read_text(encoding='utf-8')
    for m in re.finditer(r'\[[^\]]*\]\(([^)\s]+)\)',text):
        target=m.group(1)
        if target.startswith(('http://','https://','mailto:')): continue
        line=text[:m.start()].count('\n')+1
        file_part,_,anchor=target.partition('#')
        if file_part:
            t=(d.parent/file_part).resolve()
            if not t.exists(): bad.append(f"{d.name}:{line}  missing file: {target}"); continue
        else:
            t=d
        if anchor and t.suffix=='.md':
            if anchor not in anchors(t): bad.append(f"{d.name}:{line}  missing anchor: {target}")
print("\n".join(bad) if bad else "all internal links and anchors resolve")
print(f"({len(docs)} documents checked)")
sys.exit(1 if bad else 0)

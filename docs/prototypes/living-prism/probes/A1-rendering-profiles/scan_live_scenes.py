import json,os,sys,collections
idx=json.load(open('index.json'))
items=[e for e in (idx.get('examples') or []) if e.get('live')]
allk=collections.Counter()
def nodes(ns, c):
    for n in ns:
        c[n.get('kind','?')]+=1
        for key in ('effectors',):
            if n.get(key): c['+effectors']+=1
        if isinstance(n.get('lod'),dict): c['+lod']+=1
        if n.get('children'): nodes(n['children'], c)
for e in items:
    p=e.get('project') or e.get('path')
    d=json.load(open(p))
    sp=os.path.join(os.path.dirname(p), d['assets']['scene']['path'])
    try: s=json.load(open(sp))
    except Exception: print('MISSING', sp); continue
    c=collections.Counter(); nodes(s.get('nodes',[]), c)
    env=s.get('environment',{})
    post=list((s.get('post') or {}).keys())
    for k in c: allk[k]+=1
    print(os.path.basename(p)[:26].ljust(26), dict(c))
print(allk)

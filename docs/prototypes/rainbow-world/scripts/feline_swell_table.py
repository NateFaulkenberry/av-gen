import numpy as np, json
d=np.load('build/rwf/synth_env.npz'); tc=d['tc']; hi=d['hi']; t=d['t']; db=d['db']; cent=d['cent']
def sm(v,k): return np.convolve(v,np.ones(k)/k,'same')
h=sm(hi,8)  # 0.4 s
bar=lambda n: 0.2425+(n-1)*1.98347
rows=[]
for b in range(13,95,4):
    P=bar(b)
    body=(tc>P-6)&(tc<P+8)
    base=np.percentile(h[body],20)
    w=(tc>P-2.5)&(tc<P+5); i=np.flatnonzero(w); k=i[np.argmax(h[i])]; pk=h[k]
    thr=base+0.2*(pk-base)
    j=k
    while j>0 and h[j]>thr: j-=1
    e=k
    while e<len(h)-1 and h[e]>thr: e+=1
    # level dip before the phrase
    lw=(t>P-1.5)&(t<P+0.3); lb=(t>P+1)&(t<P+5)
    dip=np.median(db[lb])-db[lw].min()
    lvl=np.median(db[lb])
    rows.append(dict(bar=b,phrase=round(P,2),onset=round(tc[j],2),peak=round(tc[k],2),end=round(tc[e],2),
        rise=round(tc[k]-tc[j],2),length=round(tc[e]-tc[j],2),bright=round(float(pk),4),base=round(float(base),4),
        ratio=round(float(pk/max(base,1e-4)),2),dip_db=round(float(dip),1),level_db=round(float(lvl),1),cent=round(float(np.median(cent[(tc>P)&(tc<P+4)]))) ))
for r in rows: print(r)
json.dump(rows,open('build/rwf/swells.json','w'),indent=0)

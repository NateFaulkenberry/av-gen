import json, sys, numpy as np
from PIL import Image, ImageDraw
def srgb_to_lin(c): c=c/255.0; return np.where(c<=0.04045, c/12.92, ((c+0.055)/1.055)**2.4)
def lin_to_xyz(l):
    M=np.array([[0.4124,0.3576,0.1805],[0.2126,0.7152,0.0722],[0.0193,0.1192,0.9505]]); return l@M.T
def xyz_to_lab(x):
    w=np.array([0.95047,1.0,1.08883]); x=x/w
    f=np.where(x>0.008856, np.cbrt(x), 7.787*x+16/116)
    return np.stack([116*f[:,1]-16, 500*(f[:,0]-f[:,1]), 200*(f[:,1]-f[:,2])],1)
def kmeans(X, k, iters=40, seed=0):
    rng=np.random.default_rng(seed)
    C=X[rng.choice(len(X),1)]
    for _ in range(1,k):   # kmeans++
        d=np.min(((X[:,None,:]-C[None])**2).sum(-1),1); C=np.vstack([C,X[rng.choice(len(X),p=d/d.sum())]])
    for _ in range(iters):
        lab=np.argmin(((X[:,None,:]-C[None])**2).sum(-1),1)
        C=np.array([X[lab==j].mean(0) if (lab==j).any() else C[j] for j in range(k)])
    return C, lab
def clusters(px, k):
    lab=xyz_to_lab(lin_to_xyz(srgb_to_lin(px.astype(float))))
    C,l=kmeans(lab,k)
    out=[]
    for j in range(k):
        m=l==j
        if not m.any(): continue
        rgb=px[m].mean(0)
        chroma=float(np.hypot(*C[j,1:]))
        out.append({"share":float(m.mean()),"srgb":[int(round(v)) for v in rgb],"hex":"#%02x%02x%02x"%tuple(int(round(v)) for v in rgb),
                    "linear":[round(float(v),4) for v in srgb_to_lin(rgb)],"L":round(float(C[j,0]),1),"chroma":round(chroma,1)})
    return sorted(out,key=lambda c:-c["share"])
res={}
for f in sys.argv[1:]:
    im=Image.open(f).convert('RGB'); im.thumbnail((360,360)); a=np.asarray(im)
    h=a.shape[0]
    flat=a.reshape(-1,3)[::2]
    sky=a[:int(h*0.35)].reshape(-1,3)[::2]; land=a[int(h*0.6):].reshape(-1,3)[::2]
    res[f]={"size":list(Image.open(f).size),"whole":clusters(flat,8),"top":clusters(sky,3),"bottom":clusters(land,4)}
json.dump(res,open('palettes.json','w'),indent=1)
# swatch sheet
rows=len(res); W=1100; Hh=90
sheet=Image.new('RGB',(W,rows*Hh),(20,20,20)); d=ImageDraw.Draw(sheet)
for i,(f,r) in enumerate(res.items()):
    im=Image.open(f).convert('RGB'); im.thumbnail((120,84)); sheet.paste(im,(4,i*Hh+3))
    x=130
    for c in r["whole"]:
        w=int(560*c["share"]); d.rectangle([x,i*Hh+5,x+w,i*Hh+45],fill=tuple(c["srgb"])); x+=w
    x=130
    for tag in ("top","bottom"):
        for c in r[tag]:
            d.rectangle([x,i*Hh+50,x+40,i*Hh+84],fill=tuple(c["srgb"])); x+=42
        x+=16
    d.text((700,i*Hh+10),f,fill=(230,230,230))
sheet.save('swatches.png')
for f,r in res.items():
    print(f, ' '.join(c['hex']+':%d%%'%(100*c['share']) for c in r['whole']))

"""Tideglyph sign atlas for Neon Dreams (original glyph system, no fonts).
Glyphs live on a 3x3 lattice; strokes are bars, bowls (half arcs), hooks (quarter arcs), rings and dots.
Output: tideglyph_atlas.png (white-on-black emission mask, 2048x2048) + tideglyph_atlas.json (uv rects)."""
import json, math, random, sys
from PIL import Image, ImageDraw, ImageFilter
S = 2048; SS = 2                      # supersample
img = Image.new("L", (S*SS, S*SS), 0); d = ImageDraw.Draw(img)
rng = random.Random(7)

def glyph(seed):
    r = random.Random(seed); st = []
    n = r.choice([2,3,3,3,4])
    kinds = ["vbar","hbar","bowl","hook","ring","dot","diag","vhalf","hhalf"]
    w = [3,3,4,3,1.2,2.5,1.2,2.5,2.5]
    used = set()
    while len(st) < n:
        k = r.choices(kinds, w)[0]
        if k in ("ring",) and k in used: continue
        used.add(k)
        st.append((k, r.randrange(3), r.randrange(3), r.randrange(4)))
    return st

def draw_glyph(st, x, y, s, lw):
    """x,y top-left of an s*s cell; lattice points at 0.2,0.5,0.8"""
    P = lambda i,j: (x + s*(0.2+0.3*i), y + s*(0.2+0.3*j))
    cap = lambda p: d.ellipse([p[0]-lw/2,p[1]-lw/2,p[0]+lw/2,p[1]+lw/2], fill=255)
    def line(a,b): d.line([a,b], fill=255, width=int(lw)); cap(a); cap(b)
    for k,i,j,q in st:
        if k=="vbar": line(P(i,0),P(i,2))
        elif k=="hbar": line(P(0,j),P(2,j))
        elif k=="vhalf": line(P(i,q%2),P(i,q%2+1))
        elif k=="hhalf": line(P(q%2,j),P(q%2+1,j))
        elif k=="diag": line(P(0,0 if q%2 else 2),P(2,2 if q%2 else 0))
        elif k=="dot": p=P(i,j); d.ellipse([p[0]-lw,p[1]-lw,p[0]+lw,p[1]+lw], fill=255)
        elif k=="ring":
            c=P(1,1); R=s*0.3; d.ellipse([c[0]-R,c[1]-R,c[0]+R,c[1]+R], outline=255, width=int(lw))
        elif k in ("bowl","hook"):
            c=P(1,1); R=s*0.3; a0=q*90; a1=a0+(180 if k=="bowl" else 90)
            d.arc([c[0]-R,c[1]-R,c[0]+R,c[1]+R], a0, a1, fill=255, width=int(lw))
G = [glyph(100+i) for i in range(40)]

def picto(name, x, y, s, lw):
    c=(x+s/2,y+s/2); R=s*0.34
    if name=="moon":
        d.ellipse([c[0]-R,c[1]-R,c[0]+R,c[1]+R], fill=255); d.ellipse([c[0]-R*0.45,c[1]-R*1.05,c[0]+R*1.25,c[1]+R*0.75], fill=0)
    elif name=="bowl":
        d.pieslice([c[0]-R,c[1]-R*0.9,c[0]+R,c[1]+R*1.1],0,180,fill=255)
        for k in (-1,0,1): d.arc([c[0]+k*R*0.5-R*0.2,c[1]-R*1.2,c[0]+k*R*0.5+R*0.2,c[1]-R*0.3],100,260,fill=255,width=int(lw*0.7))
    elif name=="wave":
        for k in (-1,0,1):
            yy=c[1]+k*R*0.6
            for m in range(3): d.arc([c[0]-R+m*R*0.66,yy-R*0.22,c[0]-R+(m+1)*R*0.66,yy+R*0.22],180 if m%2==0 else 0,360 if m%2==0 else 180,fill=255,width=int(lw))
    elif name=="lamp":
        d.ellipse([c[0]-R*0.62,c[1]-R*0.8,c[0]+R*0.62,c[1]+R*0.8], outline=255, width=int(lw))
        d.line([(c[0]-R*0.5,c[1]),(c[0]+R*0.5,c[1])], fill=255, width=int(lw*0.7))
        d.line([(c[0],c[1]-R*1.15),(c[0],c[1]-R*0.8)], fill=255, width=int(lw)); d.line([(c[0],c[1]+R*0.8),(c[0],c[1]+R*1.2)], fill=255, width=int(lw))
    elif name=="kite":
        d.polygon([(c[0],c[1]-R),(c[0]+R*0.7,c[1]-R*0.1),(c[0],c[1]+R*0.9),(c[0]-R*0.7,c[1]-R*0.1)], outline=255, width=int(lw))
        d.line([(c[0],c[1]+R*0.9),(c[0]+R*0.3,c[1]+R*1.3)], fill=255, width=int(lw*0.6))
    elif name=="cross":
        for a,b in ((-1,0),(1,0),(0,-1),(0,1),(0,0)): d.ellipse([c[0]+a*R*0.62-lw,c[1]+b*R*0.62-lw,c[0]+a*R*0.62+lw,c[1]+b*R*0.62+lw], fill=255)
    elif name=="arcs":
        for k in (1,2,3): d.arc([c[0]-R*k/3,c[1]-R*k/3+R*0.3,c[0]+R*k/3,c[1]+R*k/3+R*0.3],200,340,fill=255,width=int(lw))
        d.ellipse([c[0]-lw,c[1]+R*0.3-lw,c[0]+lw,c[1]+R*0.3+lw], fill=255)
    elif name=="cup":
        d.rounded_rectangle([c[0]-R*0.6,c[1]-R*0.5,c[0]+R*0.6,c[1]+R*0.8], radius=R*0.3, outline=255, width=int(lw))
        d.ellipse([c[0]-lw*0.9,c[1]-R*1.0-lw*0.9,c[0]+lw*0.9,c[1]-R*1.0+lw*0.9], fill=255)
    elif name=="nine":
        for a in range(3):
            for b in range(3): p=(c[0]+(a-1)*R*0.6,c[1]+(b-1)*R*0.6); d.ellipse([p[0]-lw*0.9,p[1]-lw*0.9,p[0]+lw*0.9,p[1]+lw*0.9], fill=255)
    elif name=="chev":
        for k in (-1,0,1): d.line([(c[0]-R*0.7,c[1]+k*R*0.5+R*0.25),(c[0],c[1]+k*R*0.5-R*0.25),(c[0]+R*0.7,c[1]+k*R*0.5+R*0.25)], fill=255, width=int(lw), joint="curve")

BRANDS = [  # name, trade, pictogram, glyph ids (wordmark)
 ("Lumen & Daughters","lamp makers","lamp",[0,5,9]),      ("Ninefold Tea","tea house","nine",[3,7]),
 ("Kite Line","cable ferry","chev",[11,2,14,6]),           ("Slow Tide Baths","bath house","wave",[1,8,12]),
 ("Oru Noodle","noodle counter","bowl",[4,10]),            ("Vesper Hotel","hotel","moon",[13,15,17,19]),
 ("Halcyon Remedies","pharmacy","cross",[16,18,20]),       ("Deepwell Radio","radio repair","arcs",[21,22,5,23]),
 ("Paper Kite Books","bookshop","kite",[24,25,26]),        ("Pilot Light","bar","cup",[27,28]),
 ("Mooring Seven","district mark","nine",[29,30,31]),      ("Greywater Laundry","laundry","wave",[32,33,34,35]),
]
cells = []
def frame(x0,y0,x1,y1,lw): d.rounded_rectangle([x0,y0,x1,y1], radius=lw*3, outline=255, width=int(lw))
# 8 vertical blades: 224 x 1088 px each (aspect 1:4.86), row at top
bw, bh = 224, 1088
for i in range(8):
    name,trade,pic,word = BRANDS[i]
    x0, y0 = i*256+16, 16; X0,Y0 = x0*SS, y0*SS; W,H = bw*SS, bh*SS; lw = 9*SS
    frame(X0+lw, Y0+lw, X0+W-lw, Y0+H-lw, lw*0.6)
    cs = W*0.78; cx = X0+(W-cs)/2; yy = Y0+W*0.14
    picto(pic, cx, yy, cs, lw); yy += cs*1.05
    d.line([(cx+cs*0.15,yy),(cx+cs*0.85,yy)], fill=255, width=int(lw*0.5)); yy += cs*0.12
    for g in (word+word)[:4]:
        draw_glyph(G[g], cx, yy, cs, lw); yy += cs*1.02
    cells.append({"id":"blade_%d"%i,"brand":name,"trade":trade,"kind":"blade","px":[x0,y0,bw,bh],"aspect":bw/bh})
# 8 horizontal fascias: 480 x 128 px, two columns x 4 rows
fw, fh = 480, 128
for i in range(8):
    name,trade,pic,word = BRANDS[(i+4)%12]
    x0 = 16 + (i%4)*512; y0 = 1136 + (i//4)*160; X0,Y0=x0*SS,y0*SS; W,H=fw*SS,fh*SS; lw=7*SS
    frame(X0+lw, Y0+lw, X0+W-lw, Y0+H-lw, lw*0.6)
    cs = H*0.74; yy = Y0+(H-cs)/2; xx = X0+H*0.2
    picto(pic, xx, yy, cs, lw); xx += cs*1.15
    d.line([(xx,yy+cs*0.1),(xx,yy+cs*0.9)], fill=255, width=int(lw*0.5)); xx += cs*0.15
    for g in word[:3]:
        draw_glyph(G[g], xx, yy, cs, lw); xx += cs*1.0
    cells.append({"id":"fascia_%d"%i,"brand":name,"trade":trade,"kind":"fascia","px":[x0,y0,fw,fh],"aspect":fw/fh})
# 8 roundels / lightboxes 192x192 (single pictogram), row
pics = ["lamp","nine","chev","wave","bowl","moon","cross","arcs","kite","cup"]
for i in range(10):
    x0 = 16 + i*200; y0 = 1472; X0,Y0=x0*SS,y0*SS; W=184*SS; lw=10*SS
    d.ellipse([X0+lw,Y0+lw,X0+W-lw,Y0+W-lw], outline=255, width=int(lw*0.6))
    picto(pics[i], X0+W*0.14, Y0+W*0.14, W*0.72, lw)
    cells.append({"id":"roundel_%d"%i,"brand":pics[i],"trade":"pictogram","kind":"roundel","px":[x0,y0,184,184],"aspect":1.0})
# rooftop channel-letter rows: 4 x (960x160): big glyphs without frame
for i in range(4):
    x0 = 16 + (i%2)*1008; y0 = 1680 + (i//2)*176; X0,Y0=x0*SS,y0*SS; H=160*SS; lw=12*SS
    word = BRANDS[[5,2,0,10][i]][3]; xx = X0+8*SS
    for g in (word+word)[:6]:
        draw_glyph(G[g], xx, Y0+H*0.06, H*0.88, lw); xx += H*0.98
    cells.append({"id":"roof_%d"%i,"brand":BRANDS[[5,2,0,10][i]][0],"trade":"rooftop letters","kind":"roof","px":[x0,y0,960,160],"aspect":6.0})
img = img.resize((S,S), Image.LANCZOS)
glow = img.filter(ImageFilter.GaussianBlur(5))
out = Image.merge("RGB", (img, Image.eval(glow, lambda v: min(255,int(v*1.6))), img.filter(ImageFilter.GaussianBlur(14))))
# R = crisp tube mask, G = near glow, B = wide glow (engine can weight them); also save a plain mask
out.save("tideglyph_atlas.png"); img.save("tideglyph_mask.png")
for c in cells:
    x,y,w,h = c["px"]; c["uv"] = [x/S, 1-(y+h)/S, (x+w)/S, 1-y/S]   # u0,v0,u1,v1 (v up)
json.dump({"size":S,"channels":"R tube mask, G near glow, B wide glow","cells":cells,"brands":[{"name":b[0],"trade":b[1],"pictogram":b[2]} for b in BRANDS]}, open("tideglyph_atlas.json","w"), indent=1)
print("cells", len(cells))

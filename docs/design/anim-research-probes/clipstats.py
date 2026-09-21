import json, struct, sys, math
p = sys.argv[1]
d = open(p,'rb').read()
jl = struct.unpack('<I', d[12:16])[0]
j = json.loads(d[20:20+jl])
# bin chunk
off = 20+jl
blen, btype = struct.unpack('<II', d[off:off+8])
bin_ = d[off+8:off+8+blen]
CT={5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),5125:('I',4),5126:('f',4)}
NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
def acc(i):
    a=j['accessors'][i]
    n=NC[a['type']]; fmt,sz=CT[a['componentType']]
    bv=j['bufferViews'][a['bufferView']]
    base=bv.get('byteOffset',0)+a.get('byteOffset',0)
    stride=bv.get('byteStride', n*sz)
    out=[]
    for k in range(a['count']):
        s=base+k*stride
        out.append(struct.unpack_from('<'+fmt*n, bin_, s))
    return out
nodes=j['nodes']
names=[nd.get('name','') for nd in nodes]
tot_keys=0; tot_dur=0.0; tot_ch=0
rows=[]
for a in j['animations']:
    dur=0.0; start=1e9; keys=0; ch=0
    rootdisp=None
    for c in a['channels']:
        s=a['samplers'][c['sampler']]
        t=acc(s['input'])
        if not t: continue
        keys+=len(t); ch+=1
        start=min(start,t[0][0]); dur=max(dur,t[-1][0])
        node=c['target']['node']; path=c['target']['path']
        if path=='translation' and names[node] in ('rig','root.x'):
            v=acc(s['output'])
            dx=v[-1][0]-v[0][0]; dy=v[-1][1]-v[0][1]; dz=v[-1][2]-v[0][2]
            rootdisp=(names[node],dx,dy,dz)
    L=dur-start
    tot_keys+=keys; tot_dur+=L; tot_ch+=ch
    rows.append((a['name'],round(start,4),round(dur,4),round(L,4),ch,keys,rootdisp))
rows.sort()
print(f"{'clip':22s} {'start':>7s} {'end':>7s} {'len':>7s} {'ch':>4s} {'keys':>6s}  rootdisp")
for r in rows:
    rd = '' if r[6] is None else f"{r[6][0]}: {r[6][1]:+.3f} {r[6][2]:+.3f} {r[6][3]:+.3f}"
    print(f"{r[0]:22s} {r[1]:7.3f} {r[2]:7.3f} {r[3]:7.3f} {r[4]:4d} {r[5]:6d}  {rd}")
print(f"\nTOTALS: {len(rows)} clips, {tot_dur:.2f} s of motion, {tot_ch} channels, {tot_keys} keys")
print(f"at 30 fps that is {tot_dur*30:.0f} frames; at 60 fps {tot_dur*60:.0f} frames")

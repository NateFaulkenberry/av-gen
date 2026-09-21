import json, struct, sys, math
p='assets/aliens/alien-scout.glb'
d=open(p,'rb').read(); jl=struct.unpack('<I',d[12:16])[0]; j=json.loads(d[20:20+jl])
off=20+jl; blen,_=struct.unpack('<II',d[off:off+8]); bin_=d[off+8:off+8+blen]
CT={5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),5125:('I',4),5126:('f',4)}
NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
def acc(i):
    a=j['accessors'][i]; n=NC[a['type']]; fmt,sz=CT[a['componentType']]
    bv=j['bufferViews'][a['bufferView']]; base=bv.get('byteOffset',0)+a.get('byteOffset',0)
    stride=bv.get('byteStride', n*sz)
    return [struct.unpack_from('<'+fmt*n,bin_,base+k*stride) for k in range(a['count'])]
names=[nd.get('name','') for nd in j['nodes']]
for a in j['animations']:
    if a['name'] not in ('Walking','Running','Idle','Idle_turn','Jumping'): continue
    for c in a['channels']:
        tgt=c['target']
        if tgt['path']!='translation': continue
        nm=names[tgt['node']]
        if nm not in ('rig','root.x'): continue
        v=acc(a['samplers'][c['sampler']]['output'])
        xs=[k[0] for k in v]; ys=[k[1] for k in v]; zs=[k[2] for k in v]
        path=sum(math.dist(v[i],v[i+1]) for i in range(len(v)-1))
        print(f"{a['name']:14s} {nm:8s} n={len(v):4d} x[{min(xs):+.4f},{max(xs):+.4f}] y[{min(ys):+.4f},{max(ys):+.4f}] z[{min(zs):+.4f},{max(zs):+.4f}] pathlen={path:.4f}")

import json,glob,os,sys,statistics as st,collections,csv
D=os.path.expanduser(sys.argv[1] if len(sys.argv)>1 else '~/Desktop/av-gen-review/80-living-prism-audit/A-data/baseline')
runs=collections.defaultdict(list)
for f in sorted(glob.glob(D+'/*.json')):
    b=os.path.basename(f)[:-5]; key=b.rsplit('-r',1)[0]
    try: runs[key].append((b,json.load(open(f))))
    except Exception as e: print('BAD',f,e)
def med(x): return st.median(x) if x else float('nan')
def rng(x): return (min(x),max(x)) if x else (float('nan'),)*2
for key,rs in runs.items():
    print('\n###',key,'reps',len(rs))
    for metric in ['frameMs','gpuMs','cpuWorkMs','waitMs']:
        vals={p:[r['frame'][metric][p] for _,r in rs if r['frame'].get(metric)] for p in ['p50','p95','p99','max']}
        print(f"  {metric:10s} "+'  '.join(f"{p} {med(v):6.2f} [{rng(v)[0]:.2f}-{rng(v)[1]:.2f}]" for p,v in vals.items()))
    bud=[r['frame']['budget'] for _,r in rs]
    print('  budget: overBudget', [b['overBudget'] for b in bud], 'frames',[b['frames'] for b in bud], 'misses',[b.get('deadlineMisses') for b in bud], 'vsyncsMissed',[b.get('vsyncsMissed') for b in bud])
    print('  critical', [r['criticalPath']['verdict'] for _,r in rs], 'level', [r['conditions']['quality'].get('level') for _,r in rs], 'internal', rs[0][1]['conditions']['internal'])
    cats=collections.defaultdict(list); labels=collections.defaultdict(list)
    for _,r in rs:
        for c in r['gpu']:
            cats[c['category']].append(c['medianMs'])
            for l in c['labels']: labels[l['label']].append(l['medianMs'])
    print('  gpu categories:', ', '.join(f"{k} {med(v):.2f}" for k,v in sorted(cats.items(), key=lambda kv:-med(kv[1])) if med(v)>0.05))
    print('  top labels:', ', '.join(f"{k} {med(v):.2f}" for k,v in sorted(labels.items(), key=lambda kv:-med(kv[1]))[:14]))
    cpu=collections.defaultdict(list)
    for _,r in rs:
        for c in r['cpu']:
            if c.get('medianMs') is not None and c['medianMs']>=0: cpu[c['name']].append(c['medianMs'])
    print('  cpu:', ', '.join(f"{k} {med(v):.2f}" for k,v in sorted(cpu.items(), key=lambda kv:-med(kv[1]))[:8]))
    res=rs[0][1]['resources']
    g=res['geometry']; m=res['memory']; p=res['particles']; po=res['post']; sh=res['shadows']
    print('  geom', {k:g[k] for k in list(g)[:8]})
    print('  mem', {k:m.get(k) for k in ['textureBytes','bufferBytes','renderTargetBytes','totalBytes'] if k in m}, 'particles', {k:p[k] for k in list(p)[:5]}, 'post', {k:po[k] for k in list(po)[:5]}, 'shadows', {k:sh[k] for k in list(sh)[:6]})
    print('  cold', {k:rs[0][1]['cold'][k] for k in ['loadMs','firstFrameMs','compilesDuringMeasure','steadyReached']}, 'audio', rs[0][1]['conditions']['audio'])
    # ladder occupancy from traces
    occ=collections.Counter(); chg=0
    for b,_ in rs:
        t=D+'/'+b+'.csv'
        if not os.path.exists(t): continue
        rows=list(csv.DictReader(open(t)))
        if 'level' in rows[0]:
            prev=None
            for row in rows:
                if row['phase']!='m': continue
                occ[row['level']+'@'+row['scale']]+=1
                if prev and row['level']!=prev: chg+=1
                prev=row['level']
    if occ: print('  ladder occupancy (measured frames, all reps):', dict(occ), 'level changes', chg)

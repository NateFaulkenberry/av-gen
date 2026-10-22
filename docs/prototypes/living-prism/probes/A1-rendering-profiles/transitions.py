import csv,sys,glob,os,statistics as st
for t in sys.argv[1:]:
    rows=[r for r in csv.DictReader(open(t))]
    if 'level' not in rows[0]: continue
    seq=[]; prev=None
    fm=[float(r['frameMs']) for r in rows]
    trans=[]
    for i,r in enumerate(rows):
        if prev is not None and r['level']!=prev:
            seq.append(f"{r['phase']}{float(r['t']):.1f}s {prev}->{r['level']}")
            trans.append(i)
        prev=r['level']
    # frame intervals around transitions: the change frame and the 3 after, vs the median of the run
    near=[max(fm[i:i+4]) for i in trans]
    steady=st.median(fm)
    print(os.path.basename(t), 'changes',len(trans),'| median frame %.2f'%steady, '| worst of 4 frames after each change:', [round(x,1) for x in near])
    print('   ', '; '.join(seq))

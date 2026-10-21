"""Rainbow World 25: how often the trees change level of detail in view over one lap, and how big they are when
they do (the camera re-evaluated as the engine places it; trees from the built scene).

    python3 docs/prototypes/rainbow-world/scripts/tree_lod_switches.py dist|px
"""
import sys, json, math, numpy as np
sys.path.insert(0, 'docs/prototypes/rainbow-world/scripts')
import camera_motion as cm
mode = sys.argv[1]   # "dist": the old distance thresholds (22/55/90/150 m); "px": the screen-size ones (130/75/42/22 px)
path, keys, la = cm.load('examples/rainbow-world/rainbow-world-feline.json')
scene = json.load(open('examples/rainbow-world/scene/rainbow-world.scene.json'))
R_ = {"great": 20.5, "blossom": 9.6, "pine": 6.0, "weeper": 11.0}
T = []
for n in scene['nodes']:
    if n['name'].endswith('_leaves'):
        f = n['name'].split('_')[0]
        for q in n['procedural']['distribution']['points']:
            T.append((np.array([q[0], -q[2], q[1]]), R_[f] * q[8]))
P = np.array([t[0] for t in T]); RR = np.array([t[1] for t in T])
times = np.array([k['time'] for k in keys]); vals = np.array([k['value'] for k in keys]); lin = times < times[-1]
tt = np.arange(0, times[-1], 1 / 30.0); ss = np.interp(tt, times[lin], vals[lin]) * path.length
focal = 540 / math.tan(math.radians(53))
prev = None; sizes = []; last = {}; flick = 0
for i, s in enumerate(ss):
    p, Rm = cm.pose(path, s, la)
    d = P - p; dist = np.linalg.norm(d, axis=1); px = RR / dist * focal
    if mode == 'dist':
        lvl = np.searchsorted(np.array([22, 55, 90, 150]), dist)
    else:
        lvl = np.where(px > 130, 0, np.where(px > 75, 1, np.where(px > 42, 2, np.where(px > 22, 3, 4))))
    vis = (d @ Rm[:, 2]) / dist > math.cos(math.radians(60))
    if prev is not None:
        for j in np.flatnonzero((lvl != prev) & vis):
            sizes.append(px[j])
            if j in last and tt[i] - last[j] < 1.5: flick += 1
            last[j] = tt[i]
    prev = lvl
sizes = np.array(sizes); mins = tt[-1] / 60
print(mode, 'switches in view %d (%.0f/min); canopy radius at the switch: median %.0f px, p90 %.0f px, max %.0f px; over 60 px: %.0f/min; back-and-forth within 1.5 s: %.0f/min'
      % (len(sizes), len(sizes) / mins, np.median(sizes), np.percentile(sizes, 90), sizes.max(), (sizes > 60).sum() / mins, flick / mins))

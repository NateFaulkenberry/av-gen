#!/usr/bin/env python3
"""Reproduce GV3's baked per-vertex water flow (world/water.cpp flowAt, terrain.cpp buildChunkWater)
on the 1.2 m water grid, then measure how strongly each triangle shears the ripple pattern at a
timeline second (water.wgsl: noise sampled at (p - dir*speed*t) * scale).

Omits the smooth meander/turbulence noise (continuous, so it adds only a mild uniform shear);
keeps the two discontinuities: nearest-segment tangent switching and body switching (river/pool).
Read-only: reads the scene JSON, writes nothing into the repo.
"""
import json, math, sys
import numpy as np

REPO = "/Users/natefaulkenberry/Documents/GitHub/av-gen-gv3"
scene = json.load(open(f"{REPO}/examples/world/glowmere-valley-3.scene.json"))
valley = next(n for n in scene["nodes"] if n.get("name") == "valley")
feats = {f["name"]: f for f in valley["world"]["features"]}
river_pts = np.array(feats["glowmere-run-2"]["path"], dtype=np.float64)
pool = feats["elder-pool"]

def chaikin(pts, it):
    out = pts
    for _ in range(it):
        nxt = [out[0]]
        for a, b in zip(out[:-1], out[1:]):
            nxt.append(a + (b - a) * 0.25)
            nxt.append(a + (b - a) * 0.75)
        nxt.append(out[-1])
        out = np.array(nxt)
    return out

line = chaikin(river_pts, feats["glowmere-run-2"].get("smoothing", 0))
xz = line[:, [0, 2]]
seg = xz[1:] - xz[:-1]
heading = np.degrees(np.arctan2(seg[:, 1], seg[:, 0]))
turns = np.diff(heading)
length = np.sum(np.linalg.norm(seg, axis=1))
descent = line[0, 1] - line[-1, 1]
speed_river = min(max(descent / length * 12.0, 0.05), 1.6)
half_river = feats["glowmere-run-2"]["width"]
bank_shear = 0.7
print(f"smoothed centreline: {len(line)} nodes, length {length:.1f} m, descent {descent:.1f} m, "
      f"course speed {speed_river:.3f} m/s, halfWidth {half_river} m")
print(f"per-node turn: max {np.max(np.abs(turns)):.2f} deg, mean |turn| {np.mean(np.abs(turns)):.2f} deg, "
      f"nodes with |turn| > 2 deg: {int(np.sum(np.abs(turns) > 2))}")

pool_c = np.array([pool["path"][0][0], pool["path"][0][2]])
half_pool = pool["width"]
wind = np.array([0.7, 0.7]); wind /= np.linalg.norm(wind)
speed_pool = 0.55 * 0.12  # stillSpeed * stillFactor
fastest = speed_river

def river_flow(p):
    a = xz[:-1]; ab = seg
    len2 = np.sum(ab * ab, axis=1)
    t = np.clip(np.sum((p - a) * ab, axis=1) / len2, 0, 1)
    q = a + ab * t[:, None]
    d2 = np.sum((p - q) ** 2, axis=1)
    i = int(np.argmin(d2))  # first minimum wins, as the strict '<' in flowAt
    tangent = ab[i] / math.sqrt(len2[i])
    dist = math.sqrt(d2[i])
    r = min(max(dist / half_river, 0.0), 1.0)
    spd = speed_river * (1.0 - min(max(bank_shear * r * r, 0.0), 1.0))
    return tangent, spd, dist, dist <= half_river

def flow(p):
    tr, sr, dr, inr = river_flow(p)
    dp = float(np.linalg.norm(p - pool_c)); inp = dp <= half_pool
    d_r = dr - half_river if inr else dr
    d_p = dp - half_pool if inp else dp
    if d_r <= d_p:  # river listed first; strict '<' for later bodies
        return tr, sr, 0
    return wind, (speed_pool if inp else 0.0), 1

T = float(sys.argv[1]) if len(sys.argv) > 1 else 171.25
scale = float(sys.argv[2]) if len(sys.argv) > 2 else 5.2  # GV3 final rippleScale
step = 48.0 / 40.0
x0, x1, z0, z1 = -110.0, 90.0, -330.0, 330.0
gx = np.arange(-320.0 + math.floor((x0 + 320) / step) * step, x1, step)
gz = np.arange(-320.0 + math.floor((z0 + 320) / step) * step, z1, step)
F = np.zeros((len(gz), len(gx), 2)); S = np.zeros((len(gz), len(gx))); B = np.zeros((len(gz), len(gx)), int)
W = np.zeros((len(gz), len(gx)), bool)
for j, z in enumerate(gz):
    for i, x in enumerate(gx):
        p = np.array([x, z])
        d, s, b = flow(p)
        F[j, i] = d; S[j, i] = s / fastest; B[j, i] = b
        W[j, i] = np.linalg.norm(p - pool_c) <= half_pool or river_flow(p)[3]

# Per triangle (a,c,b) and (b,c,d) with a=(i,j) b=(i+1,j) c=(i,j+1) d=(i+1,j+1): offset o = dir*speed*t,
# linear over the triangle (the shader normalises dir per pixel; linear is the first-order picture).
def offset(j, i):
    return F[j, i] * S[j, i] * fastest * T

comp = []
bodyband = 0
records = []
for j in range(len(gz) - 1):
    for i in range(len(gx) - 1):
        if not (W[j, i] or W[j, i + 1] or W[j + 1, i] or W[j + 1, i + 1]):
            continue
        for tri in (((j, i), (j + 1, i), (j, i + 1)), ((j, i + 1), (j + 1, i), (j + 1, i + 1))):
            (ja, ia), (jb, ib), (jc, ic) = tri
            P = np.array([[gx[ia], gz[ja]], [gx[ib], gz[jb]], [gx[ic], gz[jc]]])
            O = np.array([offset(ja, ia), offset(jb, ib), offset(jc, ic)])
            E = np.array([P[1] - P[0], P[2] - P[0]]).T
            DO = np.array([O[1] - O[0], O[2] - O[0]]).T
            J = DO @ np.linalg.inv(E)  # d offset / d p
            M = np.eye(2) - J          # d (p - o) / d p
            sv = np.linalg.svd(M, compute_uv=False)
            comp.append(sv[0])
            mixed = len({B[ja, ia], B[jb, ib], B[jc, ic]}) > 1
            bodyband += int(mixed and sv[0] > 3)
            if sv[0] > 3:
                records.append((P.mean(axis=0), sv[0], mixed))

comp = np.array(comp)
print(f"t = {T:.2f} s, rippleScale {scale} cycles/m: {len(comp)} wet triangles")
for k in (1.5, 3, 5, 10, 20):
    print(f"  compression > {k:>4}: {int(np.sum(comp > k)):6d} triangles ({100*np.mean(comp > k):.2f}%)")
print(f"  max compression {comp.max():.1f}; band triangles at the river/pool switch: {bodyband}")
# Where the strong bands are: cluster centres by z to show one ray per node
zs = sorted(r[0][1] for r in records if not r[2])
if zs:
    groups = [[zs[0]]]
    for z in zs[1:]:
        (groups[-1].append(z) if z - groups[-1][-1] < 2.5 else groups.append([z]))
    print(f"  segment-switch bands (compression > 3): {len(groups)} separate runs along the river")
# Stripe spacing inside a band for the three layers, and what the fade believes
worst = comp.max()
for name, mult, spd in (("layer1", 1.0, 1.0), ("layer2", 2.7, 1.6), ("layer3", 7.4, 2.4)):
    print(f"  {name}: nominal wavelength {100/(scale*mult):.1f} cm; in the median band triangle "
          f"~{100/(scale*mult*np.median(comp[comp>3]) if np.any(comp>3) else 1):.1f} cm")

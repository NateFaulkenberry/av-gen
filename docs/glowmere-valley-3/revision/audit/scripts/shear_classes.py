#!/usr/bin/env python3
"""Classify the shear GV3's water applies to its ripple pattern into its three sources, and write a
top-down map of where the bands are. Builds on shear_bands.py (same reproduction of flowAt).

  class 'pool'    : a triangle whose vertices take their flow from different bodies (river vs pool)
  class 'segment' : same body, vertex directions differ by > 0.25 deg (a nearest-segment switch)
  class 'smooth'  : neither (the bank-shear profile; continuous)

Also evaluates the same map under a two-phase offset (period P) to show the bound.
"""
import math, sys
import numpy as np
from PIL import Image

sys.argv = sys.argv[:1] + ["0"]  # silence the base script's own report
exec(open(__file__.replace("shear_classes.py", "shear_bands.py")).read().split("T = float")[0])

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
        F[j, i] = d; S[j, i] = s; B[j, i] = b
        tr, sr, dr, inr = river_flow(p)
        W[j, i] = np.linalg.norm(p - pool_c) <= half_pool or inr

def tri_stats(T, period=None):
    """Per-triangle max singular value of d(p - o)/dp. With `period`, o uses the two-phase phase
    fract(t/period)*period, whose worst case is the full period (the phase just before the reset)."""
    tt = T if period is None else period  # worst case of a two-phase scheme: offset reaches one period
    out = {"pool": [], "segment": [], "smooth": []}
    for j in range(len(gz) - 1):
        for i in range(len(gx) - 1):
            if not (W[j, i] or W[j, i + 1] or W[j + 1, i] or W[j + 1, i + 1]):
                continue
            for tri in (((j, i), (j + 1, i), (j, i + 1)), ((j, i + 1), (j + 1, i), (j + 1, i + 1))):
                P = np.array([[gx[ii], gz[jj]] for jj, ii in tri])
                O = np.array([F[jj, ii] * S[jj, ii] * tt for jj, ii in tri])
                E = np.array([P[1] - P[0], P[2] - P[0]]).T
                DO = np.array([O[1] - O[0], O[2] - O[0]]).T
                sv = np.linalg.svd(np.eye(2) - DO @ np.linalg.inv(E), compute_uv=False)[0]
                bodies = {B[jj, ii] for jj, ii in tri}
                dirs = [F[jj, ii] for jj, ii in tri]
                ang = max(math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(a, b))))))
                          for a in dirs for b in dirs)
                cls = "pool" if len(bodies) > 1 else ("segment" if ang > 0.25 else "smooth")
                out[cls].append((sv, P.mean(axis=0)))
    return out

def report(label, stats):
    print(label)
    for cls in ("pool", "segment", "smooth"):
        v = np.array([s for s, _ in stats[cls]]) if stats[cls] else np.array([1.0])
        print(f"  {cls:8s} n={len(stats[cls]):6d}  median {np.median(v):6.2f}  p95 {np.percentile(v, 95):6.2f}  "
              f"max {v.max():6.2f}")

for T in (33.7, 171.25, 214.6):
    report(f"t = {T} s (today: offset = dir*speed*t, unbounded)", tri_stats(T))
report("any t, two-phase offset with an 8 s period (worst phase)", tri_stats(0.0, period=8.0))

# Top-down map at 171.25 s: pixel per grid cell, value = worst compression in the cell's two triangles
T = 171.25
st = tri_stats(T)
img = np.zeros((len(gz), len(gx), 3), np.uint8)
for cls, colour in (("smooth", (40, 90, 140)), ("segment", (255, 200, 40)), ("pool", (255, 40, 40))):
    for sv, c in st[cls]:
        i = int((c[0] - gx[0]) / step); j = int((c[1] - gz[0]) / step)
        k = min(1.0, math.log(max(sv, 1.0)) / math.log(20.0))
        if cls == "smooth":
            img[j, i] = np.maximum(img[j, i], (np.array(colour) * (0.35 + 0.65 * k)).astype(np.uint8))
        elif sv > 1.5:
            img[j, i] = np.array(colour) * (0.5 + 0.5 * k)
Image.fromarray(img[::-1]).resize((len(gx) * 2, len(gz) * 2), Image.NEAREST).save(
    __file__.replace("shear_classes.py", "shear_map_171s.png"))
print("wrote shear_map_171s.png (top-down, +z up; red = river/pool switch, amber = segment switch, "
      "blue = smooth bank shear, brighter = stronger)")

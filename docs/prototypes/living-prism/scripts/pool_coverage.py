#!/usr/bin/env python3
"""The Living Prism: the pool's share of the frame along the camera route (spec section 5: reflective 10-20%).

    python3 docs/prototypes/living-prism/scripts/pool_coverage.py [--step 1.0] [--res 192]

Casts one ray per pixel of a low-resolution frame (16:9, the route's vertical fov) from the baked camera at each
time, and counts the rays that meet the water plane (y = 0) where the terrain lies below it (layout.height < 0).
Occluders (formations, boulders) are ignored, so the share is an upper bound; at the low stations the near shore is
open, so the bound is close.
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..",
                                "examples", "living-prism"))
import layout as L  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--step", type=float, default=1.0)
ap.add_argument("--res", type=int, default=160)
ap.add_argument("--times", type=str, default="")
a = ap.parse_args()
W, H = a.res, a.res * 9 // 16
th = math.tan(math.radians(L.CAMERA_FOV) / 2)
samples, _ = L.camera_samples(a.step)
if a.times:
    want = [float(t) for t in a.times.split(",")]
    allS, _ = L.camera_samples(0.05)
    samples = [min(allS, key=lambda s: abs(s[0] - t)) for t in want]


def share(p, t):
    f = [t[i] - p[i] for i in range(3)]
    n = math.sqrt(sum(x * x for x in f))
    f = [x / n for x in f]
    r = [f[2], 0.0, -f[0]]
    rn = math.hypot(r[0], r[2])
    r = [-r[0] / rn, 0.0, -r[2] / rn]  # right = forward x up
    u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]]
    hit = 0
    for j in range(H):
        v = (1 - 2 * (j + 0.5) / H) * th
        for i in range(W):
            h = (2 * (i + 0.5) / W - 1) * th * W / H
            d = [f[k] + h * r[k] + v * u[k] for k in range(3)]
            if d[1] >= -1e-6:
                continue
            s = -p[1] / d[1]
            x, z = p[0] + s * d[0], p[2] + s * d[2]
            if L.height(x, z) < 0.0:
                hit += 1
    return hit / (W * H)


vals = [(s[0], share(s[1], s[2])) for s in samples]
for t, v in vals:
    print("t=%6.2f pool %.1f%%" % (t, 100 * v))
xs = sorted(v for _, v in vals)
print("loop: p50 %.1f%%  p90 %.1f%%  max %.1f%%  mean %.1f%%" % (100 * xs[len(xs) // 2], 100 * xs[int(0.9 * len(xs))],
                                                             100 * xs[-1], 100 * sum(xs) / len(xs)))

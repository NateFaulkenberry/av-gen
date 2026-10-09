"""Scene support, the CANOPY tree and GROVE: valley-floor trees seen from 300 m to 2 km, instanced by the
thousand.

Each tree is a short, slightly leaning trunk with two or three limbs carrying a crown. The crown is built from
lumpy, overlapping leaf-mass lobes on a dome, so it has real volume. Self-shadowing is painted in:
  - every lobe darkens toward its underside;
  - the lower lobes sit in the darker half of the texture;
  - clumped leaf detail goes into the normal map.
A faint speckle of emerald and cyan glints marks the upper crown. Tree height is 13-19 m.

The single tree is under 1,000 triangles. The GROVE is seven trees of graded size in a loose cluster, about 30 m
across, as one instance. There is only one level of detail; both files carry the same mesh.

Units metres, Z up in Blender (Y up exported), origin at the base.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (CYAN, EMERALD, Asset, bezier, col, fbm, lerp, smoothstep, tube_grid, worley)

TAU = 2 * math.pi


def tree(crown_slot, crown_reg, trunk_slot, trunk_reg, base, H, rng):
    lean = rng.normal(0, 0.06, 2)
    top = base + np.array([lean[0] * H, lean[1] * H, H * 0.40])
    # crown: lobes on a dome above the trunk top
    R = H * rng.uniform(0.36, 0.44)
    centre = top + np.array([0, 0, R * 0.55])
    # the trunk runs on up into the crown, so it never floats
    trunk = bezier(base, base + [0, 0, H * 0.2], top - [0, 0, H * 0.1], centre, 5)
    t = np.linspace(0, 1, 5)
    trunk_slot.add_grid(tube_grid(trunk, H * (0.035 - 0.015 * t), 6), trunk_reg, wrap_u=True)
    nl = rng.integers(10, 13)
    lobes = []
    for j in range(nl):
        if j == 0:
            off, r = np.array([0, 0, R * 0.35]), R * 0.7
        else:
            a = TAU * j / (nl - 1) + rng.uniform(-0.3, 0.3)
            el = rng.uniform(-0.25, 0.65)
            off = np.array([math.cos(a) * math.cos(el), math.sin(a) * math.cos(el), math.sin(el) * 0.8]) * R * 0.7
            r = R * rng.uniform(0.36, 0.5)
        lobes.append((centre + off, r))
    zs = [c[2] for c, _ in lobes]
    zlo, zhi = min(zs), max(zs)
    for c, r in lobes:
        nv, nt = 6, 9
        v = np.linspace(0, 1, nv)
        th = -np.linspace(0, TAU, nt, endpoint=False) + rng.uniform(0, TAU)
        V, TH = np.meshgrid(v, th, indexing="ij")
        ph = -math.pi / 2 + math.pi * V
        lump = 1 + 0.18 * (fbm(TH / TAU * 3 + c[0], V * 2 + c[1], 2, int(abs(c[0] * 100)) % 997, px=3) - 0.5) * 2
        rr = r * lump
        P = np.stack([rr * np.cos(ph) * np.cos(TH), rr * np.cos(ph) * np.sin(TH), rr * np.sin(ph) * 0.85], -1) + c
        # rows rise from the bottom pole; flip u so the lobe faces out. Lower lobes take the darker half of v.
        f = (c[2] - zlo) / max(zhi - zlo, 1e-6)
        crown_slot.add_grid(P[:, ::-1], crown_reg, wrap_u=True, t_range=(0.35 * f, 0.65 + 0.35 * f))
    return H


class Canopy:
    def __init__(self, seed=131, lod=0, grove=False):
        self.seed = seed
        self.grove = grove

    def build(self):
        rng = np.random.default_rng(self.seed)
        A = Asset("canopygrove" if self.grove else "canopy")
        crown = A.slot("crown", res=(512, 512), mode="OPAQUE", double_sided=False, emissive_strength=4.0)
        trunk = A.slot("trunk", res=(128, 128), mode="OPAQUE", double_sided=False, emissive_strength=4.0)
        rc = crown.region(0, 1, self.paint_crown, bump=4.0)
        rt = trunk.region(0, 1, self.paint_trunk, bump=1.0)
        if not self.grove:
            tree(crown, rc, trunk, rt, np.array([0, 0, -0.3]), 16.0, rng)
        else:
            spots = [(0, 0, 19.0), (9, 3, 15.0), (-7, 6, 14.0), (3, -9, 16.5), (-10, -5, 13.0), (13, -6, 12.5),
                     (-2, 12, 13.5)]
            for x, y, h in spots:
                tree(crown, rc, trunk, rt, np.array([x + rng.normal(0, 1), y + rng.normal(0, 1), -0.3]),
                     h * rng.uniform(0.9, 1.1), rng)
        self.A = A
        return A

    def paint_crown(self, S, T):
        sd = self.seed + 5
        F1, F2, _, h = worley(S * 14, T * 8, sd, px=14)
        clump = 1 - np.clip(F1, 0, 1) ** 1.5
        gap = smoothstep(0.08, 0.0, F2 - F1)
        mott = fbm(S * 6, T * 4, 3, sd + 1, px=6)
        shade = smoothstep(0.05, 0.95, T)  # underside dark, crown top lit
        base = lerp(np.array([0.004, 0.010, 0.008]), np.array([0.02, 0.05, 0.035]), shade * (0.5 + 0.5 * clump))
        base = base * (0.75 + 0.5 * mott)[..., None] * (1 - 0.6 * gap)[..., None]
        glint = smoothstep(0.9, 0.97, fbm(S * 60, T * 40, 2, sd + 2, px=60)) * smoothstep(0.5, 1.0, T)
        e = col(lerp(EMERALD, CYAN, mott), glint * 0.35)
        return dict(base=base, emis=e, height=clump - gap, rough=0.85, ao=0.25 + 0.75 * shade)

    def paint_trunk(self, S, T):
        base = np.ones(S.shape + (3,)) * np.array([0.012, 0.012, 0.014])
        return dict(base=base, emis=np.zeros(S.shape + (3,)), height=np.zeros_like(S), rough=0.9, ao=0.5 + 0.5 * T)


def make_tree(lod=0):
    return Canopy(lod=lod).build()


def make_grove(lod=0):
    return Canopy(seed=137, lod=lod, grove=True).build()

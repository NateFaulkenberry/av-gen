"""The LATTICE: curling, perforated honeycomb sheets rising from a knobbed foot (reference panel 3, top left).

Four ear-like sheets of different sizes, each a partly rolled funnel that opens and curls outward as it rises, its
outer margin ruffled. The sheet is a honeycomb cut out of a membrane (alpha MASK): small closed cells low down where
the tissue is young, larger open cells toward the rim, a continuous strand along every free edge. Cell walls glow
blue-violet, the knots where walls meet glow brighter cyan-violet, and the light gathers toward the rim.

Units metres, Z up in Blender (Y up exported), origin at the base. Height ~0.24 m.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (BLUE, CYAN, TISSUE_DARK, VIOLET, Asset, col, fbm, lace, lerp, revolve, rot_z, smoothstep)

TAU = 2 * math.pi


class Lattice:
    def __init__(self, seed=11, lod=0):
        self.seed = seed
        self.lod = lod
        self.q = 1.0 if lod == 0 else 0.35
        rng = np.random.default_rng(seed)
        # (height, max radius, facing angle, lean, curl, span) per sheet
        self.sheets = [
            (0.235, 0.070, 0.2, 0.18, 1.0, 1.25),
            (0.175, 0.058, 2.3, 0.30, 1.2, 1.10),
            (0.140, 0.050, 4.1, 0.35, 0.9, 1.35),
            (0.095, 0.036, 5.3, 0.45, 1.3, 1.05),
        ]
        self.ph = rng.uniform(0, TAU, 16)

    def sheet(self, k, nu, nv):
        H, Rm, face, lean, curl, span = self.sheets[k]
        p = self.ph[k * 3:k * 3 + 3]
        u = np.linspace(0, 1, nu)
        v = np.linspace(0, 1, nv)
        U, V = np.meshgrid(u, v)
        # the sheet wraps part way round its own axis; the opening widens as it rises
        phi = math.pi * span * (0.9 + 0.5 * V)
        th = (U - 0.5) * phi + 0.9 * V ** 2 * (1 if k % 2 else -1)  # the sheet twists as it rises
        r = 0.006 + Rm * V ** 1.1
        # outward curl of the upper margin and a ruffle that grows toward it
        r = r + 0.05 * curl * V ** 2.6 * (0.5 + 0.5 * np.cos(th * 1.3 + p[0]))
        wob = 2 * fbm(U * 4, V, 2, self.seed + k)
        ruffle = V ** 2.5 * (0.016 * np.sin(5 * th + p[1] + wob) + 0.007 * np.sin(11 * th + p[2] + wob)
                             + 0.0025 * np.sin(23 * th + p[2]))
        # the free side edges roll inward
        edge = smoothstep(0.82, 1.0, np.abs(2 * U - 1))
        r = r - 0.012 * edge * V ** 0.7
        z = H * V + 0.03 * curl * V ** 3 * np.cos(th + p[0]) - 0.008 * edge * V ** 2 + ruffle * 0.9
        x = r * np.cos(th)
        y = r * np.sin(th) + ruffle * 0.6
        x = x + ruffle * np.cos(th)
        P = np.stack([x, y, z], -1)
        # lean away from the cluster's centre, turn to face
        lean_m = np.array([[math.cos(lean), 0, math.sin(lean)], [0, 1, 0], [-math.sin(lean), 0, math.cos(lean)]])
        P = P @ lean_m.T
        P = P @ rot_z(face).T
        # each sheet springs from a point on the foot
        P = P + np.array([0.012 * math.cos(face), 0.012 * math.sin(face), 0.010])
        return P

    def build(self):
        A = Asset("lattice")
        self.A = A
        res = 1.0 if self.lod == 0 else 0.5
        net = A.slot("net", res=(int(2048 * res), int(1024 * res)), mode="MASK", emissive_strength=4.0)
        foot = A.slot("foot", res=(int(512 * res), int(512 * res)), mode="OPAQUE", double_sided=False,
                      emissive_strength=4.0)
        n = len(self.sheets)
        for k in range(n):
            reg = net.region(k / n, (k + 1) / n, self._painter(k), bump=3.0)
            nu = int(96 * self.q) if k < 2 else int(72 * self.q)
            nv = int(64 * self.q) if k < 2 else int(48 * self.q)
            P = self.sheet(k, nu, nv)
            # u across the sheet; flip so cross(Pu, Pv) faces out of the funnel
            net.add_grid(P[:, ::-1], reg)
            # emitters: a few bright knots near the rim
            for j in range(5):
                i = int((0.15 + 0.7 * j / 4) * (nu - 1))
                A.emitters.append(list(P[-2, i]) + [1.0, j / 4, 0.0015])
        # the foot: a lumpy, squashed mound
        prof_v = np.linspace(0, 1, int(20 * self.q) + 4)
        pr = 0.034 * np.sin(np.minimum(prof_v * 1.15, 1.0) * math.pi / 2) ** 0.6 * (1 - prof_v ** 6)
        pz = 0.022 * (1 - (1 - prof_v) ** 2) - 0.004
        pr = pr[::-1]
        pz = pz[::-1]

        def lumps(th, v):
            return 1 + 0.18 * (fbm(th[None, :] / TAU * 5, v[:, None] * 2, 3, self.seed + 9, px=5) - 0.5) * 2
        P, _ = revolve(np.maximum(pr, 1e-4), pz, int(48 * self.q) + 8, lumps)
        foot.add_grid(P, foot.region(0, 1, self.paint_foot, bump=4.0), wrap_u=True)
        return A

    def _painter(self, k):
        def paint(S, T):
            sd = self.seed + 100 * k
            # cells grow toward the rim; more of them across the wider upper sheet
            y = T * 16 + 9 * T ** 1.6
            cells_u = 22 + 4 * (k < 2)
            wall, knot, h, F1 = lace(S, y / 25.0 + 0 * T, cells_u, 25.0, sd, wall=(0.05, 0.11), px=False)
            # a continuous strand along every free edge, and solid young tissue at the base
            dist_edge = np.minimum(np.minimum(S, 1 - S) * 2.6, (1 - T) * 1.4)
            rimstrand = 1 - smoothstep(0.012, 0.03, dist_edge)
            young = 1 - smoothstep(0.04, 0.16, T)
            open_cells = (h < 0.92 - 0.6 * young)
            solid = np.maximum(np.maximum(wall, rimstrand), young * (1 - open_cells))
            # the margin is ragged: a few rim cells torn open
            alpha = (solid > 0.5).astype(float) * (T < 0.985)
            mott = fbm(S * 12, T * 12, 3, sd + 5)
            up = smoothstep(0.1, 1.0, T)
            base = lerp(np.array([0.008, 0.01, 0.03]), np.array([0.025, 0.03, 0.09]), 0.3 * wall + 0.5 * up)
            hue = lerp(BLUE, VIOLET, 0.3 + 0.6 * fbm(S * 3, T * 3, 2, sd + 6))
            e = (col(hue, wall * (0.03 + 0.17 * up ** 1.3) * (0.5 + 0.8 * mott))
                 + col(lerp(CYAN, VIOLET, 0.45), knot * wall * (0.08 + 0.32 * up))
                 + col(lerp(VIOLET, CYAN, 0.5), rimstrand * 0.28))
            height = wall + 0.6 * knot + 0.3 * rimstrand
            return dict(base=base, emis=e, alpha=alpha, height=height, rough=0.35 + 0.2 * mott, ao=0.6 + 0.4 * T)
        return paint

    def paint_foot(self, S, T):
        mott = fbm(S * 30, T * 12, 4, self.seed + 77, px=30)
        dots = smoothstep(0.82, 0.95, fbm(S * 90, T * 30, 2, self.seed + 78, px=90))
        base = TISSUE_DARK * (0.8 + 0.8 * mott)[..., None]
        e = col(VIOLET, dots * 0.18 * T)
        return dict(base=base, emis=e, height=mott + 0.5 * dots, rough=0.8, ao=0.4 + 0.6 * T)


def make(lod=0, seed=11):
    return Lattice(seed, lod).build()

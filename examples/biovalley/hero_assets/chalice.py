"""Archetype 3, the CHALICE: a cluster of folded trumpets.

Three trumpets of different ages rise from one point. Each is a slender velvet stem that flares into a deep folded bell.
The lip is asymmetric, like a spathe: it rises into a hood on one side and rolls outward and down on the other. Seven
deep folds run down into the throat. The wall has two sheets:
  - outside, dark ribbed tissue with forking false gills whose crests catch a faint cyan;
  - inside, a smooth gelatinous lining whose emerald veins converge into the throat, where the light pools.
The lip carries a thin cyan margin.

Units metres, Z up in Blender (Y up exported), origin at the base. Height ~0.19 m.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (CYAN, EMERALD, MINT, TISSUE_DARK, Asset, col, fbm, grow_veins, lerp, raster_segments, ridge,
                 rot_y, rot_z, sample_canvas, smoothstep)

TAU = 2 * math.pi


class Chalice:
    # (height, lip radius, lean, facing, folds)
    TRUMPETS = [(0.185, 0.050, 0.12, 0.3, 7), (0.140, 0.040, 0.32, 2.4, 6), (0.095, 0.030, 0.45, 4.3, 5)]

    def __init__(self, seed=31, lod=0):
        self.seed = seed
        self.lod = lod
        self.q = 1.0 if lod == 0 else 0.4
        vr = np.random.default_rng(seed + 5)
        segs = grow_veins(vr, 14, r0=0.05, step=0.02, wander=0.10, branch_p=0.10, spread=0.4, w0=1.0)
        res = 1024 if lod == 0 else 512
        self.vc, self.vg = raster_segments(segs, res, 0.022)

    def surface(self, k, nth, nv, inner):
        H, R, lean, face, nf = self.TRUMPETS[k]
        rng = np.random.default_rng(self.seed + k)
        ph = rng.uniform(0, TAU, 4)
        v0 = 0.42 if inner else 0.0
        v = np.linspace(v0, 1.0, nv)
        th = -np.linspace(0, TAU, nth, endpoint=False)
        V, TH = np.meshgrid(v, th, indexing="ij")
        bell = smoothstep(0.35, 1.0, V)
        r = 0.0045 * (1 + 0.6 * (1 - smoothstep(0.0, 0.06, V))) + R * V ** 3.4
        z = H * (V ** 0.85)
        # folds deepen toward the lip; their phase wanders
        wob = 1.2 * fbm(TH / TAU * 3, V * 2, 2, self.seed + 10 * k, px=3)
        r = r * (1 + 0.26 * bell ** 1.5 * np.sin(nf * TH + ph[0] + wob))
        # spathe: a hood on one side, a rolled lip on the other
        side = np.cos(TH - ph[1])
        z = z + H * 0.22 * V ** 5 * side
        roll = smoothstep(0.86, 1.0, V) * (0.5 - 0.5 * side)
        r = r + R * 0.35 * roll
        z = z - H * 0.10 * roll ** 2
        # small ruffle on the margin
        r = r * (1 + 0.035 * V ** 8 * np.sin(23 * TH + ph[2]))
        if inner:
            r = np.maximum(r - 0.0014 - 0.003 * (1 - bell), 0.0004)
            z = z + 0.0008
        P = np.stack([r * np.cos(TH), r * np.sin(TH), z], -1)
        M = rot_z(face) @ rot_y(lean)
        P = P @ M.T
        return P, v

    def build(self):
        A = Asset("chalice")
        res = 1.0 if self.lod == 0 else 0.5
        sl = A.slot("bell", res=(int(2048 * res), int(1024 * res)), mode="MASK", emissive_strength=4.0)
        n = len(self.TRUMPETS)
        for k in range(n):
            ro = sl.region(k / n, k / n + 0.5 / n, self.paint_outer, bump=5.0)
            ri = sl.region(k / n + 0.5 / n, (k + 1) / n, self.paint_inner, bump=3.0)
            nth = int((160 if k == 0 else 128) * self.q)
            Po, _ = self.surface(k, nth, int(64 * self.q), False)
            # clockwise u faces a rising wall inward: flip the outer sheet's u so it faces out
            sl.add_grid(Po[:, ::-1], ro, wrap_u=True)
            Pi, _ = self.surface(k, nth, int(40 * self.q), True)
            sl.add_grid(Pi, ri, wrap_u=True)  # the lining faces inward, rows throat -> lip
            # the throat's light, for the ecosystem's glints
            A.emitters.append(list(Pi[0].mean(0)) + [0.0, 0.0, 0.004])
        self.A = A
        return A

    def _rim_alpha(self, S, T):
        return (T < 0.985 + 0.012 * fbm(S * 300, 1.0, 2, self.seed + 3, px=300)).astype(float)

    def paint_outer(self, S, T):
        sd = self.seed + 40
        bell = smoothstep(0.35, 1.0, T)
        # false gills: forking ridges running down the bell
        rid = ridge(S * 38, T * 3.0, 3, sd, px=38)
        rib = smoothstep(0.80, 0.97, rid) * bell
        mott = fbm(S * 30, T * 20, 3, sd + 1, px=30)
        fuzz = fbm(S * 200, T * 120, 2, sd + 2, px=200)
        base = lerp(TISSUE_DARK, np.array([0.02, 0.05, 0.05]), 0.3 * bell + 0.4 * rib)
        base = base * (0.7 + 0.6 * mott)[..., None]
        lip = smoothstep(0.968, 0.997, T) * (0.3 + 0.9 * fbm(S * 40, 2.0, 2, self.seed + 8, px=40))
        e = (col(CYAN, rib * 0.10 * bell ** 2 * (0.5 + mott)) + col(CYAN, lip ** 2 * 0.35)
             + col(EMERALD, 0.002 + 0.01 * bell ** 3))
        height = 0.8 * rib + 0.3 * mott + 0.15 * fuzz
        rough = 0.8 - 0.25 * bell
        return dict(base=base, emis=e, alpha=self._rim_alpha(S, T), height=height, rough=rough,
                    ao=0.5 + 0.5 * bell)

    def paint_inner(self, S, T):
        sd = self.seed + 60
        X = T * np.cos(-TAU * S)
        Y = T * np.sin(-TAU * S)
        c = sample_canvas(self.vc, X, Y)
        g = sample_canvas(self.vg, X, Y)
        mott = fbm(X * 8 + 3, Y * 8, 3, sd)
        throat = (1 - smoothstep(0.0, 0.55, T)) ** 2
        lip = smoothstep(0.968, 0.997, T) * (0.3 + 0.9 * fbm(S * 40, 2.0, 2, self.seed + 8, px=40))
        base = lerp(np.array([0.03, 0.10, 0.06]), np.array([0.015, 0.04, 0.04]), T) + col([0.02, 0.06, 0.03], c)
        e = (col(lerp(MINT, EMERALD, T), throat * 0.75)
             + col(EMERALD, c * (0.12 + 0.5 * (1 - T)) * (0.6 + 0.6 * mott))
             + col(EMERALD, g ** 2 * 0.05) + col(CYAN, lip ** 2 * 0.3)
             + col(EMERALD, 0.004 + 0.012 * (1 - T)))
        height = 0.6 * c + 0.2 * mott
        return dict(base=base, emis=e, alpha=self._rim_alpha(S, T), height=height, rough=0.22 + 0.12 * mott,
                    ao=0.4 + 0.6 * T)


def make(lod=0, seed=31):
    return Chalice(seed, lod).build()

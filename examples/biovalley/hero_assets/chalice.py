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
    TRUMPETS = [(0.175, 0.046, 0.22, 0.3, 7), (0.145, 0.040, 0.42, 1.55, 6), (0.125, 0.035, 0.52, 2.85, 6),
                (0.100, 0.030, 0.60, 4.05, 5), (0.075, 0.024, 0.72, 5.25, 5)]

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
        # a slender stem that opens into a deep cup, then a lip that flares out all the way round
        flare = smoothstep(0.82, 1.0, V) ** 2
        r = 0.0035 * (1 + 0.6 * (1 - smoothstep(0.0, 0.06, V))) + R * V ** 2.6 + R * 0.55 * flare
        z = H * (V ** 0.85) - H * 0.07 * flare
        # folds deepen toward the lip; their phase wanders
        wob = 1.2 * fbm(TH / TAU * 3, V * 2, 2, self.seed + 10 * k, px=3)
        r = r * (1 + 0.17 * bell ** 1.5 * np.sin(nf * TH + ph[0] + wob))
        z = z + H * 0.05 * flare * np.sin(nf * TH + ph[0] + wob)  # the folds crimp the lip
        # a slight hood on one side only
        side = np.cos(TH - ph[1])
        z = z + H * 0.06 * V ** 5 * side
        # small ruffle on the margin
        r = r * (1 + 0.03 * V ** 8 * np.sin(23 * TH + ph[2]))
        if inner:
            r = np.maximum(r - 0.0014 - 0.003 * (1 - bell), 0.0004)
            r = r * smoothstep(v0, v0 + 0.1, V) ** 0.5  # the lining closes to a point at the bottom of the throat
            z = z + 0.0008
        P = np.stack([r * np.cos(TH), r * np.sin(TH), z], -1)
        M = rot_z(face) @ rot_y(lean)
        P = P @ M.T
        return P, v

    def build(self):
        A = Asset("chalice")
        res = 1.0 if self.lod == 0 else 0.5
        out = A.slot("bell", res=(int(2048 * res), int(1024 * res)), mode="MASK", emissive_strength=4.0)
        lin = A.slot("throat", res=(int(2048 * res), int(1024 * res)), mode="MASK", emissive_strength=4.0)
        n = len(self.TRUMPETS)
        for k in range(n):
            ro = out.region(k / n, (k + 1) / n, self.paint_outer, bump=5.0)
            ri = lin.region(k / n, (k + 1) / n, self.paint_inner, bump=3.0)
            nth = int((128 if k < 2 else 96) * self.q)
            Po, _ = self.surface(k, nth, int(56 * self.q), False)
            # clockwise u faces a rising wall inward: flip the outer sheet's u so it faces out
            out.add_grid(Po[:, ::-1], ro, wrap_u=True)
            Pi, _ = self.surface(k, nth, int(36 * self.q), True)
            lin.add_grid(Pi, ri, wrap_u=True)  # the lining faces inward, rows throat -> lip
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
        thin = smoothstep(0.8, 1.0, T)  # the flared lip is thin, translucent tissue
        base = lerp(TISSUE_DARK, np.array([0.02, 0.05, 0.05]), 0.3 * bell + 0.4 * rib)
        base = base * (0.7 + 0.6 * mott)[..., None] + col([0.015, 0.045, 0.04], thin)
        lip = smoothstep(0.975, 0.998, T) * (0.4 + 0.8 * fbm(S * 40, 2.0, 2, self.seed + 8, px=40))
        # the throat's light shows through the thin lip
        e = (col(CYAN, rib * 0.12 * bell ** 2 * (0.5 + mott)) + col(CYAN, lip ** 2 * 0.4)
             + col(lerp(EMERALD, CYAN, thin), thin ** 2 * 0.035 * (0.5 + mott))
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
        throat = (1 - smoothstep(0.0, 0.75, T)) ** 1.6
        lip = smoothstep(0.968, 0.997, T) * (0.3 + 0.9 * fbm(S * 40, 2.0, 2, self.seed + 8, px=40))
        base = lerp(np.array([0.03, 0.10, 0.06]), np.array([0.015, 0.04, 0.04]), T) + col([0.02, 0.06, 0.03], c)
        thin = smoothstep(0.8, 1.0, T)
        e = (col(lerp(MINT, EMERALD, T), throat * 0.9)
             + col(EMERALD, c * (0.2 + 0.55 * (1 - T)) * (0.6 + 0.6 * mott))
             + col(CYAN, thin ** 2 * 0.06 * (0.5 + mott))
             + col(EMERALD, g ** 2 * 0.05) + col(CYAN, lip ** 2 * 0.3)
             + col(EMERALD, 0.004 + 0.012 * (1 - T)))
        height = 0.6 * c + 0.2 * mott
        return dict(base=base, emis=e, alpha=self._rim_alpha(S, T), height=height, rough=0.22 + 0.12 * mott,
                    ao=0.4 + 0.6 * T)


def make(lod=0, seed=31):
    return Chalice(seed, lod).build()

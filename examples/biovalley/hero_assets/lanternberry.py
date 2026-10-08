"""Archetype 4, the LANTERNBERRY: small luminous bodies hung on an irregular, gnarled branching body.

A low, knotted body of dark, lumpy limbs forks three times like driftwood. Pendant fruits of varied size hang from
its limbs on thin curved pedicels, larger toward the ends. Each fruit is a five-ribbed lantern husk, skeletonised
into a lace between its ribs, with a glowing berry inside. Berries come in three hues (cyan, emerald, a rare blue),
and the light reaches the husk's ribs from within. The limbs carry faint luminous lenticels.

Units metres, Z up in Blender (Y up exported), origin at the base. Height ~0.2 m.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (BLUE, CYAN, EMERALD, MINT, TISSUE_DARK, Asset, add_branches, bezier, col, fbm, grow_branches,
                 lace, lerp, smoothstep, tube_grid)

TAU = 2 * math.pi


class Lanternberry:
    def __init__(self, seed=41, lod=0):
        self.seed = seed
        self.lod = lod

    def build(self):
        rng = np.random.default_rng(self.seed)
        lod0 = self.lod == 0
        A = Asset("lanternberry")
        res = 1.0 if lod0 else 0.5
        body = A.slot("body", res=(int(1024 * res), int(1024 * res)), mode="OPAQUE", double_sided=False,
                      emissive_strength=4.0)
        husk = A.slot("husk", res=(int(1024 * res), int(1024 * res)), mode="MASK", emissive_strength=4.0)
        berry = A.slot("berry", res=(int(256 * res), int(256 * res)), mode="OPAQUE", double_sided=False,
                       emissive_strength=4.0)
        br = []
        for k in range(3):
            ang = TAU * k / 3 + rng.uniform(-0.5, 0.5)
            tilt = rng.uniform(0.2, 0.5)
            d = [math.sin(tilt) * math.cos(ang), math.sin(tilt) * math.sin(ang), math.cos(tilt)]
            br += grow_branches(rng, [0, 0, -0.005], d, rng.uniform(0.06, 0.085), 0.0068, max_gen=3,
                                children=(1, 3), spread=(0.35, 0.8), shrink=0.74, rshrink=0.68, curl=0.6, up=0.3,
                                segs=16 if lod0 else 6, crozier=0.0, min_r=0.0016, child_t=(0.3, 1.0))

        def lumpy(th, t):  # knots and swellings along the limbs
            return (1 + 0.28 * smoothstep(0.55, 0.9, fbm(th[None, :] * 0.5, t[:, None] * 9, 3, self.seed + 2))
                    + 0.08 * (fbm(th[None, :] * 1.5, t[:, None] * 30, 2, self.seed + 3) - 0.5))
        rb = body.region(0, 0.8, self.paint_body, bump=5.0)
        add_branches(body, rb, br, lambda b: max([14, 11, 9, 7][min(b.gen, 3)] - (0 if lod0 else 4), 5), lumpy,
                     s_width=0.3, seed=self.seed)
        rp = body.region(0.8, 1.0, self.paint_pedicel, bump=2.0)
        rh = husk.region(0, 1, self.paint_husk, bump=3.0)
        hues = [berry.region(0, 0.34, self._paint_berry(CYAN)), berry.region(0.34, 0.67, self._paint_berry(EMERALD)),
                berry.region(0.67, 1.0, self._paint_berry(BLUE))]
        # fruit hang along the outer limbs
        nfruit = 0
        for b in br:
            if b.gen == 0:
                continue
            m = int(rng.random() < 0.55) + int(b.tip and rng.random() < 0.85)
            for _ in range(m):
                t = rng.uniform(0.45, 1.0) if not b.tip else rng.uniform(0.7, 1.0)
                i = min(int(t * (len(b.pts) - 1)), len(b.pts) - 1)
                p0 = b.pts[i]
                out = b.pts[i] - b.pts[max(i - 1, 0)]
                side = np.cross(out, [0, 0, 1.0])
                side /= max(np.linalg.norm(side), 1e-9)
                side *= rng.choice([-1, 1])
                L = rng.uniform(0.012, 0.03)
                R = rng.uniform(0.004, 0.0085) * (1.35 if b.tip else 1.0)
                end = p0 + side * L * 0.5 + np.array([0, 0, -L])
                ped = bezier(p0, p0 + side * L * 0.5 + [0, 0, 0.004], end + [0, 0, L * 0.3], end, 10 if lod0 else 4)
                rad = np.linspace(0.0007, 0.00045, len(ped))
                body.add_grid(tube_grid(ped, rad, 5 if lod0 else 3), rp, wrap_u=True)
                self.fruit(husk, rh, berry, hues[0 if rng.random() < 0.45 else (1 if rng.random() < 0.8 else 2)],
                           end, R, rng, lod0)
                A.emitters.append(list(end + np.array([0, 0, -1.1 * R])) + [0.5, 0.5, R * 0.6])
                nfruit += 1
        self.A = A
        return A

    def fruit(self, husk, rh, berry, rbe, top, R, rng, lod0):
        nth = 20 if lod0 else 8
        nv = 13 if lod0 else 6
        th = -np.linspace(0, TAU, nth, endpoint=False) + rng.uniform(0, TAU)
        v = np.linspace(0, 1, nv)
        V, TH = np.meshgrid(v, th, indexing="ij")
        # lantern: five papery ribs, widest a little below the middle, a drawn-out point at the bottom
        prof = np.sin(math.pi * np.clip(V, 0, 1) ** 0.85) ** 0.9
        lobes = 1 + 0.16 * np.cos(5 * TH) ** 2 - 0.08
        r = R * prof * lobes + 0.0004 * (1 - V)
        z = -2.3 * R * V
        P = np.stack([r * np.cos(TH), r * np.sin(TH), z], -1) + top
        # a little sway: the lantern leans
        lean = rng.normal(0, 0.15, 2)
        P[..., 0] += lean[0] * (P[..., 2] - top[2])
        P[..., 1] += lean[1] * (P[..., 2] - top[2])
        # clockwise u with rows going down faces outward already
        husk.add_grid(P, rh, wrap_u=True)
        # the berry: a slightly pear-shaped core inside
        bv = np.linspace(0, 1, 10 if lod0 else 5)
        bth = -np.linspace(0, TAU, 14 if lod0 else 6, endpoint=False)
        BV, BT = np.meshgrid(bv, bth, indexing="ij")
        br = 0.52 * R * np.sin(math.pi * BV) ** 0.8 * (1 + 0.15 * BV)
        bz = -1.15 * R - 0.55 * R * np.cos(math.pi * BV) * 1.0
        Q = np.stack([br * np.cos(BT), br * np.sin(BT), bz], -1) + top
        Q[..., 0] += lean[0] * (Q[..., 2] - top[2])
        Q[..., 1] += lean[1] * (Q[..., 2] - top[2])
        berry.add_grid(Q[::-1], rbe, wrap_u=True)

    def paint_body(self, S, T):
        sd = self.seed + 70
        mott = fbm(S * 40, T * 50, 4, sd)
        bark = fbm(S * 120, T * 25, 2, sd + 1)
        lent = smoothstep(0.83, 0.93, fbm(S * 160, T * 120, 2, sd + 2))
        base = TISSUE_DARK * (0.8 + 0.9 * mott)[..., None] + col([0.01, 0.02, 0.02], bark)
        e = col(lerp(EMERALD, CYAN, T), lent * (0.05 + 0.3 * T ** 2)) + col(EMERALD, 0.002 * T)
        return dict(base=base, emis=e, height=0.6 * mott + 0.4 * bark + 0.4 * lent, rough=0.85 - 0.1 * lent,
                    ao=0.4 + 0.6 * T)

    def paint_pedicel(self, S, T):
        base = lerp(TISSUE_DARK, np.array([0.02, 0.05, 0.05]), T)
        return dict(base=base, emis=col(CYAN, 0.03 * T ** 2), height=np.zeros_like(S), rough=0.6, ao=1.0)

    def paint_husk(self, S, T):
        sd = self.seed + 80
        # five ribs (u around), a net between them that opens up across the middle of the lantern
        ribc = np.abs(((S * 5) % 1.0) - 0.5) * 2
        rib = 1 - smoothstep(0.06, 0.16, 1 - ribc)
        wall, knot, h, _ = lace(S, T, 30, 16, sd, wall=(0.06, 0.13))
        mid = smoothstep(0.12, 0.35, T) * (1 - smoothstep(0.75, 0.92, T))
        solid = np.maximum(np.maximum(rib, wall), 1 - mid)
        alpha = (solid > 0.5).astype(float)
        mott = fbm(S * 20, T * 10, 2, sd + 1, px=20)
        glow_from_within = np.sin(math.pi * T) ** 1.5
        base = lerp(np.array([0.02, 0.04, 0.035]), np.array([0.06, 0.12, 0.10]), 0.5 * rib + 0.3 * wall)
        e = (col(lerp(EMERALD, CYAN, mott), (rib * 0.25 + wall * 0.12) * glow_from_within)
             + col(MINT, knot * wall * 0.15 * glow_from_within) + col(EMERALD, 0.01 * (1 - mid)))
        return dict(base=base, emis=e, alpha=alpha, height=rib + 0.5 * wall, rough=0.5, ao=1.0)

    def _paint_berry(self, hue):
        def paint(S, T):
            core = np.sin(math.pi * T) ** 0.7
            spots = smoothstep(0.7, 0.9, fbm(S * 12, T * 8, 2, self.seed + 90, px=12))
            base = col(hue, 0.08)
            e = col(lerp(hue, MINT, 0.15 * core), 0.12 + 0.30 * core) + col(MINT, spots * 0.1)
            return dict(base=base, emis=e, height=spots, rough=0.15, ao=1.0)
        return paint


def make(lod=0, seed=41):
    return Lanternberry(seed, lod).build()

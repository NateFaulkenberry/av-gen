"""Two species added in the colour pass.

LACEFAN: three translucent, perforated fans (reference panel 3's lattice sheet), flat and gently rippled rather than
curled. Each fan rises from a short stalk and spreads 90-140 degrees. Radial ribs run from the base to the rim. Between
the ribs is a honeycomb of open cells, smaller near the base and larger toward the rim, held by a continuous rim strand.
Cell walls glow electric blue, brightest toward the rim; the knots where walls meet are brighter still; a faint glow
lives in the solid tissue near the base.

BEADCLUSTER: a pincushion of tiny round beads on hair-thin stems, like the dense bead clusters of panels 1 and 3. Sixty
or so stems radiate from a small knob in a dome; each carries a 1.5-3.5 mm bead, emerald or blue, bead by bead.

Units metres, Z up in Blender (Y up exported), origin at the base.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (BLUE, CYAN, EMERALD, MINT, TISSUE_DARK, VIOLET, Asset, bezier, col, fbm, frame_to, lace, lerp,
                 smoothstep, tube_grid)

TAU = 2 * math.pi
ELECTRIC = np.array([0.03, 0.15, 1.0])


class Lacefan:
    # (radius, spread (rad), facing, tilt back, ripple)
    FANS = [(0.30, 2.3, 0.2, 0.30, 1.0), (0.24, 1.9, 1.3, 0.45, 1.2), (0.19, 1.6, -0.9, 0.55, 0.8)]

    def __init__(self, seed=151, lod=0):
        self.seed = seed
        self.lod0 = lod == 0

    def build(self):
        A = Asset("lacefan")
        res = 1.0 if self.lod0 else 0.5
        sl = A.slot("fan", res=(int(2048 * res), int(1024 * res)), mode="MASK", emissive_strength=4.0)
        st = A.slot("stalk", res=(int(256 * res), int(256 * res)), mode="OPAQUE", double_sided=False,
                    emissive_strength=4.0)
        rs = st.region(0, 1, self.paint_stalk, bump=2.0)
        n = len(self.FANS)
        for k, (R, span, face, tilt, rip) in enumerate(self.FANS):
            reg = sl.region(k / n, (k + 1) / n, self._paint_fan(k), bump=3.0)
            nu = (90 if self.lod0 else 24)
            nv = (56 if self.lod0 else 14)
            u = np.linspace(-0.5, 0.5, nu)
            v = np.linspace(0, 1, nv)
            U, V = np.meshgrid(u, v)
            th = U * span
            r = 0.015 + R * V * (1 + 0.06 * np.sin(7 * th + k) * V ** 2)
            # the fan lies in its own x-z plane: x across, z up; ripple and a slight cup go along y
            x = r * np.sin(th)
            z = r * np.cos(th)
            y = (0.018 * rip * V ** 2 * np.sin(6 * th + 1.7 * k) + 0.03 * V ** 2 * np.cos(th) * 0.6
                 + 0.006 * V ** 4 * np.sin(23 * th))
            P = np.stack([x, y, z], -1)
            ct, sn = math.cos(tilt), math.sin(tilt)
            P = P @ np.array([[1, 0, 0], [0, ct, sn], [0, -sn, ct]]).T  # tilt back
            cf, sf = math.cos(face), math.sin(face)
            P = P @ np.array([[cf, -sf, 0], [sf, cf, 0], [0, 0, 1]]).T
            P = P + np.array([0.0, 0.0, 0.035])
            sl.add_grid(P, reg)
            for f in (-0.35, 0.0, 0.35):
                A.emitters.append(list(P[-2, int((f + 0.5) * (nu - 1))]) + [1.0, f + 0.5, 0.002])
        stem = bezier([0, 0, -0.005], [0, 0, 0.015], [0, 0, 0.03], [0, 0, 0.045], 8 if self.lod0 else 3)
        st.add_grid(tube_grid(stem, np.linspace(0.012, 0.007, len(stem)), 12 if self.lod0 else 6), rs, wrap_u=True)
        self.A = A
        return A

    def _paint_fan(self, k):
        def paint(S, T):
            sd = self.seed + 40 * k
            # cells grow toward the rim
            y = T * 14 + 10 * T ** 1.5
            wall, knot, h, F1 = lace(S, y / 24.0, 34, 24.0, sd, wall=(0.05, 0.11), px=False, warp=0.5)
            # radial ribs, slightly wandering
            rw = 0.035 * (1 - 0.6 * T)  # ribs taper toward the rim
            ribs = 1 - smoothstep(rw, rw + 0.02, np.abs(((S * 9 + 0.15 * fbm(S * 3, T * 3, 2, sd + 1)) % 1.0) - 0.5))
            rim = 1 - smoothstep(0.015, 0.03, np.minimum(1 - T, np.minimum(S, 1 - S) * 0.6))
            young = 1 - smoothstep(0.03, 0.12, T)
            solid = np.maximum(np.maximum(wall, ribs), np.maximum(rim, young))
            alpha = (solid > 0.5).astype(float) * (T < 0.99)
            up = smoothstep(0.05, 1.0, T)
            mott = fbm(S * 10, T * 8, 2, sd + 2)
            base = lerp(np.array([0.01, 0.015, 0.05]), np.array([0.04, 0.06, 0.16]), 0.4 * wall + 0.4 * up)
            hue = lerp(ELECTRIC, BLUE, mott)
            e = (col(hue, wall * (0.07 + 0.38 * up ** 1.2) * (0.6 + 0.6 * mott))
                 + col(lerp(BLUE, CYAN, 0.35), knot * wall * (0.2 + 0.6 * up))
                 + col(lerp(ELECTRIC, CYAN, 0.2), ribs * (0.1 + 0.25 * up)) + col(lerp(ELECTRIC, CYAN, 0.4), rim * 0.4)
                 + col(BLUE, young * 0.06))
            return dict(base=base, emis=e, alpha=alpha, height=wall + 0.7 * ribs + 0.5 * knot, rough=0.35,
                        ao=0.6 + 0.4 * T)
        return paint

    def paint_stalk(self, S, T):
        base = lerp(TISSUE_DARK, np.array([0.02, 0.03, 0.07]), T)
        return dict(base=base, emis=col(BLUE, 0.02 + 0.1 * T ** 2), height=fbm(S * 20, T * 10, 2, self.seed, px=20),
                    rough=0.7, ao=0.5 + 0.5 * T)


class Beadcluster:
    """A colony of translucent sacs on a branching, coral-like body (archetype D): a gelatinous pad, three or four
    stems that fork twice or three times, small sacs at the tips and larger ones swelling at the forks, a few
    sagging strands that link neighbouring sacs. Every sac is lit from inside to its own degree."""

    def __init__(self, seed=161, lod=0):
        self.seed = seed
        self.lod0 = lod == 0

    def build(self):
        from lib import add_branches, grow_branches, normalize
        rng = np.random.default_rng(self.seed)
        lod0 = self.lod0
        A = Asset("beadcluster")
        res = 1.0 if lod0 else 0.5
        body = A.slot("body", res=(int(512 * res), int(512 * res)), mode="OPAQUE", double_sided=False,
                      emissive_strength=4.0)
        sacs = A.slot("sac", res=(int(512 * res), int(256 * res)), mode="OPAQUE", double_sided=False,
                      emissive_strength=4.0)
        r_body = body.region(0.0, 0.7, self.paint_body, bump=2.0)
        r_strand = body.region(0.7, 1.0, self.paint_strand, bump=1.0)
        r_green = sacs.region(0.0, 0.5, self._paint_sac(EMERALD, 0), bump=1.0)
        r_blue = sacs.region(0.5, 1.0, self._paint_sac(BLUE, 1), bump=1.0)
        br = []
        for k in range(4):
            a = TAU * k / 4 + rng.uniform(-0.3, 0.3)
            tilt = rng.uniform(0.15, 0.55)
            d = [math.sin(tilt) * math.cos(a), math.sin(tilt) * math.sin(a), math.cos(tilt)]
            br += grow_branches(rng, [0.006 * math.cos(a), 0.006 * math.sin(a), 0.004], d, rng.uniform(0.035, 0.05),
                                0.0028, max_gen=3, children=(2, 3), spread=(0.35, 0.75), shrink=0.72, rshrink=0.7,
                                curl=0.35, up=0.25, segs=8 if lod0 else 4, crozier=0.0, min_r=0.0008,
                                child_t=(0.55, 1.0), sink=0.8)
        add_branches(body, r_body, br, lambda b: 7 if lod0 else 4, seed=self.seed)
        # the pad
        V, TH = np.meshgrid(np.linspace(0, 1, 8 if lod0 else 4), -np.linspace(0, TAU, 20 if lod0 else 8, endpoint=False),
                            indexing="ij")
        pr = 0.024 * np.sin(math.pi * (0.5 + 0.5 * V)) * (1 + 0.15 * np.cos(5 * TH)) + 0.001
        pz = 0.012 * V - 0.003
        body.add_grid(np.stack([pr * np.cos(TH), pr * np.sin(TH), pz], -1)[::-1, ::-1], r_body, wrap_u=True,
                      t_range=(0.0, 0.1))
        centres = []
        j = 0

        def sac(c, d, R, elong):
            nonlocal j
            nv, nt = (8, 10) if lod0 else (4, 5)
            V, TH = np.meshgrid(np.linspace(0, 1, nv), -np.linspace(0, TAU, nt, endpoint=False), indexing="ij")
            ph = -math.pi / 2 + math.pi * V
            # a teardrop: wider toward its free end
            rr = R * np.cos(ph) * (0.85 + 0.25 * V)
            B = np.stack([rr * np.cos(TH), rr * np.sin(TH), R * elong * np.sin(ph)], -1)
            B = B @ frame_to(d).T + c
            reg = r_green if rng.random() < 0.5 else r_blue
            sacs.add_grid(B[:, ::-1], reg, wrap_u=True, s_range=((j % 12) / 12, (j % 12 + 1) / 12))
            centres.append(c)
            A.emitters.append(list(c) + [0.5, 0.5, R])
            j += 1
        for b in br:
            d = normalize(b.pts[-1] - b.pts[-2])
            if b.tip:  # small sacs crown the tips
                R = rng.uniform(0.0022, 0.0042)
                sac(b.pts[-1] + d * R * 1.1, d, R, 1.35)
            elif rng.random() < 0.6:  # larger ones swell at the forks
                R = rng.uniform(0.0045, 0.0075)
                i = len(b.pts) - 1
                sac(b.pts[i] + d * R * 0.3, d, R, 1.2)
        # strands between neighbouring sacs
        if lod0:
            C = np.array(centres)
            for _ in range(10):
                i = rng.integers(len(C))
                dist = np.linalg.norm(C - C[i], axis=1)
                dist[i] = 1e9
                k = int(np.argmin(dist + rng.uniform(0, 0.01, len(C))))
                p0, p1 = C[i], C[k]
                mid = (p0 + p1) / 2 - np.array([0, 0, 0.25 * np.linalg.norm(p1 - p0)])
                st = bezier(p0, mid, mid, p1, 8)
                body.add_grid(tube_grid(st, np.full(8, 0.00025), 3), r_strand, wrap_u=True)
        self.A = A
        return A

    def paint_body(self, S, T):
        sd = self.seed + 7
        mott = fbm(S * 12, T * 30, 2, sd)
        up = smoothstep(0.2, 1.0, T)
        base = lerp(np.array([0.02, 0.04, 0.06]), np.array([0.06, 0.12, 0.16]), up * (0.6 + 0.4 * mott))
        e = col(lerp(BLUE, CYAN, up), (0.01 + 0.10 * up ** 2) * (0.5 + 0.6 * mott))
        return dict(base=base, emis=e, height=mott, rough=0.25, ao=0.5 + 0.5 * T)

    def paint_strand(self, S, T):
        return dict(base=col(CYAN, 0.08), emis=col(CYAN, 0.12 + 0.0 * T), height=np.zeros_like(S), rough=0.3,
                    ao=1.0)

    def _paint_sac(self, hue, k):
        def paint(S, T):
            idx = np.floor(S * 12)
            bright = 0.2 + 0.8 * (((idx + 3 * k) * 0.618) % 1.0) ** 1.5  # each sac lit to its own degree
            core = np.sin(math.pi * T) ** 1.4
            skin = smoothstep(0.75, 1.0, np.abs(2 * T - 1))  # the membrane shows at the ends
            base = lerp(np.array([0.05, 0.08, 0.12]), np.array([0.12, 0.2, 0.26]), core)
            e = col(hue, 1.0) * ((0.05 + 0.55 * core) * bright)[..., None] + col(MINT, 0.05 * core ** 8 * bright)
            return dict(base=base, emis=e, height=skin, rough=0.1, ao=1.0)
        return paint


def make_lacefan(lod=0):
    return Lacefan(lod=lod).build()


def make_beadcluster(lod=0):
    return Beadcluster(lod=lod).build()

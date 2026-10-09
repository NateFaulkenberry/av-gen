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
    def __init__(self, seed=161, lod=0):
        self.seed = seed
        self.lod0 = lod == 0

    def build(self):
        rng = np.random.default_rng(self.seed)
        A = Asset("beadcluster")
        res = 1.0 if self.lod0 else 0.5
        sl = A.slot("bead", res=(int(512 * res), int(512 * res)), mode="OPAQUE", double_sided=False,
                    emissive_strength=4.0)
        r_stem = sl.region(0.0, 0.4, self.paint_stem, bump=1.0)
        r_green = sl.region(0.4, 0.7, self._paint_bead(EMERALD), bump=1.0)
        r_blue = sl.region(0.7, 1.0, self._paint_bead(BLUE), bump=1.0)
        n = 64 if self.lod0 else 24
        ga = math.pi * (3 - math.sqrt(5))
        for j in range(n):
            az = j * ga + rng.uniform(-0.2, 0.2)
            el = math.radians(8 + 70 * math.sqrt((j + 0.5) / n)) + rng.uniform(-0.08, 0.08)
            d = np.array([math.sin(el) * math.cos(az), math.sin(el) * math.sin(az), math.cos(el)])
            L = rng.uniform(0.02, 0.06) * (1.1 - 0.35 * math.sin(el))
            p0 = np.array([0, 0, 0.008])
            end = p0 + d * L + np.array([0, 0, -0.15 * L * math.sin(el) ** 2])
            m = 6 if self.lod0 else 3
            stem = bezier(p0, p0 + d * L * 0.4, end - d * L * 0.2 + [0, 0, 0.1 * L], end, m)
            sl.add_grid(tube_grid(stem, np.linspace(0.0005, 0.0003, m), 4 if self.lod0 else 3), r_stem, wrap_u=True)
            R = rng.uniform(0.001, 0.0025)
            nv, nt = (7, 10) if self.lod0 else (4, 5)
            V, TH = np.meshgrid(np.linspace(0, 1, nv), -np.linspace(0, TAU, nt, endpoint=False), indexing="ij")
            ph = -math.pi / 2 + math.pi * V
            B = np.stack([R * np.cos(ph) * np.cos(TH), R * np.cos(ph) * np.sin(TH), R * np.sin(ph)], -1)
            B = B @ frame_to(d).T + end + d * R * 0.8
            sl.add_grid(B[:, ::-1], r_green if rng.random() < 0.5 else r_blue, wrap_u=True)
            A.emitters.append(list(end + d * R * 0.8) + [1.0, 0.5, R])
        # the knob
        V, TH = np.meshgrid(np.linspace(0, 1, 6 if self.lod0 else 3), -np.linspace(0, TAU, 12 if self.lod0 else 6,
                                                                                   endpoint=False), indexing="ij")
        ph = -math.pi / 2 + math.pi * V
        K = np.stack([0.009 * np.cos(ph) * np.cos(TH), 0.009 * np.cos(ph) * np.sin(TH), 0.006 * np.sin(ph) + 0.004], -1)
        sl.add_grid(K[:, ::-1], r_stem, wrap_u=True)
        self.A = A
        return A

    def paint_stem(self, S, T):
        base = lerp(TISSUE_DARK, np.array([0.03, 0.06, 0.07]), T)
        return dict(base=base, emis=col(CYAN, 0.01 + 0.08 * T ** 3), height=np.zeros_like(S), rough=0.5,
                    ao=0.5 + 0.5 * T)

    def _paint_bead(self, hue):
        def paint(S, T):
            core = np.sin(math.pi * T) ** 1.5
            base = col(hue, 0.15)
            e = col(hue, 0.18 + 0.4 * core) + col(MINT, 0.05 * core ** 6)
            return dict(base=base, emis=e, height=np.zeros_like(S), rough=0.15, ao=1.0)
        return paint


def make_lacefan(lod=0):
    return Lacefan(lod=lod).build()


def make_beadcluster(lod=0):
    return Beadcluster(lod=lod).build()

"""Archetype 7, the GLOWMAT: a low moss and fungal mat with fine surface detail.

An irregular cushion, about 0.45 m across and 4 cm high, made of overlapping domed clumps of velvet moss. Through it runs
a mycelium of faint blue-cyan threads that fork outward from the oldest part. Spore pores glow in tight colonies, not
evenly. Three colonies of hair-fine sporophytes stand up from the moss, each with a luminous capsule, and a small
patch of pixie-cup lichens holds a little light in each cup.

Units metres, Z up in Blender (Y up exported), origin at the base (the cushion's centre on the ground).
"""
from __future__ import annotations

import math

import numpy as np

from lib import (BLUE, CYAN, EMERALD, MINT, TISSUE_DARK, Asset, bezier, col, fbm, grow_veins, lerp,
                 raster_segments, sample_canvas, smoothstep, tube_grid, worley, worley3)

TAU = 2 * math.pi


class Mat:
    def __init__(self, seed=71, lod=0, name="glowmat", R0=0.22, dome=0.0, dot_cell=0.0045, cups=True,
                 sporo_scale=1.0):
        self.seed = seed
        self.lod = lod
        self.lod0 = lod == 0
        self.name = name
        self.R0 = R0
        self.dome = dome  # height of one big underlying dome (the large mound variant)
        self.f = R0 / 0.22  # unit-disc frequencies scale with size so the detail keeps its physical size
        self.dot_cell = dot_cell
        self.cups = cups
        self.sporo_scale = sporo_scale
        # the profile unrolled: arc length along the surface as a function of the planar radius, so fine detail
        # painted in (arc length, angle) keeps its shape on a steep mound instead of stretching down its flanks
        rr = np.linspace(0, 1, 513)
        hh = (self.dome * np.clip(1 - rr ** 2, 0, 1) ** 0.55 + 0.010 * self.f * (1 - rr ** 2)) / R0
        ds = np.sqrt(np.diff(rr) ** 2 + np.diff(hh) ** 2)
        arc = np.r_[0, np.cumsum(ds)]
        self.arc_r, self.arc = rr, arc  # arc is in R0 units; arc[-1] >= 1
        vr = np.random.default_rng(seed + 3)
        # two thread networks: long runners from the centre, and local tangles that start anywhere
        segs = grow_veins(vr, 22, r0=0.12, step=0.016, wander=0.45, branch_p=0.3, spread=0.75, w0=0.9, decay=0.75)
        segs2 = grow_veins(vr, 40, r0=0.5, step=0.012, wander=0.6, branch_p=0.3, spread=0.9, w0=0.6, decay=0.7,
                           phase=0.4)
        cres = 2048 if self.lod0 else 1024
        self.mc, self.mg = raster_segments(np.concatenate([segs, segs2]), cres, 0.0045 / self.f ** 0.5)
        rng = np.random.default_rng(seed)
        self.domes = [(rng.uniform(-0.55, 0.55), rng.uniform(-0.55, 0.55), rng.uniform(0.18, 0.4),
                       rng.uniform(0.5, 1.0)) for _ in range(9)]
        self.colonies = [(-0.35, 0.2), (0.3, 0.3), (0.1, -0.4)]

    def outline(self, th):
        return self.R0 * (1 + 0.28 * (fbm(th / TAU * 4, 1.3, 3, self.seed, px=4) - 0.5) * 2)

    def height(self, x, y, r):
        """Height in metres at unit-disc coords (x, y) and normalised radius r."""
        h = 0.010 * self.f * (1 - r ** 2) + self.dome * np.clip(1 - r ** 2, 0, 1) ** 0.55
        for cx, cy, rad, amp in self.domes:
            d = np.hypot(x - cx, y - cy) / rad
            h = h + 0.032 * self.f * amp * np.clip(1 - d ** 2, 0, 1) ** 0.7
        F1, F2, _, _ = worley(x * 22 * self.f, y * 22 * self.f, self.seed + 5)
        clump = 0.0035 * self.f ** 0.5 * (1 - np.clip(F1, 0, 1) ** 2)
        h = h + clump
        return h * smoothstep(1.0, 0.82, r) - 0.004 * smoothstep(0.85, 1.0, r)

    def build(self):
        A = Asset(self.name)
        self.A = A
        lod0 = self.lod0
        res = 1.0 if lod0 else 0.5
        mat = A.slot("moss", res=(int(2048 * res), int(1024 * res)), mode="OPAQUE", double_sided=False,
                     emissive_strength=4.0)
        sp = A.slot("sporophyte", res=(int(512 * res), int(512 * res)), mode="OPAQUE", double_sided=True,
                    emissive_strength=4.0)
        nth = (220 if lod0 else 72) + (60 if self.dome else 0)
        nr = (70 if lod0 else 22) + (20 if self.dome else 0)
        th = -np.linspace(0, TAU, nth, endpoint=False)  # clockwise with v outward: faces up
        rr = np.linspace(0.0, 1.0, nr) ** 0.8
        RR, TH = np.meshgrid(rr, th, indexing="ij")
        x = RR * np.cos(TH)
        y = RR * np.sin(TH)
        Ro = self.outline(TH)
        h = self.height(x, y, RR)
        P = np.stack([RR * Ro * np.cos(TH), RR * Ro * np.sin(TH), h], -1)
        mat.add_grid(P, mat.region(0, 1, self.paint_moss, bump=5.0), wrap_u=True)
        self.surface = (RR, TH, P)

        rng = np.random.default_rng(self.seed + 9)
        r_st = sp.region(0, 0.7, self.paint_sporo, bump=1.0)
        r_cup = sp.region(0.7, 1.0, self.paint_cup, bump=2.0)
        nper = (70 if lod0 else 18)
        for (cx, cy) in self.colonies:
            for j in range(nper):
                ang = rng.uniform(0, TAU)
                d = abs(rng.normal(0, 0.13))
                ux, uy = cx + d * math.cos(ang), cy + d * math.sin(ang)
                p = self.point(ux, uy)
                if p is None:
                    continue
                Ls = rng.uniform(0.008, 0.024) * (1.2 - d) * self.sporo_scale
                lean = rng.normal(0, 0.25, 2)
                tip = p + np.array([lean[0] * Ls, lean[1] * Ls, Ls])
                curve = bezier(p, p + [0, 0, Ls * 0.4], tip + [lean[0] * Ls * 0.3, lean[1] * Ls * 0.3, -Ls * 0.1],
                               tip, 8 if lod0 else 3)
                t = np.linspace(0, 1, len(curve))
                rad = 0.00025 + 0.0007 * smoothstep(0.8, 0.92, t) * (1 - smoothstep(0.94, 1.0, t)) ** 0.5
                rad[-1] = 0.00015
                sp.add_grid(tube_grid(curve, rad, 4 if lod0 else 3), r_st, wrap_u=True,
                            s_range=(j % 10 / 10, (j % 10 + 1) / 10))
                if j % 4 == 0:
                    A.emitters.append(list(curve[-2]) + [1.0, 0.5, 0.0008])
        # pixie cups
        for j in range((14 if lod0 else 5) if self.cups else 0):
            ang = rng.uniform(0, TAU)
            d = abs(rng.normal(0, 0.08))
            p = self.point(-0.15 + d * math.cos(ang), -0.15 + d * math.sin(ang))
            if p is None:
                continue
            H = rng.uniform(0.006, 0.013)
            R = H * rng.uniform(0.35, 0.5)
            nv, nt = (8, 12) if lod0 else (4, 6)
            v = np.linspace(0, 1, nv)
            tt = -np.linspace(0, TAU, nt, endpoint=False)
            V, TT = np.meshgrid(v, tt, indexing="ij")
            r = 0.0009 + R * smoothstep(0.45, 1.0, V) ** 1.5
            z = H * V
            Q = np.stack([r * np.cos(TT), r * np.sin(TT), z], -1) + p
            sp.add_grid(Q[:, ::-1], r_cup, wrap_u=True)
            A.emitters.append(list(p + [0, 0, H * 0.85]) + [0.9, 0.5, R * 0.6])
        return A

    def point(self, ux, uy):
        """The surface point over unit-disc coords (ux, uy), or None outside the cushion."""
        r = math.hypot(ux, uy)
        if r > 0.85:
            return None
        th = math.atan2(uy, ux)
        Ro = float(self.outline(np.array(th)))
        h = float(self.height(np.array(ux), np.array(uy), np.array(r)))
        return np.array([r * Ro * math.cos(th), r * Ro * math.sin(th), h])

    def paint_moss(self, S, T):
        sd = self.seed + 20
        f = self.f
        Tr = T ** 0.8  # rows are spaced as rr = t ** 0.8 in the geometry
        x = Tr * np.cos(-TAU * S)
        y = Tr * np.sin(-TAU * S)
        F1, F2, _, h = worley(x * 22 * f, y * 22 * f, self.seed + 5)
        dome = 1 - np.clip(F1, 0, 1) ** 2
        ra = np.interp(Tr, self.arc_r, self.arc)  # unrolled radius for the fine detail
        xa = ra * np.cos(-TAU * S)
        ya = ra * np.sin(-TAU * S)
        # the surface's own position (in R0 units) for the detail that must not stretch
        Ro = self.outline(-TAU * S) / self.R0
        X3, Y3 = x * Ro, y * Ro
        Z3 = self.height(x, y, Tr) / self.R0
        f1, h_head = worley3(X3 * 140 * f, Y3 * 140 * f, Z3 * 140 * f, sd + 1)  # individual moss heads
        hh = h_head
        heads = 1 - smoothstep(0.0, 0.55, f1)
        fuzz = fbm(xa * 400 * f, ya * 400 * f, 2, sd + 2)
        myc = sample_canvas(self.mc, x, y)
        mycg = sample_canvas(self.mg, x, y)
        patch = smoothstep(0.35, 0.65, fbm(x * 4 * f + 2, y * 4 * f, 3, sd + 3))
        # glowing spore dots: a speckle over the mounds whose density comes and goes in drifts
        drift = smoothstep(0.35, 0.7, fbm(x * 4 * f + 9, y * 4 * f, 3, sd + 4))
        dc = self.R0 / self.dot_cell
        g1, gh = worley3(X3 * dc, Y3 * dc, Z3 * dc, sd + 6, jitter=0.95)
        # a few large dots and many small ones, dense in drifts and sparse between them
        size = 0.10 + 0.42 * ((gh * 29.0) % 1.0) ** 3
        dots = (1 - smoothstep(size * 0.55, size, g1)) * (gh < 0.06 + 0.85 * drift ** 1.5) * (0.35 + 0.65 * dome)
        dot_hue = np.where((gh * 7.0) % 1.0 < 0.45, 1.0, 0.0)  # emerald or blue, dot by dot
        bright = 0.2 + 0.8 * ((gh * 13.0) % 1.0) ** 2 + 0.6 * (((gh * 29.0) % 1.0) > 0.85)  # the big ones burn
        hue_mix = fbm(x * 3 * f, y * 3 * f + 5, 2, sd + 7)
        base = lerp(np.array([0.006, 0.014, 0.012]), np.array([0.02, 0.05, 0.035]), 0.4 * dome + 0.5 * heads * h)
        base = base * (0.7 + 0.5 * fuzz)[..., None]
        edge = smoothstep(0.88, 1.0, T)
        base = base * (1 - edge)[..., None] + col(TISSUE_DARK, edge)
        dot_col = EMERALD[None, None, :] * (1 - dot_hue)[..., None] + lerp(BLUE, CYAN, 0.35)[None, None, :] * dot_hue[..., None]
        e = (col(lerp(BLUE, CYAN, hue_mix), myc * (0.18 + 0.45 * patch))
             + col(CYAN, mycg ** 3 * 0.06 * patch)
             + dot_col * (dots * bright * 1.0)[..., None]
             + col(EMERALD, heads * 0.015 * (0.3 + patch)))
        e = e * (1 - edge)[..., None]
        height = 0.6 * dome + 0.5 * heads + 0.25 * fuzz + 0.2 * myc + 0.3 * dots
        rough = 0.92 - 0.3 * dots
        return dict(base=base, emis=e, height=height, rough=rough, ao=0.35 + 0.65 * dome * (0.5 + 0.5 * heads))

    def paint_sporo(self, S, T):
        tip = smoothstep(0.78, 0.92, T)
        base = lerp(np.array([0.02, 0.03, 0.025]), np.array([0.15, 0.3, 0.2]), tip)
        e = col(lerp(EMERALD, MINT, tip), 0.004 + 0.75 * tip ** 2)
        return dict(base=base, emis=e, height=np.zeros_like(S), rough=0.4, ao=1.0)

    def paint_cup(self, S, T):
        lip = smoothstep(0.85, 1.0, T)
        inside = smoothstep(0.5, 0.9, T)
        base = lerp(np.array([0.03, 0.05, 0.04]), np.array([0.07, 0.12, 0.10]), T)
        e = col(EMERALD, inside * 0.08) + col(MINT, lip * 0.25)
        return dict(base=base, emis=e, height=fbm(S * 20, T * 6, 2, self.seed + 30, px=20), rough=0.7, ao=1.0)


def make(lod=0, seed=71):
    return Mat(seed, lod).build()


def make_mound(lod=0, seed=73):
    """The large variant: a 1 m moss mound, 0.42 m high, for the macro's out-of-focus foreground."""
    return Mat(seed, lod, name="glowmound", R0=0.5, dome=0.36, dot_cell=0.011, cups=False, sporo_scale=1.6).build()

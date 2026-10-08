"""Scene support 3c and 3d: the riverbank sedge and the meadow bulb-stalk. Both are built to be instanced in thousands.

GLOWSEDGE (3c): a clump of arching, V-folded, tapering blades, some bent over at the tip, with a few taller stems.
Each stem ends in a spindle-shaped, beaded seed-head that glows mint-cyan; the blade tips catch a faint cold light.
0.6-1.5 m tall.

BULBSTALK (3d): the meadow's glowing-tip stalks (reference panel 4). Three to five hair-thin stalks that lean and curve
from a tiny basal rosette, each carrying a translucent bulb with a bright core. 0.12-0.32 m tall. LOD1 is under
300 triangles.

Units metres, Z up in Blender (Y up exported), origin at the base.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (CYAN, EMERALD, MINT, Asset, bezier, col, fbm, frames, lerp, normalize, smoothstep, tube_grid)

TAU = 2 * math.pi


def blade(base, direction, height, width, bend, rng, n, cols):
    """A tapering, V-folded ribbon that rises and arches over. Returns an (n, cols, 3) grid, u across, v up."""
    d = normalize(np.asarray(direction, dtype=np.float64))
    up = np.array([0, 0, 1.0])
    out = normalize(d - up * np.dot(d, up)) if abs(np.dot(d, up)) < 0.999 else np.array([1.0, 0, 0])
    tip = base + up * height * (1 - 0.45 * bend) + out * height * (0.25 + 0.55 * bend)
    mid = base + up * height * 0.75 + out * height * 0.15
    C = bezier(base, base + up * height * 0.35, mid, tip, n)
    T, N, B = frames(C)
    # the blade's width direction: across the arch (horizontal, perpendicular to `out`)
    side = normalize(np.cross(T, out[None, :] * np.ones_like(T) + 1e-6))
    nrm = normalize(np.cross(side, T))
    t = np.linspace(0, 1, n)
    w = width * (1 - t ** 1.6) * (0.6 + 0.4 * smoothstep(0.0, 0.15, t))
    twist = rng.uniform(-0.6, 0.6) * t
    x = np.linspace(-1, 1, cols)
    rows = []
    for i in range(n):
        s = side[i] * math.cos(twist[i]) + nrm[i] * math.sin(twist[i])
        nn = np.cross(s, T[i])
        rows.append(C[i][None, :] + s[None, :] * (x * w[i] / 2)[:, None] + nn[None, :] * (np.abs(x) * w[i] * 0.25)[:, None])
    return np.array(rows), C


class Sedge:
    def __init__(self, seed=111, lod=0):
        self.seed = seed
        self.lod = lod
        self.lod0 = lod == 0

    def build(self):
        rng = np.random.default_rng(self.seed)
        lod0 = self.lod0
        A = Asset("glowsedge")
        res = 1.0 if lod0 else 0.5
        sl = A.slot("blade", res=(int(256 * res), int(1024 * res)), mode="OPAQUE", double_sided=True,
                    emissive_strength=4.0)
        st = A.slot("seedhead", res=(int(256 * res), int(512 * res)), mode="OPAQUE", double_sided=False,
                    emissive_strength=4.0)
        rb = sl.region(0, 1, self.paint_blade, bump=2.0)
        rs = st.region(0, 0.5, self.paint_stem, bump=1.0)
        rh = st.region(0.5, 1.0, self.paint_head, bump=2.0)
        nb = 40 if lod0 else 12
        for j in range(nb):
            a = rng.uniform(0, TAU)
            r = abs(rng.normal(0, 0.025))
            base = np.array([r * math.cos(a), r * math.sin(a), -0.01])
            lean = rng.uniform(0.15, 0.6)
            d = [math.sin(lean) * math.cos(a), math.sin(lean) * math.sin(a), math.cos(lean)]
            h = rng.uniform(0.5, 1.25)
            bend = rng.uniform(0.0, 1.0) ** 2
            P, _ = blade(base, d, h, rng.uniform(0.010, 0.02), bend, rng, 12 if lod0 else 4, 3 if lod0 else 2)
            sl.add_grid(P, rb)
            if lod0 and rng.random() < 0.3:
                A.emitters.append(list(P[-2].mean(0)) + [0.95, 0.5, 0.0015])
        for j in range(6 if lod0 else 3):
            a = rng.uniform(0, TAU)
            base = np.array([0.01 * math.cos(a), 0.01 * math.sin(a), -0.01])
            h = rng.uniform(1.0, 1.5)
            lean = rng.uniform(0.05, 0.3)
            top = base + np.array([math.sin(lean) * math.cos(a) * h, math.sin(lean) * math.sin(a) * h, h])
            n = 14 if lod0 else 4
            stem = bezier(base, base + [0, 0, h * 0.4], top - [0, 0, h * 0.15], top, n)
            tt = np.linspace(0, 1, n)
            st.add_grid(tube_grid(stem, 0.0022 - 0.0012 * tt, 5 if lod0 else 3), rs, wrap_u=True)
            # the seed-head: a beaded spindle continuing the stem
            L = rng.uniform(0.05, 0.09)
            dvec = normalize(top - stem[-2])
            hn = 16 if lod0 else 4
            head = top[None, :] + dvec[None, :] * np.linspace(0, L, hn)[:, None]
            ht = np.linspace(0, 1, hn)
            hr = 0.0045 * np.sin(math.pi * ht) ** 0.7 * (1 + 0.18 * np.sin(ht * 9 * math.pi) * lod0) + 0.0012
            st.add_grid(tube_grid(head, hr, 8 if lod0 else 4), rh, wrap_u=True)
            A.emitters.append(list(head[hn // 2]) + [0.5, 0.5, 0.004])
        self.A = A
        return A

    def paint_blade(self, S, T):
        sd = self.seed + 10
        x = np.abs(S - 0.5) * 2
        rib = 1 - smoothstep(0.0, 0.15, x)
        vein = smoothstep(0.85, 1.0, 0.5 + 0.5 * np.cos(S * TAU * 7))
        mott = fbm(S * 4, T * 30, 3, sd)
        tip = smoothstep(0.8, 1.0, T)
        base = lerp(np.array([0.008, 0.016, 0.012]), np.array([0.025, 0.05, 0.035]), 0.4 * T + 0.3 * mott)
        e = col(lerp(EMERALD, CYAN, tip), tip ** 2 * 0.25 * (0.4 + 0.8 * mott)) + col(EMERALD, rib * 0.01 * T)
        return dict(base=base, emis=e, height=0.6 * rib + 0.3 * vein + 0.2 * mott, rough=0.5, ao=0.4 + 0.6 * T)

    def paint_stem(self, S, T):
        base = lerp(np.array([0.01, 0.02, 0.015]), np.array([0.03, 0.06, 0.05]), T)
        e = col(CYAN, 0.04 * T ** 4)
        return dict(base=base, emis=e, height=np.zeros_like(S), rough=0.5, ao=0.5 + 0.5 * T)

    def paint_head(self, S, T):
        beads = 0.5 + 0.5 * np.cos(T * TAU * 9)
        mid = np.sin(math.pi * T)
        base = lerp(np.array([0.05, 0.1, 0.08]), np.array([0.2, 0.35, 0.3]), mid)
        e = col(lerp(CYAN, MINT, mid), (0.2 + 0.6 * mid) * (0.6 + 0.4 * beads))
        return dict(base=base, emis=e, height=beads, rough=0.3, ao=1.0)


class Bulbstalk:
    def __init__(self, seed=121, lod=0):
        self.seed = seed
        self.lod = lod
        self.lod0 = lod == 0

    def build(self):
        rng = np.random.default_rng(self.seed)
        lod0 = self.lod0
        A = Asset("bulbstalk")
        res = 1.0 if lod0 else 0.5
        sl = A.slot("stalk", res=(int(256 * res), int(512 * res)), mode="OPAQUE", double_sided=True,
                    emissive_strength=4.0)
        rs = sl.region(0, 0.4, self.paint_stalk, bump=1.0)
        rb = sl.region(0.4, 0.8, self.paint_bulb, bump=1.0)
        rl = sl.region(0.8, 1.0, self.paint_leaf, bump=1.0)
        n = 5
        for j in range(n):
            a = TAU * j / n + rng.uniform(-0.5, 0.5)
            h = rng.uniform(0.12, 0.32)
            lean = rng.uniform(0.05, 0.4)
            base = np.array([0.006 * math.cos(a), 0.006 * math.sin(a), -0.005])
            top = base + np.array([math.sin(lean) * math.cos(a) * h, math.sin(lean) * math.sin(a) * h,
                                   h * math.cos(lean)])
            if not lod0 and j >= 3:
                continue
            m = 10 if lod0 else 4
            stalk = bezier(base, base + [0, 0, h * 0.5], top - [0, 0, h * 0.2] + 0.3 * (top - base) * [1, 1, 0],
                           top, m)
            t = np.linspace(0, 1, m)
            sl.add_grid(tube_grid(stalk, 0.0011 - 0.0005 * t, 5 if lod0 else 3), rs, wrap_u=True)
            # the bulb: a small, slightly pointed ovoid along the stalk's end direction
            dvec = normalize(top - stalk[-2])
            R = rng.uniform(0.0035, 0.006)
            bn = 8 if lod0 else 4
            bt = np.linspace(0, 1, bn)
            axis = top[None, :] + dvec[None, :] * (bt * 2.6 * R)[:, None]
            br = R * np.sin(math.pi * bt) ** 0.75 * (1 - 0.25 * bt) + 0.0004
            sl.add_grid(tube_grid(axis, br, 8 if lod0 else 5), rb, wrap_u=True)
            A.emitters.append(list(axis[bn // 2]) + [0.5, 0.5, R])
        # a tiny rosette of basal leaves (LOD0 only)
        if lod0:
            for j in range(3):
                a = TAU * j / 3 + rng.uniform(-0.3, 0.3)
                P, _ = blade(np.array([0, 0, -0.004]), [math.cos(a) * 0.8, math.sin(a) * 0.8, 0.6], 0.045, 0.008,
                             0.8, rng, 6, 2)
                sl.add_grid(P, rl)
        self.A = A
        return A

    def paint_stalk(self, S, T):
        base = lerp(np.array([0.01, 0.025, 0.02]), np.array([0.04, 0.08, 0.06]), T)
        e = col(EMERALD, 0.01 + 0.08 * T ** 3)
        return dict(base=base, emis=e, height=np.zeros_like(S), rough=0.5, ao=0.5 + 0.5 * T)

    def paint_bulb(self, S, T):
        core = np.sin(math.pi * T) ** 1.2
        base = lerp(np.array([0.04, 0.10, 0.07]), np.array([0.08, 0.2, 0.15]), core)
        hue = lerp(EMERALD, CYAN, 0.5 + 0.5 * np.cos(TAU * S))  # a cyan cheek on an emerald bulb
        k = (0.25 * core ** 4)[..., None]
        e = (hue * (1 - k) + MINT * k) * (0.2 + 0.5 * core)[..., None]
        return dict(base=base, emis=e, height=np.zeros_like(S), rough=0.25, ao=1.0)

    def paint_leaf(self, S, T):
        base = lerp(np.array([0.008, 0.02, 0.012]), np.array([0.02, 0.045, 0.03]), T)
        return dict(base=base, emis=col(EMERALD, 0.005 * T), height=np.zeros_like(S), rough=0.6, ao=0.5 + 0.5 * T)


def make_sedge(lod=0):
    return Sedge(lod=lod).build()


def make_bulbstalk(lod=0):
    return Bulbstalk(lod=lod).build()


class Darkfern:
    """Scene support (optional), the DARKFERN: a low understorey fern for dark contrast between the glowing
    species. Ten or so pinnate fronds arch out from a crown, 0.35-0.7 m long; their alpha-cut leaflets slant forward
    and shorten toward the tip. Almost unlit: a cold sheen on the leaflets and a rare glint at a frond's tip.
    LOD1 is about 100 triangles."""

    def __init__(self, seed=141, lod=0):
        self.seed = seed
        self.lod0 = lod == 0

    def build(self):
        rng = np.random.default_rng(self.seed)
        lod0 = self.lod0
        A = Asset("darkfern")
        res = 1.0 if lod0 else 0.5
        sl = A.slot("frond", res=(int(512 * res), int(1024 * res)), mode="MASK", emissive_strength=4.0)
        rf = sl.region(0, 1, self.paint_frond, bump=2.0)
        n = 11 if lod0 else 6
        for j in range(n):
            a = TAU * j / n + rng.uniform(-0.25, 0.25)
            lean = rng.uniform(0.5, 1.0)
            d = [math.sin(lean) * math.cos(a), math.sin(lean) * math.sin(a), math.cos(lean)]
            L = rng.uniform(0.35, 0.7)
            P, C = blade(np.array([0, 0, -0.01]), d, L * 0.55, L * 0.32, rng.uniform(0.6, 1.0), rng,
                         10 if lod0 else 4, 3 if lod0 else 2)
            sl.add_grid(P, rf)
            if lod0 and rng.random() < 0.25:
                A.emitters.append(list(C[-2]) + [0.95, 0.5, 0.0015])
        self.A = A
        return A

    def paint_frond(self, S, T):
        sd = self.seed + 3
        x = (S - 0.5) * 2
        ax = np.abs(x)
        env = np.sin(math.pi * np.clip(T * 0.92 + 0.04, 0, 1)) ** 0.6 * (1 - 0.35 * T)  # the frond's outline
        N = 26.0
        phase = T * N - ax * 4.0  # leaflets slant toward the tip
        f = np.abs((phase % 1.0) - 0.5) * 2  # 0 on a leaflet's midline
        width = 0.55 * (1 - ax / np.maximum(env, 1e-3))
        leaflet = (f < width) & (ax < env)
        rachis = ax < 0.04 * (1 - 0.6 * T)
        alpha = (leaflet | rachis).astype(float)
        mott = fbm(S * 6, T * 20, 2, sd)
        base = lerp(np.array([0.004, 0.010, 0.008]), np.array([0.014, 0.03, 0.022]), 0.5 * mott + 0.3 * (1 - f))
        tip = smoothstep(0.88, 1.0, T)
        e = col(lerp(EMERALD, CYAN, 0.5), tip * 0.06 * rachis)
        return dict(base=base, emis=e, alpha=alpha, height=(1 - f) * leaflet + rachis, rough=0.6, ao=0.4 + 0.6 * T)


def make_darkfern(lod=0):
    return Darkfern(lod=lod).build()

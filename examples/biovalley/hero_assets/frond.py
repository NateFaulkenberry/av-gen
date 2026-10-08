"""Archetype 5, the VEILFERN: a rosette of broad, delicate, translucent fronds.

Five fronds rise from a dark crown on slim petioles, arch outward and droop toward their tips. Each blade is lobed and
folded slightly along its midrib, and its margin rolls and ruffles. The youngest frond is still unfurling, its tip
coiled into a crozier. The midrib glows faintly. Pinnate veins sweep toward the tip and brighten toward the margin,
which carries a thin cyan edge. In a few places near the margin the blade has decayed into a skeleton of lace windows.

Units metres, Z up in Blender (Y up exported), origin at the base. Height ~0.3 m, spread ~0.6 m.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (CYAN, EMERALD, MINT, TEAL_DARK, TISSUE_DARK, Asset, col, fbm, lace, lerp, normalize, smoothstep,
                 tube_grid, vnoise)

TAU = 2 * math.pi


class Frond:
    # (length, width, azimuth, pitch at base (deg), pitch at tip, young)
    FRONDS = [(0.44, 0.105, 0.3, 84, -8, False), (0.38, 0.095, 1.25, 80, -22, False),
              (0.42, 0.10, 2.2, 86, -5, False), (0.33, 0.085, 3.2, 76, -30, False),
              (0.40, 0.10, 4.1, 83, -15, False), (0.27, 0.07, 5.0, 72, -38, False),
              (0.22, 0.055, 5.75, 88, -330, True)]

    def __init__(self, seed=51, lod=0):
        self.seed = seed
        self.lod = lod
        self.q = 1.0 if lod == 0 else 0.35

    def midrib(self, L, az, p0, p1, young, n):
        v = np.linspace(0, 1, n)
        if young:
            a = np.radians(p0 + (p1 - p0) * smoothstep(0.35, 1.0, v) ** 1.4)
            step = L / (n - 1) * (1 - 0.7 * smoothstep(0.4, 1.0, v))
        else:
            a = np.radians(p0 + (p1 - p0) * v ** 1.3)
            step = np.full(n, L / (n - 1))
        fwd = np.array([math.cos(az), math.sin(az), 0.0])
        up = np.array([0, 0, 1.0])
        d = np.cos(a)[:, None] * fwd + np.sin(a)[:, None] * up
        P = np.zeros((n, 3))
        P[1:] = np.cumsum(d[:-1] * step[:-1, None], axis=0)
        side = np.cross(up, fwd)
        return P, d, side, v

    def blade(self, k, nu, nv):
        L, W, az, p0, p1, young = self.FRONDS[k]
        rng = np.random.default_rng(self.seed + k)
        pet = 0.22 * L  # the petiole is part of the length, the blade starts above it
        C, T, side, v = self.midrib(L, az, p0, p1, young, nv)
        Nm = normalize(np.cross(side[None, :], T))
        x = np.linspace(-1, 1, nu)
        X, V = np.meshgrid(x, v)
        bv = np.clip((V - 0.2) / 0.8, 0, 1)  # blade parameter, 0 at the petiole's end
        w = W * np.sin(math.pi * bv ** 0.75) ** 0.8
        ph = rng.uniform(0, TAU, 3)
        lobes = 1 - 0.10 * np.abs(np.sin(6.5 * math.pi * bv + ph[0])) ** 3 * smoothstep(0.1, 0.4, bv)
        w = w * lobes * (1 + 0.06 * np.sin(3 * math.pi * bv + ph[1]))
        fold = np.abs(X) * w * 0.35
        roll = 0.18 * w * X ** 4 * (1 + 0.5 * np.sin(5 * bv + ph[2]))
        ruffle = 0.10 * w * np.abs(X) ** 3 * np.sin(38 * bv + 4 * X + ph[2])
        lift = fold + roll + ruffle
        P = C[:, None, :] + side[None, None, :] * (X * w)[..., None] + Nm[:, None, :] * lift[..., None]
        return P, C, w, pet

    def build(self):
        A = Asset("veilfern")
        res = 1.0 if self.lod == 0 else 0.5
        bl = A.slot("blade", res=(int(2048 * res), int(1024 * res)), mode="MASK", emissive_strength=4.0)
        st = A.slot("stalk", res=(int(512 * res), int(512 * res)), mode="OPAQUE", double_sided=False,
                    emissive_strength=4.0)
        n = len(self.FRONDS)
        rs = st.region(0, 1, self.paint_stalk, bump=3.0)
        for k in range(n):
            rb = bl.region(k / n, (k + 1) / n, self._paint_blade(k), bump=3.0)
            nu = int(36 * self.q) + 4
            nv = int(110 * self.q) + 6
            P, C, w, pet = self.blade(k, nu, nv)
            # only the blade rows (where it has width) become the blade; the petiole is a tube
            i0 = int(0.2 * (nv - 1))
            Pb = P[i0:]
            Pu = np.gradient(Pb, axis=1)
            Pv = np.gradient(Pb, axis=0)
            nrm = np.cross(Pu, Pv)[len(Pb) // 2, nu // 2]
            if nrm[2] < 0:  # face the upper (adaxial) side up
                Pb = Pb[:, ::-1]
            bl.add_grid(Pb, rb)
            rad = np.linspace(0.0035, 0.0016, i0 + 1)
            st.add_grid(tube_grid(C[:i0 + 1], rad, 8 if self.lod == 0 else 5), rs, wrap_u=True,
                        s_range=(k / n, (k + 1) / n), t_range=(0, 0.2))
            # glints: along the margin near the tip
            for f in (0.55, 0.75, 0.9):
                j = int(f * (len(Pb) - 1))
                A.emitters.append(list(Pb[j, 0]) + [f, 0.0, 0.0012])
                A.emitters.append(list(Pb[j, -1]) + [f, 1.0, 0.0012])
        # the crown: a dark knot of old leaf bases
        th = -np.linspace(0, TAU, 20, endpoint=False)
        vv = np.linspace(0, 1, 8)
        VV, TH = np.meshgrid(vv, th, indexing="ij")
        r = 0.022 * np.sin(math.pi * (0.5 + 0.5 * VV)) * (1 + 0.15 * np.cos(5 * TH)) + 0.002
        z = 0.022 * VV - 0.004
        st.add_grid(np.stack([r * np.cos(TH), r * np.sin(TH), z], -1)[:, ::-1], rs, wrap_u=True,
                    t_range=(0, 0.1))
        self.A = A
        return A

    def _paint_blade(self, k):
        def paint(S, T):
            sd = self.seed + 30 * k
            # S across the blade (0..1, midrib at 0.5), T from the petiole's end (0) to the tip (1)
            x = (S - 0.5) * 2
            ax = np.abs(x)
            wob = 0.03 * (fbm(S * 6, T * 12, 2, sd) - 0.5)
            mid = 1 - smoothstep(0.008, 0.022, np.abs(x + wob * 0.3))
            # pinnate laterals sweeping toward the tip
            lat_f = (T * 19 - ax * 2.6 + wob * 14 + 0.6 * fbm(S * 3, T * 4, 2, sd + 7))
            lat_b = 0.25 + 0.75 * vnoise(np.floor(lat_f) * 1.0, 0.5, sd + 8) ** 1.5  # each lateral its own brightness
            lat = smoothstep(0.88, 0.97, 1 - np.abs((lat_f % 1.0) * 2 - 1)) * (0.5 + 0.5 * lat_b)
            lat = lat * smoothstep(0.02, 0.12, ax)
            # a fine areolar net between the laterals
            net, _, _, _ = lace(S, T, 40, 70, sd + 1, wall=(0.03, 0.08), px=False, warp=0.3)
            margin = smoothstep(0.945, 0.995, ax) * (0.4 + 0.8 * fbm(S * 3, T * 30, 2, sd + 9))
            # skeletonised windows near the margin
            patch = smoothstep(0.62, 0.72, fbm(S * 5 + 1, T * 7, 3, sd + 2)) * smoothstep(0.45, 0.75, ax) \
                * (1 - margin)
            alpha = np.where(patch > 0.5, ((net > 0.5) | (lat > 0.5)).astype(float), 1.0)
            # torn, toothed edge
            alpha = alpha * (ax < 0.985 + 0.015 * fbm(S * 4, T * 200, 2, sd + 3))
            mott = fbm(S * 14, T * 30, 3, sd + 4)
            thin = smoothstep(0.2, 1.0, ax)
            base = lerp(TEAL_DARK, np.array([0.02, 0.07, 0.06]), thin * (0.5 + 0.5 * mott))
            base = base + col([0.02, 0.05, 0.04], lat + mid)
            hue = lerp(EMERALD, CYAN, smoothstep(0.3, 1.0, ax))
            e = (col(EMERALD, mid * (0.08 + 0.2 * T))
                 + col(hue, lat * lat_b * (0.03 + 0.45 * ax ** 1.5) * (0.5 + 0.7 * mott))
                 + col(hue, net * patch * 0.25) + col(CYAN, margin ** 2 * 0.28)
                 + col(hue, 0.002 + 0.008 * thin * mott))
            height = 0.8 * mid + 0.5 * lat + 0.2 * net + 0.2 * mott
            return dict(base=base, emis=e, alpha=alpha, height=height, rough=0.42 + 0.15 * mott,
                        ao=0.6 + 0.4 * T)
        return paint

    def paint_stalk(self, S, T):
        mott = fbm(S * 30, T * 60, 3, self.seed + 90)
        hair = smoothstep(0.75, 0.9, fbm(S * 200, T * 300, 2, self.seed + 91))
        base = TISSUE_DARK * (0.8 + 0.8 * mott)[..., None] + col([0.01, 0.03, 0.02], T * 4)
        e = col(EMERALD, 0.01 * smoothstep(0.1, 0.2, T))
        return dict(base=base, emis=e, height=mott + hair, rough=0.85, ao=0.5 + 0.5 * T)


def make(lod=0, seed=51):
    return Frond(seed, lod).build()

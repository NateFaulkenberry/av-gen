"""Archetype 6, the BELLSPIRE: a tall stalk with a complex head and smaller secondary branches.

A slender stalk rises 0.7 m in a long, lazy S-curve, sheathed at intervals by papery bracts. Its head is an umbel:
thirty-odd pedicels burst outward and arc down under their own weight, each ending in a small pendant bell with a
glowing clapper. A crown of short upright stamens with luminous tips sits at the head's centre. Three secondary
branches leave the upper stalk, curve up and nod, each carrying a smaller umbel of its own.

Units metres, Z up in Blender (Y up exported), origin at the base. Height ~0.72 m.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (CYAN, EMERALD, MINT, TISSUE_DARK, Asset, bezier, col, fbm, frame_to, lerp, rodrigues, smoothstep,
                 tube_grid)

TAU = 2 * math.pi


class Spire:
    def __init__(self, seed=61, lod=0):
        self.seed = seed
        self.lod = lod
        self.lod0 = lod == 0

    def build(self):
        rng = np.random.default_rng(self.seed)
        lod0 = self.lod0
        A = Asset("bellspire")
        self.A = A
        res = 1.0 if lod0 else 0.5
        st = A.slot("stalk", res=(int(1024 * res), int(1024 * res)), mode="OPAQUE", double_sided=False,
                    emissive_strength=4.0)
        bl = A.slot("bell", res=(int(1024 * res), int(512 * res)), mode="MASK", emissive_strength=4.0)
        self.r_stalk = st.region(0.0, 0.5, self.paint_stalk, bump=4.0)
        self.r_ped = st.region(0.5, 0.8, self.paint_pedicel, bump=1.0)
        self.r_bead = st.region(0.8, 1.0, self.paint_bead, bump=1.0)
        self.r_bell = bl.region(0.0, 0.6, self.paint_bell, bump=2.0)
        self.r_bract = bl.region(0.6, 1.0, self.paint_bract, bump=3.0)
        self.st, self.bl = st, bl

        main = bezier([0, 0, 0], [0.03, 0.0, 0.25], [-0.05, 0.03, 0.45], [0.02, 0.01, 0.70], 90 if lod0 else 30)
        t = np.linspace(0, 1, len(main))
        rad = 0.0075 - 0.0042 * t + 0.004 * np.exp(-t / 0.03)
        st.add_grid(tube_grid(main, rad, 14 if lod0 else 7), self.r_stalk, wrap_u=True, s_range=(0, 0.5))
        # bracts at the nodes
        for f in (0.22, 0.4, 0.56, 0.7):
            i = int(f * (len(main) - 1))
            self.bract(main, i, rad[i], rng)
        tangent_top = main[-1] - main[-2]
        self.umbel(main[-1], tangent_top, 30 if lod0 else 12, 0.13, 0.0115, np.random.default_rng(self.seed + 200),
                   stamens=True)
        # secondary branches
        for k, f in enumerate((0.58, 0.68, 0.78)):
            i = int(f * (len(main) - 1))
            p0 = main[i]
            az = rng.uniform(0, TAU) + k * 2.1
            out = np.array([math.cos(az), math.sin(az), 0.0])
            L = rng.uniform(0.09, 0.14)
            curve = bezier(p0, p0 + out * L * 0.4 + [0, 0, L * 0.15], p0 + out * L * 0.8 + [0, 0, L * 0.75],
                           p0 + out * L * 1.05 + [0, 0, L * 0.65], 30 if lod0 else 10)
            tt = np.linspace(0, 1, len(curve))
            r = rad[i] * 0.55 * (1 - 0.45 * tt)
            st.add_grid(tube_grid(curve, r, 9 if lod0 else 5), self.r_stalk, wrap_u=True,
                        s_range=(0.5 + 0.15 * k, 0.65 + 0.15 * k), t_range=(f, 1.0))
            self.umbel(curve[-1], curve[-1] - curve[-2], 12 if lod0 else 5, 0.065, 0.0085,
                       np.random.default_rng(self.seed + 300 + k), stamens=False)
        return A

    def bract(self, curve, i, r, rng):
        """A papery sheath wrapping the stalk part way, its free tip pointing up and out."""
        n = 8
        T = curve[min(i + 1, len(curve) - 1)] - curve[max(i - 1, 0)]
        T = T / np.linalg.norm(T)
        M = frame_to(T)
        L = rng.uniform(0.035, 0.05)
        span = rng.uniform(1.4, 1.9) * math.pi
        a0 = rng.uniform(0, TAU)
        nu, nv = (16, n) if self.lod0 else (7, 4)
        u = np.linspace(0, 1, nu)
        v = np.linspace(0, 1, nv)
        U, V = np.meshgrid(u, v)
        th = a0 + (U - 0.5) * span * (1 - 0.85 * V ** 1.5)
        rr = r * 1.15 + 0.004 * V ** 2
        z = L * V
        P = np.stack([rr * np.cos(th), rr * np.sin(th), z], -1) @ M.T + curve[i]
        self.bl.add_grid(P, self.r_bract)

    def umbel(self, p0, axis, n, L, bell_r, rng, stamens):
        axis = axis / np.linalg.norm(axis)
        lod0 = self.lod0
        ga = math.pi * (3 - math.sqrt(5))
        for j in range(n):
            # golden-angle azimuths; elevations from steep to wide
            az = j * ga + rng.uniform(-0.2, 0.2)
            el = math.radians(15 + 85 * math.sqrt((j + 0.5) / n)) + rng.uniform(-0.1, 0.1)
            M = frame_to(axis)
            d = M @ np.array([math.sin(el) * math.cos(az), math.sin(el) * math.sin(az), math.cos(el)])
            Lj = L * rng.uniform(0.75, 1.1)
            end = p0 + d * Lj * 0.95 + np.array([0, 0, -0.45 * Lj * math.sin(el)])
            curve = bezier(p0, p0 + d * Lj * 0.45, p0 + d * Lj * 0.95 + [0, 0, 0.05 * Lj], end, 12 if lod0 else 5)
            tt = np.linspace(0, 1, len(curve))
            self.st.add_grid(tube_grid(curve, 0.0007 * (1 - 0.4 * tt) + 0.0002, 5 if lod0 else 3), self.r_ped,
                             wrap_u=True, s_range=((j % 8) / 8, (j % 8 + 1) / 8))
            tan = curve[-1] - curve[-2]
            tan = tan / np.linalg.norm(tan)
            # the bell hangs: its mouth points between the pedicel's direction and straight down
            mouth = tan + np.array([0, 0, -1.2])
            self.bell(end, mouth / np.linalg.norm(mouth), bell_r * rng.uniform(0.8, 1.15), rng)
        if stamens:
            for j in range(9 if lod0 else 4):
                az = j * ga
                el = math.radians(rng.uniform(0, 30))
                M = frame_to(axis)
                d = M @ np.array([math.sin(el) * math.cos(az), math.sin(el) * math.sin(az), math.cos(el)])
                Lj = rng.uniform(0.015, 0.03)
                curve = bezier(p0, p0 + d * Lj * 0.4, p0 + d * Lj * 0.8, p0 + d * Lj, 8 if lod0 else 4)
                tt = np.linspace(0, 1, len(curve))
                rr = 0.0006 + 0.0011 * smoothstep(0.82, 0.95, tt) * (1 - smoothstep(0.95, 1.0, tt)) ** 0.5
                rr[-1] = 0.0002
                self.st.add_grid(tube_grid(curve, rr, 5 if lod0 else 3), self.r_bead, wrap_u=True,
                                 s_range=(j / 9, (j + 1) / 9))
                self.A.emitters.append(list(curve[-2]) + [1.0, 0.5, 0.0012])

    def bell(self, top, mouth, R, rng):
        lod0 = self.lod0
        nth, nv = (16, 8) if lod0 else (6, 4)
        v = np.linspace(0, 1, nv)
        th = -np.linspace(0, TAU, nth, endpoint=False) + rng.uniform(0, TAU)
        V, TH = np.meshgrid(v, th, indexing="ij")
        r = R * (0.25 + 0.55 * V ** 0.6 + 0.35 * smoothstep(0.7, 1.0, V) ** 2) * (1 + 0.1 * np.cos(5 * TH) * V)
        z = 1.7 * R * V - 0.25 * R * smoothstep(0.75, 1.0, V)
        P = np.stack([r * np.cos(TH), r * np.sin(TH), z], -1)
        M = frame_to(mouth)
        # rows run top -> mouth; clockwise u with a rising profile faces inward, so flip u to face out
        self.bl.add_grid((P @ M.T + top)[:, ::-1], self.r_bell, wrap_u=True)
        # the clapper: a small glowing bead inside the mouth
        cv = np.linspace(0, 1, 6 if lod0 else 3)
        cth = -np.linspace(0, TAU, 8 if lod0 else 4, endpoint=False)
        CV, CT = np.meshgrid(cv, cth, indexing="ij")
        cr = 0.32 * R * np.sin(math.pi * CV)
        cz = 1.05 * R - 0.32 * R * np.cos(math.pi * CV)
        Q = np.stack([cr * np.cos(CT), cr * np.sin(CT), cz], -1) @ M.T + top
        self.st.add_grid(Q, self.r_bead, wrap_u=True, s_range=(0, 1), t_range=(0.9, 1.0))
        self.A.emitters.append(list(top + mouth * 1.2 * R) + [1.0, 0.5, 0.6 * R])

    # ---- painters
    def paint_stalk(self, S, T):
        sd = self.seed + 7
        mott = fbm(S * 30, T * 120, 3, sd)
        stri = 0.5 + 0.5 * np.sin(S * 2 * math.pi * 18 + 3 * fbm(S * 4, T * 30, 2, sd + 1))
        up = smoothstep(0.2, 1.0, T)
        base = lerp(TISSUE_DARK, np.array([0.03, 0.06, 0.06]), up * (0.5 + 0.5 * mott))
        chan = smoothstep(0.9, 1.0, stri)
        e = col(lerp(EMERALD, CYAN, up), chan * (0.03 + 0.25 * up ** 2)) + col(EMERALD, 0.003 + 0.02 * up ** 3)
        return dict(base=base, emis=e, height=0.4 * stri + 0.4 * mott, rough=0.75 - 0.2 * up, ao=0.5 + 0.5 * up)

    def paint_pedicel(self, S, T):
        base = lerp(TISSUE_DARK, np.array([0.03, 0.07, 0.07]), T)
        e = col(CYAN, 0.01 + 0.10 * T ** 3)
        return dict(base=base, emis=e, height=np.zeros_like(S), rough=0.5, ao=1.0)

    def paint_bead(self, S, T):
        tip = smoothstep(0.82, 0.95, T)
        base = lerp(np.array([0.02, 0.05, 0.05]), np.array([0.2, 0.4, 0.3]), tip)
        e = col(lerp(EMERALD, MINT, tip), 0.02 + 0.85 * tip)
        return dict(base=base, emis=e, height=np.zeros_like(S), rough=0.2, ao=1.0)

    def paint_bell(self, S, T):
        sd = self.seed + 20
        ribs = smoothstep(0.7, 1.0, 0.5 + 0.5 * np.cos(S * 2 * math.pi * 10))
        mott = fbm(S * 10, T * 6, 2, sd, px=10)
        lip = smoothstep(0.82, 0.98, T)
        base = lerp(np.array([0.015, 0.03, 0.035]), np.array([0.05, 0.12, 0.12]), T * 0.7 + 0.2 * ribs)
        e = (col(lerp(CYAN, MINT, lip), lip ** 1.5 * 0.45 * (0.6 + 0.6 * mott)) + col(CYAN, ribs * 0.10 * T)
             + col(CYAN, 0.006 + 0.03 * T ** 2))
        alpha = (T < 0.97 + 0.03 * fbm(S * 60, 1.0, 2, sd + 1, px=60)).astype(float)
        return dict(base=base, emis=e, alpha=alpha, height=ribs + 0.3 * mott, rough=0.35, ao=0.5 + 0.5 * T)

    def paint_bract(self, S, T):
        sd = self.seed + 30
        veins = smoothstep(0.85, 0.98, 0.5 + 0.5 * np.cos(S * 2 * math.pi * 9 + 2 * fbm(S * 3, T * 2, 2, sd)))
        mott = fbm(S * 8, T * 8, 3, sd + 1)
        # papery, torn toward the tip
        tip = smoothstep(0.6, 1.0, T)
        alpha = (mott > 0.3 * tip ** 2 + 0.15 * tip - 0.05).astype(float)
        base = lerp(np.array([0.02, 0.03, 0.03]), np.array([0.05, 0.08, 0.07]), T) * (0.7 + 0.5 * mott)[..., None]
        e = col(EMERALD, veins * 0.12 * T) + col(CYAN, smoothstep(0.85, 1.0, T) * 0.15)
        return dict(base=base, emis=e, alpha=alpha, height=veins + mott * 0.4, rough=0.7, ao=0.6 + 0.4 * T)


def make(lod=0, seed=61):
    return Spire(seed, lod).build()

"""Karst formations for the vista (07-cinematic-finishing-pass.md section 6.1).

env_karst_tower: the right-hand landmark.
  - A tall, vertically articulated limestone tower, about 520 m high, on a base about 300 m across.
  - Its plan is an elongated, lobed section, carved into sharp vertical fins and flutes.
  - The radius steps inward at a few erosion ledges, each with a dark recess band undercut beneath it and dark
    vegetation on its top.
  - Vertical fissures, and water staining streaking down the pale faces.
  - The crown breaks into three narrow pinnacles of different heights: a distinctive, broken upper profile.
  - The base flares into a buttressed scree apron that sinks below the terrain.
env_karst_fin: a supporting formation. A long, thin, blade-like ridge with a jagged, saw-toothed crest.
env_karst_stack: a supporting formation. A squat, leaning stack with a flat, overhanging table top.

The three silhouettes are deliberately different: spire cluster, blade, table.
Units metres, Z up in Blender (Y up exported), origin at the base centre on the terrain.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (CYAN, EMERALD, Asset, col, fbm, grid_normals, grid_sample, lerp, ridge, smoothstep, worley3)
from rocks import noise3

TAU = 2 * math.pi


class Karst:
    def __init__(self, name, seed, H, R, elong, fins, ledges, crown, lean=(0.0, 0.0), table=False, lod=0,
                 taper=0.55, nth=320, nv=240, pinch_at=0.68, saw=0, apron=0.55, fin_amp=0.10, undercut=1.2,
                 broken_top=0.0, tufts=0):
        self.name, self.seed, self.H, self.R = name, seed, H, R
        self.elong, self.fins, self.ledges, self.crown = elong, fins, ledges, crown
        self.lean, self.table, self.taper, self.pinch_at = lean, table, taper, pinch_at
        self.saw, self.apron = saw, apron
        self.fin_amp, self.undercut, self.broken_top, self.tufts = fin_amp, undercut, broken_top, tufts
        self.lod0 = lod == 0
        self.nth = nth if self.lod0 else nth // 4
        self.nv = nv if self.lod0 else nv // 4

    def surface(self):
        sd = self.seed
        th = -np.linspace(0, TAU, self.nth, endpoint=False)
        v = np.linspace(0, 1, self.nv)
        V, TH = np.meshgrid(v, th, indexing="ij")
        # the crown: the top of every column of the grid ends at its own height, so the summit breaks into pinnacles
        crown = np.zeros_like(TH)
        for a, h, w in self.crown:  # (angle, relative height, angular half-width)
            d = np.abs(((TH - a + math.pi) % TAU) - math.pi)
            crown = np.maximum(crown, h * np.exp(-(d / w) ** 2))
        if self.saw:  # a saw-toothed crest: teeth all along the ridge, of irregular height
            ph = TH / TAU * self.saw + 0.35 * fbm(TH / TAU * 5, 0.5, 2, sd + 9, px=5)
            tooth = (1 - np.abs(2 * (ph % 1.0) - 1)) ** 1.6
            crown = np.maximum(crown * 0.0, tooth * (0.55 + 0.45 * fbm(np.floor(ph) * 0.37, 1.3, 1, sd + 8)))
        lo = 0.3 if self.saw else 0.78
        top = self.H * (lo + (1 - lo) * crown) if not self.table else self.H * np.ones_like(TH)
        if self.broken_top:  # a broken, notched top edge: blocks fallen out of the rim
            notches = smoothstep(0.55, 0.75, fbm(TH / TAU * 11, 0.3, 2, sd + 11, px=11))
            top = top * (1 - self.broken_top * (0.4 * notches + 0.6 * (fbm(TH / TAU * 23, 0.7, 2, sd + 12, px=23) - 0.5)))
        Z = V * top
        zz = Z / self.H
        # radius: tapering with height, stepped in at the ledges, the base flaring into scree
        r = 1.0 - self.taper * zz ** 1.15
        for z0, step in self.ledges:
            r = r - step * smoothstep(z0 - 0.004, z0 + 0.012, zz)
            # an undercut recess just below each ledge
            r = r - self.undercut * step * np.exp(-((zz - (z0 - 0.022)) / 0.014) ** 2)
        apron = self.apron * (1 - smoothstep(0.0, 0.14, zz)) ** 2 * (1 + 0.4 * (fbm(TH / TAU * 8, zz * 3, 2, sd, px=8) - 0.5))
        r = r + apron
        if not self.table:
            # toward the crown the section pinches to the pinnacles
            pinch = smoothstep(self.pinch_at, self.pinch_at + 0.2, zz)
            r = r * (1 - pinch * (1 - (0.08 + 0.92 * crown ** 1.5)))  # deep gaps between the pinnacles
            r = r * (1 - smoothstep(0.92, 1.0, V) ** 1.5 * 0.9)
        else:
            # the table: an overhanging cap
            r = r + 0.18 * smoothstep(0.88, 0.95, zz) * (1 - smoothstep(0.985, 1.0, V))
            r = r * (1 - 0.9 * smoothstep(0.985, 1.0, V))
        # fins and flutes: sharp vertical ribs, wandering a little with height
        wob = 0.35 * (fbm(TH / TAU * 3, zz * 2, 2, sd + 1, px=3) - 0.5)
        rib = np.abs(np.sin((TH + wob) * self.fins / 2))
        fins = self.fin_amp * (1 - rib) ** 5 - 0.5 * self.fin_amp * rib ** 8  # crisp, sharp-edged fins
        fissure = smoothstep(0.92, 0.99, 1 - np.abs(2 * fbm(TH / TAU * 23, zz * 1.5, 3, sd + 2, px=23) - 1))
        crag = 0.07 * (fbm(TH / TAU * 16, zz * 10, 4, sd + 3, px=16) - 0.5)
        r = np.maximum(r + fins - 0.05 * fissure + crag, 0.01)
        # the plan: an elongated, lobed section
        lobes = 1 + 0.16 * np.cos(3 * TH + 0.7) + 0.08 * np.cos(5 * TH + 2.1)
        X = self.R * r * lobes * np.cos(TH) * self.elong
        Y = self.R * r * lobes * np.sin(TH)
        X = X + self.lean[0] * Z
        Y = Y + self.lean[1] * Z
        Zs = Z - 0.06 * self.H * (1 - smoothstep(0.0, 0.05, zz))  # the foot sinks into the ground
        return np.stack([X, Y, Zs], -1)

    def build(self):
        A = Asset(self.name)
        res = 1.0 if self.lod0 else 0.5
        sl = A.slot("rock", res=(int(4096 * res), int(2048 * res)), mode="OPAQUE", double_sided=False,
                    emissive_strength=4.0)
        P = self.surface()
        # rows rise, columns run clockwise: cross(Pu, Pv) faces inward; flip u
        P = P[:, ::-1]
        N = grid_normals(P, True)
        Pw = np.concatenate([P, P[:, :1]], 1)
        Nw = np.concatenate([N, N[:, :1]], 1)
        sl.add_grid(P, sl.region(0, 1, self.painter(Pw, Nw), bump=6.0), wrap_u=True, N=N)
        if self.tufts:
            self.add_tufts(A, P, N)
        self.A = A
        return A

    def add_tufts(self, A, P, N):
        """Dark vegetation tufts on the ledges: low, lumpy shrubs where the surface faces up."""
        from rocks import Rock
        rng = np.random.default_rng(self.seed + 70)
        sl = A.slot("veg", res=(1024, 256) if self.lod0 else (512, 128), mode="OPAQUE", double_sided=False,
                    emissive_strength=4.0)
        nz = N[..., 2] / np.maximum(np.linalg.norm(N, axis=-1), 1e-6)
        cand = np.argwhere((nz > 0.55) & (P[..., 2] > 0.12 * self.H))
        if len(cand) == 0:
            return
        n = self.tufts if self.lod0 else self.tufts // 3
        pick = cand[rng.choice(len(cand), size=min(n, len(cand)), replace=False)]
        reg = sl.region(0, 1, self.paint_tuft, bump=3.0)
        for k, (i, j) in enumerate(pick):
            R = self.H * rng.uniform(0.008, 0.02)
            t = Rock(f"tuft{k}", self.seed + 100 + k, R, (1.0, 0.9, 0.7), 0.0, lod=0 if self.lod0 else 1)
            nth, nph = (16, 8) if self.lod0 else (8, 4)
            S2, T2 = np.meshgrid(np.linspace(0, 1, nth, endpoint=False), np.linspace(0, 1, nph))
            x, y, z = t.position(S2, T2)
            Q = np.stack([x, y, z], -1) + P[i, j]
            sl.add_grid(Q[:, ::-1], reg, wrap_u=True)

    def paint_tuft(self, S, T):
        n = fbm(S * 40, T * 20, 3, self.seed + 80)
        base = lerp(np.array([0.006, 0.016, 0.01]), np.array([0.02, 0.045, 0.028]), n)
        dots = smoothstep(0.82, 0.9, fbm(S * 120, T * 60, 2, self.seed + 81)) * T
        e = col(lerp(CYAN, EMERALD, n), dots * 0.35)
        return dict(base=base, emis=e, height=n, rough=0.9, ao=0.4 + 0.6 * T)

    def painter(self, P, N):
        H, sd = self.H, self.seed + 50

        def paint(S, T):
            pos = grid_sample(P, S, T)
            nrm = grid_sample(N, S, T)
            x, y, z = pos[..., 0], pos[..., 1], pos[..., 2]
            nz = nrm[..., 2] / np.maximum(np.linalg.norm(nrm, axis=-1), 1e-6)
            zz = z / H
            mott = noise3(x, y, z, 0.012, sd, 4)
            grain = noise3(x, y, z, 0.08, sd + 1, 3)
            # water staining: dark streaks running down the faces from each ledge and from the crown
            streak = smoothstep(0.55, 0.85, fbm(S * 140, zz * 2.0, 3, sd + 2, px=140)) * (0.4 + 0.6 * mott)
            band = 0.5 + 0.5 * np.sin(zz * 70 + 3 * noise3(x, y, z, 0.01, sd + 3, 2))
            # dark vegetation clings to what faces up: ledges and the apron
            veg = smoothstep(0.45, 0.75, nz + 0.25 * (noise3(x, y, z, 0.03, sd + 4, 3) - 0.5) * 2)
            vegn = noise3(x, y, z, 0.3, sd + 5, 2)
            # recesses (undercut, facing down or deep) are darkest
            recess = smoothstep(-0.05, -0.4, nz)
            stone = lerp(np.array([0.10, 0.11, 0.14]), np.array([0.26, 0.27, 0.31]), 0.45 * mott + 0.25 * band + 0.2 * grain)
            stone = stone * (1 - 0.55 * streak)[..., None] * (1 - 0.6 * recess)[..., None]
            plant = lerp(np.array([0.008, 0.02, 0.014]), np.array([0.025, 0.05, 0.035]), vegn)
            base = stone * (1 - veg)[..., None] + plant * veg[..., None]
            # a sparse bioluminescent speckle on the vegetated ledges only
            G1, gh = worley3(x * 0.5, y * 0.5, z * 0.5, sd + 7, jitter=0.95)
            dots = (1 - smoothstep(0.05, 0.14, G1)) * (gh < 0.25) * veg
            e = col(lerp(CYAN, EMERALD, np.clip(gh * 4, 0, 1)), dots * 0.5)
            height = 0.5 * mott + 0.3 * grain + 0.25 * band - 0.3 * streak + 0.3 * veg * vegn
            rough = 0.85 - 0.1 * veg
            return dict(base=base, emis=e, height=height, rough=rough, ao=0.45 + 0.55 * (1 - recess))
        return paint


def make_tower(lod=0):
    return Karst("env_karst_tower", 600, H=640.0, R=105.0, elong=0.8, fins=26,
                 ledges=[(0.22, 0.07), (0.38, 0.08), (0.54, 0.07), (0.66, 0.09)],
                 crown=[(0.4, 1.0, 0.3), (2.3, 0.8, 0.26), (4.2, 0.62, 0.24)], taper=0.2, pinch_at=0.62,
                 fin_amp=0.16, undercut=1.8, apron=0.7, tufts=60, lod=lod).build()


def make_fin(lod=0):
    return Karst("env_karst_fin", 610, H=380.0, R=260.0, elong=0.12, fins=40, ledges=[(0.42, 0.03)],
                 crown=[], taper=0.15, saw=14, apron=0.12, pinch_at=0.3, fin_amp=0.14, broken_top=0.12,
                 tufts=30, lod=lod).build()


def make_stack(lod=0):
    return Karst("env_karst_stack", 620, H=220.0, R=110.0, elong=0.9,
                 ledges=[(0.3, 0.04), (0.55, 0.05)], crown=[], lean=(0.12, 0.04), table=True, taper=0.25,
                 fins=28, fin_amp=0.14, broken_top=0.18, tufts=30, lod=lod).build()

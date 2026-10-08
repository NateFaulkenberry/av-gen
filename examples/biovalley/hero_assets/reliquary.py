"""Archetype 8, the RELIQUARY: the focal hero organism.

From a lumpy dark bulb, seven tusk-like ribs (flattened, tapering, slightly twisted) rise and arch outward, then curl
back toward the centre like cupped hands. A sap channel glows along each rib's inner face. In the cage they make sits
the heart, carried on a slender central stalk:
  - an outer shell of blue-violet lace;
  - inside it, a finer emerald lace shell;
  - at the centre, a bright veined core.
Fine tethers with luminous beads run from the ribs to the outer shell. From each rib tip hangs a short fringe of
beaded filaments.

Units metres, Z up in Blender (Y up exported), origin at the base. Height ~0.56 m.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (BLUE, CYAN, EMERALD, MINT, TISSUE_DARK, VIOLET, Asset, bezier, col, fbm, lace, lerp, normalize,
                 ridge, smoothstep, tube_grid)

TAU = 2 * math.pi


def tube_ref(curve, radii, sides, ref, flat=1.0):
    """A tube whose u = 0 faces `ref(i)` (a direction per point); `flat` < 1 squashes it across that direction."""
    T = normalize(np.gradient(curve, axis=0))
    R = np.array([ref(i) for i in range(len(curve))])
    N = normalize(R - T * np.sum(R * T, axis=1, keepdims=True))
    B = np.cross(T, N)
    th = np.linspace(0, TAU, sides, endpoint=False)
    ring = (np.cos(th)[None, :, None] * N[:, None, :] + flat * np.sin(th)[None, :, None] * B[:, None, :])
    return curve[:, None, :] + np.asarray(radii)[:, None, None] * ring


class Reliquary:
    H = 0.56
    HEART = np.array([0.0, 0.0, 0.27])

    def __init__(self, seed=81, lod=0):
        self.seed = seed
        self.lod = lod
        self.lod0 = lod == 0

    def build(self):
        rng = np.random.default_rng(self.seed)
        lod0 = self.lod0
        A = Asset("reliquary")
        self.A = A
        res = 1.0 if lod0 else 0.5
        rib = A.slot("rib", res=(int(1024 * res), int(1024 * res)), mode="OPAQUE", double_sided=False,
                     emissive_strength=4.0)
        shell = A.slot("shell", res=(int(2048 * res), int(1024 * res)), mode="MASK", emissive_strength=4.0)
        core = A.slot("core", res=(int(512 * res), int(512 * res)), mode="OPAQUE", double_sided=False,
                      emissive_strength=4.0)
        r_rib = rib.region(0.0, 0.55, self.paint_rib, bump=4.0)
        r_bulb = rib.region(0.55, 0.75, self.paint_bulb, bump=5.0)
        r_thread = rib.region(0.75, 1.0, self.paint_thread, bump=1.0)

        # ---- ribs
        nrib = 7
        n = 70 if lod0 else 22
        for k in range(nrib):
            rng = np.random.default_rng(self.seed + 10 * k)  # per rib, so LOD1 keeps LOD0's ribs
            a = TAU * k / nrib + rng.uniform(-0.12, 0.12)
            h = self.H * rng.uniform(0.88, 1.0)
            tw = rng.uniform(0.35, 0.6)
            t = np.linspace(0, 1, n)
            r = 0.035 + 0.16 * np.sin(math.pi * 0.8 * t ** 0.9) ** 1.1
            r = r - 0.035 * smoothstep(0.8, 1.0, t)  # the tips curl in, but stay apart: cupped hands
            ang = a + tw * t ** 1.5
            z = 0.025 + h * t ** 0.95 - 0.04 * smoothstep(0.85, 1.0, t)
            curve = np.stack([r * np.cos(ang), r * np.sin(ang), z], -1)
            rad = 0.012 * (1 - t) ** 1.2 + 0.0013
            P = tube_ref(curve, rad, 14 if lod0 else 6,
                         lambda i: np.array([-curve[i, 0], -curve[i, 1], 0.0]), flat=0.55)
            rib.add_grid(P, r_rib, wrap_u=True, s_range=(k / nrib, (k + 1) / nrib))
            # tip fringe
            tip = curve[-1]
            for j in range(3 if lod0 else 1):
                L = rng.uniform(0.03, 0.07)
                off = rng.normal(0, 0.006, 3)
                fc = bezier(tip, tip + off + [0, 0, -L * 0.3], tip + off * 2 + [0, 0, -L * 0.7],
                            tip + off * 2.5 + [0, 0, -L], 10 if lod0 else 4)
                tt = np.linspace(0, 1, len(fc))
                fr = 0.0006 + 0.0011 * smoothstep(0.84, 0.95, tt) * (1 - smoothstep(0.95, 1.0, tt)) ** 0.5
                fr[-1] = 0.0002
                rib.add_grid(tube_grid(fc, fr, 5 if lod0 else 3), r_thread, wrap_u=True,
                             s_range=(j / 3, (j + 1) / 3))
                A.emitters.append(list(fc[-2]) + [1.0, 0.5, 0.0016])
            # tethers from the rib to the outer shell
            for j in range(2 if lod0 else 1):
                i = int(rng.uniform(0.35, 0.6) * (n - 1))
                p0 = curve[i]
                d = self.HEART - p0
                p1 = self.HEART - normalize(d) * 0.075
                mid = (p0 + p1) / 2 + np.array([0, 0, -0.02])
                tc = bezier(p0, mid, mid, p1, 14 if lod0 else 5)
                tt = np.linspace(0, 1, len(tc))
                tr = 0.0005 + 0.0008 * np.exp(-((tt - 0.5) / 0.04) ** 2)  # a bead at the middle
                rib.add_grid(tube_grid(tc, tr, 5 if lod0 else 3), r_thread, wrap_u=True)
                A.emitters.append(list(tc[len(tc) // 2]) + [0.5, 0.5, 0.0012])

        # ---- bulb
        nv, nt = (24, 56) if lod0 else (8, 16)
        v = np.linspace(0, 1, nv)
        th = -np.linspace(0, TAU, nt, endpoint=False)
        V, TH = np.meshgrid(v, th, indexing="ij")
        lumps = 1 + 0.2 * (fbm(TH / TAU * 6, V * 3, 3, self.seed + 4, px=6) - 0.5) * 2
        lumps = lumps * (1 + 0.12 * np.cos(nrib * TH))
        br = 0.058 * np.sin(math.pi * np.clip(V * 0.92 + 0.04, 0, 1)) ** 0.7 * lumps
        bz = 0.062 * V - 0.006
        Pb = np.stack([br * np.cos(TH), br * np.sin(TH), bz], -1)
        rib.add_grid(Pb[:, ::-1], r_bulb, wrap_u=True)

        # ---- central stalk carrying the heart
        sc = bezier([0, 0, 0.05], [0.004, 0.0, 0.12], [-0.004, 0.003, 0.19], self.HEART - [0, 0, 0.018], 30 if lod0 else 8)
        tt = np.linspace(0, 1, len(sc))
        rib.add_grid(tube_grid(sc, 0.006 - 0.0028 * tt, 10 if lod0 else 5), r_rib, wrap_u=True, s_range=(0.0, 0.1),
                     t_range=(0.0, 0.5))

        # ---- the heart: two lace shells and a core
        r_out = shell.region(0.0, 0.55, self._paint_shell(BLUE, VIOLET, 26, 15, 0), bump=3.0)
        r_in = shell.region(0.55, 1.0, self._paint_shell(EMERALD, CYAN, 22, 13, 1), bump=3.0)
        for (rad, zr, reg, nt, nv, ph) in ((0.072, 0.085, r_out, 128, 48, 0.0), (0.046, 0.055, r_in, 96, 36, 0.7)):
            nt, nv = (nt, nv) if lod0 else (nt // 3, nv // 3)
            phi = np.linspace(-math.pi / 2 + 0.38, math.pi / 2 - 0.3, nv)
            th = -np.linspace(0, TAU, nt, endpoint=False) + ph
            PH, TH = np.meshgrid(phi, th, indexing="ij")
            lob = 1 + 0.10 * np.sin(5 * TH + ph * 3) * np.cos(PH) + 0.05 * np.sin(3 * PH + TH)
            r = rad * np.cos(PH) * lob
            z = zr * np.sin(PH) + 0.01 * np.sin(4 * TH) * np.cos(PH)
            P = np.stack([r * np.cos(TH), r * np.sin(TH), z], -1) + self.HEART
            shell.add_grid(P[:, ::-1], reg, wrap_u=True)  # rising rows + clockwise u face in: flip
        nv, nt = (20, 32) if lod0 else (8, 12)
        phi = np.linspace(-math.pi / 2, math.pi / 2, nv)
        th = -np.linspace(0, TAU, nt, endpoint=False)
        PH, TH = np.meshgrid(phi, th, indexing="ij")
        r = 0.022 * np.cos(PH) * (1 + 0.08 * np.sin(6 * TH) * np.cos(PH))
        z = 0.03 * np.sin(PH)
        core.add_grid((np.stack([r * np.cos(TH), r * np.sin(TH), z], -1) + self.HEART)[:, ::-1],
                      core.region(0, 1, self.paint_core, bump=2.0), wrap_u=True)
        A.emitters.append(list(self.HEART) + [0.5, 0.5, 0.02])
        return A

    # ---- painters
    def paint_rib(self, S, T):
        sd = self.seed + 10
        # u = 0 (and 1) faces the cage's axis: the sap channel runs there
        local = (S * 7) % 1.0
        inner = np.exp(-(np.minimum(local, 1 - local) / 0.07) ** 2)
        mott = fbm(S * 40, T * 80, 3, sd)
        stri = ridge(S * 50, T * 6, 2, sd + 1)
        up = smoothstep(0.05, 0.9, T)
        base = lerp(TISSUE_DARK, np.array([0.03, 0.05, 0.06]), 0.3 * up + 0.3 * inner) * (0.7 + 0.6 * mott)[..., None]
        e = (col(lerp(EMERALD, CYAN, up), inner * (0.02 + 0.38 * up ** 2) * (0.6 + 0.5 * mott))
             + col(CYAN, smoothstep(0.93, 1.0, T) * 0.35) + col(EMERALD, 0.002 * up))
        height = 0.5 * stri + 0.4 * mott + 0.3 * inner
        rough = 0.35 + 0.3 * mott * (1 - inner)
        return dict(base=base, emis=e, height=height, rough=rough, ao=0.5 + 0.5 * up)

    def paint_bulb(self, S, T):
        sd = self.seed + 20
        mott = fbm(S * 18, T * 6, 4, sd, px=18)
        wart = smoothstep(0.6, 0.85, fbm(S * 60, T * 20, 2, sd + 1, px=60))
        base = TISSUE_DARK * (0.8 + 0.8 * mott)[..., None]
        e = col(VIOLET, wart * 0.08 * T) + col(EMERALD, 0.002 * T)
        return dict(base=base, emis=e, height=mott + wart, rough=0.8, ao=0.3 + 0.7 * T)

    def paint_thread(self, S, T):
        bead = smoothstep(0.82, 0.95, T) + np.exp(-((T - 0.5) / 0.05) ** 2)
        base = lerp(np.array([0.02, 0.04, 0.05]), np.array([0.2, 0.35, 0.3]), np.clip(bead, 0, 1))
        e = col(lerp(CYAN, MINT, np.clip(bead, 0, 1)), 0.02 + 0.7 * np.clip(bead, 0, 1))
        return dict(base=base, emis=e, height=np.zeros_like(S), rough=0.3, ao=1.0)

    def _paint_shell(self, hue_a, hue_b, cu, cv, k):
        def paint(S, T):
            sd = self.seed + 100 * (k + 1)
            wall, knot, h, F1 = lace(S, T, cu, cv, sd, wall=(0.05, 0.11))
            edge = np.maximum(smoothstep(0.03, 0.0, T), smoothstep(0.97, 1.0, T))
            alpha = (np.maximum(wall, edge) > 0.5).astype(float)
            mott = fbm(S * 6, T * 4, 2, sd + 5, px=6)
            hue = lerp(hue_a, hue_b, mott)
            base = lerp(np.array([0.01, 0.015, 0.04]), np.array([0.04, 0.06, 0.12]), wall)
            e = col(hue, wall * (0.10 + 0.25 * mott)) + col(lerp(hue_b, MINT, 0.3), knot * wall * 0.45) \
                + col(hue_b, edge * 0.3)
            return dict(base=base, emis=e, alpha=alpha, height=wall + 0.5 * knot, rough=0.3, ao=1.0)
        return paint

    def paint_core(self, S, T):
        sd = self.seed + 300
        veins = smoothstep(0.9, 0.98, ridge(S * 12, T * 4, 3, sd, px=12))
        mott = fbm(S * 10, T * 6, 3, sd + 1, px=10)
        mid = np.sin(math.pi * T)
        base = lerp(np.array([0.05, 0.12, 0.10]), np.array([0.12, 0.25, 0.2]), mid)
        e = col(lerp(EMERALD, MINT, mid), 0.14 + 0.22 * mid * (0.6 + 0.6 * mott)) + col(CYAN, veins * 0.3)
        return dict(base=base, emis=e, height=veins + 0.3 * mott, rough=0.15, ao=1.0)


def make(lod=0, seed=81):
    return Reliquary(seed, lod).build()

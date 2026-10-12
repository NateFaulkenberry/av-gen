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


def tube_ref(curve, radii, sides, ref, flat=1.0, keel=0.0):
    """A tube whose u = 0 faces `ref(i)` (a direction per point); `flat` < 1 squashes it across that direction and
    `keel` raises a ridge on the side facing away from it (u = 0.5)."""
    T = normalize(np.gradient(curve, axis=0))
    R = np.array([ref(i) for i in range(len(curve))])
    N = normalize(R - T * np.sum(R * T, axis=1, keepdims=True))
    B = np.cross(T, N)
    th = np.linspace(0, TAU, sides, endpoint=False)
    ring = (np.cos(th)[None, :, None] * N[:, None, :] + flat * np.sin(th)[None, :, None] * B[:, None, :])
    k = 1 + keel * np.clip(-np.cos(th), 0, 1) ** 6
    return curve[:, None, :] + np.asarray(radii)[:, None, None] * k[None, :, None] * ring


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
            # articulation: swollen nodes and a slight change of direction at each
            nodes = np.array([0.17, 0.32, 0.46, 0.59, 0.71, 0.82, 0.91])
            node = np.exp(-((t[:, None] - nodes[None, :]) / 0.013) ** 2).sum(1)
            kink = (1 / (1 + np.exp(-(t[:, None] - nodes[None, :]) / 0.006)) * np.where(np.arange(7) % 2, 1, -1)).sum(1)
            ang = ang + 0.035 * kink
            curve = np.stack([r * np.cos(ang), r * np.sin(ang), z], -1)
            rad = (0.012 * (1 - t) ** 1.2 + 0.0013) * (1 + 0.32 * node)
            ref = lambda i: np.array([-curve[i, 0], -curve[i, 1], 0.0])
            P = tube_ref(curve, rad, 16 if lod0 else 6, ref, flat=0.42, keel=0.9 if lod0 else 0.5)
            rib.add_grid(P, r_rib, wrap_u=True, s_range=(k / nrib, (k + 1) / nrib))
            # thorns along the outer keel, between the nodes, leaning up
            if lod0:
                for tn in (nodes[:-1] + nodes[1:]) / 2:
                    i = int(tn * (n - 1))
                    out = normalize(np.array([curve[i, 0], curve[i, 1], 0.0]))
                    tang = normalize(curve[min(i + 1, n - 1)] - curve[max(i - 1, 0)])
                    base_p = curve[i] + out * rad[i] * 1.6
                    L = rng.uniform(0.010, 0.02) * (1.2 - t[i])
                    tip_p = base_p + normalize(out + 0.8 * tang) * L
                    th_c = bezier(base_p - out * rad[i] * 0.6, base_p, (base_p + tip_p) / 2 + tang * L * 0.15, tip_p, 5)
                    rib.add_grid(tube_grid(th_c, np.linspace(rad[i] * 0.35, 0.0002, 5), 5), r_thread, wrap_u=True,
                                 s_range=(0.0, 0.05), t_range=(0.0, 0.4))
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
                p1 = self.HEART - normalize(d) * 0.07
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

        # ---- the heart: a bud of three petal layers, closed inside, half open, reflexed outside
        layers = [  # (count, length, width, opening, curl, base radius, hue pair)
            (5, 0.105, 0.050, 0.06, 0.12, 0.010),
            (6, 0.125, 0.060, 0.28, 0.32, 0.018),
            (7, 0.140, 0.068, 0.52, 0.62, 0.026),
        ]
        prng = np.random.default_rng(self.seed + 500)
        for li, (cnt, L, W, opn, curl, r0) in enumerate(layers):
            reg = shell.region(li / 3, (li + 1) / 3, self._paint_petal(li), bump=3.0)
            for j in range(cnt):
                a = TAU * (j + 0.5 * li) / cnt + prng.uniform(-0.1, 0.1)
                P = self.petal(a, r0, L * prng.uniform(0.9, 1.08), W * prng.uniform(0.9, 1.1),
                               opn + prng.uniform(-0.08, 0.08), curl, prng, 24 if lod0 else 8, 12 if lod0 else 4)
                shell.add_grid(P, reg, s_range=(j / cnt, (j + 1) / cnt))
        nv, nt = (20, 32) if lod0 else (8, 12)
        phi = np.linspace(-math.pi / 2, math.pi / 2, nv)
        th = -np.linspace(0, TAU, nt, endpoint=False)
        PH, TH = np.meshgrid(phi, th, indexing="ij")
        r = 0.016 * np.cos(PH) * (1 + 0.08 * np.sin(6 * TH) * np.cos(PH))
        z = 0.024 * np.sin(PH) + 0.008
        core.add_grid((np.stack([r * np.cos(TH), r * np.sin(TH), z], -1) + self.HEART)[:, ::-1],
                      core.region(0, 1, self.paint_core, bump=2.0), wrap_u=True)
        A.emitters.append(list(self.HEART) + [0.5, 0.5, 0.02])
        return A

    def petal(self, a, r0, L, W, opn, curl, rng, nv, nu):
        """A cupped, pointed petal rising from the heart's base ring at azimuth a; v base -> tip, u across."""
        z = np.array([0, 0, 1.0])
        radial = np.array([math.cos(a), math.sin(a), 0.0])
        side = np.array([-math.sin(a), math.cos(a), 0.0])
        v = np.linspace(0, 1, nv)
        phi = opn + curl * v ** 1.6
        d = np.cos(phi)[:, None] * z + np.sin(phi)[:, None] * radial
        C = np.zeros((nv, 3))
        C[1:] = np.cumsum(d[:-1] * (L / (nv - 1)), axis=0)
        C = C + self.HEART - np.array([0, 0, 0.045]) + radial * r0
        nrm = np.cross(d, side)  # points toward the axis: the cup's hollow faces the core
        w = W * np.sin(math.pi * np.clip(v, 0, 1) ** 0.72) ** 0.75 * (1 - 0.15 * v)
        u = np.linspace(-1, 1, nu)
        V_, U_ = np.meshgrid(v, u, indexing="ij")
        ph = rng.uniform(0, TAU)
        ruff = 0.10 * np.abs(U_) ** 3 * np.sin(22 * V_ + 3 * U_ + ph) * V_
        cup = 0.35 * U_ ** 2 + ruff
        P = (C[:, None, :] + side[None, None, :] * (U_ * w[:, None])[..., None]
             + nrm[:, None, :] * (cup * w[:, None])[..., None])
        return P

    def _paint_petal(self, li):
        inner_hue, outer_hue = [(MINT, CYAN), (CYAN, BLUE), (BLUE, VIOLET)][li]

        def paint(S, T):
            sd = self.seed + 600 + 37 * li
            # S across all petals of the layer; each petal spans its own strip, so the local across is frac
            cnt = [5, 6, 7][li]
            x = ((S * cnt) % 1.0) * 2 - 1
            ax = np.abs(x)
            # veins converge at the base by geometry: constant-x lines; a midrib and branching secondaries
            veins = smoothstep(0.82, 0.98, 0.5 + 0.5 * np.cos(x * math.pi * 6 + 0.6 * fbm(x * 3 + li, T * 4, 2, sd)))
            mid = 1 - smoothstep(0.03, 0.07, ax)
            sec = smoothstep(0.9, 0.98, 1 - np.abs(2 * fbm(x * 9, T * 14, 2, sd + 1) - 1))
            edge = smoothstep(0.86, 0.99, ax) + smoothstep(0.9, 1.0, T)
            thin = smoothstep(0.3, 1.0, np.maximum(ax, T))
            # the outer layer is perforated toward its tip
            alpha = (ax < 0.985).astype(float)
            if li == 2:
                F1, F2, _, h = __import__("lib").worley(x * 6 + 20, T * 14, sd + 2)
                hole = (F1 < 0.32) & (h < smoothstep(0.45, 0.95, T) * 0.8) & (veins < 0.3) & (mid < 0.3)
                alpha = alpha * (~hole)
            near = (1 - T) ** 1.5  # lit from the core: brighter toward the base
            hue = lerp(inner_hue, outer_hue, T)
            base = lerp(np.array([0.02, 0.03, 0.08]), np.array([0.07, 0.10, 0.20]), thin)
            e = (col(hue, (veins * 0.35 + mid * 0.45 + sec * 0.15) * (0.25 + 0.75 * near))
                 + col(outer_hue, np.clip(edge, 0, 1) * 0.30)
                 + col(inner_hue, 0.03 + 0.10 * near * (1 - thin)))
            height = veins * 0.5 + mid * 0.8 + sec * 0.3
            return dict(base=base, emis=e, alpha=alpha, height=height, rough=0.3 + 0.2 * thin, ao=0.5 + 0.5 * T)
        return paint

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

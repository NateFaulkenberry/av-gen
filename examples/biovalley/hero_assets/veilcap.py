"""Archetype 1, the VEILCAP: a delicate translucent fungus with a layered cap.

Anatomy (bottom to top):
  - root tendrils splaying over the ground from a bulbous, fluted foot;
  - a leaning, S-curved, fluted stalk with a raised reticulate net on its upper half and faint blue sap veins;
  - a lace veil (indusium) hanging from beneath the cap: a cut-out cellular net whose strands and knots glow blue;
  - the outer cap: a broad drooping parasol membrane with lobed, ruffled, frayed rim, emerald veins and a cyan
    translucent margin;
  - lamellae beneath it: full, half and quarter gills with wavy free edges that glow emerald-cyan;
  - the crown: a smaller domed second cap sitting proud of the first, honeycombed, its rim dissolving into lace;
  - hair-fine filaments hanging from the rim, each ending in a luminous bead.

Units metres, Blender Z up (exported Y up), origin at the base. Height ~0.36 m.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (Asset, bezier, col, fbm, grow_veins, lerp, raster_segments, ridge, rot_y, rot_z, sample_canvas,
                 smoothstep, tube_grid, vnoise, worley)

TAU = 2 * math.pi

# palette (linear)
EMERALD = np.array([0.06, 1.0, 0.30])
CYAN = np.array([0.02, 0.75, 1.0])
BLUE = np.array([0.10, 0.30, 1.0])
TEAL_DARK = np.array([0.012, 0.035, 0.045])
TEAL_THIN = np.array([0.014, 0.045, 0.055])
STALK_DARK = np.array([0.010, 0.016, 0.022])


class Veilcap:
    def __init__(self, seed=7, lod=0):
        self.seed = seed
        self.lod = lod
        self.q = 1.0 if lod == 0 else 0.4
        rng = np.random.default_rng(seed)
        self.rng = rng
        # stalk
        self.stalk = bezier([0, 0, 0], [0.0, 0.004, 0.10], [0.032, -0.012, 0.17], [0.014, -0.006, 0.262], 96)
        self.top = self.stalk[-1]
        # the cap's frame: tilted toward +x a little, turned
        self.R = rot_z(0.4) @ rot_y(0.13)
        # outer cap shape
        self.cap_R0 = 0.108
        self.ph = rng.uniform(0, TAU, 8)
        # the cap's vein trees, drawn once on a planar canvas of the unit disc and sampled by the painters
        cres = 1536 if lod == 0 else 768
        vr = np.random.default_rng(seed + 50)
        prim = grow_veins(vr, 11, r0=0.07, step=0.018, wander=0.12, branch_p=0.16, spread=0.45, w0=1.0, decay=0.8)
        sec = grow_veins(vr, 44, r0=0.42, step=0.015, wander=0.22, branch_p=0.16, spread=0.55, w0=0.55,
                         phase=0.3)
        self.v1_core, self.v1_glow = raster_segments(prim, cres, 0.020)
        self.v2_core, self.v2_glow = raster_segments(sec, cres, 0.008)

    # -- the outer cap surface, in cap space (origin = top of stalk, z up)
    def cap_radius(self, th):
        p = self.ph
        return self.cap_R0 * (1 + 0.075 * np.sin(5 * th + p[0]) + 0.035 * np.sin(9 * th + p[1])
                              + 0.05 * (fbm(th / TAU * 12, 3.3, 3, self.seed, px=12) - 0.5))

    def cap_point(self, rho, th):
        p = self.ph
        R = self.cap_radius(th)
        ruffle_ph = 3.0 * fbm(th / TAU * 9, 1.7, 2, self.seed + 3, px=9)
        amp = 0.35 + 1.1 * fbm(th / TAU * 7, 4.1, 2, self.seed + 4, px=7)  # the frill comes and goes
        rr = rho * R + 0.0045 * rho ** 5 * amp * np.cos(29 * th + ruffle_ph)
        z = (0.030 - 0.026 * rho ** 1.6 - 0.030 * rho ** 3.5
             + 0.007 * rho ** 2 * np.sin(5 * th + p[0] + 0.6)
             + rho ** 5 * amp * (0.0065 * np.sin(29 * th + ruffle_ph) + 0.0025 * np.sin(13 * th + 2 * ruffle_ph)))
        return np.stack([rr * np.cos(th), rr * np.sin(th), z], -1)

    def crown_point(self, rho, th):
        p = self.ph
        R = 0.062 * (1 + 0.06 * np.sin(4 * th + p[2]) + 0.03 * np.sin(11 * th + p[3]))
        rr = rho * R + 0.0025 * rho ** 4 * np.cos(17 * th + p[4])
        z = 0.027 + 0.033 * (1 - rho ** 2) ** 0.85 - 0.004 * rho ** 3 + 0.004 * rho ** 4 * np.sin(17 * th + p[4])
        return np.stack([rr * np.cos(th), rr * np.sin(th), z], -1)

    def to_world(self, P):
        return P @ self.R.T + self.top

    # ---------------------------------------------------------------------------------------------------------
    def build(self):
        q = self.q
        A = Asset("veilcap")
        res = 1.0 if self.lod == 0 else 0.5
        mem = A.slot("membrane", res=(int(2048 * res), int(1024 * res)), mode="MASK", emissive_strength=4.0)
        lace = A.slot("veil", res=(int(2048 * res), int(1024 * res)), mode="MASK", emissive_strength=4.0)
        stalk = A.slot("stalk", res=(int(1024 * res), int(1024 * res)), mode="OPAQUE", double_sided=False,
                       emissive_strength=4.0)
        self.A = A

        # ---- membrane: outer cap, crown, gills
        r_cap = mem.region(0.0, 0.50, self.paint_cap, bump=6.0)
        r_under = mem.region(0.50, 0.66, self.paint_under, bump=4.0)
        r_crown = mem.region(0.66, 0.86, self.paint_crown, bump=8.0)
        r_gill = mem.region(0.86, 1.0, self.paint_gill, bump=4.0)

        nth = int(256 * q)
        nr = int(56 * q)
        th = np.linspace(0, TAU, nth, endpoint=False)
        # clockwise in u so cross(Pu, Pv) points up (out of the cap's top)
        th = -th
        rho = np.linspace(0.03, 1.0, nr) ** 0.9
        RH, TH = np.meshgrid(rho, th, indexing="ij")
        capl = self.cap_point(RH, TH)
        mem.add_grid(self.to_world(capl), r_cap, wrap_u=True, t_range=(rho[0], 1.0))
        # the underside: a second sheet 1.2 mm below (thicker toward the centre), its own darker, ribbed paint
        under = capl - np.array([0, 0, 1.0])[None, None, :] * (0.0012 + 0.004 * (1 - RH) ** 2)[..., None]
        mem.add_grid(self.to_world(under), r_under, wrap_u=True, t_range=(rho[0], 1.0))

        ncth = int(160 * q)
        ncr = int(30 * q)
        th = -np.linspace(0, TAU, ncth, endpoint=False)
        rho = np.linspace(0.012, 1.0, ncr) ** 0.85
        RH, TH = np.meshgrid(rho, th, indexing="ij")
        crown = self.to_world(self.crown_point(RH, TH))
        mem.add_grid(crown, r_crown, wrap_u=True, t_range=(rho[0], 1.0))

        self.build_gills(mem, r_gill)

        # ---- veil
        r_veil = lace.region(0.0, 1.0, self.paint_veil, bump=3.0)
        self.build_veil(lace, r_veil)

        # ---- stalk, roots, filaments
        r_stalk = stalk.region(0.0, 0.62, self.paint_stalk, bump=5.0)
        r_root = stalk.region(0.62, 0.80, self.paint_root, bump=4.0)
        r_fil = stalk.region(0.80, 1.0, self.paint_filament, bump=2.0)
        self.build_stalk(stalk, r_stalk)
        self.build_roots(stalk, r_root)
        self.build_filaments(stalk, r_fil)
        return A

    # ---------------------------------------------------------------------------------------------------------
    def build_gills(self, slot, region):
        rng = np.random.default_rng(self.seed + 11)
        n = 112 if self.lod == 0 else 48
        ncol = int(30 * self.q) if self.lod == 0 else 8
        nrow = 5 if self.lod == 0 else 2
        for k in range(n):
            kind = 0 if k % 4 == 0 else (1 if k % 4 == 2 else 2)
            if self.lod and kind == 2:
                continue
            r0 = [0.14, 0.46, 0.70][kind] + rng.uniform(-0.03, 0.03)
            r1 = 0.95 + rng.uniform(-0.02, 0.015)
            dmax = [0.017, 0.012, 0.0075][kind] * rng.uniform(0.85, 1.15)
            th0 = -TAU * (k + rng.uniform(-0.25, 0.25)) / n
            rho = np.linspace(r0, r1, ncol)
            x = (rho - r0) / (r1 - r0)
            th = th0 + 0.035 * np.sin(math.pi * x) * rng.uniform(-1, 1)
            top = self.cap_point(rho, th) - np.array([0, 0, 0.0016]) - np.array([0, 0, 1.0]) * (0.004 * (1 - rho) ** 2)[:, None]
            depth = dmax * np.sin(math.pi * np.clip(x, 0, 1)) ** 0.55
            depth *= 1 + 0.12 * np.sin(23 * x + k)
            # lateral direction (tangential) for the wavy free edge
            tang = np.stack([-np.sin(th), np.cos(th), np.zeros_like(th)], -1)
            wav = 0.0011 * np.sin(41 * x + 1.7 * k) * np.sin(math.pi * x)
            rows = []
            for j in range(nrow):
                f = j / (nrow - 1)
                p = top.copy()
                p[:, 2] -= depth * f
                p += tang * (wav * f ** 1.5)[:, None]
                p[:, 2] += 0.0012 * np.sin(57 * x + k) * f * np.sin(math.pi * x)
                rows.append(p)
            P = self.to_world(np.array(rows))
            slot.add_grid(P, region)
            # emitter knots along the free edge (sparse)
            if self.lod == 0 and kind == 0:
                for xi in (0.35, 0.6, 0.82):
                    i = int(xi * (ncol - 1))
                    self.A.emitters.append(list(P[-1, i]) + [1.0, xi, 0.0012])

    def build_veil(self, slot, region):
        q = self.q
        nth = int(160 * q)
        nv = int(44 * q)
        th = -np.linspace(0, TAU, nth, endpoint=False)  # clockwise: cross(Pu, Pv) faces outward
        v = np.linspace(0, 1, nv)
        V, TH = np.meshgrid(v, th, indexing="ij")
        hem = 0.135 * (1 + 0.16 * np.sin(3 * TH + 0.7) + 0.08 * np.sin(7 * TH + 2.0))
        z = -0.006 - hem * V
        pleat = 1 + 0.10 * V * np.sin(11 * TH + 1.3) + 0.05 * V * np.sin(23 * TH)
        r = (0.0175 + 0.058 * V ** 0.9) * pleat
        # hangs a bit outward on the downhill side and sways
        P = np.stack([r * np.cos(TH) + 0.006 * V ** 2, r * np.sin(TH), z], -1)
        # the veil hangs from the stalk under the cap: cap space but only half tilted (gravity)
        Pw = P @ (rot_z(0.4) @ rot_y(0.05)).T + self.top
        slot.add_grid(Pw, region, wrap_u=True)

    def build_stalk(self, slot, region):
        q = self.q
        n = len(self.stalk) if self.lod == 0 else 40
        curve = self.stalk if self.lod == 0 else self.stalk[np.linspace(0, len(self.stalk) - 1, n).astype(int)]
        t = np.linspace(0, 1, n)
        radius = (0.0105 + 0.002 * np.sin(math.pi * t * 1.3) + 0.014 * np.exp(-t / 0.05)
                  + 0.010 * smoothstep(0.86, 1.0, t) ** 2)

        def shape(th, tt):
            flute = 1 + 0.07 * np.cos(7 * th[None, :] + 3.0 * tt[:, None]) * (0.3 + 0.7 * smoothstep(0.1, 0.5, tt[:, None]))
            flat = 1 + 0.08 * np.cos(2 * th[None, :])
            lump = 1 + 0.06 * (fbm(th[None, :] / TAU * 6, tt[:, None] * 14, 3, self.seed + 5, px=6) - 0.5)
            return flute * flat * lump
        sides = int(36 * q) if self.lod == 0 else 12
        P = tube_grid(curve, radius, sides, shape)
        slot.add_grid(P, region, wrap_u=True)

    def build_roots(self, slot, region):
        rng = np.random.default_rng(self.seed + 21)
        nr = 9 if self.lod == 0 else 4
        for k in range(nr):
            a = TAU * k / nr + rng.uniform(-0.3, 0.3)
            L = rng.uniform(0.035, 0.10) * (1.0 if k % 3 else 0.6)
            d = np.array([math.cos(a), math.sin(a), 0])
            side = np.array([-math.sin(a), math.cos(a), 0])
            w1, w2 = rng.uniform(-0.35, 0.35, 2)
            n = 28 if self.lod == 0 else 9
            curve = bezier(d * 0.008 + [0, 0, 0.016], d * 0.018 + [0, 0, 0.004],
                           d * L * 0.55 + side * L * w1 + [0, 0, 0.0015],
                           d * L + side * L * w2 + [0, 0, -0.003], n)
            # a little meander
            t = np.linspace(0, 1, n)
            curve += side[None, :] * (0.003 * np.sin(t * 9 + k) * t)[:, None]
            rad = 0.0068 * (1 - t) ** 1.6 + 0.0005

            def flat(th, tt):  # buttress: flattened against the ground near the foot
                return 1 + 0.35 * (1 - tt[:, None]) * np.cos(2 * th[None, :])
            P = tube_grid(curve, rad, 10 if self.lod == 0 else 5, flat)
            slot.add_grid(P, region, wrap_u=True, s_range=(k / nr, (k + 1) / nr))

    def build_filaments(self, slot, region):
        rng = np.random.default_rng(self.seed + 31)
        nf = 22 if self.lod == 0 else 8
        for k in range(nf):
            th = -TAU * (k + rng.uniform(0, 0.8)) / nf
            rho0 = rng.uniform(0.9, 0.98)
            p0 = self.cap_point(np.array(rho0), np.array(th))
            L = rng.uniform(0.018, 0.075)
            out = np.array([math.cos(th), math.sin(th), 0.0])
            curve = bezier(p0, p0 + out * 0.006 - [0, 0, 0.004], p0 + out * L * 0.35 - [0, 0, L * 0.6],
                           p0 + out * L * rng.uniform(0.1, 0.4) - [0, 0, L], 16 if self.lod == 0 else 6)
            t = np.linspace(0, 1, len(curve))
            rad = 0.0007 * (1 - 0.6 * t) + 0.0008 * smoothstep(0.88, 0.97, t) * (1 - smoothstep(0.97, 1.0, t)) ** 0.5
            rad[-1] = 0.0002
            P = self.to_world(tube_grid(curve, rad, 6 if self.lod == 0 else 4))
            slot.add_grid(P, region, wrap_u=True, s_range=(k / nf, (k + 1) / nf))
            tip = P[-3].mean(0)
            self.A.emitters.append(list(tip) + [1.0, k / nf, 0.0018])

    # ---------------------------------------------------------------------------------------------------------
    # painters: (S, T) in the region -> channels (linear colour)

    def _cap_veins(self, S, T):
        X = T * np.cos(-TAU * S)
        Y = T * np.sin(-TAU * S)
        return (sample_canvas(self.v1_core, X, Y), sample_canvas(self.v1_glow, X, Y),
                sample_canvas(self.v2_core, X, Y), sample_canvas(self.v2_glow, X, Y), X, Y)

    def _rim_alpha(self, S, T):
        teeth = fbm(S * 420, 2.0, 2, self.seed + 45, px=420)
        lobes = fbm(S * 30, 5.0, 2, self.seed + 47, px=30)
        return (T < 0.975 + 0.012 * teeth + 0.01 * lobes).astype(float)

    def paint_cap(self, S, T):
        sd = self.seed
        c1, g1, c2, g2, X, Y = self._cap_veins(S, T)
        mott = fbm(X * 9 + 2, Y * 9, 4, sd + 44)
        blotch = smoothstep(0.35, 0.75, fbm(X * 3 + 7, Y * 3, 3, sd + 48))
        rings = 0.5 + 0.5 * np.sin(T * 85 + 5 * fbm(X * 4, Y * 4, 2, sd + 43))
        thin = smoothstep(0.3, 1.0, T)
        rim = smoothstep(0.88, 0.99, T)
        base = lerp(TEAL_DARK, TEAL_THIN, thin * (0.6 + 0.4 * mott))
        base = base + col([0.015, 0.05, 0.035], c1) + col([0.01, 0.03, 0.03], c2)
        vis = 0.5 + 0.7 * blotch  # glow pools in patches, not uniformly
        e = (col(EMERALD, c1 * (0.25 + 0.65 * T ** 1.2) * vis)
             + col(EMERALD, g1 ** 2 * 0.06 * (0.3 + T))
             + col(lerp(EMERALD, CYAN, T), c2 * 0.45 * T ** 1.2 * vis)
             + col(CYAN, rim ** 3 * 0.40 * (0.4 + 0.6 * mott))
             # the translucent body: a dim inner light, stronger where the membrane thins
             + col(lerp(EMERALD, CYAN, 0.4 + 0.5 * T), 0.0015 + 0.006 * thin * (0.3 + mott)))
        height = 0.7 * c1 + 0.35 * c2 + 0.02 * rings + 0.25 * mott
        rough = 0.62 + 0.12 * mott - 0.08 * c1
        return dict(base=base, emis=e, alpha=self._rim_alpha(S, T), height=height, rough=rough, ao=0.8 + 0.2 * T)

    def paint_under(self, S, T):
        sd = self.seed + 60
        c1, g1, c2, g2, X, Y = self._cap_veins(S, T)
        # ribs where the lamellae attach, a dark velvet between them
        rib = 1 - np.abs(2 * ((S * 112 + 0.3 * fbm(S * 20, T * 3, 2, sd, px=20)) % 1.0) - 1)
        rib = smoothstep(0.75, 1.0, rib)
        mott = fbm(X * 12, Y * 12, 3, sd + 1)
        rim = smoothstep(0.9, 0.99, T)
        base = lerp(np.array([0.006, 0.014, 0.018]), np.array([0.03, 0.07, 0.07]), 0.3 * T + 0.4 * rib)
        # the top's veins show through the thin membrane, dimly
        e = (col(EMERALD, (c1 * 0.10 + g1 * 0.04) * T) + col(CYAN, rim ** 2 * 0.18)
             + col(EMERALD, rib * 0.025 * smoothstep(0.1, 0.9, T)))
        height = 0.5 * rib + 0.2 * mott
        return dict(base=base, emis=e, alpha=self._rim_alpha(S, T), height=height, rough=0.55 + 0.2 * mott, ao=0.5 + 0.4 * T)

    def paint_crown(self, S, T):
        sd = self.seed + 100
        X = T * np.cos(TAU * S)
        Y = T * np.sin(TAU * S)
        wx = X + 0.03 * (fbm(X * 6 + 5, Y * 6, 2, sd) - 0.5)
        wy = Y + 0.03 * (fbm(X * 6, Y * 6 + 9, 2, sd + 1) - 0.5)
        cells = 12.0 + 6.0 * T  # finer toward the rim
        F1, F2, F3, h = worley(wx * cells, wy * cells, sd + 2, jitter=0.9)
        wall = 1 - smoothstep(0.04, 0.12, F2 - F1)
        knot = 1 - smoothstep(0.03, 0.12, F3 - F1)
        dome = 1 - smoothstep(0.0, 0.7, F1)
        open_p = smoothstep(0.6, 0.97, T) * 0.9
        hole = (h < open_p) & (wall < 0.5)
        alpha = 1.0 - hole.astype(float)
        alpha = alpha * (T < 0.985 + 0.01 * h)
        mott = fbm(X * 9 + 3, Y * 9, 3, sd + 3)
        patch = smoothstep(0.4, 0.8, fbm(X * 3 + 1, Y * 3 + 4, 3, sd + 4))
        base = lerp(TEAL_DARK, TEAL_THIN * 0.8, 0.2 + 0.5 * T) * (0.6 + 0.4 * dome)[..., None]
        base = base + col([0.02, 0.05, 0.04], wall)
        glow_w = wall * (0.08 + 0.50 * smoothstep(0.4, 1.0, T)) * (0.4 + 0.8 * patch)
        e = col(lerp(EMERALD, CYAN, 0.35 + 0.4 * T), glow_w) + col(CYAN, knot * wall * 0.28 * smoothstep(0.5, 0.95, T))
        e = e * (0.7 + 0.4 * mott)[..., None]
        height = 0.7 * wall + 0.45 * dome * (1 - wall) + 0.1 * mott
        rough = 0.35 + 0.25 * (1 - wall) * mott
        return dict(base=base, emis=e, alpha=alpha, height=height, rough=rough, ao=0.7 + 0.3 * wall)

    def paint_gill(self, S, T):
        sd = self.seed + 200
        # S along the radius (centre -> rim), T from the cap (0) to the free edge (1)
        stri = 0.5 + 0.5 * np.sin(T * 120 + 8 * fbm(S * 10, T * 3, 2, sd))
        edge = smoothstep(0.80, 1.0, T) ** 2
        beads = smoothstep(0.7, 0.95, vnoise(S * 90, 3.0, sd + 1)) * smoothstep(0.9, 0.99, T)
        mott = fbm(S * 30, T * 8, 3, sd + 2)
        along = smoothstep(0.0, 0.5, S)
        base = lerp(np.array([0.006, 0.014, 0.016]), np.array([0.05, 0.12, 0.11]), smoothstep(0.5, 1.0, T))
        e = (col(lerp(EMERALD, CYAN, S), edge * (0.40 + 0.35 * mott) * (0.4 + 0.6 * along)) + col(EMERALD, 0.008 * T)
             + col(np.array([0.5, 1.0, 0.7]), beads * 0.55))
        height = 0.25 * stri + 0.5 * beads + 0.2 * mott
        alpha = (T < 0.975 + 0.025 * fbm(S * 140, 1.0, 2, sd + 3)).astype(float)
        return dict(base=base, emis=e, alpha=alpha, height=height, rough=0.3 + 0.2 * mott, ao=0.4 + 0.6 * T)

    def paint_veil(self, S, T):
        sd = self.seed + 300
        # physical-ish coordinates: circumference grows down the veil
        circ = 0.11 + 0.36 * T ** 0.9
        cells_u = 46
        # cells get bigger toward the hem
        y = T * 13 + 6 * T ** 1.5
        x = S * cells_u
        wx = x + 0.6 * (fbm(S * 8, T * 4, 2, sd, px=8) - 0.5)
        wy = y + 0.6 * (fbm(S * 8 + 3, T * 4, 2, sd + 1, px=8) - 0.5)
        F1, F2, F3, h = worley(wx, wy, sd + 2, px=cells_u, jitter=0.9)
        wall = 1 - smoothstep(0.045, 0.10, F2 - F1)
        knot = 1 - smoothstep(0.05, 0.2, F3 - F1)
        # irregular hem: strands stop at a ragged line, a few dangle further
        hem = 0.86 + 0.1 * fbm(S * 14, 2.0, 3, sd + 3, px=14)
        alive = smoothstep(hem + 0.03, hem, T)
        alpha = (wall > 0.5).astype(float) * (alive > 0.5)
        # a membrane collar at the top where the veil attaches
        collar = T < 0.05 + 0.02 * fbm(S * 30, 1.0, 2, sd + 4, px=30)
        alpha = np.maximum(alpha, collar.astype(float))
        base = lerp(np.array([0.01, 0.02, 0.05]), np.array([0.03, 0.06, 0.14]), wall)
        e = col(lerp(BLUE, CYAN, 0.2 + 0.4 * h), wall * (0.14 + 0.2 * (1 - T))) + col(CYAN, knot * wall * 0.5)
        e = e * (0.6 + 0.6 * fbm(S * 20, T * 6, 2, sd + 5, px=20))[..., None]
        height = wall + 0.5 * knot
        return dict(base=base, emis=e, alpha=alpha, height=height, rough=0.35, ao=1.0)

    def paint_stalk(self, S, T):
        sd = self.seed + 400
        veins = ridge(S * 14, T * 2.5, 3, sd, px=14)
        vein = smoothstep(0.93, 0.985, veins)
        # reticulate net on the upper stalk (a raised, stretched mesh)
        F1, F2, _, h = worley(S * 22, T * 30, sd + 1, px=22)
        net = (1 - smoothstep(0.05, 0.14, F2 - F1)) * smoothstep(0.45, 0.8, T)
        mott = fbm(S * 30, T * 40, 3, sd + 2, px=30)
        velvet = 1 - smoothstep(0.0, 0.35, T)
        fuzz = fbm(S * 160, T * 220, 2, sd + 3, px=160)  # velvet nap
        up = smoothstep(0.08, 1.0, T)
        base = lerp(STALK_DARK, np.array([0.025, 0.06, 0.06]), 0.15 + 0.55 * up + 0.3 * net)
        base = base * (0.75 + 0.5 * mott)[..., None]
        # translucent pale green-cyan rising up the stalk, sap veins and the net catching it
        glow = lerp(np.array([0.05, 0.9, 0.45]), np.array([0.15, 0.9, 1.0]), up)
        e = (col(glow, (0.0015 + 0.018 * up ** 2) * (0.5 + 0.7 * mott))
             + col(lerp(BLUE, CYAN, up), vein * 0.5 * up) + col(CYAN, net * 0.16 * up)
             + col(EMERALD, 0.35 * smoothstep(0.95, 1.0, T)))
        height = 0.5 * net + 0.35 * vein + 0.4 * mott * (0.4 + velvet) + 0.15 * fuzz
        rough = 0.78 + 0.15 * velvet + 0.07 * fuzz
        return dict(base=base, emis=e, height=height, rough=rough, ao=0.55 + 0.45 * smoothstep(0, 0.3, T))

    def paint_root(self, S, T):
        mott = fbm(S * 40, T * 20, 3, self.seed + 500)
        base = STALK_DARK * (0.8 + 0.6 * mott)[..., None]
        e = col(BLUE, 0.15 * smoothstep(0.85, 1.0, T))
        return dict(base=base, emis=e, height=mott, rough=0.8, ao=0.5 + 0.5 * T)

    def paint_filament(self, S, T):
        lit = 0.2 + 0.8 * ((np.floor(S * 22) * 0.618) % 1.0) ** 1.5  # each filament's bead its own brightness
        tip = smoothstep(0.84, 0.95, T) * lit
        e = col(lerp(CYAN, np.array([0.45, 1.0, 0.6]), tip), 0.05 + 0.75 * tip) + col(EMERALD, 0.008 * T)
        base = lerp(np.array([0.02, 0.05, 0.06]), np.array([0.1, 0.3, 0.2]), tip)
        return dict(base=base, emis=e, height=np.zeros_like(S), rough=0.25, ao=1.0)


def make(lod=0, seed=7):
    return Veilcap(seed, lod).build()

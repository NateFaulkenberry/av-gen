"""Scene support, the SHADOWBOUGH: a big, dark foreground framing tree.

For the vista's left frame and the riverbank's edges (reference panels 1 and 2): almost all silhouette. About 30 m tall:
  - a gnarled trunk leaning out over the water (+X) on a ring of tall, plank-like buttress roots;
  - long near-horizontal limbs that sag under their weight and fork down to twigs;
  - moss along the upper side of every limb.
The foliage hangs in ragged, layered masses:
  - horizontal pads of leaf clusters along the outer limbs;
  - curtains of drooping leaf strands beneath them, alpha-cut cards crossed in pairs.
Light is scarce: a few luminous pods, a faint speckle in the moss, a cold glint at some leaf tips.

LOD0 is for 20-40 m from camera; LOD1 (generations 0-2, fewer and larger foliage cards) is for 100 m and beyond.
Units metres, Z up in Blender (Y up exported), origin at the base of the trunk.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (CYAN, EMERALD, Asset, add_branches, bezier, col, fbm, grow_branches, lerp, ridge, smoothstep,
                 tube_up)

TAU = 2 * math.pi
K = 6.5  # the tree was designed at a smaller scale


def _draw_leaf(mask, rib, tips, xs, ys, x0, y0, dx, dy, L, wmax, curve):
    H, W = mask.shape
    x1, y1 = x0 + dx * L, y0 + dy * L
    lo_x, hi_x = min(x0, x1) - wmax, max(x0, x1) + wmax
    lo_y, hi_y = min(y0, y1) - wmax, max(y0, y1) + wmax
    i0 = max(int(np.searchsorted(xs, lo_x)), 0)
    i1 = min(int(np.searchsorted(xs, hi_x)) + 1, W)
    j0 = max(int(np.searchsorted(ys, lo_y)), 0)
    j1 = min(int(np.searchsorted(ys, hi_y)) + 1, H)
    if i0 >= i1 or j0 >= j1:
        return
    XX, YY = np.meshgrid(xs[i0:i1], ys[j0:j1])
    u = ((XX - x0) * dx + (YY - y0) * dy) / L
    v = (-(XX - x0) * dy + (YY - y0) * dx) - curve * L * u ** 2
    w = wmax * np.clip(np.sin(math.pi * np.clip(u, 0, 1)) ** 0.75, 0, 1) * (u > 0) * (u < 1)
    leaf = 1 - smoothstep(w * 0.85, w + 1e-4, np.abs(v))
    sl = (slice(j0, j1), slice(i0, i1))
    mask[sl] = np.maximum(mask[sl], leaf)
    rib[sl] = np.maximum(rib[sl], leaf * (1 - smoothstep(0.0, wmax * 0.18, np.abs(v))))
    tips[sl] = np.maximum(tips[sl], leaf * smoothstep(0.8, 1.0, u))


def paint_strand(H, W, length, width, rng, n_leaf):
    """Drooping leaves on a thread: (H, W) canvas spanning width x length (card units); v = 0 at the top."""
    ys = (np.arange(H) + 0.5) / H * length
    xs = (np.arange(W) + 0.5) / W * width - width / 2
    mask, rib, tips = np.zeros((H, W)), np.zeros((H, W)), np.zeros((H, W))
    thread_x = 0.004 * np.sin(ys * 9.0)
    mask = np.maximum(mask, 1 - smoothstep(0.0012, 0.0024, np.abs(xs[None, :] - thread_x[:, None])))
    for _ in range(n_leaf):
        t0 = rng.uniform(0.02, 0.97) * length
        side = rng.choice([-1, 1])
        L = rng.uniform(0.035, 0.075) * (1.15 - 0.4 * t0 / length)
        ang = math.radians(rng.uniform(20, 55))
        _draw_leaf(mask, rib, tips, xs, ys, 0.004 * math.sin(t0 * 9.0), t0, side * math.sin(ang), math.cos(ang),
                   L, L * rng.uniform(0.16, 0.24), 0.12 * side)
    return mask, rib, tips


def paint_clump(N, rng, n_leaf):
    """A ragged mass of leaves on twigs, seen from above or below, on an N x N canvas of the unit square."""
    xs = (np.arange(N) + 0.5) / N
    ys = xs.copy()
    mask, rib, tips = np.zeros((N, N)), np.zeros((N, N)), np.zeros((N, N))
    for _ in range(n_leaf):
        a = rng.uniform(0, TAU)
        rr = 0.42 * math.sqrt(rng.uniform(0, 1)) * (0.75 + 0.25 * math.sin(5 * a + 1.3))
        x0, y0 = 0.5 + rr * math.cos(a), 0.5 + rr * math.sin(a)
        ang = a + rng.normal(0, 0.6)
        L = rng.uniform(0.04, 0.085)
        _draw_leaf(mask, rib, tips, xs, ys, x0, y0, math.cos(ang), math.sin(ang), L, L * rng.uniform(0.18, 0.26),
                   rng.uniform(-0.15, 0.15))
    return mask, rib, tips


class Bough:
    def __init__(self, seed=91, lod=0):
        self.seed = seed
        self.lod = lod
        self.lod0 = lod == 0
        res = 1.0 if self.lod0 else 0.5
        self.strand = [paint_strand(int(2048 * res), int(256 * res), 1.4, 0.2, np.random.default_rng(seed + 10 + k),
                                    int(110 * res) + 20) for k in range(2)]
        self.clump = [paint_clump(int(1024 * res), np.random.default_rng(seed + 20 + k), int(900 * res) + 100)
                      for k in range(2)]

    def build(self):
        rng = np.random.default_rng(self.seed)
        lod0 = self.lod0
        A = Asset("shadowbough")
        self.A = A
        res = 1.0 if lod0 else 0.5
        bark = A.slot("bark", res=(int(1024 * res), int(2048 * res)), mode="OPAQUE", double_sided=False,
                      emissive_strength=4.0)
        leaf = A.slot("foliage", res=(int(2048 * res), int(2048 * res)), mode="MASK", emissive_strength=4.0)
        pod = A.slot("pod", res=(int(128 * res), int(128 * res)), mode="OPAQUE", double_sided=False,
                     emissive_strength=4.0)
        br = grow_branches(rng, [0, 0, -0.15 * K], [0.45, 0.05, 1.0], 2.4 * K, 0.24 * K, max_gen=4,
                           children=(2, 3), spread=(0.5, 1.0), shrink=0.72, rshrink=0.6, curl=0.35, up=0.0,
                           segs=18 if lod0 else 8, crozier=0.0, min_r=0.03, child_t=(0.4, 1.0), min_dz=0.08,
                           sink=0.9)
        for b in br:  # heavy limbs sag with their reach
            reach = np.hypot(b.pts[:, 0], b.pts[:, 1])
            b.pts[:, 2] -= 0.06 * K * np.maximum(reach / K - 1.0, 0) ** 1.5 * (b.gen > 0)

        def gnarl(th, t):
            return (1 + 0.14 * (fbm(th[None, :] * 0.8, t[:, None] * 8, 3, self.seed + 4) - 0.5) * 2
                    + 0.06 * np.cos(3 * th[None, :] + t[:, None] * 5))
        rb = bark.region(0, 1, self.paint_bark, bump=6.0)
        keep = [b for b in br if lod0 or b.gen <= 2]
        add_branches(bark, rb, keep, lambda b: max([22, 14, 10, 7, 5][min(b.gen, 4)] - (0 if lod0 else 8), 4),
                     gnarl, seed=self.seed, up_frame=True)
        # buttress roots: tall, thin planks flaring from the trunk's foot
        nroot = 7 if lod0 else 5
        for j in range(nroot):
            a = TAU * j / nroot + rng.uniform(-0.3, 0.3)
            d = np.array([math.cos(a), math.sin(a), 0.0])
            reach = rng.uniform(2.2, 4.0)
            n = 16 if lod0 else 6
            c = bezier(d * 0.6 + [0, 0, 2.6], d * 1.2 + [0, 0, 1.2], d * reach * 0.7 + [0, 0, 0.3],
                       d * reach + [0, 0, -0.3], n)
            t = np.linspace(0, 1, n)
            rad = 0.75 * (1 - t) ** 1.2 + 0.08

            def plank(th, tt):  # tall and thin: squash across the horizontal
                return (np.abs(np.cos(th[None, :])) + 0.3 * np.abs(np.sin(th[None, :]))) * np.ones_like(tt)[:, None]
            bark.add_grid(tube_up(c, rad, 10 if lod0 else 6, plank), rb, wrap_u=True, t_range=(0.0, 0.05))
        # foliage: two strand variants and two clump variants side by side
        regs = [leaf.region(k / 4, (k + 1) / 4, self._paint_foliage(k), bump=2.0) for k in range(4)]
        rp = pod.region(0, 1, self.paint_pod, bump=1.0)
        for b in [b for b in br if b.gen >= 2]:
            npad = (3 if b.gen >= 3 else 2) if lod0 else (1 if (b.gen == 2 or rng.random() < 0.45) else 0)
            for _ in range(npad):
                t = rng.uniform(0.35, 1.0)
                i = min(int(t * (len(b.pts) - 1)), len(b.pts) - 1)
                self.pad(leaf, regs[2 + rng.integers(0, 2)], b.pts[i], rng, big=not lod0)
            nstr = (2 if b.gen >= 3 else 1) if lod0 else (1 if rng.random() < 0.3 else 0)
            for _ in range(nstr):
                t = rng.uniform(0.3, 1.0)
                i = min(int(t * (len(b.pts) - 1)), len(b.pts) - 1)
                p0 = b.pts[i]
                if p0[2] < 4.0:
                    continue
                L = rng.uniform(2.5, 8.0) * min(1.0, (p0[2] - 1.5) / 9.0)
                self.strand_cards(leaf, regs[rng.integers(0, 2)], p0, L, rng.uniform(0, math.pi), rng)
                if rng.random() < 0.05:
                    self.pod(pod, rp, p0 + np.array([0, 0, -rng.uniform(0.3, 0.8)]), rng)
        return A

    def pad(self, slot, reg, c, rng, big=False):
        """A ragged leaf mass: stacked, turned, roughly horizontal cards whose rims sag."""
        size = rng.uniform(2.8, 4.6) * (1.5 if big else 1.0)
        for j in range(2 if big else 3):
            yaw = rng.uniform(0, TAU)
            tilt = rng.normal(0, 0.25, 2)
            dz = -0.25 * j + rng.uniform(-0.15, 0.15)
            u = np.linspace(-0.5, 0.5, 4 if self.lod0 else 2)
            UU, VV = np.meshgrid(u, u)
            x, y = UU * size, VV * size
            z = -0.35 * (UU ** 2 + VV ** 2) * size + tilt[0] * x + tilt[1] * y
            ca, sa = math.cos(yaw), math.sin(yaw)
            slot.add_grid(np.stack([ca * x - sa * y, sa * x + ca * y, z + dz], -1) + c, reg)

    def strand_cards(self, slot, reg, p0, L, yaw, rng):
        n = 10 if self.lod0 else 4
        t = np.linspace(0, 1, n)
        sway = rng.normal(0, 0.06, 2)
        C = np.stack([p0[0] + sway[0] * t ** 2 * L, p0[1] + sway[1] * t ** 2 * L, p0[2] - L * t], -1)
        W = 0.6 * (L / 1.4) ** 0.5 * (1 - 0.35 * t)
        for a in ((yaw, yaw + math.pi / 2) if self.lod0 else (yaw,)):
            a2 = a + 0.4 * t * rng.uniform(-1, 1)
            side = np.stack([np.cos(a2), np.sin(a2), np.zeros_like(t)], -1)
            P = np.stack([C - side * (W / 2)[:, None], C + side * (W / 2)[:, None]], 1)
            slot.add_grid(P, reg, t_range=(0, min(L / 7.0, 1.0)))

    def pod(self, slot, reg, c, rng):
        nv, nt = (8, 10) if self.lod0 else (4, 5)
        V, TH = np.meshgrid(np.linspace(0, 1, nv), -np.linspace(0, TAU, nt, endpoint=False), indexing="ij")
        R = rng.uniform(0.06, 0.1)
        r = R * np.sin(math.pi * V) ** 0.8
        slot.add_grid(np.stack([r * np.cos(TH), r * np.sin(TH), -R * 2.2 * V], -1) + c, reg, wrap_u=True)
        self.A.emitters.append(list(c + np.array([0, 0, -R * 1.1])) + [0.5, 0.5, R])

    def _paint_foliage(self, k):
        mask, rib, tips = (self.strand + self.clump)[k]

        def paint(S, T):
            H, W = mask.shape
            j = np.clip((T * H).astype(int), 0, H - 1)
            i = np.clip((S * W).astype(int), 0, W - 1)
            m, r, tp = mask[j, i], rib[j, i], tips[j, i]
            mott = fbm(S * 6, T * 20, 2, self.seed + 50 + k)
            base = lerp(np.array([0.006, 0.012, 0.010]), np.array([0.018, 0.035, 0.028]), mott) + col([0.01, 0.02, 0.015], r)
            glint = tp * smoothstep(0.84, 0.95, fbm(S * 3 + k, T * 30, 2, self.seed + 60 + k))
            e = col(lerp(EMERALD, CYAN, 0.6), glint * 0.12)
            return dict(base=base, emis=e, alpha=(m > 0.5).astype(float), height=0.6 * r + 0.3 * m, rough=0.8,
                        ao=0.6 + 0.4 * mott)
        return paint

    def paint_bark(self, S, T):
        sd = self.seed + 30
        furrow = ridge(S * 9, T * 3.0, 3, sd, px=9)
        plates = smoothstep(0.55, 0.9, furrow)
        mott = fbm(S * 30, T * 120, 3, sd + 1, px=30)
        top = 0.5 + 0.5 * np.cos(TAU * S)  # u = 0 faces up on every limb: moss lies along the top
        moss = smoothstep(0.55, 0.8, top + 0.35 * (fbm(S * 8, T * 40, 3, sd + 2, px=8) - 0.5) * 2)
        mossn = fbm(S * 120, T * 600, 2, sd + 4, px=120)
        speck = smoothstep(0.88, 0.95, fbm(S * 200, T * 900, 2, sd + 3, px=200)) * moss
        base = lerp(np.array([0.010, 0.010, 0.012]), np.array([0.03, 0.028, 0.03]), plates * (0.6 + 0.4 * mott))
        base = base * (1 - moss)[..., None] + col([0.012, 0.03, 0.018], moss * (0.6 + 0.6 * mossn))
        e = col(lerp(EMERALD, CYAN, mott), speck * 0.15)
        height = 0.9 * plates + 0.3 * mott + 0.4 * moss * mossn
        return dict(base=base, emis=e, height=height, rough=0.9 - 0.1 * moss, ao=0.3 + 0.7 * plates)

    def paint_pod(self, S, T):
        core = np.sin(math.pi * T)
        return dict(base=col(CYAN, 0.08), emis=col(lerp(CYAN, EMERALD, 0.3), 0.15 + 0.35 * core),
                    height=np.zeros_like(S), rough=0.2, ao=1.0)


def make(lod=0, seed=91):
    return Bough(seed, lod).build()

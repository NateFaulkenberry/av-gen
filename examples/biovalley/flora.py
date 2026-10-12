"""The valley's large vegetation as generated meshes (CP2): canopy trees and groves for the floor, the hero framing
tree, arched trees for the meadow's horizon, shrubs for the mountains' ledges, boulders for the banks.

Every species is two parts with the same placements: `<name>_wood` (trunk and branches) and `<name>_leaf` (the
foliage masses), so each gets its own material, and later its own emitter layer.

Foliage is modelled as masses, not leaves: displaced, lumpy clumps whose silhouettes break up like a crown seen at
night against haze. The reference's trees are dark shapes; their detail is in the silhouette and in the life on them,
which the bioluminescence pass adds.

UVs: v = position from the root (0) to the tips (1) on wood, and height in the crown on leaves; u = a per-clump hash.
"""
from __future__ import annotations

import math

import numpy as np

from geom import Mesh, grid_faces

# ---- noise and primitives --------------------------------------------------------------------------------------------


def _h3(ix, iy, iz, seed):
    v = (ix * 374761393 + iy * 668265263 + iz * 1274126177 + seed * 2246822519) & 0xFFFFFFFF
    v = ((v ^ (v >> 13)) * 1274126177) & 0xFFFFFFFF
    return (v & 0xFFFFFF) / float(0xFFFFFF)


def noise3(p, seed):
    """Smooth 3D value noise in [0, 1] for points p (n, 3)."""
    p = np.asarray(p, float)
    i = np.floor(p).astype(np.int64)
    f = p - i
    f = f * f * (3 - 2 * f)
    out = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = ((f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) *
                     (f[:, 2] if dz else 1 - f[:, 2]))
                out = out + w * _h3(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz, seed)
    return out


def fbm3(p, seed, octaves=3):
    s, a, f, n = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        s = s + a * noise3(p * f + o * 17.31, seed + o * 7)
        n += a
        a *= 0.5
        f *= 2.07
    return s / n


_ICO = {}


def icosphere(level):
    if level in _ICO:
        return _ICO[level]
    g = (1 + 5 ** 0.5) / 2
    V = [[-1, g, 0], [1, g, 0], [-1, -g, 0], [1, -g, 0], [0, -1, g], [0, 1, g], [0, -1, -g], [0, 1, -g],
         [g, 0, -1], [g, 0, 1], [-g, 0, -1], [-g, 0, 1]]
    F = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6],
         [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11], [6, 2, 10],
         [8, 6, 7], [9, 8, 1]]
    V = [np.array(v, float) / np.linalg.norm(v) for v in V]
    for _ in range(level):
        cache = {}

        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key not in cache:
                m = V[a] + V[b]
                V.append(m / np.linalg.norm(m))
                cache[key] = len(V) - 1
            return cache[key]

        nf = []
        for a, b, c in F:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [[a, ab, ca], [b, bc, ab], [c, ca, bc], [ab, bc, ca]]
        F = nf
    _ICO[level] = (np.array(V), np.array(F))
    return _ICO[level]


def frames(pts):
    t = np.gradient(pts, axis=0)
    t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-9)
    ref = np.array([1.0, 0.0, 0.0]) if abs(t[0][1]) > 0.9 else np.array([0.0, 1.0, 0.0])
    n0 = np.cross(t[0], ref)
    n0 /= np.linalg.norm(n0)
    ns = [n0]
    for k in range(1, len(pts)):
        n = ns[-1] - np.dot(ns[-1], t[k]) * t[k]
        ln = np.linalg.norm(n)
        ns.append(ns[-1] if ln < 1e-6 else n / ln)
    ns = np.array(ns)
    return t, ns, np.cross(t, ns)


def tube(mesh, pts, radii, sides, v0=0.0, v1=1.0, u=0.0, gnarl=0.0, seed=0):
    """Sweep a ring along pts; `gnarl` adds lumpy bark (radial noise)."""
    pts = np.asarray(pts, float)
    radii = np.broadcast_to(np.asarray(radii, float), (len(pts),))
    t, ns, bs = frames(pts)
    ang = np.linspace(0, 2 * math.pi, sides, endpoint=False)
    ca, sa = np.cos(ang), np.sin(ang)
    R = radii[:, None] * np.ones((1, sides))
    if gnarl > 0:
        q = np.stack([np.repeat(np.arange(len(pts)) * 0.35, sides), np.tile(ca * 1.5, len(pts)),
                      np.tile(sa * 1.5, len(pts))], -1)
        R = R * (1.0 + gnarl * (fbm3(q, seed, 2) - 0.5).reshape(len(pts), sides))
    P = pts[:, None, :] + R[:, :, None] * (ca[None, :, None] * ns[:, None, :] + sa[None, :, None] * bs[:, None, :])
    vs = np.linspace(v0, v1, len(pts))
    UV = np.stack([np.full((len(pts), sides), u), np.repeat(vs[:, None], sides, 1)], -1)
    mesh.add(P.reshape(-1, 3), grid_faces(len(pts), sides, True)[:, [0, 2, 1]], UV.reshape(-1, 2))  # wound outward


def clump(mesh, centre, radius, seed, level=2, squash=0.72, lump=0.45, u=0.0, top=None, tuft_amp=0.22,
          tuft_freq=5.0):
    """A foliage mass: an icosphere pushed out by noise into lobes and tufts, flattened underneath."""
    V, F = icosphere(level)
    P = V * radius
    n = fbm3(V * 1.6 + seed * 0.37, seed, 3)
    tuft = fbm3(V * tuft_freq + seed * 0.11, seed + 5, 2)
    d = 1.0 + lump * (n - 0.5) * 2.0 + tuft_amp * 2.0 * (tuft - 0.5)
    P = P * d[:, None]
    P[:, 1] *= np.where(P[:, 1] < 0, squash * 0.75, squash)
    P = P + np.asarray(centre)
    hv = (P[:, 1] - (centre[1] - radius)) / (2 * radius) if top is None else P[:, 1] / top
    mesh.add(P, F, np.stack([np.full(len(P), u), np.clip(hv, 0, 1)], -1))


# ---- branching -------------------------------------------------------------------------------------------------------


def _rot(v, axis, ang):
    axis = axis / np.linalg.norm(axis)
    return v * math.cos(ang) + np.cross(axis, v) * math.sin(ang) + axis * np.dot(axis, v) * (1 - math.cos(ang))


def _curve(p0, d, length, n, rng, up_bias, wobble):
    pts = [np.asarray(p0, float)]
    d = np.asarray(d, float) / np.linalg.norm(d)
    step = length / (n - 1)
    for k in range(1, n):
        d = d + np.array([0, up_bias, 0]) + rng.normal(0, wobble, 3)
        d /= np.linalg.norm(d)
        pts.append(pts[-1] + d * step)
    return np.array(pts)


class Tree:
    """A recursive branching skeleton: wood tubes, and foliage clumps at the twig ends."""

    def __init__(self, seed, style):
        self.r = np.random.default_rng(seed)
        self.seed = seed
        self.style = style
        self.wood = Mesh()
        self.leaf = Mesh()
        self.tips = []

    def branch(self, p0, d, length, radius, depth, v0):
        S = self.style
        r = self.r
        n = max(4, int(6 + length / 1.5) // (1 + (S["depth"] - depth)))
        pts = _curve(p0, d, length, n, r, S["up"][depth], S["wobble"][depth])
        rad = np.linspace(radius, radius * S["taper"], n)
        sides = max(5, int(S["sides"] * (radius / S["r0"]) ** 0.4))
        v1 = v0 + (1.0 - v0) * 0.5
        tube(self.wood, pts, rad, sides, v0, v1, gnarl=S["gnarl"] if depth == S["depth"] else 0.0,
             seed=self.seed + depth)
        if depth == 0:
            self.tips.append((pts[-1], length))
            return
        kids = int(r.integers(*S["kids"][depth]))
        for k in range(kids):
            t = r.uniform(*S["at"][depth]) if k < kids - 1 else 1.0
            i = min(int(t * (n - 1)), n - 2)
            base = pts[i]
            tang = pts[i + 1] - pts[i]
            tang /= np.linalg.norm(tang)
            az = r.uniform(0, 2 * math.pi) + k * 2.4
            side = np.cross(tang, [0, 1, 0])
            if np.linalg.norm(side) < 1e-3:
                side = np.array([1.0, 0, 0])
            side = _rot(side / np.linalg.norm(side), tang, az)
            spread = math.radians(r.uniform(*S["spread"][depth]))
            nd = _rot(tang, side, spread)
            self.branch(base, nd, length * r.uniform(*S["ratio"][depth]), rad[i] * S["kid_r"],
                        depth - 1, v0 + (v1 - v0) * t)

    def foliage(self):
        S = self.style
        r = self.r
        for k, (p, length) in enumerate(self.tips):
            rad = S["clump"] * r.uniform(0.7, 1.3)
            self.clump_at(p + np.array([0, rad * 0.25, 0]), rad, k)

    def clump_at(self, p, rad, k):
        S = self.style
        clump(self.leaf, p, rad, self.seed * 31 + k, level=S["leaf_level"], u=(k * 0.618) % 1.0,
              squash=S.get("squash", 0.72), lump=S.get("lump", 0.45), tuft_amp=S.get("tuft", 0.22),
              tuft_freq=S.get("tuft_freq", 5.0))


STYLES = {
    # the floor's forest: a straight-ish trunk, a rounded crown of many clumps
    "canopy": {"depth": 3, "r0": 0.45, "sides": 8, "taper": 0.55, "gnarl": 0.25, "kid_r": 0.62,
               "up": {3: 0.10, 2: 0.06, 1: 0.03, 0: 0.0}, "wobble": {3: 0.05, 2: 0.10, 1: 0.14, 0: 0.15},
               "kids": {3: (3, 5), 2: (2, 3), 1: (2, 3)}, "at": {3: (0.45, 0.95), 2: (0.3, 0.9), 1: (0.3, 0.9)},
               "spread": {3: (30, 60), 2: (25, 50), 1: (20, 45)}, "ratio": {3: (0.45, 0.65), 2: (0.5, 0.75),
                                                                           1: (0.5, 0.75)},
               "clump": 3.1, "leaf_level": 2, "squash": 0.8, "lump": 0.35},
    # the hero framing tree: a gnarled leaning trunk, long near-horizontal limbs, clumps along them
    "hero": {"depth": 4, "r0": 1.3, "sides": 14, "taper": 0.45, "gnarl": 0.5, "kid_r": 0.58,
             "up": {4: 0.02, 3: -0.035, 2: 0.0, 1: 0.04, 0: 0.0}, "wobble": {4: 0.08, 3: 0.14, 2: 0.18, 1: 0.22, 0: 0.2},
             "kids": {4: (4, 7), 3: (3, 5), 2: (2, 4), 1: (2, 4)}, "at": {4: (0.3, 0.95), 3: (0.15, 0.95),
                                                                          2: (0.2, 0.95), 1: (0.3, 0.95)},
             "spread": {4: (70, 95), 3: (30, 60), 2: (25, 55), 1: (20, 50)},
             "ratio": {4: (0.85, 1.15), 3: (0.55, 0.75), 2: (0.5, 0.7), 1: (0.5, 0.7)},
             "clump": 1.6, "leaf_level": 3, "squash": 0.55, "lump": 0.35, "tuft": 0.3, "tuft_freq": 7.0},
    # an arched tree (the meadow's horizon): a short trunk, a broad flat umbrella
    "arch": {"depth": 3, "r0": 0.7, "sides": 10, "taper": 0.5, "gnarl": 0.3, "kid_r": 0.6,
             "up": {3: 0.06, 2: -0.02, 1: 0.0, 0: 0.0}, "wobble": {3: 0.05, 2: 0.08, 1: 0.12, 0: 0.12},
             "kids": {3: (4, 7), 2: (2, 4), 1: (2, 4)}, "at": {3: (0.6, 0.95), 2: (0.4, 0.9), 1: (0.3, 0.9)},
             "spread": {3: (60, 80), 2: (20, 40), 1: (15, 35)},
             "ratio": {3: (0.7, 0.9), 2: (0.5, 0.7), 1: (0.5, 0.7)}, "clump": 2.2, "leaf_level": 2},
    # a ledge shrub / understorey bush
    "shrub": {"depth": 2, "r0": 0.12, "sides": 5, "taper": 0.5, "gnarl": 0.0, "kid_r": 0.6,
              "up": {2: 0.12, 1: 0.05, 0: 0.0}, "wobble": {2: 0.15, 1: 0.2, 0: 0.2},
              "kids": {2: (3, 6), 1: (2, 3)}, "at": {2: (0.2, 0.8), 1: (0.3, 0.9)},
              "spread": {2: (30, 70), 1: (20, 50)}, "ratio": {2: (0.5, 0.8), 1: (0.5, 0.7)},
              "clump": 0.9, "leaf_level": 1},
}


def tree(style, seed, height, leaf_level=None, sides=None):
    S = dict(STYLES[style])
    if leaf_level is not None:
        S["leaf_level"] = leaf_level
    if sides is not None:
        S["sides"] = sides
    t = Tree(seed, S)
    r = t.r
    if style == "hero":
        d = np.array([r.uniform(-0.45, 0.45), 1.0, r.uniform(-0.45, 0.45)])
        trunk = height * 0.3
    elif style == "arch":
        d = np.array([r.normal(0, 0.1), 1.0, r.normal(0, 0.1)])
        trunk = height * 0.5
    else:
        d = np.array([r.normal(0, 0.06), 1.0, r.normal(0, 0.06)])
        trunk = height * 0.55
    t.branch(np.array([0.0, -0.4, 0.0]), d, trunk, S["r0"] * height / 18.0, S["depth"], 0.0)
    t.foliage()
    return t


# ---- species meshes ---------------------------------------------------------------------------------------------------


def grove(seed, size=26.0, count=(5, 9), height=(13.0, 22.0), leaf_level=2, sides=None):
    """Several canopy trees merged into one mesh pair: a patch of forest, so the floor needs fewer instances and each
    instance already has variety inside it."""
    r = np.random.default_rng(seed)
    wood, leaf = Mesh(), Mesh()
    n = int(r.integers(*count))
    placed = []
    for k in range(n):
        for _ in range(20):
            x, z = r.uniform(-size / 2, size / 2, 2)
            if all(math.hypot(x - a, z - b) > 4.5 for a, b in placed):
                break
        placed.append((x, z))
        t = tree("canopy", seed * 100 + k, r.uniform(*height), leaf_level=leaf_level, sides=sides)
        for src, dst in ((t.wood, wood), (t.leaf, leaf)):
            P, F, UV = src.arrays()
            dst.add(P + np.array([x, 0, z]), F, UV)
    return wood, leaf


def boulder(seed, size):
    """A bank boulder: a squashed, faceted, cracked lump."""
    r = np.random.default_rng(seed)
    V, F = icosphere(3)
    P = V.copy()
    n = fbm3(V * 1.2 + seed, seed, 3)
    crag = np.abs(noise3(V * 3.5 + seed * 0.3, seed + 3) - 0.5)
    P = P * (1.0 + 0.45 * (n - 0.5) - 0.35 * crag)[:, None]
    P *= np.array([size * r.uniform(0.9, 1.4), size * r.uniform(0.45, 0.7), size * r.uniform(0.8, 1.2)])
    P[:, 1] = np.maximum(P[:, 1], -0.25 * size)
    m = Mesh()
    m.add(P, F, np.stack([np.zeros(len(P)), np.clip(P[:, 1] / size + 0.5, 0, 1)], -1))
    return m


def outward_from_axis(P):
    d = P.copy()
    d[:, 1] = 0.25 * np.sign(d[:, 1])
    return d


# ---- hero fungi ------------------------------------------------------------------------------------------------------


def fungus(seed, height, cap_r, style="parasol"):
    """A hero fungus as three parts: (stalk, cap, threads). `parasol`: a broad, thin, frilled cap with gill ribs
    under it (the macro panel's caps). `veil`: an umbrella whose edge trails hanging luminous threads (the riverbank
    panel's tall tree-fungus). The cap's v runs from its centre (0) to its rim (1), so a material can make the core
    brighter than the body (spec §10: core, body, halo)."""
    r = np.random.default_rng(seed)
    stalk, cap, threads = Mesh(), Mesh(), Mesh()
    threads.emit = []
    # stalk: a soft S-curve
    n = 14
    lean = r.normal(0, 0.12, 2)
    ts = np.linspace(0, 1, n)
    pts = np.stack([lean[0] * height * ts ** 2 + 0.03 * height * np.sin(ts * 3 + seed), height * ts,
                    lean[1] * height * ts ** 2 + 0.03 * height * np.cos(ts * 2.5 + seed)], -1)
    base_r = height * (0.035 if style == "veil" else 0.05)
    tube(stalk, pts, np.linspace(base_r * 1.5, base_r * 0.7, n), 10, 0.0, 1.0)
    top = pts[-1]
    # cap: a radial shell
    rings, segs = 14, 72
    th = np.linspace(0, 2 * math.pi, segs, endpoint=False)
    rr = np.linspace(0.0, 1.0, rings)
    frill = 1.0 + 0.07 * np.sin(7 * th + r.uniform(0, 6)) + 0.05 * np.sin(13 * th + r.uniform(0, 6)) \
        + 0.04 * np.sin(23 * th + r.uniform(0, 6))
    dome = 0.32 if style == "parasol" else 0.55
    P = np.zeros((rings, segs, 3))
    for i, t in enumerate(rr):
        R = cap_r * t * frill
        y = cap_r * dome * (1.0 - t ** 2) ** 0.8 - (cap_r * 0.12 * t ** 4 if style == "veil" else cap_r * 0.05 * t ** 3)
        wave = 0.04 * cap_r * t ** 2 * np.sin(9 * th + 2.0 * t)
        P[i, :, 0] = top[0] + R * np.cos(th)
        P[i, :, 2] = top[2] + R * np.sin(th)
        P[i, :, 1] = top[1] + y + wave
    UV = np.stack([np.tile(th / (2 * math.pi), rings), np.repeat(rr, segs)], -1)
    cap.add(P.reshape(-1, 3), grid_faces(rings, segs, True), UV)
    if style == "parasol":
        # gill ribs: thin radial fins under the cap
        for k in range(36):
            a = k * 2 * math.pi / 36
            d = np.array([math.cos(a), 0, math.sin(a)])
            inner = top + d * cap_r * 0.15 + np.array([0, cap_r * dome * 0.75, 0])
            outer = top + d * cap_r * 0.95 * (1.0 + 0.07 * math.sin(7 * a)) + np.array([0, -cap_r * 0.02, 0])
            lo = np.array([0, -cap_r * 0.12, 0])
            V = np.array([inner, outer, inner + lo * 0.6, outer + lo * 0.2])
            cap.add(V, np.array([[0, 1, 2], [1, 3, 2]]), np.array([[a / 6.283, 0.2], [a / 6.283, 1], [a / 6.283, 0.2],
                                                                   [a / 6.283, 1]]))
    else:
        # hanging threads from the rim, each a thin ribbon with a bead at its end
        for k in range(56):
            a = r.uniform(0, 2 * math.pi)
            f = 1.0 + 0.07 * math.sin(7 * a)
            start = top + np.array([math.cos(a) * cap_r * 0.98 * f, -cap_r * 0.1, math.sin(a) * cap_r * 0.98 * f])
            length = height * (0.12 + 0.6 * r.random() ** 1.8)
            m = 10
            s = np.linspace(0, 1, m)
            inward = -np.array([math.cos(a), 0, math.sin(a)]) * 0.12 * length
            line = start[None, :] + np.stack([0.06 * length * np.sin(s * 3 + k) * np.ones(m), -length * s,
                                              0.06 * length * np.cos(s * 2 + k)], -1) + np.outer(s ** 2, inward)
            w = 0.0025 * height * r.uniform(0.6, 1.4)
            side = np.array([-math.sin(a), 0, math.cos(a)]) * w
            V = np.concatenate([line - side, line + side])
            F = []
            for i in range(m - 1):
                F += [[i, i + 1, m + i], [i + 1, m + i + 1, m + i]]
            threads.add(V, np.array(F), np.stack([np.full(2 * m, (k * 0.618) % 1), np.tile(s, 2)], -1))
            clump(threads, line[-1], 0.009 * height, seed * 7 + k, level=1, squash=1.0, lump=0.1, u=(k * 0.618) % 1,
                  top=None)
            threads.emit.append([*line[-1], 1.0, (k * 0.618) % 1, 0.012 * height])
            threads.emit.append([*line[m // 2], 0.5, (k * 0.31) % 1, 0.004 * height])
    return stalk, cap, threads

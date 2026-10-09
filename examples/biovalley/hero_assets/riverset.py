"""The riverbank waterfall set and shoreline pieces (07-cinematic-finishing-pass.md sections 3.4-3.5).

THE FALL SET shares one frame. The origin is the plunge point at water level, +X runs toward the riverbank camera
(the direction the water falls away from the cliff), +Y runs along the cliff face, and Z is up. In glTF (Y up),
+X stays +X and +Y becomes -Z. It is built for the riverbank's bank-cliff fall: a 32 m drop, ~26 m wide.
  env_fall_rock   the lip, and below it the ledges and the plunge pool's rim.
                  - The lip is a row of separate rock blocks with gaps, so the water leaves it as broken streams,
                    not a sheet.
                  - Ledges stand out from the cliff on either side of the fall at mid-height.
                  - The rim is a ring of half-sunk, wet, mossy rocks, open on the outflow side.
  env_fall_water  the falling water: three main streams through the gaps plus thin threads.
                  - Each stream rolls over the lip, then drops on a ballistic curve, widening and thinning as it falls.
                  - A front and a back layer give it depth.
                  - Frayed edges and growing gaps toward the foot (alpha MASK).
                  - UV v runs DOWN the flow (0 at the lip, 1 at the pool), u across: the lead's shader can scroll v.
  env_fall_foam   a disc of foam and churned water at the impact, v from the centre outward.
  env_fall_pool   the pool's rocky bowl below the water line (dark).
Placement: put the set's origin at the fall's foot; fall3 lands at about (-147, 0.8, 862) in world space, with +X
already pointing at the riverbank camera.

SHORELINE PIECES (origin at the waterline, Z up; water level z = 0):
  env_shelf_a, env_shelf_b                 shallow stepped rock shelves, their tops a hand's width above the water.
  env_waterrock_a, env_waterrock_b, env_waterrock_c
                                           dark mossy rocks sunk at the waterline: wet, glossy and darker below about
                                           0.4 m; dry, matte and mossy on top.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (CYAN, Asset, col, fbm, grid_sample, lerp, smoothstep, worley)
from rocks import Rock, noise3
from lib import worley3

TAU = 2 * math.pi
H = 32.0  # the drop
LIP_X = -1.0  # where the water leaves the lip
VIOLET_WHITE = np.array([0.62, 0.62, 1.0])


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1.0]])


class SetRock(Rock):
    """A Rock placed in a set frame. Its wet band and spray wetness are painted from set-space height and distance to
    the falls, so neighbouring rocks agree on where the water is."""

    def __init__(self, name, seed, R, scale, chunk, at, yaw=0.0, lod=0, spray=None, sink=0.0):
        super().__init__(name, seed, R, scale, chunk, lod)
        self.M = rot_z(yaw)
        self.t = np.asarray(at, dtype=float)
        self.spray = spray  # (x, y, radius) in the set frame: everything near the impact is soaked
        self.sink = sink

    def world(self, S, T):
        x, y, z = self.position(S, T)
        P = np.stack([x, y, z - self.sink * self.R], -1) @ self.M.T + self.t
        return P[..., 0], P[..., 1], P[..., 2]

    def grid(self):
        nth, nph = (96, 48) if self.lod0 else (28, 14)
        s = np.linspace(0, 1, nth, endpoint=False)
        t = np.linspace(0, 1, nph)
        S, T = np.meshgrid(s, t)
        X, Y, Z = self.world(S, T)
        return np.stack([X, Y, Z], -1)[:, ::-1]

    def paint(self, S, T):
        sd = self.seed + 50
        S = 1 - S
        dx, dy, dz = self.dirs(S, T)
        x, y, z = self.world(S, T)
        mott = noise3(x, y, z, 1.2, sd, 4)
        grain = noise3(x, y, z, 9.0, sd + 1, 2)
        strata = 0.5 + 0.5 * np.sin(z * 4.0 + 5 * noise3(x, y, z, 0.6, sd + 2, 2))
        crack = self.cracks(dx, dy, dz)
        # wet below 0.4 m above the water, and soaked inside the spray zone
        wet = 1 - smoothstep(0.1, 0.5, z)
        if self.spray is not None:
            sx, sy, sr = self.spray
            wet = np.maximum(wet, 1 - smoothstep(0.5 * sr, sr, np.hypot(x - sx, y - sy)))
        moss = smoothstep(0.5, 0.8, dz + 0.35 * (noise3(x, y, z, 1.0, sd + 3, 3) - 0.5) * 2)
        moss = np.maximum(moss, crack * smoothstep(-0.4, 0.2, dz)) * (1 - 0.8 * wet)
        mossn = noise3(x, y, z, 12.0, sd + 4, 2)
        rock = lerp(np.array([0.028, 0.03, 0.036]), np.array([0.075, 0.075, 0.085]), 0.5 * mott + 0.3 * strata)
        rock = rock * (0.8 + 0.4 * grain)[..., None] * (1 - 0.6 * wet)[..., None] * (1 - 0.6 * crack)[..., None]
        mosscol = lerp(np.array([0.012, 0.04, 0.02]), np.array([0.04, 0.10, 0.05]), mossn)
        base = rock * (1 - moss)[..., None] + mosscol * moss[..., None]
        # the faintest spore speckle in the moss
        G1, gh = worley3(x * 18, y * 18, z * 18, sd + 7, jitter=0.95)
        mdots = (1 - smoothstep(0.08, 0.18, G1)) * (gh < 0.2) * moss
        e = col(lerp(np.array([0.06, 1.0, 0.3]), CYAN, np.clip(gh * 4, 0, 1)), mdots * 0.4)
        height = 0.4 * mott + 0.3 * grain + 0.3 * strata + 0.4 * moss * mossn - 0.6 * crack
        rough = np.clip(0.88 - 0.7 * wet + 0.05 * moss, 0.12, 1.0)
        return dict(base=base, emis=e, height=height, rough=rough, ao=0.5 + 0.5 * (1 - crack))


# ---- the fall set -------------------------------------------------------------------------------------------------

STREAMS = [  # (y centre, width, layered)
    (-6.0, 6.5, True), (1.2, 4.2, True), (6.8, 2.6, True)]
THREADS = [-10.4, -2.6, 4.3, 9.6, -12.8]


def fall_rocks(lod):
    """The lip blocks (with gaps for the streams), side ledges and the pool rim, all in the set frame."""
    rocks = []
    sp = (2.0, 0.0, 11.0)
    # the pool rim: a broken ring, open toward +x (the outflow)
    rng = np.random.default_rng(360)
    for k in range(9):
        a = math.radians(95 + 170 * k / 8) + rng.uniform(-0.08, 0.08)
        r = rng.uniform(9.0, 11.5)
        R = rng.uniform(1.3, 3.2)
        rocks.append(SetRock(f"rim{k}", 360 + k, R, (1.0, rng.uniform(0.7, 1.0), rng.uniform(0.55, 0.85)), 0.12,
                             (1.5 + r * math.cos(a), r * math.sin(a), 0.0), yaw=rng.uniform(0, TAU), lod=lod,
                             spray=sp, sink=0.35))
    for r in rocks:
        r.spray = sp
    return rocks


def notch(y):
    """How deep the lip is cut where each stream pours through it."""
    d = np.zeros_like(np.asarray(y, dtype=float))
    for c, w, _ in STREAMS:
        d = d + (0.7 + 0.12 * w) * np.exp(-((y - c) / (w * 0.55)) ** 4)
    return d


def cliff_grids(lod):
    """The cliff face (bottom -> lip) and the plateau behind the lip (lip -> back), as two grids sharing the lip."""
    nu = 220 if lod == 0 else 60
    nvf = 110 if lod == 0 else 30
    nvt = 40 if lod == 0 else 10
    y = np.linspace(-24, 24, nu)
    zn = np.linspace(0, 1, nvf)
    ZN, Y = np.meshgrid(zn, y, indexing="ij")
    # an irregular top edge that drops to lower shoulders either side: a gorge head, not a box
    near = np.zeros_like(Y)
    for c, w, _ in STREAMS:
        near = np.maximum(near, np.exp(-((Y - c) / (w + 2.0)) ** 2))
    top_h = H + (1 - near) * (3.0 * (fbm(Y * 0.09, 0.5, 3, 505) - 0.5) - 9.0 * smoothstep(11.0, 24.0, np.abs(Y)) ** 1.4)
    Z = -2.5 + ZN * (top_h + 2.5)
    zz = Z / H
    # the alcove behind the fall: deepest mid-height, closing toward the lip so the lip overhangs
    alcove = 4.0 * np.exp(-(Y / 12.0) ** 2) * smoothstep(0.0, 0.25, zz) * (1 - smoothstep(0.8, 0.98, zz))
    # flat-topped ledges either side of the fall
    ledge = np.zeros_like(Y)
    for y0, z0, w, d in ((-16.0, 0.52 * H, 3.5, 3.2), (14.0, 0.45 * H, 3.0, 2.6), (-11.5, 0.22 * H, 2.4, 2.2),
                         (19.0, 0.72 * H, 2.6, 2.0)):
        ledge = ledge + d * np.exp(-((Y - y0) / w) ** 4) * smoothstep(z0 - 3.0, z0 - 0.4, Z) * (1 - smoothstep(z0, z0 + 0.25, Z))
    crag = (1.1 * (fbm(Y * 0.12, Z * 0.12, 4, 500) - 0.5) + 0.5 * (fbm(Y * 0.5, Z * 0.35, 2, 501) - 0.5)
            + 0.45 * np.floor(fbm(Y * 0.08 + 3, Z * 0.2, 2, 502) * 5) / 5)
    fissure = smoothstep(0.9, 0.985, 1 - np.abs(2 * fbm(Y * 0.22, Z * 0.025, 3, 503) - 1))
    wing = 11.0 * smoothstep(9.0, 24.0, np.abs(Y)) ** 1.6  # the face curves back: a gorge head, no cut edges
    X = LIP_X - 0.6 - alcove + ledge + crag - 0.9 * fissure - wing
    Zf = Z - notch(Y) * smoothstep(0.95, 1.0, ZN)
    face = np.stack([X, Y, Zf], -1)
    # the plateau: back from the lip edge, channels continuing back where the streams come from
    xs = np.linspace(0, 1, nvt)
    XS, Y2 = np.meshgrid(xs, y, indexing="ij")
    lip_x = face[-1, :, 0][None, :]
    Xt = lip_x - 22.0 * XS ** 1.2 - 4.0 * XS * smoothstep(9.0, 24.0, np.abs(Y2))
    top_edge = face[-1, :, 2][None, :] + notch(Y2[:1])
    Zt = (top_edge - notch(Y2) * np.exp(-XS * 2.5) + 1.8 * XS + 0.9 * (fbm(Y2 * 0.15, XS * 4, 3, 504) - 0.5) * smoothstep(0, 0.1, XS))
    top = np.stack([Xt, Y2, Zt], -1)
    top[0] = face[-1]
    return face, top


def make_cliff_painter(P, N, seed):
    def paint(S, T):
        pos = grid_sample(P, S, T)
        nrm = grid_sample(N, S, T)
        x, y, z = pos[..., 0], pos[..., 1], pos[..., 2]
        nz = nrm[..., 2] / np.maximum(np.linalg.norm(nrm, axis=-1), 1e-6)
        mott = noise3(x, y, z, 0.35, seed, 4)
        grain = noise3(x, y, z, 2.5, seed + 1, 2)
        strata = 0.5 + 0.5 * np.sin(z * 1.6 + 4 * noise3(x, y, z, 0.2, seed + 2, 2))
        # wet: behind the falling water, streaks below each notch, the spray zone, and the waterline
        behind = np.zeros_like(x)
        for c, w, _ in STREAMS:
            behind = np.maximum(behind, np.exp(-((y - c) / (w * 0.8)) ** 2))
        spray = 1 - smoothstep(6.0, 14.0, np.hypot(x - 1.5, y))
        wet = np.clip(np.maximum.reduce([behind * smoothstep(-6.0, -3.0, x) * (1 - smoothstep(H - 0.3, H + 0.5, z)),
                                         spray * (1 - smoothstep(4.0, 10.0, z)), 1 - smoothstep(0.1, 0.6, z)]), 0, 1)
        moss = smoothstep(0.55, 0.85, nz + 0.3 * (noise3(x, y, z, 0.4, seed + 3, 3) - 0.5) * 2) * (1 - 0.85 * wet)
        mossn = noise3(x, y, z, 3.0, seed + 4, 2)
        rock = lerp(np.array([0.028, 0.03, 0.036]), np.array([0.08, 0.08, 0.09]), 0.5 * mott + 0.3 * strata)
        rock = rock * (0.8 + 0.4 * grain)[..., None] * (1 - 0.65 * wet)[..., None]
        base = rock * (1 - moss)[..., None] + lerp(np.array([0.012, 0.04, 0.02]), np.array([0.04, 0.1, 0.05]), mossn) * moss[..., None]
        G1, gh = worley3(x * 4, y * 4, z * 4, seed + 7, jitter=0.95)
        mdots = (1 - smoothstep(0.08, 0.18, G1)) * (gh < 0.2) * moss
        e = col(lerp(np.array([0.06, 1.0, 0.3]), CYAN, np.clip(gh * 4, 0, 1)), mdots * 0.35)
        height = 0.4 * mott + 0.35 * grain + 0.3 * strata + 0.3 * moss * mossn
        rough = np.clip(0.88 - 0.72 * wet + 0.05 * moss, 0.1, 1.0)
        return dict(base=base, emis=e, height=height, rough=rough, ao=0.55 + 0.45 * (1 - wet * 0.3))
    return paint


def build_fall_rock(lod=0):
    from lib import grid_normals
    A = Asset("env_fall_rock")
    res = 1.0 if lod == 0 else 0.5
    cl = A.slot("cliff", res=(int(4096 * res), int(2048 * res)), mode="OPAQUE", double_sided=False,
                emissive_strength=4.0)
    face, top = cliff_grids(lod)
    Nf, Nt = grid_normals(face, False), grid_normals(top, False)
    cl.add_grid(face, cl.region(0.0, 0.62, make_cliff_painter(face, Nf, 510), bump=6.0))
    cl.add_grid(top, cl.region(0.62, 1.0, make_cliff_painter(top, Nt, 520), bump=6.0))
    rocks = fall_rocks(lod)
    sl = A.slot("rock", res=(int(4096 * res), int(512 * res)), mode="OPAQUE", double_sided=False,
                emissive_strength=4.0)
    n = len(rocks)
    for k, r in enumerate(rocks):
        sl.add_grid(r.grid(), sl.region(k / n, (k + 1) / n, r.paint, bump=5.0), wrap_u=True)
    return A


def stream_sheet(c, w, rng, nv, nu, layer=0.0):
    """One stream: rolls over the lip and falls on a ballistic curve, widening as it goes. (nv, nu, 3)."""
    v = np.linspace(0, 1, nv)
    # the first 12% rolls over the rounded lip, the rest is free fall
    roll = smoothstep(0.0, 0.12, v)
    t = np.clip((v - 0.12) / 0.88, 0, 1)
    v0 = 1.4 + rng.uniform(-0.2, 0.3)
    tf = math.sqrt(2 * (H - 1.0) / 9.81)
    floor = H - float(notch(np.array(c))) + 0.12  # the water surface in its channel
    x = LIP_X - 0.4 - 5.0 * (1 - roll) + v0 * t * tf + layer * 0.35
    z = floor + 0.25 * (1 - roll) - (floor + 0.1) * t ** 2  # along the channel, over the lip, then ballistic
    z = np.maximum(z, -0.2)
    width = w * (1 + 0.9 * t ** 1.3) * (0.85 + 0.1 * np.sin(5 * t + c))
    sway = 0.4 * np.sin(3.1 * t + c) * t
    u = np.linspace(-0.5, 0.5, nu)
    V, U = np.meshgrid(v, u, indexing="ij")
    X = np.broadcast_to(x[:, None], V.shape) + 0.25 * np.sin(U * 9 + V * 6 + c) * t[:, None]
    Y = c + sway[:, None] + U * width[:, None]
    Z = np.broadcast_to(z[:, None], V.shape) + 0.15 * np.sin(U * 13 + c) * (1 - t[:, None])
    return np.stack([X, Y, Z], -1)


def build_fall_water(lod=0):
    A = Asset("env_fall_water")
    rng = np.random.default_rng(400)
    res = 1.0 if lod == 0 else 0.5
    sl = A.slot("water", res=(int(1024 * res), int(2048 * res)), mode="MASK", double_sided=True,
                emissive_strength=2.0)
    r_main = sl.region(0.0, 0.7, paint_water(0), bump=2.0)
    r_thread = sl.region(0.7, 1.0, paint_water(1), bump=1.0)
    nv = 60 if lod == 0 else 20
    for k, (c, w, layered) in enumerate(STREAMS):
        nu = 14 if lod == 0 else 5
        sl.add_grid(stream_sheet(c, w, rng, nv, nu), r_main, s_range=(k / 3, (k + 1) / 3))
        if layered and lod == 0:  # a narrower, offset back layer gives the water depth
            sl.add_grid(stream_sheet(c + 0.3, w * 0.75, rng, nv, nu - 4, layer=-1.0), r_main,
                        s_range=(k / 3, (k + 1) / 3))
    for k, c in enumerate(THREADS[: (5 if lod == 0 else 2)]):
        sl.add_grid(stream_sheet(c, rng.uniform(0.35, 0.8), rng, nv, 3), r_thread, s_range=(k / 5, (k + 1) / 5))
    return A


def paint_water(kind):
    def paint(S, T):
        sd = 410 + kind
        # streaks run down the flow; they break up as the water falls
        st = fbm(S * (90 if kind == 0 else 20), T * 1.2, 3, sd)
        streak = smoothstep(0.35, 0.7, st)
        brk = fbm(S * 24, T * 70, 3, sd + 1)  # round holes, about half a metre, not slits
        edge = np.minimum(S * 3 % 1.0, 1 - (S * 3 % 1.0)) * 2 if kind == 0 else np.minimum(S * 5 % 1.0, 1 - (S * 5 % 1.0)) * 2
        fray = smoothstep(0.0, 0.25 + 0.25 * fbm(S * 40, T * 6, 2, sd + 2), edge)
        # fine vertical gaps open between the streaks as the water falls; round holes only near the foot
        gap = smoothstep(0.30 - 0.18 * T, 0.22 - 0.18 * T, st)
        hole = smoothstep(0.86, 0.9, brk) * smoothstep(0.7, 0.95, T)
        alpha = ((fray * (1 - gap) * (1 - hole)) > 0.4).astype(float)
        alpha = alpha * (T > 0.004)
        base = lerp(np.array([0.05, 0.06, 0.12]), np.array([0.2, 0.22, 0.38]), streak)
        e = col(VIOLET_WHITE, (0.03 + 0.18 * streak) * (0.6 + 0.4 * T)) + col(CYAN, 0.02 * T)
        return dict(base=base, emis=e, alpha=alpha, height=streak, rough=0.08, ao=1.0)
    return paint


def build_fall_foam(lod=0):
    A = Asset("env_fall_foam")
    sl = A.slot("foam", res=(1024, 512) if lod == 0 else (512, 256), mode="MASK", double_sided=True,
                emissive_strength=2.0)
    reg = sl.region(0, 1, paint_foam, bump=2.0)
    nth, nr = (96, 16) if lod == 0 else (32, 5)
    th = -np.linspace(0, TAU, nth, endpoint=False)
    rr = np.linspace(0.3, 1.0, nr)
    RR, TH = np.meshgrid(rr, th, indexing="ij")
    Rx = 8.0 * RR * (1 + 0.15 * np.sin(3 * TH))
    Ry = 13.0 * RR
    X = 1.5 + Rx * np.cos(TH)
    Y = Ry * np.sin(TH)
    Z = 0.04 + 0.12 * (1 - RR) ** 2
    A_ = np.stack([X, Y, np.broadcast_to(Z, X.shape)], -1)
    sl.add_grid(A_, reg, wrap_u=True)
    return A


def paint_foam(S, T):
    F1, F2, _, h = worley(S * 60, T * 10, 430, px=60)
    lace = 1 - smoothstep(0.05, 0.16, F2 - F1)
    density = 1 - smoothstep(0.2, 0.95, T)
    blob = smoothstep(0.45, 0.7, fbm(S * 30, T * 6, 3, 431, px=30))
    alpha = (((lace * 0.8 + blob) * density) > 0.4).astype(float)
    base = lerp(np.array([0.2, 0.24, 0.36]), np.array([0.55, 0.6, 0.8]), density)
    e = col(VIOLET_WHITE, 0.10 * density * (0.4 + 0.6 * blob))
    return dict(base=base, emis=e, alpha=alpha, height=lace + blob, rough=0.5, ao=1.0)


def build_fall_pool(lod=0):
    A = Asset("env_fall_pool")
    sl = A.slot("bed", res=(1024, 512) if lod == 0 else (512, 256), mode="OPAQUE", double_sided=False,
                emissive_strength=1.0)
    reg = sl.region(0, 1, paint_bed, bump=4.0)
    nth, nr = (96, 24) if lod == 0 else (24, 8)
    th = -np.linspace(0, TAU, nth, endpoint=False)
    rr = np.linspace(0.0, 1.0, nr)
    RR, TH = np.meshgrid(rr, th, indexing="ij")
    X = 1.5 + 11.5 * RR * np.cos(TH)
    Y = 14.0 * RR * np.sin(TH)
    depth = 3.2 * (1 - RR ** 2) ** 0.8 + 0.4 * (fbm(X * 0.3, Y * 0.3, 3, 440) - 0.5)
    Z = -depth + 0.15 * smoothstep(0.85, 1.0, RR)
    sl.add_grid(np.stack([X, Y, Z], -1), reg, wrap_u=True)
    return A


def paint_bed(S, T):
    F1, _, _, h = worley(S * 40, T * 8, 450, px=40)
    pebble = 1 - smoothstep(0.0, 0.6, F1)
    base = lerp(np.array([0.01, 0.015, 0.025]), np.array([0.03, 0.04, 0.06]), pebble * h)
    return dict(base=base, emis=np.zeros(S.shape + (3,)), height=pebble, rough=0.35, ao=0.4 + 0.6 * pebble)


# ---- shoreline pieces ---------------------------------------------------------------------------------------------


def build_shelf(name, seed, length, width, lod=0):
    """A shallow, stepped shelf: two or three slabs stepping down into the water, tops a hand above it."""
    A = Asset(name)
    rng = np.random.default_rng(seed)
    res = 1.0 if lod == 0 else 0.5
    sl = A.slot("rock", res=(int(2048 * res), int(512 * res)), mode="OPAQUE", double_sided=False,
                emissive_strength=4.0)
    steps = [(0.0, 0.0, 0.16, 1.0), (length * 0.32, width * 0.15, 0.02, 0.7), (length * 0.6, -width * 0.1, -0.12, 0.5)]
    for k, (x, y, top, s) in enumerate(steps):
        r = SetRock(f"{name}{k}", seed + k, 1.0, (length * 0.32 * s + 0.4, width * 0.5 * s, 0.35), 0.06,
                    (x, y, top - 0.12), yaw=rng.uniform(-0.3, 0.3), lod=lod)
        sl.add_grid(r.grid(), sl.region(k / 3, (k + 1) / 3, r.paint, bump=4.0), wrap_u=True)
    return A


def build_waterrock(name, seed, R, scale, lod=0):
    A = Asset(name)
    res = 1.0 if lod == 0 else 0.5
    sl = A.slot("rock", res=(int(2048 * res), int(1024 * res)), mode="OPAQUE", double_sided=False,
                emissive_strength=4.0)
    r = SetRock(name, seed, R, scale, 0.14, (0, 0, 0), lod=lod, sink=0.0)
    sl.add_grid(r.grid(), sl.region(0, 1, r.paint, bump=5.0), wrap_u=True)
    return A


def make_rock(lod=0):
    return build_fall_rock(lod)


def make_water(lod=0):
    return build_fall_water(lod)


def make_foam(lod=0):
    return build_fall_foam(lod)


def make_pool(lod=0):
    return build_fall_pool(lod)


def make_shelf_a(lod=0):
    return build_shelf("env_shelf_a", 470, 5.5, 3.2, lod)


def make_shelf_b(lod=0):
    return build_shelf("env_shelf_b", 480, 3.6, 2.4, lod)


def make_waterrock_a(lod=0):
    return build_waterrock("env_waterrock_a", 490, 1.1, (1.0, 0.75, 0.6), lod)


def make_waterrock_b(lod=0):
    return build_waterrock("env_waterrock_b", 491, 0.7, (0.9, 0.85, 0.75), lod)


def make_waterrock_c(lod=0):
    return build_waterrock("env_waterrock_c", 492, 0.45, (1.0, 0.7, 0.55), lod)

"""Macro substrate (07-cinematic-finishing-pass.md section 8): the dark ground the organisms root into.

env_mosscarpet_a, env_mosscarpet_b: broad, low, irregular moss carpets (2.0 m and 1.4 m across, 3-6 cm high) of
  cushion lobes, in glowmat tissue but almost unlit: the threads and spore dots are at an eighth of the glowmat's
  level, so they sit under the organisms rather than competing with them.
env_roots: a tangle of surface roots, half sunk into the ground, running out from one side, forking and tapering,
  with moss along their tops. Dark matte; a few faint lichen specks.
env_litter: a substrate patch about 1.2 m across.
  - A dark humus disc with small embedded stones.
  - Fallen, curled leaf cards and twig fragments.
  - The edge feathers into the ground.

Units metres, Z up in Blender (Y up exported), origin at the base centre.
"""
from __future__ import annotations

import math

import numpy as np

from lib import (CYAN, EMERALD, TISSUE_DARK, Asset, add_branches, bezier, col, cup_leaf, fbm, grow_branches,
                 lerp, smoothstep, tube_grid, tube_up, worley)
from mat import Mat
from rocks import Rock

TAU = 2 * math.pi


def make_mosscarpet_a(lod=0):
    return Mat(171, lod, name="env_mosscarpet_a", R0=1.0, dot_cell=0.012, cups=False, lobe_amp=0.45, glow=0.12,
               extras=False).build()


def make_mosscarpet_b(lod=0):
    return Mat(181, lod, name="env_mosscarpet_b", R0=0.7, dot_cell=0.01, cups=False, lobe_amp=0.6, glow=0.12,
               extras=False).build()


def paint_root(S, T):
    top = 0.5 + 0.5 * np.cos(TAU * S)  # tube_up: u = 0 faces up
    mott = fbm(S * 8, T * 60, 3, 190, px=8)
    bark = fbm(S * 30, T * 200, 2, 191, px=30)
    moss = smoothstep(0.55, 0.8, top + 0.3 * (fbm(S * 6, T * 30, 3, 192, px=6) - 0.5) * 2)
    speck = smoothstep(0.9, 0.96, fbm(S * 60, T * 300, 2, 193, px=60)) * moss
    base = lerp(TISSUE_DARK, np.array([0.035, 0.03, 0.03]), 0.4 * mott + 0.3 * bark)
    base = base * (1 - moss)[..., None] + col([0.012, 0.035, 0.02], moss * (0.6 + 0.6 * mott))
    e = col(lerp(EMERALD, CYAN, mott), speck * 0.12)
    return dict(base=base, emis=e, height=0.5 * bark + 0.4 * mott + 0.3 * moss, rough=0.85, ao=0.4 + 0.6 * top)


def make_roots(lod=0):
    A = Asset("env_roots")
    rng = np.random.default_rng(195)
    sl = A.slot("root", res=(1024, 1024) if lod == 0 else (512, 512), mode="OPAQUE", double_sided=False,
                emissive_strength=4.0)
    reg = sl.region(0, 1, paint_root, bump=4.0)
    br = []
    for k in range(5):
        a = -0.9 + 1.8 * k / 4 + rng.uniform(-0.15, 0.15)
        d = np.array([math.cos(a), math.sin(a), -0.04])
        br += grow_branches(rng, [-0.3, 0.0, 0.02], d, rng.uniform(0.35, 0.6), 0.035, max_gen=3, children=(1, 2),
                            spread=(0.3, 0.7), shrink=0.7, rshrink=0.65, curl=0.25, up=0.0,
                            segs=14 if lod == 0 else 5, crozier=0.0, min_r=0.003, child_t=(0.3, 1.0), min_dz=-0.08,
                            sink=0.7)
    for b in br:  # roots hug the ground: flatten their height and half-sink them
        b.pts[:, 2] = 0.3 * b.pts[:, 2] + 0.15 * b.radii * np.sin(np.linspace(0, 6, len(b.pts)) + b.d0 * 9)
    add_branches(sl, reg, br, lambda b: max([12, 9, 7, 5][min(b.gen, 3)] - (0 if lod == 0 else 4), 4),
                 lambda th, t: 1 + 0.25 * np.cos(2 * th[None, :]) * np.ones_like(t)[:, None], up_frame=True)
    return A


def paint_humus(S, T):
    F1, F2, _, h = worley(S * 50, T * 8, 200, px=50)
    clod = 1 - smoothstep(0.0, 0.6, F1)
    mott = fbm(S * 20, T * 4, 3, 201, px=20)
    base = lerp(np.array([0.008, 0.009, 0.012]), np.array([0.03, 0.028, 0.03]), 0.5 * clod + 0.4 * mott)
    return dict(base=base, emis=np.zeros(S.shape + (3,)), height=clod + 0.5 * mott, rough=0.9, ao=0.5 + 0.5 * clod)


def paint_leaf_litter(S, T):
    x = (S - 0.5) * 2
    vein = 1 - smoothstep(0.02, 0.06, np.abs(x))
    mott = fbm(S * 6, T * 10, 3, 210)
    base = lerp(np.array([0.012, 0.016, 0.014]), np.array([0.04, 0.04, 0.035]), mott) + col([0.01, 0.01, 0.01], vein)
    holes = (fbm(S * 14, T * 20, 2, 211) > 0.68).astype(float)  # decayed
    return dict(base=base, emis=np.zeros(S.shape + (3,)), alpha=1 - holes, height=vein + 0.3 * mott, rough=0.75,
                ao=0.6 + 0.4 * mott)


def make_litter(lod=0):
    A = Asset("env_litter")
    rng = np.random.default_rng(220)
    lod0 = lod == 0
    ground = A.slot("humus", res=(1024, 512) if lod0 else (512, 256), mode="OPAQUE", double_sided=False,
                    emissive_strength=1.0)
    nth, nr = (96, 16) if lod0 else (32, 6)
    th = -np.linspace(0, TAU, nth, endpoint=False)
    rr = np.linspace(0, 1, nr)
    RR, TH = np.meshgrid(rr, th, indexing="ij")
    R = 0.6 * (1 + 0.2 * (fbm(TH / TAU * 5, 0.5, 2, 221, px=5) - 0.5) * 2)
    Z = 0.02 * (1 - RR ** 2) + 0.008 * (fbm(RR * 6 * np.cos(TH), RR * 6 * np.sin(TH), 3, 222) - 0.5) - 0.01 * RR ** 4
    ground.add_grid(np.stack([RR * R * np.cos(TH), RR * R * np.sin(TH), Z], -1), ground.region(0, 1, paint_humus, bump=4.0),
                    wrap_u=True)
    stones = A.slot("stone", res=(512, 256) if lod0 else (256, 128), mode="OPAQUE", double_sided=False,
                    emissive_strength=4.0)
    for k in range(9 if lod0 else 4):
        a, r = rng.uniform(0, TAU), 0.45 * math.sqrt(rng.uniform(0, 1))
        st = Rock(f"st{k}", 230 + k, rng.uniform(0.02, 0.06), (1.0, 0.8, 0.6), 0.1, lod=lod)
        nth2, nph2 = (24, 12) if lod0 else (10, 5)
        S2, T2 = np.meshgrid(np.linspace(0, 1, nth2, endpoint=False), np.linspace(0, 1, nph2))
        x, y, z = st.position(S2, T2)
        P = np.stack([x + r * math.cos(a), y + r * math.sin(a), z + 0.005], -1)[:, ::-1]
        stones.add_grid(P, stones.region(k / 9, (k + 1) / 9, st.paint, bump=3.0), wrap_u=True)
    leaves = A.slot("leaf", res=(256, 512), mode="MASK", emissive_strength=1.0)
    reg = leaves.region(0, 1, paint_leaf_litter, bump=2.0)
    for k in range(26 if lod0 else 8):
        a, r = rng.uniform(0, TAU), 0.55 * math.sqrt(rng.uniform(0, 1))
        p = np.array([r * math.cos(a), r * math.sin(a), 0.022 * (1 - (r / 0.6) ** 2) + 0.004])
        yaw = rng.uniform(0, TAU)
        d = np.array([math.cos(yaw), math.sin(yaw), 0.08])
        side = np.array([-math.sin(yaw), math.cos(yaw), 0.0])
        P = cup_leaf(p, d, side, rng.uniform(0.04, 0.09), rng.uniform(0.012, 0.025), cup=-0.4, curl=rng.uniform(-0.3, 0.4),
                     nv=6 if lod0 else 3, nu=3)
        leaves.add_grid(P, reg)
    twigs = A.slot("twig", res=(256, 256), mode="OPAQUE", double_sided=False, emissive_strength=1.0)
    rt = twigs.region(0, 1, paint_root, bump=2.0)
    for k in range(6 if lod0 else 2):
        a, r = rng.uniform(0, TAU), 0.4 * math.sqrt(rng.uniform(0, 1))
        p = np.array([r * math.cos(a), r * math.sin(a), 0.02])
        yaw = rng.uniform(0, TAU)
        d = np.array([math.cos(yaw), math.sin(yaw), 0.0]) * rng.uniform(0.1, 0.25)
        c = bezier(p, p + d * 0.3 + [0, 0, 0.004], p + d * 0.7 - [0, 0, 0.002], p + d, 6 if lod0 else 3)
        twigs.add_grid(tube_up(c, np.linspace(0.004, 0.002, len(c)), 6 if lod0 else 4), rt, wrap_u=True)
    return A

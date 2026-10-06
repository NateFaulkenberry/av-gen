#!/usr/bin/env python3
"""DIGITAL MOSH: the source of truth for the scene and its projects.

    python3 examples/digital-mosh/build.py

Writes digital-mosh.scene.json (the world) and the projects beside it. Edit this file, not the JSON: the
palette, the tree's skeleton (shared by its SDF, its voxel shell and its fragments), the stage table and the
audio mapping all live here so they stay consistent.

Design: docs/prototypes/digital-mosh/02-design.md. Research: 01-research.md.
"""
from __future__ import annotations

import json
import math
import random
from pathlib import Path

HERE = Path(__file__).resolve().parent

# ============================================================================================ layout
TREE_AT = [-6.0, 0.0, 0.0]
STONE_AT = [12.0, 1.15, -14.0]       # hovers: its underside sits ~0.35 m above its shadow (S8)
DOUBLE_AT = [-34.0, 1.15, -46.0]     # where the stone's double appears (Uncanny, P2)
HIDDEN = [0.0, -400.0, 0.0]          # parked far below the plain (its shadow cannot reach the ground)
STUCK_AT = [-3.2, 3.9, 0.35]         # the macroblock that does not refresh (Recovery): on the long limb

# ============================================================================================ palette
# Linear RGB. Dream: Dali's Catalan afternoon -- cream, ochre, Cap de Creus blue, peach horizon (brief §7).
DREAM = {
    "zenith": [0.22, 0.38, 0.66], "horizon": [0.96, 0.78, 0.60], "groundSky": [0.50, 0.40, 0.30],
    "sun": [1.0, 0.80, 0.58], "ochreDark": [0.36, 0.25, 0.14], "ochre": [0.56, 0.41, 0.24],
    "ochreLight": [0.70, 0.56, 0.38], "barkDark": [0.07, 0.05, 0.045], "bark": [0.16, 0.115, 0.09],
    "barkLight": [0.28, 0.22, 0.17], "stone": [0.78, 0.73, 0.67],
}
# The corruption colours are the decoder's own failure colours (research Part 4): overflowing chroma is magenta,
# zeroed chroma is green-chartreuse.
MAGENTA = [1.0, 0.08, 0.62]
MAGENTA_DARK = [0.30, 0.01, 0.16]
CHARTREUSE = [0.62, 1.0, 0.06]
CYAN = [0.05, 0.85, 1.0]

# ============================================================================================ helpers


def norm(v):
    m = math.sqrt(sum(x * x for x in v))
    return [x / m for x in v]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def add(a, b):
    return [x + y for x, y in zip(a, b)]


def mul(a, s):
    return [x * s for x in a]


def euler_from_matrix(R):
    """Degrees (x, y, z) such that glm::quat(radians(e)) == R, with R = Rz * Ry * Rx (glm's convention)."""
    y = math.asin(max(-1.0, min(1.0, -R[2][0])))
    x = math.atan2(R[2][1], R[2][2])
    z = math.atan2(R[1][0], R[0][0])
    return [math.degrees(x), math.degrees(y), math.degrees(z)]


def rot_y_to(d):
    """Rotation matrix taking +Y onto the unit vector d (Rodrigues)."""
    dx, dy, dz = d
    ax, az = dz, -dx
    s = math.sqrt(ax * ax + az * az)
    c = dy
    if s < 1e-9:
        return [[1, 0, 0], [0, 1 if c > 0 else -1, 0], [0, 0, 1 if c > 0 else -1]]
    ax, az = ax / s, az / s
    t = 1 - c
    return [[t * ax * ax + c, -s * az, t * ax * az], [s * az, c, -s * ax], [t * ax * az, s * ax, t * az * az + c]]


def frame_x_to(d):
    """Columns (x, y, z): x along d, z horizontal, y = z x x (upward-ish). Returns the matrix R (local->world)."""
    x = norm(d)
    z = cross(x, [0.0, 1.0, 0.0])
    if math.sqrt(dot(z, z)) < 1e-6:
        z = [0.0, 0.0, 1.0]
    z = norm(z)
    y = cross(z, x)
    return [[x[0], y[0], z[0]], [x[1], y[1], z[1]], [x[2], y[2], z[2]]]


def mat_t_vec(R, v):
    """R^T v."""
    return [R[0][i] * v[0] + R[1][i] * v[1] + R[2][i] * v[2] for i in range(3)]


def sdf(kind, children=None, name=None, **kw):
    n = {"kind": kind}
    n.update(kw)
    if children is not None:
        n["children"] = children
    if name:
        n["name"] = name
    return n


def capsule_between(a, b, r, name=None):
    d = sub(b, a)
    L = math.sqrt(dot(d, d))
    mid = mul(add(a, b), 0.5)
    e = euler_from_matrix(rot_y_to(norm(d)))
    return sdf("translate", [sdf("rotate", [sdf("capsule", radius=r, height=L)], rotation=e)], translation=mid,
               name=name)


def chunked(items, kind, k):
    """smoothUnion takes at most 8 children: fold in chunks of 8."""
    if len(items) == 1:
        return items[0]
    out = [sdf(kind, items[i:i + 8], smooth=k) if len(items[i:i + 8]) > 1 else items[i]
           for i in range(0, len(items), 8)]
    return out[0] if len(out) == 1 else sdf(kind, out, smooth=k)


def field(name, position=None, **f):
    f.setdefault("falloff", {"kind": "none"})
    return {"name": name, "kind": "field", "position": position or [0, 0, 0], "field": f}


def op(kind, dst, **kw):
    o = {"kind": kind, "dst": dst}
    o.update(kw)
    return o


# ============================================================================================ the tree
# One bare tree, the protagonist (brief §4). A dead-olive gesture: a short leaning trunk that turns into three
# limbs, one long and nearly horizontal (the limb the dream softens first). The seeded skeleton is shared by the
# SDF, the voxel shell (its blocks) and nothing else, so the blocks sit exactly on the bark they replace.

TREE_SEED = 7


def tree_skeleton():
    rnd = random.Random(TREE_SEED)
    segs = []  # (a, b, radius, limb) in tree-local space

    def grow(a, d, L, r, limb, nseg=2):
        p = a
        for s in range(nseg):
            jitter = [rnd.uniform(-0.18, 0.18), rnd.uniform(-0.05, 0.12), rnd.uniform(-0.18, 0.18)]
            d = norm(add(d, jitter))
            q = add(p, mul(d, L / nseg))
            r1 = r * (0.78 if s < nseg - 1 else 0.62)
            segs.append((p, q, (r + r1) / 2, limb))
            p, r = q, r1
        return p, d, r

    top, d, r = grow([0, -0.2, 0], norm([0.22, 1, 0.08]), 3.1, 0.42, -1, nseg=3)
    limbs = [(norm([1.0, 0.18, 0.15]), 4.6), (norm([-0.55, 0.85, 0.2]), 3.0), (norm([0.1, 0.9, -0.6]), 2.6)]
    bases = []
    for li, (ld, LL) in enumerate(limbs):
        bases.append((list(top), ld))
        end, dd, rr = grow(top, ld, LL, r * 0.82, li, nseg=2)
        for k in range(2):
            side = [rnd.uniform(-0.8, 0.8), rnd.uniform(0.3, 0.9), rnd.uniform(-0.8, 0.8)]
            sd = norm(add(mul(dd, 0.6), side))
            t = 0.55 + 0.4 * k
            a = add(top, mul(sub(end, top), t))
            e2, d2, r2 = grow(a, sd, LL * 0.45, rr * 0.9, li, nseg=1)
            twig = norm([d2[0] + rnd.uniform(-0.6, 0.6), d2[1] + 0.3, d2[2] + rnd.uniform(-0.6, 0.6)])
            grow(e2, twig, LL * 0.22, r2 * 0.8, li, nseg=1)
    return segs, bases


def tree_sdf_tree():
    segs, bases = tree_skeleton()
    trunk = chunked([capsule_between(a, b, r) for (a, b, r, limb) in segs if limb == -1], "smoothUnion", 0.25)
    limbs = []
    for li, (base, ld) in enumerate(bases):
        # Each limb is built in its own frame (x along the limb, from its base), so `bend` curls it down like wax
        # (brief §8: "objects becoming wax-like") and `translate` lifts it away whole (it detaches, §4).
        R = frame_x_to(ld)
        local = [capsule_between(mat_t_vec(R, sub(a, base)), mat_t_vec(R, sub(b, base)), r)
                 for (a, b, r, limb) in segs if limb == li]
        bent = sdf("bend", [chunked(local, "smoothUnion", 0.12)], amount=0.0, name=f"melt{li}")
        oriented = sdf("rotate", [bent], rotation=euler_from_matrix(R))
        limbs.append(sdf("translate", [oriented], translation=base, name=f"limb{li}"))
    body = sdf("smoothUnion", [trunk] + limbs, smooth=0.22, name="joints")
    # The surface recedes where the contagion has eaten it, and the voxel shell's blocks take its place.
    eaten = sdf("displaceField", [body], amount=0.0, reference="rot", name="eaten")
    return {"root": sdf("displaceNoise", [eaten], amount=0.016, frequency=7.0, speed=0.0, seed=3, name="bark")}


def capsule_distance(p, a, b, r):
    pa, ba = sub(p, a), sub(b, a)
    h = max(0.0, min(1.0, dot(pa, ba) / max(dot(ba, ba), 1e-9)))
    q = sub(pa, mul(ba, h))
    return math.sqrt(dot(q, q)) - r


def voxel_shell(dist, lo, hi, cell, thickness=1.0):
    """Cell centres whose distance is within (-thickness * cell, 0]: one layer of blocks just inside the surface."""
    out = []
    nx, ny, nz = [int(math.ceil((hi[i] - lo[i]) / cell)) for i in range(3)]
    for i in range(nx):
        x = lo[0] + (i + 0.5) * cell
        for j in range(ny):
            y = lo[1] + (j + 0.5) * cell
            for k in range(nz):
                z = lo[2] + (k + 0.5) * cell
                d = dist([x, y, z])
                if -thickness * cell < d <= 0.4 * cell:
                    out.append([x, y, z])
    return out


TREE_CELL = 0.16


def tree_blocks():
    segs, _ = tree_skeleton()
    caps = [(a, b, r) for (a, b, r, _l) in segs]

    def dist(p):
        return min(capsule_distance(p, a, b, r) for (a, b, r) in caps)

    pts = voxel_shell(dist, [-5, -0.2, -5], [9, 9, 5], TREE_CELL)
    s = TREE_CELL * 0.94
    return [[round(x, 4), round(y, 4), round(z, 4), 0, 0, 0, 1, s, s, s] for (x, y, z) in pts]


# ============================================================================================ the stone
STONE_BLOBS = [([0.0, 0.0, 0.0], 1.0), ([0.75, -0.18, 0.2], 0.72), ([-0.7, -0.3, -0.25], 0.55)]


def stone_sdf_tree():
    blob = sdf("smoothUnion", [sdf("translate", [sdf("sphere", radius=r)], translation=c) for c, r in STONE_BLOBS],
               smooth=0.6)
    eaten = sdf("displaceField", [blob], amount=0.0, reference="rot", name="eaten")
    return {"root": eaten}


STONE_CELL = 0.2


def stone_blocks():
    def smin(a, b, k):
        h = max(k - abs(a - b), 0.0) / k
        return min(a, b) - h * h * k * 0.25

    def dist(p):
        d = None
        for c, r in STONE_BLOBS:
            e = math.sqrt(dot(sub(p, c), sub(p, c))) - r
            d = e if d is None else smin(d, e, 0.6)
        return d

    pts = voxel_shell(dist, [-1.6, -1.4, -1.4], [1.7, 1.2, 1.3], STONE_CELL)
    s = STONE_CELL * 0.94
    return [[round(x, 4), round(y, 4), round(z, 4), 0, 0, 0, 1, s, s, s] for (x, y, z) in pts]


# ============================================================================================ far things
def headland_sdf_tree():
    # Cap de Creus seen from far away: a long low mass, its profile broken by two knuckles. It is the scale reference
    # the plain otherwise lacks, and it is softened by 700 m of haze.
    parts = [sdf("translate", [sdf("sphere", radius=r)], translation=c) for c, r in
             [([0, -40, 0], 75), ([90, -55, 20], 80), ([-80, -60, -10], 70), ([150, -70, 0], 75), ([40, -5, 10], 26)]]
    return {"root": sdf("displaceNoise", [sdf("smoothUnion", parts, smooth=30.0)], amount=6.0, frequency=0.02,
                        seed=21)}


def cloud_sdf_tree(seed):
    rnd = random.Random(seed)
    parts = []
    for k in range(6):
        c = [rnd.uniform(-60, 60), rnd.uniform(-4, 6), rnd.uniform(-14, 14)]
        parts.append(sdf("translate", [sdf("sphere", radius=rnd.uniform(14, 24))], translation=c))
    flat = sdf("smoothUnion", parts, smooth=16.0)
    return {"root": sdf("displaceNoise", [flat], amount=2.5, frequency=0.05, seed=seed)}


# ============================================================================================ fields
def fields():
    return [
        # the contagion, as every consumer reads it (the grid's value, gained)
        field("stain", kind="grid", reference="contagion", strength=1.0),
        # the contagion's source: the stone (the first block that went bad)
        field("infect", position=STONE_AT, kind="sphere", radius=1.6, softness=2.5, strength=0.0),
        # how it creeps: outward from the stone, along curl noise (ink in water; Ernst's decalcomania)
        field("creep", kind="curlNoise", frequency=0.06, speed=0.05, seed=5, strength=1.0),
        field("outward", position=STONE_AT, kind="radialVector", strength=0.6),
        field("flow", kind="compound", children=["creep", "outward"], combine="add", strength=1.0),
        # infection climbs: 1 below the plane, which rises as the dose grows
        field("climb", position=[0, 0.0, 0], kind="plane", axis=[0, 1, 0], softness=1.2, invert=True, strength=1.0),
        # rot: how eaten a point is -- the stain where the climbing front has reached. ONE compound level: the GPU
        # evaluates a compound inside a compound as 0 (docs/gpu-fields.md); the grid's ceiling (ADR-1163) bounds it.
        field("rot", kind="compound", children=["stain", "climb"], combine="multiply", strength=1.0),
        # the stuck macroblock (Recovery): a box of space that did not refresh
        field("stuck", position=STUCK_AT, kind="box", size=[0.45, 0.45, 0.45], softness=0.02, strength=0.0),
        # the kick: fronts travelling out across the plain from the stone
        field("kick", position=STONE_AT, kind="onset", onsetSource="low", onsetDecay=1.2, onsetWidth=4.0,
              audioSpeed=24.0, waveGeometry="radial", axis=[0, 1, 0], strength=1.0),
        # each infected cell hears its own band
        field("bands", kind="spectrum", audioBand="element", bandLow=0.05, bandHigh=1.0, strength=1.0),
        # the fragments' release: up, and a tumble
        field("lift", kind="direction", axis=[0, 1, 0], strength=1.0),
        field("tumble", kind="curlNoise", frequency=0.4, speed=0.3, seed=9, strength=1.0),
    ]


CONTAGION = {
    "name": "contagion", "enabled": True, "mode": "scalar", "wrap": "clamp",
    "resolution": [128, 1, 128], "boundsMin": [-64, -1, -64], "boundsMax": [64, 1, 64],
    "injectField": "infect", "injectRate": 1.0, "velocityField": "flow", "advect": 0.0,
    "diffusion": 0.4, "diffuseIterations": 4, "dissipation": 0.0, "ceiling": 1.0,
    "simRate": 30.0, "maxSubSteps": 4, "seed": 2026, "seedAmount": 0.0, "checkpointInterval": 5.0,
}


# ============================================================================================ material programs
def plain_program():
    # The plain: Dali's ochre, mottled at the scale of dunes, and the contagion as square cells that fail one at a
    # time as the stain passes their own random (ADR-1162) -- a stain with a macroblock edge.
    return {"name": "plain", "ops": [
        op("input", 0, input="worldPosition"),
        op("quantize", 1, srcA=0, value=2.2, seed=3),                      # 0.45 m macroblocks; w = cell random
        op("field", 2, field="stain"),
        op("field", 3, field="stuck"),
        op("add", 2, srcA=2, srcB=3),
        op("remap", 2, srcA=2, value=1, constant=[0.04, 0.9, 0.0, 1.0]),   # stain -> 0..1
        op("swizzle", 4, srcA=1, constant=[3, 3, 3, 3]),
        op("constant", 5, constant=[-1, -1, -1, -1]),
        op("multiply", 4, srcA=4, srcB=5),
        op("add", 4, srcA=2, srcB=4),                                      # stain - cell random
        op("smoothstep", 4, srcA=4, constant=[0.0, 0.004, 0, 0]),          # the cell has failed: 0 or 1
        # the ground: dune-scale mottle between three ochres
        op("noise", 6, srcA=0, value=0.045, seed=7),
        op("ramp", 6, srcA=6, constant=DREAM["ochreDark"] + [1], constant2=DREAM["ochre"] + [1],
           constant3=DREAM["ochreLight"] + [1]),
        op("constant", 5, constant=MAGENTA_DARK + [1]),
        op("mixBy", 6, srcA=6, srcB=5, srcC=4),
        # the failed cells glow, with the treble and the kick's front
        op("input", 2, input="audio"),
        op("swizzle", 2, srcA=2, constant=[3, 3, 3, 3]),
        op("field", 3, field="kick"),
        op("add", 2, srcA=2, srcB=3),
        op("remap", 2, srcA=2, value=0, constant=[0.0, 1.0, 0.0, 1.6]),
        op("constant", 7, constant=MAGENTA + [1]),
        op("constant", 5, constant=[0, 0, 0, 0]),
        op("hueShift", 7, srcA=7, srcB=5, value=0.0),                     # timbre turns the strain (routed)
        op("multiply", 7, srcA=7, srcB=2),
        op("multiply", 7, srcA=7, srcB=4),
    ], "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 0.0, "opacity": -1}


def bark_program():
    return {"name": "bark", "ops": [
        op("input", 0, input="worldPosition"),
        op("quantize", 1, srcA=0, value=6.0, seed=11),                     # 0.17 m blocks on the bark
        op("field", 2, field="rot"),
        op("field", 3, field="stuck"),
        op("add", 2, srcA=2, srcB=3),
        op("swizzle", 4, srcA=1, constant=[3, 3, 3, 3]),
        op("constant", 5, constant=[-1, -1, -1, -1]),
        op("multiply", 4, srcA=4, srcB=5),
        op("add", 4, srcA=2, srcB=4),
        op("smoothstep", 4, srcA=4, constant=[0.0, 0.004, 0, 0]),
        op("noise", 6, srcA=0, value=2.3, seed=5),
        op("ramp", 6, srcA=6, constant=DREAM["barkDark"] + [1], constant2=DREAM["bark"] + [1],
           constant3=DREAM["barkLight"] + [1]),
        op("constant", 5, constant=MAGENTA_DARK + [1]),
        op("mixBy", 6, srcA=6, srcB=5, srcC=4),
        op("constant", 7, constant=MAGENTA + [1]),
        op("multiply", 7, srcA=7, srcB=4),
    ], "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 0.0, "opacity": -1}


def stone_program():
    return {"name": "skin", "ops": [
        op("input", 0, input="worldPosition"),
        op("quantize", 1, srcA=0, value=5.0, seed=13),
        op("field", 2, field="rot"),
        op("field", 3, field="stuck"),
        op("add", 2, srcA=2, srcB=3),
        op("swizzle", 4, srcA=1, constant=[3, 3, 3, 3]),
        op("constant", 5, constant=[-1, -1, -1, -1]),
        op("multiply", 4, srcA=4, srcB=5),
        op("add", 4, srcA=2, srcB=4),
        op("smoothstep", 4, srcA=4, constant=[0.0, 0.004, 0, 0]),
        op("constant", 6, constant=DREAM["stone"] + [1]),
        op("constant", 5, constant=MAGENTA_DARK + [1]),
        op("mixBy", 6, srcA=6, srcB=5, srcC=4),
        op("constant", 7, constant=MAGENTA + [1]),
        op("multiply", 7, srcA=7, srcB=4),
    ], "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 0.0, "opacity": -1}


def blocks_program():
    # The blocks are corrupted matter: the stain's colour, each block its own value, flaring with its band.
    return {"name": "blocks", "ops": [
        op("input", 0, input="instanceRandom"),
        op("swizzle", 1, srcA=0, constant=[0, 0, 0, 0]),
        op("ramp", 2, srcA=1, constant=MAGENTA_DARK + [1], constant2=[0.55, 0.05, 0.32, 1], constant3=MAGENTA + [1]),
        op("field", 3, field="kick"),
        op("remap", 3, srcA=3, value=0, constant=[0.0, 1.0, 0.25, 2.5]),
        op("multiply", 4, srcA=2, srcB=3),
    ], "baseColor": 2, "metallic": -1, "roughness": -1, "emission": 4, "emissionIntensity": 0.0, "opacity": -1}


# ============================================================================================ the scene
SUN_DIR = norm([0.42, -0.2, 0.88])   # the direction the light travels: low (~11 deg), from behind-left


def scene():
    sky = {"enabled": True, "background": True, "useKeyLight": False, "sunDirection": mul(SUN_DIR, -1.0),
           "zenithColor": DREAM["zenith"], "horizonColor": DREAM["horizon"], "groundColor": DREAM["groundSky"],
           "sunColor": DREAM["sun"], "haze": 0.55, "sunIntensity": 1.0, "sunSize": 0.5, "sunGlow": 0.15,
           "intensity": 1.0}
    tree_pts = tree_blocks()
    stone_pts = stone_blocks()
    nodes = fields() + [
        {"name": "plain", "kind": "procedural", "procedural": {
            # 6 km: a size parameter runs at most 1000 (it clamped, and the plain's edge was the "horizon"); scaled
            "source": {"kind": "box", "size": [1000.0, 0.02, 1000.0], "subdivisions": 1},
            "sourceTransform": {"position": [0, -0.01, 0], "rotation": [0, 0, 0], "scale": [6, 1, 6]},
            "distribution": {"kind": "single"}, "lod": {"cull": False, "count": 1},
            "material": {"baseColor": DREAM["ochre"], "roughness": 0.95, "metallic": 0.0, "emissiveColor": [1, 1, 1],
                         "emissiveIntensity": 1.0, "program": "plain"}}},
        {"name": "tree", "kind": "sdf", "position": TREE_AT, "sdf": {
            "tree": tree_sdf_tree(),
            "material": {"baseColor": DREAM["bark"], "roughness": 0.8, "metallic": 0.0, "emissiveColor": [1, 1, 1],
                         "emissiveIntensity": 1.0, "program": "bark"},
            "renderMode": "raymarch", "boundsMin": [-7, -0.5, -7], "boundsMax": [11, 10, 7],
            "compile": True, "maxSteps": 128, "stepScale": 0.8, "epsilon": 0.0008, "normalEpsilon": 0.003,
            "maxDistance": 600.0, "castShadows": True, "depthPrepass": True, "look": {"aoStrength": 0.6, "aoDistance": 0.6}}},
        {"name": "stone", "kind": "sdf", "position": STONE_AT, "sdf": {
            "tree": stone_sdf_tree(),
            "material": {"baseColor": DREAM["stone"], "roughness": 0.32, "metallic": 0.0, "emissiveColor": [1, 1, 1],
                         "emissiveIntensity": 1.0, "program": "skin"},
            "renderMode": "raymarch", "boundsMin": [-2, -1.6, -1.6], "boundsMax": [2.2, 1.4, 1.7],
            "compile": True, "maxSteps": 96, "stepScale": 0.85, "maxDistance": 600.0,
            "castShadows": True, "depthPrepass": True, "look": {"aoStrength": 0.4}}},
        # the double: the same stone, the same light, somewhere it should not be (P2). Parked until the Uncanny.
        {"name": "double", "kind": "sdf", "position": HIDDEN, "sdf": {
            "tree": {"root": sdf("smoothUnion", [sdf("translate", [sdf("sphere", radius=r)], translation=c)
                                                 for c, r in STONE_BLOBS], smooth=0.6)},
            "material": {"baseColor": DREAM["stone"], "roughness": 0.32, "metallic": 0.0},
            "renderMode": "raymarch", "boundsMin": [-2, -1.6, -1.6], "boundsMax": [2.2, 1.4, 1.7],
            "compile": True, "maxSteps": 96, "stepScale": 0.85, "maxDistance": 900.0,
            "castShadows": True, "depthPrepass": True}},
        {"name": "headland", "kind": "sdf", "position": [-260.0, 0.0, -760.0], "sdf": {
            "tree": headland_sdf_tree(),
            "material": {"baseColor": [0.42, 0.33, 0.27], "roughness": 0.9, "metallic": 0.0},
            "renderMode": "raymarch", "boundsMin": [-170, -10, -110], "boundsMax": [250, 60, 120],
            "compile": True, "maxSteps": 96, "stepScale": 0.75, "maxDistance": 2000.0,
            "castShadows": False, "depthPrepass": True}},
        blocks_node("treeBlocks", TREE_AT, tree_pts, TREE_CELL),
        blocks_node("stoneBlocks", STONE_AT, stone_pts, STONE_CELL),
        {"name": "motes", "kind": "particles", "particles": {
            # dust in the dream light; later the infected ground's spores (emit mask = the stain)
            "capacity": 20000, "seed": 4, "shape": "disc", "position": [2, 0.2, -6], "extent": [40, 0, 40],
            "direction": [0, 1, 0], "spread": 0.6, "spawnRate": 40, "speedMin": 0.05, "speedMax": 0.25,
            "gravity": [0, 0.04, 0], "drag": 0.2, "turbulence": 0.25, "turbulenceScale": 0.15,
            "turbulenceSpeed": 0.15, "sizeStart": 0.035, "sizeEnd": 0.01, "sizeVariance": 0.6,
            "lifetimeMin": 6.0, "lifetimeMax": 11.0, "colorStart": [1.0, 0.85, 0.6, 0.9], "colorEnd": [1.0, 0.7, 0.5, 0.0],
            "emissive": 2.0, "blend": "additive", "softness": 0.6}},
    ]
    return {
        "format": "avgen-scene", "version": 1, "name": "DIGITAL MOSH",
        "_note": "Generated by build.py; edit that, not this file.",
        "camera": {"mode": 1, "position": [0.0, 1.4, 24.0], "target": [0.0, 3.6, 0.0], "fov": 30},
        "environment": {
            "intensity": 0.3, "background": DREAM["horizon"], "fogColor": DREAM["horizon"],
            "shadowRange": 160.0, "shadowCascades": 3,
            # aerial perspective: a thin, low medium that takes the sky's colour with distance, so the plain never
            # ends -- it dissolves into the sky (Tanguy). No edge, no line, no black (the owner's dead-space rule).
            "volumeDensity": 0.0011, "volumeScattering": 0.9, "volumeAbsorption": 0.12, "volumeAnisotropy": 0.25,
            "volumeSteps": 24, "volumeMaxDistance": 3000.0, "fogHeight": 0.0, "fogHeightFalloff": 0.02,
            "fogSky": 1.0, "fogSkyDistance": 1400.0,
            "sky": sky,
        },
        "lights": [
            {"name": "sun", "id": "sun", "type": "directional", "role": "key", "direction": SUN_DIR,
             "color": DREAM["sun"], "intensity": 7.0, "castsShadow": True, "shadowStrength": 1.0, "softness": 0.35},
            # the first fracture's light: magenta, at the stone, off until the infection
            {"name": "fracture", "id": "fracture", "type": "point", "position": add(STONE_AT, [0, 0.5, 0]),
             "color": MAGENTA, "intensity": 0.0, "range": 30.0, "radius": 0.6, "castsShadow": False, "volumetric": 1.0},
        ],
        "grids": [CONTAGION],
        "materialPrograms": [plain_program(), bark_program(), stone_program(), blocks_program()],
        "nodes": nodes,
    }


def blocks_node(name, at, pts, cell):
    return {"name": name, "kind": "procedural", "position": at, "procedural": {
        "source": {"kind": "box", "size": [1.0, 1.0, 1.0], "subdivisions": 1},
        "variation": {"seed": 3},
        "distribution": {"kind": "points", "points": pts},
        "effectors": [
            # revealed where the contagion has eaten the surface (scale 0 elsewhere: culled)
            {"field": "rot", "op": "scale", "blend": "multiply", "strength": 1.0, "scaleAxis": [1, 1, 1]},
            # released: up, with a tumble (strengths routed by the stage)
            {"field": "lift", "op": "positionOffset", "blend": "add", "strength": 0.0},
            {"field": "tumble", "op": "positionOffset", "blend": "add", "strength": 0.0},
            {"field": "tumble", "op": "rotation", "blend": "add", "strength": 0.0},
            {"field": "kick", "op": "positionOffset", "blend": "add", "strength": 0.0, "axis": [0, 1, 0]},
        ],
        "lod": {"cull": True, "count": 1},
        "material": {"baseColor": MAGENTA_DARK, "roughness": 0.35, "metallic": 0.0, "emissiveColor": [1, 1, 1],
                     "emissiveIntensity": 1.0, "program": "blocks"}}}


# ============================================================================================ the performance
ZERO_DEFAULTS = [
    {"source": "audio.bass", "target": "root/scale", "op": "add", "amount": 0.0},
    {"source": "audio.mid", "target": "root/rotationSpeed", "op": "add", "amount": 0.0},
    {"source": "audio.rms", "target": "scene/brightness", "op": "add", "amount": 0.0},
    {"source": "audio.onset", "target": "root/impulse", "op": "add", "amount": 0.0},
]

POST = {
    "post/bloom/enabled": True, "post/bloom/threshold": 1.4, "post/bloom/emissionWeight": 0.7,
    "post/tonemap/chroma-retention": 0.55, "post/output/vignette": 0.22,
    "camera/exposure/mode": 0, "camera/mode": 0, "camera/orbitPivotWeight": 1.0,
    # The temporal ring is always warm (enabled) and every amount is routed or staged (agent survey: amount 0 draws
    # nothing but keeps the ring filled, so a burst never opens on an empty history).
    "temporal/echo/enabled": True, "temporal/echo/frames": 8.0, "temporal/echo/decay": 0.7,
    "temporal/mosh/enabled": True, "temporal/mosh/frames": 16.0, "temporal/mosh/smear": 22.0,
    "temporal/mosh/rate": 10.0,
    "sources/driftA/rate": 0.017, "sources/driftB/rate": 0.0113, "sources/turn/rate": 0.006,
}

# ------------------------------------------------------------------------------------------------ the stages
# One preset per stage: the large-scale configuration of the world. Continuous expression (layers 1-3 of the audio
# mapping) rides on top as routes whose depth is a GATE macro the stage sets, so the same music does more as the dream
# decays (restraint early, brief §16). Each stage breaks ONE more law than the last (research S2).
#
# Keys: camera (dist, height, fov, pivot, orbit, roll), contagion (inject, advect, dissip, climb), eaten (tree, stone),
# limbs (melt per limb: + droops like wax, - lifts against gravity; lift = metres a limb floats off), blocks (rise,
# tumble, spin, kick), light (sunYaw: the shadows swing while the sky's sun stays; fracture), image (echo, mosh,
# glitch, pixel, poster, sort, split, exposure, bloom), atmosphere (fog density, fog tint), gates (g*), keyframe, heal.
BASE = dict(dist=24.0, height=1.4, fov=30.0, pivot=[0.0, 3.6, 0.0], orbit=0.010, roll=0.0,
            inject=0.0, advect=0.0, dissip=0.6, climb=0.0, eatTree=0.0, eatStone=0.0,
            melt=[0.0, 0.0, 0.0], lift=[0.0, 0.0, 0.0], bark=0.016, barkSpeed=0.0,
            rise=0.0, tumble=0.0, spin=0.0, kick=0.0,
            sunYaw=0.0, sunColor=DREAM["sun"], fracture=0.0, double=False, motes=40.0,
            echo=0.0, mosh=0.0, moshBlock=32.0, glitch=0.0, pixel=0.0, poster=0.0, sort=0.0, split=0.0,
            exposure=0.0, bloom=0.08, volDen=0.0011, fogTint=DREAM["horizon"],
            gMicro=0.15, gRhythm=0.0, gMosh=0.0, gGlitch=0.0, gMelt=0.0, gLift=0.0,
            glowPlain=0.0, glowBark=0.0, glowBlocks=0.0, keyframe=0.0, heal=0.0, stainHue=0.0, grade=0.0)


def stage(**kw):
    v = dict(BASE)
    v.update(kw)
    return v


STAGES = {
    # Beautiful, quiet, hypnotic. Only the light and the dust move.
    "Dream": stage(),
    # Relation errors only (Magritte, S12): the shadows swing against the sky's sun, the stone appears twice, a
    # dolly-zoom stretches the space while the tree keeps its size (D1), the long limb lifts a little against gravity,
    # the light cools toward turquoise. Nothing is broken; everything is slightly wrong.
    "Uncanny": stage(dist=19.0, fov=38.0, pivot=[0.5, 3.4, -1.0], orbit=0.012, sunYaw=26.0, double=True,
                     melt=[-0.05, 0.0, 0.0], sunColor=[0.86, 0.9, 0.86], echo=0.08, gMicro=0.3, gRhythm=0.1,
                     grade=0.35, volDen=0.0013),
    # The first block goes bad: the magenta fracture at the stone. Its light, its stain on the plain, the first blocks;
    # the stain creeps toward the tree and starts to climb it. The long limb begins to soften.
    "Infection": stage(dist=17.0, height=1.8, fov=36.0, pivot=[3.0, 3.0, -5.0], orbit=0.016, inject=1.4, advect=0.9,
                       dissip=0.02, climb=1.6, eatTree=0.18, eatStone=0.35, melt=[0.06, 0.0, 0.0], fracture=700.0,
                       double=True, sunYaw=8.0, glowPlain=2.5, glowBark=1.8, glowBlocks=3.0, echo=0.1, mosh=0.0,
                       gMicro=0.5, gRhythm=0.35, gMosh=0.15, gMelt=0.2, motes=70.0, volDen=0.0014, grade=0.2),
    # The rules are going: the stain owns the ground near the stone, the tree is eaten from the root up and its bark
    # flows like wax (liquefies), the limbs droop under an imaginary gravity, the first blocks lift and tumble.
    "Corruption": stage(dist=13.0, height=1.1, fov=42.0, pivot=[-1.0, 3.2, -2.0], orbit=-0.03, inject=2.4,
                        advect=2.0, dissip=0.0, climb=4.6, eatTree=0.32, eatStone=0.5, melt=[0.16, 0.08, -0.06],
                        bark=0.05, barkSpeed=0.5, rise=0.35, tumble=0.25, spin=0.4, kick=0.25, fracture=1900.0,
                        double=True, glowPlain=4.0, glowBark=3.0, glowBlocks=5.0, echo=0.18, mosh=0.04,
                        moshBlock=32.0, exposure=0.05, gMicro=0.75, gRhythm=0.65, gMosh=0.45, gMelt=0.5, gLift=0.3,
                        motes=160.0, volDen=0.0018, fogTint=[0.9, 0.62, 0.72], grade=0.0, stainHue=0.0),
    # Systemic: the tree is blocks, its limbs float free of the trunk, the camera stands inside the tree and the
    # horizon tilts (P5: the ground plane is lost, not shaken). Mosh and tears on the drums.
    "Nightmare": stage(dist=8.0, height=2.8, fov=54.0, pivot=[-5.0, 3.8, 0.5], orbit=0.07, roll=7.0, inject=3.5,
                       advect=3.4, dissip=0.0, climb=9.5, eatTree=0.55, eatStone=0.7, melt=[0.26, 0.18, -0.14],
                       lift=[1.3, 0.7, 1.0], bark=0.09, barkSpeed=1.0, rise=1.8, tumble=1.1, spin=1.5, kick=0.6,
                       sunYaw=-34.0, sunColor=[1.0, 0.62, 0.46], fracture=3200.0, double=True, glowPlain=6.0,
                       glowBark=4.5, glowBlocks=7.0, echo=0.3, mosh=0.1, moshBlock=24.0, glitch=0.0, exposure=0.1,
                       gMicro=1.0, gRhythm=1.0, gMosh=0.85, gGlitch=0.6, gMelt=1.0, gLift=1.0, motes=420.0,
                       volDen=0.0026, fogTint=[0.85, 0.42, 0.62]),
    # The collapse, through the representations the renderer built the world from (G10, brief §15), on the bar grid:
    "Collapse": stage(dist=40.0, height=46.0, fov=58.0, pivot=[2.0, 0.0, -6.0], orbit=0.05, inject=5.0, advect=4.0,
                      dissip=0.0, climb=12.0, eatTree=1.0, eatStone=1.0, melt=[0.4, 0.3, -0.3], lift=[3.0, 2.0, 2.5],
                      bark=0.12, barkSpeed=1.5, rise=5.0, tumble=2.5, spin=3.0, kick=1.2, fracture=5000.0,
                      double=True, glowPlain=8.0, glowBark=6.0, glowBlocks=9.0, echo=0.35, mosh=0.25, moshBlock=48.0,
                      exposure=0.15, gMicro=1.0, gRhythm=1.0, gMosh=1.0, gGlitch=0.8, gMelt=1.0, gLift=1.0,
                      motes=900.0, volDen=0.0030, fogTint=[0.8, 0.3, 0.6]),
    #   ... fragments become particles and temporal fragments (the mosh holds old frames over new)
    "Decay": stage(dist=40.0, height=60.0, fov=60.0, pivot=[2.0, 0.0, -6.0], orbit=0.05, inject=5.0, advect=4.0,
                   dissip=0.0, climb=12.0, eatTree=1.0, eatStone=1.0, melt=[0.4, 0.3, -0.3], lift=[5.0, 3.5, 4.0],
                   bark=0.12, barkSpeed=1.5, rise=9.0, tumble=4.0, spin=5.0, kick=1.6, fracture=5000.0, double=True,
                   glowPlain=8.0, glowBark=6.0, glowBlocks=10.0, echo=0.5, mosh=0.6, moshBlock=64.0, exposure=0.2,
                   gMicro=1.0, gRhythm=1.0, gMosh=1.0, gGlitch=1.0, gMelt=1.0, gLift=1.0, motes=2600.0,
                   volDen=0.0034, fogTint=[0.8, 0.3, 0.6]),
    #   ... pixels, then colour (posterised to a few levels, sorted)
    "Pixels": stage(dist=40.0, height=60.0, fov=60.0, pivot=[2.0, 0.0, -6.0], orbit=0.05, inject=5.0, advect=4.0,
                    dissip=0.0, climb=12.0, eatTree=1.0, eatStone=1.0, melt=[0.4, 0.3, -0.3], lift=[6.0, 4.5, 5.0],
                    bark=0.12, barkSpeed=1.5, rise=12.0, tumble=5.0, spin=6.0, kick=1.6, fracture=5000.0,
                    double=True, glowPlain=8.0, glowBark=6.0, glowBlocks=10.0, echo=0.4, mosh=0.5, moshBlock=96.0,
                    pixel=18.0, poster=5.0, sort=0.6, exposure=0.4, gMicro=1.0, gRhythm=1.0, gMosh=1.0,
                    gGlitch=1.0, gMelt=1.0, gLift=1.0, motes=2600.0, volDen=0.0034, fogTint=[0.8, 0.3, 0.6]),
    #   ... and light: everything burns out to white.
    "Light": stage(dist=40.0, height=60.0, fov=60.0, pivot=[2.0, 0.0, -6.0], orbit=0.05, inject=5.0, advect=4.0,
                   dissip=0.0, climb=12.0, eatTree=1.0, eatStone=1.0, lift=[6.0, 4.5, 5.0], rise=12.0, tumble=5.0,
                   spin=6.0, fracture=5000.0, double=True, glowPlain=8.0, glowBark=6.0, glowBlocks=10.0, echo=0.6,
                   mosh=0.3, pixel=40.0, poster=3.0, exposure=4.5, bloom=1.5, gMicro=1.0, gRhythm=1.0, gMosh=1.0,
                   gGlitch=1.0, motes=2600.0, volDen=0.004, fogTint=[1.0, 1.0, 1.0]),
    # A breakdown's respite: the dream comes back, but the stain stays on the ground (a temporary recovery).
    "Respite": stage(dist=22.0, height=1.5, fov=32.0, pivot=[1.0, 3.5, -1.0], orbit=0.008, dissip=0.25, climb=0.0,
                     double=True, keyframe=1.0, heal=0.45, glowPlain=1.0, gMicro=0.3, echo=0.05, volDen=0.0012),
    # The keyframe: everything suddenly calm, the dream exactly as it was. One macroblock did not refresh.
    "Recovery": stage(dissip=9.0, keyframe=1.0, heal=1.0),
}
LADDER = ["Dream", "Uncanny", "Infection", "Corruption", "Nightmare", "Collapse"]
DEPTH_AT = {"Uncanny": 0.08, "Infection": 0.28, "Corruption": 0.48, "Nightmare": 0.66, "Collapse": 0.86}


def sun_azimuth(yaw_deg):
    """The key's azimuth (Composition::lightAngles: atan2 of the to-source vector's x, z) turned by yaw, so the
    shadows swing while the sky's sun -- authored separately, useKeyLight off -- stays where it was."""
    return math.degrees(math.atan2(-SUN_DIR[0], -SUN_DIR[2])) + yaw_deg


def program_op(prog_fn, kind):
    """The 1-based index of the first op of `kind` in a program (material/<prog>/op/<i>/<kind>/...)."""
    for k, o in enumerate(prog_fn()["ops"], start=1):
        if o["kind"] == kind:
            return k
    raise KeyError(kind)


def presets():
    _segs, bases = tree_skeleton()
    hue_op = program_op(plain_program, "hueShift")
    out = []
    for name, v in STAGES.items():
        vals = {
            "camera/distance": [v["dist"]], "camera/height": [v["height"]], "camera/fov": [v["fov"]],
            "camera/orbitPivot": v["pivot"], "camera/orbitSpeed": [v["orbit"]], "camera/roll": [v["roll"]],
            "field/infect/strength": [v["inject"]], "grid/contagion/advect": [v["advect"]],
            "grid/contagion/dissipation": [v["dissip"]], "field/climb/position": [0.0, v["climb"], 0.0],
            "sdf/tree/node/eaten/amount": [v["eatTree"]], "sdf/stone/node/eaten/amount": [v["eatStone"]],
            "sdf/tree/node/bark/amount": [v["bark"]], "sdf/tree/node/bark/speed": [v["barkSpeed"]],
            "lights/fracture/intensity": [v["fracture"]], "lights/sun/azimuth": [sun_azimuth(v["sunYaw"])],
            "lights/sun/color": v["sunColor"],
            "material/plain/emissionIntensity": [v["glowPlain"]], "material/bark/emissionIntensity": [v["glowBark"]],
            "material/skin/emissionIntensity": [v["glowBark"]],
            "material/blocks/emissionIntensity": [v["glowBlocks"]],
            f"material/plain/op/{hue_op}/hueShift/value": [v["stainHue"]],
            "nodes/double/position": DOUBLE_AT if v["double"] else HIDDEN,
            "particles/motes/spawnRate": [v["motes"]],
            "temporal/echo/strength": [v["echo"]], "temporal/mosh/amount": [v["mosh"]],
            "temporal/mosh/block": [v["moshBlock"]], "post/glitch/amount": [v["glitch"]],
            "post/display/pixelate": [v["pixel"]], "post/display/posterize": [v["poster"]],
            "post/sort/amount": [v["sort"]], "post/split/amount": [v["split"]],
            "camera/exposure/compensation": [v["exposure"]], "post/bloom/intensity": [v["bloom"]],
            "scene/volumeDensity": [v["volDen"]], "scene/fogColor": v["fogTint"],
            "post/grade/temperature": [-0.12 * v["grade"]], "post/grade/tint": [-0.08 * v["grade"]],
            "macros/gMicro": [v["gMicro"]], "macros/gRhythm": [v["gRhythm"]], "macros/gMosh": [v["gMosh"]],
            "macros/gGlitch": [v["gGlitch"]], "macros/gMelt": [v["gMelt"]], "macros/gLift": [v["gLift"]],
            "macros/keyframe": [v["keyframe"]], "macros/heal": [v["heal"]],
        }
        for li, (base, _ld) in enumerate(bases):
            vals[f"sdf/tree/node/melt{li}/amount"] = [v["melt"][li]]
            vals[f"sdf/tree/node/limb{li}/translation"] = [base[0] + 0.15 * v["lift"][li], base[1] + v["lift"][li],
                                                           base[2]]
        for blocks in ("treeBlocks", "stoneBlocks"):
            vals[f"procedural/{blocks}/effector/2/strength"] = [v["rise"]]
            vals[f"procedural/{blocks}/effector/3/strength"] = [v["tumble"]]
            vals[f"procedural/{blocks}/effector/4/strength"] = [v["spin"]]
            vals[f"procedural/{blocks}/effector/5/strength"] = [v["kick"]]
        if name == "Recovery":
            vals["field/stuck/strength"] = [1.0]  # set here only: it persists for the rest of the piece
        out.append({"name": name.lower(), "values": vals})
    return out


def states():
    def held(signal, threshold, frm, falling=False):
        t = {"kind": "signal", "signal": signal, "threshold": threshold, "from": frm, "hold": True}
        if falling:
            t["falling"] = True
        return t

    def after(seconds, frm):
        return {"kind": "elapsed", "threshold": seconds, "from": frm}

    S = []
    for k, name in enumerate(LADDER):
        trig = []
        if k > 0:
            # the ladder (ADR-1164 hold: a stage whose exit is already exceeded on entry still leaves)
            trig.append(held("visual.depth", DEPTH_AT[name], LADDER[k - 1]))
        if name == "Uncanny":
            trig.append(held("visual.lift", 0.55, "Respite"))  # the music comes back after a breakdown
        if name == "Dream":
            trig.append(after(20.0, "Recovery"))               # the keyframe holds, then the dream simply goes on
        tr = {"Dream": (8, "smooth"), "Uncanny": (12, "smooth"), "Infection": (6, "smooth"),
              "Corruption": (3, "easeIn"), "Nightmare": (2, "easeIn"), "Collapse": (4, "easeIn")}[name]
        S.append({"name": name, "preset": name.lower(), "transition": {"seconds": tr[0], "easing": tr[1]},
                  "triggers": trig})
    # the collapse decomposes on the bar grid: geometry -> particles/temporal -> pixels/colour -> light
    S.append({"name": "Decay", "preset": "decay", "transition": {"seconds": 3, "easing": "easeIn", "quantize": "bar"},
              "triggers": [after(6.0, "Collapse")]})
    S.append({"name": "Pixels", "preset": "pixels", "transition": {"seconds": 2, "easing": "easeIn",
                                                                    "quantize": "bar"},
              "triggers": [after(5.0, "Decay")]})
    S.append({"name": "Light", "preset": "light", "transition": {"seconds": 3, "easing": "easeIn", "quantize": "bar"},
              "triggers": [after(4.0, "Pixels")]})
    S.append({"name": "Respite", "preset": "respite", "transition": {"seconds": 1.5, "easing": "smooth"},
              "triggers": [held("visual.lift", 0.28, f, falling=True) for f in ("Infection", "Corruption",
                                                                                   "Nightmare")]})
    # the keyframe: an instant cut, from white to the calm dream (brief §15: "everything suddenly becomes calm")
    S.append({"name": "Recovery", "preset": "recovery", "transition": {"seconds": 0.0, "easing": "linear"},
              "triggers": [after(3.5, "Light")]})
    return {"initial": "Dream", "states": S}


# ------------------------------------------------------------------------------------------------ the listening
# LAYER 4, the arc (docs/prototypes/digital-mosh/03-implementation.md, "The arc"; tuned on both tracks by
# simulation, tools-free): drive -> energy (fast follower) and baseline (30 s follower) -> lift (relative energy) ->
# pace -> dose (a bounded integral, ADR-1161) -> depth (dose, masked by the keyframe) -> the stage ladder.
LISTEN = {"kind": "interpret", "name": "listen", "settings": {"mappings": [
    # loudness-independent energy (ADR-897) and the air: what "how much is happening" is, for any master
    {"name": "drive", "combine": "mean", "inputs": [
        {"signal": "audio.energy", "weight": 1.0}, {"signal": "audio.trebleLevel", "weight": 1.0}],
     "bias": -0.95, "gain": 3.0, "curve": 1.0},
    # timbre, slowly: dark sound melts, bright sound pixelates and turns the strain toward chartreuse
    {"name": "bright", "combine": "mean", "inputs": [{"signal": "sonic.brightness.slow", "weight": 1.0}],
     "bias": -0.25, "gain": 2.0, "curve": 1.0},
]}}
ARC = {"kind": "interpret", "name": "arc", "settings": {"mappings": [
    # lift: the music relative to its own last half-minute (0.5 = as usual; low = a breakdown)
    {"name": "lift", "combine": "sum", "inputs": [
        {"signal": "macro.energy", "weight": 1.0}, {"signal": "macro.baseline", "weight": 1.0, "invert": True}],
     "bias": -2.5, "gain": 3.0, "curve": 1.0},
    # pace: how fast the dream decays now. A respite (heal 0.45) slows it to healing; the keyframe (heal 2) drains it.
    {"name": "pace", "combine": "sum", "inputs": [
        {"signal": "macro.energy", "weight": 0.3}, {"signal": "visual.lift", "weight": 0.8},
        # weights are >= 0 (parameters), so heal enters inverted: + (1 - heal), and the bias pays the 1 back
        {"signal": "macro.heal", "weight": 1.0, "invert": True}], "bias": -1.05, "gain": 1.0, "curve": 1.0},
    # depth: what the ladder reads -- the dose, masked by the keyframe
    {"name": "depth", "combine": "product", "inputs": [
        {"signal": "macro.dose", "weight": 1.0}, {"signal": "macro.keyframe", "weight": 1.0, "invert": True}],
     "bias": 0.0, "gain": 1.0, "curve": 1.0},
]}}

GATES = ["gMicro", "gRhythm", "gMosh", "gGlitch", "gMelt", "gLift"]


def macros():
    m = [{"name": "energy", "label": "ENERGY", "default": 0.0, "targets": []},
         {"name": "baseline", "label": "BASELINE", "default": 0.0, "targets": []},
         {"name": "dose", "label": "DOSE", "default": 0.0, "targets": []},
         {"name": "keyframe", "label": "KEYFRAME", "default": 0.0, "targets": []},
         {"name": "heal", "label": "HEAL", "default": 0.0, "targets": []},
         {"name": "sensitivity", "label": "SENSITIVITY", "default": 0.25, "targets": []}]
    m += [{"name": g, "label": g.upper(), "default": BASE[g], "targets": []} for g in GATES]
    return m


def gated(source, target, amount, gate, chain=None, component=None):
    r = {"source": source, "target": target, "op": "add", "amount": amount, "depthSource": f"macro.{gate}",
         "depthMin": 0.0, "depthMax": 1.0}
    if chain:
        r["chain"] = chain
    if component is not None:
        r["component"] = component
    return r


PEAK = lambda hold, fall: {"envelope": "peakhold", "envelopeHoldMs": hold, "envelopeFallPerSecond": fall}


def routes():
    hue_op = program_op(plain_program, "hueShift")
    r = list(ZERO_DEFAULTS)
    # ---- LAYER 4: the arc
    r.append({"source": "visual.drive", "target": "macros/energy", "op": "add", "amount": 1.0,
              "depthSource": "macro.sensitivity", "depthMin": 0.0, "depthMax": 4.0,
              "chain": {"attackMs": 1500, "decayMs": 3000}})
    r.append({"source": "visual.drive", "target": "macros/baseline", "op": "add", "amount": 1.0,
              "depthSource": "macro.sensitivity", "depthMin": 0.0, "depthMax": 4.0,
              "chain": {"attackMs": 30000, "decayMs": 30000}})
    # dose rate = -0.05 + 0.0605 * pace^0.25 per second: the keyframe drains it in ~20 s, a breakdown heals it, ordinary
    # music advances it (~3 min from Dream to Collapse), intense music faster (ADR-1161 keeps it in [0, 1])
    r.append({"source": "visual.pace", "target": "macros/dose", "op": "add", "amount": 1.0,
              "chain": {"curve": "power", "curveAmount": 0.25, "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0,
                        "remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": -0.05,
                        "remapOutMax": 0.0105, "integrate": True, "integrateMin": 0.0, "integrateMax": 1.0}})
    # ---- LAYER 1, micro (ms): high frequencies are fine detail -- dust glints, the infected cells shimmer
    r.append(gated("audio.treble", "particles/motes/spawnRate", 260.0, "gMicro", {"attackMs": 40, "decayMs": 600}))
    r.append(gated("audio.onsetHigh", "material/plain/emissionIntensity", 3.0, "gRhythm", PEAK(20, 6.0)))
    r.append(gated("audio.onsetHigh", "material/blocks/emissionIntensity", 4.0, "gRhythm", PEAK(20, 6.0)))
    r.append(gated("audio.trebleLevel", "post/glitch/amount", 0.05, "gGlitch", {"attackMs": 30, "decayMs": 300}))
    # ---- LAYER 2, rhythmic (beat): the kick fractures, the snare spreads
    r.append(gated("audio.onsetLow", "lights/fracture/intensity", 2500.0, "gRhythm", PEAK(30, 5.0)))
    r.append(gated("audio.onsetLow", "procedural/treeBlocks/effector/5/strength", 0.6, "gRhythm", PEAK(40, 4.0)))
    r.append(gated("audio.onsetLow", "procedural/stoneBlocks/effector/5/strength", 0.6, "gRhythm", PEAK(40, 4.0)))
    r.append(gated("audio.onsetMid", "field/infect/strength", 4.0, "gRhythm", PEAK(60, 3.0)))
    r.append(gated("audio.onsetMid", "temporal/mosh/amount", 0.28, "gMosh", PEAK(90, 3.5)))
    r.append(gated("audio.onsetMid", "post/glitch/tear", 0.35, "gGlitch", PEAK(50, 6.0)))
    r.append(gated("audio.onsetLow", "post/split/amount", 6.0, "gGlitch", PEAK(15, 12.0)))
    # ---- LAYER 3, musical (phrase): bass is mass and gravity -- the limbs sag, the bark flows, the haze breathes
    for li in range(3):
        r.append(gated("audio.bassLevel", f"sdf/tree/node/melt{li}/amount", 0.10 if li < 2 else -0.08, "gMelt",
                       {"attackMs": 400, "decayMs": 1200}))
        r.append(gated("audio.bass", f"sdf/tree/node/limb{li}/translation", 0.6, "gLift",
                       {"attackMs": 300, "decayMs": 1500}, component=1))
    r.append(gated("audio.bassLevel", "sdf/tree/node/bark/amount", 0.04, "gMelt", {"attackMs": 200, "decayMs": 900}))
    r.append(gated("macro.energy", "scene/volumeDensity", 0.0012, "gRhythm", {"attackMs": 2000, "decayMs": 4000}))
    # timbre turns the strain: bright music -> chartreuse (a zeroed chroma), dark -> magenta (overflowing chroma)
    r.append(gated("visual.bright", f"material/plain/op/{hue_op}/hueShift/value", 0.32, "gRhythm",
                   {"attackMs": 3000, "decayMs": 3000}))
    r.append(gated("visual.bright", "grid/contagion/advect", 1.5, "gRhythm", {"attackMs": 2000, "decayMs": 2000}))
    # ---- the camera: a slow, incommensurate drift (dolly and crane) and a slow turn of the double toward the viewer
    r.append({"source": "lfo.driftA.bipolar", "target": "camera/height", "op": "add", "amount": 0.3})
    r.append({"source": "lfo.driftB.bipolar", "target": "camera/distance", "op": "add", "amount": 1.6})
    return r


def project(name, audio, live=False, sensitivity=None):
    p = {
        "format": "avgen-project", "version": 4,
        "app": {"name": name},
        "assets": {"scene": {"kind": "composition", "path": "digital-mosh.scene.json"}},
        "live": {"qualityStrategy": "effects_first", "targetFps": 60},
        "parameters": dict(POST),
        "sources": [LISTEN, ARC, {"kind": "lfo", "name": "driftA", "settings": {"shape": "sine"}},
                    {"kind": "lfo", "name": "driftB", "settings": {"shape": "sine"}},
                    {"kind": "lfo", "name": "turn", "settings": {"shape": "sine"}}],
        # the Sonic runtime: sonic.brightness.slow and the response.* hits (and live input in the live project)
        "sonic": {},
        "routes": routes(),
        "worldMacros": macros(),
        "presets": presets(),
        "states": states(),
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/digital-mosh.mp4"},
    }
    if audio:
        p["assets"]["audio"] = {"path": audio}
    if sensitivity is not None:
        p["parameters"]["macros/sensitivity"] = sensitivity
    if live:
        p["sonic"] = {"live": True}
    return p


def main():
    (HERE / "digital-mosh.scene.json").write_text(json.dumps(scene(), indent=1) + "\n")
    projects = {
        "digital-mosh.json": project("DIGITAL MOSH", "../../assets/audio/feline-footwear.wav"),
        "digital-mosh-trench.json": project("DIGITAL MOSH / Trench", "../../assets/audio/trench.wav"),
        "digital-mosh-live.json": project("DIGITAL MOSH LIVE", None, live=True),
    }
    for n, p in projects.items():
        (HERE / n).write_text(json.dumps(p, indent=1) + "\n")
    print("wrote", HERE / "digital-mosh.scene.json", *projects,
          f"(tree blocks {len(tree_blocks())}, stone blocks {len(stone_blocks())})")


if __name__ == "__main__":
    main()

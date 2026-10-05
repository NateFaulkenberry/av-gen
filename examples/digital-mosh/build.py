#!/usr/bin/env python3
"""DIGITAL MOSH: the source of truth for the scene and its projects.

    python3 examples/digital-mosh/build.py

Writes digital-mosh.scene.json (the world) and the projects beside it. Edit this file, not the JSON.
Design: docs/prototypes/digital-mosh/02-design.md.
"""
from __future__ import annotations

import json
import math
import random
from pathlib import Path

HERE = Path(__file__).resolve().parent

# ============================================================================ geometry helpers


def euler_from_matrix(R):
    """Degrees (x, y, z) such that glm::quat(radians(e)) == R, with R = Rz * Ry * Rx."""
    y = math.asin(max(-1.0, min(1.0, -R[2][0])))
    x = math.atan2(R[2][1], R[2][2])
    z = math.atan2(R[1][0], R[0][0])
    return [math.degrees(x), math.degrees(y), math.degrees(z)]


def rot_y_to(d):
    """A rotation matrix taking +Y onto the unit vector d."""
    dx, dy, dz = d
    # axis = Y x d, angle = acos(dy)
    ax, ay, az = dz, 0.0, -dx
    s = math.sqrt(ax * ax + az * az)
    c = dy
    if s < 1e-9:
        return [[1, 0, 0], [0, 1 if c > 0 else -1, 0], [0, 0, 1 if c > 0 else -1]]
    ax, az = ax / s, az / s
    t = 1 - c
    return [[t * ax * ax + c, -s * az, t * ax * az],
            [s * az, c, -s * ax],
            [t * ax * az, s * ax, t * az * az + c]]


def norm(v):
    m = math.sqrt(sum(x * x for x in v))
    return [x / m for x in v]


def sdf(kind, children=None, name=None, **kw):
    n = {"kind": kind}
    n.update(kw)
    if children is not None:
        n["children"] = children
    if name:
        n["name"] = name
    return n


def capsule_between(a, b, r, name=None):
    d = [b[i] - a[i] for i in range(3)]
    L = math.sqrt(sum(x * x for x in d))
    mid = [(a[i] + b[i]) / 2 for i in range(3)]
    e = euler_from_matrix(rot_y_to(norm(d)))
    return sdf("translate", [sdf("rotate", [sdf("capsule", radius=r, height=L)], rotation=e)],
               translation=mid, name=name)


# ============================================================================ the tree
# One bare tree, the protagonist (brief §4). A dead-olive gesture: a short leaning trunk that twists into three
# limbs, one of them long and nearly horizontal (the limb the dream will soften first). Built from a seeded
# skeleton so the fragments (later passes) can be placed along the same branches.

TREE_SEED = 7


def tree_skeleton():
    rnd = random.Random(TREE_SEED)
    segs = []  # (a, b, r0, r1, level, limb)

    def grow(a, d, L, r, level, limb, nseg=2):
        p = a
        for s in range(nseg):
            # each segment bends a little: gnarled, not wobbly
            jitter = [rnd.uniform(-0.18, 0.18), rnd.uniform(-0.05, 0.12), rnd.uniform(-0.18, 0.18)]
            d = norm([d[i] + jitter[i] for i in range(3)])
            q = [p[i] + d[i] * L / nseg for i in range(3)]
            r1 = r * (0.78 if s < nseg - 1 else 0.62)
            segs.append((p, q, r, r1, level, limb))
            p, r = q, r1
        return p, d, r

    # trunk: leans toward +x, twists back
    top, d, r = grow([0, -0.2, 0], norm([0.22, 1, 0.08]), 3.1, 0.42, 0, -1, nseg=3)
    limbs = [
        (norm([1.0, 0.18, 0.15]), 4.6),    # the long near-horizontal limb (the dream's first casualty)
        (norm([-0.55, 0.85, 0.2]), 3.0),
        (norm([0.1, 0.9, -0.6]), 2.6),
    ]
    for li, (ld, LL) in enumerate(limbs):
        end, dd, rr = grow(top, ld, LL, r * 0.82, 1, li, nseg=2)
        for k in range(2 if li == 0 else 2):
            side = [rnd.uniform(-0.8, 0.8), rnd.uniform(0.3, 0.9), rnd.uniform(-0.8, 0.8)]
            sd = norm([dd[i] * 0.6 + side[i] for i in range(3)])
            # secondary branches leave from along the limb, not only its tip
            t = 0.55 + 0.4 * k
            a = [top[i] + (end[i] - top[i]) * t for i in range(3)]
            e2, d2, r2 = grow(a, sd, LL * 0.45, rr * 0.9, 2, li, nseg=1)
            twig = norm([d2[0] + rnd.uniform(-0.6, 0.6), d2[1] + 0.3, d2[2] + rnd.uniform(-0.6, 0.6)])
            grow(e2, twig, LL * 0.22, r2 * 0.8, 3, li, nseg=1)
    return segs


def tree_sdf_tree():
    segs = tree_skeleton()
    groups = {-1: [], 0: [], 1: [], 2: []}
    for (a, b, r0, r1, level, limb) in segs:
        groups[limb].append(capsule_between(a, b, (r0 + r1) / 2))

    def chunked_union(items, k):
        # smoothUnion takes at most 8 children
        out = []
        for i in range(0, len(items), 8):
            out.append(sdf("smoothUnion", items[i:i + 8], smooth=k) if len(items[i:i + 8]) > 1 else items[i])
        return out[0] if len(out) == 1 else sdf("smoothUnion", out, smooth=k)

    trunk = chunked_union(groups[-1], 0.25)
    limbs = [sdf("translate", [chunked_union(groups[i], 0.12)], translation=[0, 0, 0], name=f"limb{i}")
             for i in range(3)]
    body = sdf("smoothUnion", [trunk] + limbs, smooth=0.22)
    # bark: a fine, slow noise -- the precision is the point, so it is small
    return {"root": sdf("displaceNoise", [body], amount=0.018, frequency=7.0, speed=0.0, seed=3, name="bark")}


# ============================================================================ the stone
# A Tanguy pebble: smooth, singular, of no determinable size. It hovers a hand's width above its own shadow.


def stone_sdf_tree():
    blob = sdf("smoothUnion", [
        sdf("sphere", radius=1.0),
        sdf("translate", [sdf("sphere", radius=0.72)], translation=[0.75, -0.18, 0.2]),
        sdf("translate", [sdf("sphere", radius=0.55)], translation=[-0.7, -0.3, -0.25]),
    ], smooth=0.6)
    squash = sdf("scale", [blob], scale=1.0)
    return {"root": squash}


# ============================================================================ palette (linear)
DREAM = {
    "zenith": [0.30, 0.46, 0.68],
    "horizon": [0.98, 0.80, 0.62],
    "ground_sky": [0.55, 0.42, 0.30],
    "sun": [1.0, 0.82, 0.62],
    "ground": [0.52, 0.38, 0.22],
    "bark": [0.20, 0.15, 0.12],
    "stone": [0.72, 0.66, 0.60],
}


def scene():
    # the direction the light travels: low (11 deg), from behind the tree and to the left, so every shadow
    # runs long toward the viewer and to the right (de Chirico's late afternoon)
    sun_dir = norm([0.42, -0.2, 0.88])
    nodes = [
        {"name": "plain", "kind": "procedural", "procedural": {
            "source": {"kind": "box", "size": [6000.0, 0.02, 6000.0], "subdivisions": 1},
            "sourceTransform": {"position": [0, -0.01, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
            "distribution": {"kind": "single"},
            "lod": {"cull": False, "count": 1},
            "material": {"baseColor": DREAM["ground"], "roughness": 0.95, "metallic": 0.0}}},
        {"name": "tree", "kind": "sdf", "position": [-6.0, 0, 0], "sdf": {
            "tree": tree_sdf_tree(),
            "material": {"baseColor": DREAM["bark"], "roughness": 0.8, "metallic": 0.0},
            "renderMode": "raymarch", "boundsMin": [-5, -0.5, -5], "boundsMax": [9, 9, 5],
            "compile": True, "maxSteps": 128, "stepScale": 0.85, "epsilon": 0.001, "normalEpsilon": 0.003,
            "maxDistance": 400.0, "castShadows": True, "depthPrepass": True,
            "look": {"aoStrength": 0.6, "aoDistance": 0.6}}},
        {"name": "stone", "kind": "sdf", "position": [14.0, 1.25, -18.0], "sdf": {
            "tree": stone_sdf_tree(),
            "material": {"baseColor": DREAM["stone"], "roughness": 0.45, "metallic": 0.0},
            "renderMode": "raymarch", "boundsMin": [-2, -1.6, -1.6], "boundsMax": [2, 1.4, 1.6],
            "compile": True, "maxSteps": 96, "stepScale": 0.9, "maxDistance": 400.0,
            "castShadows": True, "depthPrepass": True, "look": {"aoStrength": 0.4}}},
    ]
    return {
        "format": "avgen-scene", "version": 1, "name": "DIGITAL MOSH",
        "_note": "Generated by build.py; edit that, not this file.",
        "camera": {"mode": 1, "position": [3.0, 1.35, 24.0], "target": [-1.5, 4.2, 0.0], "fov": 36},
        "environment": {
            "intensity": 0.3, "background": DREAM["horizon"], "fogColor": DREAM["horizon"],
            "shadowRange": 140.0, "shadowCascades": 3,
            # aerial perspective: a thin, low medium that takes the sky's colour with distance, so the plain
            # never ends -- it dissolves into the sky (Tanguy). No edge, no line, no black.
            "volumeDensity": 0.0022, "volumeScattering": 0.9, "volumeAbsorption": 0.1, "volumeAnisotropy": 0.55,
            "volumeSteps": 24, "volumeMaxDistance": 3000.0, "fogHeight": 0.0, "fogHeightFalloff": 0.035,
            "fogSky": 1.0, "fogSkyDistance": 900.0,
            "sky": {"enabled": True, "background": True, "useKeyLight": True,
                    "zenithColor": DREAM["zenith"], "horizonColor": DREAM["horizon"],
                    "groundColor": DREAM["ground_sky"], "haze": 0.25, "sunIntensity": 1.0, "sunSize": 0.5,
                    "sunGlow": 0.35, "intensity": 1.0},
        },
        "lights": [
            {"name": "sun", "id": "sun", "type": "directional", "role": "key", "direction": sun_dir,
             "color": DREAM["sun"], "intensity": 7.0, "castsShadow": True, "shadowStrength": 1.0, "softness": 0.3},
        ],
        "nodes": nodes,
    }


ZERO_DEFAULTS = [
    {"source": "audio.bass", "target": "root/scale", "op": "add", "amount": 0.0},
    {"source": "audio.mid", "target": "root/rotationSpeed", "op": "add", "amount": 0.0},
    {"source": "audio.rms", "target": "scene/brightness", "op": "add", "amount": 0.0},
    {"source": "audio.onset", "target": "root/impulse", "op": "add", "amount": 0.0},
]


def project(name, audio, live=False):
    p = {
        "format": "avgen-project", "version": 4,
        "app": {"name": name},
        "assets": {"scene": {"kind": "composition", "path": "digital-mosh.scene.json"}},
        "live": {"qualityStrategy": "balanced", "targetFps": 60},
        "parameters": {
            "post/bloom/enabled": True, "post/bloom/intensity": 0.08, "post/bloom/threshold": 1.2,
            "post/tonemap/chroma-retention": 0.5, "post/output/vignette": 0.25,
            "camera/exposure/mode": 0,
        },
        "routes": list(ZERO_DEFAULTS),
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/digital-mosh.mp4"},
    }
    if audio:
        p["assets"]["audio"] = {"path": audio}
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
    for name, p in projects.items():
        (HERE / name).write_text(json.dumps(p, indent=1) + "\n")
    print("wrote", HERE / "digital-mosh.scene.json", *projects)


if __name__ == "__main__":
    main()

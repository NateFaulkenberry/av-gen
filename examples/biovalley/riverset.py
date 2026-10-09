"""The riverbank's authored set (spec 07 Deliverable B): the artist's fall set at fall3's foot, shoreline shelves and
waterline rocks along the near reach, and the violet-magenta light the falls give the water.

The fall set shares one frame (03-hero-assets.md, "Environment pieces"): origin at the plunge point at water level,
GLB +X toward the riverbank camera, no rotation. The procedural fall3 strands it replaces are dropped by valley.py.
"""
from __future__ import annotations

import math

import numpy as np

from populate import rec

ENV = "meshes/env"

# the falls' own light: violet-magenta (spec 07 §4.3: violet reflection #7350D8, magenta accent #D34BDB)
FALL_GLOW = [0.85, 0.72, 1.25]   # violet-WHITE: bright streaky water catching light, not hot pink
FALL_GLOW_INTENSITY = 1.3


def env_node(name, asset, pts, maxd=600.0, material=None, lod=0):
    proc = {"source": {"kind": "mesh", "asset": f"../{ENV}/{asset}_lod{lod}.glb"},
            "distribution": {"kind": "points", "points": pts}, "castsShadow": False,
            "lod": {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": maxd}}
    if material:
        proc["material"] = material
    return {"name": name, "kind": "procedural", "procedural": proc}


def fall_set(foot, water_level):
    """The fall set at `foot` (x, z of fall3's foot); the origin at the water's surface."""
    x, z = float(foot[0]), float(foot[2])
    at = [[round(x, 2), round(water_level, 3), round(z, 2), 0, 0, 0, 1, 1, 1, 1]]
    glow = {"baseColor": [1.0, 1.0, 1.0], "roughness": 0.25, "metallic": 0.0,
            "emissiveColor": FALL_GLOW, "emissiveIntensity": FALL_GLOW_INTENSITY}
    foam = {"baseColor": [1.0, 1.0, 1.0], "roughness": 0.6, "metallic": 0.0,
            "emissiveColor": [0.9, 0.5, 1.2], "emissiveIntensity": 0.8}
    return [env_node("rs_fall_rock", "env_fall_rock", at),
            env_node("rs_fall_pool", "env_fall_pool", at),
            env_node("rs_fall_water", "env_fall_water", at, material=glow),
            env_node("rs_fall_foam", "env_fall_foam", at, material=foam)]


def fall_lights(foot, water_level):
    """Point lights at the plunge: the falls' violet on the pool, the rocks and (through the frame copy) the water."""
    x, z = float(foot[0]), float(foot[2])
    return [{"name": "fallGlow", "type": "point", "position": [round(x + 3.0, 2), round(water_level + 4.0, 2),
                                                              round(z, 2)],
             "color": [0.75, 0.35, 1.0], "intensity": 60.0, "range": 45.0, "radius": 3.0, "castsShadow": False},
            {"name": "fallGlowHigh", "type": "point", "position": [round(x + 2.0, 2), round(water_level + 18.0, 2),
                                                                  round(z, 2)],
             "color": [0.6, 0.35, 1.0], "intensity": 30.0, "range": 35.0, "radius": 4.0, "castsShadow": False}]


def shoreline(g, eye, head, water_level, rng_seed=73):
    """Shelves and waterline rocks along both banks of the reach the camera looks up (10-70 m), where the bank meets
    the water; half-sunk so they sit in it. -> list of nodes."""
    r = np.random.default_rng(rng_seed)
    ex, ez = eye[0], eye[2]
    fx, fz = math.sin(head), math.cos(head)
    lx, lz = fz, -fx
    groups = {"env_shelf_a": [], "env_shelf_b": [], "env_waterrock_a": [], "env_waterrock_b": [],
              "env_waterrock_c": []}
    for d in np.arange(16.0, 75.0, 2.2):
        for side in (1.0, -1.0):
            # walk out from the stream's middle to the first dry ground on this side
            edge = None
            for s in np.arange(0.0, 40.0, 0.25):
                x, z = ex + fx * d + lx * s * side, ez + fz * d + lz * s * side
                if not g.wet(np.array([x]), np.array([z]))[0]:
                    edge = (x, z, s)
                    break
            if edge is None or r.random() < 0.35:
                continue
            x, z, s = edge
            back = 0.6 if r.random() < 0.5 else -0.4      # a little into the water, or onto the bank
            x, z = x - lx * side * back, z - lz * side * back
            u = r.random()
            if u < 0.18:
                sp, sc = ("env_shelf_a" if r.random() < 0.5 else "env_shelf_b"), r.uniform(0.6, 0.9)
            elif u < 0.45:
                sp, sc = "env_waterrock_a", r.uniform(0.8, 1.6)
            elif u < 0.75:
                sp, sc = "env_waterrock_b", r.uniform(0.8, 1.6)
            else:
                sp, sc = "env_waterrock_c", r.uniform(0.8, 1.8)
            # shelves lie along the bank (their long axis is GLB X): turn X along the flow
            yaw = (head - math.pi / 2 if sp.startswith("env_shelf") else r.uniform(0, 6.28)) + r.uniform(-0.3, 0.3)
            groups[sp].append(rec(x, water_level - (0.12 if sp.startswith("env_shelf") else 0.08) * sc, z, yaw, sc))
    return [env_node(f"rs_{sp}", sp, pts, maxd=200.0) for sp, pts in groups.items() if pts]

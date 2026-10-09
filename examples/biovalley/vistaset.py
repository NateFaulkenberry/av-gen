"""The vista's authored set (spec 07 Deliverable C): the lobe clouds (ADR-1289), the dimensional moon lit by its own
sun, the karst landmarks, and the dark near canopy. Placement is in the vista world frame (valley.py's)."""
from __future__ import annotations

import math

import numpy as np

from populate import rec

ENV = "meshes/env"

MOON_DIST = 20000.0
MOON_ANG = math.radians(2.8)      # angular radius, as the old crescent


def env_node(name, asset, pts, maxd=30000.0, material=None, lod=0, shadow=False):
    proc = {"source": {"kind": "mesh", "asset": f"../{ENV}/{asset}_lod{lod}.glb"},
            "distribution": {"kind": "points", "points": pts}, "castsShadow": shadow,
            "lod": {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": maxd}}
    if material:
        proc["material"] = material
    return {"name": name, "kind": "procedural", "procedural": proc}


def _basis_facing(forward):
    z = np.asarray(forward, float)
    z /= np.linalg.norm(z)
    x = np.cross([0.0, 1.0, 0.0], z)
    x /= np.linalg.norm(x)
    y = np.cross(z, x)
    return x, y, z


def moon(eye, moon_dir, quat, sun_side=(0.75, -0.15, -0.65), intensity=4.0e7):
    """The sphere at the old crescent's place, its designed face (+Z) toward the eye, and a point light on its sun side
    whose range ends well before anything else in the world: the crescent is the terminator, not a cut shape.
    `sun_side` is (right, up, toward-camera) of the moon's own frame (negative toward = behind, so a crescent)."""
    rad = MOON_DIST * math.tan(MOON_ANG)
    c = np.asarray(eye, float) + np.asarray(moon_dir, float) * MOON_DIST
    x, y, z = _basis_facing(-np.asarray(moon_dir, float))
    q = quat(np.stack([x, y, z], 1))
    s = np.asarray(sun_side, float)
    sun = x * s[0] + y * s[1] + z * s[2]
    sun /= np.linalg.norm(sun)
    node = env_node("moon", "env_moon", [[*np.round(c, 1).tolist(), *q, rad, rad, rad]], maxd=40000.0,
                    material={"baseColor": [1.0, 1.0, 1.0], "roughness": 1.0, "metallic": 0.0,
                              "emissiveColor": [0.0, 0.0, 0.0], "emissiveIntensity": 0.0})
    light = {"name": "moonSun", "type": "point", "position": np.round(c + sun * rad * 3.0, 1).tolist(),
             "color": [0.92, 0.95, 1.0], "intensity": intensity, "range": rad * 4.2, "radius": rad * 0.2,
             "castsShadow": False}
    return node, light


def clouds_block(moon_dir, glow_dir, lobes="../meshes/env/env_vista_clouds.lobes.json"):
    return {"lobes": lobes, "extinction": 0.004, "steps": 128, "lightSteps": 6, "lightDistance": 500.0,
            "lightDirection": [round(float(v), 5) for v in moon_dir], "lightColor": [0.6, 0.68, 1.0],
            "lightIntensity": 1.6, "anisotropy": 0.6,
            "ambientTop": [0.035, 0.05, 0.19], "ambientBottom": [0.012, 0.012, 0.055],
            "warmColor": [0.55, 0.22, 0.24], "warmDirection": [round(float(v), 5) for v in glow_dir],
            "warmHeight": 1600.0, "noiseScale": 0.006, "noiseAmount": 0.5,
            "hazeColor": [0.03, 0.045, 0.2], "hazeDistance": 26000.0, "powder": 0.6, "seed": 7}


def karst(gc, eye_xz, yaw0_deg, specs):
    """specs: (asset, bearing deg (+ = screen left), distance m, scale, yaw deg). Stood on the coarse ground, sunk 4%."""
    ex, ez = eye_xz
    nodes = []
    for k, (asset, bearing, dist, sc, yaw) in enumerate(specs):
        b = math.radians(yaw0_deg + bearing)
        x, z = ex + dist * math.sin(b), ez + dist * math.cos(b)
        y = float(gc.height(np.array([x]), np.array([z]))[0])
        nodes.append(env_node(f"vs_{asset}_{k}", asset, [rec(x, y - 0.04 * 600 * sc, z, math.radians(yaw), sc)],
                              lod=0))
    return nodes


def canopy(gc, eye, yaw0_deg, n=26, seed=91):
    """Dark designed crowns down the slope below the lookout, filling the bottom and the left of the frame (the
    reference's near canopy): near enough to be silhouettes against the lit valley, varied habits and scales."""
    r = np.random.default_rng(seed)
    ex, ey, ez = eye
    groups = {"env_canopy_a": [], "env_canopy_b": [], "env_canopy_c": []}
    for k in range(n):
        bearing = r.uniform(-34.0, 46.0)
        dist = r.uniform(60.0, 260.0)
        b = math.radians(yaw0_deg + bearing)
        x, z = ex + dist * math.sin(b), ez + dist * math.cos(b)
        y = float(gc.height(np.array([x]), np.array([z]))[0])
        sp = ["env_canopy_a", "env_canopy_a", "env_canopy_b", "env_canopy_c"][int(r.integers(0, 4))]
        groups[sp].append(rec(x, y - 1.0, z, r.uniform(0, 6.28), r.uniform(0.9, 1.5)))
    dark = {"baseColor": [0.55, 0.6, 0.75], "roughness": 1.0, "metallic": 0.0, "emissiveColor": [1.0, 1.0, 1.0],
            "emissiveIntensity": 0.6}
    return [env_node(f"vs_{sp}", sp, pts, maxd=3000.0, material=dark) for sp, pts in groups.items() if pts]

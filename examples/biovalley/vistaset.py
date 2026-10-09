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


def moon(eye, moon_dir, quat, sun_side=(0.8, -0.2, -0.55)):
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
    # A local light cannot reach 20 km (the clustered lights end long before), so the phase is shaded by a material
    # program instead: emission = the sun-side hemisphere of the sphere's own normal, with a soft terminator, a
    # low-frequency maria pattern and a faint earthshine on the dark side. The geometry is the artist's sphere.
    node = env_node("moon", "env_moon", [[*np.round(c, 1).tolist(), *q, rad, rad, rad]], maxd=40000.0,
                    material={"baseColor": [0.0, 0.0, 0.0], "roughness": 1.0, "metallic": 0.0,
                              "emissiveColor": [0.86, 0.9, 1.0], "emissiveIntensity": 110.0, "program": "moonPhase",
                              "unlit": True})
    node["procedural"]["source"]["asset"] = "../meshes/derived/env_moon_lit_lod0.glb"
    return node, moon_program([float(v) for v in sun])


def moon_program(sun):
    def op(kind, dst, **kw):
        d = {"kind": kind, "dst": dst}
        d.update(kw)
        return d
    return {"name": "moonPhase", "ops": [
        op("input", 0, input="normal"),
        # lit = saturate(dot(n, sun) * 3 + 0.15): a soft terminator just past the sun-side hemisphere
        op("gradient", 1, srcA=0, value=3.0, constant=[sun[0], sun[1], sun[2], 0.15]),
        op("smoothstep", 1, srcA=1, constant=[0.0, 1.0, 0, 0]),
        # maria: broad darker patches over the sphere (object space, so they turn with the face)
        op("input", 2, input="localPosition"),
        op("noise", 3, srcA=2, value=2.2, seed=5),
        op("remap", 3, srcA=3, value=1, constant=[0.2, 0.8, 0.62, 1.0]),
        op("noise", 4, srcA=2, value=7.0, seed=9),
        op("remap", 4, srcA=4, value=1, constant=[0.2, 0.8, 0.85, 1.05]),
        op("multiply", 3, srcA=3, srcB=4),
        op("multiply", 1, srcA=1, srcB=3),
        # earthshine: the dark side faintly there, so the sphere reads whole
        op("constant", 5, constant=[0.006, 0.006, 0.006, 0.0]),
        op("multiply", 5, srcA=5, srcB=3),
        op("add", 1, srcA=1, srcB=5),
        # unlit: the base colour IS the radiance (times the artist's albedo texture), so the phase goes there; the
        # emission texture of this asset is black ("no emission") and would zero any emission (ADR-1284)
        op("input", 6, input="materialEmission"),
        op("multiply", 6, srcA=6, srcB=1),
        op("constant", 7, constant=[0.0, 0.0, 0.0, 1.0]),
    ], "baseColor": 7, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 1.0}


def clouds_block(moon_dir, glow_dir, lobes="../meshes/env/env_vista_clouds.lobes.json"):
    # Night cloud: bodies darker than the sky behind them (cool, deep), edges toward the moon catching silver, the
    # undersides over the far glow warm. Contrast, not brightness.
    # veil_upper (a row of small puffs across the top) read as cotton balls in a line: the three big masses only
    return {"lobes": lobes, "masses": ["bank_left", "tower_right", "horizon_band"], "extinction": 0.009, "steps": 128, "lightSteps": 6, "lightDistance": 500.0,
            "lightDirection": [round(float(v), 5) for v in moon_dir], "lightColor": [0.6, 0.7, 1.0],
            "lightIntensity": 0.1, "anisotropy": 0.6,
            "ambientTop": [0.008, 0.012, 0.06], "ambientBottom": [0.003, 0.004, 0.02],
            "warmColor": [0.3, 0.1, 0.12], "warmDirection": [round(float(v), 5) for v in glow_dir],
            "warmHeight": 1600.0, "noiseScale": 0.011, "noiseAmount": 0.82,
            "hazeColor": [0.03, 0.045, 0.2], "hazeDistance": 30000.0, "powder": 0.7, "seed": 7}


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


def canopy(gc, eye, yaw0_deg, n=160, seed=91):
    """Dark designed crowns down the slope below the lookout, filling the bottom and the left of the frame (the
    reference's near canopy): near enough to be silhouettes against the lit valley, varied habits and scales."""
    r = np.random.default_rng(seed)
    ex, ey, ez = eye
    groups = {"env_canopy_a": [], "env_canopy_b": [], "env_canopy_c": []}
    for k in range(n):
        # the slope below the lookout: the bottom of frame (near, low) and the left edge
        bearing = r.uniform(-30.0, 40.0) if k % 3 else r.uniform(20.0, 46.0)
        dist = 60.0 + 420.0 * r.random() ** 1.5
        b = math.radians(yaw0_deg + bearing)
        x, z = ex + dist * math.sin(b), ez + dist * math.cos(b)
        y = float(gc.height(np.array([x]), np.array([z]))[0])
        sp = ["env_canopy_a", "env_canopy_a", "env_canopy_b", "env_canopy_c"][int(r.integers(0, 4))]
        sc = r.uniform(1.6, 2.8) * (1.0 + dist / 400.0)
        # the crown must sit in the bottom band of the frame (at least ~19 degrees below the eye), not in front of
        # the lens: scale it down until its top clears that line, and drop it if even a small one does not
        top_room = (ey - 0.34 * dist) - (y - 2.0)
        height = {"env_canopy_a": 30.0, "env_canopy_b": 37.0, "env_canopy_c": 34.0}[sp]
        sc = min(sc, top_room / height)
        if sc < 0.9:
            continue
        groups[sp].append(rec(x, y - 2.0, z, r.uniform(0, 6.28), sc))
    dark = {"baseColor": [0.3, 0.33, 0.45], "roughness": 1.0, "metallic": 0.0, "emissiveColor": [1.0, 1.0, 1.0],
            "emissiveIntensity": 0.4}
    return [env_node(f"vs_{sp}", sp, pts, maxd=3000.0, material=dark) for sp, pts in groups.items() if pts]


def derive_moon(meshes_dir):
    """A copy of the artist's env_moon whose emissive texture is its albedo (the asset ships a black emission map,
    "no emission", which zeroes any emission the phase program writes, ADR-1284). -> the asset path stem."""
    import json
    import struct
    from pathlib import Path
    src = Path(meshes_dir) / "env" / "env_moon_lod0.glb"
    out_dir = Path(meshes_dir) / "derived"
    out_dir.mkdir(parents=True, exist_ok=True)
    b = src.read_bytes()
    jlen = struct.unpack("<I", b[12:16])[0]
    j = json.loads(b[20:20 + jlen])
    for m in j.get("materials", []):
        base = m.get("pbrMetallicRoughness", {}).get("baseColorTexture")
        if base is not None:
            m["emissiveTexture"] = {"index": base["index"]}
            m["emissiveFactor"] = [1.0, 1.0, 1.0]
    # images are referenced by URI relative to the GLB? keep embedded buffers; fix relative URIs to point back
    for im in j.get("images", []):
        if "uri" in im and not im["uri"].startswith("data:"):
            im["uri"] = "../env/" + im["uri"]
    for buf in j.get("buffers", []):
        if "uri" in buf and not buf["uri"].startswith("data:"):
            buf["uri"] = "../env/" + buf["uri"]
    js = json.dumps(j, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    rest = b[20 + jlen:]
    total = 12 + 8 + len(js) + len(rest)
    (out_dir / "env_moon_lit_lod0.glb").write_bytes(b[:8] + struct.pack("<I", total) +
                                                   struct.pack("<I", len(js)) + b"JSON" + js + rest)
    return "derived/env_moon_lit"

"""Checkpoint 1: the Rift built ONLY from what the general (WORLD) renderer already does.

This is the baseline the specialised Environment path is measured against (brief §6, §14, §20.6). Every organism is
a generated GLB placed as `points` on the engine's own terrain heights, every glow is a material emission plus audio
fields, every pool of light is a clustered point light, the haze is the volumetric medium, the river is terrain water.

  python3 examples/bioluminescent/cp1.py            -> cp1/rift-cp1.scene.json + one project per still
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import land  # noqa: E402
import organisms  # noqa: E402

OUT = HERE / "cp1"
REGION = (-260.0, 260.0, -520.0, 520.0)

# Palette (linear, unit peak). Abyssal: dinoflagellate blue, turquoise, violet, magenta; warm accents are rare.
BLUE = [0.04, 0.38, 1.0]
CYAN = [0.0, 0.85, 1.0]
TURQ = [0.02, 0.8, 1.0]
VIOLET = [0.42, 0.12, 1.0]
MAGENTA = [1.0, 0.08, 0.62]
AMBER = [1.0, 0.52, 0.12]
PALE = [0.72, 0.92, 1.0]
BODY = [0.035, 0.045, 0.06]


def rng(seed):
    return np.random.default_rng(seed)


def value_noise(x, z, scale, seed):
    """Smooth 2D value noise in [0, 1] (numpy), for clustering."""
    fx, fz = np.asarray(x) / scale, np.asarray(z) / scale
    ix, iz = np.floor(fx).astype(np.int64), np.floor(fz).astype(np.int64)
    tx, tz = fx - ix, fz - iz
    tx, tz = tx * tx * (3 - 2 * tx), tz * tz * (3 - 2 * tz)

    def h(a, b):
        v = (a * 374761393 + b * 668265263 + seed * 2246822519) & 0xFFFFFFFF
        v = ((v ^ (v >> 13)) * 1274126177) & 0xFFFFFFFF
        return (v & 0xFFFFFF) / float(0xFFFFFF)

    return (h(ix, iz) * (1 - tx) * (1 - tz) + h(ix + 1, iz) * tx * (1 - tz) + h(ix, iz + 1) * (1 - tx) * tz +
            h(ix + 1, iz + 1) * tx * tz)


def quat_from_matrix(m):
    m = np.asarray(m)
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        return [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    i = int(np.argmax([m[0, 0], m[1, 1], m[2, 2]]))
    if i == 0:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        return [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    if i == 1:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        return [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
    return [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]


def basis(up, facing):
    """Rotation whose +y is `up` and whose +z points as close to `facing` as it can."""
    y = np.asarray(up, float)
    y /= np.linalg.norm(y)
    z = np.asarray(facing, float) - np.dot(facing, y) * y
    if np.linalg.norm(z) < 1e-6:
        z = np.cross([1.0, 0, 0], y)
    z /= np.linalg.norm(z)
    x = np.cross(y, z)
    return np.stack([x, y, z], 1)


def candidates(g, density, seed, region=REGION):
    """Jittered-grid candidates at `density` per m^2 over the region."""
    r = rng(seed)
    step = 1.0 / math.sqrt(density)
    xs = np.arange(region[0], region[1], step)
    zs = np.arange(region[2], region[3], step)
    X, Z = np.meshgrid(xs, zs)
    X = X.ravel() + r.uniform(0, step, X.size)
    Z = Z.ravel() + r.uniform(0, step, Z.size)
    return X, Z


def records(g, X, Z, scale, up_mix, seed, facing=None, sink=0.0, yaw=True):
    r = rng(seed + 1)
    Y = g.height(X, Z)
    N = g.normal(X, Z)
    out = []
    for k in range(len(X)):
        up = (1 - up_mix) * np.array([0, 1.0, 0]) + up_mix * N[k]
        if facing is not None:
            f = facing(X[k], Z[k])
        else:
            a = r.uniform(0, 2 * math.pi)
            f = np.array([math.cos(a), 0, math.sin(a)])
        R = basis(up, f)
        q = quat_from_matrix(R)
        s = float(scale[k]) if np.ndim(scale) else float(scale)
        out.append([round(float(X[k]), 3), round(float(Y[k] - sink * s), 3), round(float(Z[k]), 3),
                    *[round(float(v), 5) for v in q], round(s, 4), round(s, 4), round(s, 4)])
    return out


def toward_centre(x, z):
    return np.array([land.centre_x(z) - x, 0.0, 0.0]) + np.array([0, 0, 0.3])


# ---- placement ------------------------------------------------------------------------------------------------------

def place(g):
    P = {}
    dxr = lambda X, Z: X - np.array([land.river_x(z) for z in Z])  # noqa: E731
    dxc = lambda X, Z: X - np.array([land.centre_x(z) for z in Z])  # noqa: E731

    # crinoids: two loose rows on the banks, leaning over the river, and a few on the lower walls
    r = rng(101)
    zs = np.arange(REGION[2] + 10, REGION[3] - 10, 15.0)
    zs = zs + r.uniform(-5, 5, len(zs))
    X, Z, S = [], [], []
    for k, z in enumerate(zs):
        side = 1 if k % 2 else -1
        X.append(land.river_x(z) + side * r.uniform(9, 22))
        Z.append(z)
        S.append(r.uniform(0.75, 1.5))
    for z in np.arange(REGION[2] + 20, REGION[3] - 20, 33.0):
        side = r.choice([-1, 1])
        X.append(land.centre_x(z) + side * r.uniform(35, 70))
        Z.append(z + r.uniform(-8, 8))
        S.append(r.uniform(0.45, 0.9))
    X, Z, S = np.array(X), np.array(Z), np.array(S)
    # lean toward the river: the canopy closes over the water
    P["crinoid"] = records(g, X, Z, S, 0.0, 102, facing=lambda x, z: np.array([land.river_x(z) - x, 0, 0.01]),
                           sink=0.3)

    # sea-pen meadows on the floor and lower slopes, in drifts
    X, Z = candidates(g, 1.8, 201)
    sl = g.slope(X, Z)
    cl = value_noise(X, Z, 22.0, 7) * 0.7 + value_noise(X, Z, 7.0, 8) * 0.3
    keep = (sl < 0.45) & (~g.wet(X, Z)) & (np.abs(dxc(X, Z)) < 60) & (cl > 0.42)
    X, Z = X[keep], Z[keep]
    r = rng(202)
    P["seapen"] = records(g, X, Z, r.uniform(0.6, 1.5, len(X)), 0.3, 203, sink=0.02)

    # polyp mats: everywhere the ground holds, denser low down
    X, Z = candidates(g, 0.45, 301)
    sl = g.slope(X, Z)
    d = np.abs(dxc(X, Z))
    p = np.clip(1.3 - d / 160.0, 0, 1) * (value_noise(X, Z, 15.0, 9) * 0.6 + 0.5)
    keep = (sl < 4.0) & (~g.wet(X, Z)) & (rng(302).random(len(X)) < p)
    X, Z = X[keep], Z[keep]
    P["mat"] = records(g, X, Z, rng(303).uniform(0.7, 1.8, len(X)), 0.85, 304, sink=0.03)

    # sea fans on the walls, facing the canyon's axis (face-on from the river)
    X, Z = candidates(g, 0.03, 401)
    sl = g.slope(X, Z)
    d = np.abs(dxc(X, Z))
    keep = (sl > 0.35) & (sl < 3.0) & (d > 18) & (d < 120) & (value_noise(X, Z, 30.0, 10) > 0.35)
    X, Z = X[keep], Z[keep]
    P["fan"] = records(g, X, Z, rng(402).uniform(0.6, 1.9, len(X)), 0.35, 403, facing=toward_centre, sink=0.05)

    # whip tufts along the banks
    X, Z = candidates(g, 0.15, 501)
    dr = np.abs(dxr(X, Z))
    keep = (dr > 7) & (dr < 26) & (~g.wet(X, Z)) & (g.slope(X, Z) < 0.8) & (value_noise(X, Z, 12.0, 11) > 0.35)
    X, Z = X[keep], Z[keep]
    P["whips"] = records(g, X, Z, rng(502).uniform(0.7, 1.4, len(X)), 0.2, 503)

    # lanterns: rare clusters in hollows and at crinoid feet
    X, Z = candidates(g, 0.05, 601)
    keep = (~g.wet(X, Z)) & (g.slope(X, Z) < 1.0) & (np.abs(dxc(X, Z)) < 70) & (value_noise(X, Z, 9.0, 12) > 0.62)
    X, Z = X[keep], Z[keep]
    P["lanterns"] = records(g, X, Z, rng(602).uniform(0.8, 1.6, len(X)), 0.5, 603)

    # comb jellies drifting over the river
    r = rng(701)
    n = 260
    Z = r.uniform(REGION[2], REGION[3], n)
    X = np.array([land.river_x(z) for z in Z]) + r.normal(0, 10, n)
    Y = g.height(X, Z)
    combs = []
    for k in range(n):
        y = max(Y[k], land.FLOOR - 1.0) + r.uniform(2.5, 16)
        a = r.uniform(0, 6.28)
        q = quat_from_matrix(basis([math.sin(a) * 0.3, 1, math.cos(a) * 0.3], [1, 0, 0]))
        s = r.uniform(0.2, 0.5)
        combs.append([round(X[k], 3), round(y, 3), round(Z[k], 3), *[round(v, 5) for v in q], s, s, s])
    P["comb"] = combs
    return P


# ---- scene ----------------------------------------------------------------------------------------------------------

def mesh_layer(name, part, pts, mat, lod=(), max_distance=0.0, shadow=False, effectors=(), variation=None,
               program=None):
    proc = {
        "source": {"kind": "mesh", "asset": f"../meshes/{part}.glb"},
        "distribution": {"kind": "points", "points": pts},
        "castsShadow": shadow,
        "lod": {"cull": True, "count": 1 + len(lod), "byScreenSize": False, "maxDistance": max_distance,
                **{f"distance{i + 1}": d for i, d in enumerate(lod)}},
        "material": mat,
    }
    if effectors:
        proc["effectors"] = list(effectors)
    if variation:
        proc["materialVariation"] = variation
    if program:
        proc["material"] = dict(mat, program=program)
    return {"name": name, "kind": "procedural", "procedural": proc}


def m(base, emit=None, intensity=0.0, rough=0.55, metallic=0.0, opacity=1.0, double=False):
    d = {"baseColor": base, "roughness": rough, "metallic": metallic}
    if emit is not None:
        d.update({"emissiveColor": emit, "emissiveIntensity": intensity})
    if double:
        d["doubleSided"] = True
    return d


def chunks(lst, n=60000):
    return [lst[i:i + n] for i in range(0, len(lst), n)] or [[]]


def scene(P, wake=1.0, light_gain=1.0):
    """`wake` scales every organism's emission (0.15 = asleep, 1 = awake, 3 = the drop)."""
    W = wake
    kick = {"field": "kick", "op": "emission", "blend": "add", "strength": 6.0 * W}
    band = {"field": "bands", "op": "emission", "blend": "add", "strength": 2.5 * W}
    nodes = [
        {"name": "kick", "kind": "field", "position": [land.river_x(-60), land.FLOOR, -60.0],
         "field": {"kind": "onset", "onsetSource": "low", "onsetDecay": 2.2, "onsetWidth": 9.0, "audioSpeed": 38.0,
                   "strength": 1.0, "falloff": {"kind": "none"}}},
        {"name": "bands", "kind": "field", "position": [0, 0, 0],
         "field": {"kind": "spectrum", "audioBand": "element", "bandLow": 0.05, "bandHigh": 0.95, "audioDelay": 0.0,
                   "audioSpeed": 0.0, "strength": 1.0, "falloff": {"kind": "none"}}},
        {"name": "land", "kind": "terrain", "position": [0, 0, 0], "world": land.world(),
         "terrain": {"chunkSize": 40.0, "resolution": 40, "lodLevels": 4, "lodDistance": 140.0, "viewDistance": 1600.0,
                     "shadowDistance": 150.0, "skirtDepth": 2.0, "groundMottle": True, "groundGlow": 1.0 * W,
                     "groundGlowScale": 0.09, "groundGlowCoverage": 0.32, "groundGlowColor": BLUE,
                     "water": {"shallowColor": [0.004, 0.008, 0.014], "deepColor": [0.001, 0.003, 0.008], "reflection": 0.6, "reflectionTint": [0.3, 0.45, 0.8], "specular": 0.6, "fresnel": 0.3, "foam": 0.0, "ripple": 0.5, "glow": 0.5 * W, "glowColor": BLUE, "glowCoverage": 0.14, "glowScale": 1.1, "sparkle": 0.35 * W, "sparkleColor": PALE}},
         "material": {"baseColor": [0.045, 0.05, 0.065], "roughness": 0.9, "metallic": 0.0}},
    ]
    lod_small = (25.0, 60.0, 140.0)
    nodes.append(mesh_layer("crinoidStalk", "crinoid_stalk", P["crinoid"], m(BODY, VIOLET, 0.02 * W, 0.6),
                            lod=(120.0, 300.0, 700.0), shadow=False))
    nodes.append(mesh_layer("crinoidArms", "crinoid_arms", P["crinoid"], m([0.05, 0.04, 0.08], VIOLET, 0.25 * W, 0.5,
                                                                            double=True), lod=(90.0, 250.0, 600.0)))
    nodes.append(mesh_layer("crinoidBeads", "crinoid_beads", P["crinoid"], m(BODY, CYAN, 9.0 * W, 0.3),
                            lod=(80.0, 220.0, 600.0), effectors=[kick],
                            variation={"emissiveRandom": 0.6, "emissiveSparsity": 0.25}))
    nodes.append(mesh_layer("crinoidChains", "crinoid_chains", P["crinoid"], m(BODY, PALE, 7.0 * W, 0.3),
                            lod=(80.0, 220.0, 600.0), effectors=[band]))
    for i, part in enumerate(chunks(P["seapen"])):
        nodes.append(mesh_layer(f"seapen{i}", "seapen", part, m([0.03, 0.06, 0.07], TURQ, 1.6 * W, 0.45, double=True),
                                lod=lod_small, max_distance=260.0, effectors=[kick, band],
                                variation={"emissiveRandom": 0.7, "emissiveSparsity": 0.15}))
    for i, part in enumerate(chunks(P["mat"])):
        nodes.append(mesh_layer(f"matCushion{i}", "mat_cushion", part, m([0.006, 0.008, 0.012], BLUE, 0.0, 0.95),
                                lod=lod_small, max_distance=300.0))
        nodes.append(mesh_layer(f"matPolyps{i}", "mat_polyps", part, m(BODY, BLUE, 7.0 * W, 0.3), lod=lod_small,
                                max_distance=300.0, effectors=[kick],
                                variation={"emissiveRandom": 0.8, "emissiveSparsity": 0.35}))
    nodes.append(mesh_layer("fanBody", "fan_body", P["fan"], m([0.07, 0.03, 0.08], MAGENTA, 0.12 * W, 0.6),
                            lod=(40.0, 110.0, 300.0), max_distance=700.0))
    nodes.append(mesh_layer("fanPolyps", "fan_polyps", P["fan"], m(BODY, MAGENTA, 6.0 * W, 0.3),
                            lod=(40.0, 110.0, 300.0), max_distance=700.0, effectors=[band],
                            variation={"emissiveRandom": 0.5}))
    nodes.append(mesh_layer("whipsBody", "whips_body", P["whips"], m([0.03, 0.04, 0.05], CYAN, 0.1 * W, 0.5),
                            lod=lod_small, max_distance=220.0))
    nodes.append(mesh_layer("whipsTips", "whips_tips", P["whips"], m(BODY, PALE, 12.0 * W, 0.3), lod=lod_small,
                            max_distance=220.0, effectors=[band],
                            variation={"emissiveRandom": 0.9, "emissiveSparsity": 0.3}))
    nodes.append(mesh_layer("lanternStalks", "lantern_stalks", P["lanterns"], m([0.05, 0.03, 0.02]), lod=lod_small,
                            max_distance=250.0))
    nodes.append(mesh_layer("lanternPods", "lantern_pods", P["lanterns"], m([0.3, 0.12, 0.03], AMBER, 22.0 * W, 0.25),
                            lod=lod_small, max_distance=400.0, variation={"emissiveRandom": 0.4}))
    nodes.append(mesh_layer("combBody", "comb_body", P["comb"], m([0.004, 0.005, 0.008], VIOLET, 0.12 * W, 0.15),
                            lod=(40.0, 120.0, 300.0)))
    rows = mesh_layer("combRows", "comb_rows", P["comb"], m([0.01, 0.01, 0.02], CYAN, 1.2 * W, 0.2),
                      lod=(40.0, 120.0, 300.0))
    rows["procedural"]["material"]["thinFilm"] = {"thickness": 420.0, "ior": 1.45}
    nodes.append(rows)

    # spores and plankton
    nodes.append({"name": "spores", "kind": "particles", "particles": {
        "capacity": 60000, "seed": 4, "shape": "disc", "position": [land.river_x(-40), land.FLOOR + 1.0, -40.0],
        "extent": [60, 0, 220], "direction": [0, 1, 0], "spread": 0.5, "spawnRate": 1500 * max(W, 0.3),
        "speedMin": 0.1, "speedMax": 0.6, "gravity": [0, 0.08, 0], "drag": 0.3, "turbulence": 0.6,
        "turbulenceScale": 0.08, "turbulenceSpeed": 0.2, "sizeStart": 0.05, "sizeEnd": 0.02, "sizeVariance": 0.7,
        "lifetimeMin": 8.0, "lifetimeMax": 16.0, "colorStart": [0.5, 0.85, 1.0, 1.0], "colorEnd": [0.4, 0.3, 1.0, 0.0],
        "emissive": 3.0 * W, "blend": "additive", "softness": 0.6}})

    # lights: pools under lantern clusters and crinoid crowns (clustered forward; cost follows light volume)
    lights = [{"name": "moon", "id": "moon", "type": "directional", "role": "key",
               "direction": [0.25, -0.92, 0.3], "color": [0.55, 0.65, 1.0], "intensity": 0.06,
               "castsShadow": False}]
    r = rng(901)
    lant = P["lanterns"]
    for k in r.choice(len(lant), min(70, len(lant)), replace=False):
        x, y, z = lant[k][:3]
        lights.append({"name": f"lantern{k}", "type": "point", "position": [x, y + 0.6, z], "color": AMBER,
                       "intensity": 40.0 * W * light_gain, "range": 9.0, "radius": 0.2, "castsShadow": False,
                       "volumetric": 0.6})
    for k, c in enumerate(P["crinoid"][:69]):
        x, y, z = c[:3]
        s = c[7]
        lights.append({"name": f"crown{k}", "type": "point", "position": [x, y + 22 * s, z],
                       "color": CYAN, "intensity": 300.0 * W * light_gain, "range": 30.0 * s, "radius": 2.0,
                       "castsShadow": False, "volumetric": 0.35})
    sc = {
        "format": "avgen-scene", "version": 1, "name": "The Rift (CP1, general renderer)",
        "_note": "Generated by examples/bioluminescent/cp1.py. Do not edit.",
        "camera": {"mode": 1, "position": [0, -140, -100], "target": [0, -140, 0], "fov": 50, "orbitSpeed": 0},
        "environment": {
            "background": [0.004, 0.006, 0.014], "intensity": 0.08,
            "fogColor": [0.006, 0.01, 0.026], "volumeDensity": 0.007, "volumeScattering": 0.9,
            "volumeAbsorption": 0.3, "volumeAnisotropy": 0.35, "volumeSteps": 32, "volumeMaxDistance": 900.0,
            "volumeLocalLights": 1.0, "volumeNoise": 0.6, "volumeNoiseScale": 0.03,             "fogHeight": land.FLOOR + 25.0, "fogHeightFalloff": 0.025, "fogUpperDensity": 0.15,
            "fogSky": 1.0, "fogSkyDistance": 1400.0,
            "sky": {"enabled": True, "background": True, "useKeyLight": False, "sunDirection": [0.2, 0.3, 0.9],
                    "zenithColor": [0.002, 0.004, 0.012], "horizonColor": [0.02, 0.025, 0.06],
                    "groundColor": [0.003, 0.003, 0.006], "sunColor": [0.6, 0.7, 1.0], "haze": 0.1,
                    "sunIntensity": 0.0, "sunSize": 0.01, "sunGlow": 0.0, "intensity": 1.0},
        },
        "lights": lights,
        "nodes": nodes,
    }
    return sc


STILLS = {
    # name: (eye, target, fov, wake, t)
    "01-rest": ((-4, 2.2, -150), (8, 4, -60), 48, 0.18, 3.0),
    "02-dense": ((None, 1.4, -70, 14), (None, 3.0, -40, -18), 42, 1.0, 40.0),
    "03-wave": ((None, 34, -170, 0), (None, 0, -20, 0), 55, 1.0, 95.3),
    "04-river": ((None, 0.6, -110, 0), (None, 2.0, -50, 0), 52, 1.2, 60.0),
    "05-canopy": ((None, 1.6, -30, 4), (None, 30, -14, 2), 72, 1.0, 70.0),
    "06-flight": ((None, 9, -95, -3), (None, 6, -40, 6), 62, 1.4, 100.0),
    "07-drop": ((None, 60, -260, 0), (None, -10, -40, 0), 58, 3.0, 96.0),
    "08-aftermath": ((None, 4, -40, -6), (None, 2, 20, 4), 50, 0.35, 204.0),
}


def resolve(g, spec):
    """(None, height-above-ground, z, offset-from-river) -> a point; plain triples pass through as river offsets."""
    if spec[0] is None:
        _, h, z, off = spec
    else:
        off, h, z = spec
    x = land.river_x(z) + off
    y = float(g.height(np.array([x]), np.array([z]))[0])
    wl = float(g.water_level(np.array([x]), np.array([z])))
    return [round(x, 2), round(max(y, wl) + h, 2), round(z, 2)]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    organisms.build_all()
    print("ground...")
    g = land.Ground(land.world(), *REGION, step=2.0)
    P = place(g)
    for k, v in P.items():
        print(f"  {k}: {len(v)}")
    routes = [{"source": s, "target": t, "op": "add", "amount": 0.0} for s, t in
              (("audio.bass", "root/scale"), ("audio.mid", "root/rotationSpeed"), ("audio.rms", "scene/brightness"),
               ("audio.onset", "root/impulse"))]
    post = {"post/bloom/enabled": True, "post/bloom/threshold": 0.9, "post/bloom/intensity": 0.32,
            "post/bloom/emissionWeight": 0.75, "post/tonemap/chroma-retention": 0.6, "post/output/vignette": 0.28,
            "post/grade/contrast": 1.08, "post/grade/saturation": 1.05}
    for name, (eye, tgt, fov, wake, t) in STILLS.items():
        e, c = resolve(g, eye), resolve(g, tgt)
        sc = scene(P, wake=wake)
        sc["camera"] = {"mode": 1, "position": e, "target": c, "fov": fov, "orbitSpeed": 0}
        (OUT / f"{name}.scene.json").write_text(json.dumps(sc, separators=(",", ":")))
        params = dict(post)
        proj = {"format": "avgen-project", "version": 4, "app": {"name": f"Rift CP1 {name}"},
                "assets": {"scene": {"kind": "composition", "path": f"{name}.scene.json"},
                           "audio": {"path": "../../../assets/audio/trench.wav"}},
                "parameters": params, "routes": routes, "_t": t}
        (OUT / f"{name}.json").write_text(json.dumps(proj, indent=1))
    print("wrote", OUT)


if __name__ == "__main__":
    main()

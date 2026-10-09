"""THE BIOLUMINESCENT VALLEY: the scene generator.

    python3 examples/biovalley/valley.py            -> meshes/, sky/, valley.scene.json, shots/<shot>.{scene,}.json

One world, four shot types matching the reference's four panels (docs/prototypes/bioluminescent-valley/
01-analysis-and-plan.md): VISTA (panel 2), RIVERBANK (panel 1), MACRO (panel 3), MEADOW (panel 4).

Built large-to-small, in the spec's order (§35). Phase 1 (this file's first version): terrain, river, karst
towers, far ranges, sky, moon, cameras. Later phases add to `scene()`.

Everything generated (meshes, the sky map, scenes) is gitignored and reproducible from these scripts.
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
import sky  # noqa: E402
import towers  # noqa: E402
import massifs  # noqa: E402
import populate  # noqa: E402
import falls  # noqa: E402
import ecosys  # noqa: E402
import life  # noqa: E402
import glowmap  # noqa: E402
import library  # noqa: E402
from geom import Mesh, quat_y  # noqa: E402

MESHES = HERE / "meshes"
SHOTS = HERE / "shots"
SKY = HERE / "sky"
AUDIO = "../../../assets/audio/trench.wav"   # relative to shots/

# ---- palette (linear) -----------------------------------------------------------------------------------------------
ROCK = [0.075, 0.08, 0.10]
ROCK_FAR = [0.07, 0.075, 0.10]
GROUND = [0.045, 0.05, 0.06]
MOON = [0.80, 0.86, 1.0]



def rng(seed):
    return np.random.default_rng(seed)


# ---- the shots -----------------------------------------------------------------------------------------------------

def _dir(yaw_deg, pitch_deg):
    y, p = math.radians(yaw_deg), math.radians(pitch_deg)
    return np.array([math.sin(y) * math.cos(p), math.sin(p), math.cos(y) * math.cos(p)])


VISTA_YAW = 4.0         # degrees from +z toward +x: looking across the valley, not straight down it
MOON_DIR = _dir(VISTA_YAW - 16.0, 9.5)   # +yaw is screen-left (RH, +z ahead)
GLOW_DIR = _dir(VISTA_YAW - 10.0, 0.0)
VISTA_EYE_XZ = (land.VISTA_X - 40, land.VISTA_Z + 60)


def shots(gc, gfine):
    """name -> (eye, target, fov, near). Eyes stand on the engine's ground."""
    def at(g, x, z, h):
        y = float(g.height(np.array([x]), np.array([z]))[0])
        wl = float(g.water_level(np.array([x]), np.array([z]))[0])
        return [round(x, 2), round(max(y, wl) + h, 2), round(z, 2)]

    S = {}
    # VISTA: on the lookout, over the tree line, across the valley toward the moon
    e = at(gc, *VISTA_EYE_XZ, 14.0)
    d = _dir(VISTA_YAW, -8.5)
    S["vista"] = (e, [round(e[0] + d[0] * 1000, 2), round(e[1] + d[1] * 1000, 2), round(e[2] + d[2] * 1000, 2)],
                  44.0, 1.0, 26000.0)
    # MEADOW: knee height on the terrace, looking north up the valley at the far towers
    mx, mz = land.MEADOW_X + 4, land.MEADOW_Z - 30
    e = at(gfine["meadow"], mx, mz, 1.15)
    d = _dir(12.0, -4.0)
    S["meadow"] = (e, [round(e[0] + d[0] * 500, 2), round(e[1] + d[1] * 500, 2), round(e[2] + d[2] * 500, 2)],
                   46.0, 0.3, 26000.0)
    # RIVERBANK: just above the tributary's water, looking upstream (west) toward the cliff and its falls
    bx = land.BANK_X0 + 185
    g = gfine["bank"]
    zs = np.arange(land.BANK_Z - 40, land.BANK_Z + 40, 0.25)
    wet = g.wet(np.full(len(zs), bx), zs)
    bz = float(zs[np.where(wet)[0].max()]) + 1.2 if wet.any() else land.BANK_Z - 14   # just up the north bank
    e = at(g, bx, bz, 0.85)
    yaw = math.degrees(math.atan2(land.BANK_X0 + 45 - bx, (land.BANK_Z + 8) - bz)) + 4.0   # the falls right of centre
    d = _dir(yaw, 3.0)
    tgt = [round(e[0] + d[0] * 100, 2), round(e[1] + d[1] * 100, 2), round(e[2] + d[2] * 100, 2)]
    S["riverbank"] = (e, tgt, 48.0, 0.2, 26000.0)
    # MACRO: on the mossy bank of the tributary, 30 cm up, looking along it
    bx, bz = land.BANK_X0 + 150, land.BANK_Z + 9
    e = at(gfine["bank"], bx, bz, 0.2)
    tgt = at(gfine["bank"], bx - 6, bz + 3, 0.45)
    S["macro"] = (e, tgt, 42.0, 0.03, 26000.0)
    return S


# ---- towers ---------------------------------------------------------------------------------------------------------

def build_meshes():
    MESHES.mkdir(parents=True, exist_ok=True)
    tris = massifs.build(MESHES)
    r = towers.ranges()
    tris_r = r.write(MESHES / "ranges.glb", "ranges", outward_from=lambda P: np.tile([0, 1.0, 0], (len(P), 1)))
    crescent_mesh(MESHES / "moon.glb")
    print(f"massifs: {len(massifs.CATALOGUE)} variants, {tris} tris; ranges {tris_r} tris")
    print(f"flora: {populate.build(MESHES)} tris; fungi {populate.build_fungi(MESHES)} tris")
    print(f"life: {ecosys.build(MESHES)} tris")


def local_to_world(rec, P):
    """Catalogue-local points (n, 3) of an instance record [x, y, z, qx, qy, qz, qw, s, s, s] (a turn about y)."""
    x, y, z, qx, qy, qz, qw, s = rec[:8]
    yaw = 2.0 * math.atan2(qy, qw)
    c, sn = math.cos(yaw), math.sin(yaw)
    P = np.asarray(P, float) * s
    return np.stack([x + c * P[:, 0] + sn * P[:, 2], y + P[:, 1], z - sn * P[:, 0] + c * P[:, 2]], -1)


def place_massifs(gc):
    """Mountain masses in depth layers along both walls, a far end of ranges, and authored hero masses that frame
    the vista. Scale varies a lot (heavy-tailed); each mass is oriented roughly along the valley."""
    r = rng(77)
    cand = []                      # (name, x, z, scale, yaw, forced)
    names = {k: [n for n, c in massifs.CATALOGUE.items() if c[0] == k] for k in massifs.KINDS}

    def add(kind, x, z, scale, yaw=None, forced=False, name=None):
        nm = name or str(r.choice(names[kind]))
        if yaw is None:
            yaw = math.radians(-90.0 + r.normal(0, 22.0))
        cand.append((nm, x, z, scale, yaw, forced))

    # layers: (offset range from the centre line, mean spacing along z, kinds, scale range)
    layers = (((420, 760), 330.0, ("peak", "cliff", "spire", "range"), (0.45, 1.0)),
              ((780, 1400), 420.0, ("range", "peak", "range", "cliff"), (0.7, 1.35)),
              ((1500, 2600), 560.0, ("range", "range", "peak"), (1.0, 1.75)))
    for side in (-1, 1):
        for (o0, o1), gap, kinds, (s0, s1) in layers:
            z = -1700.0 + r.uniform(0, gap)
            while z < 4600:
                off = r.uniform(o0, o1)
                sc = s0 + (s1 - s0) * min(r.pareto(3.0), 1.0)
                add(str(r.choice(kinds)), land.cx(z) + side * off, z, sc)
                z += gap * r.uniform(0.6, 1.4)
    # the far end, across the valley, receding
    for z in np.arange(2900, 6200, 380.0):
        for _ in range(2):
            add(str(r.choice(("range", "peak", "range", "spire"))), land.cx(z) + r.uniform(-1700, 1700),
                z + r.uniform(-120, 120), r.uniform(0.9, 1.8))
    # a few masses standing on the floor (the reference's isolated towers in the valley)
    for z, off, kind, sc in ((1650, 230, "spire", 0.55), (2150, -200, "peak", 0.5), (2550, 120, "spire", 0.7)):
        add(kind, land.cx(z) + off, z, sc)
    # the vista's framing masses. Bearings from the vista eye, degrees, + = screen left.
    ex, ez = VISTA_EYE_XZ
    for bearing, dist, name, sc, yaw in ((24.0, 1900.0, "peak2", 1.25, -60.0), (33.0, 1500.0, "peak0", 0.95, -100.0),
                                         (17.0, 2600.0, "range1", 1.2, -80.0), (-26.0, 1500.0, "cliff0", 1.0, -90.0),
                                         (-34.0, 2000.0, "peak1", 1.1, -90.0), (-20.0, 2900.0, "range3", 1.3, -95.0)):
        b = math.radians(VISTA_YAW + bearing)
        add(None, ex + dist * math.sin(b), ez + dist * math.cos(b), sc, math.radians(yaw), True, name)
    # the riverbank's cliff: its long axis runs north-south, its face east toward the camera
    add(None, land.BANK_X0 - 25, land.BANK_Z + 10, 1.0, math.radians(-90.0), True, "bankcliff")

    eyes = [(ex, ez), (land.MEADOW_X, land.MEADOW_Z), (land.BANK_X0 + 150, land.BANK_Z)]
    keep = []
    for (nm, x, z, sc, yaw, forced) in cand:
        _, L_, W_, _, _ = massifs.CATALOGUE[nm]
        c_, s_ = math.cos(yaw), math.sin(yaw)

        def covers(a, b):
            dx, dz = a - x, b - z
            lx, lz = c_ * dx - s_ * dz, s_ * dx + c_ * dz
            return abs(lx) < 0.62 * L_ * sc + 80 and abs(lz) < 0.62 * W_ * sc + 80
        if nm != "bankcliff" and any(covers(a, b) for a, b in eyes):
            continue
        if not forced:
            dv = math.hypot(x - ex, z - ez)
            bearing = math.degrees(math.atan2(x - ex, z - ez)) - VISTA_YAW
            if dv < 900 or (abs(bearing) < 14.0 and dv < 3600):
                continue
            if math.hypot(x - land.MEADOW_X, z - land.MEADOW_Z) < 420:
                continue
            if math.hypot(x - (land.BANK_X0 + 100), z - land.BANK_Z) < 330:
                continue
        keep.append((nm, x, z, sc, yaw, forced))
    keep.sort(key=lambda c: (not c[5], -c[3] * massifs.CATALOGUE[c[0]][1]))
    placed = []
    for c in keep:
        nm, x, z, sc = c[:4]
        rad = 0.32 * sc * massifs.CATALOGUE[nm][1]
        if c[5] or all(math.hypot(x - p[1], z - p[2]) > 0.8 * (rad + 0.32 * p[3] * massifs.CATALOGUE[p[0]][1])
                       for p in placed):
            placed.append(c)
    # each mass stands on the lowest ground under its footprint, so none hangs over a slope
    out = {n: [] for n in massifs.CATALOGUE}
    ang = np.linspace(0, 2 * math.pi, 24, endpoint=False)
    for k, (nm, x, z, sc, yaw, forced) in enumerate(placed):
        _, L, W, _, _ = massifs.CATALOGUE[nm]
        ring = []
        for f in (0.0, 0.25, 0.5):
            ring += [(x + f * L * sc * math.cos(a), z + f * L * sc * math.sin(a)) for a in ang]
        R = np.array(ring)
        y0 = float(gc.height(R[:, 0], R[:, 1]).min())
        out[nm].append([round(x, 2), round(y0 - 6.0, 2), round(z, 2), *quat_y(yaw),
                        round(sc, 4), round(sc, 4), round(sc, 4)])
    print(f"massifs placed: {len(placed)}")
    return out


def find_instance(tw, name, x, z):
    """The placed record of catalogue `name` nearest (x, z)."""
    best = min(tw[name], key=lambda rc: math.hypot(rc[0] - x, rc[2] - z))
    return best


def waterfalls(tw, eyes):
    """The falls (one mesh per fall, in world space) and their mist: three off the vista's framing masses, one at
    the riverbank's stream head."""
    nodes = []
    ex, ez = VISTA_EYE_XZ
    specs = []
    for bearing, dist, name, width, lipfrac in ((-26.0, 1500.0, "cliff0", 22.0, 0.5), (33.0, 1500.0, "peak0", 14.0, 0.45),
                                               (24.0, 1900.0, "peak2", 18.0, 0.4)):
        b = math.radians(VISTA_YAW + bearing)
        rc = find_instance(tw, name, ex + dist * math.sin(b), ez + dist * math.cos(b))
        specs.append((name, rc, (ex, ez), width, lipfrac, 60, 2.5))
    bx, bz = eyes["riverbank"]
    rc = find_instance(tw, "bankcliff", land.BANK_X0 - 25, land.BANK_Z + 10)
    specs.append(("bankcliff", rc, (bx, bz), 6.0, 0.22, 50, 1.2))
    for k, (name, rc, toward, width, frac, strands, speed) in enumerate(specs):
        F = massifs.Field(MESHES, name)
        lip, foot = falls.find_lip(F, rc, toward, frac)
        facing = (toward[0] - lip[0], toward[1] - lip[2])
        m = falls.fall(lip, foot[1], facing, width, 600 + k, strands=strands, speed=speed)
        m.write(MESHES / f"fall{k}.glb", f"fall{k}")
        nodes.append({"name": f"fall{k}", "kind": "procedural", "procedural": {
            "source": {"kind": "mesh", "asset": f"../meshes/fall{k}.glb"},
            "distribution": {"kind": "points", "points": [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]]}, "castsShadow": False,
            "material": mat([0.1, 0.14, 0.2], 0.3, [0.45, 0.62, 1.0], 0.35, doubleSided=True, program="falls")}})
        big = width > 10
        nodes.append(falls.mist(f"fallMist{k}", foot + np.array([0, 1.0, 0]), width * (1.2 if big else 1.5),
                                60.0 if big else 90.0, 610 + k, size=(6.0, 22.0) if big else (3.0, 9.0),
                                emissive=1.2 if big else 1.4))
        print(f"  fall{k} ({name}): lip {np.round(lip, 1).tolist()} drop {lip[1] - foot[1]:.0f} m")
    return nodes


# ---- scene ----------------------------------------------------------------------------------------------------------

def mat(base, rough=0.85, emit=None, intensity=0.0, **kw):
    d = {"baseColor": base, "roughness": rough, "metallic": 0.0}
    if emit is not None:
        d.update({"emissiveColor": emit, "emissiveIntensity": intensity})
    d.update(kw)
    return d


def mesh_node(name, glb, pts, material, max_distance=0.0, shadow=False):
    return {"name": name, "kind": "procedural", "procedural": {
        "source": {"kind": "mesh", "asset": f"../meshes/{glb}.glb"},
        "distribution": {"kind": "points", "points": pts},
        "castsShadow": shadow,
        "lod": {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": max_distance},
        "material": material}}


def crescent_mesh(path, phase=0.24, tilt=0.75, n=256):
    """A crescent as a flat lens in its own xy plane, unit radius, facing +z: the points of the unit disc outside a
    second disc of the same radius offset by `2 * phase` along a direction tilted by `tilt` radians. A mesh rather
    than an occluding sphere, because an occluder at 9 km is fogged to the sky's colour beside a bright halo and reads
    as a pale disc."""
    off = 2.0 * phase * np.array([math.cos(tilt), math.sin(tilt)])
    # outer arc: the lit limb, the half of the unit circle away from the offset; inner arc: the terminator, the
    # part of the offset circle inside the unit disc
    a0 = math.atan2(-off[1], -off[0])
    # where the two circles meet
    d = np.linalg.norm(off)
    h = math.acos(d / 2.0)
    base = math.atan2(off[1], off[0])
    outer = np.linspace(base + h, base + 2 * math.pi - h, n)
    P_out = np.stack([np.cos(outer), np.sin(outer)], -1)
    # the terminator: the offset circle's arc between the same two meeting points, through the disc's inside
    c_ang0 = math.atan2(math.sin(base + h) - off[1], math.cos(base + h) - off[0])
    c_ang1 = math.atan2(math.sin(base - h) - off[1], math.cos(base - h) - off[0])
    if c_ang1 < c_ang0:
        c_ang1 += 2 * math.pi
    inner = np.linspace(c_ang0, c_ang1, n)
    P_in = off + np.stack([np.cos(inner), np.sin(inner)], -1)
    # the terminator walks the same way round as the limb; pair them from the matching ends
    P_in = P_in[::-1] if np.linalg.norm(P_in[0] - P_out[0]) > np.linalg.norm(P_in[-1] - P_out[0]) else P_in
    P = np.concatenate([np.c_[P_out, np.zeros(n)], np.c_[P_in, np.zeros(n)]])
    F = []
    for k in range(n - 1):
        F += [[k, k + 1, n + k], [k + 1, n + k + 1, n + k]]
    m = Mesh()
    m.add(P, np.array(F), np.c_[P[:, 0] * 0.5 + 0.5, P[:, 1] * 0.5 + 0.5])
    return m.write(path, "moon", outward_from=lambda Q: np.tile([0, 0, 1.0], (len(Q), 1)))


def moon_nodes(eye):
    """The crescent, 20 km out, turned to face the vista camera (it is far enough not to parallax between shots)."""
    D, ang = 20000.0, math.radians(2.8)
    rad = D * math.tan(ang)
    c = np.asarray(eye) + MOON_DIR * D
    z = -MOON_DIR                                   # the mesh's +z faces the camera
    x = np.cross([0, 1.0, 0], z)
    x /= np.linalg.norm(x)
    y = np.cross(z, x)
    q = _quat(np.stack([x, y, z], 1))
    return [mesh_node("moon", "moon", [[*np.round(c, 1).tolist(), *q, rad, rad, rad]],
                      mat([0.0, 0.0, 0.0], 1.0, MOON, 9.0, unlit=True, doubleSided=True))]


def _quat(m):
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        return [round(v, 6) for v in ((m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s)]
    i = int(np.argmax([m[0, 0], m[1, 1], m[2, 2]]))
    if i == 0:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = (0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s)
    elif i == 1:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = ((m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s)
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = ((m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s)
    return [round(v, 6) for v in q]


def scene(tw, eye, target, fov, near, far, extra=(), eco=None, grids=(), air=None):
    water = {"shallowColor": [0.004, 0.008, 0.016], "deepColor": [0.001, 0.003, 0.010], "clarity": 0.8,
             "reflection": 1.3, "reflectionTint": [0.5, 0.5, 1.0], "specular": 1.2, "fresnel": 0.3, "foam": 0.0,
             "ripple": 0.35, "rippleScale": 0.5, "edgeFade": 0.5}
    nodes = [
        {"name": "land", "kind": "terrain", "position": [0, 0, 0], "world": land.world(),
         "terrain": {"chunkSize": 200.0, "resolution": 80, "lodLevels": 4, "lodDistance": 300.0,
                     "viewDistance": 4500.0, "shadowDistance": 60.0, "skirtDepth": 30.0, "groundMottle": True,
                     "water": water},
         "material": mat(GROUND, 0.92, program="ground")},
        {"name": "ranges", "kind": "procedural", "procedural": {
            "source": {"kind": "mesh", "asset": "../meshes/ranges.glb"},
            "distribution": {"kind": "points", "points": [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]]},
            "castsShadow": False, "material": mat(ROCK_FAR, 0.9)}},
    ]
    for k, pts in tw.items():
        if pts:
            nodes.append(mesh_node(f"massif_{k}", f"massif_{k}", pts, mat(ROCK, 0.85, program="rock"), shadow=False))
    nodes += list(extra)
    nodes += moon_nodes(eye)

    moon_light = -MOON_DIR
    return {
        "format": "avgen-scene", "version": 1, "name": "Bioluminescent Valley",
        "_note": "Generated by examples/biovalley/valley.py. Do not edit.",
        "camera": {"mode": 1, "position": eye, "target": target, "fov": fov, "orbitSpeed": 0, "near": near, "far": far},
        "environment": {
            "map": "../sky/night.hdr", "intensity": 0.9, "skyIntensity": 1.0, "rotation": 0.0,
            "lightFromEnvironment": False,
            "background": [0.004, 0.006, 0.02],
            "fogColor": [0.010, 0.012, 0.075],
            "volumeDensity": 0.0014, "volumeAbsorption": 1.0, "volumeScattering": 0.0,
            "volumeSteps": (air or {}).get("steps", 16), "volumeMaxDistance": (air or {}).get("reach", 200.0),
            "volumeEmission": (air or {}).get("emission", 0.0), "volumeColorField": (air or {}).get("mist", ""),
            "volumeNoise": 0.35, "volumeNoiseScale": 0.02,
            "fogHeight": 18.0, "fogHeightFalloff": 0.012, "fogUpperDensity": 0.1, "fogHeightAmount": 1.0,
            "fogSky": 0.6, "fogSkyDistance": 5500.0,
        },
        "lights": [{"name": "moon", "id": "moon", "type": "directional", "role": "key",
                    "direction": [round(float(v), 4) for v in moon_light], "color": [0.62, 0.70, 1.0],
                    "intensity": 0.55, "castsShadow": True}],
        "effects": [{"id": "stars", "name": "stars", "type": "stars", "owner": {"kind": "world"}, "enabled": True,
                     "order": 0, "activation": "always",
                     "parameters": {"brightness": 0.9, "density": 0.45, "magnitudeSlope": 6.5, "colorSpread": 0.35,
                                    "twinkle": 0.25, "twinkleRate": 1.2, "horizonFade": 0.7, "band": 0.3,
                                    "bandTilt": 32.0, "daylight": 0.0}}],
        "nodes": nodes,
        "materialPrograms": life.programs(),
        **({"grids": list(grids)} if grids else {}),
        **({"ecosystem": eco} if eco else {}),
    }


# The library's glint colours and gains (its own textures carry the bodies' light; the glints are accents)
LIB_COLORS = {"veilcap": [0.55, 0.95, 1.0], "glowmound": [0.2, 1.0, 0.6], "glowmat": [0.3, 0.8, 1.0],
              "lanternberry": [0.6, 1.0, 0.4], "filigree": [0.5, 0.9, 1.0], "veilfern": [0.3, 0.9, 1.0],
              "bellspire": [0.5, 0.6, 1.0], "lattice": [0.45, 0.4, 1.0], "reliquary": [0.7, 0.9, 1.0]}
LIB_COLORS.update({"glowsedge": [0.4, 1.0, 0.85], "riverrock_a": [0.2, 0.9, 0.6], "riverrock_b": [0.2, 0.9, 0.6],
                   "riverrock_c": [0.2, 0.9, 0.6], "riverrock_d": [0.2, 0.9, 0.6], "shadowbough": [0.4, 0.8, 1.0],
                   "darkfern": [0.3, 0.6, 1.0], "canopy": [0.3, 0.9, 0.9], "canopygrove": [0.3, 0.9, 0.9],
                   "bulbstalk": [0.5, 1.0, 0.6]})
# glint gains: the bodies carry their light in their own textures; the glints are sparse accents (the splat's
# sub-pixel floor makes a few hundred points per instance x thousands of instances read as a white field)
LIB_GAIN = {"glowmound": 0.12, "glowmat": 0.1, "veilcap": 2.5, "filigree": 0.04, "lanternberry": 1.5,
            "veilfern": 0.5, "glowsedge": 2.5, "riverrock_a": 0.1, "riverrock_b": 0.1, "riverrock_c": 0.1,
            "riverrock_d": 0.1, "darkfern": 0.1, "bellspire": 1.5, "lattice": 0.3, "reliquary": 0.8,
            "shadowbough": 0.4}
# Per-shot water: the riverbank's stream is dark (it carries reflections, not the sky's colour)
WATER = {"riverbank": {"reflection": 0.35, "reflectionTint": [0.2, 0.26, 0.55], "specular": 0.8, "ripple": 0.25}}

# The air per shot: how far the lit march reaches, its steps, and how strongly the mist glows where the light pools
AIR = {"vista": {"reach": 4000.0, "steps": 24, "emission": 0.7, "mist": "mistFloor"},
       "meadow": {"reach": 1500.0, "steps": 24, "emission": 0.06, "mist": "mistMeadow"},
       "riverbank": {"reach": 300.0, "steps": 24, "emission": 0.15, "mist": "mistBank"},
       "macro": {"reach": 40.0, "steps": 12, "emission": 0.08, "mist": "mistBank"}}

# Bloom as the Rootworks tuned it (restrained: it carries the cores, not the look)
POST = {"post/bloom/enabled": True, "post/bloom/threshold": 0.6, "post/bloom/intensity": 0.45,
        "post/bloom/knee": 0.7, "post/bloom/radius": 1.25, "post/bloom/emissionWeight": 0.9,
        "post/tonemap/chroma-retention": 0.85, "post/output/vignette": 0.25}

# Depth of field per shot: the macro's foreground goes soft into bokeh, the riverbank's near bank slightly
SHOT_POST = {
    "macro": {"post/dof/enabled": True, "post/dof/focusDistance": 0.9, "post/dof/focusRange": 0.3,
              "post/dof/maxRadius": 26.0},
    "riverbank": {"post/dof/enabled": True, "post/dof/focusDistance": 9.0, "post/dof/focusRange": 25.0,
                  "post/dof/maxRadius": 4.0, "camera/exposure/compensation": 0.6},
}


def project(name, scene_path):
    params = dict(POST)
    params.update(SHOT_POST.get(name, {}))
    return {"format": "avgen-project", "version": 4, "app": {"name": f"Bioluminescent Valley ({name})"},
            "assets": {"scene": {"kind": "composition", "path": scene_path}, "audio": {"path": AUDIO}},
            "parameters": params}


def main():
    SHOTS.mkdir(parents=True, exist_ok=True)
    SKY.mkdir(parents=True, exist_ok=True)
    if "--no-meshes" not in sys.argv:
        build_meshes()
    if "--no-sky" not in sys.argv and not (SKY / "night.hdr").exists() or "--sky" in sys.argv:
        print("sky...")
        sky.build(SKY / "night.hdr", MOON_DIR, GLOW_DIR)
    print("ground (coarse)...")
    gc = land.ground((-4400.0, 4400.0, -2400.0, 4400.0), 25.0)
    gfine = {
        "meadow": land.ground((land.MEADOW_X - 200, land.MEADOW_X + 200, land.MEADOW_Z - 200, land.MEADOW_Z + 300), 2.0),
        "bank": land.ground((land.BANK_X0 - 120, land.BANK_X0 + 300, land.BANK_Z - 150, land.BANK_Z + 120), 1.0),
    }
    tw = place_massifs(gc)
    print("ground (floor)...")
    gfloor = land.ground((-1500.0, 1500.0, -1700.0, 4400.0), 6.0)
    S = shots(gc, gfine)
    vista_eye = S["vista"][0]
    eyes = [(s[0][0], s[0][2]) for s in S.values()]
    hero, shrubs_f, arch, boulders = populate.framing(gc, gfine, vista_eye, VISTA_YAW)
    shrubs = populate.ledges(tw, MESHES)
    for k, v in shrubs_f.items():
        shrubs[k] += v
    groves = populate.forest(gfloor, vista_eye, VISTA_YAW, eyes)
    glow, dark = ecosys.split_groves(groves)

    def near_split(d):
        # groves within 260 m of a close shot (riverbank, macro, meadow) use the full build
        close = [(S[k][0][0], S[k][0][2]) for k in ("riverbank", "macro", "meadow")]
        hq, lo = {}, {}
        for v, pts in d.items():
            for rc in pts:
                near = any(math.hypot(rc[0] - a, rc[2] - b) < 260.0 for a, b in close)
                (hq if near else lo).setdefault(v, []).append(rc)
        return hq, lo
    glow_hq, glow_lo = near_split(glow)
    dark_hq, dark_lo = near_split(dark)
    groups = {"groveglow": glow_lo, "grovedark": dark_lo, "grovehqglow": glow_hq, "grovehqdark": dark_hq,
              "shrub": shrubs, "arch": arch, "hero": {0: hero}, "boulder": boulders}
    eyes_by = {k: (s[0][0], s[0][2]) for k, s in S.items()}
    heads = {k: math.atan2(s[1][0] - s[0][0], s[1][2] - s[0][2]) for k, s in S.items()}
    ecosys.WATER_BANK = float(gfine["bank"].water_level(np.array([eyes_by["riverbank"][0] - 20.0]),
                                                        np.array([land.BANK_Z]))[0])
    print(f"  bank water level {ecosys.WATER_BANK:.2f}")
    veg = populate.nodes(groups, mat)
    placed = ecosys.place(gc, gfine, gfloor, eyes_by, heads)
    carpet, hosts = ecosys.nodes(placed, mat)
    grids, gfields = glowmap.maps(placed, glow, eyes_by)
    extra = (gfields + veg + waterfalls(tw, eyes_by) + populate.fungus_nodes(populate.fungi(gfine, eyes_by), mat) +
             carpet + ecosys.particles(eyes_by))

    def names(prefix):
        return [n["name"] for n in veg if n["name"].startswith(prefix) and "_leaf" in n["name"]]
    forest_hosts = {"all": [names(f"groveglow{v}_leaf") + names(f"grovedark{v}_leaf") + names(f"grovehqglow{v}_leaf") +
                            names(f"grovehqdark{v}_leaf") for v in range(populate.N_GROVES)],
                    "glow": [names(f"groveglow{v}_leaf") + names(f"grovehqglow{v}_leaf")
                             for v in range(populate.N_GROVES)]}
    shrub_hosts = [n["name"] for n in veg if n["name"].startswith("shrub") and "_leaf" in n["name"]]
    eco = ecosys.ecosystem(hosts, forest_hosts, shrub_hosts, ["fungus_veil_threads"], ecosys.WATER_BANK)
    print(f"  ecosystem layers: {len(eco['layers'])}")
    # the hero library, per shot
    lib_nodes, lib_layers = {}, {}
    rb_eye = S["riverbank"][0]
    rb_head = heads["riverbank"]
    bank_mirror = {"mirror": 4.0, "mirrorLevel": round(ecosys.WATER_BANK, 3), "mirrorStretch": 2.4}
    groups = library.bank(gfine["bank"], rb_eye, rb_head, ecosys.WATER_BANK)
    nodes_b, hosts_b = library.assemble(groups, rb_eye)
    lib_nodes["riverbank"] = nodes_b
    lib_layers["riverbank"] = [library.emit_layer(f"lib_{sp}", hs, sp, LIB_COLORS[sp], LIB_GAIN.get(sp, 0.3),
                                                  bank_mirror, sparsity=0.5) for sp, hs in hosts_b.items()]
    # layers a shot cannot see are dropped (the ecosystem holds 32)
    DROP = {"riverbank": ("glade", "crown", "ledge", "understory", "orbs", "macroMoss", "gills", "veilBeads",
                          "latticeGlow", "tips", "frondGlow"), "macro": ("glade", "crown", "ledge"),
            "meadow": ("ledge",), "vista": ()}
    for name, (eye, tgt, fov, near, far) in S.items():
        eco_s = dict(eco)
        eco_s["layers"] = [l for l in eco["layers"] if not l["name"].startswith(DROP[name])] + lib_layers.get(name, [])
        assert len(eco_s["layers"]) <= 32, (name, len(eco_s["layers"]))
        sc = scene(tw, eye, tgt, fov, near, far, extra + lib_nodes.get(name, []), eco_s, grids, AIR[name])
        if name in WATER:
            for n in sc["nodes"]:
                if n["name"] == "land":
                    n["terrain"]["water"].update(WATER[name])
        (SHOTS / f"{name}.scene.json").write_text(json.dumps(sc, separators=(",", ":")))
        (SHOTS / f"{name}.json").write_text(json.dumps(project(name, f"{name}.scene.json"), indent=1))
        print(f"  shot {name}: eye {eye} target {tgt}")
    print("wrote", SHOTS)


if __name__ == "__main__":
    main()

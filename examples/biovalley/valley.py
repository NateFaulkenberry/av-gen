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

N_TOWER_VARIANTS = 8
TOWER_H = 300.0          # nominal height of a variant; instances scale it


def rng(seed):
    return np.random.default_rng(seed)


# ---- the shots -----------------------------------------------------------------------------------------------------

def _dir(yaw_deg, pitch_deg):
    y, p = math.radians(yaw_deg), math.radians(pitch_deg)
    return np.array([math.sin(y) * math.cos(p), math.sin(p), math.cos(y) * math.cos(p)])


VISTA_YAW = 4.0         # degrees from +z toward +x: looking across the valley, not straight down it
MOON_DIR = _dir(VISTA_YAW - 16.0, 9.0)   # +yaw is screen-left (RH, +z ahead)
GLOW_DIR = _dir(VISTA_YAW - 4.0, 0.0)


def shots(gc, gfine):
    """name -> (eye, target, fov, near). Eyes stand on the engine's ground."""
    def at(g, x, z, h):
        y = float(g.height(np.array([x]), np.array([z]))[0])
        wl = float(g.water_level(np.array([x]), np.array([z]))[0])
        return [round(x, 2), round(max(y, wl) + h, 2), round(z, 2)]

    S = {}
    # VISTA: on the lookout, over the tree line, across the valley toward the moon
    e = at(gc, land.VISTA_X - 40, land.VISTA_Z + 60, 80.0)
    d = _dir(VISTA_YAW, -7.0)
    S["vista"] = (e, [round(e[0] + d[0] * 1000, 2), round(e[1] + d[1] * 1000, 2), round(e[2] + d[2] * 1000, 2)],
                  44.0, 0.2, 26000.0)
    # MEADOW: knee height on the terrace, looking north up the valley at the far towers
    mx, mz = land.MEADOW_X + 4, land.MEADOW_Z - 30
    e = at(gfine["meadow"], mx, mz, 1.15)
    d = _dir(12.0, -4.0)
    S["meadow"] = (e, [round(e[0] + d[0] * 500, 2), round(e[1] + d[1] * 500, 2), round(e[2] + d[2] * 500, 2)],
                   46.0, 0.05, 26000.0)
    # RIVERBANK: just above the tributary's water, looking upstream (west) toward the cliff and its falls
    bx, bz = land.BANK_X0 + 185, land.BANK_Z - 14
    e = at(gfine["bank"], bx, bz, 0.6)
    yaw = math.degrees(math.atan2(land.BANK_X0 + 45 - bx, (land.BANK_Z + 8) - bz)) + 7.0   # the falls right of centre
    d = _dir(yaw, 2.0)
    tgt = [round(e[0] + d[0] * 100, 2), round(e[1] + d[1] * 100, 2), round(e[2] + d[2] * 100, 2)]
    S["riverbank"] = (e, tgt, 48.0, 0.05, 26000.0)
    # MACRO: on the mossy bank of the tributary, 30 cm up, looking along it
    bx, bz = land.BANK_X0 + 150, land.BANK_Z + 9
    e = at(gfine["bank"], bx, bz, 0.32)
    tgt = at(gfine["bank"], bx - 6, bz + 3, 0.5)
    S["macro"] = (e, tgt, 42.0, 0.05, 26000.0)
    return S


# ---- towers ---------------------------------------------------------------------------------------------------------

def build_towers():
    MESHES.mkdir(parents=True, exist_ok=True)
    tris = 0
    for k in range(N_TOWER_VARIANTS):
        m = towers.tower(1000 + k * 7, TOWER_H)
        tris += m.write(MESHES / f"tower{k}.glb", f"tower{k}", outward_from=towers.tower_outward)
    r = towers.ranges()
    tris_r = r.write(MESHES / "ranges.glb", "ranges", outward_from=lambda P: np.tile([0, 1.0, 0], (len(P), 1)))
    crescent_mesh(MESHES / "moon.glb")
    print(f"towers: {N_TOWER_VARIANTS} variants, {tris} tris; ranges {tris_r} tris")


def place_towers(gc):
    """Where the towers stand: rows on both walls and the plateau behind, a few on the floor, a forest of them at
    the far end. Heights follow distance from the floor's edge (taller further back) with bounded randomness, and a
    minimum spacing scaled to size so they never interpenetrate."""
    r = rng(77)
    cand = []

    def add(x, z, h, variant=None):
        cand.append((x, z, h, variant))

    # walls and plateau, both sides: CLUSTERS, not rows. Each cluster is a dominant tower with satellites;
    # between clusters the walls stay bare (negative space). Heights are heavy-tailed: most towers are modest, a
    # few are giants, so the skyline has a hierarchy.
    for side in (-1, 1):
        z = -1500.0 + r.uniform(0, 120)
        while z < 4300:
            off = r.uniform(340, 1300) if r.random() < 0.8 else r.uniform(1300, 1900)
            cxz, czz = land.cx(z) + side * off, z
            hd = min(160 + 260 * r.pareto(2.6) + off * 0.12, 820)
            add(cxz, czz, hd)
            for _ in range(int(r.integers(1, 6))):
                a_ = r.uniform(0, 6.283)
                d_ = r.uniform(0.25, 0.7) * hd
                add(cxz + d_ * math.cos(a_), czz + d_ * math.sin(a_), hd * r.uniform(0.3, 0.75))
            z += r.uniform(160, 420)
    # isolated towers on the floor (rare, they stand in the river's braids)
    for z, off, h in ((1300, -150, 210), (1720, 210, 260), (2050, -60, 300), (2250, 230, 240), (2500, -260, 330),
                      (780, 290, 150)):
        add(land.cx(z) + off, z, h)
    # the far end: towers across the whole valley, receding
    for z in np.arange(2700, 4400, 90.0):
        x = land.cx(z) + r.uniform(-1400, 1400)
        hd = min(220 + 240 * r.pareto(2.6), 760)
        add(x, z, hd)
        for _ in range(int(r.integers(0, 4))):
            a_ = r.uniform(0, 6.283)
            add(x + 0.5 * hd * math.cos(a_), z + 0.5 * hd * math.sin(a_), hd * r.uniform(0.35, 0.8))
    # giants: a few far taller than the rest, so the skyline has a hierarchy, not a comb
    for z, off, h in ((1450, -560, 760), (2200, 620, 820), (3100, -300, 900), (900, 640, 640), (2700, 150, 700)):
        add(land.cx(z) + off, z, h)
    # the vista's hero masses (panel 2): a broad group of peaks left of centre, a cliff-massif framing the right.
    # Bearings are from the vista eye, degrees, + = screen left.
    ex, ez = land.VISTA_X - 40, land.VISTA_Z + 60
    for bearing, dist, h, var in ((17.0, 1450.0, 560.0, 3), (26.0, 1600.0, 480.0, 6), (12.0, 1800.0, 430.0, 2),
                                  (34.0, 1050.0, 380.0, 5), (-30.0, 1350.0, 500.0, 5), (-36.0, 1700.0, 470.0, 3),
                                  (-17.0, 1900.0, 420.0, 6)):
        b = math.radians(VISTA_YAW + bearing)
        add(ex + dist * math.sin(b), ez + dist * math.cos(b), h, var)
    # the falls tower: the riverbank's stream comes down its face
    add(land.BANK_X0 - 150, land.BANK_Z + 10, 190, 0)

    # keep-outs: the vista's own sight line over the near floor, the meadow and the bank
    keep = []
    ex, ez = land.VISTA_X - 40, land.VISTA_Z + 60
    for (x, z, h, var) in cand:
        dv = math.hypot(x - ex, z - ez)
        bearing = math.degrees(math.atan2(x - ex, z - ez)) - VISTA_YAW
        if var is None and (dv < 300 or (abs(bearing) < 9.0 and dv < 2600)):
            continue
        if math.hypot(x - land.MEADOW_X, z - land.MEADOW_Z) < 280:
            continue
        if 20 < math.hypot(x - (land.BANK_X0 + 90), z - land.BANK_Z) < 175:
            continue
        keep.append((x, z, h, var))
    # spacing: a tower of height h claims ~0.17 h of radius
    keep.sort(key=lambda c: -c[2])
    placed = []
    for (x, z, h, var) in keep:
        rad = 0.12 * h
        if var is not None or all(math.hypot(x - px, z - pz) > 0.75 * (rad + 0.12 * ph) for (px, pz, ph, _) in placed):
            placed.append((x, z, h, var))
    by_variant = {k: [] for k in range(N_TOWER_VARIANTS)}
    X = np.array([p[0] for p in placed])
    Z = np.array([p[1] for p in placed])
    Y = gc.height(X, Z)
    for k, (x, z, h, var) in enumerate(placed):
        s = h / TOWER_H
        v = int(r.integers(0, N_TOWER_VARIANTS)) if var is None else var
        by_variant[v].append([round(x, 2), round(float(Y[k]), 2), round(z, 2), *quat_y(r.uniform(0, 6.283)),
                              round(s, 4), round(s * r.uniform(0.9, 1.1), 4), round(s, 4)])
    print(f"towers placed: {len(placed)}")
    return by_variant


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
    D, ang = 20000.0, math.radians(3.1)
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


def scene(tw, eye, target, fov, near, far):
    water = {"shallowColor": [0.004, 0.008, 0.016], "deepColor": [0.001, 0.003, 0.010], "clarity": 0.8,
             "reflection": 1.1, "reflectionTint": [0.55, 0.62, 1.0], "specular": 1.2, "fresnel": 0.3, "foam": 0.0,
             "ripple": 0.35, "rippleScale": 0.5, "edgeFade": 0.5}
    nodes = [
        {"name": "land", "kind": "terrain", "position": [0, 0, 0], "world": land.world(),
         "terrain": {"chunkSize": 200.0, "resolution": 80, "lodLevels": 4, "lodDistance": 300.0,
                     "viewDistance": 4500.0, "shadowDistance": 200.0, "skirtDepth": 4.0, "groundMottle": True,
                     "water": water},
         "material": mat(GROUND, 0.92)},
        {"name": "ranges", "kind": "procedural", "procedural": {
            "source": {"kind": "mesh", "asset": "../meshes/ranges.glb"},
            "distribution": {"kind": "points", "points": [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]]},
            "castsShadow": False, "material": mat(ROCK_FAR, 0.9)}},
    ]
    for k, pts in tw.items():
        if pts:
            nodes.append(mesh_node(f"towers{k}", f"tower{k}", pts, mat(ROCK, 0.85), shadow=False))
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
            "fogColor": [0.012, 0.022, 0.085],
            "volumeDensity": 0.0011, "volumeAbsorption": 1.0, "volumeScattering": 0.0, "volumeSteps": 8,
            "volumeMaxDistance": 200.0,
            "fogHeight": 30.0, "fogHeightFalloff": 0.007, "fogUpperDensity": 0.1, "fogHeightAmount": 1.0,
            "fogSky": 1.0, "fogSkyDistance": 5500.0,
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
    }


POST = {"post/bloom/enabled": True, "post/bloom/threshold": 0.9, "post/bloom/intensity": 0.25,
        "post/tonemap/chroma-retention": 0.8, "post/output/vignette": 0.25}


def project(name, scene_path):
    return {"format": "avgen-project", "version": 4, "app": {"name": f"Bioluminescent Valley ({name})"},
            "assets": {"scene": {"kind": "composition", "path": scene_path}, "audio": {"path": AUDIO}},
            "parameters": dict(POST)}


def main():
    SHOTS.mkdir(parents=True, exist_ok=True)
    SKY.mkdir(parents=True, exist_ok=True)
    if "--no-meshes" not in sys.argv:
        build_towers()
    if "--no-sky" not in sys.argv and not (SKY / "night.hdr").exists() or "--sky" in sys.argv:
        print("sky...")
        sky.build(SKY / "night.hdr", MOON_DIR, GLOW_DIR)
    print("ground (coarse)...")
    gc = land.ground((-4400.0, 4400.0, -2400.0, 4400.0), 25.0)
    gfine = {
        "meadow": land.ground((land.MEADOW_X - 200, land.MEADOW_X + 200, land.MEADOW_Z - 200, land.MEADOW_Z + 300), 2.0),
        "bank": land.ground((land.BANK_X0 - 120, land.BANK_X0 + 300, land.BANK_Z - 150, land.BANK_Z + 120), 1.0),
    }
    tw = place_towers(gc)
    for name, (eye, tgt, fov, near, far) in shots(gc, gfine).items():
        sc = scene(tw, eye, tgt, fov, near, far)
        (SHOTS / f"{name}.scene.json").write_text(json.dumps(sc, separators=(",", ":")))
        (SHOTS / f"{name}.json").write_text(json.dumps(project(name, f"{name}.scene.json"), indent=1))
        print(f"  shot {name}: eye {eye} target {tgt}")
    print("wrote", SHOTS)


if __name__ == "__main__":
    main()

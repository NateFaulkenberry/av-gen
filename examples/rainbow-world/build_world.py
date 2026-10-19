"""Rainbow World: build the whole world -- the knot -- and its AV Gen scene and live project.

Run inside Blender (it uses the band's analytic surface and the prototype asset scripts):
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P examples/rainbow-world/build_world.py -- \
      <outdir> [--assets <dir with engine_assets.py output>] [--look look.json] [--quick]

The knot (03-art-direction.md section 3; Stage 4 grey-box E6): a (2,3) torus knot of land, 180 m per unit (a 5.7 km
loop), its band 260 m wide, twisting one whole turn round the loop (ADR-1542) so gravity turns as the camera travels.
The band is swept on the frames AV Gen itself computes for the same spline (knot_path.Path), and the camera rides a
second spline over the meadow whose rolls make its up the band's up (ADR-1541).

Outputs in <outdir>:
  band_<k>.glb                    the band in chunks (culled as single instances)
  rainbow-world.scene.json        the composition
  rainbow-world.json              the live project (no audio file; palette, MIDI, routes)
  world_report.json               counts, lengths, clearances
"""
import argparse
import json
import math
import os
import random
import shutil
import sys

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "blender"))
import band  # noqa: E402
import knot_path  # noqa: E402
import look  # noqa: E402
import rw_common as rw  # noqa: E402
from look import R, av, node_points, point  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--assets", default=None)
ap.add_argument("--look", default=None)
ap.add_argument("--quick", action="store_true", help="coarse band (6 m rings), for layout iterations")
ap.add_argument("--scale", type=float, default=180.0)
ap.add_argument("--zamp", type=float, default=1.0)
ap.add_argument("--turns", type=int, default=1)
ap.add_argument("--chunks", type=int, default=24)
ap.add_argument("--loop-seconds", type=float, default=600.0)
a = ap.parse_args(rw.argv_after_dashes())
OUT = os.path.abspath(a.out)
ASSETS = os.path.abspath(a.assets) if a.assets else OUT
os.makedirs(OUT, exist_ok=True)
LOOK = look.load_look(a.look)
rng = random.Random(2026)

# ---- the path and the band -----------------------------------------------------------------------------------------
controls = knot_path.trefoil_controls(n=72, scale=a.scale, z_amp=a.zamp)
base = knot_path.Path(controls)
ups = []
for i in range(len(controls)):
    k = i * base.per
    ups.append(knot_path._rotate_about(base.rmf[k], base.T[k], 2 * math.pi * a.turns * base.D[k] / base.length))
rolls, path = knot_path.rolls_to_follow(controls, ups)
rw.reset()
band.configure_knot(path, ring_spacing=6.0 if a.quick else 3.0)
fr = band.frames()
L = band.L
print("KNOT length %.0f m, rings %d, roll turns %d" % (L, band.NS, path.roll_turns))


def surf(s, x, lift=0.0):
    return band.surface_point(fr, s % L, x, lift)


def surf_normal(s, x):
    p, t, u, sd = surf(s, x)
    pa, _, _, _ = surf(s + 0.6, x)
    pb, _, _, _ = surf(s, x + 0.6)
    n = (pa - p).cross(pb - p).normalized()
    return n if n.dot(u) > 0 else -n


# chunks
chunk_rings = []
step = band.NS // a.chunks
for c in range(a.chunks):
    i0 = c * step
    i1 = band.NS if c == a.chunks - 1 else (c + 1) * step
    chunk_rings.append((i0, i1))
band_nodes = []
for c, (i0, i1) in enumerate(chunk_rings):
    rw.reset()
    ob, _ = band.build_band("band_%02d" % c, rings=(i0, i1))
    m = bpy.data.materials.new("band")
    m.use_nodes = True
    ob.data.materials.append(m)
    rw.export_glb([ob], os.path.join(OUT, "band_%02d.glb" % c))
    band_nodes.append(node_points("band_%02d" % c, "band_%02d.glb" % c, [point(Vector((0, 0, 0)), Quaternion(), 1.0)],
                                  {"program": "rw_ground", "roughness": 0.93, "metallic": 0.0},
                                  lod={"cull": True}))
print("BAND chunks", len(band_nodes))

# copy the shared assets (rocks, grass, flowers, trees, clouds, rainbow) beside the scene
for f in os.listdir(ASSETS):
    if f.endswith((".glb", ".png")) and not f.startswith(("band_", "lookdev")) and ASSETS != OUT:
        shutil.copy(os.path.join(ASSETS, f), os.path.join(OUT, f))

# ---- the sun and the look ---------------------------------------------------------------------------------------------
SUN_B = Vector((0.35, -0.55, 0.76)).normalized()   # the world's sun (fixed: the orientation anchor)
SUN = Vector(rw.to_avgen(SUN_B))
look.configure(SUN, look.rim_ranges(band))
nodes, programs = list(band_nodes), look.all_programs()

# ---- rim rocks ------------------------------------------------------------------------------------------------------
rw.reset()
protos = band.append_rocks(os.path.join(ASSETS, "..", "stage2", "rocks", "rocks.blend")
                           if not os.path.exists(os.path.join(ASSETS, "rocks.blend")) else os.path.join(ASSETS, "rocks.blend"))
placed = band.place_rim_rocks(fr, protos, seed=5)
by = {}
for o in placed:
    rid = o.name.split(".")[0]
    w, x, y, z = rw.quat_to_avgen(o.rotation_quaternion)
    p = rw.to_avgen(o.location)
    sc = o.scale
    by.setdefault(rid, []).append([round(p[0], 3), round(p[1], 3), round(p[2], 3), round(x, 5), round(y, 5), round(z, 5),
                                   round(w, 5), round(sc[0], 3), round(sc[2], 3), round(sc[1], 3)])
for rid, pts in by.items():
    for k in range(0, len(pts), 60000):
        nodes.append(node_points("rim_%s_%d" % (rid.replace("rock_", ""), k // 60000), "rim" + rid + ".glb", pts[k:k + 60000],
                                 {"program": "rw_rock", "roughness": 0.85, "metallic": 0.0},
                                 castsShadow=False, lod={"cull": True, "minScreenRadius": 1.5}))
print("RIM ROCKS", sum(len(v) for v in by.values()))

# ---- the camera path: weaving over the top meadow --------------------------------------------------------------------
CAM_STEP = 30.0
cam_s = np.arange(0.0, L, CAM_STEP)


def cam_x(s):
    return 0.42 * band.HALF_W * math.sin(2 * math.pi * s / band.per(1150.0)) + 0.18 * band.HALF_W * math.sin(
        2 * math.pi * s / band.per(410.0) + 1.0)


def cam_lift(s):
    return 4.0 + 2.5 * (0.5 + 0.5 * math.sin(2 * math.pi * s / band.per(700.0)))


cam_pts, cam_ups = [], []
for s in cam_s:
    x = cam_x(s)
    p, t, u, sd = surf(s, x, 0.0)
    n = surf_normal(s, x)
    cam_pts.append(np.array(p + n * cam_lift(s)))
    cam_ups.append(np.array(u))
cam_controls = np.array(cam_pts)
cam_rolls, cam_path = knot_path.rolls_to_follow(cam_controls, cam_ups)
cam_node = cam_path.to_avgen_json("ride", rw.to_avgen)
nodes.append(cam_node)
print("CAMERA path %.0f m, roll turns %d" % (cam_path.length, cam_path.roll_turns))


def near_path(p, clearance):
    d = np.linalg.norm(cam_controls - np.array(p), axis=1)
    return d.min() < clearance


def path_clear(s, x, clearance=20.0):
    """True when the meadow point (s, x) is at least `clearance` metres across from the ride (so nothing the
    camera flies through is planted there): the ride's lateral position at the same station."""
    return abs(x - cam_x(s)) >= clearance


# ---- grass: a corridor round the ride ------------------------------------------------------------------------------
gpts = []
for s in np.arange(0.0, L, 1.0):
    xc = cam_x(s)
    for _ in range(int(rng.uniform(70, 90))):  # ~80 clumps per metre of path across a +-36 m corridor (~1.1/m^2)
        x = xc + rng.gauss(0, 18.0)
        if abs(x) > 0.8 * band.HALF_W:
            continue
        ss = s + rng.uniform(0, 1.0)
        p, t, u, sd = surf(ss, x, 0.0)
        n = surf_normal(ss, x)
        sc = rng.uniform(0.55, 0.95)
        gpts.append(point(p - n * 0.03, n.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.283)),
                          (sc, sc, sc * rng.uniform(0.8, 1.2))))
for k in range(0, len(gpts), 60000):
    nodes.append(node_points("grass%03d" % (k // 60000), "grass_lite.glb", gpts[k:k + 60000],
                             {"program": "rw_grass", "roughness": 0.95, "metallic": 0.0}, castsShadow=False,
                             lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 70.0}))
print("GRASS", len(gpts))

# ---- flowers: drifts beside the ride -------------------------------------------------------------------------------
fpts = []
for s0 in np.arange(0.0, L, 22.0):
    if rng.random() < 0.35:
        continue
    cs, cx = s0 + rng.uniform(0, 20), cam_x(s0) + rng.choice((-1, 1)) * rng.uniform(6, 28)
    for _ in range(rng.randint(30, 80)):
        s, x = cs + rng.gauss(0, 3.5), cx + rng.gauss(0, 3.5)
        if abs(x) > 0.8 * band.HALF_W:
            continue
        p, t, u, sd = surf(s, x, 0.0)
        fpts.append(point(p - u * 0.02, u.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.283)),
                          rng.uniform(0.8, 1.4)))
for k in range(0, len(fpts), 60000):
    nodes.append(node_points("flowers%02d" % (k // 60000), "flower_clump.glb", fpts[k:k + 60000],
                             {"program": "rw_flower", "roughness": 0.6, "metallic": 0.0, "doubleSided": True},
                             castsShadow=False, lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 90.0}))
print("FLOWERS", len(fpts))

# ---- trees: landmark Great Trees, clusters of small Great Trees and Blossom Puffs on hill shoulders, Weeper groves
# in the dells (03-art-direction.md section 6: a family of distinct silhouettes, each placed where its shape belongs)
fam = {"great": [], "weeper": [], "blossom": [], "pine": []}
landmarks = []
for k in range(9):  # one Great Tree every ~640 m, beside the ride: the landmarks the journey keeps passing
    s = (k + 0.5) * L / 9 + rng.uniform(-60, 60)
    side_ = rng.choice((-1, 1))
    x = cam_x(s) + side_ * rng.uniform(28, 45)
    if abs(x) > 0.75 * band.HALF_W:  # no room on that side: the other
        x = cam_x(s) - side_ * rng.uniform(28, 45)
    p, t, u, sd = surf(s, x, -0.8)
    landmarks.append((s, x))
    fam["great"].append(point(p, u.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.28)), rng.uniform(0.95, 1.2)))
for hs, hx, h, *_ in band.TOP_HILLS:
    if h >= 8 and rng.random() < 0.75:  # shoulders of the taller hills: small Great Trees, now and then a Blossom
        for _ in range(rng.randint(3, 6)):
            s, x = hs + rng.gauss(0, 14), hx + rng.gauss(0, 10)
            if abs(x) > 0.78 * band.HALF_W or not path_clear(s, x, 22.0):
                continue
            p, t, u, sd = surf(s, x, -0.5)
            q = u.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.28))
            if rng.random() < 0.18:
                fam["blossom"].append(point(p, q, rng.uniform(0.45, 0.6)))
            else:
                fam["great"].append(point(p, q, rng.uniform(0.3, 0.55)))
    elif h < 0:  # the dells: a small grove of Weepers, on the dell's far side from the ride
        away = 1.0 if hx >= cam_x(hs) else -1.0
        for _ in range(rng.randint(3, 5)):
            s, x = hs + rng.gauss(0, 14), cam_x(hs) + away * rng.uniform(30, 60)
            if abs(x) > 0.78 * band.HALF_W or not path_clear(s, x, 24.0):
                continue
            p, t, u, sd = surf(s, x, -0.4)
            fam["weeper"].append(point(p, u.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.28)), rng.uniform(0.75, 1.05)))
for hs, hx, h, rs, rx, *_ in band.TOP_HILLS:  # the long ridges: Ridge Pines strung along the spine
    if rs > 90:
        for k in range(rng.randint(6, 10)):
            s, x = hs + rng.uniform(-0.8, 0.8) * rs, hx + rng.gauss(0, 5)
            if not path_clear(s, x, 16.0):
                continue
            p, t, u, sd = surf(s, x, -0.5)
            fam["pine"].append(point(p, u.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.28)), rng.uniform(0.6, 0.95)))
TREE_ASSETS = {"great": ("great_tree", "rw_canopy"), "weeper": ("weeper", "rw_weeper"), "blossom": ("blossom", "rw_blossom"),
               "pine": ("pine", "rw_pine")}
for f, pts in fam.items():
    if not pts:
        continue
    asset, prog = TREE_ASSETS[f]
    nodes.append(node_points("%s_wood" % f, "%s_wood.glb" % asset, pts, {"baseColor": R(rw.P["bark"][:3]), "roughness": 0.9, "metallic": 0.0},
                             lod={"cull": True, "minScreenRadius": 1.0}))
    nodes.append(node_points("%s_core" % f, "%s_core.glb" % asset, pts, {"baseColor": R(rw.P["canopy_core"][:3]), "roughness": 0.95, "metallic": 0.0},
                             lod={"cull": True, "minScreenRadius": 1.0}))
    nodes.append(node_points("%s_leaves" % f, "%s_leaves.glb" % asset, pts,
                             {"program": prog, "roughness": 0.75, "metallic": 0.0, "alphaMode": "mask",
                              "alphaCutoff": 0.5, "doubleSided": True}, castsShadow=False,
                             lod={"cull": True, "minScreenRadius": 1.0}))
tpts = [p for v in fam.values() for p in v]
print("TREES", {k: len(v) for k, v in fam.items()})

# ---- rainbows: arches across the band at landmarks ------------------------------------------------------------------
rainbow_nodes = []
for k, (s, x) in enumerate(landmarks[::2]):
    s_arch = s + 140.0
    i = int(round((s_arch % L) / L * band.NS))
    _, pa, ta, ua, sa = fr[i]
    m = Matrix((sa, ta, ua)).transposed()
    eul = (rw.Q_ZUP_TO_YUP @ m.to_quaternion() @ rw.Q_ZUP_TO_YUP.inverted()).to_euler("XYZ")
    rainbow_nodes.append({"name": "rainbow_%d" % k, "kind": "gltf", "asset": "rainbow_arch.glb",
                          "position": av(pa + ua * (band.FACE - 6.0)),
                          "rotation": [round(math.degrees(v), 3) for v in eul]})
nodes += rainbow_nodes
print("RAINBOWS", len(rainbow_nodes))

# ---- clouds: masses in the knot's free space (clear of the ride), a sea below, banks far out -----------------------
cpts = {"hero": [], "tower": [], "bank": [], "puff": []}
P = np.array(path.P[::6])
lo, hi = P.min(axis=0), P.max(axis=0)
centre = (lo + hi) / 2
extent = float(np.linalg.norm(hi - lo))
tries = 0
HALF = {"hero": 330.0, "tower": 290.0}   # half the mesh's largest extent (engine_assets CLOUD report), metres
while len(cpts["hero"]) + len(cpts["tower"]) < 14 and tries < 6000:
    tries += 1
    c = centre + np.array([rng.uniform(-0.75, 0.75), rng.uniform(-0.75, 0.75), rng.uniform(-0.45, 0.6)]) * extent * 0.6
    kind = "tower" if rng.random() < 0.4 else "hero"
    sc = rng.uniform(0.5, 0.9)
    reach = HALF[kind] * sc
    # never in the camera's way, never through the land: the cloud's own reach plus a margin from both
    if np.linalg.norm(P - c, axis=1).min() < reach + band.HALF_W + 60 or near_path(c, reach + 180):
        continue
    cpts[kind].append(point(Vector(c), Quaternion((0, 0, 1), rng.uniform(0, 6.28)), sc))
sea_z = lo[2] - 380.0
for k in range(260):  # the cloud sea, out to 6 km
    aa, rr = rng.uniform(0, 2 * math.pi), 6000 * math.sqrt(rng.random())
    c = Vector((centre[0] + rr * math.cos(aa), centre[1] + rr * math.sin(aa), sea_z - rng.uniform(0, 160)))
    cpts["puff"].append(point(c, Quaternion((0, 0, 1), rng.uniform(0, 6.28)),
                              (rng.uniform(2.6, 4.2), rng.uniform(2.6, 4.2), rng.uniform(0.6, 1.0))))
for k in range(16):  # banks on a ring round the knot
    aa = 2 * math.pi * k / 16 + rng.uniform(-0.1, 0.1)
    c = Vector((centre[0] + 3800 * math.cos(aa), centre[1] + 3800 * math.sin(aa), centre[2] + rng.uniform(-300, 500)))
    cpts["bank"].append(point(c, Quaternion((0, 0, 1), rng.uniform(0, 6.28)),
                              (rng.uniform(2.0, 3.0), rng.uniform(1.4, 2.2), rng.uniform(1.4, 2.4))))
for kind, pts in cpts.items():
    if pts:
        nodes.append(node_points("cloud_" + kind, "cloud_%s.glb" % kind, pts,
                                 {"program": "rw_cloud", "roughness": 1.0, "metallic": 0.0, "unlit": True},
                                 castsShadow=False, lod={"cull": True}))
print("CLOUDS", {k: len(v) for k, v in cpts.items()})

# ---- scene and project ----------------------------------------------------------------------------------------------
env, lights = look.environment(SUN, rw.to_avgen(Vector((0, 0, 1))))
scene = {"format": "avgen-scene", "version": 1, "name": "rainbow-world",
         "camera": {"mode": 2, "spline": "ride", "fov": 100.0, "orbitSpeed": 0.0},
         "environment": env, "lights": lights, "materialPrograms": programs, "nodes": nodes}
with open(os.path.join(OUT, "rainbow-world.scene.json"), "w") as f:
    json.dump(scene, f)
params = look.post_parameters()
params.update({"camera/splineUp": 1.0, "camera/lookAhead": 40.0, "post/lens/fisheye": 1.0, "post/lens/fisheyeModel": 0.0,
               "camera/far": 30000.0, "sources/ride/rate": 1.0 / a.loop_seconds})
project = {
    "format": "avgen-project", "version": 4, "app": {"name": "Rainbow World"},
    "assets": {"scene": {"kind": "composition", "path": "rainbow-world.scene.json"}},
    "sonic": {"live": True},
    "parameters": params,
    "sources": [{"kind": "lfo", "name": "ride", "settings": {"shape": "saw"}}],
    "routes": [{"source": "lfo.ride", "target": "camera/splineT", "amount": 1.0, "op": "replace",
                "polarity": "unipolar"}],
    "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/rainbow-world.mp4"},
}
with open(os.path.join(OUT, "rainbow-world.json"), "w") as f:
    json.dump(project, f, indent=1)
report = {"loopMetres": L, "cameraMetres": cam_path.length, "loopSeconds": a.loop_seconds,
          "grass": len(gpts), "flowers": len(fpts), "trees": len(tpts), "rimRocks": sum(len(v) for v in by.values()),
          "rainbows": len(rainbow_nodes), "clouds": {k: len(v) for k, v in cpts.items()}, "bandChunks": len(band_nodes)}
with open(os.path.join(OUT, "world_report.json"), "w") as f:
    json.dump(report, f, indent=1)
print("done", json.dumps(report))

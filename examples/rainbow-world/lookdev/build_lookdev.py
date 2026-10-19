"""Rainbow World Stage 3: the look-development scene in AV Gen.

Run inside Blender (it reuses the band's analytic surface for exact placement on the twisted section):
  Blender -b --factory-startup -P build_lookdev.py -- <assets dir (engine_assets.py output)> [look json]

Writes into the assets dir: lookdev.scene.json, and one project per controlled view (lookdev_<view>.json):
  wide       the environmental view along the band, from above the right rim
  close      the terrain and vegetation, low on the meadow under the Great Tree
  rainbow    the arch across the band, from the meadow in front of it
  clouds     the cloud banks and the cloud sea, from the band's edge looking out
  fisheye    the wide-angle view on the surface (rectilinear 110 degrees until ADR-1540)

Everything colour-related lives in LOOK (overridable by a JSON file), so look iterations do not touch placement.
"""
import json
import math
import os
import random
import sys

from mathutils import Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "blender"))
import band  # noqa: E402
import look  # noqa: E402
import rw_common as rw  # noqa: E402
from look import R, av, point, node_points  # noqa: E402

args = rw.argv_after_dashes()
OUT = os.path.abspath(args[0])
LOOK = look.load_look(args[1] if len(args) > 1 else None)

fr = band.frames()
def frame_at(s):
    return fr[max(0, min(band.NS, int(round(s / band.L * band.NS))))]


def surf(s, x, lift=0.0):
    return band.surface_point(fr, s, x, lift)


_, PM, TM, UM, SM = frame_at(band.L / 2)
SUN_B = (0.30 * UM + 0.80 * SM - 0.50 * TM).normalized()  # Stage 3 v6: lower, raking, warmer
SUN = Vector(rw.to_avgen(SUN_B))

look.configure(SUN, look.rim_ranges(band))

nodes, programs = [], look.all_programs()
# rim color for the cloud program's silver rim (r6 must hold it): prepend a constant

# the band
nodes.append(node_points("band", "band_section.glb", [point(Vector((0, 0, 0)), Quaternion(), 1.0)],
                         {"program": "rw_ground", "roughness": 0.93, "metallic": 0.0}))
# rim rocks
rim_pts = json.load(open(os.path.join(OUT, "rim_rocks.points.json")))
for rid, pts in rim_pts.items():
    nodes.append(node_points("rim_" + rid.replace("rock_", ""), rid + ".glb", pts,
                             {"program": "rw_rock", "roughness": 0.85, "metallic": 0.0}))

# grass: a near field round the camera stations (the turf colour carries the rest)
rng = random.Random(4)
stations = [(40.0, -50.0, 36.0), (95.0, -30.0, 30.0), (150.0, 10.0, 30.0), (250.0, 60.0, 26.0)]
gpts = []
for s0, x0, rad in stations:
    n = int(rad * rad * math.pi * 7.0)
    for _ in range(n):
        a, rr = rng.uniform(0, 2 * math.pi), rad * math.sqrt(rng.random())
        s, x = s0 + rr * math.cos(a), x0 + rr * math.sin(a)
        if not (2 < s < band.L - 2 and abs(x) < 0.8 * band.HALF_W):
            continue
        p, t, u, sd = surf(s, x, 0.0)
        pa, _, _, _ = surf(s + 0.5, x)
        pb, _, _, _ = surf(s, x + 0.5)
        nrm = (pa - p).cross(pb - p).normalized()
        if nrm.dot(u) < 0:
            nrm = -nrm
        q = nrm.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.283))
        sc = rng.uniform(0.55, 0.95)
        gpts.append(point(p - nrm * 0.03, q, (sc, sc, sc * rng.uniform(0.8, 1.2))))
for k in range(0, len(gpts), 60000):
    nodes.append(node_points("grass%d" % (k // 60000), "grass_lite.glb", gpts[k:k + 60000],
                             {"program": "rw_grass", "roughness": 0.95, "metallic": 0.0},
                             castsShadow=False, lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 70.0}))
print("GRASS", len(gpts))

# flower drifts: clusters along the camera stations' edges, never a uniform sprinkle
fpts = []
for s0, x0, rad in stations:
    for d in range(9):
        a = rng.uniform(0, 6.283)
        cs, cx = s0 + rad * rng.uniform(0.3, 0.9) * math.cos(a), x0 + rad * rng.uniform(0.3, 0.9) * math.sin(a)
        for _ in range(rng.randint(40, 90)):
            s, x = cs + rng.gauss(0, 3.0), cx + rng.gauss(0, 3.0)
            if not (2 < s < band.L - 2 and abs(x) < 0.8 * band.HALF_W):
                continue
            p, t, u, sd = surf(s, x, 0.0)
            sc = rng.uniform(0.8, 1.4)
            fpts.append(point(p - u * 0.02, u.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.283)), sc))
nodes.append(node_points("flowers", "flower_clump.glb", fpts, {"program": "rw_flower", "roughness": 0.6, "metallic": 0.0,
                                                               "doubleSided": True},
                         castsShadow=False, lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 90.0}))
print("FLOWERS", len(fpts))

# Great Trees: wood casts, the leaf cards do not, the canopy core casts and fills
tpts = []
for (s, x, yaw, scale) in ((95, -40, 0.4, 1.0), (180, 70, 2.1, 0.85), (300, -75, 4.0, 1.1), (70, 85, 1.0, 0.7),
                           (360, 60, 5.0, 0.9)):
    p, t, u, sd = surf(s, x, -0.8)
    tpts.append(point(p, u.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), yaw), scale))
for (cs, cx) in ((250, 40), (365, -62), (150, 75), (60, 70)):  # clusters on the authored hills' shoulders
    for _ in range(rng.randint(3, 5)):
        s, x = cs + rng.gauss(0, 9), cx + rng.gauss(0, 7)
        p, t, u, sd = surf(s, x, -0.5)
        tpts.append(point(p, u.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.28)), rng.uniform(0.32, 0.55)))
nodes.append(node_points("tree_wood", "great_tree_wood.glb", tpts, {"baseColor": R(rw.P["bark"][:3]), "roughness": 0.9, "metallic": 0.0}))
nodes.append(node_points("tree_core", "great_tree_core.glb", tpts, {"baseColor": R(rw.P["canopy_core"][:3]), "roughness": 0.95, "metallic": 0.0}))
nodes.append(node_points("tree_leaves", "great_tree_leaves.glb", tpts,
                         {"program": "rw_canopy", "roughness": 0.75, "metallic": 0.0, "alphaMode": "mask",
                          "alphaCutoff": 0.5, "doubleSided": True}, castsShadow=False))

# boulders near the first tree
bpts = []
for k in range(7):
    p, t, u, sd = surf(70 + rng.uniform(-20, 30), -60 + rng.uniform(-25, 25), -0.5)
    sc = rng.uniform(2.5, 6.5)
    bpts.append(point(p, u.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.28)), sc))
nodes.append(node_points("boulders", "rock_namaqualand_boulder_03.glb", bpts, {"program": "rw_rock", "roughness": 0.85, "metallic": 0.0}))

# clouds: hero masses beside and above, banks round the horizon, a sea of puffs far below
cpts = {"hero": [], "tower": [], "bank": [], "puff": []}
down = -UM
def cloud_at(kind, p, yaw, sc):
    cpts[kind].append(point(p, Quaternion((0, 0, 1), yaw), sc))
cloud_at("hero", PM + SM * 620 + UM * 60 + TM * 180, 0.3, 1.0)
cloud_at("tower", PM - SM * 700 + UM * 40 + TM * 420, 2.0, 1.1)
cloud_at("hero", PM + SM * 300 + UM * 520 + TM * 900, 4.0, 1.4)
for k in range(10):  # banks on a wide ring, slightly below the band's plane
    a = 2 * math.pi * k / 10 + 0.2
    d = SM * math.cos(a) + TM * math.sin(a)
    cloud_at("bank", PM + d * rng.uniform(1800, 2600) + UM * rng.uniform(-250, 150), rng.uniform(0, 6.28),
             (rng.uniform(1.4, 2.2), rng.uniform(1.0, 1.6), rng.uniform(0.9, 1.4)))
for k in range(150):  # the cloud sea: flattened puffs 450-650 m below, out to 3.4 km
    a, rr = rng.uniform(0, 2 * math.pi), 3400 * math.sqrt(rng.random())
    d = SM * math.cos(a) + TM * math.sin(a)
    cloud_at("puff", PM + d * max(rr, 500.0) - UM * rng.uniform(620, 820), rng.uniform(0, 6.28),
             (rng.uniform(2.4, 4.0), rng.uniform(2.4, 4.0), rng.uniform(0.6, 1.0)))
for kind, pts in cpts.items():
    nodes.append(node_points("cloud_" + kind, "cloud_%s.glb" % kind, pts,
                             {"program": "rw_cloud", "roughness": 1.0, "metallic": 0.0, "unlit": True}, castsShadow=False))

# (Stage 3 v4: the distant band 'echoes' are removed: a slab of land with an edge, floating far off, is a visible
# edge of the world. The real knot's strands fill those directions in Stage 4.)

# the rainbow arch across the band at s = 210 (a glTF entity: alpha-blended, U6)
_, PA, TA, UA, SA = frame_at(210.0)
from mathutils import Matrix  # noqa: E402
m = Matrix((SA, TA, UA)).transposed()   # arch XZ plane -> (side, up); its local Y (tube depth) -> along the band
q_arch = m.to_quaternion()
arch_pos = PA + UA * (band.FACE - 6.0)
eul = (rw.Q_ZUP_TO_YUP @ q_arch @ rw.Q_ZUP_TO_YUP.inverted()).to_euler("XYZ")
nodes.append({"name": "rainbow", "kind": "gltf", "asset": "rainbow_arch.glb", "position": av(arch_pos),
              "rotation": [round(math.degrees(v), 3) for v in eul]})

scene = {
    "format": "avgen-scene", "version": 1, "name": "rainbow-lookdev",
    "camera": {"mode": 1, "position": [0, 0, 0], "target": [0, 0, -1], "fov": 60.0, "orbitSpeed": 0.0},
    "environment": {
        "intensity": 1.0, "skyIntensity": LOOK["skyIntensity"], "shadowRange": 320.0, "ambientOcclusion": True,
        "contactShadows": False, "depthPrepass": True, "fogColor": R(LOOK["horizon"]), "volumeDensity": 0.0,
        "sky": {"enabled": True, "background": True, "useKeyLight": True, "zenithColor": R(LOOK["zenith"]),
                "horizonColor": R(LOOK["horizon"]), "groundColor": R(LOOK["ground"]), "haze": LOOK["haze"],
                "sunIntensity": 6.0, "intensity": 1.0}},
    "lights": [
        {"name": "sun", "type": "directional", "direction": R(-SUN), "color": R(LOOK["sunColor"]),
         "intensity": LOOK["sun"], "castsShadow": True, "contactShadow": False, "shadowBias": 0.1},
        {"name": "cloudsea", "type": "directional", "direction": R(Vector(rw.to_avgen(UM)) * 1.0),
         "color": R(rw.P["cloud_bounce"][:3]), "intensity": LOOK["bounce"], "castsShadow": False}],
    "materialPrograms": programs,
    "nodes": nodes,
}
with open(os.path.join(OUT, "lookdev.scene.json"), "w") as f:
    json.dump(scene, f)


def view(name, eye_b, tgt_b, fov, up_b=None):
    d = (tgt_b - eye_b).normalized()
    tgt_b = eye_b + d * 15.0  # AO's radius is 5% of the eye-to-target distance: keep it at tuft scale
    cam = {"mode": 1, "position": av(eye_b), "target": av(tgt_b), "fov": fov, "orbitSpeed": 0.0}
    sc = json.loads(json.dumps(scene))
    sc["camera"] = cam
    with open(os.path.join(OUT, "lookdev_%s.scene.json" % name), "w") as f:
        json.dump(sc, f)
    proj = {"format": "avgen-project", "version": 4, "app": {"name": "Rainbow World look-dev"},
            "assets": {"scene": {"kind": "composition", "path": "lookdev_%s.scene.json" % name}},
            "parameters": {"camera/exposure/compensation": LOOK["exposure"], "post/grade/saturation": LOOK["saturation"],
                           "post/grade/contrast": LOOK["contrast"], "post/tonemap/chroma-retention": LOOK["chroma"],
                           "post/look/atmospheric": LOOK["atmos"], "post/look/atmosphericDistance": LOOK["atmosDist"],
                           "post/bloom/intensity": 0.18, "post/bloom/threshold": 1.2, "post/output/antialias": 0.8},
            "render": {"width": 1920, "height": 1080, "fps": 30, "output": "png", "path": "renders/" + name}}
    with open(os.path.join(OUT, "lookdev_%s.json" % name), "w") as f:
        json.dump(proj, f)
    print("VIEW", name)


_, P0, T0, U0, S0 = frame_at(0.0)
view("wide", P0 + U0 * 110 - T0 * 160 - S0 * 170, PA + UA * 40 + TA * 40, 62)
e, t, u, sd = surf(28.0, -62.0, 1.7)
tgt, _, _, _ = surf(95.0, -40.0, 9.0)
view("close", e, tgt, 70)
e, _, _, _ = surf(120.0, 10.0, 2.0)
view("rainbow", e, arch_pos + UA * 70, 75)
e, _, _, _ = surf(250.0, 105.0, 6.0)
view("clouds", e, e + SM * 600 + UM * 80 + TM * 300, 75)
e, _, _, _ = surf(40.0, -50.0, 2.0)
tgt, _, _, _ = surf(200.0, 0.0, 40.0)
view("fisheye", e, tgt, 90)
print("done")

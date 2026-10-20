"""Stage 2 experiments E4 (live cost of the lobe clouds) and E5 (live cost of grass), in AV Gen.

Run inside Blender (it reuses the band's analytic surface for exact grass placement):
  Blender -b --factory-startup -P build_e45.py -- <outdir> <band_dir> <cloud_dir>
<band_dir> must hold band_section.glb (band.py); <cloud_dir> hero_cloud.lobes.json (clouds.py).

Writes into <outdir>: grass_clump.glb, the scene variants (*.scene.json) and one project per variant
(*.json). Every variant shares one camera, sun, sky and band; only the thing under test changes:
  base          band only
  cloud1        + one hero cloud mass (about 420 lobes)
  cloud4        + four masses (about 1,700 lobes)
  grass50k / grass150k / grass300k   + that many grass clumps on the top face (GPU cull, min screen radius)
"""
import json
import math
import os
import random
import shutil
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "blender"))
import band  # noqa: E402
import rw_common as rw  # noqa: E402

args = rw.argv_after_dashes()
OUT = os.path.abspath(args[0])
BAND_DIR = os.path.abspath(args[1])
CLOUD_DIR = os.path.abspath(args[2])
os.makedirs(OUT, exist_ok=True)

rw.reset()
fr = band.frames()

# ---- grass clump mesh ------------------------------------------------------------------------------------------
clump = band.grass_clump()
bpy.context.scene.collection.objects.link(clump)
rw.export_glb([clump], os.path.join(OUT, "grass_clump.glb"))
lite = band.grass_clump(name="grass_lite", blades=6, seed=5, segs=2)
bpy.context.scene.collection.objects.link(lite)
rw.export_glb([lite], os.path.join(OUT, "grass_lite.glb"))
print("CLUMP TRIS", sum(len(p.vertices) - 2 for p in clump.data.polygons), "LITE TRIS", sum(len(p.vertices) - 2 for p in lite.data.polygons))
shutil.copy(os.path.join(BAND_DIR, "band_section.glb"), os.path.join(OUT, "band_section.glb"))
shutil.copy(os.path.join(CLOUD_DIR, "hero_cloud.lobes.json"), os.path.join(OUT, "hero_cloud.lobes.json"))
shutil.copy(os.path.join(CLOUD_DIR, "hero_cloud_mesh.glb"), os.path.join(OUT, "hero_cloud_mesh.glb"))


def top_point(s, x):
    p, t, u, side = band.surface_point(fr, s, x, 0.0)
    return p, t, u, side


def grass_points(n, seed=1):
    rng = random.Random(seed)
    pts = []
    while len(pts) < n:
        s = rng.uniform(2, band.L - 2)
        x = rng.uniform(-0.82, 0.82) * band.HALF_W
        p, t, u, side = top_point(s, x)
        e = 0.6
        pa, _, _, _ = top_point(s + e, x)
        pb, _, _, _ = top_point(s, x + e)
        nrm = (pa - p).cross(pb - p)
        nrm = nrm.normalized() if nrm.length > 1e-9 else u
        if nrm.dot(u) < 0:
            nrm = -nrm
        q = nrm.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 2 * math.pi))
        sc = rng.uniform(0.75, 1.35)
        a = rw.to_avgen(p)
        w, x_, y_, z_ = rw.quat_to_avgen(q)
        pts.append([round(a[0], 3), round(a[1], 3), round(a[2], 3), round(x_, 5), round(y_, 5), round(z_, 5), round(w, 5),
                    round(sc, 3), round(sc, 3), round(sc, 3)])
    return pts


# ---- shared scene parts ----------------------------------------------------------------------------------------
_, pm, tm, um, sm = fr[band.NS // 2]
sun_dir_blender = (0.45 * um + 0.75 * sm - 0.45 * tm).normalized()   # toward the sun
sun_av = Vector(rw.to_avgen(sun_dir_blender))
eye_b, t0, u0, s0 = band.surface_point(fr, 40.0, -30.0, 2.2)
tgt_b, _, _, _ = band.surface_point(fr, 140.0, -10.0, 14.0)
L = lambda c: [round(v, 5) for v in c[:3]]  # noqa: E731

scene_base = {
    "format": "avgen-scene", "version": 1, "name": "rainbow-e45",
    "camera": {"mode": 1, "position": [round(v, 3) for v in rw.to_avgen(eye_b)],
               "target": [round(v, 3) for v in rw.to_avgen(tgt_b)], "fov": 70.0, "orbitSpeed": 0.0},
    "environment": {
        "intensity": 1.0, "skyIntensity": 1.0, "shadowRange": 300.0, "ambientOcclusion": True, "contactShadows": True,
        "depthPrepass": True,
        "sky": {"enabled": True, "background": True, "useKeyLight": True,
                "zenithColor": L(rw.P["zenith"]), "horizonColor": L(rw.P["haze"]), "groundColor": L(rw.P["cloud_sea"]),
                "haze": 0.5, "sunIntensity": 6.0, "intensity": 1.0},
        "fogColor": L(rw.P["haze"]), "volumeDensity": 0.0,
    },
    "lights": [
        {"name": "sun", "type": "directional", "direction": [round(-v, 5) for v in sun_av], "color": L(rw.P["sun"]),
         "intensity": 4.0, "castsShadow": True, "contactShadow": True},
        {"name": "cloudsea", "type": "directional", "direction": [0.1, 0.95, -0.2], "color": L(rw.P["cloud_bounce"]),
         "intensity": 0.6, "castsShadow": False},
    ],
    "nodes": [
        {"name": "band", "kind": "procedural", "procedural": {
            "source": {"kind": "mesh", "asset": "band_section.glb"}, "distribution": {"kind": "single"},
            "material": {"baseColor": L(rw.P["meadow"]), "roughness": 0.92, "metallic": 0.0}}},
    ],
}


def clouds_block(masses):
    return {"lobes": "hero_cloud.lobes.json", "masses": masses, "extinction": 0.006, "steps": 96, "lightSteps": 6,
            "lightDistance": 400.0, "lightDirection": [round(v, 5) for v in sun_av], "lightColor": L(rw.P["sun"]),
            "lightIntensity": 3.0, "anisotropy": 0.55, "ambientTop": [0.35, 0.5, 0.9], "ambientBottom": [0.9, 0.75, 0.6],
            "warmColor": [0.6, 0.4, 0.25], "warmDirection": [round(v, 5) for v in sun_av], "warmHeight": 400.0,
            "noiseScale": 0.008, "noiseAmount": 0.45, "hazeColor": L(rw.P["haze"]), "hazeDistance": 6000.0,
            "powder": 0.6, "seed": 1}


def write(name, scene):
    with open(os.path.join(OUT, name + ".scene.json"), "w") as f:
        json.dump(scene, f, indent=1)
    proj = {"format": "avgen-project", "version": 4, "app": {"name": "Rainbow World E4/E5"},
            "assets": {"scene": {"kind": "composition", "path": name + ".scene.json"}},
            "render": {"width": 1920, "height": 1080, "fps": 30, "output": "png", "path": "renders/" + name}}
    with open(os.path.join(OUT, name + ".json"), "w") as f:
        json.dump(proj, f, indent=1)
    print("WROTE", name)


import copy  # noqa: E402

# cloud masses: the hero mass placed beside the band, then three more copies around it
with open(os.path.join(OUT, "hero_cloud.lobes.json")) as f:
    lobes = json.load(f)["masses"]["hero_a"]
centre_b = pm + sm * 700 + um * 150 + tm * 300
offsets = [centre_b, pm - sm * 800 + um * 300 + tm * 100, pm + um * 900 + tm * 400, pm - um * 600 + sm * 200 + tm * 500]
masses = {}
for k, off in enumerate(offsets):
    oa = rw.to_avgen(off)
    masses["m%d" % k] = [dict(l, c=[l["c"][0] + oa[0], l["c"][1] + oa[1], l["c"][2] + oa[2]]) for l in lobes]
with open(os.path.join(OUT, "hero_cloud.lobes.json"), "w") as f:
    json.dump({"masses": masses}, f)

write("base", scene_base)
# the cloud variants look at the hero mass from 950 m so it covers about a third of the frame (a realistic
# share for the scene); the no-cloud control with the same camera is "cloudcam"
cloud_eye = centre_b - sm * 950 - um * 120 + tm * 120
ccam = {"mode": 1, "position": [round(v, 3) for v in rw.to_avgen(cloud_eye)],
        "target": [round(v, 3) for v in rw.to_avgen(centre_b + um * 120)], "fov": 70.0, "orbitSpeed": 0.0}
sc0 = copy.deepcopy(scene_base)
sc0["camera"] = ccam
write("cloudcam", sc0)
s1 = copy.deepcopy(sc0)
s1["clouds"] = clouds_block(["m0"])
write("cloud1", s1)
s4 = copy.deepcopy(sc0)
s4["clouds"] = clouds_block(["m0", "m1", "m2", "m3"])
write("cloud4", s4)
s1l = copy.deepcopy(s1)
s1l["clouds"]["steps"] = 32
s1l["clouds"]["lightSteps"] = 2
write("cloud1lite", s1l)
scm = copy.deepcopy(sc0)
oa = rw.to_avgen(centre_b)
scm["nodes"].append({"name": "cloudmesh", "kind": "procedural", "position": [round(v, 3) for v in oa],
    "procedural": {
    "source": {"kind": "mesh", "asset": "hero_cloud_mesh.glb"}, "distribution": {"kind": "single"},
    "material": {"baseColor": [0.95, 0.95, 1.0], "roughness": 1.0, "metallic": 0.0}}})
write("cloudmesh", scm)
allpts = grass_points(300000)
sgl = copy.deepcopy(scene_base)
for k in range(2):
    sgl["nodes"].append({"name": "grasslite%d" % k, "kind": "procedural", "procedural": {
        "source": {"kind": "mesh", "asset": "grass_lite.glb"},
        "distribution": {"kind": "points", "points": allpts[k * 30000:(k + 1) * 30000]},
        "material": {"baseColor": L(rw.P["meadow"]), "roughness": 0.95, "metallic": 0.0},
        "lod": {"cull": True, "minScreenRadius": 1.0, "maxDistance": 90.0}}})
write("grasslite60k", sgl)
# U7: the Great Tree in AV Gen (wood + leaf cards with hull normals; canopy colour from a material program)
TREE_DIR = os.path.join(os.path.dirname(BAND_DIR), "tree")
for part in ("wood", "leaves"):
    shutil.copy(os.path.join(TREE_DIR, "great_tree_%s.glb" % part), os.path.join(OUT, "great_tree_%s.glb" % part))
tpts = []
for (s_, x_, yaw, scale) in ((95, -40, 0.4, 1.0), (180, 70, 2.1, 0.85), (70, 85, 1.0, 0.7), (52, -62, 3.0, 0.8)):
    p, t, u, side = band.surface_point(fr, s_, x_, -0.8)
    q = u.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), yaw)
    a = rw.to_avgen(p)
    w_, qx, qy, qz = rw.quat_to_avgen(q)
    tpts.append([a[0], a[1], a[2], qx, qy, qz, w_, scale, scale, scale])
st = copy.deepcopy(scene_base)
st["environment"]["contactShadows"] = False
for l_ in st["lights"]:
    l_["contactShadow"] = False
st["materialPrograms"] = [{"name": "canopy", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1,
                           "emissionIntensity": 1.0, "opacity": -1, "ops": [
    {"kind": "input", "dst": 0, "input": "objectPosition"},
    {"kind": "gradient", "dst": 1, "srcA": 0, "constant": [0.0, 1.0 / 14.0, 0.0, -0.75], "value": 1.0},
    {"kind": "input", "dst": 4, "input": "worldPosition"},
    {"kind": "noise", "dst": 2, "srcA": 4, "value": 0.35, "seed": 3},
    {"kind": "mix", "dst": 1, "srcA": 1, "srcB": 2, "value": 0.35},
    {"kind": "oklabRamp", "dst": 3, "srcA": 1, "value": 1.0,
     "constant": L(rw.P["canopy_core"]) + [1.0], "constant2": L(rw.hexlin("#3A7A3A")) + [1.0],
     "constant3": L(rw.P["canopy_lit"]) + [1.0], "constant4": L(rw.hexlin("#93CF4A")) + [1.0]}]}]
st["nodes"].append({"name": "treewood", "kind": "procedural", "procedural": {
    "source": {"kind": "mesh", "asset": "great_tree_wood.glb"}, "distribution": {"kind": "points", "points": tpts},
    "material": {"baseColor": L(rw.P["bark"]), "roughness": 0.9, "metallic": 0.0}}})
st["nodes"].append({"name": "treeleaves", "kind": "procedural", "procedural": {
    "source": {"kind": "mesh", "asset": "great_tree_leaves.glb"}, "distribution": {"kind": "points", "points": tpts},
    "material": {"program": "canopy", "roughness": 0.75, "metallic": 0.0, "alphaMode": "mask", "alphaCutoff": 0.5,
                 "doubleSided": True}}})
e_t, _, u_t, _ = band.surface_point(fr, 20.0, -20.0, 3.0)
tt, _, _, _ = band.surface_point(fr, 95.0, -40.0, 14.0)
st["camera"] = {"mode": 1, "position": [round(v, 3) for v in rw.to_avgen(e_t)], "target": [round(v, 3) for v in rw.to_avgen(tt)],
                "fov": 70.0, "orbitSpeed": 0.0}
write("trees", st)
for n in (50000,):
    sg = copy.deepcopy(scene_base)
    sg["nodes"].append({"name": "grass", "kind": "procedural", "procedural": {
        "source": {"kind": "mesh", "asset": "grass_clump.glb"},
        "distribution": {"kind": "points", "points": allpts[:n]},
        "material": {"baseColor": L(rw.P["meadow"]), "roughness": 0.95, "metallic": 0.0},
        "lod": {"cull": True, "minScreenRadius": 1.0, "maxDistance": 220.0}}})
    write("grass%dk" % (n // 1000), sg)
print("done")

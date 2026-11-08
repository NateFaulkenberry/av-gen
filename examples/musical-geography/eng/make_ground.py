#!/usr/bin/env python3
# Engineering fixture for Musical Geography's ground (ADR-1950/1952/1959/1976): the tile rings and the underlay, with
# a plain material, for the perf line and the parity checks. The art agent's look scene is separate; this is the
# measured baseline. Usage: make_ground.py <out.scene.json> [--fine-view M] [--no-shadow] [--rings 1|2|3]
#   [--camera x,y,z --target x,y,z] [--fov deg] [--palette day|dusk]
import argparse, json, os

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--fine-view", type=float, default=160.0)
ap.add_argument("--rings", type=int, default=3)
ap.add_argument("--no-shadow", action="store_true")
ap.add_argument("--no-underlay", action="store_true")
ap.add_argument("--no-slabs", action="store_true")
ap.add_argument("--camera", default="0,9,0")
ap.add_argument("--target", default="0,4,-60")
ap.add_argument("--fov", type=float, default=45.0)
ap.add_argument("--palette", default="day")
ap.add_argument("--seed", type=int, default=3)
a = ap.parse_args()

TILES = {"seed": a.seed, "path": [[-400, 120], [-150, 40], [0, -30], [140, -160], [380, -260]]}
GAP = 0.92
fv = a.fine_view

def ring(name, cell, view, align, fine_pitch=0.0, hole=None, thickness=0.18, color=(0.62, 0.66, 0.9), y=0.0,
         shadow=True, rough=0.55):
    gen = {"name": "tiles", "version": 1, "cellSize": cell, "viewDistance": view, "align": align,
           "finePitch": fine_pitch, "tiles": TILES}
    if hole:
        gen["hole"] = hole
    proc = {"source": {"kind": "box", "size": [cell * GAP, thickness, cell * GAP]},
            "distribution": {"kind": "generator", "generator": gen},
            "distributionTransform": {"position": [0, y, 0]},
            "material": {"baseColor": list(color), "roughness": rough, "metallic": 0.0},
            "lod": {"cull": True}}
    if not a.no_slabs:
        proc["slabs"] = True
    if not shadow:
        proc["castsShadow"] = False
    return {"name": name, "kind": "procedural", "position": [0, 0, 0], "procedural": proc}

nodes = [ring("ground-1x", 1.0, fv, 4)]
if a.rings >= 2:
    nodes.append(ring("ground-2x", 2.0, fv * 2.0, 2, 1.0, {"cellSize": 1.0, "viewDistance": fv, "align": 4},
                      thickness=0.36))
if a.rings >= 3:
    nodes.append(ring("ground-4x", 4.0, fv * 4.0, 1, 1.0, {"cellSize": 2.0, "viewDistance": fv * 2.0, "align": 2},
                      thickness=0.72, shadow=False))
if not a.no_underlay:
    # The underlay (ADR-1960): coarse, low and dark, seen only through the gaps.
    nodes.append(ring("underlay", 8.0, fv * (4.0 if a.rings >= 3 else 2.0 if a.rings >= 2 else 1.0), 1, 2.0,
                      thickness=3.0, color=(0.05, 0.05, 0.09), y=-1.6, shadow=False, rough=0.9))

pal = {"day": {"sky": [0.32, 0.5, 0.95], "horizon": [0.78, 0.82, 0.95], "sun": [1.0, 0.92, 0.82],
               "dir": [0.45, -0.55, -0.7], "ground": [0.62, 0.66, 0.9], "sunI": 3.2},
       "dusk": {"sky": [0.18, 0.12, 0.42], "horizon": [0.95, 0.52, 0.42], "sun": [1.0, 0.55, 0.35],
                "dir": [0.7, -0.18, -0.65], "ground": [0.85, 0.5, 0.6], "sunI": 3.0}}[a.palette]
for n in nodes:
    if n["name"] != "underlay":
        n["procedural"]["material"]["baseColor"] = pal["ground"]
cam = [float(v) for v in a.camera.split(",")]
tgt = [float(v) for v in a.target.split(",")]
scene = {"format": "avgen-scene", "version": 1, "name": "geography-ground",
         "camera": {"mode": 1, "position": cam, "target": tgt, "fov": a.fov, "orbitSpeed": 0.0},
         "environment": {"intensity": 0.6, "background": pal["horizon"], "fogColor": pal["horizon"],
                         "volumeDensity": 0.0035, "fogHeight": 60.0, "fogHeightFalloff": 0.02, "shadowRange": 160.0,
                         "ambientOcclusion": False, "contactShadows": False, "depthPrepass": True,
                         "sky": {"enabled": True, "zenithColor": pal["sky"], "horizonColor": pal["horizon"],
                                 "groundColor": pal["horizon"], "haze": 1.0, "sunIntensity": 0.0, "intensity": 1.0,
                                 "background": True, "useKeyLight": False}},
         "nodes": nodes,
         "lights": [{"id": "sun", "name": "sun", "type": "directional", "direction": pal["dir"], "color": pal["sun"],
                     "intensity": pal["sunI"], "castsShadow": not a.no_shadow, "contactShadow": False,
                     "softness": 0.4}]}
os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
json.dump(scene, open(a.out, "w"), indent=1)
proj = {"format": "avgen-project", "version": 4,
        "assets": {"scene": {"kind": "composition", "path": os.path.basename(a.out)}},
        "parameters": {"post/output/dither": 1.0},
        "render": {"width": 1920, "height": 1080, "fps": 30}}
json.dump(proj, open(a.out.replace(".scene.json", ".json"), "w"), indent=1)

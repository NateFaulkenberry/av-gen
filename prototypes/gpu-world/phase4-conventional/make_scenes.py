#!/usr/bin/env python3
"""Phase 4 control: the same meadow (area, per-layer densities, size ranges, view distances) authored as a
conventional production scene -- a terrain node with ecology scatter layers. Densities are the ones the
prototype's expanded arm actually produced (records / area). Production caps a layer at 1M instances, so a
layer that needs more is split into as many equal layers as it takes (different seeds): that is what an
author would have to do, and it keeps the content equal rather than letting the cap thin it."""
import json, math, sys
A = "../../../assets/quaternius/glTF/"
BIOMES = ["marsh", "meadow", "forest", "scree", "rim"]
# name, asset, density /m^2 (from the expanded arm at 500 m: 1,308,500 / 20,725 / 793 / 94 over 250,000 m^2),
# height m, minScale, maxScale, viewDistance
LAYERS = [("grass", "Grass_Common_Short.gltf", 5.234, 0.42, 0.6, 1.4, 40.0),
          ("reeds", "Grass_Wispy_Tall.gltf", 0.0829, 1.55, 0.6, 1.4, 150.0),
          ("mushrooms", "Mushroom_Common.gltf", 0.00317, 1.6, 0.5, 1.5, 220.0),
          ("spires", "Rock_Medium_1.gltf", 0.000376, 10.0, 0.4, 1.6, 700.0)]
CAP = 1000000

def scene(size, with_scatter=True):
    scatter = []
    for name, asset, dens, h, smin, smax, vd in LAYERS:
        n = dens * size * size
        k = max(1, math.ceil(n / (CAP * 0.95)))
        for i in range(k):
            scatter.append({"name": f"{name}-{i}" if k > 1 else name, "asset": A + asset,
                            "densities": {b: dens / k for b in BIOMES}, "maxSlope": 1.0, "avoidWater": False,
                            "minScale": smin, "maxScale": smax, "height": h, "seed": 101 + 17 * i, "randomYaw": 1.0,
                            "clusterScale": 40.0, "clustering": 0.5, "maxInstances": CAP, "viewDistance": vd,
                            "minScreenRadius": 0.0, "castsShadow": False})
    cam_x = -0.15 * min(size, 400.0)
    node = {"name": "meadow", "kind": "terrain", "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1],
            "terrain": {"chunkSize": 48.0, "resolution": 40, "lodLevels": 4, "lodDistance": 86.0, "viewDistance": 700.0,
                        "skirtDepth": 1.2},
            "material": {"baseColor": [0.02, 0.05, 0.03], "roughness": 0.9, "metallic": 0.0, "emissiveIntensity": 0.0},
            "world": {"name": "meadow", "seed": 20261005, "size": [size, size], "baseHeight": 0.0, "erosion": 0.0,
                      "seaLevel": -1000.0, "layers": [{"frequency": 0.004, "amplitude": 3.0}, {"frequency": 0.02, "amplitude": 0.6}]}}
    if with_scatter:
        node["scatter"] = scatter
    return {"format": "avgen-scene", "version": 1, "name": f"meadow-{size}",
            "camera": {"mode": 1, "position": [cam_x, 2.0, 0.0], "target": [cam_x + 40.0, 0.8, 12.0], "fov": 60.0, "orbitSpeed": 0.0},
            "environment": {"intensity": 0.3, "background": [0.05, 0.06, 0.09]},
            "nodes": [node]}

for size in (250, 500, 1000, 2000):
    for sc in (True, False):
        tag = f"meadow-{size}" + ("" if sc else "-terrain-only")
        json.dump(scene(size, sc), open(tag + ".scene.json", "w"), indent=1)
        proj = {"format": "avgen-project", "version": 4, "app": {"name": "avgen", "version": "0.1.0"},
                "assets": {"scene": {"kind": "composition", "path": tag + ".scene.json"}},
                "parameters": {}, "sources": [], "routes": [], "presets": []}
        json.dump(proj, open(tag + ".json", "w"), indent=1)
print("ok")

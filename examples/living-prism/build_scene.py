#!/usr/bin/env python3
"""The Living Prism: write the scene and the live project from layout.py and the generated meshes.

    python3 examples/living-prism/build_scene.py --assets build/living-prism/assets
        [--scene-dir examples/living-prism/scene] [--project examples/living-prism/living-prism.json]

Stage 2 (the blockout): simple materials, the pool on the cinematic water (ADR-1286), blockout lights, the camera
route baked into six looping timelines (seek equals play; the loop's seam is C1). No audio routes yet.
The scene and its meshes (scene/) are generated and gitignored; the project is committed and is a pure function of
the scripts.
"""
import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import layout as L  # noqa: E402

# ---------------------------------------------------------------------------------------------------------------
# Blockout materials (linear RGB). Stage 3 replaces these with material programs bound to the palette roles
# (docs/prototypes/living-prism/05-stage1-plan.md section 4); the colours here are the Ultraviolet look's.
ROCK = {"baseColor": [0.018, 0.016, 0.03], "roughness": 0.82, "metallic": 0.0}
FLOOR = {"baseColor": [0.014, 0.012, 0.024], "roughness": 0.7, "metallic": 0.0}
XTAL = {  # body colour, emissive colour, emissive strength
    "spire": ([0.30, 0.08, 0.62], [0.55, 0.12, 1.0], 0.9),
    "fan": ([0.06, 0.12, 0.55], [0.12, 0.3, 1.0], 0.6),
    "geode": ([0.62, 0.05, 0.32], [1.0, 0.1, 0.5], 0.8),
    "chandelier": ([0.35, 0.18, 0.7], [0.5, 0.3, 1.0], 0.5),
    "ember": ([0.7, 0.22, 0.04], [1.0, 0.35, 0.05], 0.7),
    "arch": ([0.4, 0.1, 0.65], [0.7, 0.2, 1.0], 0.5),
    "druse": ([0.4, 0.12, 0.6], [0.6, 0.2, 1.0], 0.35),
    "shard": ([0.3, 0.3, 0.75], [0.4, 0.45, 1.0], 0.3),
}

WATER = {  # ADR-1286's cinematic path: a dark absorbing mirror; SSR reflects the lit grotto
    "shallowColor": [0.004, 0.004, 0.014], "deepColor": [0.002, 0.002, 0.008], "shallow": 1.0,
    "clarity": 1.2, "edgeFade": 0.15, "maxOpacity": 0.97, "fresnel": 0.14, "reflection": 1.0,
    "reflectionTint": [0.06, 0.04, 0.16], "specular": 1.2, "roughness": 0.03,
    "ripple": 0.12, "rippleScale": 1.4, "rippleSpeed": 0.5, "chop": 0.3, "foam": 0.0, "refraction": 0.1,
    "glow": 0.0, "sparkle": 0.0,
    "ssr": 1.0, "ssrDistance": 30.0, "ssrSteps": 64.0, "ssrGain": 1.4, "streak": 0.06, "reflectionRays": 2.0,
    "transmission": 1.0, "absorption": [2.2, 1.8, 1.0], "undulation": 0.15, "undulationScale": 0.2,
    "roughnessVariation": 0.3, "tintPatches": 0.0, "glints": 0.0,
}


def mesh_node(name, asset, points, material, shadow=True, lod=None):
    pr = {"source": {"kind": "mesh", "asset": asset}, "distribution": {"kind": "points", "points": points},
          "material": dict(material)}
    if not shadow:
        pr["castsShadow"] = False
    if lod:
        pr["lod"] = lod
    return {"name": name, "kind": "procedural", "procedural": pr}


def xtal_mat(key):
    base, emis, k = XTAL[key]
    return {"baseColor": base, "emissiveColor": emis, "emissiveIntensity": k, "roughness": 0.12, "metallic": 0.0}


def scene_json():
    nodes = [{"name": "floor", "kind": "terrain", "position": [0, 0, 0], "world": L.world_json(),
              "terrain": {"chunkSize": 20.0, "resolution": 64, "lodLevels": 1, "lodDistance": 200.0,
                          "viewDistance": 200.0, "shadowDistance": 40.0, "skirtDepth": 0.5, "groundMottle": False,
                          "water": WATER},
              "material": dict(FLOOR)}]
    nodes.append(mesh_node("shell", "shell.glb", [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]], ROCK, shadow=False))
    for h in L.HEROES:
        rec = L.hero_placement(h)
        nodes.append(mesh_node(h["name"] + "_xtal", h["name"] + "_xtal.glb", [rec], xtal_mat(h["name"])))
        nodes.append(mesh_node(h["name"] + "_rock", h["name"] + "_rock.glb", [rec], ROCK))
    a = L.ARCH
    rec = L.placement(*a["pos"], a["yaw"], a["scale"], a["sink"])
    nodes.append(mesh_node("arch_rock", "arch_rock.glb", [rec], ROCK))
    nodes.append(mesh_node("arch_xtal", "arch_xtal.glb", [rec], xtal_mat("arch")))
    groups = {}
    for v, x, z, yaw, s in L.CLUSTERS:
        groups.setdefault(v, []).append(L.placement(x, z, yaw, s, 0.12 * s))
    for v, pts in sorted(groups.items()):
        nodes.append(mesh_node(v + "_xtal", v + "_xtal.glb", pts, xtal_mat("druse")))
        nodes.append(mesh_node(v + "_rock", v + "_rock.glb", pts, ROCK))
    groups = {}
    for v, x, z, yaw, s, lean in L.SHARDS:
        groups.setdefault(v, []).append(L.placement(x, z, yaw, s, 0.1 * s, (lean, 0.0)))
    for v, pts in sorted(groups.items()):
        nodes.append(mesh_node(v + "_xtal", v + "_xtal.glb", pts, xtal_mat("shard"), shadow=False))
    groups = {}
    for v, x, z, yaw, s, sink in L.BOULDERS:
        groups.setdefault(v, []).append(L.placement(x, z, yaw, s, sink))
    for v, pts in sorted(groups.items()):
        nodes.append(mesh_node(v, v + ".glb", pts, ROCK))

    p0, t0, _ = L.CAMERA_STATIONS[0]
    lights = [
        # the shaft: a cool violet spot from high front-left onto the pool and the spire (the only shadow caster)
        {"name": "shaft", "id": "shaft", "type": "spot", "role": "key", "position": [-3.5, 9.0, 3.5],
         "direction": [0.25, -0.75, -0.62], "color": [0.62, 0.55, 1.0], "intensity": 900.0, "range": 30.0,
         "innerCone": 12.0, "outerCone": 26.0, "castsShadow": True, "volumetric": 1.0},
        # the passage behind the arch: blue depth, haze lit from inside
        {"name": "passage", "id": "passage", "type": "point", "position": [-2.5, 3.0, -19.0],
         "color": [0.15, 0.4, 1.0], "intensity": 260.0, "range": 16.0, "radius": 1.5, "castsShadow": False,
         "volumetric": 0.8},
        {"name": "spireGlow", "id": "spireGlow", "type": "point", "position": [0.6, 1.6, -6.6],
         "color": [0.8, 0.2, 1.0], "intensity": 70.0, "range": 9.0, "radius": 0.6, "castsShadow": False,
         "volumetric": 0.4},
        {"name": "geodeGlow", "id": "geodeGlow", "type": "point", "position": [7.4, 1.6, -0.2],
         "color": [1.0, 0.2, 0.55], "intensity": 30.0, "range": 5.0, "radius": 0.4, "castsShadow": False},
        {"name": "fanGlow", "id": "fanGlow", "type": "point", "position": [-6.4, 2.6, -0.8],
         "color": [0.2, 0.4, 1.0], "intensity": 35.0, "range": 7.0, "radius": 0.5, "castsShadow": False},
        {"name": "chandelierGlow", "id": "chandelierGlow", "type": "point", "position": [-1.2, 5.6, -0.8],
         "color": [0.6, 0.35, 1.0], "intensity": 30.0, "range": 7.0, "radius": 0.5, "castsShadow": False},
        {"name": "emberGlow", "id": "emberGlow", "type": "point", "position": [3.9, 1.2, 4.6],
         "color": [1.0, 0.45, 0.1], "intensity": 14.0, "range": 4.0, "radius": 0.3, "castsShadow": False},
    ]
    return {
        "format": "avgen-scene", "version": 1, "name": "The Living Prism",
        "_note": "Generated by examples/living-prism/build_scene.py from layout.py. Do not edit.",
        "camera": {"mode": 1, "position": list(p0), "target": list(t0), "fov": L.CAMERA_FOV, "orbitSpeed": 0,
                   "near": 0.1, "far": 120.0},
        "environment": {
            "background": [0.004, 0.003, 0.012], "intensity": 0.12, "lightFromEnvironment": False,
            "fogColor": [0.02, 0.012, 0.05], "volumeDensity": 0.025, "volumeScattering": 0.9,
            "volumeAbsorption": 0.3, "volumeAnisotropy": 0.4, "volumeSteps": 24, "volumeMaxDistance": 40.0,
            "volumeLocalLights": 1.0, "volumeNoise": 0.3, "volumeNoiseScale": 0.15, "volumeEmission": 0.0,
            "fogHeight": 3.0, "fogHeightFalloff": 0.25, "fogUpperDensity": 0.4,
            "shadowCascades": 1, "shadowRange": 40.0, "ambientOcclusion": True, "depthPrepass": True,
            "sky": {"enabled": True, "background": True, "useKeyLight": False, "zenithColor": [0.01, 0.006, 0.03],
                    "horizonColor": [0.02, 0.012, 0.05], "groundColor": [0.006, 0.004, 0.012],
                    "sunColor": [0.6, 0.5, 1.0], "sunDirection": [0.2, 0.9, 0.3], "haze": 0.1,
                    "sunIntensity": 0.0, "sunSize": 0.01, "sunGlow": 0.0, "intensity": 1.0}},
        "lights": lights,
        "nodes": nodes,
    }


def project_json(scene_rel):
    samples, _ = L.camera_samples(0.25)
    sources, routes = [], []
    for key, idx, comp in (("camPX", 1, 0), ("camPY", 1, 1), ("camPZ", 1, 2),
                           ("camTX", 2, 0), ("camTY", 2, 1), ("camTZ", 2, 2)):
        keys = [{"time": s[0], "value": round(s[idx][comp], 4), "interp": "linear"} for s in samples]
        sources.append({"kind": "timeline", "name": key, "settings": {"loopLength": round(L.CAMERA_LOOP, 4),
                                                                      "keys": keys}})
        routes.append({"source": "timeline." + key, "target": "camera/position" if idx == 1 else "camera/target",
                       "amount": 1.0, "op": "replace", "polarity": "unipolar", "component": comp})
    return {
        "format": "avgen-project", "version": 4, "app": {"name": "The Living Prism"},
        "assets": {"scene": {"kind": "composition", "path": scene_rel}},
        "sonic": {"live": True},
        "sonicScene": {"category": "world", "id": "living-prism", "title": "The Living Prism",
                       "thesis": "A compact crystal grotto over a mirror pool, played by the music: the drums strike "
                                 "the crystals, the bass moves the water and the stone, the synth colours the light."},
        "parameters": {
            "camera/exposure/compensation": 0.0, "post/bloom/enabled": True, "post/bloom/threshold": 1.0,
            "post/bloom/intensity": 0.35, "post/bloom/radius": 1.0, "post/tonemap/chroma-retention": 0.85,
            "post/output/vignette": 0.22, "post/grade/saturation": 1.1,
        },
        "sources": sources,
        "routes": routes,
        "render": {"width": 1920, "height": 1080, "fps": 30},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", required=True)
    ap.add_argument("--scene-dir", default=os.path.join(HERE, "scene"))
    ap.add_argument("--project", default=os.path.join(HERE, "living-prism.json"))
    a = ap.parse_args()
    os.makedirs(a.scene_dir, exist_ok=True)
    sc = scene_json()
    used = set()
    for n in sc["nodes"]:
        src = n.get("procedural", {}).get("source", {})
        if src.get("kind") == "mesh":
            used.add(src["asset"])
    for f in sorted(used):
        shutil.copy(os.path.join(a.assets, f), os.path.join(a.scene_dir, f))
    path = os.path.join(a.scene_dir, "living-prism.scene.json")
    with open(path, "w") as f:
        json.dump(sc, f, indent=1)
    rel = os.path.relpath(path, os.path.dirname(os.path.abspath(a.project)))
    with open(a.project, "w") as f:
        json.dump(project_json(rel), f, indent=1)
        f.write("\n")
    print("wrote %s (%d nodes, %d meshes) and %s" % (path, len(sc["nodes"]), len(used), a.project))
    print("station times:", L.station_times())


if __name__ == "__main__":
    main()

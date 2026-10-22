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
    "spire": ([0.10, 0.02, 0.22], [0.45, 0.08, 1.0], 1.1),
    "fan": ([0.02, 0.04, 0.2], [0.08, 0.25, 1.0], 0.8),
    "geode": ([0.2, 0.01, 0.09], [1.0, 0.08, 0.45], 1.0),
    "chandelier": ([0.10, 0.04, 0.22], [0.45, 0.25, 1.0], 0.7),
    "ember": ([0.25, 0.06, 0.01], [1.0, 0.32, 0.04], 0.9),
    "arch": ([0.12, 0.03, 0.22], [0.65, 0.15, 1.0], 0.7),
    "druse": ([0.12, 0.03, 0.2], [0.55, 0.15, 1.0], 0.5),
    "shard": ([0.08, 0.08, 0.25], [0.35, 0.4, 1.0], 0.45),
}

# Blockout material programs: dark hued rock with broad tonal variation (no flat CG grey), and the floor. Without a
# program the terrain draws its biome colours (green), which a grotto has no use for.
def rock_program(name, dark, light, scale):
    return {"name": name, "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": [
                {"kind": "input", "dst": 0, "input": "worldPosition"},
                {"kind": "noise", "dst": 1, "srcA": 0, "value": scale, "seed": 3},
                {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.3, 0.75, 0, 0]},
                {"kind": "constant", "dst": 2, "constant": dark + [1.0]},
                {"kind": "constant", "dst": 4, "constant": light + [1.0]},
                {"kind": "mixBy", "dst": 3, "srcA": 2, "srcB": 4, "srcC": 1}]}


PROGRAMS = [rock_program("lp_rock", [0.010, 0.008, 0.018], [0.032, 0.024, 0.05], 0.45),
            rock_program("lp_floor", [0.008, 0.007, 0.014], [0.022, 0.017, 0.034], 0.6)]
ROCK = dict(ROCK, program="lp_rock")
FLOOR = dict(FLOOR, program="lp_floor")

WATER = {  # ADR-1286's cinematic path: a dark absorbing mirror; SSR reflects the lit grotto
    "shallowColor": [0.004, 0.004, 0.014], "deepColor": [0.002, 0.002, 0.008], "shallow": 1.0,
    "clarity": 1.2, "edgeFade": 0.15, "maxOpacity": 0.97, "fresnel": 0.14, "reflection": 1.0,
    "reflectionTint": [0.06, 0.04, 0.16], "specular": 1.2, "roughness": 0.03,
    "ripple": 0.035, "rippleScale": 0.9, "rippleSpeed": 0.4, "chop": 0.2, "foam": 0.0, "refraction": 0.1,
    "glow": 0.0, "sparkle": 0.0,
    "ssr": 1.0, "ssrDistance": 30.0, "ssrSteps": 64.0, "ssrGain": 1.4, "streak": 0.06, "reflectionRays": 2.0,
    "transmission": 1.0, "absorption": [2.2, 1.8, 1.0], "undulation": 0.04, "undulationScale": 0.2,
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
        {"name": "shaft", "id": "shaft", "type": "spot", "role": "key", "position": [-3.0, 14.0, 1.5],
         "direction": [0.18, -0.85, -0.5], "color": [0.62, 0.55, 1.0], "intensity": 260.0, "range": 34.0,
         "innerCone": 8.0, "outerCone": 18.0, "castsShadow": True, "volumetric": 0.5},
        # the passage behind the arch: blue depth, haze lit from inside
        {"name": "passage", "id": "passage", "type": "point", "position": [-2.5, 3.0, -19.0],
         "color": [0.15, 0.4, 1.0], "intensity": 260.0, "range": 16.0, "radius": 1.5, "castsShadow": False,
         "volumetric": 0.6},
    ]
    # each formation's own glow light sits at (or in) the formation, so it reads as the crystal's inner light
    def at(name, dx, dy, dz):
        h = next(h for h in L.HEROES if h["name"] == name)
        r = L.hero_placement(h)
        return [round(r[0] + dx, 3), round(r[1] + dy, 3), round(r[2] + dz, 3)]
    for name, off, color, k, rng_, rad in (
            ("spire", (0.1, 1.0, 1.6), [0.8, 0.2, 1.0], 60.0, 9.0, 0.6),
            ("geode", (-0.6, 1.6, 0.3), [1.0, 0.2, 0.55], 30.0, 5.0, 0.4),
            ("fan", (1.8, 2.4, 0.6), [0.2, 0.4, 1.0], 30.0, 7.0, 0.5),
            ("chandelier", (0.0, -3.4, 0.0), [0.6, 0.35, 1.0], 25.0, 7.0, 0.5),
            ("ember", (-0.6, 1.0, -0.6), [1.0, 0.45, 0.1], 14.0, 4.0, 0.3)):
        lights.append({"name": name + "Glow", "id": name + "Glow", "type": "point", "position": at(name, *off),
                       "color": color, "intensity": k, "range": rng_, "radius": rad, "castsShadow": False,
                       "volumetric": 0.0})
    return {
        "format": "avgen-scene", "version": 1, "name": "The Living Prism",
        "_note": "Generated by examples/living-prism/build_scene.py from layout.py. Do not edit.",
        "camera": {"mode": 1, "position": list(p0), "target": list(t0), "fov": L.CAMERA_FOV, "orbitSpeed": 0,
                   "near": 0.1, "far": 120.0},
        "environment": {
            "background": [0.004, 0.003, 0.012], "intensity": 0.04, "lightFromEnvironment": False,
            "fogColor": [0.02, 0.012, 0.05], "volumeDensity": 0.004, "volumeScattering": 0.9,
            "volumeAbsorption": 0.3, "volumeAnisotropy": 0.4, "volumeSteps": 24, "volumeMaxDistance": 40.0,
            "volumeLocalLights": 1.0, "volumeNoise": 0.3, "volumeNoiseScale": 0.15, "volumeEmission": 0.0,
            "fogHeight": 3.0, "fogHeightFalloff": 0.25, "fogUpperDensity": 0.15,
            "shadowCascades": 1, "shadowRange": 40.0, "ambientOcclusion": True, "depthPrepass": True,
            "sky": {"enabled": True, "background": True, "useKeyLight": False, "zenithColor": [0.01, 0.006, 0.03],
                    "horizonColor": [0.02, 0.012, 0.05], "groundColor": [0.006, 0.004, 0.012],
                    "sunColor": [0.6, 0.5, 1.0], "sunDirection": [0.2, 0.9, 0.3], "haze": 0.1,
                    "sunIntensity": 0.0, "sunSize": 0.01, "sunGlow": 0.0, "intensity": 1.0}},
        "lights": lights,
        "materialPrograms": PROGRAMS,
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
            "camera/exposure/compensation": -0.3, "post/bloom/enabled": True, "post/bloom/threshold": 1.0,
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

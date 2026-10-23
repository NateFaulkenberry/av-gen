#!/usr/bin/env python3
"""The Living Prism (aurora revision): write the scene and the projects from layout.py and the generated meshes.

    python3 examples/living-prism-aurora/build_scene.py --assets build/aurora/assets

Phase 2 (the composition prototype, spec 13): the fixed camera, the valley, the river, the far ranges, the seven
crystal formations, the stars and a rough aurora, with placeholder materials. The scene (scene/) is generated and
gitignored; the projects are committed and are pure functions of the scripts.
"""
import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import layout as L  # noqa: E402

# the reference's palette (15 section 1): pastel accents over dark slate
AURORA_LOW = [0.10, 0.95, 0.55]      # mint-green at the curtains' base
AURORA_MID = [0.12, 0.70, 0.75]      # teal
AURORA_TOP = [0.55, 0.30, 0.95]      # violet
AURORA2_LOW = [0.95, 0.35, 0.75]     # the pink-violet layer
AURORA2_MID = [0.65, 0.35, 0.95]
AURORA2_TOP = [0.30, 0.25, 0.85]


def prog_ground():
    return {"name": "au_ground", "baseColor": 3, "metallic": -1, "roughness": 6, "emission": -1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": [
                {"kind": "input", "dst": 0, "input": "worldPosition"},
                {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.05, "seed": 3},
                {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.3, 0.75, 0, 0]},
                {"kind": "constant", "dst": 2, "constant": [0.010, 0.030, 0.014, 1]},    # deep moss
                {"kind": "constant", "dst": 3, "constant": [0.035, 0.095, 0.040, 1]},    # lit moss
                {"kind": "mixBy", "dst": 3, "srcA": 2, "srcB": 3, "srcC": 1},
                {"kind": "input", "dst": 4, "input": "normal"},                            # rock on steep ground
                {"kind": "gradient", "dst": 4, "srcA": 4, "constant": [0, 1, 0, 0], "value": 1.0},
                {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.5, 0.72, 0, 0]},     # moss on all but steep
                {"kind": "gradient", "dst": 7, "srcA": 0, "constant": [0, 1, 0, 1.6], "value": -0.05},  # and below ~30 m
                {"kind": "add", "dst": 4, "srcA": 4, "srcB": 7},
                {"kind": "constant", "dst": 5, "constant": [0.012, 0.014, 0.018, 1]},    # near-black rock
                {"kind": "mixBy", "dst": 3, "srcA": 5, "srcB": 3, "srcC": 4},
                {"kind": "remap", "dst": 6, "srcA": 4, "constant": [0, 1, 0.75, 0.9]}]}


def prog_crystal():
    """Placeholder crystal (Phase 3 replaces it): a pale body, an aurora-mint rim, a soft pink interior."""
    return {"name": "au_crystal", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": 1,
            "emissionIntensity": 1.0, "opacity": -1, "ops": [
                {"kind": "input", "dst": 0, "input": "normal"},
                {"kind": "input", "dst": 1, "input": "viewDirection"},
                {"kind": "fresnel", "dst": 2, "srcA": 0, "srcB": 1, "value": 2.5},
                {"kind": "constant", "dst": 3, "constant": [0.20, 0.18, 0.28, 1]},
                {"kind": "constant", "dst": 4, "constant": [0.25, 0.9, 0.7, 1]},
                {"kind": "multiply", "dst": 5, "srcA": 2, "srcB": 4},
                {"kind": "constant", "dst": 6, "constant": [0.35, 0.12, 0.30, 1]},
                {"kind": "remap", "dst": 7, "srcA": 2, "constant": [0, 1, 1, 0.2]},
                {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
                {"kind": "add", "dst": 1, "srcA": 5, "srcB": 6}]}


WATER = {  # long exposure (spec 7): dark body, view-stretched rough reflections, aurora-coloured streak patches
    "shallowColor": [0.003, 0.006, 0.010], "deepColor": [0.001, 0.002, 0.005], "shallow": 1.0, "clarity": 1.0,
    "edgeFade": 0.4, "maxOpacity": 0.97, "fresnel": 0.35, "reflection": 1.6, "reflectionTint": [0.6, 0.85, 0.9],
    "specular": 1.0, "roughness": 0.12, "ripple": 0.25, "rippleScale": 0.35, "rippleSpeed": 0.6, "chop": 0.1,
    "foam": 0.0, "refraction": 0.1, "glow": 0.0, "sparkle": 0.0,
    "ssr": 1.0, "ssrDistance": 400.0, "ssrSteps": 48.0, "ssrGain": 1.5, "streak": 0.45, "reflectionRays": 3.0,
    "transmission": 0.6, "absorption": [2.0, 1.5, 1.0], "undulation": 0.3, "undulationScale": 0.04,
    "roughnessVariation": 0.5, "tintPatches": 0.8, "reflectionTint2": [0.9, 0.45, 0.8], "tintPatchScale": 0.03,
    "glints": 0.0}


def aurora(name, low, mid, top, height, base, radius, count, intensity, complexity, flow, waves=6.5):
    return {"id": name, "name": name, "type": "aurora", "owner": {"kind": "world"}, "enabled": True, "order": 0,
            "activation": "always", "style": "",
            "timing": {"delay": 0.0, "fadeIn": 0.0, "fadeOut": 0.0, "lifetime": 0.0, "repeatSeconds": 0.0,
                       "windowSeconds": 6.0, "windowStart": 0.0},
            "parameters": {
                "appearance": {"edgeBrightness": 3.2, "emission": 0.25, "filaments": 2.0, "horizonGlow": 0.0,
                               "intensity": intensity, "lowColor": low, "midColor": mid, "topColor": top,
                               "opacity": 0.95, "sparkle": 0.15},
                "audio": {"bass": 0.0, "beat": 0.0, "glints": 0.0, "high": 0.0, "lowMid": 0.0, "mid": 0.0,
                          "sensitivity": 0.0, "spectrumShape": 0.0},
                "rainbow": {"brightness": 1.0, "enabled": False, "hueOffset": 0.0, "saturation": 0.85, "scale": 1.1,
                            "speed": 0.0},
                "shape": {"anchor": "camera", "anchorPosition": [0.0, 0.0, 0.0], "baseHeight": base,
                          "complexity": complexity, "curtainCount": count, "curtainHeight": height,
                          "driftSpeed": 0.05, "flowSpeed": flow, "layerSpacing": 0.35, "radius": radius,
                          "turbulence": 0.35, "verticalSpeed": 0.03, "waveAmplitude": 0.6, "waveScale": waves}},
            "ground": {"color": low, "falloff": 2.0, "intensity": 0.15, "mode": "subtle", "radius": 600.0},
            "flow": {"field": "", "influence": 0.0}}


def mesh_node(name, asset, points, material, shadow=True):
    pr = {"source": {"kind": "mesh", "asset": asset}, "distribution": {"kind": "points", "points": points},
          "material": dict(material)}
    if not shadow:
        pr["castsShadow"] = False
    return {"name": name, "kind": "procedural", "procedural": pr}


def scene_json():
    nodes = [{"name": "valley", "kind": "terrain", "position": [0, 0, 0], "world": L.world_json(),
              "terrain": {"chunkSize": 200.0, "resolution": 64, "lodLevels": 3, "lodDistance": 200.0,
                          "viewDistance": 5000.0, "shadowDistance": 300.0, "skirtDepth": 3.0, "groundMottle": False,
                          "water": WATER},
              "material": {"baseColor": [0.02, 0.05, 0.02], "roughness": 0.85, "metallic": 0.0,
                           "program": "au_ground"}}]
    xmat = {"baseColor": [0.2, 0.18, 0.28], "roughness": 0.08, "metallic": 0.0, "program": "au_crystal"}
    for name, x, z, yaw, s, fam in L.FORMATIONS:
        q = L.quat_y(yaw)
        y = L.height(x, z) - 3.0 * s   # the noise layer moves the ground by up to ~7 m; sink enough to cover it
        pt = [round(x, 3), round(y, 3), round(z, 3)] + [round(c, 6) for c in q] + [s, s, s]
        nodes.append(mesh_node("xtal_" + name, fam + ".glb", [pt], xmat))
    # the far mountains: three ridges in place, receding in value through the fog (phase 2b)
    rmat = {"baseColor": [0.018, 0.020, 0.030], "roughness": 0.9, "metallic": 0.0}
    for r in ("ridgeNear", "ridgeMid", "ridgeFar"):
        nodes.append(mesh_node(r, r + ".glb", [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]], rmat, shadow=False))
    p0, t0 = L.CAMERA_POS, L.CAMERA_TARGET
    return {
        "format": "avgen-scene", "version": 1, "name": "The Living Prism: Aurora Valley",
        "_note": "Generated by examples/living-prism-aurora/build_scene.py from layout.py. Do not edit.",
        "camera": {"mode": 1, "position": list(p0), "target": list(t0), "fov": L.CAMERA_FOV, "orbitSpeed": 0,
                   "near": 0.5, "far": L.FAR},
        "environment": {
            # the air: a light peach-lavender extinction, so the ridges recede in value toward the horizon glow
            "intensity": 0.25, "lightFromEnvironment": False, "fogColor": [0.10, 0.07, 0.10],
            "volumeDensity": 0.00035, "volumeAbsorption": 1.0, "volumeScattering": 0.0, "volumeSteps": 4,
            "volumeMaxDistance": 40.0, "volumeLocalLights": 0.0, "horizonDensity": 0.4,
            "fogHeight": 60.0, "fogHeightFalloff": 0.01, "fogUpperDensity": 0.05,
            "fogSky": 0.0, "fogSkyDistance": 4500.0,
            "shadowCascades": 2, "shadowRange": 600.0, "ambientOcclusion": True, "depthPrepass": True,
            "sky": {"enabled": True, "background": True, "useKeyLight": False,
                    "zenithColor": [0.006, 0.010, 0.030], "horizonColor": [0.42, 0.20, 0.18],
                    "groundColor": [0.01, 0.012, 0.02], "sunColor": [0.6, 0.6, 0.9], "sunDirection": [0, -1, 0],
                    "haze": 0.05, "sunIntensity": 0.0, "sunSize": 0.01, "sunGlow": 0.0, "intensity": 1.0}},
        "lights": [
            # the aurora's light on the valley: a broad mint fill from the sky ahead, and a dim violet from above
            {"name": "auroraFill", "id": "auroraFill", "type": "directional", "role": "key",
             "direction": [0.1, -0.45, 0.89], "color": AURORA_LOW, "intensity": 0.7, "castsShadow": True},
            {"name": "skyFill", "id": "skyFill", "type": "directional", "direction": [-0.2, -0.95, -0.2],
             "color": AURORA_TOP, "intensity": 0.18, "castsShadow": False}],
        "effects": [
            {"id": "stars", "name": "stars", "type": "stars", "owner": {"kind": "world"}, "enabled": True, "order": 0,
             "activation": "always", "parameters": {"brightness": 1.6, "density": 0.55, "magnitudeSlope": 6.5,
                                                    "colorSpread": 0.3, "twinkle": 0.2, "twinkleRate": 1.0,
                                                    "horizonFade": 0.6, "band": 0.15, "bandTilt": 30.0,
                                                    "daylight": 0.0}},
            # two ribbon bands with dark, starry sky between and above them (phase 2b): short curtains, undulating tops
            aurora("auroraMain", AURORA_LOW, AURORA_MID, AURORA_TOP, 750.0, 320.0, 3000.0, 2.0, 2.6, 72.0, 0.02, 5.0),
            aurora("auroraPink", AURORA2_LOW, AURORA2_MID, AURORA2_TOP, 650.0, 1350.0, 3400.0, 2.0, 2.0, 50.0,
                   0.015, 3.5)],
        "materialPrograms": [prog_ground(), prog_crystal()],
        "nodes": nodes,
    }


def project_json(scene_rel, offline=False, root=""):
    p = {"format": "avgen-project", "version": 4,
         "app": {"name": "The Living Prism: Aurora Valley" + (" (offline)" if offline else "")},
         "assets": {"scene": {"kind": "composition", "path": scene_rel}},
         "sonic": {"live": not offline},
         "parameters": {"camera/exposure/compensation": 0.0, "post/bloom/enabled": True, "post/bloom/threshold": 1.4,
                        "post/bloom/intensity": 0.3, "post/bloom/radius": 0.8, "post/tonemap/chroma-retention": 0.8,
                        "post/output/vignette": 0.3, "post/grade/saturation": 1.0},
         "sources": [], "routes": [],
         "render": {"width": 1920, "height": 1080, "fps": 30}}
    if offline:
        p["assets"]["audio"] = {"path": os.path.join(root, "assets/audio/feline-footwear.wav")}
        p["assets"]["stems"] = [{"key": k, "label": k.title(), "file": os.path.join(
            root, "assets/audio/stems/feline-footwear (%s).wav" % k.title())} for k in ("drums", "bass", "synth")]
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", required=True)
    ap.add_argument("--scene-dir", default=os.path.join(HERE, "scene"))
    ap.add_argument("--project-dir", default=HERE)
    a = ap.parse_args()
    os.makedirs(a.scene_dir, exist_ok=True)
    sc = scene_json()
    for n in sc["nodes"]:
        src = n.get("procedural", {}).get("source", {})
        if src.get("kind") == "mesh":
            shutil.copy(os.path.join(a.assets, src["asset"]), os.path.join(a.scene_dir, src["asset"]))
    path = os.path.join(a.scene_dir, "aurora-valley.scene.json")
    json.dump(sc, open(path, "w"), indent=1)
    rel = os.path.relpath(path, a.project_dir)
    root = os.path.relpath(os.path.join(HERE, "..", ".."), a.project_dir)
    for name, off in (("aurora-valley.json", False), ("aurora-valley-offline.json", True)):
        with open(os.path.join(a.project_dir, name), "w") as f:
            json.dump(project_json(rel, off, root), f, indent=1)
            f.write("\n")
    print("wrote", path)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""The Volumetric Lighting Lab's environments (ADR-2210..2214; programme 115 Agent 4's lab conventions).

Edit this script, not the JSON beside it: every eNN-*.json and eNN-*.scene.json in this folder is its output, and
`build.py --check` (run by tests/unit/test_volumetric_lab_fixtures.cpp) fails when the committed files differ from
what it would write, byte for byte.

One project per environment, numbered by the programme spec's list (spec 2, Agent 2, "Volumetric lighting test lab"):
  e01 light shafts          a low sun through a colonnade and a spot through a grille: shafts, not an overlay
  e02 dense atmosphere      unlit slabs at known distances, nothing scattering: the analytic transmittance check
  e06 coloured scattering   red, green and blue spots through one haze, three anisotropies
Environments 3, 4, 5, 7 and 8 arrive with the phases that build what they test (B2..B4).

Procedural geometry only; no assets, no audio (none of these three is audio-reactive). Stdlib only.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def box(name, position, size, color, *, unlit=False, casts=True, roughness=0.9):
    material = {"baseColor": color, "roughness": roughness, "metallic": 0.0}
    if unlit:
        material["unlit"] = True
    return {
        "name": name,
        "kind": "procedural",
        "position": position,
        "procedural": {
            "source": {"kind": "box", "size": size, "subdivisions": 1},
            "distribution": {"kind": "single"},
            "castsShadow": casts,
            "material": material,
        },
    }


def project(scene_file, note, parameters=None):
    return {
        "format": "avgen-project",
        "version": 4,
        "_note": note,
        "app": {"name": "avgen", "version": "0.1.0"},
        "assets": {"scene": {"kind": "composition", "path": scene_file}},
        "parameters": parameters or {},
        "sources": [],
        "routes": [],
        "timeline": {"enabled": True, "tracks": [], "cues": []},
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "sequence",
                   "path": "renders/" + scene_file.replace(".scene.json", "")},
    }


def dark_environment(**extra):
    env = {
        "background": [0.004, 0.005, 0.009],
        "skyIntensity": 0.0,
        "skybox": False,
        "lightFromEnvironment": False,
        "intensity": 0.0,
        "fogColor": [0.02, 0.022, 0.03],
        "fogHeightAmount": 0.0,
        "volumeScattering": 1.0,
        "volumeAbsorption": 1.0,
        "volumeNoise": 0.0,
        "volumeEmission": 0.0,
        "volumetricLight": {"enabled": True, "distance": 160.0},
    }
    env.update(extra)
    return env


def e01():
    """Light shafts. The sun is low and behind the colonnade, so the camera looks into the light: forward
    scattering (anisotropy 0.6) makes the lit air between the columns bright and the air in each column's
    shadow dark -- the shafts are the shadow map seen in the medium, which a fog overlay cannot draw. A spot
    behind a grille wall on the right throws barred beams across the floor."""
    nodes = [box("floor", [0.0, -0.1, -30.0], [120.0, 0.2, 140.0], [0.16, 0.155, 0.15])]
    for i in range(9):
        x = -24.0 + i * 6.0
        nodes.append(box(f"column-{i:02d}", [x, 6.0, -28.0], [1.6, 12.0, 1.6], [0.55, 0.52, 0.47]))
    nodes.append(box("lintel", [0.0, 12.6, -28.0], [52.0, 1.2, 2.0], [0.5, 0.47, 0.43]))
    # The grille: a wall of vertical bars with a spot behind it, on the right of frame.
    for i in range(7):
        nodes.append(box(f"bar-{i:02d}", [14.0 + i * 0.9, 3.0, -12.0], [0.35, 6.0, 0.35], [0.4, 0.4, 0.42]))
    nodes.append(box("grille-head", [16.7, 6.3, -12.0], [7.0, 0.6, 0.6], [0.4, 0.4, 0.42]))
    scene = {
        "format": "avgen-scene",
        "version": 1,
        "name": "Volumetric Lighting Lab e01: light shafts",
        "_note": e01.__doc__.replace("\n    ", " ").strip(),
        "camera": {"mode": 1, "position": [0.0, 3.0, 14.0], "target": [0.0, 6.0, -40.0], "fov": 55.0,
                   "orbitSpeed": 0.0},
        # Thin air (an optical depth under 1 across the hall) so the lit and shadowed air differ, rather than
        # saturating to a white-out as 0.022 did in the first gate's still; the sun slants across the view.
        "environment": dark_environment(volumeDensity=0.008, volumeAnisotropy=0.55, shadowRange=120.0,
                                        shadowCascades=4),
        "lights": [
            {"name": "sun", "type": "directional", "direction": [0.5, -0.38, 0.78],
             "color": [1.0, 0.82, 0.62], "intensity": 6.0, "castsShadow": True, "volumetric": 1.0},
            {"name": "grille-spot", "type": "spot", "position": [17.0, 7.0, -22.0], "direction": [-0.05, -0.45, 0.89],
             "color": [0.7, 0.85, 1.0], "intensity": 900.0, "range": 45.0, "innerCone": 14.0, "outerCone": 24.0,
             "castsShadow": True, "volumetric": 1.0},
        ],
        "nodes": nodes,
    }
    return scene, "Light shafts: a low sun through a colonnade, a spot through a grille."


SLAB_DISTANCES = [4.0, 12.0, 24.0, 40.0, 60.0]


def e02():
    """Dense atmosphere. Five identical unlit slabs at 4, 12, 24, 40 and 60 m in front of a camera at z = 6, in a
    homogeneous medium (no height layer, no noise) that scatters nothing: the light is volumetric 0 and the
    emission is 0. So what reaches the eye from a slab is its own radiance times exp(-sigma_t * d), and the lab's
    analytic check compares the froxel grid's transmittance with that number. The slabs are staggered sideways so
    that all five are visible in one frame; the test reads each at its own known distance."""
    # On the view axis, as the Volumetric / Atmosphere Lab's own bench: the nearest occludes the rest in the still,
    # and the test reads each one by moving the nearer ones aside (tests/rendering/test_volumetric_light_gpu.cpp).
    nodes = [box(f"slab-{int(d):03d}", [0.0, 1.0, -d], [2.4, 6.0, 0.2], [0.8, 0.8, 0.8], unlit=True, casts=False)
             for d in SLAB_DISTANCES]
    scene = {
        "format": "avgen-scene",
        "version": 1,
        "name": "Volumetric Lighting Lab e02: dense atmosphere",
        "_note": e02.__doc__.replace("\n    ", " ").strip(),
        "camera": {"mode": 1, "position": [0.0, 1.0, 0.0], "target": [0.0, 1.0, -30.0], "fov": 70.0,
                   "orbitSpeed": 0.0},
        "environment": dark_environment(volumeDensity=0.02, volumeAnisotropy=0.0, background=[0.0, 0.0, 0.0],
                                        fogColor=[0.0, 0.0, 0.0]),
        "lights": [
            {"name": "key", "type": "directional", "direction": [-0.55, -0.62, -0.56], "color": [1.0, 1.0, 1.0],
             "intensity": 3.0, "castsShadow": False, "volumetric": 0.0},
        ],
        "nodes": nodes,
    }
    # Bloom off: a reading here is the air's transmittance and nothing the post chain adds.
    return scene, "Dense atmosphere: the analytic transmittance ladder.", {"post/bloom/enabled": False}


def e06():
    """Coloured scattering. Red, green and blue spots stand over a dark floor and point down through one grey
    haze; each beam's colour in the air must be its light's colour (the medium is neutral), and three posts cut
    a shadow through each beam. Anisotropy 0.3: the beams read from the side as well as toward the camera."""
    nodes = [box("floor", [0.0, -0.1, -20.0], [80.0, 0.2, 80.0], [0.2, 0.2, 0.21])]
    spots = []
    for i, (name, color) in enumerate([("red", [1.0, 0.12, 0.08]), ("green", [0.12, 1.0, 0.18]),
                                       ("blue", [0.15, 0.3, 1.0])]):
        x = -9.0 + 9.0 * i
        spots.append({"name": f"spot-{name}", "type": "spot", "position": [x, 14.0, -22.0],
                      "direction": [0.0, -1.0, 0.0], "color": color, "intensity": 2200.0, "range": 40.0,
                      "innerCone": 10.0, "outerCone": 20.0, "castsShadow": True, "volumetric": 1.0})
        nodes.append(box(f"post-{name}", [x + 0.8, 4.0, -22.0], [0.5, 8.0, 0.5], [0.5, 0.5, 0.5]))
    scene = {
        "format": "avgen-scene",
        "version": 1,
        "name": "Volumetric Lighting Lab e06: coloured scattering",
        "_note": e06.__doc__.replace("\n    ", " ").strip(),
        "camera": {"mode": 1, "position": [0.0, 5.0, 10.0], "target": [0.0, 6.0, -22.0], "fov": 55.0,
                   "orbitSpeed": 0.0},
        "environment": dark_environment(volumeDensity=0.03, volumeAnisotropy=0.3),
        "lights": spots,
        "nodes": nodes,
    }
    return scene, "Coloured scattering: red, green and blue spots through one neutral haze."


ENVIRONMENTS = [("e01-light-shafts", e01), ("e02-dense-atmosphere", e02), ("e06-coloured-scattering", e06)]


def outputs():
    files = {}
    for stem, make in ENVIRONMENTS:
        made = make()
        scene, note = made[0], made[1]
        parameters = made[2] if len(made) > 2 else None
        files[stem + ".scene.json"] = json.dumps(scene, indent=1) + "\n"
        files[stem + ".json"] = json.dumps(project(stem + ".scene.json", note, parameters), indent=1) + "\n"
    return files


def main(argv):
    files = outputs()
    if "--check" in argv:
        stale = []
        for name, text in files.items():
            path = os.path.join(HERE, name)
            if not os.path.exists(path) or open(path, encoding="utf-8").read() != text:
                stale.append(name)
        if stale:
            print("stale (run examples/labs/volumetric/build.py): " + ", ".join(stale))
            return 1
        print(f"ok: {len(files)} files match")
        return 0
    for name, text in files.items():
        with open(os.path.join(HERE, name), "w", encoding="utf-8") as f:
            f.write(text)
    print(f"wrote {len(files)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""Writes CHRONOS, The Devourer's scene file and projects (docs/prototypes/chronos/12-devourer-recon-plan.md).

Gate D1 is the portrait (Test A): the creature alone, lit in chiaroscuro against near black, with its baked life clip
(breathing jaw, one slow hungry opening, eye saccades) and a camera that barely moves. Two compositions, both kept
until the owner chooses: direct frontal and a frontal three-quarter.

    python3 examples/chronos/devourer/build_scene.py [--asset chronos-scan|chronos-creature]

The creature GLB is built by build_scan_creature.py (the scan route) or build_creature.py (the MPFB route) into
assets/chronos/build/, which is gitignored: the scene names it by relative path and does not carry it. The projects
name Feline Footwear and its Drums stem by relative path (audio is never committed). Out of examples/index.json until
the owner approves.
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSET = "chronos-scan"
if "--asset" in sys.argv:
    ASSET = sys.argv[sys.argv.index("--asset") + 1]
SCALE = 50.0              # the bust's height in metres: the head is about 33 m from beard to crown
FACE = (0.0, 32.0, 0.0)   # what the cameras look at: between the eyes and the mouth
SONG = 210.5


def spot(name, pos, target, color, intensity, inner, outer, shadow, role="key"):
    d = [target[i] - pos[i] for i in range(3)]
    n = math.sqrt(sum(x * x for x in d))
    return {"id": name, "name": name, "type": "spot", "role": role, "position": list(pos),
            "direction": [round(x / n, 4) for x in d], "color": color, "intensity": intensity,
            "range": 400.0, "innerCone": inner, "outerCone": outer, "castsShadow": shadow, "volumetric": 0.0}


def scene():
    return {
        "format": "avgen-scene", "version": 1, "name": "chronos-devourer",
        "_note": "CHRONOS, The Devourer (docs/prototypes/chronos/10-12): the Chronos portrait, Gate D1. The creature "
                 "GLB is built into assets/chronos/build/ (gitignored) by examples/chronos/devourer/build_*.py.",
        "camera": {"mode": 1, "position": [0.0, 33.0, 52.0], "target": list(FACE), "fov": 30.0, "near": 0.5,
                   "far": 2000.0},
        "environment": {"background": [0.012, 0.004, 0.004], "intensity": 0.0, "skyIntensity": 0.0,
                        "lightFromEnvironment": False, "fallbackAmbient": False, "volumeDensity": 0.0, "shadowCascades": 2,
                        "shadowRange": 300.0, "ambientOcclusion": False, "contactShadows": False,
                        "sky": {"enabled": False, "background": False}},
        "post": {"bloomIntensity": 0.06, "vignette": 0.42, "grain": 0.14, "contrast": 1.08, "saturation": 0.92},
        # Chiaroscuro (r02): one hard, dirty-amber key raking from high on the creature's left at three-quarters, so the
        # far side of the face falls into black and the near eye still catches it; a faint crimson from below and in
        # front, where the procession will come from; a burgundy rim from behind on the shadow side. The fallback
        # ambient is off (ADR-1882), so what is not lit is black. A frontal key -- the first set-up -- flattened the
        # whole face, which is what the Gate D1 lighting tests showed.
        "lights": [
            {"id": "key", "name": "key", "type": "directional", "role": "key", "direction": [0.78, -0.45, -0.43],
             "color": [1.0, 0.66, 0.36], "intensity": 5.0, "castsShadow": True},
            spot("under", (6.0, 2.0, 42.0), (0.0, 26.0, 0.0), [0.9, 0.1, 0.04], 450.0, 14.0, 32.0, False, "fill"),
            {"id": "rim", "name": "rim", "type": "directional", "role": "rim", "direction": [-0.55, -0.25, 0.8],
             "color": [0.62, 0.12, 0.2], "intensity": 1.5, "castsShadow": False},
        ],
        "nodes": [
            {"name": "chronos", "kind": "gltf", "asset": f"../../../assets/chronos/build/{ASSET}.glb",
             "position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [SCALE, SCALE, SCALE],
             "animation": {"state": "idle", "blend": 0.0}},
        ],
    }


def project(composition):
    # the camera drifts a metre or two over 24 s: alive, never travelling
    if composition == "three-quarter":
        a = math.radians(32.0)
        base = [round(52.0 * math.sin(a), 2), 34.0, round(52.0 * math.cos(a), 2)]
    else:
        base = [0.0, 33.0, 52.0]
    sway = [[0.0, 0.0, 0.0], [1.4, 0.6, -1.2], [-0.8, -0.4, 0.6], [0.0, 0.0, 0.0]]
    keys = [{"time": t, "value": [round(base[i] + sway[k][i], 2) for i in range(3)], "interp": "smooth"}
            for k, t in enumerate((0.0, 8.0, 16.0, 24.0))]
    return {
        "format": "avgen-project", "version": 4, "app": {"name": "CHRONOS - The Devourer"},
        "assets": {"scene": {"kind": "composition", "path": "devourer.scene.json"},
                   "audio": "../../../assets/audio/feline-footwear.wav",
                   "stems": [{"key": "drums", "label": "Drums", "role": "drums",
                              "file": "../../../assets/audio/stems/feline-footwear (Drums).wav"}]},
        "feeds": {"beat": "stem.drums"},
        "parameters": {"camera/position": base, "camera/target": list(FACE)},
        "sources": [], "routes": [],
        "timeline": {"enabled": True, "cues": [], "tracks": [
            {"target": "camera/position", "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
             "loopLength": 24.0, "keys": keys}]},
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": f"renders/devourer-{composition}.mp4"},
    }


if __name__ == "__main__":
    with open(os.path.join(HERE, "devourer.scene.json"), "w") as f:
        json.dump(scene(), f, indent=1)
    for comp in ("frontal", "three-quarter"):
        with open(os.path.join(HERE, f"devourer-{comp}.json"), "w") as f:
            json.dump(project(comp), f, indent=1)
    print("wrote devourer.scene.json, devourer-frontal.json, devourer-three-quarter.json")

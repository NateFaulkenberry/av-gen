#!/usr/bin/env python3
"""Writes CHRONOS, Temporal Singularity's scene and project (docs/prototypes/chronos/20-22).

Gate S0 is Test A: the singularity alone. Everything visible is one Background shader layer, singularity.wgsl, a pure
function of the render time: the field (a drifting, breathing main mass and two counter-spinning eddies), the dark
core, the broken rim, three accretion bands, radial streaks, fragments, the eddies' spirals and the full-screen
nebula, filaments and stars. No geometry yet.

    python3 examples/chronos/singularity/build_singularity.py

The project names Feline Footwear and its Drums stem by relative path (audio is never committed). Out of
examples/index.json until the owner approves.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def scene():
    return {
        "format": "avgen-scene", "version": 1, "name": "chronos-singularity",
        "_note": "CHRONOS, Temporal Singularity (docs/prototypes/chronos/20-22). Gate S0: the singularity alone, drawn "
                 "by the project's background shader layer singularity.wgsl.",
        "camera": {"mode": 1, "position": [0.0, 0.0, 30.0], "target": [0.0, 0.0, 0.0], "fov": 40.0, "near": 0.5,
                   "far": 2000.0},
        "environment": {"background": [0.0, 0.0, 0.0], "intensity": 0.0, "skyIntensity": 0.0,
                        "lightFromEnvironment": False, "fallbackAmbient": False, "volumeDensity": 0.0,
                        "ambientOcclusion": False, "contactShadows": False,
                        "sky": {"enabled": False, "background": False}},
        "lights": [],
        "nodes": [],
    }


def project():
    return {
        "format": "avgen-project", "version": 4, "app": {"name": "CHRONOS - Temporal Singularity"},
        "assets": {"scene": {"kind": "composition", "path": "singularity.scene.json"},
                   "audio": "../../../assets/audio/feline-footwear.wav",
                   "stems": [{"key": "drums", "label": "Drums", "role": "drums",
                              "file": "../../../assets/audio/stems/feline-footwear (Drums).wav"}]},
        "feeds": {"beat": "stem.drums"},
        "shaders": [{"path": "singularity.wgsl", "stage": "background"}],
        "parameters": {
            "camera/exposure/mode": 0,
            "camera/exposure/compensation": -0.3,
            "post/bloom/enabled": True,
            "post/bloom/intensity": 0.32,
            "post/bloom/threshold": 1.1,
            "post/tonemap/chroma-retention": 0.65,
            "post/output/vignette": 0.18,
            "post/output/grain": 0.03,
            "post/grade/contrast": 1.06,
            "post/grade/saturation": 1.08,
            "temporal/echo/enabled": False,
            "temporal/mosh/enabled": False,
        },
        "sources": [], "routes": [],
        "timeline": {"enabled": True, "cues": [], "tracks": []},
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/singularity.mp4"},
    }


if __name__ == "__main__":
    with open(os.path.join(HERE, "singularity.scene.json"), "w") as f:
        json.dump(scene(), f, indent=1)
    with open(os.path.join(HERE, "singularity.json"), "w") as f:
        json.dump(project(), f, indent=1)
    print("wrote singularity.scene.json, singularity.json")

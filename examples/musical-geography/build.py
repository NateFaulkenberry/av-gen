#!/usr/bin/env python3
"""Musical Geography: assemble the scene and the project from look.py (art direction) and world.py (the land).

    python3 examples/musical-geography/build.py [--state day|sunset|night|dawn] [--interim] [--geography]
                                                [--camera low|reveal] [--out DIR]

--state      the palette state: pins DayNight's phase there, and bakes that state's terrain colours into the tile
             program (until request 1972 makes them curves).
--interim    stand today's procedural slab grid (world.interim_nodes) so the look can be judged before TileTerrain.
--geography  write world.GEOGRAPHY into the scene (only once the engine reads it).
--camera     a still camera for look development (the flight comes in Phase 5).

Writes <out>/musical-geography.scene.json and <out>/musical-geography.json (both gitignored). Pure Python.
"""
from __future__ import annotations

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import look   # noqa: E402
import world  # noqa: E402

CAMERAS = {
    # still cameras (engine axes, y up, north = -z): a low pass over the land, and the reveal altitude for §11
    "low": {"position": [-20.0, 9.0, 20.0], "target": [-10.0, 4.0, -120.0], "fov": 50.0},
    "reveal": {"position": [60.0, 85.0, 0.0], "target": [-60.0, 0.0, -220.0], "fov": 36.0},
}


def scene_doc(state, interim, geography, camera):
    s = look.STATES[state]
    env = {
        "intensity": s["amb_int"],
        "background": look.hexlin(s["sky_hor"]),
        "proceduralSkyBackground": True,
        "skybox": True,
        "shadowRange": 220.0,
        "depthPrepass": True,
        "sky": look.sky(state),
        "dayNight": look.day_night(phase=s["phase"]),
    }
    env.update(look.fog_medium(state))
    nodes = world.interim_nodes() if interim else []
    doc = {
        "format": "avgen-scene", "version": 1, "name": "musical-geography",
        "camera": dict({"mode": 1, "orbitSpeed": 0.0}, **CAMERAS[camera]),
        "environment": env,
        "nodes": nodes,
        "lights": [look.sun_light(state)],
        "materialPrograms": [look.tile_program(state)],
    }
    if geography:
        doc["geography"] = world.GEOGRAPHY
    return doc


AUDIO = os.path.join(HERE, "..", "..", "assets", "audio", "feline-footwear.wav")


def project_doc(scene_rel, state):
    return {
        "format": "avgen-project", "version": 4,
        "assets": {"scene": {"kind": "composition", "path": scene_rel},
                   "audio": AUDIO},
        "parameters": look.post(state),
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video",
                   "path": "renders/musical-geography.mp4"},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--state", default="day", choices=sorted(look.STATES))
    ap.add_argument("--interim", action="store_true")
    ap.add_argument("--geography", action="store_true")
    ap.add_argument("--camera", default="low", choices=sorted(CAMERAS))
    ap.add_argument("--out", default=os.path.join(HERE, "scene"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    scene_path = os.path.join(a.out, "musical-geography.scene.json")
    with open(scene_path, "w") as f:
        json.dump(scene_doc(a.state, a.interim, a.geography, a.camera), f, indent=1)
    proj_dir = a.out   # look-development projects live beside the scene (gitignored) until the scene is approved
    proj_path = os.path.join(proj_dir, "musical-geography.json")
    with open(proj_path, "w") as f:
        json.dump(project_doc(os.path.relpath(scene_path, proj_dir), a.state), f, indent=1)
    print("wrote", scene_path, "and", proj_path)


if __name__ == "__main__":
    main()

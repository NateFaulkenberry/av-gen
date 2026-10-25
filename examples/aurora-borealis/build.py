#!/usr/bin/env python3
"""Aurora Borealis: write the live scene's generated files.

    python3 examples/aurora-borealis/build.py

Pure Python (no Blender, no downloads), about a minute. It writes

  examples/aurora-borealis/scene/            the scene file and its faceted GLBs (generated, gitignored)
  examples/aurora-borealis/aurora-borealis.json   the committed live project (a pure function of these scripts)

The scene (pass 5): a still lake ringed by faceted mountains at dusk -- a horseshoe open toward a static camera --
under a sky of ribbon aurora (world.py builds the land, the lakebed and the fish; look.py holds the art direction --
palette, the land's program, sky, aurora, water, the light in the water, the fireballs; music.py the stem-driven
signals and routes).
See docs/prototypes/aurora-borealis/.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import look  # noqa: E402
import music  # noqa: E402
import world  # noqa: E402

SCENE_DIR = os.path.join(HERE, "scene")
PROJECT = os.path.join(HERE, "aurora-borealis.json")             # Sabertooth, the default
PROJECTS = {"sabertooth": "aurora-borealis.json", "feline": "aurora-borealis-feline.json"}
SCENE_FILE = "aurora-borealis.scene.json"

# pass 5 (the owner: "static camera"): one composition, held for the whole song. The camera stands just inside the lake's
# near end, a little above the water: the lake fills the lower middle of the frame, the horseshoe's arms rise on the
# left and right and its back closes the view, and the view is pitched up so the aurora takes the upper half.
CAM_POS = (0.0, look.CAM_H_WATER, 0.0)
CAM_PITCH_DEG = float(os.environ.get("AB_PITCH", "-1.0"))
CAM_FOV = 50.0


def camera_doc():
    """The fixed camera (no timeline tracks: nothing moves it). Explicit clip planes: the automatic far plane is 50x the
    world's radius, and at that ratio the sky's direction came out NaN on some frames (pass 1)."""
    tgt = (CAM_POS[0], CAM_POS[1] + 1000.0 * math.tan(math.radians(CAM_PITCH_DEG)), CAM_POS[2] - 1000.0)
    return {"mode": 1, "position": list(CAM_POS), "target": [round(v, 3) for v in tgt], "fov": CAM_FOV,
            "orbitSpeed": 0.0, "near": 1.0, "far": 45000.0}


def scene_doc(camera):
    nodes = [look.water_node()]
    # each land mesh in halves: the left (x < 0) takes the Moog's program, the right the bass's
    for name in [n + side for n in world.LAND for side in "LR"]:
        prog = look.ROCK_L if name.endswith("L") else look.ROCK_R
        nodes.append({"name": name, "kind": "procedural", "procedural": {
            "source": {"kind": "mesh", "asset": "ab-%s.glb" % name}, "distribution": {"kind": "single"},
            "material": look.rock_material(prog), "castsShadow": False}})
    nodes.append(look.bed_node())
    nodes.append(look.fish_node())
    return {"format": "avgen-scene", "version": 1, "name": "aurora-borealis", "camera": camera,
            "environment": look.environment(), "nodes": nodes, "lights": [look.unshadowed_key()],
            "materialPrograms": [look.rock_program(look.ROCK_L), look.rock_program(look.ROCK_R), look.bed_program(),
                                 look.fish_program()]}


def project_doc(song):
    doc = {
        "format": "avgen-project", "version": 4, "app": {"name": "Aurora Borealis"},
        "assets": {"scene": {"kind": "composition", "path": "scene/" + SCENE_FILE}},
        "sonic": {"live": True, "response": {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0,
                                             "release": 1.0}},
        "parameters": look.parameters(),
        "live": {"overrides": {"lodBias": 1.0}},
        "effects": [look.aurora_effect(), look.stars_effect()] + look.ball_effects() + look.river_effects(),
        "palette": look.palette(),
        "timeline": {"enabled": True, "cues": [], "tracks": []},
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/aurora-borealis.mp4"},
    }
    music.apply(doc, song)
    return doc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scene-dir", default=SCENE_DIR)
    ap.add_argument("--project-dir", default=HERE, help="where the two song projects are written")
    ap.add_argument("--skip-meshes", action="store_true", help="keep the GLBs already in the scene folder")
    a = ap.parse_args()
    os.makedirs(a.scene_dir, exist_ok=True)
    if not a.skip_meshes:
        counts = world.generate(a.scene_dir)
        print("meshes: %s (%d triangles)" % (", ".join("%s %d" % kv for kv in counts.items()), sum(counts.values())))
    with open(os.path.join(a.scene_dir, SCENE_FILE), "w") as f:
        json.dump(scene_doc(camera_doc()), f, indent=1)
        f.write("\n")
    for song, name in PROJECTS.items():
        with open(os.path.join(a.project_dir, name), "w") as f:
            json.dump(project_doc(song), f, indent=1)
            f.write("\n")
    print("static camera; wrote %s and %s" % (os.path.relpath(a.scene_dir),
                                                  ", ".join(PROJECTS.values())))


if __name__ == "__main__":
    main()

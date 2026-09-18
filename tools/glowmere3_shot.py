#!/usr/bin/env python3
"""Render one still of Glowmere Valley 3 from a named camera at a chosen second.

    python3 tools/glowmere3_shot.py "The Grove" 26.0 grove-26s

The fixed shot list in the scene can only be in one place at a time, and the cast does not wait
its turn: the crossing and the character-awareness approach both happen inside the first forty
seconds. So a deliverable still names its camera rather than borrowing whichever one the cut
happened to be on.

It writes a temporary scene beside the original -- a scene's asset paths are relative to the scene
file, so a copy anywhere else resolves none of them -- with `camera` set to the named camera, and
deletes it afterwards. Nothing about the world, the cast or the second differs from the shipped
file.
"""
import json, collections, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
OD = collections.OrderedDict

camera_name, second, label = sys.argv[1], float(sys.argv[2]), sys.argv[3]
source = sys.argv[4] if len(sys.argv) > 4 else "glowmere-valley-3"
# An optional shift applied to every scatter layer's seed: a different world for the cast to
# perceive, on the same terrain, with their authored config unchanged to the byte. This is the
# picture half of ADR-338 §4's variation arm -- the numbers say the itinerary moves, and a frame
# from the same camera at the same second is what shows it.
ecology_shift = int(sys.argv[5]) if len(sys.argv) > 5 else 0

d = json.load(open("examples/world/%s.scene.json" % source), object_pairs_hook=OD)
cam = next(c for c in d["cameraDirection"]["cameras"] if c.get("name") == camera_name)
d["camera"] = OD([("mode", 1), ("position", cam["position"]), ("target", cam["target"]),
                  ("fov", cam["fov"]), ("orbitSpeed", 0.0)])
# One shot, the whole film, on the camera asked for: the cut cannot move off it mid-still.
d["cameraDirection"]["shots"] = [OD([("camera", cam["id"]), ("start", 0.0), ("end", 1.0e4),
                                     ("transition", "cut"), ("locked", True),
                                     ("label", label)])]
d["cameraDirection"]["default"] = cam["id"]
if ecology_shift:
    for n in d["nodes"]:
        for layer in n.get("scatter", []):
            layer["seed"] = layer.get("seed", 0) + ecology_shift
tmp_scene = "examples/world/_v3shot.scene.json"
json.dump(d, open(tmp_scene, "w"), indent=1)

project = json.load(open("examples/world/%s.json" % source), object_pairs_hook=OD)
project["assets"]["scene"]["path"] = OD([("path", "_v3shot.scene.json")])
tmp_project = "examples/world/_v3shot.json"
json.dump(project, open(tmp_project, "w"), indent=1)
try:
    out = "renders/v3-%s" % label
    subprocess.run(["tools/gpu-lock.sh", "./build/release/src/avgen", "--project", tmp_project,
                    "--render", out, "--range", "%f:%f" % (second, second + 0.034)],
                   check=True, stdout=subprocess.DEVNULL)
    print("wrote examples/world/%s (camera '%s', t = %.2f s, ecology seed shift %d)"
          % (out, camera_name, second, ecology_shift))
finally:
    for f in (tmp_scene, tmp_project):
        if os.path.exists(f):
            os.remove(f)

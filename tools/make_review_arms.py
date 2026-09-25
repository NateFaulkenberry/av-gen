#!/usr/bin/env python3
"""The Glowmere review build's arms: the multicam film, one variable changed per arm (ADR-827).

  ai-off      the five aliens run the pre-awareness behaviours of glowmere-valley-2.scene.json
              (explore, liveliness, lookAt; beat hops removed so awareness is the only variable)
  procedural  the five aliens on the Phase B provider seam (`proceduralMotion`)
  matcher     procedural, plus motion matching on the scout's baked database (ADR-825; build it
              with tools/make_scout_motion_db.sh)

Each arm is written beside the film as `_arm-<name>-<file>` so every relative asset path resolves;
the film itself is the "ai-on" arm. Nothing here is tracked output: delete the `_arm-*` files after.

Usage: tools/make_review_arms.py [arm ...]   (default: all three). Prints each arm's project path.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WORLD = ROOT / "examples" / "world"
FILM = WORLD / "glowmere-valley-2-multicam.json"
ALIENS = ("rook", "tide", "sage", "ember", "vane")


def arm_scene(name, scene):
    if name == "ai-off":
        plain = {e["name"]: e for e in json.loads((WORLD / "glowmere-valley-2.scene.json").read_text())["entities"]}
        for e in scene["entities"]:
            if e["name"] in ALIENS:
                behaviors = json.loads(json.dumps(plain[e["name"]]["behaviors"]))
                for b in behaviors:
                    for key in ("jumpSignal", "jumpRange"):
                        b.pop(key, None)
                e["behaviors"] = behaviors
                for key in ("perception", "personality"):
                    e.pop(key, None)
    elif name in ("procedural", "matcher"):
        for e in scene["entities"]:
            if e["name"] in ALIENS:
                e["proceduralMotion"] = True
                if name == "matcher":
                    e["motionMatching"] = {
                        "joints": ["foot.l", "foot.r", "head.x"], "contacts": ["foot.l", "foot.r"],
                        "trajectory": [0.2, 0.4, 0.6],
                        "pack": "../../assets/aliens/scout-pack",
                        "database": "../../assets/aliens/scout-pack/databases/scout.motiondb"}
    else:
        raise SystemExit(f"unknown arm '{name}'")
    return scene


def main():
    arms = sys.argv[1:] or ["ai-off", "procedural", "matcher"]
    project = json.loads(FILM.read_text())
    scene_rel = project["assets"]["scene"]["path"]["path"]
    for name in arms:
        scene = arm_scene(name, json.loads((WORLD / scene_rel).read_text()))
        out_scene = WORLD / f"_arm-{name}-{scene_rel}"
        out_scene.write_text(json.dumps(scene, indent=2) + "\n")
        p = json.loads(json.dumps(project))
        p["assets"]["scene"] = {"kind": "composition", "path": {"path": out_scene.name}}
        out = WORLD / f"_arm-{name}-{FILM.name}"
        out.write_text(json.dumps(p, indent=2) + "\n")
        print(out)


if __name__ == "__main__":
    main()

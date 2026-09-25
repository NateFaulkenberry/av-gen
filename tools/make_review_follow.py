#!/usr/bin/env python3
"""A review variant of a Glowmere project: the same film, one locked shot on a camera welded to a body.

The owner reviews motion on specific bodies (a hop, a reaction), and the film's own cut may be
looking elsewhere at that second. This writes `_review-<body>-<project>` beside the originals (so
every relative asset path still resolves) with the scene's camera direction replaced by a single
follow camera on `<body>`, and the project's own camera overrides removed. The simulation is
untouched: cameras do not steer entities (Phase D §32), and an offline render lifts the distance
detail limits, so the bodies do in the variant exactly what they do in the film.

Usage: tools/make_review_follow.py <project.json> <body> [--offset x,y,z] [--aim-height h]
Prints the variant project's path. Delete the two `_review-*` files when done; they are not tracked.
"""
import argparse
import json
import pathlib


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("project")
    ap.add_argument("body")
    ap.add_argument("--offset", default="7,3.5,7")
    ap.add_argument("--aim-height", type=float, default=1.8)
    ap.add_argument("--fov", type=float, default=40.0)
    ap.add_argument("--tag", default="")
    ap.add_argument("--fixed", default="", help="x,y,z: stand here and only aim at the body, so its height reads")
    ap.add_argument("--target", default="", help="x,y,z with --fixed: look HERE instead of at the body -- a locked-off frame the body rises through")
    a = ap.parse_args()
    project_path = pathlib.Path(a.project).resolve()
    project = json.loads(project_path.read_text())
    scene_ref = project["assets"]["scene"]["path"]
    scene_rel = scene_ref["path"] if isinstance(scene_ref, dict) else scene_ref
    scene_path = (project_path.parent / scene_rel).resolve()
    scene = json.loads(scene_path.read_text())
    offset = [float(v) for v in a.offset.split(",")]
    rig = {"id": 2, "name": f"Review {a.body}", "slug": "review", "placement": "free", "fov": a.fov,
           "aimNode": a.body, "aimOffset": [0.0, a.aim_height, 0.0], "autoDirector": False}
    if a.fixed:
        rig["position"] = [float(v) for v in a.fixed.split(",")]
        rig["target"] = rig["position"]
        if a.target:
            rig["target"] = [float(v) for v in a.target.split(",")]
            del rig["aimNode"]
            del rig["aimOffset"]
    else:
        rig["followNode"] = a.body
        rig["followOffset"] = offset
    scene["cameraDirection"] = {
        "cameras": [{"id": 1, "name": "Hero Free Roam", "autoDirector": False}, rig],
        "shots": [{"camera": 2, "start": 0.0, "end": 100000.0, "transition": "cut", "locked": True, "label": "review"}],
    }
    tag = f"-{a.tag}" if a.tag else ""
    out_scene = scene_path.parent / f"_review-{a.body}{tag}-{scene_path.name}"
    out_scene.write_text(json.dumps(scene, indent=2) + "\n")
    project["assets"]["scene"] = {"kind": project["assets"]["scene"].get("kind", "composition"),
                                  "path": {"path": out_scene.name}}
    for key in ("cameraShotSpans", "cameraAimFollow", "autoDirector"):
        project.pop(key, None)
    # The film's own cut, baked into camera tracks, wins over any camera the scene declares.
    timeline = project.get("timeline")
    if isinstance(timeline, dict):
        timeline["tracks"] = [t for t in timeline.get("tracks", []) if not str(t.get("target", "")).startswith("camera/")]
    out_project = project_path.parent / f"_review-{a.body}{tag}-{project_path.name}"
    out_project.write_text(json.dumps(project, indent=2) + "\n")
    print(out_project)


if __name__ == "__main__":
    main()

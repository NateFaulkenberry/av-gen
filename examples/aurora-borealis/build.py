#!/usr/bin/env python3
"""Aurora Borealis: write the live scene's generated files.

    python3 examples/aurora-borealis/build.py

Pure Python (no Blender, no downloads), about a minute. It writes

  examples/aurora-borealis/scene/            the scene file and its faceted GLBs (generated, gitignored)
  examples/aurora-borealis/aurora-borealis.json   the committed live project (a pure function of these scripts)

The scene: pass 3's closed-loop river with its seven mild bends, through a valley of faceted mountains at dusk (pass 7
restored pass 3's landscape and camera), under a sky of ribbon aurora, flown down slowly by a timeline camera (world.py builds the land and the path; look.py holds the art direction --
palette, the land's program, sky, aurora, water; music.py the stem-driven signals and routes).
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

CAM_H = 65.0              # metres above the water
CAM_SPEED = 22.0          # metres a second along the river
AHEAD = 1600.0            # the target this far ahead on the centreline
PITCH_DEG = 1.5           # the view pitched up so the horizon sits low and the sky takes the frame
SWAY = 0.22               # the camera drifts across the river by this fraction of its half-width
KEYS_PER_LOOP = 500


def camera_tracks():
    """Pass 3's camera, restored in pass 7 (bd0a44ca, less the shooting stars' azimuth tracks): a seamless loop down the
    river at a constant speed: position on (near) the centreline, the target AHEAD metres
    further along it and pitched up, a gentle roll into the bends. Deterministic (seek equals play)."""
    arcs, ths = world.theta_at_arc()
    L = arcs[-1]
    period = round(L / CAM_SPEED, 3)
    kp, kt, kr = [], [], []
    for i in range(KEYS_PER_LOOP + 1):
        s = L * i / KEYS_PER_LOOP
        th = world.lerp_table(arcs, ths, s % L)
        tha = world.lerp_table(arcs, ths, (s + AHEAD) % L)
        c = world.axis_centre(th)
        # sideways drift: a slow sine across the river, two cycles a loop, so the view is not always dead centre
        # the camera flies the valley's axis (smoothed), not every meander; a slow sideways drift
        side = SWAY * 180.0 * math.sin(2.0 * world.TAU * i / KEYS_PER_LOOP + 0.4)
        r = world.rc_axis(th) + side
        pos = (r * math.sin(th), CAM_H, r * math.cos(th))
        ca = world.axis_centre(tha)
        dist = math.hypot(ca[0] - pos[0], ca[2] - pos[2])
        tgt = (ca[0], CAM_H + dist * math.tan(math.radians(PITCH_DEG)), ca[2])
        # roll: the turn's signed curvature (heading change over the next stretch), a few degrees at most
        th2 = world.lerp_table(arcs, ths, (s + 160.0) % L)
        h1 = math.atan2(ca[0] - c[0], ca[2] - c[2])
        c2 = world.axis_centre(th2)
        h0 = math.atan2(c2[0] - c[0], c2[2] - c[2])
        turn = (h1 - h0 + math.pi) % world.TAU - math.pi
        roll = max(-3.5, min(3.5, -math.degrees(turn) * 0.35))
        t = round(period * i / KEYS_PER_LOOP, 4)
        kp.append({"time": t, "value": [round(v, 3) for v in pos], "interp": "smooth"})
        kt.append({"time": t, "value": [round(v, 3) for v in tgt], "interp": "smooth"})
        kr.append({"time": t, "value": round(roll, 3), "interp": "smooth"})
    kr[-1]["value"] = kr[0]["value"]

    def track(target, keys):
        return {"target": target, "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
                "loopLength": period, "keys": keys}
    # explicit clip planes: the automatic far plane is 50x the world's radius (about 1,000 km here), and at that ratio the
    # sky's direction, unprojected from the far plane, came out NaN on some frames and the bloom blacked the whole frame
    camera = {"mode": 1, "position": kp[0]["value"], "target": kt[0]["value"], "fov": 50.0, "orbitSpeed": 0.0,
              "near": 1.0, "far": 45000.0}
    return [track("camera/position", kp), track("camera/target", kt), track("camera/roll", kr)], \
        camera, period


def scene_doc(camera):
    nodes = [look.water_node()]
    # pass 3: the inner bank (always on the camera's left) takes the left program, the outer bank and the ranges the
    # right one (22-research-pass3-scene.md s5.1)
    for name in ["valleyL", "valleyR"] + [r[0] for r in world.RANGES]:
        prog = look.ROCK_L if name == "valleyL" else look.ROCK_R   # left: the bass, right and the ranges: the Moog
        nodes.append({"name": name, "kind": "procedural", "procedural": {
            "source": {"kind": "mesh", "asset": "ab-%s.glb" % name}, "distribution": {"kind": "single"},
            "material": look.rock_material(prog), "castsShadow": False}})
    nodes.append(look.bed_node())
    nodes.append(look.fish_node())
    return {"format": "avgen-scene", "version": 1, "name": "aurora-borealis", "camera": camera,
            "environment": look.environment(), "nodes": nodes, "lights": [look.unshadowed_key()],
            "materialPrograms": [look.rock_program(look.ROCK_L), look.rock_program(look.ROCK_R), look.bed_program(),
                                 look.fish_program()]}


def project_doc(tracks, period, song):
    doc = {
        "format": "avgen-project", "version": 4, "app": {"name": "Aurora Borealis"},
        "assets": {"scene": {"kind": "composition", "path": "scene/" + SCENE_FILE}},
        "sonic": {"live": True, "response": {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0,
                                             "release": 1.0}},
        "parameters": look.parameters(),
        "live": {"overrides": {"lodBias": 1.0}},
        "effects": [look.aurora_effect(), look.stars_effect()] + look.ball_effects() + look.river_effects(),
        "palette": look.palette(),
        "timeline": {"enabled": True, "cues": [], "tracks": tracks},
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
    tracks, camera, period = camera_tracks()
    with open(os.path.join(a.scene_dir, SCENE_FILE), "w") as f:
        json.dump(scene_doc(camera), f, indent=1)
        f.write("\n")
    for song, name in PROJECTS.items():
        with open(os.path.join(a.project_dir, name), "w") as f:
            json.dump(project_doc(tracks, period, song), f, indent=1)
            f.write("\n")
    print("camera loop %.1f s; wrote %s and %s" % (period, os.path.relpath(a.scene_dir),
                                                  ", ".join(PROJECTS.values())))


if __name__ == "__main__":
    main()

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
PROJECTS = {"sabertooth": "aurora-borealis.json"}
SCENE_FILE = "aurora-borealis.scene.json"

CAM_H = 65.0              # metres above the water
CAM_SPEED = 22.0          # metres a second along the river
AHEAD = 900.0             # the target this far ahead on the course (pass 10: was 1600, nearer so the river below stays centred)
PITCH_DEG = 1.5           # the view pitched up so the horizon sits low and the sky takes the frame
SWAY = 0.22               # the camera drifts across the river by this fraction of its half-width
KEYS_PER_LOOP = 500


# Pass 10 (the owner: "try to have the camera stay a bit more centered to the river"): pass 3's camera flew the valley's
# smoothed axis, so through the bends the river drifted to one side of the frame. It now follows the river's course
# blended in -- the course smoothed over +-SMOOTH_M metres, so no meander swings it -- POS_FOLLOW of the way for its
# position and TARGET_FOLLOW for where it looks, with the sideways drift cut to SWAY_KEEP. Same height, speed and pitch.
POS_FOLLOW, TARGET_FOLLOW, SWAY_KEEP, SMOOTH_M = 0.9, 0.95, 0.25, 220.0


def follow_r(th, k=None):
    """The radius the camera keeps: the valley's axis, blended `k` of the way to the river's smoothed course."""
    k = POS_FOLLOW if k is None else k
    w = SMOOTH_M / world.R0
    smooth = sum(world.rc(th + w * (j / 4.0 - 1.0)) for j in range(9)) / 9.0
    return world.rc_axis(th) + k * (smooth - world.rc_axis(th))


def follow_centre(th, k=None):
    r = follow_r(th, k)
    return (r * math.sin(th), 0.0, r * math.cos(th))


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
        c = follow_centre(th)
        # sideways drift: a slow sine across the river, two cycles a loop, so the view is not always dead centre
        # the camera flies the valley's axis (smoothed), not every meander; a slow sideways drift
        side = SWAY * SWAY_KEEP * 180.0 * math.sin(2.0 * world.TAU * i / KEYS_PER_LOOP + 0.4)
        r = follow_r(th) + side
        pos = (r * math.sin(th), CAM_H, r * math.cos(th))
        ca = follow_centre(tha, TARGET_FOLLOW)
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
    nodes += look.fish_nodes()
    return {"format": "avgen-scene", "version": 1, "name": "aurora-borealis", "camera": camera,
            "environment": look.environment(), "nodes": nodes, "lights": [look.unshadowed_key()],
            "materialPrograms": [look.rock_program(look.ROCK_L), look.rock_program(look.ROCK_R), look.bed_program()]
                                + look.fish_programs()}


FISH_KEYS = 40           # keys per lap
FISH_WAG_DEG = 9.0       # the wag: the body's heading swings this many degrees either way (music.py's two LFOs)


def fish_tracks():
    """Pass 10 (the owner: "animate fish if possible"): each fish swims its circle (world.fish_list), its position and
    heading keyed round one lap and looping, so the swim is seamless and a function of time (seek equals play). The
    heading turns once a lap, kept continuous across the keys. Its height never changes, so no fish nears the surface."""
    tracks = []
    for k, f in enumerate(world.fish_list()):
        period = round(f[5], 3)
        kp, kr = [], []
        prev = None
        for i in range(FISH_KEYS + 1):
            t = period * i / FISH_KEYS
            (px, py, pz), h = world.fish_at(f, t)
            deg = math.degrees(h)
            if prev is not None:      # unwrapped, so the interpolation turns the short way
                while deg - prev > 180.0:
                    deg -= 360.0
                while deg - prev < -180.0:
                    deg += 360.0
            prev = deg
            kp.append({"time": round(t, 4), "value": [round(px, 3), round(py, 3), round(pz, 3)], "interp": "smooth"})
            kr.append({"time": round(t, 4), "value": [0.0, round(deg, 2), 0.0], "interp": "smooth"})
        # the lap's heading spans 360 degrees; kept inside the rotation's +-360 range
        lo, hi = min(key["value"][1] for key in kr), max(key["value"][1] for key in kr)
        shift = -360.0 if hi > 360.0 else (360.0 if lo < -360.0 else 0.0)
        for key in kr:
            key["value"][1] = round(key["value"][1] + shift, 2)
        for target, keys in (("nodes/fish%02d/position" % k, kp), ("nodes/fish%02d/rotation" % k, kr)):
            tracks.append({"target": target, "component": -1, "enabled": True, "timeBase": "seconds",
                           "mode": "replace", "loopLength": period, "keys": keys})
    return tracks


def fish_wag(doc):
    """The fish's wag: two sine LFOs (1.4 and 1.1 Hz), each fish on one, swinging its heading +-FISH_WAG_DEG."""
    doc["sources"] += [{"kind": "lfo", "name": "fishWagA", "settings": {"shape": "sine"}},
                       {"kind": "lfo", "name": "fishWagB", "settings": {"shape": "sine"}}]
    doc["parameters"].update({"sources/fishWagA/rate": 1.4, "sources/fishWagB/rate": 1.1})
    for k in range(world.FISH_COUNT):
        doc["routes"].append({"source": "lfo.fishWag%s" % "AB"[k % 2], "target": "nodes/fish%02d/rotation" % k,
                              "component": 1, "amount": FISH_WAG_DEG, "op": "add", "polarity": "bipolar"})


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
        "timeline": {"enabled": True, "cues": [], "tracks": tracks + fish_tracks()},
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/aurora-borealis.mp4"},
    }
    music.apply(doc, song)
    fish_wag(doc)
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

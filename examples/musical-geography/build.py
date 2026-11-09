#!/usr/bin/env python3
"""Musical Geography: assemble the scene and the project from look.py (art direction) and world.py (the land).

    python3 examples/musical-geography/build.py [--state day|sunset|night|dawn] [--interim]
                                                [--camera low|reveal|land|bay|topdown|overview] [--out DIR]

--state      the palette state: pins DayNight's phase there, and bakes that state's terrain colours into the tile
             program (until request 1972 makes them curves).
--interim    the pre-generator stand-in (world.interim_nodes) instead of the "tiles" rings.
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

# Still cameras for look development (engine axes, y up, north = -z). The camera's y and the target's y are ABOVE THE
# GROUND there (terrain_mirror) when "ground" is set. The seed-3 world: a deep bay at the origin opening south, a
# ridge on its west shore, hills to the north-west and north-east (terrain_mirror.py --map).
CAMERAS = {
    "low": {"position": [60.0, 4.0, -300.0], "target": [40.0, -6.0, -700.0], "fov": 50.0},          # over the bay
    "reveal": {"position": [-900.0, 90.0, 300.0], "target": [-200.0, 0.0, -350.0], "fov": 36.0, "ground": True},
    "land": {"position": [-120.0, 7.0, -640.0], "target": [-420.0, 0.0, -880.0], "fov": 40.0, "ground": True},
    "bay": {"position": [60.0, 60.0, -250.0], "target": [-150.0, 0.0, -800.0], "fov": 40.0},
    "topdown": {"position": [-60.0, 150.0, -640.0], "target": [-60.0, 0.0, -641.0], "fov": 40.0, "ground": True},
    "overview": {"position": [0.0, 900.0, 0.5], "target": [0.0, 0.0, 0.0], "fov": 70.0},
    # CP-A composed candidates (frame_preview.py): less sky; the reveal frames coast, hills and path; the coast view
    # sets land against sea
    "revealC": {"position": [60.0, 120.0, -430.0], "target": [-118.0, 0.0, -668.0], "fov": 36.0},
    "coastC": {"position": [-380.0, 22.0, 120.0], "target": [-700.0, -40.0, -260.0], "fov": 40.0},
    "valleyC": {"position": [190.0, 95.0, -400.0], "target": [300.0, -40.0, -800.0], "fov": 36.0},
    "lowC": {"position": [-60.0, 25.0, -600.0], "target": [-330.0, -8.0, -860.0], "fov": 40.0, "ground": True},  # ~25 m up
    "nightC": {"position": [-60.0, 45.0, -560.0], "target": [-330.0, -30.0, -870.0], "fov": 40.0},
}


# Ambient-occlusion fade band per view, metres (engineering's suggestion): the reveal's near ground starts 200 m out.
AO_BANDS = {"revealC": (200.0, 420.0), "valleyC": (120.0, 300.0)}


def camera(name):
    c = dict(CAMERAS[name])
    if c.pop("ground", False):
        import numpy as np
        import terrain_mirror
        for key in ("position", "target"):
            x, y, z = c[key]
            h = float(terrain_mirror.tile_sample(world.TILES, np.array([x]), np.array([z]))["height"][0])
            c[key] = [x, round(h + y, 2), z]
    return c


TILE_SCALE = 4.0   # owner, 2026-11-09: 4 m tiles (--tile-scale overrides)


# ---- the CP-B flight (ADR-1956), in metres: bay -> low over the moving sea -> up the river valley -> climb to the reveal.
# The camera looks where it travels (lookAhead), never above the horizon (pitchMax 0), a little down (pitchBias), so the
# climb keeps the land in frame; banks only from the turns. Tile-scale independent.
FLIGHT = {
    "ground": "mgTiles1", "startTime": 30.0, "duration": 90.0, "speed": 22.0, "fov": 46.0,
    "route": [
        {"position": [60, -250], "altitude": 12},                              # the bay
        {"position": [60, -430], "altitude": 6},                               # low over the moving sea
        {"snap": "shore", "position": [40, -600], "snapRadius": 40, "altitude": 10},   # cross the shore
        {"follow": "path", "from": 0.0, "to": 0.5, "offset": 0, "altitude": 12},       # along the lit path
        {"position": [-300, -1050], "altitude": 150, "speed": 30, "nadir": 6},  # the top-down pass, ~37 tiles wide
        {"position": [100, -1000], "altitude": 60, "speed": 30},               # dive east toward the river
        {"follow": "river", "from": 0.55, "to": 0.9, "offset": 0, "altitude": 12},     # up the valley on the river
        {"position": [300, -1560], "altitude": 90, "speed": 26},               # the climb to the reveal
    ],
    "minClearance": 4, "clearanceRadius": 10, "amplitudeHeadroom": 2,
    "climbRate": 12, "fallRate": 25, "lookAhead": 3.0, "pitchBias": 0.22,
    "pitchMin": -32, "pitchMax": 0, "bankLimit": 8, "bankGain": 1.4, "loop": False,
}
FLY = False   # set by --flight


def scene_doc(state, interim, camera_name):
    camera_pose = camera(camera_name)
    s = look.STATES[state]
    env = {
        "intensity": s["amb_int"],
        "background": look.hexlin(s["sky_hor"]),
        "proceduralSkyBackground": True,
        "skybox": True,
        "shadowRange": 480.0,   # the low suns' long shadows reach the reveal's middle ground (+0.7 ms; 220 changed no pixel there)
        "depthPrepass": True,
        "horizonFill": 1.0,   # ADR-1962: below the horizon the sky is the fog at infinity -- no band past the tiles
        "aoRadius": 1.0,   # 4 m tiles (engineering: the gaps are 0.34 m; 0.6 at 1 m); Phase 5: the automatic radius (5% of the look-ahead distance) welled every tile gap
        # GTAO, faded out with distance (ADR-1951): it darkened every far step riser into dark dashes (97fe0568).
        # The band is per view (AO_BANDS): near ground only.
        "ambientOcclusion": True,
        # the remaining dark dashes were screen-space contact shadows marching across far risers (engineering, CP-A r7,
        # canaried): off. The sun's shadow map still shades the hills. Saves about 7.4 ms.
        "contactShadows": False,
        "aoFadeStart": AO_BANDS.get(camera_name, (40.0, 120.0))[0],
        "aoFadeEnd": AO_BANDS.get(camera_name, (40.0, 120.0))[1],
        "sky": look.sky(state),
        "dayNight": look.day_night(phase=s["phase"]),
        "tileDynamics": look.tile_dynamics(),
    }
    env.update(look.fog_medium(state))
    if interim:
        nodes, programs = world.interim_nodes(), [look.tile_program(state, 1.0, "mgTile")]
    else:
        prog = lambda cell: "mgTile%dx_%s" % (int(cell), state)   # noqa: E731
        under = "mgUnder_%s" % state
        nodes = world.ring_nodes(prog, look.hexlin(s["under"]), under, s.get("side_px", 0.0), TILE_SCALE,
                                 look.hexlin(s["land"][1]))
        programs = [look.tile_program(state, c, scale=TILE_SCALE) for c in (1.0, 2.0, 4.0)]
        programs.append(look.tile_program(state, 4.0, under, far=True, shade=look.UNDER_SHADE, border=1.0,
                                          scale=TILE_SCALE, contour=False))
    doc = {
        "format": "avgen-scene", "version": 1, "name": "musical-geography",
        "camera": dict({"mode": 1, "orbitSpeed": 0.0}, **camera_pose),
        "environment": env,
        "nodes": nodes,
        "lights": [look.sun_light(state), look.moon_light(state)] + ([look.fill_light(state)] if look.STATES[state].get("fill_int", 0) > 0 else []),
        "materialPrograms": programs,
    }
    doc["camera"]["far"] = 200000.0   # the 64 m far ring reaches 4.5 km
    if FLY:
        doc["flight"] = FLIGHT
    return doc


AUDIO = os.path.join(HERE, "..", "..", "assets", "audio", "feline-footwear.wav")


def project_doc(scene_rel, state):
    return {
        "format": "avgen-project", "version": 4,
        "assets": {"scene": {"kind": "composition", "path": scene_rel},
                   "audio": AUDIO},
        "parameters": dict(look.post(state), **{"post/output/dither": 1.0, "env/tiles/rings/ahead": 150.0}),
        "routes": look.audio_routes(),
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video",
                   "path": "renders/musical-geography.mp4"},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--state", default="day", choices=sorted(look.STATES))
    ap.add_argument("--interim", action="store_true")
    ap.add_argument("--camera", default="low", choices=sorted(CAMERAS))
    ap.add_argument("--out", default=os.path.join(HERE, "scene"))
    ap.add_argument("--tile-scale", type=float, default=4.0)
    ap.add_argument("--flight", action="store_true")
    a = ap.parse_args()
    global TILE_SCALE
    TILE_SCALE = a.tile_scale
    global FLY
    FLY = a.flight
    os.makedirs(a.out, exist_ok=True)
    scene_path = os.path.join(a.out, "musical-geography.scene.json")
    with open(scene_path, "w") as f:
        json.dump(scene_doc(a.state, a.interim, a.camera), f, indent=1)
    proj_dir = a.out   # look-development projects live beside the scene (gitignored) until the scene is approved
    proj_path = os.path.join(proj_dir, "musical-geography.json")
    with open(proj_path, "w") as f:
        json.dump(project_doc(os.path.relpath(scene_path, proj_dir), a.state), f, indent=1)
    print("wrote", scene_path, "and", proj_path)


if __name__ == "__main__":
    main()

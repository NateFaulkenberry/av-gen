#!/usr/bin/env python3
"""Generates examples/liminal/ -- the engineering example of the Liminal POC (ADR-1040 to 1043).

About 32 s that exercise every piece the art direction builds on:
  * the vocabulary: a corridor, a doorway, a large empty room with a window onto the void, a block stair
    up to a door high in the far wall, and a bridge (a platform) over the void to the next corridor;
  * the screw: the whole cell repeats forever, each one 3 m higher (an endless climb), so through the
    window the neighbouring cells stand in the fog above and below;
  * continuous change: the room grows (a morph keyed smoothly on the timeline), the walls breathe on the
    bass (a spring route into the warp), and the warp's flow runs at the music's energy (an integrating
    route into its phase) and stops in silence;
  * the journey: an authored walk with a hesitation at the door, a pause to look out of the window, the
    climb, the bridge, and the invisible wrap into the next cell; a small figure walks ahead and leaves;
  * the palette: four states keyed on the timeline, bound to the walls, the lamps, the lights, the fog
    and the edge light;
  * placed assets: a rock in every room (static, per cell), and the walking figure on the journey.

The song is referenced as ~/Desktop/All You Got.wav and never copied. Run from the repository root:
    python3 tools/make_liminal_example.py
"""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from liminal_sdf import (box, breathing, corridor, doorway, landing, opening, room, screw, screw_apply,  # noqa: E402
                         sdf_node, slab, stairway, translate, union)

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "examples", "liminal")

# ---- the cell (content centred on the origin along the screw, see liminal_sdf.screw) ----------------
T = [30.0, 3.0, 0.0]  # every cell is the last one 30 m on and 3 m up: an endless climb
WALL = 0.3
ROOM_X = (-3.0, 11.0)
STAIR_FOOT = 3.0
STEPS, RUN, RISE = 16, 0.5, 3.0 / 16  # 3 m in 8 m


ROOM = (ROOM_X, (0.0, 9.0), (-7.0, 7.0))  # interior; it grows to 14 m high and 20 m wide on the timeline
room_doors = [
    doorway("-x", ROOM, along=0.0, width=2.4, height=2.8),             # from the corridor
    doorway("+x", ROOM, along=0.0, width=2.0, height=2.6, sill=3.0),   # high in the far wall
    # a window onto the void, deep enough to stay open as the -z wall moves from z = -7 to z = -10
    opening(((2.0, 8.0), (1.5, 6.0), (-10.7, -6.8))),
]

cell = union(
    corridor(-15.0, ROOM_X[0] - WALL, width=3.0, height=3.2, wall=WALL, name="hall"),
    room(ROOM, wall=WALL, openings=room_doors, name="room"),
    stairway([STAIR_FOOT, 0.0, 0.0], STEPS, run=RUN, rise=RISE, width=2.0, name="flight"),
    landing([13.15, 3.0, 0.0], 3.7, 3.0, thickness=0.3, name="bridge"),
)
world_tree = screw(breathing(cell, amount=0.08, frequency=0.07, axes=(1.0, 0.0, 1.0), name="breath"), T,
                   name="climb")

# Lamps: a separate emissive object -- ceiling panels in the corridor, one long panel over the room.
lamp_cell = union(
    slab(((-13.0, -5.0), (3.14, 3.2), (-0.25, 0.25)), name="hallLamp"),
    translate([4.0, 8.95, 0.0], box([4.0, 0.05, 0.6], name="roomLamp"), name="roomLampAt"),  # follows the ceiling
)
lamps_tree = screw(lamp_cell, T, name="lampClimb")

# The path: floor points in cell 0 (the camera adds its eye height). The next point after the last is
# the first carried by the screw: (15, 3, 0).
PATH = [
    [-15.0, 0.0, 0.0],
    [-9.0, 0.0, 0.25],
    [-5.0, 0.0, 0.0],
    [-1.5, 0.0, 0.0],   # through the door
    [1.0, 0.0, -1.6],   # drift towards the window
    [2.6, 0.0, -0.2],   # back to the foot of the stair
    [7.0, 1.5, 0.0],    # half-way up
    [11.6, 3.0, 0.0],   # the upper door
    [13.4, 3.0, 0.0],   # on the bridge
]

# ---- scene -----------------------------------------------------------------------------------------
lights = []
for k in range(-1, 4):
    lights.append({"name": f"roomLamp{k + 1}", "type": "point", "position": screw_apply([4.0, 8.2, 0.0], T, 0, k),
                   "color": [0.8, 0.85, 1.0], "intensity": 60.0, "range": 22.0, "castsShadow": False})
    lights.append({"name": f"hallLamp{k + 1}", "type": "point", "position": screw_apply([-9.0, 2.8, 0.0], T, 0, k),
                   "color": [0.8, 0.85, 1.0], "intensity": 14.0, "range": 9.0, "castsShadow": False})

nodes = [
    sdf_node("world", world_tree,
             {"baseColor": [0.55, 0.56, 0.6], "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
              "roughness": 0.85, "metallic": 0.0},
             step_scale=0.8, max_distance=200.0,
             look={"aoStrength": 0.7, "aoDistance": 1.6, "edgeIntensity": 0.4, "edgeWidth": 0.04,
                   "edgeColor": [0.6, 0.7, 1.0]}),
    sdf_node("lamps", lamps_tree,
             {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [1.0, 0.95, 0.85], "emissiveIntensity": 6.0,
              "roughness": 1.0, "metallic": 0.0},
             step_scale=0.9, max_distance=200.0, look={}),
]
# A rock in every room of the covered cells: static props that repeat with the world and survive the wrap.
for k in range(-1, 4):
    nodes.append({"name": f"stone{k + 1}", "kind": "gltf", "asset": "../../assets/kenney/rock_tallB.glb",
                  "position": screw_apply([6.5, 0.0, -4.5], T, 0, k), "rotation": [0.0, 25.0, 0.0],
                  "scale": [2.2, 2.2, 2.2]})
# The figure: a small human walking the journey ahead of the camera (Quaternius UAL1, CC0, local-only).
nodes.append({"name": "figure", "kind": "gltf", "asset": "../../assets/quaternius/animations/UAL1_Standard.glb",
              "position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0],
              "journey": {"distance": 6.0},
              "animation": {"state": "Walk_Loop", "blend": 0.3, "updateHz": 0.0, "cullDistance": 0.0}})

scene = {
    "format": "avgen-scene", "version": 1, "name": "Liminal (engineering example)",
    "camera": {"mode": 3, "fov": 62.0,
               "journey": {"path": PATH, "screw": {"translation": T, "count": 0}, "collide": "world", "radius": 0.35}},
    "environment": {"intensity": 0.0, "background": [0.05, 0.055, 0.07], "fogColor": [0.05, 0.055, 0.07],
                    "lightRig": "liminal.rig.json", "volumeDensity": 0.018, "volumeAbsorption": 1.0,
                    "volumeMaxDistance": 160.0, "volumeScattering": 0.6},
    "nodes": nodes,
    "lights": lights,
}

rig = {
    "format": "avgen-lightrig", "version": 1, "name": "LiminalEng",
    "description": "Engineering example: a soft cold key from high in the void and a low ambient; the rooms are lit "
                   "by their own lamps. Directional light is safe because the journey wraps by a pure translation.",
    "keyIntensity": 1.0, "ambientIntensity": 0.12, "ambientColor": [0.55, 0.6, 0.75], "ambientTemperature": 8000,
    "lights": [
        {"name": "key", "type": "directional", "role": "key", "azimuth": 200.0, "elevation": 55.0, "distance": 2.0,
         "intensity": 0.35, "color": [0.8, 0.85, 1.0], "temperature": 7500, "castsShadow": False, "volumetric": 0.0,
         "followCamera": False},
    ],
}

# ---- project: timeline (the director), routes (the music), palette ----------------------------------


def track(target: str, keys, component: int = -1, interp: str = "smooth") -> dict:
    return {"target": target, "component": component, "timeBase": "seconds", "mode": "replace", "loopLength": 0.0,
            "enabled": True,
            "keys": [{"time": float(t), "value": v if isinstance(v, list) else [float(v)], "interp": interp}
                     for t, v in keys]}


timeline = {"enabled": True, "cues": [], "tracks": [
    # The walk: metres along the path. Equal neighbouring values are a pause.
    track("camera/journey/distance", [(0, 0.0), (6, 6.5), (9, 9.0), (11, 9.6), (12.5, 10.2), (15, 13.2), (16.5, 14.2),
                                      (19.5, 14.6), (22, 17.5), (28, 26.5), (32, 34.5)]),
    # Hesitate at the door (a small look aside), look out of the window during the pause, look up the stair.
    # (+yaw turns left: looking along +X, left is -Z, where the window is.)
    track("camera/journey/yaw", [(0, 0.0), (9.5, 0.0), (11, -14.0), (12.5, 0.0), (15.5, 0.0), (17, 48.0), (19, 52.0),
                                 (21, 0.0), (32, 0.0)]),
    track("camera/journey/pitch", [(0, 0.0), (21, 0.0), (23, 9.0), (27, 4.0), (29, 0.0), (32, 0.0)]),
    track("camera/journey/sway", [(0, 2.5), (32, 2.5)]),
    # The figure walks ahead, reaches the room first, climbs, and is gone into the next corridor.
    track("nodes/figure/journey/distance", [(0, 6.0), (10, 18.0), (16, 26.5), (22, 34.0), (32, 46.0)], interp="linear"),
    # The room grows while we are in it: its box and its centre, keyed together so the floor stays put
    # (half extents = interior / 2 + wall / 2; centre y = floor + height / 2). Continuous, never a switch.
    track("sdf/world/node/room/size", [(0, [7.15, 4.65, 7.15]), (12, [7.15, 4.65, 7.15]), (24, [7.15, 7.15, 10.15]),
                                       (32, [7.15, 7.15, 10.15])]),
    track("sdf/world/node/roomAt/translation", [(0, [4.0, 4.5, 0.0]), (12, [4.0, 4.5, 0.0]), (24, [4.0, 7.0, 0.0]),
                                                (32, [4.0, 7.0, 0.0])]),
    track("sdf/lamps/node/roomLampAt/translation", [(0, [4.0, 8.95, 0.0]), (12, [4.0, 8.95, 0.0]),
                                                    (24, [4.0, 13.95, 0.0]), (32, [4.0, 13.95, 0.0])]),
    # The palette: muted -> awake -> recede -> open.
    track("palette/position", [(0, 0.0), (8, 0.0), (14, 1.0), (19, 2.0), (24, 2.0), (30, 3.0), (32, 3.0)]),
]}


def route(source, target, amount=1.0, component=-1, **chain):
    return {"source": source, "target": target, "amount": amount, "op": "add", "polarity": "unipolar",
            "component": component, "enabled": True, "chain": chain}


routes = [
    # The world breathes under the low end: a spring, so it swells and settles instead of twitching.
    route("audio.bass", "sdf/world/node/breath/amount", 0.22, attackMs=120, decayMs=900, springHz=0.35,
          springDamping=0.55),
    # The breathing flows at the music's energy (a phase integrated from a rate), and stands still in silence.
    route("audio.energy", "sdf/world/node/breath/translation", 0.18, 0, attackMs=300, decayMs=1500, integrate=True),
    route("audio.energy", "sdf/world/node/breath/translation", 0.11, 2, attackMs=300, decayMs=1500, integrate=True),
    # The music nudges the walk (at most 0.12 m/s more). Never put a depth on an integrating route.
    route("audio.rms", "camera/journey/distance", 0.12, attackMs=400, decayMs=2000, integrate=True),
    # High detail as fine edge light, springy.
    route("audio.highMid", "sdf/world/look/edge/intensity", 1.2, attackMs=40, decayMs=400, springHz=1.5,
          springDamping=0.8),
]

palette = {
    "states": [
        {"name": "muted", "colors": {"wall": [0.42, 0.45, 0.5], "lamp": [0.75, 0.85, 1.0], "light": [0.7, 0.8, 1.0],
                                     "fog": [0.045, 0.05, 0.065], "edge": [0.45, 0.55, 0.8]}},
        {"name": "awake", "colors": {"wall": [0.55, 0.42, 0.38], "lamp": [1.0, 0.62, 0.35], "light": [1.0, 0.7, 0.45],
                                     "fog": [0.07, 0.035, 0.05], "edge": [1.0, 0.45, 0.35]}},
        {"name": "recede", "colors": {"wall": [0.3, 0.31, 0.33], "lamp": [0.45, 0.5, 0.55], "light": [0.5, 0.55, 0.6],
                                      "fog": [0.02, 0.022, 0.028], "edge": [0.3, 0.33, 0.4]}},
        {"name": "open", "colors": {"wall": [0.72, 0.68, 0.6], "lamp": [1.0, 0.92, 0.75], "light": [1.0, 0.9, 0.7],
                                    "fog": [0.16, 0.14, 0.12], "edge": [1.0, 0.85, 0.6]}},
    ],
    "bindings": [
        {"role": "wall", "target": "sdf/world/material/baseColor"},
        {"role": "lamp", "target": "sdf/lamps/material/emissiveColor"},
        {"role": "fog", "target": "scene/fogColor"},
        {"role": "edge", "target": "sdf/world/look/edge/color"},
    ] + [{"role": "light", "target": f"lights/{l['name']}/color"} for l in lights],
    "position": 0.0, "saturation": 1.0, "value": 1.0,
}

project = {
    "format": "avgen-project", "version": 4, "app": {"name": "avgen", "version": "0.1.0"},
    "assets": {"audio": {"path": "~/Desktop/All You Got.wav"},
               "scene": {"kind": "composition", "path": "liminal.scene.json"}},
    "parameters": {
        "camera/exposure/mode": 0, "camera/exposure/compensation": 0.0, "post/tonemap/operator": 1,
        "post/bloom/enabled": True, "post/bloom/intensity": 0.18, "post/bloom/threshold": 1.2,
        "post/grade/contrast": 1.05, "post/grade/saturation": 1.0, "post/output/vignette": 0.35,
        "post/output/grain": 0.02, "scene/volumeJitter": 0.5,
    },
    "sources": [],
    "routes": routes,
    "timeline": timeline,
    "palette": palette,
    "render": {"width": 1920, "height": 1080, "fps": 60, "output": "video", "startSeconds": 0.0, "endSeconds": 32.0},
}

os.makedirs(OUT, exist_ok=True)
for name, doc in (("liminal.scene.json", scene), ("liminal.rig.json", rig), ("liminal.json", project)):
    with open(os.path.join(OUT, name), "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
print("wrote", OUT)

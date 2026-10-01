#!/usr/bin/env python3
"""Generates examples/liminal/ -- the engineering example of the Liminal POC (ADR-1040 to 1044).

About 48 s that exercise every piece the art direction builds on. It is a test bed, not a look: the art
direction replaces the palette, the light and the air.

  * the vocabulary: a corridor, a doorway, a large empty room with a window onto the void, a block stair
    up to a door high in the far wall, and a bridge (a platform) over the void to the next corridor;
  * surfaces (ADR-1044): plaster walls, a floor, the stair as an accent, and the lamps as an emissive
    surface of the same world object (one march, not two);
  * the screw: the whole cell repeats forever, each one 3 m higher (an endless climb);
  * continuous change: the room grows (its box keyed smoothly on the timeline), the walls breathe on the
    bass (a spring route into the warp), and the warp's flow runs at the music's energy (an integrating
    route into its phase) and stops in silence;
  * the journey: an authored walk with a hesitation at the door, a pause to look out of the window, the
    climb, the bridge, and the invisible wrap into the next cell; a small figure walks ahead and leaves;
  * chapters: at about 38 s the journey passes through a white-out (the palette's value and the exposure
    keyed up and down) into a second world, a Penrose stairwell (a helix screw, four flights per turn,
    climbing forever), placed 400 m away; the first world is hidden once the camera leaves it;
  * the palette: four states keyed on the timeline, bound to the surfaces, the lights, the fog and the
    edge light;
  * placed assets: the walking figure on the journey.

The song is referenced as ~/Desktop/All You Got.wav and never copied. Run from the repository root:
    python3 tools/make_liminal_example.py
"""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from liminal_sdf import (box, breathing, corridor, doorway, landing, opening, room, screw, screw_apply,  # noqa: E402
                         sdf_node, slab, stairway, surface, translate, tremble, union)

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "examples", "liminal")

PLASTER, FLOOR, ACCENT, LAMP = 0, 1, 2, 3  # the world's surfaces

# ---- chapter A: the climbing cell (content centred on the origin along the screw) --------------------
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

# The seam (the plane where one cell hands over to the next, perpendicular to T) runs through the middle
# of a corridor, never past the end of a wall: across it the floor, walls and ceiling simply continue,
# so nothing is ever clipped by it. So the cell ends with the first 2.5 m of the next cell's corridor
# (at the next cell's height), overlapping the seam, and starts with the rest of it, also overlapping.
cell_a = union(
    corridor(-15.5, ROOM_X[0] - WALL, width=3.0, height=3.2, wall=WALL, name="hall", floor_surface=FLOOR),
    corridor(13.0, 15.5, width=3.0, height=3.2, wall=WALL, floor_y=T[1], name="hallNext", floor_surface=FLOOR),
    room(ROOM, wall=WALL, openings=room_doors, name="room", floor_surface=FLOOR),
    surface(stairway([STAIR_FOOT, 0.0, 0.0], STEPS, run=RUN, rise=RISE, width=2.0, name="flight"), ACCENT),
    surface(landing([12.15, 3.0, 0.0], 1.7, 3.0, thickness=0.3, name="bridge"), FLOOR),
    # The lamps, an emissive surface of the same object: a panel in the corridor ceiling, a long one under
    # the room's ceiling (keyed with it as the room grows).
    surface(slab(((-13.0, -5.0), (3.14, 3.21), (-0.25, 0.25)), name="hallLamp"), LAMP),
    surface(translate([4.0, 8.95, 0.0], box([4.0, 0.05, 0.6], name="roomLamp"), name="roomLampAt"), LAMP),
)
tree_a = screw(breathing(cell_a, amount=0.08, frequency=0.07, axes=(1.0, 0.0, 1.0), cell=T, name="breath"), T,
               name="climb")
# A near-field tremble round the whole world (above the screw): only walls within 4 m of the camera shiver.
tree_a = tremble(tree_a, amount=0.0, frequency=3.0, radius=4.0, fade=2.0)

PATH_A = [
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

# ---- chapter B: the Penrose stairwell (a helix screw: 4 flights a turn round a square well) -----------
R = 4.0           # the flights run along x = R; the corners are 2 m squares centred on (+-R, +-R)
RISE_B = 2.4      # per flight (12 risers of 0.2 over 6 m)
TURN = 4 * RISE_B
cell_b = union(
    # the flight up the +X side, floating (you see the undersides of the flights above)
    surface(stairway([R, 0.0, -3.0], 12, run=0.5, rise=0.2, width=2.0, direction="+z", thickness=0.3,
                     name="flightB"), ACCENT),
    # the corner landings (each shared with the neighbouring flight, which the helix turns onto it)
    surface(landing([R, 0.0, -R], 2.0, 2.0, thickness=0.3), FLOOR),
    surface(landing([R, RISE_B, R], 2.0, 2.0, thickness=0.3), FLOOR),
    # the outer wall, one full turn tall (a helix cell holds content within half a turn of its height)
    slab(((R + 1.0, R + 1.3), (RISE_B / 2 - TURN / 2, RISE_B / 2 + TURN / 2), (-R - 1.3, R + 1.3)), name="wallB"),
    # a glowing doorway in the wall at every landing: the recurring beacon
    surface(slab(((R + 0.98, R + 1.02), (RISE_B, RISE_B + 2.4), (R - 0.6, R + 0.6))), LAMP),
)
# A helix's seams run through the corners, where the walls meet across them, so its seam margin is kept
# small (just above epsilon x maxDistance = 0.0012 x 120 = 0.144): the margin is how far a ray may step
# into a neighbour's wall end.
tree_b = screw(cell_b, [0.0, RISE_B, 0.0], count=4, seam=0.16, name="penrose")
# The path in cell 0 of the helix: the bottom corner, up the flight, to the top corner (= the next cell's
# bottom corner, which the screw turns 90 degrees and raises 2.4 m).
PATH_B = [
    [R, 0.0, -R],
    [R, 0.1, -2.8],
    [R, 1.2, 0.0],
    [R, 2.3, 2.8],
]
B_OFFSET = [0.0, 0.0, 400.0]  # chapter B's world lives 400 m away; the camera is carried there at the swap
SWAP = 40.0                   # the global journey distance of the swap

SURFACES = [{"color": [0.62, 0.63, 0.66]}, {"color": [0.45, 0.44, 0.43]}, {"color": [0.7, 0.66, 0.6]},
            {"color": [0.0, 0.0, 0.0], "emission": [6.0, 5.8, 5.4]}]

# ---- scene -----------------------------------------------------------------------------------------
lights = []
for k in range(-1, 4):
    lights.append({"name": f"roomLamp{k + 1}", "type": "point", "position": screw_apply([4.0, 8.2, 0.0], T, 0, k),
                   "color": [0.8, 0.85, 1.0], "intensity": 60.0, "range": 22.0, "castsShadow": False})
    lights.append({"name": f"hallLamp{k + 1}", "type": "point", "position": screw_apply([-9.0, 2.8, 0.0], T, 0, k),
                   "color": [0.8, 0.85, 1.0], "intensity": 14.0, "range": 9.0, "castsShadow": False})
lights_a = [l["name"] for l in lights]

nodes = [
    sdf_node("world", tree_a, surfaces=SURFACES, step_scale=0.8, max_distance=200.0,
             look={"aoStrength": 0.7, "aoDistance": 1.6, "edgeIntensity": 0.15, "edgeWidth": 0.04,
                   "edgeColor": [0.6, 0.7, 1.0]}),
    sdf_node("stairwell", tree_b, surfaces=SURFACES, step_scale=0.8, max_distance=120.0, position=B_OFFSET,
             look={"aoStrength": 0.7, "aoDistance": 1.6}),
    # The figure: a small human walking the journey ahead of the camera (Quaternius UAL1, CC0, local-only).
    {"name": "figure", "kind": "gltf", "asset": "../../assets/quaternius/animations/UAL1_Standard.glb",
     "position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0],
     "journey": {"distance": 6.0},
     "animation": {"state": "Walk_Loop", "blend": 0.3, "updateHz": 0.0, "cullDistance": 0.0}},
]

journey = {"chapters": [
    {"name": "climb", "start": 0.0, "from": 0.0, "path": PATH_A, "screw": {"translation": T, "count": 0},
     "collide": "world", "radius": 0.35, "nodes": ["world", "figure"], "lights": lights_a},
    {"name": "penrose", "start": SWAP, "from": 0.0, "path": PATH_B,
     "screw": {"translation": [0.0, RISE_B, 0.0], "count": 4}, "collide": "stairwell", "radius": 0.35,
     "offset": B_OFFSET, "yaw": 0.0, "nodes": ["stairwell"]},
]}

scene = {
    "format": "avgen-scene", "version": 1, "name": "Liminal (engineering example)",
    "camera": {"mode": 3, "fov": 62.0, "journey": journey},
    "environment": {"intensity": 0.0, "background": [0.05, 0.055, 0.07], "fogColor": [0.05, 0.055, 0.07],
                    "lightRig": "liminal.rig.json", "volumeDensity": 0.018, "volumeAbsorption": 1.0,
                    "volumeMaxDistance": 160.0, "volumeScattering": 0.6},
    "nodes": nodes,
    "lights": lights,
}

rig = {
    "format": "avgen-lightrig", "version": 1, "name": "LiminalEng",
    "description": "Engineering example: a soft cold key from high in the void and a low ambient; the rooms are lit "
                   "by their own lamps. Directional light is safe because every chapter wraps by a pure translation.",
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


END = 48.0
timeline = {"enabled": True, "cues": [], "tracks": [
    # The walk: metres along the journey. Equal neighbouring values are a pause. The swap to chapter B is
    # at 40 m (about 38 s), inside the white-out.
    track("camera/journey/distance", [(0, 0.0), (6, 6.5), (9, 9.0), (11, 9.6), (12.5, 10.2), (15, 13.2), (16.5, 14.2),
                                      (19.5, 14.6), (22, 17.5), (28, 26.5), (32, 34.5), (36, 38.6), (38, 40.4),
                                      (41, 41.4), (48, 49.0)]),
    # (+yaw turns left: looking along +X, left is -Z, where the window is.)
    track("camera/journey/yaw", [(0, 0.0), (9.5, 0.0), (11, -14.0), (12.5, 0.0), (15.5, 0.0), (17, 48.0), (19, 52.0),
                                 (21, 0.0), (40, 0.0), (43, -35.0), (46, -25.0), (48, -10.0)]),
    track("camera/journey/pitch", [(0, 0.0), (21, 0.0), (23, 9.0), (27, 4.0), (29, 0.0), (40, 0.0), (43, 22.0),
                                   (48, 8.0)]),
    track("camera/journey/sway", [(0, 2.5), (48, 2.5)]),
    # The figure walks ahead, reaches the room first, climbs, and is gone into the next corridor.
    # Past 40 m it rides chapter B's path, 400 m away, so it is gone into the light before the camera gets there.
    track("nodes/figure/journey/distance", [(0, 6.0), (10, 18.0), (16, 26.5), (22, 34.0), (32, 39.5), (36, 44.0)],
          interp="linear"),
    # The room grows while we are in it: its box and its centre, keyed together so the floor stays put
    # (half extents = interior / 2 + wall / 2; centre y = floor + height / 2). Continuous, never a switch.
    track("sdf/world/node/room/size", [(0, [7.15, 4.65, 7.15]), (12, [7.15, 4.65, 7.15]), (24, [7.15, 7.15, 10.15]),
                                       (END, [7.15, 7.15, 10.15])]),
    track("sdf/world/node/roomAt/translation", [(0, [4.0, 4.5, 0.0]), (12, [4.0, 4.5, 0.0]), (24, [4.0, 7.0, 0.0]),
                                                (END, [4.0, 7.0, 0.0])]),
    track("sdf/world/node/roomLampAt/translation", [(0, [4.0, 8.95, 0.0]), (12, [4.0, 8.95, 0.0]),
                                                    (24, [4.0, 13.95, 0.0]), (END, [4.0, 13.95, 0.0])]),
    # The palette: muted -> awake -> recede -> open, and back to muted in the stairwell.
    track("palette/position", [(0, 0.0), (8, 0.0), (14, 1.0), (19, 2.0), (24, 2.0), (30, 3.0), (40, 3.0), (46, 0.0),
                               (END, 0.0)]),
    # The white-out that hides the chapter swap: lightness and exposure up, the swap, and down again.
    track("palette/value", [(0, 1.0), (36, 1.0), (37.8, 2.6), (38.4, 2.6), (40.5, 1.0), (END, 1.0)]),
    track("camera/exposure/compensation", [(0, 0.0), (36, 0.0), (37.8, 3.5), (38.4, 3.5), (40.5, 0.0), (END, 0.0)]),
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
    # (No audio pace on camera/journey/distance here: a chapter swap happens at a journey DISTANCE, so a
    # route that adds pace moves the swap in time, out of the white-out keyed to hide it. To let the music
    # nudge the walk, integrate it into the distance -- route("audio.rms", "camera/journey/distance", 0.12,
    # integrate=True, ...) -- only in a chapter with no swap ahead, and never with a depth.)
    # The bright, noisy top end as a nearby shiver: the tremble's amount on a spring, its phase integrated.
    route("audio.treble", "sdf/world/node/tremble/amount", 0.05, attackMs=30, decayMs=500, springHz=2.0,
          springDamping=0.6),
    route("audio.treble", "sdf/world/node/tremble/translation", 3.0, 1, attackMs=30, decayMs=500, integrate=True),
    # High detail as fine edge light, springy.
    route("audio.highMid", "sdf/world/look/edge/intensity", 0.6, attackMs=40, decayMs=400, springHz=1.5,
          springDamping=0.8),
]

palette = {
    "states": [
        {"name": "muted", "colors": {"plaster": [0.42, 0.45, 0.5], "floor": [0.3, 0.31, 0.33],
                                     "accent": [0.55, 0.56, 0.6], "lamp": [5.0, 5.6, 6.0], "light": [0.7, 0.8, 1.0],
                                     "fog": [0.045, 0.05, 0.065], "edge": [0.45, 0.55, 0.8]}},
        {"name": "awake", "colors": {"plaster": [0.55, 0.42, 0.38], "floor": [0.35, 0.25, 0.22],
                                     "accent": [0.75, 0.35, 0.3], "lamp": [6.0, 3.7, 2.1], "light": [1.0, 0.7, 0.45],
                                     "fog": [0.07, 0.035, 0.05], "edge": [1.0, 0.45, 0.35]}},
        {"name": "recede", "colors": {"plaster": [0.3, 0.31, 0.33], "floor": [0.22, 0.22, 0.23],
                                      "accent": [0.34, 0.35, 0.37], "lamp": [2.2, 2.5, 2.7], "light": [0.5, 0.55, 0.6],
                                      "fog": [0.02, 0.022, 0.028], "edge": [0.3, 0.33, 0.4]}},
        {"name": "open", "colors": {"plaster": [0.72, 0.68, 0.6], "floor": [0.5, 0.46, 0.4],
                                    "accent": [0.85, 0.72, 0.5], "lamp": [6.0, 5.5, 4.5], "light": [1.0, 0.9, 0.7],
                                    "fog": [0.16, 0.14, 0.12], "edge": [1.0, 0.85, 0.6]}},
    ],
    "bindings": [
        {"role": "plaster", "target": "sdf/world/surface/0/color"},
        {"role": "floor", "target": "sdf/world/surface/1/color"},
        {"role": "accent", "target": "sdf/world/surface/2/color"},
        {"role": "lamp", "target": "sdf/world/surface/3/emission"},
        {"role": "plaster", "target": "sdf/stairwell/surface/0/color"},
        {"role": "floor", "target": "sdf/stairwell/surface/1/color"},
        {"role": "accent", "target": "sdf/stairwell/surface/2/color"},
        {"role": "lamp", "target": "sdf/stairwell/surface/3/emission"},
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
    "render": {"width": 1920, "height": 1080, "fps": 60, "output": "video", "startSeconds": 0.0, "endSeconds": END},
}

os.makedirs(OUT, exist_ok=True)
for name, doc in (("liminal.scene.json", scene), ("liminal.rig.json", rig), ("liminal.json", project)):
    with open(os.path.join(OUT, name), "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
print("wrote", OUT)

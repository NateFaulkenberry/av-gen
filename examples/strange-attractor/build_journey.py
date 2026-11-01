"""Writes the journey into the canyon scene (strange-attractor-flythrough.scene.json), Art Pass 2 (ADR-1885, 1886):

    python3 examples/strange-attractor/build_journey.py

- the Moon route after portal 1 (the canyon's `portal.flight`), timed to the 210 s cut;
- the canyon's looks: the day as authored, dusk from crossing 1 (when the camera leaves for the Moon, so the canyon it
  returns to, and sees through the Moon's floor, is the dusk canyon);
- the dusk lights (with the day's, off until the look turns them on), the campfire's light;
- the first encounter: three Sasquatches round a fire on the terrace beside the dusk exit, shown only in the dusk look.

The dusk look's values came from the dusk world (Gate C 2, ADR-1884), which this replaces: the canyon is one world now.
M1b: it first scales the canyon (ADR-1893, canyon_scale.py), brings back the portal's spill across the river, writes the
short Moon route, the campfire (pass2/build_campfire.py) and the Earth's bass route in the project.
The Sasquatches are built by pass2/build_sasquatch.py from the owner's CC BY download (assets-pass2.md).
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from canyon_scale import WORLD_SCALE as WS, canyon as canyon_frame, rescale  # noqa: E402  (ADR-1893)

PATH = os.path.join(HERE, "strange-attractor-flythrough.scene.json")
PROJECT = os.path.join(HERE, "strange-attractor.json")
scene = json.load(open(PATH))
project = json.load(open(PROJECT))

# --- M1b: the world's scale (ADR-1893). Every Pass 1 length in the scene and the project, once, by ratio.
rescale(scene, project)
# M1b (owner): the portal's corruption spills across the river again, as at Gate A -- the bed's blocks and glyphs rise
# through the water. ADR-1873's floor kept them under it; it goes.
scene["corruption"].pop("floor", None)


def canyon(d, lat):
    """d real metres down-canyon of the dusk exit, lat real metres toward the camp bank (x, z), in the scaled world."""
    return canyon_frame(d / WS, lat / WS)


# --- the Moon route (x, height above the lunar ground, z, t), M1b: a short Moon. Out of portal 1 low over the sunlit
# plain, the Earth ahead; rising over the near rim of the great basin, the domes glimpsed far across its night; the
# dive into the portal crater on the plain (Gate C 2's near-vertical drop along +x). Crossing 1 at 32.025 s (bar 17),
# crossing 2 at 47.87 s (bar 25).
scene["portal"]["flight"] = [
    [0, 5, 1445, 34.2],        # low over the plain, the Earth ahead (the domes are below the far rim's line)
    [-14, 8, 1395, 37.4],
    [-30, 80, 1345, 40.6],     # rising: the far domes come over the rim, out on the night side
    [-34, 158, 1300, 43.2],    # the top, slowing
    [-16, 150, 1270, 44.5],    # over the crater; the floor opens beneath
    [0, 0, 1270, 47.87],       # the drop through the floor (the portal turns it into the wall's level exit)
    [1, -45, 1270, 48.9],
]
scene["portal"].pop("aims", None)
# the Moon's exit: its y is a height over the Moon's ground (8 m: the arrival settles to 5 m by 34.2 s)
scene["portal"]["exit"] = [0.0, 8.0, 1500.0]

# --- the lights: the day's fills, the dusk's (off by day), the campfire
CAMP = canyon(16.0, 16.0)   # the low bank right of the dusk exit (build_moon.py's CAMP)
CAMP_Y = 4.4 * WS           # measured on the bank (generator 4.4 m)
lights = [l for l in scene["lights"] if l["name"] in ("canyon-bounce", "floor-bounce")]
lights += [
    {"name": "dusk-sky", "type": "directional", "role": "fill", "direction": [0.1, -1.0, 0.15], "color": [0.36, 0.38, 1.0],
     "intensity": 0.0, "castsShadow": False, "shadowStrength": 0.0},
    {"name": "dusk-bounce", "type": "directional", "role": "fill", "direction": [-0.9, -0.15, 0.4], "color": [0.55, 0.42, 0.95],
     "intensity": 0.0, "castsShadow": False, "shadowStrength": 0.0},
    {"name": "campfire", "type": "point", "position": [CAMP[0], CAMP_Y + 1.6, CAMP[1]], "color": [1.0, 0.52, 0.22],
     "intensity": 0.0, "range": 30.0, "castsShadow": True, "shadowStrength": 1.0, "flicker": 0.3, "flickerRate": 2.6},
]
scene["lights"] = lights

# --- the encounter: three Sasquatches round the fire, facing it, and the fire itself (particles)
nodes = [n for n in scene.get("nodes", []) if not n["name"].startswith(("sasquatch-", "campfire"))]
A = "../../assets/strange-attractor/build/"
# round the fire on its far side from the camera's arc (which passes about (13, 10) real metres), facing the fire and
# so, across it, the camera; the fire ring is 0.8 m and they stand back from it
toward_cam = [(canyon(13.0, 10.0)[i] - CAMP[i]) for i in (0, 1)]
n_ = math.hypot(*toward_cam)
away = [-toward_cam[0] / n_, -toward_cam[1] / n_]
# M2 (coordinator): none directly behind the fire from the camera -- they stand either side of it and one to the right
for k, (name, ang, dist) in enumerate((("sasquatch-a", -52.0, 2.3), ("sasquatch-b", 22.0, 2.6), ("sasquatch-c", 64.0, 2.3))):
    a = math.radians(ang)
    ux, uz = away[0] * math.cos(a) - away[1] * math.sin(a), away[0] * math.sin(a) + away[1] * math.cos(a)
    x, z = CAMP[0] + dist * ux, CAMP[1] + dist * uz
    # face the fire: the glb faces -y in Blender, +z in glTF; yaw so +z points at the camp
    yaw = math.degrees(math.atan2(CAMP[0] - x, CAMP[1] - z))
    nodes.append({"name": name, "kind": "gltf", "asset": A + f"{name}.glb", "position": [x, CAMP_Y, z],
                  "rotation": [0, yaw, 0], "scale": [1, 1, 1], "visible": False,
                  "animation": {"state": "idle", "cullDistance": 800.0, "speed": 1.0}})
fire = [CAMP[0], CAMP_Y + 0.08, CAMP[1]]
nodes += [
    # the logs, the stone ring and the glowing ember bed (pass2/build_campfire.py), at real size
    {"name": "campfire-logs", "kind": "gltf", "asset": A + "campfire.glb", "position": [CAMP[0], CAMP_Y, CAMP[1]],
     "rotation": [0, 20, 0], "scale": [1, 1, 1], "visible": False},
    # flames: tongues rising out of the teepee, licking up and thinning, in two layers (a hot core, a softer outer)
    {"name": "campfire-flames", "kind": "particles", "visible": False, "particles": {
        "enabled": True, "capacity": 2400, "seed": 31, "shape": "sphere", "position": [fire[0], fire[1] + 0.15, fire[2]],
        "extent": [0.3, 0.06, 0.3], "direction": [0, 1, 0], "spawnRate": 300.0, "lifetimeMin": 0.35, "lifetimeMax": 0.8,
        "spread": 0.1, "speedMin": 1.3, "speedMax": 2.6, "gravity": [0.0, 2.4, 0], "drag": 0.9, "turbulence": 1.3,
        "turbulenceScale": 0.5, "sizeStart": 0.2, "sizeEnd": 0.03, "sizeVariance": 0.5,
        "colorStart": [1.0, 0.36, 0.06, 0.55], "colorEnd": [0.7, 0.06, 0.0, 0.0], "emissive": 2.4, "blend": "additive",
        "velocityStretch": 1.1, "stretchMax": 2.0}},
    {"name": "campfire-core", "kind": "particles", "visible": False, "particles": {
        "enabled": True, "capacity": 900, "seed": 33, "shape": "sphere", "position": [fire[0], fire[1] + 0.1, fire[2]],
        "extent": [0.16, 0.04, 0.16], "direction": [0, 1, 0], "spawnRate": 160.0, "lifetimeMin": 0.2, "lifetimeMax": 0.45,
        "spread": 0.08, "speedMin": 0.9, "speedMax": 1.7, "gravity": [0.0, 2.0, 0], "drag": 0.8, "turbulence": 0.9,
        "turbulenceScale": 0.35, "sizeStart": 0.14, "sizeEnd": 0.03, "sizeVariance": 0.3,
        "colorStart": [1.0, 0.72, 0.28, 0.7], "colorEnd": [1.0, 0.3, 0.03, 0.0], "emissive": 3.0, "blend": "additive",
        "velocityStretch": 0.8, "stretchMax": 1.4}},
    {"name": "campfire-embers", "kind": "particles", "visible": False, "particles": {
        "enabled": True, "capacity": 600, "seed": 37, "shape": "sphere", "position": [fire[0], fire[1] + 0.3, fire[2]],
        "extent": [0.25, 0.1, 0.25], "direction": [0, 1, 0], "spawnRate": 45.0, "lifetimeMin": 1.5, "lifetimeMax": 3.5,
        "spread": 0.35, "speedMin": 1.0, "speedMax": 2.6, "gravity": [0.25, 0.5, 0.1], "drag": 0.3, "turbulence": 0.9,
        "turbulenceScale": 0.5, "sizeStart": 0.03, "sizeEnd": 0.008, "colorStart": [1.0, 0.7, 0.25, 1.0],
        "colorEnd": [1.0, 0.15, 0.0, 0.0], "emissive": 16.0, "blend": "additive"}},
    {"name": "campfire-smoke", "kind": "particles", "visible": False, "particles": {
        "enabled": True, "capacity": 700, "seed": 41, "shape": "sphere", "position": [fire[0], fire[1] + 1.1, fire[2]],
        "extent": [0.25, 0.15, 0.25], "direction": [0, 1, 0], "spawnRate": 24.0, "lifetimeMin": 5.0, "lifetimeMax": 8.0,
        "spread": 0.2, "speedMin": 0.5, "speedMax": 0.9, "gravity": [0.25, 0.3, 0.08], "drag": 0.6, "turbulence": 0.5,
        "turbulenceScale": 0.3, "sizeStart": 0.4, "sizeEnd": 2.6, "colorStart": [0.5, 0.46, 0.48, 0.035],
        "colorEnd": [0.3, 0.28, 0.32, 0.0], "emissive": 0.0, "blend": "alpha"}},
]
scene["nodes"] = nodes

# --- the looks: dusk from crossing 1 (ADR-1886); the values the dusk world carried (ADR-1884)
dusk = {
    "attractorWorld/sunAzimuth": -130.0, "attractorWorld/sunElevation": 8.0,
    "env/intensity": 0.13,
    "env/sky/zenithColor": [0.022, 0.020, 0.115], "env/sky/horizonColor": [0.36, 0.22, 0.46],
    "env/sky/groundColor": [0.012, 0.010, 0.045], "env/sky/haze": 0.18, "env/sky/sunColor": [1.0, 0.52, 0.26],
    "env/sky/sunIntensity": 7.0, "env/sky/sunSize": 0.05, "env/sky/sunGlow": 0.55,
    "scene/fogColor": [0.10, 0.075, 0.22],
    "lights/canyon-bounce/intensity": 0.0, "lights/floor-bounce/color": [0.55, 0.38, 0.62],
    "lights/floor-bounce/intensity": 0.25, "lights/dusk-sky/intensity": 0.6, "lights/dusk-bounce/intensity": 0.3,
    "lights/campfire/intensity": 30.0,
    # the contour lines off at dusk and night (M1b quieted them, M2 removes them: the encounter owns the frame; ADR-1894)
    "attractorWorld/contourGain": 0.0,
    "nodes/sasquatch-a/visible": True, "nodes/sasquatch-b/visible": True, "nodes/sasquatch-c/visible": True,
    "nodes/campfire-logs/visible": True, "nodes/campfire-flames/visible": True, "nodes/campfire-core/visible": True,
    "nodes/campfire-embers/visible": True, "nodes/campfire-smoke/visible": True,
}
# home from the Moon (crossing 2): still dusk, and the second portal's corruption tuned to the outer cliff -- a small
# field that gathers only once the camera has left the fire and climbs toward the cliff, a larger opening
dusk_portal = dict(dusk)
dusk_portal.update({
    "corruption/radius": 36.0, "corruption/fieldNear": 12.0, "corruption/fieldFar": 95.0, "corruption/openNear": 10.0,
    "corruption/openFar": 55.0, "corruption/width": 15.0, "corruption/height": 13.0, "corruption/depth": 6.0,
    "corruption/echoOffset": 0.35,
})
# night (crossing 3: the canyon behind the pyramid portal and home): dark, a cold moon, the fire down to embers
night = {
    "attractorWorld/sunAzimuth": 60.0, "attractorWorld/sunElevation": 38.0, "attractorWorld/sunIntensity": 0.025,
    "attractorWorld/contourGain": 0.0,
    "env/intensity": 0.02,
    "env/sky/zenithColor": [0.003, 0.005, 0.02], "env/sky/horizonColor": [0.02, 0.028, 0.07],
    "env/sky/groundColor": [0.003, 0.003, 0.01], "env/sky/haze": 0.1, "env/sky/sunColor": [0.62, 0.72, 1.0],
    "env/sky/sunIntensity": 0.6, "env/sky/sunSize": 0.03, "env/sky/sunGlow": 0.2,
    "scene/fogColor": [0.008, 0.01, 0.03],
    "lights/canyon-bounce/intensity": 0.0, "lights/floor-bounce/color": [0.35, 0.42, 0.7],
    "lights/floor-bounce/intensity": 0.03, "lights/dusk-sky/intensity": 0.06, "lights/dusk-bounce/intensity": 0.0,
    "lights/campfire/intensity": 5.0, "lights/campfire/color": [1.0, 0.36, 0.1],
    "nodes/sasquatch-a/visible": True, "nodes/sasquatch-b/visible": True, "nodes/sasquatch-c/visible": True,
    "nodes/campfire-logs/visible": True, "nodes/campfire-flames/visible": False, "nodes/campfire-core/visible": False,
    "nodes/campfire-embers/visible": True, "nodes/campfire-smoke/visible": True,
}
scene["looks"] = [{"name": "dusk", "from": 1, "params": dusk}, {"name": "dusk-portal", "from": 2, "params": dusk_portal},
                  {"name": "night", "from": 3, "params": night}]

# --- the second leg (ADR-1895): the outer cliff behind the camp opens onto Mars (build_mars.py)
import build_mars  # noqa: E402
_, mars_flight = build_mars.write()
cliff = canyon(-8.0, 91.8)
into = [canyon(0.0, 1.0)[i] - canyon(0.0, 0.0)[i] for i in (0, 1)]
n_into = math.hypot(*into)
scene["portals"] = [{"world": "strange-attractor-mars.scene.json", "exit": build_mars.EXIT, "exitYaw": 0.0,
                     "exitAxis": build_mars.EXIT_AXIS, "entryUp": [0.0, 1.0, 0.0], "exitUp": [0.0, 1.0, 0.0],
                     "entry": [cliff[0], 95.0, cliff[1]], "entryAxis": [into[0] / n_into, 0.0, into[1] / n_into],
                     "flight": mars_flight}]
json.dump(scene, open(PATH, "w"), indent=2)

# --- the project: the Earth answers the music (a first pass for the owner to tune): the bass lifts its glow
routes = [r for r in project["routes"] if not r["target"].startswith("moon/nodes/earth/")]
routes.append({"source": "response.bass", "target": "moon/nodes/earth/emissiveBoost", "amount": 1.0, "op": "replace",
               "polarity": "unipolar", "chain": {"remapEnabled": True, "remapInMin": 0.3, "remapInMax": 1.0,
                                                 "remapOutMin": 0.0, "remapOutMax": 1.6, "clampEnabled": True,
                                                 "clampMin": 0.0, "clampMax": 1.0, "attackMs": 60.0, "decayMs": 450.0}})
project["routes"] = routes
json.dump(project, open(PROJECT, "w"), indent=2)
print(PATH)

"""Writes the journey into the canyon scene (strange-attractor-flythrough.scene.json), Art Pass 2 (ADR-1885, 1886):

    python3 examples/strange-attractor/build_journey.py

- the Moon route after portal 1 (the canyon's `portal.flight`), timed to the 210 s cut;
- the canyon's looks: the day as authored, dusk from crossing 1 (when the camera leaves for the Moon, so the canyon it
  returns to, and sees through the Moon's floor, is the dusk canyon);
- the dusk lights (with the day's, off until the look turns them on), the campfire's light;
- the first encounter: three Sasquatches round a fire on the terrace beside the dusk exit, shown only in the dusk look.

The dusk look's values came from the dusk world (Gate C 2, ADR-1884), which this replaces: the canyon is one world now.
The Sasquatches are built by pass2/build_sasquatch.py from the owner's CC BY download (assets-pass2.md).
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PATH = os.path.join(HERE, "strange-attractor-flythrough.scene.json")
scene = json.load(open(PATH))


def canyon(d, lat):
    return [166.5 - 0.426 * d + 0.905 * lat, 3427.3 - 0.905 * d - 0.426 * lat]


# --- the Moon route (x, height above the lunar ground, z, t); crossing 1 at 32.025 s, crossing 2 at 67.70 s (bar 35)
scene["portal"]["flight"] = [
    [-8, 38, 1420, 35.0],      # rising off the exit over a sunlit plain, the shadows long
    [-60, 18, 1310, 38.2],     # low beside a fresh crater, through its boulders
    [-20, 40, 1150, 42.5],     # cresting the great basin's rim: the night side beyond, the domes far across it
    [-120, 70, 950, 47.0],     # down into its shadow (earthshine)
    [-10, 75, 1010, 50.5],     # swinging east along the shadow, the domes off to the right
    [180, 80, 960, 54.5],
    [290, 85, 860, 58.5],      # turned south toward the portal crater, the domes ahead beyond it
    [270, 130, 690, 62.0],
    [284, 150, 650, 64.2],     # over the crater, slowing; the floor opens beneath (the Gate C 2 dive: a near-vertical
                               # drop along +x, which the rotation portal turns into the wall's level exit)
    [300, 0, 650, 67.70],      # the drop through the floor (the portal turns it into the wall's level exit)
    [301, -45, 650, 68.7],
]
# while the route swings east along the shadow, the eye stays on the domes (x, y, z, from, to)
scene["portal"]["aims"] = [[-30, -60, -670, 49.5, 59.5]]

# --- the lights: the day's fills, the dusk's (off by day), the campfire
CAMP = canyon(75, 45)
CAMP_Y = 3.0   # measured on the bank (2.7-3.3 m round the fire)
lights = [l for l in scene["lights"] if l["name"] in ("canyon-bounce", "floor-bounce")]
lights += [
    {"name": "dusk-sky", "type": "directional", "role": "fill", "direction": [0.1, -1.0, 0.15], "color": [0.36, 0.38, 1.0],
     "intensity": 0.0, "castsShadow": False, "shadowStrength": 0.0},
    {"name": "dusk-bounce", "type": "directional", "role": "fill", "direction": [-0.9, -0.15, 0.4], "color": [0.55, 0.42, 0.95],
     "intensity": 0.0, "castsShadow": False, "shadowStrength": 0.0},
    {"name": "campfire", "type": "point", "position": [CAMP[0], CAMP_Y + 0.9, CAMP[1]], "color": [1.0, 0.52, 0.22],
     "intensity": 0.0, "range": 45.0, "castsShadow": True, "shadowStrength": 1.0, "flicker": 0.35, "flickerRate": 3.2},
]
scene["lights"] = lights

# --- the encounter: three Sasquatches round the fire, facing it, and the fire itself (particles)
nodes = [n for n in scene.get("nodes", []) if not n["name"].startswith(("sasquatch-", "campfire"))]
A = "../../assets/strange-attractor/build/"
for k, (name, ang, dist) in enumerate((("sasquatch-a", 200.0, 2.6), ("sasquatch-b", 320.0, 2.9), ("sasquatch-c", 80.0, 2.4))):
    a = math.radians(ang)
    x, z = CAMP[0] + dist * math.cos(a), CAMP[1] + dist * math.sin(a)
    # face the fire: the glb faces -y in Blender, +z in glTF; yaw so +z points at the camp
    yaw = math.degrees(math.atan2(CAMP[0] - x, CAMP[1] - z))
    nodes.append({"name": name, "kind": "gltf", "asset": A + f"{name}.glb", "position": [x, CAMP_Y, z],
                  "rotation": [0, yaw, 0], "scale": [1, 1, 1], "visible": False,
                  "animation": {"state": "idle", "cullDistance": 800.0, "speed": 1.0}})
fire = [CAMP[0], CAMP_Y + 0.1, CAMP[1]]
nodes += [
    {"name": "campfire-flames", "kind": "particles", "visible": False, "particles": {
        "enabled": True, "capacity": 1500, "seed": 31, "shape": "sphere", "position": fire, "extent": [0.35, 0.1, 0.35],
        "direction": [0, 1, 0], "spawnRate": 260.0, "lifetimeMin": 0.35, "lifetimeMax": 0.8, "spread": 0.18,
        "speedMin": 0.9, "speedMax": 2.0, "gravity": [0.05, 1.6, 0], "drag": 0.5, "turbulence": 0.7, "turbulenceScale": 0.6,
        "sizeStart": 0.45, "sizeEnd": 0.08, "sizeVariance": 0.4, "colorStart": [1.0, 0.62, 0.2, 0.55],
        "colorEnd": [1.0, 0.1, 0.0, 0.0], "emissive": 7.0, "blend": "additive", "velocityStretch": 0.2, "stretchMax": 0.4}},
    {"name": "campfire-embers", "kind": "particles", "visible": False, "particles": {
        "enabled": True, "capacity": 600, "seed": 37, "shape": "sphere", "position": fire, "extent": [0.3, 0.1, 0.3],
        "direction": [0, 1, 0], "spawnRate": 40.0, "lifetimeMin": 1.5, "lifetimeMax": 3.5, "spread": 0.35,
        "speedMin": 1.0, "speedMax": 2.6, "gravity": [0.25, 0.5, 0.1], "drag": 0.3, "turbulence": 0.9, "turbulenceScale": 0.5,
        "sizeStart": 0.035, "sizeEnd": 0.008, "colorStart": [1.0, 0.7, 0.25, 1.0], "colorEnd": [1.0, 0.15, 0.0, 0.0],
        "emissive": 16.0, "blend": "additive"}},
    {"name": "campfire-smoke", "kind": "particles", "visible": False, "particles": {
        "enabled": True, "capacity": 500, "seed": 41, "shape": "sphere", "position": [fire[0], fire[1] + 1.2, fire[2]],
        "extent": [0.3, 0.2, 0.3], "direction": [0, 1, 0], "spawnRate": 18.0, "lifetimeMin": 4.0, "lifetimeMax": 7.0,
        "spread": 0.25, "speedMin": 0.5, "speedMax": 1.0, "gravity": [0.3, 0.35, 0.1], "drag": 0.6, "turbulence": 0.5,
        "turbulenceScale": 0.3, "sizeStart": 0.5, "sizeEnd": 2.2, "colorStart": [0.18, 0.16, 0.2, 0.25],
        "colorEnd": [0.12, 0.11, 0.16, 0.0], "emissive": 0.0, "blend": "alpha"}},
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
    "lights/campfire/intensity": 90.0,
    "nodes/sasquatch-a/visible": True, "nodes/sasquatch-b/visible": True, "nodes/sasquatch-c/visible": True,
    "nodes/campfire-flames/visible": True, "nodes/campfire-embers/visible": True, "nodes/campfire-smoke/visible": True,
}
scene["looks"] = [{"name": "dusk", "from": 1, "params": dusk}]
json.dump(scene, open(PATH, "w"), indent=2)
print(PATH)

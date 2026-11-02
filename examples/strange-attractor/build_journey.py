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
from canyon_scale import (WORLD_SCALE as WS, R, POC, PREFIX, CAMP_SITE, O_UNIT, L_UNIT, site, near,  # noqa: E402
                          rescale)  # (ADR-1893)
from sample_ground import heights  # noqa: E402

# M3b: the canyon scale POC (SA_WORLD_SCALE=0.2) is the same journey in a canyon half the size: it starts from the
# current canyon's files and writes its own (poc-*.scene.json, strange-attractor-poc.json); the current canyon is kept
SRC = os.path.join(HERE, "strange-attractor-flythrough.scene.json")
SRC_PROJECT = os.path.join(HERE, "strange-attractor.json")
PATH = os.path.join(HERE, PREFIX + "strange-attractor-flythrough.scene.json")
PROJECT = os.path.join(HERE, "strange-attractor-poc.json" if POC else "strange-attractor.json")
scene = json.load(open(SRC))
project = json.load(open(SRC_PROJECT))
project["assets"]["scene"]["path"] = os.path.basename(PATH)
if POC:
    project["app"]["name"] = "The Strange Attractor (canyon scale POC)"
    project["render"]["path"] = "renders/strange-attractor-poc.mp4"

# --- M1b: the world's scale (ADR-1893). Every Pass 1 length in the scene and the project, once, by ratio.
rescale(scene, project)
# M1b (owner): the portal's corruption spills across the river again, as at Gate A -- the bed's blocks and glyphs rise
# through the water. ADR-1873's floor kept them under it; it goes.
scene["corruption"].pop("floor", None)


def canyon(d, lat):
    """A place on the terrain (d, lat in the current canyon's metres): it follows the terrain at any scale."""
    return site(d, lat)


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
scene["portal"]["world"] = PREFIX + "strange-attractor-moon.scene.json"

# --- the camp (M3b): the gathering's places, the ground under them (sampled; the builder runs twice, as the pads change
# the ground), the opened bench (ADR-1897), the props
from canyon_scale import CAST, GATHER_AT  # noqa: E402
import random  # noqa: E402

CAMP = site(*CAMP_SITE)       # the low bank right of the dusk exit (build_moon.py's CAMP)
CAMP_Y = 4.4 * WS             # measured on the bank (generator 4.4 m)
GRAY_SCALE = 0.68             # the downloaded Gray read as tall as a Sasquatch at its own scale: about 1.5 m here
UFO_SCALE = 0.018             # the model is 666 x 238 units (load log): a 12 m disc, 4.3 m tall
UFO_LIFT = 1.9                # its underside (-119 units) just above the ground: resting on its keel
UFO_AT = (-11.0, 11.0)         # behind the group (seen past it from the night arc), on the opened bench, clear of the descent


def at(dd, dl):
    return near(CAMP_SITE, dd, dl)


def ground(points):
    try:
        return heights(os.path.basename(PATH), [tuple(p) for p in points])
    except Exception:
        return [CAMP_Y] * len(points)


# the bench opened round the camp (owner: open up the Sasquatch area): the terrace steps under the gathering and the
# craft eased to one footing (generator units are the world's: the pad is in world metres)
PAD_C = at(-3.0, 5.0)
scene["attractorWorld"]["pads"] = [{"x": PAD_C[0], "z": PAD_C[1], "radius": 15.0, "level": CAMP_Y}]

lights = [l for l in scene["lights"] if l["name"] in ("canyon-bounce", "floor-bounce")]
UFO_PARK = at(*UFO_AT)
ufo_fwd = (math.sin(math.radians(33.0)), math.cos(math.radians(33.0)))
lights += [
    {"name": "dusk-sky", "type": "directional", "role": "fill", "direction": [0.1, -1.0, 0.15], "color": [0.36, 0.38, 1.0],
     "intensity": 0.0, "castsShadow": False, "shadowStrength": 0.0},
    {"name": "dusk-bounce", "type": "directional", "role": "fill", "direction": [-0.9, -0.15, 0.4], "color": [0.55, 0.42, 0.95],
     "intensity": 0.0, "castsShadow": False, "shadowStrength": 0.0},
    {"name": "moon-rim", "type": "directional", "role": "fill", "direction": [0.55, -0.45, 0.7], "color": [0.55, 0.65, 1.0],
     "intensity": 0.0, "castsShadow": False, "shadowStrength": 0.0},
    # M3b: the fire is the night's key -- it casts the gathering's shadows across the bench
    {"name": "campfire", "type": "point", "position": [CAMP[0], CAMP_Y + 0.9, CAMP[1]], "color": [1.0, 0.52, 0.22],
     "intensity": 0.0, "range": 30.0, "castsShadow": True, "shadowStrength": 1.0, "flicker": 0.3, "flickerRate": 2.6,
     "volumetric": 0.2},
    # M3b: the craft lit by its own lights -- the underside's glow and its spill on the ground, the rim's lamps
    {"name": "ufo-glow", "type": "point", "position": [UFO_PARK[0], CAMP_Y + 0.9, UFO_PARK[1]],
     "color": [0.45, 0.85, 1.0], "intensity": 0.0, "range": 18.0, "castsShadow": False, "volumetric": 0.0},
] + [{"name": f"ufo-rim-{k}", "type": "point",
      "position": [UFO_PARK[0] + 6.3 * math.cos(a_), CAMP_Y + UFO_LIFT + 0.4, UFO_PARK[1] + 6.3 * math.sin(a_)],
      "color": c, "intensity": 0.0, "range": 7.5, "castsShadow": False, "volumetric": 0.0}
     for k, (a_, c) in enumerate(((0.3, [1.0, 0.55, 0.2]), (1.9, [0.4, 0.9, 1.0]), (3.5, [1.0, 0.25, 0.2]),
                                   (5.1, [0.4, 0.9, 1.0])))]
scene["lights"] = lights

# --- the encounter: three Sasquatches round the fire, the Gray facing them (night), the craft, the fire (particles)
nodes = [n for n in scene.get("nodes", []) if not n["name"].startswith(
    ("sasquatch-", "campfire", "gray", "ufo-parked", "camp-", "seat-rock", "cave-mouth", "canyon-rim"))]
A = "../../assets/strange-attractor/build/"
cast_xz = {k: at(*v) for k, v in CAST.items()}
cast_g = dict(zip(cast_xz, ground(cast_xz.values())))
gray_xz = cast_xz["gray"]


def yaw_to(src, dst):
    """The yaw that turns a glb's front (+z) from src toward dst."""
    return math.degrees(math.atan2(dst[0] - src[0], dst[1] - src[1]))


group = [cast_xz[k] for k in ("sasquatch-a", "sasquatch-b", "sasquatch-c")]
group_c = [sum(p[0] for p in group) / 3.0, sum(p[1] for p in group) / 3.0]
faces = {"sasquatch-a": [0.5 * (CAMP[0] + gray_xz[0]), 0.5 * (CAMP[1] + gray_xz[1])],   # half toward the Gray
         "sasquatch-b": CAMP, "sasquatch-c": CAMP}
for name in ("sasquatch-a", "sasquatch-b", "sasquatch-c"):
    x, z = cast_xz[name]
    nodes.append({"name": name, "kind": "gltf", "asset": A + f"{name}.glb", "position": [x, cast_g[name], z],
                  "rotation": [0, yaw_to((x, z), faces[name]), 0], "scale": [1, 1, 1], "visible": False,
                  "animation": {"state": "idle", "cullDistance": 800.0, "speed": 1.0}})
# Sasquatch C sits: the rock under it, 0.36 m behind its feet (its hips' offset in the sitting pose)
cx, cz = cast_xz["sasquatch-c"]
cyaw = math.radians(yaw_to((cx, cz), CAMP))
seat = [cx - 0.36 * math.sin(cyaw), cz - 0.36 * math.cos(cyaw)]
nodes.append({"name": "seat-rock", "kind": "gltf", "asset": A + "seat-rock.glb",
              "position": [seat[0], cast_g["sasquatch-c"] - 0.04, seat[1]], "rotation": [0, math.degrees(cyaw) + 20.0, 0],
              "scale": [0.95, 0.92, 0.95], "visible": True})
# the Gray: across the fire from the three, facing them, fully in frame (night only)
nodes.append({"name": "gray", "kind": "gltf", "asset": "../../assets/strange-attractor/sketchfab/gray/grey-alien-idle/grey_alien_idle.glb",
              "position": [gray_xz[0], cast_g["gray"], gray_xz[1]], "rotation": [0, yaw_to(gray_xz, group_c), 0],
              "scale": [GRAY_SCALE] * 3, "visible": False,
              "animation": {"state": "mixamo.com", "cullDistance": 800.0, "speed": 0.8}})
nodes.append({"name": "ufo-parked", "kind": "gltf", "asset": A + "ufo-craft.glb",
              "position": [UFO_PARK[0], CAMP_Y + UFO_LIFT, UFO_PARK[1]], "rotation": [0, 33, 0],
              "scale": [UFO_SCALE] * 3, "visible": False, "emissiveBoost": 4.0})

# the cave mouth in the bank behind the camp: where the ground first stands 4 m over the bench, along the bank's
# normal, sunk 3.6 m into it, its front (+z) toward the river
probe = [at(-4.0, dl) for dl in [x * 0.5 for x in range(8, 60)]]
pg = ground(probe)
cave = next(((p, g) for p, g in zip(probe, pg) if g - CAMP_Y > 4.0), (probe[-1], pg[-1]))
cave_xz = [cave[0][0] + 3.6 * L_UNIT[0], cave[0][1] + 3.6 * L_UNIT[1]]
nodes.append({"name": "cave-mouth", "kind": "gltf", "asset": A + "cave-mouth.glb",
              "position": [cave_xz[0], CAMP_Y - 0.6, cave_xz[1]],
              "rotation": [0, math.degrees(math.atan2(-L_UNIT[0], -L_UNIT[1])), 0], "scale": [1.0, 1.0, 1.0],
              "visible": True})


# scrub and rocks across the bench and the banks round it (seeded), kept off the camera's low paths, the gathering,
# the craft and the water; heights sampled
def seg_dist(p, a, b):
    ax, az, bx, bz = a[0], a[1], b[0], b[1]
    vx, vz = bx - ax, bz - az
    t = max(0.0, min(1.0, ((p[0] - ax) * vx + (p[1] - az) * vz) / max(vx * vx + vz * vz, 1e-9)))
    return math.hypot(p[0] - (ax + t * vx), p[1] - (az + t * vz))


import build_mars  # noqa: E402


def build_ocean_home_path():
    import build_ocean
    return build_ocean.HOME_PATH


keep_paths = [[at(-4.0, -7.0), at(-2.5, -5.0), at(1.5, -5.5), at(1.5, -4.5)],              # the dusk arrival
              [at(-1.0, -9.5), at(-5.5, -7.5), at(-8.0, -3.5),
               [build_mars.RIVER_PORTAL[0], build_mars.RIVER_PORTAL[1]]],                # the night arc and glide
              [site(d, l) for d, l, _ in build_ocean_home_path()]]                       # home at dawn
rng = random.Random(1897)
cands = []
for _ in range(900):
    r_, a_ = 3.5 + 26.0 * math.sqrt(rng.random()), rng.random() * 2.0 * math.pi
    dd, dl = r_ * math.cos(a_), r_ * math.sin(a_)
    p = at(dd, dl)
    if any(seg_dist(p, path[i], path[i + 1]) < 2.6 for path in keep_paths for i in range(len(path) - 1)):
        continue
    if min(math.dist(p, q) for q in list(cast_xz.values()) + [CAMP]) < 2.4 or math.dist(p, UFO_PARK) < 8.0:
        continue
    if math.dist(p, cave_xz) < 5.0:
        continue
    cands.append(p)
cg = ground(cands)
k = 0
for p, g in zip(cands, cg):
    if g < 0.05 or abs(g - CAMP_Y) > 7.0:     # not in the water, not up the cliff
        continue
    kind = rng.choice(["scrub-a", "scrub-b", "scrub-c", "scrub-a", "scrub-b", "rock-a", "rock-b", "rock-c", "rock-d"])
    sc = rng.uniform(0.7, 1.25) * (1.0 if kind.startswith("scrub") else rng.uniform(0.6, 1.3))
    nodes.append({"name": f"camp-{kind}-{k}", "kind": "gltf", "asset": A + f"{kind}.glb", "position": [p[0], g - 0.05, p[1]],
                  "rotation": [0, rng.uniform(0, 360), 0], "scale": [sc] * 3, "visible": True})
    k += 1
    if k >= 70:
        break
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
    # M3c (owner: too long staring at a plain wall): the field reaches back to the camp, so the code gathers on the
    # cliff as the camera leaves the fire, and the tear starts 80 m out
    "corruption/radius": 80.0, "corruption/fieldNear": 12.0, "corruption/fieldFar": 260.0, "corruption/openNear": 10.0,
    "corruption/openFar": 100.0, "corruption/width": 15.0, "corruption/height": 13.0, "corruption/depth": 6.0,
    "corruption/echoOffset": 0.35,
})
# night (crossing 3: the canyon behind the pyramid portal and home): dark, a cold moon, the fire down to embers
night = {
    # M3b (owner: a real night pass): a cold moon, higher and stronger, that casts the figures', the rocks' and the
    # craft's shadows on the bench; less sky fill so the shadows hold; the fire burning again, the shadow-casting key
    # at the gathering; the craft lit by its own lights
    "attractorWorld/sunAzimuth": 55.0, "attractorWorld/sunElevation": 38.0, "attractorWorld/sunIntensity": 0.22, "attractorWorld/sunWarmth": 0.0,
    "attractorWorld/contourGain": 0.0,
    "env/intensity": 0.022,
    "env/sky/zenithColor": [0.004, 0.007, 0.028], "env/sky/horizonColor": [0.035, 0.05, 0.12],
    "env/sky/groundColor": [0.003, 0.003, 0.01], "env/sky/haze": 0.1, "env/sky/sunColor": [0.62, 0.72, 1.0],
    "env/sky/sunIntensity": 0.6, "env/sky/sunSize": 0.03, "env/sky/sunGlow": 0.2,
    "scene/fogColor": [0.008, 0.01, 0.03],
    "lights/canyon-bounce/intensity": 0.0, "lights/floor-bounce/color": [0.35, 0.42, 0.7],
    "lights/floor-bounce/intensity": 0.03, "lights/dusk-sky/intensity": 0.05, "lights/dusk-bounce/intensity": 0.0,
    "lights/moon-rim/intensity": 0.2, "lights/ufo-glow/intensity": 90.0,
    "lights/ufo-rim-0/intensity": 30.0, "lights/ufo-rim-1/intensity": 30.0, "lights/ufo-rim-2/intensity": 30.0,
    "lights/ufo-rim-3/intensity": 30.0,
    "lights/campfire/intensity": 12.0, "lights/campfire/color": [1.0, 0.45, 0.16], "lights/campfire/range": 13.0,
    "nodes/gray/visible": True, "nodes/ufo-parked/visible": True,
    "nodes/sasquatch-a/visible": True, "nodes/sasquatch-b/visible": True, "nodes/sasquatch-c/visible": True,
    "nodes/campfire-logs/visible": True, "nodes/campfire-flames/visible": True, "nodes/campfire-core/visible": True,
    "nodes/campfire-embers/visible": True, "nodes/campfire-smoke/visible": True,
}
# home from Mars (crossing 4): night, and the riverbed portal's corruption: small, gathering as the camera leaves the camp
night_portal = dict(night)
night_portal.update({
    # M3c (the owner: the entrance's animation was lost): the glide starts 25 m out, so the tear now opens during it
    # (from 14 m), seen whole, not already open when the camera turns to the river
    "corruption/radius": 24.0, "corruption/fieldNear": 6.0, "corruption/fieldFar": 40.0, "corruption/openNear": 2.5,
    "corruption/openFar": 14.0, "corruption/width": 8.0, "corruption/height": 8.0, "corruption/depth": 4.0,
    "corruption/echoOffset": 0.35,
})
# dawn (M3b; from crossing 5, so the canyon seen through the launch bay is already the morning one): home through
# portal 1's wall, the craft and the Gray gone, the fire out to a thread of smoke, the three still there; a low sun
# up the canyon, warm on the walls, the river's sheen; no portal open
dawn = {
    "attractorWorld/sunAzimuth": 150.0, "attractorWorld/sunElevation": 7.0, "attractorWorld/sunIntensity": 0.9,
    "attractorWorld/sunWarmth": 1.0,
    "attractorWorld/contourGain": 0.0,
    "env/intensity": 0.32,
    "env/sky/zenithColor": [0.09, 0.15, 0.36], "env/sky/horizonColor": [0.95, 0.62, 0.42],
    "env/sky/groundColor": [0.03, 0.025, 0.03], "env/sky/haze": 0.3, "env/sky/sunColor": [1.0, 0.66, 0.38],
    "env/sky/sunIntensity": 6.0, "env/sky/sunSize": 0.05, "env/sky/sunGlow": 0.6,
    "scene/fogColor": [0.42, 0.34, 0.36],
    "lights/canyon-bounce/intensity": 0.0, "lights/floor-bounce/color": [0.9, 0.6, 0.5],
    "lights/floor-bounce/intensity": 0.2, "lights/dusk-sky/intensity": 0.35, "lights/dusk-bounce/intensity": 0.2,
    "lights/campfire/intensity": 0.0,
    "corruption/amount": 0.0,
    "nodes/sasquatch-a/visible": True, "nodes/sasquatch-b/visible": True, "nodes/sasquatch-c/visible": True,
    "nodes/campfire-logs/visible": True, "nodes/campfire-smoke/visible": True,
}
scene["looks"] = [{"name": "dusk", "from": 1, "params": dusk}, {"name": "dusk-portal", "from": 2, "params": dusk_portal},
                  {"name": "night", "from": 3, "params": night}, {"name": "night-portal", "from": 4, "params": night_portal},
                  {"name": "dawn", "from": 5, "params": dawn}]
# the portals' sizes follow the terrain (the POC's canyon is half the size)
for look in scene["looks"]:
    for k_ in ("radius", "fieldNear", "fieldFar", "openNear", "openFar", "width", "height", "depth"):
        if f"corruption/{k_}" in look["params"]:
            look["params"][f"corruption/{k_}"] *= R

# --- the second leg (ADR-1895): the outer cliff behind the camp opens onto Mars (build_mars.py)
_, mars_flight, mars_aims = build_mars.write()
cliff = site(-8.0, 91.8)
into = [site(0.0, 1.0)[i] - site(0.0, 0.0)[i] for i in (0, 1)]
n_into = math.hypot(*into)
scene["portals"] = [{"world": PREFIX + "strange-attractor-mars.scene.json", "exit": build_mars.EXIT, "exitYaw": 0.0,
                     "exitAxis": build_mars.EXIT_AXIS, "entryUp": [0.0, 1.0, 0.0], "exitUp": [0.0, 1.0, 0.0],
                     "entry": [cliff[0], 95.0 * R, cliff[1]], "entryAxis": [into[0] / n_into, 0.0, into[1] / n_into],
                     "flight": mars_flight, "aims": mars_aims}]
# the third leg (M3; M3b: one smooth glide): the riverbed below the camp opens onto the deep ocean (build_ocean.py). The
# opening lies in the river (its axis down); the camera glides into it on one straight line, 25 degrees down, at a
# constant speed. The exit frame is tilted by the same 25 degrees, so the glide comes out level, upright and at the
# same speed in the ocean: the crossing changes nothing in the motion (owner: the water portal felt like a step)
import build_ocean  # noqa: E402
_, ocean_flight = build_ocean.write()
sl = build_mars.GLIDE_SLOPE
scene["portals"].append({"world": PREFIX + "strange-attractor-ocean.scene.json", "exit": build_ocean.EXIT, "exitYaw": 0.0,
                         "exitAxis": [0.0, -math.cos(sl), -math.sin(sl)], "exitUp": [0.0, math.sin(sl), -math.cos(sl)],
                         "entryUp": [build_mars.TOWARD_RIVER[0], 0.0, build_mars.TOWARD_RIVER[1]],
                         "entry": [build_mars.RIVER_PORTAL[0], build_mars.RIVER_Y, build_mars.RIVER_PORTAL[1]],
                         "entryAxis": [0.0, -1.0, 0.0], "flight": ocean_flight})
# M3c (owner): no rim particles on the portals (ADR-1898 withdrawn)
scene["nodes"] = [n for n in scene["nodes"] if n["name"] != "canyon-rim"]
json.dump(scene, open(PATH, "w"), indent=2)

# --- the project: the Earth answers the music (a first pass for the owner to tune): the bass lifts its glow
routes = [r for r in project["routes"] if not r["target"].startswith("moon/nodes/earth/")]
routes.append({"source": "response.bass", "target": "moon/nodes/earth/emissiveBoost", "amount": 1.0, "op": "add",
               "polarity": "unipolar", "chain": {"remapEnabled": True, "remapInMin": 0.3, "remapInMax": 1.0,
                                                 "remapOutMin": 0.0, "remapOutMax": 0.8, "clampEnabled": True,
                                                 "clampMin": 0.0, "clampMax": 1.0, "attackMs": 60.0, "decayMs": 450.0}})
# M3b: the Moon's rave and the Earth's datamosh, on the beat clock and the response (an envelope that falls in a
# quarter-second, so a strobe is a deliberate flash, not a shimmer; with audio off the sources rest at 0)
routes = [r for r in routes if not (r["target"].startswith("moon/lights/rave") or r["target"].startswith("moon/nodes/laser")
                                    or "-rave-" in r["target"] or r["target"].startswith("moon/nodes/earth-halo")
                                    or r["target"].startswith("moon/nodes/earth-echo"))]


def beat_route(source, target, lo, hi, attack=8.0, decay=260.0, op="add"):
    return {"source": source, "target": target, "amount": 1.0, "op": op, "polarity": "unipolar",
            "chain": {"remapEnabled": True, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": lo, "remapOutMax": hi,
                      "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0, "attackMs": attack, "decayMs": decay}}


for k in range(3):
    routes.append(beat_route("beat.pulse", f"moon/lights/rave{k}-m/intensity", 0.0, 30000.0))
    routes.append(beat_route("response.high", f"moon/lights/rave{k}-c/intensity", 0.0, 24000.0, decay=200.0))
    routes.append(beat_route("response.onsetLow", f"moon/lights/rave{k}-g/intensity", 0.0, 28000.0, decay=320.0))
    # M3c: the windows -- five colour bands per dome, each on its own source, so the strobes move round the glass
    for g, src, hi, dec in (("m", "beat.pulse", 3.0, 260.0), ("c", "response.high", 2.6, 200.0),
                            ("g", "response.onsetLow", 3.0, 320.0), ("a", "response.mid", 2.2, 240.0),
                            ("v", "response.bass", 2.4, 380.0)):
        routes.append(beat_route(src, f"moon/nodes/dome{k}-rave-{g}/emissiveBoost", 0.0, hi, decay=dec))
for c in ("r", "c"):
    routes.append(beat_route("beat.pulse", f"moon/nodes/earth-echo-{c}/opacity", 0.0, 0.55, decay=220.0, op="replace"))
routes.append(beat_route("beat.pulse", "moon/nodes/earth/emissiveBoost", 0.0, 1.2, decay=400.0))
routes.append(beat_route("beat.pulse", "moon/nodes/earth-halo/emissiveBoost", 0.0, 1.6, decay=360.0))
project["routes"] = routes
# M3: the submersibles' paths and their lights, keyed in the timeline (seconds, smooth)
tracks = [t for t in project.get("timeline", {}).get("tracks", []) if not t["target"].startswith("ocean/")]
for node, pos, yaw in build_ocean.sub_tracks():
    tracks.append({"target": f"ocean/nodes/{node}/position", "keys": [{"time": t, "value": p, "interp": "smooth"} for t, p in pos]})
    tracks.append({"target": f"ocean/nodes/{node}/rotation", "keys": [{"time": t, "value": list(r), "interp": "smooth"} for t, r in yaw]})
    tracks.append({"target": f"ocean/lights/{node}-light/position",
                   "keys": [{"time": t, "value": [p[0], p[1] - 3.0, p[2]], "interp": "smooth"} for t, p in pos]})
# M3b: the rover drives, its tracks appear, its camera flashes (M3c: the lasers are gone)
tracks = [t for t in tracks if not (t["target"].startswith("moon/nodes/laser") or t["target"].startswith("mars/"))]
tracks += build_mars.rover_tracks()
project["timeline"] = {"enabled": True, "tracks": tracks, "cues": project.get("timeline", {}).get("cues", [])}
json.dump(project, open(PROJECT, "w"), indent=2)
print(PATH, PROJECT)

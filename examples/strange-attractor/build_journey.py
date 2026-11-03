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
from canyon_scale import (GAZE, WORLD_SCALE as WS, R, CAMP_SITE, O_UNIT, L_UNIT, site, near,  # noqa: E402
                          rescale)  # (ADR-1893)
from sample_ground import heights  # noqa: E402

SRC = os.path.join(HERE, "strange-attractor-flythrough.scene.json")
SRC_PROJECT = os.path.join(HERE, "strange-attractor.json")
PATH = SRC
PROJECT = SRC_PROJECT
scene = json.load(open(SRC))
project = json.load(open(SRC_PROJECT))
project["assets"]["scene"]["path"] = os.path.basename(PATH)

# --- M1b: the world's scale (ADR-1893). Every Pass 1 length in the scene and the project, once, by ratio.
rescale(scene, project)
# M1b (owner): the portal's corruption spills across the river again, as at Gate A -- the bed's blocks and glyphs rise
# through the water. ADR-1873's floor kept them under it; it goes.
scene["corruption"].pop("floor", None)

# M3d (owner: close-ups must hold up next to the characters): photoscanned detail in the canyon's rock program --
# Poly Haven's `dry_ground_rocks` (CC0; never committed), triplanar in world space at 2.5 m a tile, multiplied into the
# program's own colour (the strata and their palette are kept) and faded out between 30 and 140 m from the camera,
# so the wide shots are the canyon as it was
_rock = [p for p in scene["materialPrograms"] if p["name"] == "canyonRock"][0]
_rock["ops"] = _rock["ops"][:85]
_rock["textures"] = [{"name": "ground", "path": "../../assets/strange-attractor/polyhaven/textures/dry_ground_rocks/dry_ground_rocks_diff_2k.jpg"}]
_rock["ops"] += [
    # M3e (owner: no visible tiling; ADR-1905): hex-tiled reads, a second larger layer, macro variation over 20 m
    {"kind": "textureTriplanar", "dst": 0, "texture": "ground", "value": 0.4, "constant3": [1.0, 0.45, 8.0, 0.6]},
    {"kind": "constant", "dst": 1, "constant": [2.1, 2.1, 2.1, 1.0]},
    {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 1},
    {"kind": "constant", "dst": 1, "constant": [1.0, 1.0, 1.0, 1.0]},
    {"kind": "input", "dst": 3, "input": "depth"},
    {"kind": "remap", "dst": 3, "srcA": 3, "constant": [30.0, 140.0, 0.9, 0.0], "value": 1},
    {"kind": "mixBy", "dst": 0, "srcA": 1, "srcB": 0, "srcC": 3},
    {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 0},
]


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
scene["portal"]["world"] = "strange-attractor-moon.scene.json"
scene["portal"].update(GAZE)   # M3g (54- §1, ADR-1909)
scene.setdefault("post", {})["tonemap"] = 1   # M3g (54- §2): AgX for the whole film (the frame's tone map is global)
# M3g (54- §1): the canyon's own flythrough looks where it goes, not 2.5 s ahead and down at the river (ADR-1909)
scene["attractorWorld"]["flightLookAhead"] = 0.6
scene["attractorWorld"]["flightLookDown"] = 0.03

# --- the camp (M3b): the gathering's places, the ground under them (sampled; the builder runs twice, as the pads change
# the ground), the opened bench (ADR-1897), the props
from canyon_scale import CAST, GATHER_AT  # noqa: E402
import random  # noqa: E402

CAMP = site(*CAMP_SITE)       # the low bank right of the dusk exit (build_moon.py's CAMP)
CAMP_Y = 4.4 * WS             # measured on the bank (generator 4.4 m)
GRAY_SCALE = 0.68             # the downloaded Gray read as tall as a Sasquatch at its own scale: about 1.5 m here
UFO_SCALE = 0.018             # the model is 666 x 238 units (load log): a 12 m disc, 4.3 m tall
UFO_LIFT = 1.9                # its underside (-119 units) just above the ground: resting on its keel
# M3f (owner §2): the craft is the Gray's, beside him -- down-canyon of him on the river's side of the group, not behind
# the Sasquatches. Owner, after 01-craft-move: and inside the owner's camp camera (which looks from the water's edge
# toward the bank, so the shore itself is behind it): 10 m down-canyon, 5 m bankward of the fire, 9.5 m from the Gray,
# 27 degrees left of that camera's axis (its frame reaches 44.5), on a pad at the camp's level
UFO_AT = (10.0, 5.0)
UFO_SHORE = CAMP_Y


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
PORTAL1_XZ = site(37.2, 27.2)          # portal 1's focus on the ground (REF metres)
scene["attractorWorld"]["pads"] = [{"x": PAD_C[0], "z": PAD_C[1], "radius": 15.0, "level": CAMP_Y},
                                   {"x": at(*UFO_AT)[0], "z": at(*UFO_AT)[1], "radius": 9.0, "level": UFO_SHORE}]

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
    # M3e (48-night-lighting-research.md F1/F3, ADR-1906): the fire's cube now fits beside the moon's cascades and is
    # biased in its own depth, so it casts; its contact march grounds feet, rocks and the pit; it lights the haze
    # M3f (hero H4): raised above the new flames' tips (the light inside the flame sheets lit them from within)
    {"name": "campfire", "type": "point", "position": [CAMP[0], CAMP_Y + 1.35, CAMP[1]], "color": [1.0, 0.52, 0.22],
     "intensity": 0.0, "range": 30.0, "castsShadow": True, "shadowStrength": 1.0, "contactShadow": True, "softness": 0.45,
     "flicker": 0.3, "flickerRate": 2.6, "volumetric": 0.6},
    # M3e (research A4): the fire's bounce -- a broad, soft, unshadowed warm wrap low at the pit, the light the ground
    # and the bank throw back onto the figures' undersides and the shrubs
    {"name": "fire-bounce", "type": "point", "position": [CAMP[0], CAMP_Y + 0.3, CAMP[1]], "color": [1.0, 0.62, 0.38],
     "intensity": 0.0, "range": 24.0, "castsShadow": False, "contactShadow": False, "flicker": 0.15, "flickerRate": 1.5,
     "volumetric": 0.0},
    # M3b: the craft lit by its own lights -- the underside's glow and its spill on the ground, the rim's lamps
    # M3e (owner §9: a white spill on the bank behind the craft with no source): the underside's glow is a spot straight
    # down from the hull's belly, so it pools on the ground under the craft and nowhere else
    {"name": "ufo-glow", "type": "spot", "position": [UFO_PARK[0], UFO_SHORE + UFO_LIFT - 0.2, UFO_PARK[1]],
     "direction": [0.0, -1.0, 0.0], "innerCone": 35.0, "outerCone": 62.0,
     "color": [0.35, 0.75, 1.0], "intensity": 0.0, "range": 7.0, "castsShadow": False, "contactShadow": False,
     "volumetric": 0.0},
] + [{"name": f"ufo-rim-{k}", "type": "point",
      "position": [UFO_PARK[0] + 6.3 * math.cos(a_), UFO_SHORE + UFO_LIFT + 0.4, UFO_PARK[1] + 6.3 * math.sin(a_)],
      # M3e (owner §9: an unmotivated white spill behind the craft): the rim lamps light the hull, not the bank behind;
      # M3f: back on at night, a 2 m reach, pulsing with the beat
      "color": c, "intensity": 0.0, "range": 2.0, "castsShadow": False, "volumetric": 0.0}
     for k, (a_, c) in enumerate(((0.3, [1.0, 0.55, 0.2]), (1.9, [0.4, 0.9, 1.0]), (3.5, [1.0, 0.25, 0.2]),
                                   (5.1, [0.4, 0.9, 1.0])))]
for l_ in lights:   # M3e: only the fire marches contact shadows (the scene's switch is on, ADR-1907 turns the moon's off)
    l_.setdefault("contactShadow", False)
# M3f (owner §2): the craft's window band spills toward the Gray beside it -- a soft, unshadowed cyan from the hull's rim
# on his side, falling on him and the shore between them (it pulses with the craft's lights)
_g = at(*CAST["gray"])
_d = (_g[0] - UFO_PARK[0], _g[1] - UFO_PARK[1])
_dn = math.hypot(*_d)
lights.append({"name": "ufo-spill", "type": "point",
               "position": [UFO_PARK[0] + _d[0] / _dn * 6.8, UFO_SHORE + UFO_LIFT + 1.2, UFO_PARK[1] + _d[1] / _dn * 6.8],
               "color": [0.4, 0.78, 1.0], "intensity": 0.0, "range": 9.0, "castsShadow": False, "contactShadow": False,
               "volumetric": 0.0})
scene["lights"] = lights
scene["environment"]["contactShadows"] = True
scene["attractorWorld"]["look"]["sunContact"] = 0.0
# M3e (research §3, the river): physical-ish water at every hour -- a dark body that reflects at grazing angles (f0 0.06,
# not 0.35), rough enough for a moon path and streaks, glints in the colour of what it reflects, a little transmission
# M3f (owner §2a: white streak bands and vertical streaks on the night river): `streak` spreads the surface normal along
# the view, so EVERY reflection -- the moonlit far bank, the sky -- was drawn out into vertical streaks, and the six
# reflection rays and glints re-lit them; meant for the fire's streaks, but the water takes no local lights (48- F7).
# Off; the chop gentler (ripple 0.48 -> 0.3, roughness 0.085 -> 0.06), so the moon path stays but the bands go.
scene["attractorWorld"]["water"].update({"fresnel": 0.06, "reflection": 1.0, "roughness": 0.06, "ripple": 0.3,
                                         "rippleScale": 0.35, "sparkle": 0.15, "specular": 1.5, "skyMirror": 0.35,
                                         "transmission": 0.5})
for _k in ("streak", "reflectionRays", "glints"):
    scene["attractorWorld"]["water"].pop(_k, None)

# --- the encounter: three Sasquatches round the fire, the Gray facing them (night), the craft, the fire (particles)
nodes = [n for n in scene.get("nodes", []) if not n["name"].startswith(
    ("sasquatch-", "campfire", "gray", "ufo-parked", "camp-", "seat-rock", "cave-mouth", "canyon-rim"))]
A = "../../assets/strange-attractor/build/"
cast_xz = {k: at(*v) for k, v in CAST.items()}
cast_g = dict(zip(cast_xz, ground(cast_xz.values())))
gray_xz = cast_xz["gray"]
# M3g (owner 54- §4: he still looks levitating -- the seat was 3 cm over the stone's top, which reads as a gap from the
# passing camera): Sasquatch C lowered 3 cm onto the stone (check_contacts: no body or card vertex inside the stone;
# the stone cannot come further forward under the thighs -- 6 cm forward already meets them)
SEAT_SIT = 0.03


def yaw_to(src, dst):
    """The yaw that turns a glb's front (+z) from src toward dst."""
    return math.degrees(math.atan2(dst[0] - src[0], dst[1] - src[1]))


group = [cast_xz[k] for k in ("sasquatch-a", "sasquatch-b", "sasquatch-c")]
group_c = [sum(p[0] for p in group) / 3.0, sum(p[1] for p in group) / 3.0]
faces = {"sasquatch-a": [0.5 * (CAMP[0] + gray_xz[0]), 0.5 * (CAMP[1] + gray_xz[1])],   # half toward the Gray
         "sasquatch-b": CAMP, "sasquatch-c": CAMP}
for name in ("sasquatch-a", "sasquatch-b", "sasquatch-c"):
    x, z = cast_xz[name]
    # M3f (hero H3, the hero agent's v1 Sasquatches; assets/strange-attractor/hero, never committed): B lifted 1.3 cm
    # (its fur shells dipped into the ground at the toes)
    asset = "../../assets/strange-attractor/hero/sasquatch/" + f"{name}-v1.glb"
    lift = 0.013 if name == "sasquatch-b" else (-SEAT_SIT if name == "sasquatch-c" else 0.0)
    nodes.append({"name": name, "kind": "gltf", "asset": asset, "position": [x, cast_g[name] + lift, z],
                  "rotation": [0, yaw_to((x, z), faces[name]), 0], "scale": [1, 1, 1], "visible": False,
                  "animation": {"state": "idle", "cullDistance": 800.0, "speed": 1.0}})
# Sasquatch C sits (hero H3: the v1 seat is 0.48 m up and spans 0.42-0.79 m behind the feet). M3f (owner, not the
# hero agent's stool-sized stone): a broad boulder it visibly sits on -- its long axis across (1.0 m, wider than the
# hips), 0.56 m deep, its top about 0.46 m up, its front face about 0.30 m behind the feet (checked with
# hero/check_contacts.py over the whole loop: SEAT_BACK and SEAT_SY are the values that check passed with)
SEAT_BACK, SEAT_SY = 0.68, 0.88
cx, cz = cast_xz["sasquatch-c"]
cyaw = math.radians(yaw_to((cx, cz), CAMP))
seat = [cx - SEAT_BACK * math.sin(cyaw), cz - SEAT_BACK * math.cos(cyaw)]
nodes.append({"name": "seat-rock", "kind": "gltf", "asset": A + "ph-namaqualand_boulder_05.glb",
              "position": [seat[0], cast_g["sasquatch-c"] - 0.05, seat[1]], "rotation": [0, math.degrees(cyaw), 0],
              "scale": [0.75, SEAT_SY, 0.75], "visible": True})
# the Gray: across the fire from the three, facing them, fully in frame (night only)
# M3e (the hero agent's audit): the Gray matte -- metallic 0, no emission (a patched copy, pass2/patch_glb_materials.py)
# M3f (hero H2): the grey alien rigged by the hero agent, talking and gesturing across the fire ("talk", 8 s loop)
nodes.append({"name": "gray", "kind": "gltf", "asset": "../../assets/strange-attractor/hero/alien-grey-h2.glb",
              "position": [gray_xz[0], cast_g["gray"], gray_xz[1]], "rotation": [0, yaw_to(gray_xz, group_c), 0],
              "scale": [GRAY_SCALE] * 3, "visible": False,
              "animation": {"state": "talk", "cullDistance": 800.0, "speed": 1.0}})
# M3f (hero H5): the reworked craft, the same frame and units (rougher panels, lifted occlusion, warm window band)
nodes.append({"name": "ufo-parked", "kind": "gltf", "asset": "../../assets/strange-attractor/hero/ufo/ufo-v1.glb",
              "position": [UFO_PARK[0], UFO_SHORE + UFO_LIFT, UFO_PARK[1]], "rotation": [0, 33, 0],
              "scale": [UFO_SCALE] * 3, "visible": False, "emissiveBoost": 4.0})

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
    if math.dist(p, PORTAL1_XZ) < 18.0:      # M3d (owner): nothing in or round portal 1's opening
        continue
    cands.append(p)
# M3d (owner: rocks and shrubs float; the burger rocks look like toys): photoscanned boulders, stones and shrubs
# (Poly Haven, CC0) and the kit's desert scrub; each set on the LOWEST of five ground samples under its footprint
# (the centre and four round it), and only where those agree within 0.35 m (no perching on a slope or a lip)
KINDS = [("ph-namaqualand_boulder_02", 0.5, (0.5, 1.1)), ("ph-namaqualand_boulder_04", 1.2, (0.35, 0.8)),
         ("ph-namaqualand_boulder_03", 1.3, (0.3, 0.6)), ("ph-namaqualand_stones_01", 0.5, (1.2, 2.2)),
         ("ph-shrub_04", 0.3, (1.6, 2.6)), ("scrub-a", 0.5, (0.8, 1.2)), ("scrub-b", 0.5, (0.8, 1.2)),
         ("scrub-c", 0.4, (0.8, 1.2)), ("ph-shrub_04", 0.3, (1.6, 2.6))]
picks = []
for p in cands:
    kind, foot, (s0, s1) = rng.choice(KINDS)
    sc = rng.uniform(s0, s1)
    r = foot * sc
    picks.append((p, kind, sc, [p] + [[p[0] + r * math.cos(a), p[1] + r * math.sin(a)] for a in (0.0, 1.57, 3.14, 4.71)]))
cg = ground([q for _, _, _, qs in picks for q in qs])
k = 0
for i, (p, kind, sc, _) in enumerate(picks):
    g5 = cg[i * 5:(i + 1) * 5]
    g = min(g5)
    if g < 0.15 or abs(g - CAMP_Y) > 4.0 or max(g5) - g > 0.35:   # not in the water, not up the bank, not on a slope
        continue
    nodes.append({"name": f"camp-{kind}-{k}", "kind": "gltf", "asset": A + f"{kind}.glb", "position": [p[0], g - 0.06, p[1]],
                  "rotation": [0, rng.uniform(0, 360), 0], "scale": [sc] * 3, "visible": True})
    k += 1
    if k >= 60:
        break
fire = [CAMP[0], CAMP_Y + 0.08, CAMP[1]]
nodes += [
    # the logs, the stone ring and the glowing ember bed (pass2/build_campfire.py), at real size
    # M3d (owner: the teepee and ring looked like toy props): a photoscanned stone fire pit and split logs
    # (Poly Haven, CC0: stone_fire_pit, dead_tree_trunk via pass2/build_teepee.py)
    {"name": "campfire-logs", "kind": "gltf", "asset": A + "ph-stone_fire_pit.glb", "position": [CAMP[0], CAMP_Y - 0.08, CAMP[1]],
     "rotation": [0, 20, 0], "scale": [0.85, 0.85, 0.85], "visible": False},
    # M3f (hero H4): the hero agent's fire -- logs charred and cracked, an ash bed with coals, animated flame sheets
    # ("burn", a seamless 4 s loop) -- in the pit's own frame; the flame particles retired, the core kept dim
    {"name": "campfire-teepee", "kind": "gltf", "asset": "../../assets/strange-attractor/hero/campfire/campfire-v1.glb",
     "position": [CAMP[0], CAMP_Y - 0.08, CAMP[1]], "rotation": [0, 20, 0], "scale": [0.85, 0.85, 0.85], "visible": False,
     "animation": {"state": "burn", "cullDistance": 800.0, "speed": 1.0}},
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
        "colorStart": [1.0, 0.72, 0.28, 0.35], "colorEnd": [1.0, 0.3, 0.03, 0.0], "emissive": 1.2, "blend": "additive",
        "velocityStretch": 0.8, "stretchMax": 1.4}},
    {"name": "campfire-embers", "kind": "particles", "visible": False, "particles": {
        "enabled": True, "capacity": 600, "seed": 37, "shape": "sphere", "position": [fire[0], fire[1] + 0.3, fire[2]],
        "extent": [0.25, 0.1, 0.25], "direction": [0, 1, 0], "spawnRate": 30.0, "lifetimeMin": 0.6, "lifetimeMax": 1.6,
        "spread": 0.3, "speedMin": 0.8, "speedMax": 1.8, "gravity": [0.1, 0.2, 0.05], "drag": 0.6, "turbulence": 0.9,
        "turbulenceScale": 0.5, "sizeStart": 0.03, "sizeEnd": 0.008, "colorStart": [1.0, 0.7, 0.25, 1.0],
        "colorEnd": [1.0, 0.15, 0.0, 0.0], "emissive": 16.0, "blend": "additive"}},
    {"name": "campfire-smoke", "kind": "particles", "visible": False, "particles": {
        "enabled": True, "capacity": 700, "seed": 41, "shape": "sphere", "position": [fire[0], fire[1] + 1.1, fire[2]],
        # M3e (owner: a thin column, not a haze cone): a narrow source, little spread and drift, small puffs
        "extent": [0.08, 0.1, 0.08], "direction": [0, 1, 0], "spawnRate": 30.0, "lifetimeMin": 5.0, "lifetimeMax": 8.0,
        "spread": 0.06, "speedMin": 0.6, "speedMax": 0.9, "gravity": [0.1, 0.3, 0.04], "drag": 0.6, "turbulence": 0.25,
        "turbulenceScale": 0.4, "sizeStart": 0.12, "sizeEnd": 0.75, "colorStart": [0.9, 0.62, 0.42, 0.07],
        "colorEnd": [0.45, 0.42, 0.48, 0.0], "emissive": 0.35, "blend": "alpha"}},
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
    "lights/campfire/intensity": 30.0, "lights/fire-bounce/intensity": 4.0,
    # the contour lines off at dusk and night (M1b quieted them, M2 removes them: the encounter owns the frame; ADR-1894)
    "attractorWorld/contourGain": 0.0,
    "nodes/sasquatch-a/visible": True, "nodes/sasquatch-b/visible": True, "nodes/sasquatch-c/visible": True,
    "nodes/campfire-logs/visible": True, "nodes/campfire-teepee/visible": True, "nodes/campfire-core/visible": True,
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
    # M3d (45-pass2-m3d-plan.md): the moon a hard, cool key-rim from behind the group, lower; less fill everywhere so
    # darkness frames the group; the fire the warm key with a short, strong reach
    "attractorWorld/sunAzimuth": 150.0, "attractorWorld/sunElevation": 24.0, "attractorWorld/sunIntensity": 0.3, "attractorWorld/sunWarmth": 0.0,
    "attractorWorld/contourGain": 0.0,
    "env/intensity": 0.035,
    "env/sky/zenithColor": [0.004, 0.007, 0.028], "env/sky/horizonColor": [0.035, 0.05, 0.12],
    "env/sky/groundColor": [0.003, 0.003, 0.01], "env/sky/haze": 0.1, "env/sky/sunColor": [0.62, 0.72, 1.0],
    "env/sky/sunIntensity": 0.6, "env/sky/sunSize": 0.03, "env/sky/sunGlow": 0.2,
    "scene/fogColor": [0.008, 0.01, 0.03],
    "lights/canyon-bounce/intensity": 0.0, "lights/floor-bounce/color": [0.35, 0.42, 0.7],
    "lights/floor-bounce/intensity": 0.012, "lights/dusk-sky/intensity": 0.02, "lights/dusk-bounce/intensity": 0.0,
    # M3e (48-night-lighting-research.md §2 A1-A6; owner §9): AgX and a stop up (ACES's toe crushed the night to black);
    # more moon fill and rim so shadows hold detail; the craft's spill tamed and kept under the hull; the fire's bounce;
    # a little haze the fire lights; the moon's shadow over a short range (finer texels), a small bias (the 3.2 m day
    # offset smeared the figures into the bank's "capsule"; ADR-1907) and no moon contact march
    "attractorWorld/sunShadowBias": 0.2, "attractorWorld/sunContact": 0.0,
    "scene/shadowRange": 200.0, "scene/volumeDensity": 0.0007, "scene/volumeLocalLights": 1.0,
    "lights/moon-rim/intensity": 0.5, "lights/ufo-glow/intensity": 30.0, "lights/fire-bounce/intensity": 5.0,
    "lights/ufo-rim-0/intensity": 4.0, "lights/ufo-rim-1/intensity": 4.0, "lights/ufo-rim-2/intensity": 4.0,
    "lights/ufo-rim-3/intensity": 4.0, "lights/ufo-spill/intensity": 9.0,
    "lights/campfire/intensity": 34.0, "lights/campfire/color": [1.0, 0.46, 0.15], "lights/campfire/range": 17.0,
    "nodes/gray/visible": True, "nodes/ufo-parked/visible": True,
    "nodes/sasquatch-a/visible": True, "nodes/sasquatch-b/visible": True, "nodes/sasquatch-c/visible": True,
    "nodes/campfire-logs/visible": True, "nodes/campfire-teepee/visible": True, "nodes/campfire-core/visible": True,
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
    "nodes/campfire-logs/visible": True, "nodes/campfire-teepee/visible": True, "nodes/campfire-smoke/visible": True,
}
scene["looks"] = [{"name": "dusk", "from": 1, "params": dusk}, {"name": "dusk-portal", "from": 2, "params": dusk_portal},
                  {"name": "night", "from": 3, "params": night}, {"name": "night-portal", "from": 4, "params": night_portal},
                  {"name": "dawn", "from": 5, "params": dawn}]
# the portals' sizes follow the terrain
for look in scene["looks"]:
    for k_ in ("radius", "fieldNear", "fieldFar", "openNear", "openFar", "width", "height", "depth"):
        if f"corruption/{k_}" in look["params"]:
            look["params"][f"corruption/{k_}"] *= R

# --- the second leg (ADR-1895): the outer cliff behind the camp opens onto Mars (build_mars.py)
_, mars_flight, mars_aims = build_mars.write()
cliff = site(-8.0, 91.8)
into = [site(0.0, 1.0)[i] - site(0.0, 0.0)[i] for i in (0, 1)]
n_into = math.hypot(*into)
scene["portals"] = [{**GAZE, "world": "strange-attractor-mars.scene.json", "exit": build_mars.EXIT, "exitYaw": 0.0,
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
scene["portals"].append({**GAZE, "world": "strange-attractor-ocean.scene.json", "exit": build_ocean.EXIT, "exitYaw": 0.0,
                         "exitAxis": [-math.sin(sl), -math.cos(sl), 0.0], "exitUp": [-math.cos(sl), math.sin(sl), 0.0],   # heading -x (M3d)
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
                                    or r["target"].startswith("moon/nodes/earth-aura")
                                    or r["target"].startswith(("lights/ufo-", "nodes/ufo-parked"))
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
# M3f (owner §2): the craft's lights breathe with the beat -- multiplied (a look's 0 by day stays 0), from 0.8 between
# beats to 4.5x on one, falling over 0.6 s: a pulse, not a strobe (0.8-1.5 measured a 1.5% swing, 0.5-3 about 5%: the
# owner asked for it clearly readable); the hull's emissive 0.35-4.5x
for _l in ("ufo-glow", "ufo-spill", "ufo-rim-0", "ufo-rim-1", "ufo-rim-2", "ufo-rim-3"):
    routes.append(beat_route("beat.pulse", f"lights/{_l}/intensity", 0.3, 4.5, attack=30.0, decay=600.0, op="multiply"))
routes.append(beat_route("beat.pulse", "nodes/ufo-parked/emissiveBoost", 0.35, 4.5, attack=30.0, decay=600.0,
                         op="multiply"))
# M3e (owner §6): the aura keeps the halo's beat pulse; its glitch and datamosh are deliberate hits, 0 between them
routes.append(beat_route("beat.pulse", "moon/nodes/earth-aura/emissiveBoost", 0.0, 1.6, decay=360.0))
for c in ("r", "c"):   # the RGB split, on the beat
    routes.append(beat_route("beat.pulse", f"moon/nodes/earth-aura-{c}/opacity", 0.0, 0.55, decay=240.0, op="replace"))
_tear = beat_route("response.transient", "moon/nodes/earth-aura-tear/opacity", 0.0, 0.9, decay=200.0, op="replace")
_tear["chain"]["remapInMin"] = 0.45   # the tear only on the strong transients: deliberate hits, not a constant shimmer
routes.append(_tear)
routes.append(beat_route("response.onsetLow", "moon/nodes/earth-aura-trail-1/opacity", 0.0, 0.45, decay=480.0, op="replace"))
routes.append(beat_route("response.onsetLow", "moon/nodes/earth-aura-trail-2/opacity", 0.0, 0.28, decay=720.0, op="replace"))
project["routes"] = routes
# M3: the submersibles' paths and their lights, keyed in the timeline (seconds, smooth)
tracks = [t for t in project.get("timeline", {}).get("tracks", []) if not t["target"].startswith("ocean/")]
for node, pos, yaw in build_ocean.sub_tracks():
    tracks.append({"target": f"ocean/nodes/{node}/position", "keys": [{"time": t, "value": p, "interp": "smooth"} for t, p in pos]})
    tracks.append({"target": f"ocean/nodes/{node}/rotation", "keys": [{"time": t, "value": list(r), "interp": "smooth"} for t, r in yaw]})
    tracks.append({"target": f"ocean/lights/{node}-light/position",
                   "keys": [{"time": t, "value": [p[0], p[1] - 3.0, p[2]], "interp": "smooth"} for t, p in pos]})
for n, pts in build_ocean.octopus_tracks():   # M3g: the octopi
    tracks.append({"target": f"ocean/nodes/{n}/position", "keys": [{"time": t, "value": p, "interp": "linear"} for t, p in pts]})
# M3d: the ocean's animals cruise (their swim loops play on their own)
for n, _, _, pts, _ in build_ocean.creature_tracks():
    tracks.append({"target": f"ocean/nodes/{n}/position", "keys": [{"time": t, "value": p, "interp": "linear"} for t, p in pts]})
tracks.append({"target": "ocean/nodes/bay-strips/emissiveBoost",
               "keys": [{"time": t, "value": v, "interp": "linear"} for t, v in build_ocean.STRIPS_DIE]})
# M3b: the rover drives, its tracks appear, its camera flashes (M3c: the lasers are gone)
tracks = [t for t in tracks if not (t["target"].startswith("moon/nodes/laser") or t["target"].startswith("mars/")
                                    or t["target"].startswith("moon/nodes/earth-halo"))]
# M3g (owner 54- §2: contrast steps at the Mars and ocean crossings): the cause was the night look -- from crossing 3,
# the step INTO Mars -- switching the frame's tone map (ACES -> AgX) and exposure (+1 EV), and the dawn look switching
# them back at crossing 5, the step into the ocean. The whole film is AgX now (the scene's post), and the exposure
# is a timeline track that eases across each of those crossings over 2 s instead of stepping on one frame
X_MARS, X_OCEAN = 69.655, 121.217
tracks = [t for t in tracks if t["target"] != "camera/exposure/compensation"]
tracks.append({"target": "camera/exposure/compensation", "keys": [
    {"time": 0.0, "value": [0.0], "interp": "smooth"}, {"time": X_MARS - 1.0, "value": [0.0], "interp": "smooth"},
    {"time": X_MARS + 1.0, "value": [1.0], "interp": "smooth"}, {"time": X_OCEAN - 1.0, "value": [1.0], "interp": "smooth"},
    {"time": X_OCEAN + 1.0, "value": [0.0], "interp": "smooth"}]})
tracks += build_mars.rover_tracks()
project["timeline"] = {"enabled": True, "tracks": tracks, "cues": project.get("timeline", {}).get("cues", [])}
json.dump(project, open(PROJECT, "w"), indent=2)
print(PATH, PROJECT)

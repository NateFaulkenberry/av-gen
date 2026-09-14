#!/usr/bin/env python3
"""Generate Glowmere Valley 2's world, scene and project from their authored description.

The scene file is 40 kB of JSON with 13 nodes, 13 scatter layers and a 15-feature world map, and
almost all of it is inherited unchanged from the painterly Glowmere it succeeds. What is *authored*
about Glowmere Valley 2 is this file: the river's centreline, the features cut against it, the
terrain settings re-derived for a 640 m map, the three primitive heroes that leave, and where the
remaining heroes stand.

Run it from the repository root:

    python3 tools/make_glowmere_valley_2.py

It rewrites examples/world/glowmere-valley-2.{scene.json,json} from
examples/world/glowmere-stylized.{scene.json,json}. It is idempotent and it never writes to the
scene it reads from -- the original Glowmere Valley is not this script's to change.

Every hardcoded Y below is a ground height the probe in tests/unit/test_glowmere_valley_2.cpp
reported at that XZ (`avgen_tests "[.probe][glowmere2]"`). Re-run the probe after changing the
world, because moving the ground moves everything standing on it.
"""
import collections
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

# The river: authored as a descending centreline whose first and last points lie OUTSIDE the map.
# That is how the traversal property is guaranteed by construction rather than by luck -- the channel
# crosses both boundaries instead of stopping at them, so there is no edge at which it can end.
# (x, water-surface level, z). Monotone descending in y, by 39 m over ~700 m of course.
RIVER = [
    (-44.0,  15.0, -352.0),
    (-58.0,  12.8, -286.0),
    (-33.0,  10.7, -220.0),
    (  6.0,   8.6, -154.0),
    ( 27.0,   6.4,  -92.0),
    (  5.0,   4.3,  -34.0),
    (-32.0,   2.2,   20.0),
    (-41.0,   0.1,   76.0),
    (-13.0,  -2.0,  132.0),
    ( 29.0,  -4.2,  188.0),
    ( 45.0,  -6.4,  244.0),
    ( 24.0,  -8.6,  300.0),
    ( 11.0, -11.0,  352.0),
]

def offset_line(pts, dx, dy):
    """The valley walls follow the river's trend without copying its wiggle: a wall that meandered
    with the channel would read as a corridor of constant width, which is the one thing a real
    valley never is."""
    out = []
    n = len(pts)
    for i, (x, y, z) in enumerate(pts):
        t = i / (n - 1)
        # a slow lateral drift so the corridor opens and narrows along its length
        widen = 1.0 + 0.35 * (0.5 - abs(t - 0.5)) * 2.0
        out.append((round(x * 0.35 + dx * widen, 1), round(y + dy, 1), z))
    return out

def pts(seq):
    return [[float(x), float(y), float(z)] for (x, y, z) in seq]

world = collections.OrderedDict()
world["name"] = "glowmere-valley-2"
world["seed"] = 20260914
world["size"] = [640.0, 640.0]
# The river's course runs from +15 down to -11, and the noise stack spans about +-14, so the water
# sits inside the range the ground already occupies. Getting this wrong is what produced the first
# authoring's defect: a river descending 36 m over ground that did not descend at all left 1,607
# sampled points of hillside below their own water table.
world["baseHeight"] = 4.0
# Lower than the shipped world's 0.35: erosion collects detail on crests, and this map's crests are
# authored ridges rather than noise maxima, so a high value fights the geography instead of helping it.
world["erosion"] = 0.30
world["seaLevel"] = -1000.0
world["moistureReach"] = 110.0
world["lowlandMoisture"] = 0.30
# Deliberately quieter than the shipped world's 73 m of peak-to-trough noise. The geography here is
# authored -- a valley of 30 m and walls of 54 m -- and noise that large would swamp it, which is the
# failure mode the brief names as "arbitrary noise".
world["layers"] = [
    {"frequency": 0.0026, "amplitude": 11.0, "ridged": 0.0,  "warp": 22.0},
    {"frequency": 0.0062, "amplitude": 9.0,  "ridged": 0.15, "warp": 14.0},
    {"frequency": 0.0175, "amplitude": 7.0,  "ridged": 0.70},
    {"frequency": 0.0460, "amplitude": 3.4,  "ridged": 0.35},
    {"frequency": 0.1300, "amplitude": 1.2},
    {"frequency": 0.3400, "amplitude": 0.38},
]

features = []

# The regional descent is the corridor's own flatten target, which descends with the river. An
# earlier version added a separate map-wide "gradient" feature for this and it was a mistake worth
# recording: overlapping flatteners *average by weight* (`world_map.cpp`), so a wide weak flatten
# does not underlie the local ones -- it competes with them. It pulled the valley walls 36% back
# down toward the valley floor and put the riverbank's own bench 11 m above the water.
# 1. The corridor. Cut first and widest: everything else is shaped against it.
features.append({
    "name": "valley-corridor", "kind": "valley",
    "path": pts([(x, y + 9.0, z) for (x, y, z) in RIVER]),
    # The basin is made by *flatten*, which targets a descending level, and only lightly by
    # `amplitude`, which subtracts an absolute depth from whatever was there. The first authoring of
    # this used amplitude 30 and the probe found ground 17 m BELOW the water table at (-92, 96): a
    # valley floor under its own river, which is a hole rather than a basin. An absolute cut cannot
    # know where the water is; a flatten target is stated relative to it and therefore cannot.
    # `amplitude` is zero and the basin is made entirely by `flatten`. A Valley's amplitude is an
    # absolute subtraction and its flatten is a target, and the two fight: the cut lowers the ground
    # from wherever the noise left it, and the flatten then drags only part of the way back. At
    # amplitude 11 the probe still found ground below the river's own water table 100 m out, which
    # would have told Phase 3's habitat field that a hundred metres of hillside was riverbank. A
    # target cannot undershoot the thing it is stated relative to; a subtraction has no idea where
    # the water is.
    "width": 300.0, "amplitude": 0.0, "falloff": 0.62,
    "flatten": 0.55,
    # The term the research called roughness(|d|), and it already existed: `roughness` is mixed by
    # the feature's own smoothstep falloff, so noise fades out toward the channel continuously.
    "roughness": 0.62,
    "smoothing": 3,
})

# 2. The river itself.
features.append({
    "name": "glowmere-run-2", "kind": "river",
    "path": pts(RIVER),
    "width": 26.0, "amplitude": 3.6, "falloff": 0.85,
    "flatten": 0.85,
    # Near-glassy in the channel. A riverbed carrying the full noise stack is what makes water
    # render as disconnected puddles.
    "roughness": 0.10,
    "water": True, "waterDepth": 0.0,
    "smoothing": 3,
})

# 3. The banks: a narrow shelf either side, so the water meets ground rather than a wall.
features.append({
    "name": "river-banks", "kind": "flat",
    "path": pts([(x, y + 1.8, z) for (x, y, z) in RIVER]),
    "width": 90.0, "amplitude": 0.0, "falloff": 0.55,
    "flatten": 0.80, "roughness": 0.45, "smoothing": 3,
})

# 4. The walls.
features.append({
    "name": "west-wall", "kind": "ridge",
    "path": pts(offset_line(RIVER, -196.0, 0.0)),
    "width": 158.0, "amplitude": 54.0, "falloff": 1.15, "roughness": 1.0, "smoothing": 3,
})
features.append({
    "name": "east-wall", "kind": "ridge",
    "path": pts(offset_line(RIVER, 205.0, 0.0)),
    "width": 150.0, "amplitude": 47.0, "falloff": 1.05, "roughness": 1.0, "smoothing": 3,
})
# Deliberately asymmetric: two walls of equal height and equal distance read as a canal.
features.append({
    "name": "west-spur", "kind": "ridge",
    "path": pts([(-150.0, 0.0, -96.0), (-104.0, 0.0, -48.0), (-78.0, 0.0, 12.0)]),
    "width": 74.0, "amplitude": 26.0, "falloff": 1.3, "roughness": 1.0, "smoothing": 3,
})

# 5. The far rim, which is what the sky sits against.
features.append({
    "name": "northern-rim", "kind": "ridge",
    "path": pts([(-330.0, 0.0, -300.0), (-120.0, 0.0, -332.0), (90.0, 0.0, -318.0), (330.0, 0.0, -296.0)]),
    "width": 150.0, "amplitude": 62.0, "falloff": 1.2, "roughness": 1.0, "smoothing": 3,
})

# 5b. The rest of the perimeter. A finite map seen from inside it shows its own edge as a horizon of
#     nothing, and the first render of this valley did exactly that on two sides. Ridges around the
#     rim are cheaper and more honest than pretending the world is larger: they give the sky
#     something to sit against from every viewpoint inside the valley, which is what the northern rim
#     was already doing on one side.
features.append({
    "name": "southern-rim", "kind": "ridge",
    "path": pts([(-330.0, 0.0, 306.0), (-90.0, 0.0, 332.0), (140.0, 0.0, 330.0), (330.0, 0.0, 300.0)]),
    "width": 132.0, "amplitude": 44.0, "falloff": 1.25, "roughness": 1.0, "smoothing": 3,
})
features.append({
    "name": "west-rim", "kind": "ridge",
    "path": pts([(-322.0, 0.0, -300.0), (-336.0, 0.0, -40.0), (-324.0, 0.0, 290.0)]),
    "width": 120.0, "amplitude": 40.0, "falloff": 1.3, "roughness": 1.0, "smoothing": 3,
})
features.append({
    "name": "east-rim", "kind": "ridge",
    "path": pts([(326.0, 0.0, -300.0), (338.0, 0.0, 20.0), (320.0, 0.0, 290.0)]),
    "width": 118.0, "amplitude": 36.0, "falloff": 1.3, "roughness": 1.0, "smoothing": 3,
})

# 6. Composition ledges: places a camera can stand and a hero can be staged, and the open ground the
#    brief's negative-space rule asks for.
features.append({
    "name": "the-hollow", "kind": "flat",
    "path": pts([(-74.0, 6.6, -18.0)]),
    "width": 52.0, "amplitude": 0.0, "falloff": 0.8, "flatten": 0.72, "roughness": 0.34, "smoothing": 0,
})
features.append({
    "name": "east-terrace", "kind": "flat",
    "path": pts([(118.0, 9.0, 60.0), (150.0, 7.0, 130.0)]),
    "width": 70.0, "amplitude": 0.0, "falloff": 0.85, "flatten": 0.6, "roughness": 0.5, "smoothing": 3,
})
features.append({
    "name": "south-shelf", "kind": "flat",
    "path": pts([(-108.0, -3.5, 214.0), (-66.0, -5.2, 268.0)]),
    "width": 62.0, "amplitude": 0.0, "falloff": 0.8, "flatten": 0.58, "roughness": 0.5, "smoothing": 3,
})

# 7. A still pool where the river widens at a bend -- the brief's "local shallows, bends and pools",
#    and a mirror for the moon.
features.append({
    "name": "elder-pool", "kind": "flat",
    "path": pts([(-38.0, 1.1, 48.0)]),
    "width": 36.0, "amplitude": 0.0, "falloff": 0.9,
    "flatten": 0.8, "roughness": 0.12, "water": True, "waterDepth": 1.3, "smoothing": 0,
})

world["features"] = features
print("world: %d features, river descends %.1f -> %.1f over %d control points"
      % (len(features), RIVER[0][1], RIVER[-1][1], len(RIVER)))


SRC = 'examples/world/glowmere-stylized.scene.json'
DST = 'examples/world/glowmere-valley-2.scene.json'

d = json.load(open(SRC), object_pairs_hook=collections.OrderedDict)

d["name"] = "Glowmere Valley 2"

# ---- the three primitive heroes leave (brief 3.4) -------------------------------------------
DROP = {"monument-spire", "far-arch", "beacon-grove"}
before = len(d["nodes"])
d["nodes"] = [n for n in d["nodes"] if n.get("name") not in DROP]
d["heroes"] = [h for h in d["heroes"] if h.get("name") not in DROP]
print("heroes: dropped %d primitive nodes, %d remain" % (before - len(d["nodes"]), len(d["heroes"])))

# The wanderer's `interest` behaviour named these; leave only what still exists. It also named
# "elder", which matches no node -- that is a defect in the original and is fixed here rather than
# carried, because carrying it would be inheriting a bug knowingly.
for e in d.get("entities", []):
    for b in e.get("behaviors", []):
        if b.get("kind") == "interest":
            b["subjects"] = ["visitor", "elder-crown"]

# ---- the new geography -----------------------------------------------------------------------
terrain = next(n for n in d["nodes"] if n.get("kind") == "terrain" or "scatter" in n)
terrain["name"] = "valley"
terrain["world"] = world
ts = terrain.setdefault("terrain", collections.OrderedDict())
# Re-derived for a map whose readable content is 640 m of valley rather than a 200 m bowl. Chunk size
# up so the chunk count does not grow with the visible distance; view distance out to the far rim.
ts["chunkSize"] = 48.0
ts["resolution"] = 40
ts["lodLevels"] = 4
ts["lodDistance"] = 86.0
ts["viewDistance"] = 640.0
ts["shadowDistance"] = 165.0

# The painterly water was tuned for a 7 m stream seen edge-on and largely in shadow. Pointed down a
# 700 m course that faces the moon it is a mirror: the first render blew a white streak across a
# third of the frame. Reflection and specular come down; everything else about the look is kept.
wat = ts.setdefault("water", collections.OrderedDict())
wat["reflection"] = 2.6
wat["specular"] = 1.2
wat["fresnel"] = 0.22

# Two cascades over 165 m was the painterly scene's setting for a shot that never looked far. This
# valley is 640 m end to end and every acceptance viewpoint looks down its length.
env = d.setdefault("environment", collections.OrderedDict())
# ADR-112's rule is that the range is chosen to honour a shadow texel target of 0.08 m, and 260 m
# over three cascades at the 2048 reference puts the coarsest texel at ~0.25 m -- three times over.
# The symptom was unmistakable once the cause was: the near field went black to a sharp boundary at
# a fixed distance from the camera, on both the stylized and the PBR paths, which is a shadow range
# failing rather than a material. The painterly scene's 140 m is the number that works on this
# content; three cascades instead of two is the part of the change worth keeping.
env["shadowCascades"] = 3
ts["shadowDistance"] = 150.0

# The ground reads off the biome axis through a three-stop ramp, and Glowmere Valley 2's valley floor
# sits at the bottom of that axis where the painterly ramp's darkest stop is [0.021, 0.063, 0.076] --
# near black. In the original that stop was almost never the ground you looked at; here it is most of
# the frame, and the first render had a black foreground under lit vegetation.
#
# So Glowmere Valley 2 gets its own ground program with the ramp shifted one stop up, rather than the
# shared one being edited: `paintedGround` belongs to the scene this one succeeds, and changing it
# would change that scene's ground too.
progs = d.setdefault("materialPrograms", [])
GROUND2 = "../materials/glowmere2-painted-ground.material.json"
if GROUND2 not in progs:
    progs.append(GROUND2)
terrain.setdefault("material", collections.OrderedDict())["program"] = "paintedGround2"

# ---- Phase 3: the riparian ladder ---------------------------------------------------------------
#
# Every layer is given a band on **height above the water table** -- the one habitat axis that is a
# property of a point relative to the geography rather than of the point on its own. Bair et al.
# (Ecosphere 2021) relate ground height above river to observed cover types and find the boundaries
# fall where adjacent types differ by about half a metre near the channel; the ladder below is that
# idea adapted, not that paper's numbers transplanted.
#
# The bands do two jobs at once, which is why they are the whole of Phase 3's composition work and
# most of its performance work. They *structure* the valley -- a wet floor, a transitional slope, an
# upland of silhouette species -- and they *remove* instances from the places the camera looks
# across rather than at, which is where a 640 m map spends coverage it gets nothing for.
#
# (min, max, feather). A layer absent from this table is unconstrained on purpose: rock is rock.
HAR_BANDS = {
    "grass":       (0.2, 11.0, 2.5),   # the valley floor's carpet, gone from the upper walls
    "ferns":       (0.4,  8.5, 1.8),   # moist, low, near the corridor
    "flowers":     (0.3,  5.0, 1.2),   # a riparian accent and nothing else
    "fungi":       (0.1,  6.0, 1.2),   # damp and shaded
    "shelf-fungi": (0.8, 11.0, 2.0),
    "fan-plants":  (0.4,  7.5, 1.6),   # the big fronds: foreground framing, floor only
    "bushes":      (1.5, 24.0, 3.5),   # the transitional band, deliberately the widest
    "canopy":      (2.5, 42.0, 5.0),   # woodland on the slopes, not standing in the river
    "deadwood":    (4.0, 46.0, 5.0),
    "pines":       (11.0, 95.0, 7.0),  # the upland silhouette, and the reason the walls read
    "beacons":     (0.4,  9.0, 1.5),   # landmarks in the corridor, where the camera travels
    "pebbles":     (-1.5, 4.0, 1.0),   # bank gravel
}

# What a layer costs is what it covers, not how many there are (ADR-126, ADR-151). These are the two
# knobs that remove coverage nobody sees: a screen radius below which an instance is not worth a
# draw, and a distance past which a small thing is not worth anything at all. Both were authored for
# a 200 m bowl and this valley is 640 m.
BUDGET = {
    # layer:        (minScreenRadius, viewDistance)
    "grass":        (2.4, 72.0),
    "ferns":        (2.6, 130.0),
    "pebbles":      (4.0, 42.0),
    "flowers":      (6.0, 120.0),
    "fungi":        (6.0, 130.0),
    "fan-plants":   (3.2, 175.0),
    "bushes":       (6.0, 165.0),
    "shelf-fungi":  (6.0, 150.0),
    "deadwood":     (1.6, 380.0),
    "boulders":     (2.4, 260.0),
}

for layer in terrain.get("scatter", []):
    band = HAR_BANDS.get(layer.get("name"))
    if band is not None:
        layer["minHeightAboveWater"] = band[0]
        layer["maxHeightAboveWater"] = band[1]
        layer["heightAboveWaterFeather"] = band[2]
    budget = BUDGET.get(layer.get("name"))
    if budget is not None:
        layer["minScreenRadius"] = budget[0]
        layer["viewDistance"] = budget[1]

# The river dressing has to name the new course.
for n in d["nodes"]:
    fl = n.get("float")
    if isinstance(fl, dict) and fl.get("body") == "glowmere-run":
        fl["body"] = "glowmere-run-2"
    p = n.get("particles")
    if isinstance(p, dict) and p.get("spline") == "valley.glowmere-run":
        p["spline"] = "valley.glowmere-run-2"
    if n.get("name") == "tarn-lilies":
        f2 = n.get("float")
        if isinstance(f2, dict):
            f2["body"] = "elder-pool"

json.dump(d, open(DST, 'w'), indent=1)
print("wrote", DST, "nodes:", len(d["nodes"]), "heroes:", [h["name"] for h in d["heroes"]])

# ---- staging ----------------------------------------------------------------------------------
# Every Y below is the ground height the probe reported at that XZ, so nothing floats and nothing is
# buried. They are baked rather than derived because a scene node carries an explicit transform;
# the probe test that produced them is committed alongside, so they can be re-derived.
ELDER = (-12.0, 3.4, 52.0)           # ground 3.66, on the pool's east bank; HAR 2.5, riparian
STEM_TO_CROWN = 14.5                 # the elder's stem length, preserved from the original
FGLEAF = (-92.0, 18.3, -58.0)        # ground 18.56, just in front of the opening camera
WANDER = (-74.0, 7.62, -18.0)        # ground 7.62, the hollow: dead flat, open, HAR 5.0
UFO    = (20.0, 29.5, 150.0)         # ground -2.33, 32 m up over the lower valley
CAM_EYE = (-118.0, 32.1, -96.0)      # ground 27.14, on the west shoulder, 5 m up
CAM_TGT = (-20.0, 4.0, 40.0)         # the bend by the elder, so the river leads the eye into frame

# ---- Phase 3: negative space -------------------------------------------------------------------
#
# The brief's 4.5 asks for open ground as a design feature, and the first render of this valley showed
# why it is not decoration: the opening camera stood inside a wood and the valley was glimpsed between
# trunks. Big near geometry is also, per ADR-126, where this renderer's frame actually goes -- coverage
# costs, triangles do not -- so the composition fix and the budget fix are the same edit.
#
# `ScatterClearance::minHeight` is the mechanism and it is the right one: it clears the *canopy* and
# leaves the ground growing, which is a glade rather than a bald patch. Clearing everything produced
# "a bald hillside with one tree on it" when the composer first tried it, and that comment is in the
# header for a reason.
#
# A clearing is placed where the camera stands, where a hero is staged, and along the water -- the
# three places a viewer's attention actually goes.
def glade(x, z, radius, softness, min_height, strength=1.0):
    return collections.OrderedDict([
        ("center", [float(x), float(z)]), ("radius", float(radius)),
        ("softness", float(softness)), ("strength", float(strength)),
        ("minHeight", float(min_height)),
    ])

clearings = [
    # The opening camera's glade: it stands in one and looks across the valley out of it.
    glade(CAM_EYE[0], CAM_EYE[2], 46.0, 34.0, 5.5),
    # The hollow the Wanderer walks, and the elder's own ground -- a hero needs room to have a
    # silhouette, and a 16 m mushroom behind a 14 m tree is not a hero.
    glade(WANDER[0], WANDER[2], 34.0, 24.0, 5.0),
    glade(ELDER[0], ELDER[2], 30.0, 22.0, 4.0),
]
# The river corridor: a lane in the canopy along the whole course, so the water is visible from the
# valley floor and the auto-director has somewhere continuous to fly. Taken from the same centreline
# the river is cut from, every other control point, so the lane meanders with it.
for cx, _cy, cz in RIVER[1:-1:2]:
    clearings.append(glade(cx, cz, 38.0, 26.0, 6.0))
# One deliberate meadow on the open valley floor: somewhere with nothing in it at all, which is what
# gives the rest of the frame something to be dense against.
clearings.append(glade(-58.0, 150.0, 44.0, 30.0, 2.2))
terrain["clearings"] = clearings

def setpos(name, xyz):
    for n in d["nodes"]:
        if n.get("name") == name:
            n["position"] = [float(v) for v in xyz]
            return True
    return False

setpos("elder-stem", ELDER)
setpos("elder-crown", (ELDER[0], ELDER[1] + STEM_TO_CROWN, ELDER[2]))
setpos("elder-filaments", (ELDER[0], ELDER[1] + STEM_TO_CROWN, ELDER[2]))
setpos("foreground-leaves", FGLEAF)
setpos("wanderer", WANDER)
setpos("visitor", UFO)

# The spore volume follows the valley rather than sitting where the old one did.
for n in d["nodes"]:
    if n.get("name") == "spores":
        n.setdefault("particles", {})["position"] = [-20.0, 8.0, 20.0]

for h in d["heroes"]:
    if h["name"] == "elder-crown":
        h["position"] = [ELDER[0], ELDER[1], ELDER[2]]
    if h["name"] == "visitor":
        h["position"] = [UFO[0], UFO[1] - 1.45, UFO[2]]

d["camera"] = collections.OrderedDict([
    ("mode", 1), ("position", list(CAM_EYE)), ("target", list(CAM_TGT)),
    ("fov", 40.0), ("orbitSpeed", 0.0),
])

json.dump(d, open(DST, 'w'), indent=1)
print("staged; camera", CAM_EYE, "->", CAM_TGT)

# ---- the project ------------------------------------------------------------------------------
PSRC = 'examples/world/glowmere-stylized.json'
PDST = 'examples/world/glowmere-valley-2.json'
proj = json.load(open(PSRC), object_pairs_hook=collections.OrderedDict)
proj["assets"]["scene"]["path"] = "glowmere-valley-2.scene.json"
# The recorded hash and size described the *other* scene file. Leaving them would be carrying a
# stale fingerprint; they are only consulted when the file is missing, but a wrong one is worse than
# none because it would relink to something else.
for k in ("sha256", "size"):
    proj["assets"]["scene"].pop(k, None)

params = proj["parameters"]
# Parameters for nodes that no longer exist would sit unbound forever.
for name in DROP:
    for k in [k for k in params if k.startswith("nodes/%s/" % name)
              or k.startswith("procedural/%s/" % name)
              or k.startswith("material/%s/" % name)]:
        params.pop(k)
# The project's parameter block is authoritative over the scene's transforms -- `applyParameters`
# rewrites them every frame -- so a staged scene whose project still carried the old positions would
# put every hero back where it was. This is the half of staging that is easy to forget.
params["nodes/elder-stem/position"] = list(ELDER)
params["nodes/elder-crown/position"] = [ELDER[0], ELDER[1] + STEM_TO_CROWN, ELDER[2]]
params["nodes/elder-filaments/position"] = [ELDER[0], ELDER[1] + STEM_TO_CROWN, ELDER[2]]
params["nodes/foreground-leaves/position"] = list(FGLEAF)
params["nodes/spores/position"] = [-20.0, 8.0, 20.0]
params["camera/position"] = list(CAM_EYE)
params["camera/target"] = list(CAM_TGT)
params["camera/mode"] = 1

# Routes that named a removed node would bind to nothing. Unresolved routes stay enabled but inert
# by design, so this is tidiness rather than a fix -- but an inert route is a lie in the UI.
before = len(proj["routes"])
proj["routes"] = [r for r in proj["routes"]
                  if not any(("/%s/" % n) in r.get("target", "") for n in DROP)]
print("routes", before, "->", len(proj["routes"]), "; params", len(params))
json.dump(proj, open(PDST, 'w'), indent=1)
print("wrote", PDST)

print('done.')

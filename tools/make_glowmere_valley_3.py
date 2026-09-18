#!/usr/bin/env python3
"""Generate Glowmere Valley 3 -- the autonomous-character showcase -- scene and project.

    python3 tools/make_glowmere_valley_3.py

## This file is the source of truth, and that is the point

`examples/world/glowmere-valley-3.{scene.json,json}` are **outputs**. Nothing here reads them, so
there is no rebase branch, no "which half is mine", and no way for a saved session to survive a
regeneration. That is deliberate and it is the whole hygiene argument of ADR-338:

* `tools/make_glowmere_valley_2.py` re-reads its own project (`REBASE = os.path.exists(PDST)`)
  because by the time it was written the project had become edited state -- 3,063 parameters where
  the generator had written 585. That was the correct call *there* and it is exactly the condition
  that made valley-2 impossible to clean with confidence: a file whose two halves cannot be told
  apart.
* ADR-264 needed `git log -S` over a 7,114-line whole-file re-save to decide whether a beam scale
  was authored or residue. ADR-271 then found that two agents had got that judgement wrong twice,
  on sound reasoning, because *from inside the project file the number is indistinguishable from
  residue*.

So valley 3's project carries only what a project must: the scene reference and its fingerprint,
the render settings, and a handful of parameters that are **stated here**. Everything else is the
scene's, and the scene is generated from this file.

## What is carried forward, and what is rebuilt

Carried, byte for byte, out of `glowmere-valley-2.scene.json`:

    the terrain node's `world` (seed, layers, features) and `terrain` settings and `material`
    the ten hero fungi (40 procedural nodes) and their ten spore emitters
    the river dressing: lilies, petals, motes, the spore volume, the foreground leaves
    `environment`, `post`, `wind`, `lightRig`, `materialPrograms`, `worldEffects`
    the ten hero *points* (`heroes[]`)

Authored here, not carried:

    two new world features -- a ford and a causeway (see THE CROSSING below)
    the scatter layers (see THE TREES below)
    the cast: five autonomous characters, their considerers and their pose layers
    the cameras and the shot list
    `composition.focalPoints`
    the whole project

Dropped, deliberately (ADR-338 names each one):

    the saucer `visitor`, its `visitor-beam`, and the `staging` block that runs the abduction
    the sixteen farm animals
    `cameraDirection` from the multicam film, `sequence`, `songPlan`, `timeline`, `routes`,
    `cameraShotSpans`, `cameraAimFollow`, `autoDirector`, `atmosphericEffects`
"""
import collections
import hashlib
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

SRC = "examples/world/glowmere-valley-2.scene.json"
SCENE = "examples/world/glowmere-valley-3.scene.json"
PROJECT = "examples/world/glowmere-valley-3.json"

OD = collections.OrderedDict


def load(path):
    return json.load(open(path), object_pairs_hook=OD)


d = load(SRC)
d["name"] = "Glowmere Valley 3"

terrain = next(n for n in d["nodes"] if n.get("kind") == "terrain")

# =================================================================================================
# 1. THE CROSSING
# =================================================================================================
#
# ADR-336 delivered `RouteConsiderer` against `examples/labs/character/river-crossing.scene.json`:
# a flat 240 m world, a 14 m river 1.40 m deep that **ends at x = -50**, and `navWadeDepth` 1.8.
# So there is a ford (the channel is shallower than the wade depth) and there is a way round (the
# river ends inside the map). Both by construction.
#
# **Glowmere has neither, and that is measured rather than assumed.**
# `tests/unit/test_glowmere_valley_3.cpp`'s river probe, on the shipped valley-2 world:
#
#     navWadeDepth 0.8536       the channel is 3.5 .. 3.65 m deep at every one of six stations
#     19 disconnected nav regions; 10,179 of 20,819 walkable cells (49%) stranded
#
# A walker that wades 0.85 m cannot cross 3.6 m of water, and `tools/make_glowmere_valley_2.py`
# authors the centreline to leave the map at both ends on purpose -- "the channel crosses both
# boundaries instead of stopping at them, so there is no edge at which it can end". So the river
# is not a crossing decision in Glowmere. It is a wall, and the nav grid says so in the region
# count.
#
# Raising `navWadeDepth` to 3.8 is the obvious fix and it is wrong: ADR-334 set it from the cast
# ("1.6 m of wade is shin-deep on a 5.67 m cow and drowns a 1.57 m one") and 3.8 m is over the head
# of a 3.48 m alien. It is also a property of the *world's* navigator, not of a character, so it
# cannot be the thing that tells two characters apart.
#
# ## What the engine will and will not let a ford be, measured the hard way
#
# The first attempt added a radial `flat` feature with `water: true, waterDepth: 0.46` over the
# channel, on the model of `elder-pool`. It made the river **deeper** -- 3.60 m to 4.07 m -- and
# `world_map.cpp` says exactly why:
#
#     cutTarget = min(cutTarget, hit.level - amplitude)    over every water feature that reaches
#     cutWeight = max(cutWeight, w)
#     waterSurface = max(hit.level + waterDepth)           over every water feature that reaches
#
# The bed is a **min** and the surface is a **max**, so a second water feature laid over a river
# can only ever deepen it. Raising the bed is not something a feature can do to another feature's
# channel; and the flatten cannot do it either, because the cut is applied *after* the flatten.
#
# The consequence is structural and it decides the geography below: **a shallow reach of a river
# is a shallow *segment of the river feature itself*, and it only reads as shallow where no deep
# segment reaches it at all** -- `featureWeight` is zero at and beyond `width`, so the two
# neighbouring deep segments must end 26 m away on each side.
#
# So valley 3 authors:
#
#   the river in three segments      `glowmere-run-3-upper`, `the-ford` (amplitude 0.50 instead of
#                                    3.60, over z in [-50, 50]), `glowmere-run-3-lower`. The ford
#                                    reads as shallow over about 48 m of the course, which is one
#                                    riffle, and it is the only place the main channel can be
#                                    crossed on foot.
#   `the-backwater`                  a shallow flood channel on the west floodplain, 0.55 m deep,
#                                    **with both ends inside the map**. This is the crossing
#                                    *decision*: the way from the cast's ground to the ford either
#                                    goes straight through it or round its end.
#
# The main channel is still a wall everywhere else, and that is correct for a 26 m river 3.6 m
# deep. The ford is what stops the valley being two worlds: the nav grid reports 19 disconnected
# regions in valley 2 with 49% of the walkable ground stranded, and the ford is the edge that
# joins the halves.

RIVER = [
    (-44.0, 15.0, -352.0), (-58.0, 12.8, -286.0), (-33.0, 10.7, -220.0), (6.0, 8.6, -154.0),
    (27.0, 6.4, -92.0), (5.0, 4.3, -34.0), (-32.0, 2.2, 20.0), (-41.0, 0.1, 76.0),
    (-13.0, -2.0, 132.0), (29.0, -4.2, 188.0), (45.0, -6.4, 244.0), (24.0, -8.6, 300.0),
    (11.0, -11.0, 352.0),
]


def on_river(z):
    """The channel's centre and water level at a given z, by linear interpolation of RIVER."""
    for (x0, y0, z0), (x1, y1, z1) in zip(RIVER, RIVER[1:]):
        if z0 <= z <= z1:
            t = (z - z0) / (z1 - z0)
            return x0 + t * (x1 - x0), y0 + t * (y1 - y0)
    raise ValueError("z outside the river's course")


def river_span(z_lo, z_hi):
    """The centreline between two z, with interpolated endpoints, as scene-file triples."""
    out = [[round(on_river(z_lo)[0], 2), round(on_river(z_lo)[1], 2), float(z_lo)]]
    out += [[x, y, z] for (x, y, z) in RIVER if z_lo < z < z_hi]
    out.append([round(on_river(z_hi)[0], 2), round(on_river(z_hi)[1], 2), float(z_hi)])
    return out


FORD_Z0, FORD_Z1 = -50.0, 50.0
FORD_Z = 0.0
FORD_X, FORD_LEVEL = on_river(FORD_Z)

# The river's own numbers, unchanged: width 26, falloff 0.85, flatten 0.85, roughness 0.10,
# smoothing 3. Only the segment boundaries and the ford's `amplitude` are new.
def run(name, z_lo, z_hi, amplitude):
    return OD([
        ("name", name), ("kind", "river"),
        ("path", river_span(z_lo, z_hi) if z_lo is not None else [list(p) for p in RIVER]),
        ("width", 26.0), ("amplitude", amplitude), ("falloff", 0.85), ("flatten", 0.85),
        ("roughness", 0.10), ("water", True), ("waterDepth", 0.0), ("smoothing", 3),
    ])


def full_span(z_lo, z_hi):
    return run(None, z_lo, z_hi, 0.0)


UPPER = run("glowmere-run-3-upper", RIVER[0][2], FORD_Z0, 3.6)
# 0.50 m of water over a 26 m channel, against `navWadeDepth` 0.8536. Under the wade depth or
# there is no ford at all; far enough under it that one sample of bed noise cannot close it; and
# far enough above zero that the crossing is visibly, and expensively, wet.
FORD = run("the-ford", FORD_Z0, FORD_Z1, 0.50)
LOWER = run("glowmere-run-3-lower", FORD_Z1, RIVER[-1][2], 3.6)

# The flood channel, and it is on the **east** floodplain rather than the west.
#
# Not a preference: the west floodplain is where everything already stands. The first siting put a
# 30 m slough through (-46, -28) and the staging probe reported the `lantern` hero fungus standing
# in 0.66 m of water. The east side between z = -130 and z = +50 is the largest piece of flat,
# empty, dry ground in the valley -- 8 to 10.5 m, slope under 0.01 at every station -- and the
# nearest staged thing to this course is the `spire` fungus, 33 m off.
#
# The shape is a crescent with **both ends open inside the map**, which is the one property the
# main river cannot have and the one property ADR-336's fixture turns on. Its ends are long enough
# that going round is a real alternative rather than a formality: measured, the direct way is
# 92 m and the shortest way round is 196 m.
#
# Levels descend along the path (a water course whose levels do not is a river running uphill and
# `WorldMap::validate` refuses it) and each sits 0.2 to 0.7 m under the local ground the staging
# probe read, so the slough lies in a hollow of the plain instead of on top of it.
#
# 0.70 m of water against `navWadeDepth` 0.8536: deep enough that a wet metre is nearly a full
# one on the grid's 0..1 `wade` scale, shallow enough to stay walkable.
BACKWATER_PATH = [(108.0, 9.90, -120.0), (96.0, 9.75, -86.0), (78.0, 9.40, -30.0),
                  (66.0, 8.10, 8.0), (56.0, 7.30, 40.0)]
BACKWATER = OD([
    ("name", "the-backwater"), ("kind", "river"),
    ("path", [[x, y, z] for (x, y, z) in BACKWATER_PATH]),
    ("width", 30.0), ("amplitude", 0.70), ("falloff", 0.9), ("flatten", 0.9),
    ("roughness", 0.16), ("water", True), ("waterDepth", 0.0), ("smoothing", 3),
])

# The river feature is replaced rather than added to, so there is exactly one authority on where
# the channel is. `glowmere-run-2` is gone from this world and the two dressing nodes that named
# it are repointed below -- a float body or a particle spline naming a feature nothing carries
# binds to nothing and says so only in a log line.
features = [f for f in terrain["world"]["features"] if f.get("name") != "glowmere-run-2"]
assert len(features) == len(terrain["world"]["features"]) - 1, "the source scene's river moved"
river_at = next(i for i, f in enumerate(terrain["world"]["features"])
                if f.get("name") == "glowmere-run-2")
features[river_at:river_at] = [UPPER, FORD, LOWER]
features.append(BACKWATER)
terrain["world"]["features"] = features

# `river-lilies` drifts on a named body and `river-motes` follows a named spline. Both named the
# one feature that no longer exists. The lower run is the longer of the two deep reaches and the
# one four of the five cameras look along, so it takes the dressing, and the ford gets its own
# lily bed -- a shallow reach is exactly where water plants grow.
for n in d["nodes"]:
    fl = n.get("float")
    if isinstance(fl, dict) and fl.get("body") == "glowmere-run-2":
        fl["body"] = "glowmere-run-3-lower"
    p = n.get("particles")
    if isinstance(p, dict) and p.get("spline") == "valley.glowmere-run-2":
        p["spline"] = "valley.glowmere-run-3-lower"
ford_lilies = OD([
    ("name", "ford-lilies"), ("kind", "procedural"),
    ("position", [round(FORD_X, 1), round(FORD_LEVEL, 2), FORD_Z]),
])
_source_lilies = next(n for n in d["nodes"] if n["name"] == "river-lilies")
ford_lilies["procedural"] = json.loads(json.dumps(_source_lilies["procedural"]),
                                       object_pairs_hook=OD)
ford_lilies["float"] = json.loads(json.dumps(_source_lilies["float"]), object_pairs_hook=OD)
ford_lilies["float"]["body"] = "the-ford"
ford_lilies["float"]["count"] = 60
ford_lilies["float"]["seed"] = 9104
ford_lilies["float"]["minDepth"] = 0.12
ford_lilies["visible"] = True
d["nodes"].append(ford_lilies)

terrain["world"]["name"] = "glowmere-valley-3"

# =================================================================================================
# 2. THE TREES
# =================================================================================================
#
# > "have the agent make the hills more densely populated with variety of trees available"
#
# ## What actually empties the hills, measured before anything was changed
#
# `tests/unit/test_glowmere_valley_3.cpp`, 240x240 dry samples of the shipped valley-2 world:
#
#     the hills (above mid-height) are 33% of dry ground, and they are
#       forest 49.9%   rim 29.9%   scree 20.2%
#
#     of hill ground, each tree layer is refused for:
#       canopy     slope  0.4%   height-above-water 95.1%   biome 0.0%
#       deadwood   slope  0.0%   height-above-water 89.9%   biome 0.0%
#       pines      slope  0.1%   height-above-water  3.2%   biome 3.0%
#
# Two plausible explanations are **false**, and they are recorded because both were believed:
#
#   * *"`canopy` has no scree or rim density, so it is forbidden on the hills."* It is not. Half
#     the hill ground is `forest` biome, and `canopy`'s forest density is its largest. Biome
#     refuses 0.0% of it.
#   * *"`maxSlope` 0.26-0.32 gates the tree layers off steeper ground."* It does not. This terrain's
#     slope is p90 0.149 and p99 0.283; 98.1% of all ground and 99.6% of hill ground is already
#     under 0.26. Raising `maxSlope` to 0.38 buys about one per cent of the map, and it is done
#     below for the pockets it does reach rather than because it was the gate.
#
# What empties the hills is **ADR-174's riparian ladder** -- `HAR_BANDS` in
# `tools/make_glowmere_valley_2.py`, which bands `canopy` at 2.5..42 m above the water table and
# `deadwood` at 4..46 m. The hills sit at p05 47.4 m, p50 66.8 m, p95 100.5 m above the water. The
# band ends where the hills begin, and it was authored to: the ladder's comment says it "removes
# instances from the places the camera looks across rather than at". Phase 7 of that script had
# already widened `bushes` from 24 m to 38 m for the same complaint -- "the east wall carried pines
# and nothing else, and wide shots read bare on that side".
#
# So this is a deliberate design being deliberately revisited, and the revision is aimed at the
# gate that is shut.
#
# ## The scale ladder is not negotiable (ADR-334, ADR-335)
#
# `tests/unit/test_glowmere_scale.cpp` asserts the 16 m elder stands a fifth again above the
# tallest instance any tree layer will place. The tree line is 11.2 m (8.0 x maxScale 1.4) and the
# ceiling is 16 / 1.2 = 13.33 m. **Every layer below is authored so that `height * maxScale` is at
# most 11.2**, which holds the tree line exactly where ADR-334 measured it and ADR-335 re-derived
# the cast's 1.94x from. Variety is in species, tint, clustering and scale *spread*, never in the
# ceiling.
#
# ## The layer named `pines` grew no pines, and both halves of that are fixed
#
# `pines` loaded `TwistedTree_2` while `Pine_1..5` sat unused. Renaming it alone would leave a
# world with no pines in it; adding pines alone would leave a layer lying about its species. So
# the layer is **renamed `twisted`** for what it actually grows, and two **real pine layers** are
# added, which is what makes the upland read as a treeline rather than as scrub.
#
# Eight tree layers across eight of the twenty available models, where there were three across
# three.

QUAT = "../../assets/quaternius/glTF/%s.gltf"

# The wind response the existing tree layers carry, by rough mass class. A species' motion is a
# property of what kind of tree it is, so three tables serve eight layers rather than eight copies.
MOTION_BROADLEAF = OD([("stiffness", 9.0), ("mass", 40.0), ("damping", 0.7),
                       ("windSensitivity", 1.4), ("bendLimit", 0.12), ("tipAmplitude", 0.22),
                       ("gustResponse", 0.8), ("bendCurve", 3.2), ("amplitudeVariance", 0.28)])
MOTION_CONIFER = OD([("stiffness", 12.0), ("mass", 55.0), ("damping", 0.75),
                     ("windSensitivity", 1.2), ("bendLimit", 0.1), ("tipAmplitude", 0.19),
                     ("gustResponse", 0.7), ("bendCurve", 3.5), ("amplitudeVariance", 0.25)])
MOTION_SNAG = OD([("stiffness", 20.0), ("mass", 60.0), ("damping", 0.85),
                  ("windSensitivity", 0.6), ("bendLimit", 0.06), ("tipAmplitude", 0.1),
                  ("gustResponse", 0.5), ("bendCurve", 3.6), ("amplitudeVariance", 0.2)])

# name, asset, densities, HAR (min, max, feather), maxSlope, height, minScale, maxScale,
# clusterScale, clustering, tint, seed, maxInstances, meshBudget, motion, sink, alignToGround
#
# `height * maxScale <= 11.2` for every row. The two tallest species keep 8.0 x 1.4; everything
# else is authored below the line so the canopy has a top rather than a plateau, which is what a
# mixed wood looks like from across a valley.
TREE_LAYERS = [
    # ---- the valley floor and the lower slopes: the wood the characters walk in ----------------
    ("canopy", "CommonTree_1",
     {"forest": 0.0072, "meadow": 0.0018},
     (2.5, 44.0, 5.0), 0.28, 8.0, 0.75, 1.40, 46.0, 0.55,
     [0.08, 0.28, 0.23], 31, 3200, 900, MOTION_BROADLEAF, 0.25, 0.0),
    ("canopy-broad", "CommonTree_4",
     {"forest": 0.0046, "meadow": 0.0013, "marsh": 0.0006},
     (2.0, 40.0, 4.5), 0.28, 7.1, 0.75, 1.40, 33.0, 0.62,
     [0.10, 0.31, 0.19], 131, 2400, 820, MOTION_BROADLEAF, 0.25, 0.0),
    # ---- the transition: the twisted trees, honestly named ---------------------------------------
    ("twisted", "TwistedTree_2",
     {"forest": 0.0030, "scree": 0.0026, "rim": 0.0014},
     (11.0, 100.0, 7.0), 0.32, 8.0, 0.70, 1.40, 38.0, 0.60,
     [0.12, 0.18, 0.32], 32, 2600, 760, MOTION_BROADLEAF, 0.30, 0.15),
    ("twisted-low", "TwistedTree_4",
     {"scree": 0.0022, "forest": 0.0016, "rim": 0.0012},
     (14.0, 106.0, 8.0), 0.34, 6.6, 0.70, 1.40, 27.0, 0.68,
     [0.15, 0.20, 0.30], 132, 2200, 700, MOTION_BROADLEAF, 0.30, 0.22),
    # ---- the upland: the pines this world was missing --------------------------------------------
    # The band starts at 26 m above the water -- above the corridor, below the hills' own p05 of
    # 47 m with the feather to reach it -- and runs to 118, which is past the map's highest dry
    # ground at 106.4. A band that stops short of the rim puts a bald cap on every ridge.
    ("pine-upper", "Pine_3",
     {"scree": 0.0038, "rim": 0.0030, "forest": 0.0022},
     (26.0, 118.0, 9.0), 0.36, 8.0, 0.70, 1.40, 42.0, 0.58,
     [0.07, 0.22, 0.27], 231, 3000, 820, MOTION_CONIFER, 0.30, 0.12),
    ("pine-rim", "Pine_1",
     {"rim": 0.0034, "scree": 0.0026, "forest": 0.0010},
     (34.0, 122.0, 10.0), 0.38, 6.8, 0.70, 1.40, 30.0, 0.66,
     [0.09, 0.19, 0.31], 232, 2400, 720, MOTION_CONIFER, 0.30, 0.18),
    # ---- the snags, on both halves of the ladder -------------------------------------------------
    ("deadwood", "DeadTree_1",
     {"scree": 0.0020, "forest": 0.0010, "rim": 0.0012},
     (4.0, 104.0, 8.0), 0.36, 6.5, 0.70, 1.30, 60.0, 0.40,
     [0.1946, 0.1779, 0.2789], 33, 1600, 800, MOTION_SNAG, 0.30, 0.30),
    ("deadwood-rim", "DeadTree_4",
     {"rim": 0.0018, "scree": 0.0014},
     (40.0, 124.0, 10.0), 0.40, 5.4, 0.70, 1.30, 48.0, 0.45,
     [0.2100, 0.1950, 0.2600], 133, 1400, 700, MOTION_SNAG, 0.30, 0.35),
]

TREE_NAMES = [row[0] for row in TREE_LAYERS]
TREE_LINE = max(row[5] * row[7] for row in TREE_LAYERS)
assert TREE_LINE <= 13.33, "the tree line at %.2f m would put the 16 m elder under 1.2x it" % TREE_LINE


def tree_layer(row):
    (name, asset, densities, har, max_slope, height, min_scale, max_scale, cluster_scale,
     clustering, tint, seed, max_instances, mesh_budget, motion, sink, align) = row
    layer = OD([
        ("name", name),
        ("asset", QUAT % asset),
        ("densities", OD((k, v) for k, v in densities.items())),
        ("maxSlope", max_slope),
        ("minScale", min_scale),
        ("maxScale", max_scale),
        ("sink", sink),
        ("randomYaw", 1.0),
        ("clusterScale", cluster_scale),
        ("clustering", clustering),
        ("seed", seed),
        ("meshBudget", mesh_budget),
        ("maxInstances", max_instances),
        ("height", height),
        ("tint", tint),
        ("viewDistance", 520.0),
        ("minScreenRadius", 1.0),
        ("castsShadow", True),
        # The fireflies-in-the-canopy speckle the existing tree layers carry (ADR-054/ADR-179).
        # Kept per species rather than dropped, because a wood where only one species glows reads
        # as one species with a bug in it.
        ("emissiveColor", [1.0, 0.82, 0.45]),
        ("emissiveIntensity", 0.035),
        ("emissiveRandom", 0.55),
        ("hueField", 0.05),
        ("hueFieldScale", 44.0),
        ("hueRandom", 0.035),
        ("emissiveSparsity", 0.14),
        ("materialProgram", ""),
        ("motion", motion),
        ("minHeightAboveWater", har[0]),
        ("maxHeightAboveWater", har[1]),
        ("heightAboveWaterFeather", har[2]),
    ])
    if align > 0.0:
        layer["alignToGround"] = align
    return layer


# The undergrowth is carried unchanged: it is not what the owner asked about, it is what ADR-334's
# first arm measures the cast against, and `fan-plants` at 5.12 m is the ceiling that arm uses.
UNDERGROWTH = ["bushes", "ferns", "grass", "fan-plants", "flowers", "fungi", "shelf-fungi",
               "boulders", "pebbles", "beacons"]
carried = {l["name"]: l for l in terrain["scatter"]}
missing = [n for n in UNDERGROWTH if n not in carried]
assert not missing, "the source scene no longer carries %s" % missing
terrain["scatter"] = [tree_layer(row) for row in TREE_LAYERS] + \
                     [carried[n] for n in UNDERGROWTH]

# The glades. Carried from valley 2 -- they are composition, and `minHeight` clears the canopy and
# leaves the ground growing -- plus one at each crossing, because a ford nobody can see is a ford
# that reads as a body walking into a wood and out of a river.
clearings = list(terrain["clearings"])


def glade(x, z, radius, softness, min_height, strength=1.0):
    return OD([("center", [float(x), float(z)]), ("radius", float(radius)),
               ("softness", float(softness)), ("strength", float(strength)),
               ("minHeight", float(min_height))])


clearings.append(glade(FORD_X, FORD_Z, 40.0, 26.0, 4.5))
clearings.append(glade(BACKWATER_PATH[2][0], BACKWATER_PATH[2][2], 36.0, 24.0, 4.5))
terrain["clearings"] = clearings

# =================================================================================================
# 3. THE CAST
# =================================================================================================
#
# Five characters, every one of them a `decide` behaviour over stock considerers (ADR-333,
# ADR-336). No scene in this repository declared one before this file: Glowmere's five aliens were
# all `explore`, which is the 700-line hardcoded decider ADR-269 was written about. So the cast is
# rebuilt rather than carried, and that is most of why valley 3 is a rebuild.
#
# **What is seeded and what is not.** The brief allows seeding environment, capability, personality
# and initial conditions so that interesting things are likely. It does not allow scripting the
# sequence. So what is authored below is: where each body starts, what it can perceive, what it
# likes, and how much it minds getting wet. What is *not* authored is what any of them does. There
# is no `actions` list on any of the five, no timeline, no staging scenario and no trigger. Every
# option in every overlay line comes out of a considerer scoring the world it found.
#
# The scale is ADR-335's 1.94, and the gait speeds are the v2 scene's own -- a stride speed is a
# claim about a clip times the node scale (ADR-204, ADR-226), so they travel with the body.

CAST_SCALE = 1.94

# Ground heights are the probe's (`avgen_tests "[.probe][glowmere2]"`) at the same XZ, because a
# scene node carries an explicit transform and a body authored off the ground either floats or is
# buried. The `ground` behaviour corrects a body that is out by a little; it cannot correct one
# that starts inside a hillside.
#
# name, asset, x, ground y, z, seed, walk m/s, run m/s, turn deg/s
CAST_SITES = [
    # The scout stands in the hollow, the one dead-flat open glade on the west bank, looking out
    # over the ford. It has the whole west floodplain and a 85 m reach to find things in.
    ("scout",     "alien-scout",   -74.0,  6.11,  -18.0, 11235813, 1.53, 3.41, 150.0),
    # The two route-takers start together on the east plain, 50 m east of the backwater, with the
    # same destination on its far side. Everything about them is identical but `wadePenalty`.
    ("wader",     "alien-diver",   132.0,  8.86,  -36.0, 31415926, 1.61, 3.62, 120.0),
    ("drylander", "alien-ranger",  128.0,  8.66,  -30.0, 16180339, 1.58, 3.55, 130.0),
    # The elder keeps to the ground by the elder fungus, 17 m from its cap.
    ("elder",     "alien-elder",     4.0,  6.14,   58.0, 27182818, 1.11, 2.35, 100.0),
    # The watcher starts 60 m north of the elder, out of its own `approach` range and inside its
    # perception range, so whether it goes is a decision and not a starting condition.
    ("watcher",   "alien-pilot",    26.0,  3.94,   96.0,  8814473, 1.47, 3.30, 140.0),
]

# **The small library, and it is small on purpose.** Each alien GLB ships 26 clips. Nine names are
# bound, and the same nine for every body. Demonstration 4 is that this is enough -- that the
# expression comes from look-at, additive reaction, gait rate-matching and slope alignment rather
# than from a bigger library -- so enlarging it would be proving the opposite.
CLIPS = OD([("idle", "Idle"), ("walk", "Walking"), ("run", "Running"), ("turn", "Idle_turn"),
            ("observe", "Idle"), ("react", "Crazy"), ("jump", "Jumping"), ("fall", "Fall_loop"),
            ("land", "Landing")])

# ADR-300's two pose layers, which no Glowmere scene has ever authored.
#
# `alien-scout.glb` is a flat Auto-Rig Pro export: the eyes, the mouth and the antenna are
# *siblings* of the head rather than children of it (ADR-337 §3 prints the hierarchy), so "turn the
# head" is five joints turning about the head's pivot and naming the pivot is not a detail. The
# joint names below are the rig's own and are shared by all six alien exports.
POSE_LAYERS = [
    OD([("name", "look"), ("kind", "aim"), ("drive", "look"),
        ("joints", ["head.x", "Eye_L", "Eye_R", "Mouth", "Antenna"]),
        ("pivot", "head.x"), ("maxYaw", 75.0), ("maxPitch", 30.0)]),
    OD([("name", "startle"), ("kind", "additive"), ("drive", "reaction"),
        ("clip", "Fight_head_hit"),
        ("joints", ["spine_02.x", "spine_03.x", "spine_04.x", "spine_05.x", "neck.x", "head.x"]),
        ("descendants", True)]),
]

# Where the two route-takers are going: a point on the east bank, the same one for both, so the
# only thing that differs between them is the price they put on a wet metre.
# The destination both route-takers are sent to: on the far side of the backwater, 35 m east of
# the main channel, on dry ground the staging probe read at 10.04 m.
FAR_SIDE = [38.0, 0.0, -46.0]


def idle_considerer(weight=0.05):
    return OD([("kind", "idle"), ("name", "idle"), ("weight", weight)])


def perception(range_m, capacity=14, hertz=4.0, fov=200.0, proximity=6.0):
    return OD([("range", range_m), ("fieldOfView", fov), ("proximityRange", proximity),
               ("capacity", capacity), ("hertz", hertz), ("occlusionTestsPerSecond", 0.0)])


def considerers_for(name):
    """Every character's taste, in one place, so the five can be read against one another."""
    if name == "scout":
        # DEMONSTRATION 1 -- environmental awareness.
        #
        # `interest` over the *percepts* is the wander: it appends one option per thing this body
        # has actually noticed, so where it goes next is a consequence of what it saw rather than
        # of a random heading. `investigate` is the stop-and-look, and it wins over `interest` for
        # the same subject because its weight is higher -- which is what turns "go there" into "go
        # there, face it, and attend to it for a while".
        #
        # The memory is what makes it resume rather than loop: `visitedCapacity` 6 and
        # `noveltyPenalty` 0.10 make a place it has just inspected worth a tenth of a fresh one.
        return [
            OD([("kind", "interest"), ("name", "roam"), ("weight", 1.0),
                ("source", "perceived"),
                ("weights", OD([("glow", 1.9), ("landmark", 1.5), ("vista", 1.0),
                                ("water", 0.8), ("character", 0.6)])),
                ("minRange", 9.0), ("maxRange", 150.0),
                ("noveltyRadius", 26.0), ("noveltyPenalty", 0.10)]),
            OD([("kind", "investigate"), ("name", "inspect"), ("weight", 1.45),
                ("kinds", ["glow", "landmark"]),
                ("activity", "observe"), ("approach", 4.5), ("dwell", 6.0),
                ("staleSeconds", 5.0), ("maxRange", 90.0),
                ("weights", OD([("glow", 2.0), ("landmark", 1.4)]))]),
            idle_considerer(),
        ]
    if name in ("wader", "drylander"):
        # DEMONSTRATION 2 -- environmental navigation (ADR-336).
        #
        # Identical but for one number. `wadePenalty` is the character's own price on a metre of
        # water; `fordPenalty` 0 and `detourPenalty` 40 are not opinions, they are the two probes
        # that find the two ways. The crossover is arithmetic and the two values below bracket it
        # with room on each side -- the measured crossover for this geography is printed by the
        # river probe, and these were chosen from it rather than from the lab's.
        wade = 1.6 if name == "wader" else 16.0
        return [
            OD([("kind", "route"), ("name", "cross"), ("weight", 1.0),
                ("wadePenalty", wade),
                ("fordPenalty", 0.0), ("detourPenalty", 40.0),
                ("falloff", 90.0), ("goalTolerance", 3.0),
                ("destinations", [OD([("name", "east-bank"), ("point", FAR_SIDE)])])]),
            # A second errand, so that "cross the river" is a thing it chose over something else
            # rather than the only line in the overlay. Weighted below the route on purpose.
            OD([("kind", "interest"), ("name", "roam"), ("weight", 0.42),
                ("source", "perceived"),
                ("weights", OD([("glow", 1.4), ("landmark", 1.1), ("character", 0.7)])),
                ("minRange", 12.0), ("maxRange", 70.0),
                ("noveltyRadius", 22.0), ("noveltyPenalty", 0.15)]),
            idle_considerer(),
        ]
    if name == "elder":
        # The one that is observed. It keeps to its ground, which is what gives `watcher` something
        # that stays findable, and it is the control for demonstration 3: a body that wandered off
        # would make "it went and looked at the other one" indistinguishable from "they both
        # happened to walk the same way".
        return [
            OD([("kind", "holdPost"), ("name", "grove"), ("weight", 1.0),
                ("post", ""), ("tolerance", 7.0), ("pull", 0.18), ("activity", "observe")]),
            OD([("kind", "interest"), ("name", "graze"), ("weight", 0.55),
                ("source", "perceived"),
                ("weights", OD([("glow", 1.6), ("landmark", 0.9), ("character", 0.5)])),
                ("minRange", 6.0), ("maxRange", 34.0), ("homeRadius", 26.0),
                ("noveltyRadius", 12.0), ("noveltyPenalty", 0.3)]),
            idle_considerer(),
        ]
    if name == "watcher":
        # DEMONSTRATION 3 -- character awareness.
        #
        # `investigate` with `kinds: ["character"]` is the whole of it. A percept of kind Character
        # is another entity this body has actually seen (`perception.cpp` publishes them from the
        # body index), so this scores bodies and nothing else; `interest` underneath gives it
        # somewhere to be when it has not noticed anyone.
        #
        # `dwell` 7 s is the observation, `approach` 6 m is how close it goes. Neither says *who*
        # and neither says *when*.
        return [
            OD([("kind", "investigate"), ("name", "watch"), ("weight", 1.7),
                ("kinds", ["character"]),
                ("activity", "observe"), ("approach", 6.0), ("dwell", 7.0),
                ("staleSeconds", 6.0), ("maxRange", 110.0),
                ("weights", OD([("character", 2.4)]))]),
            OD([("kind", "interest"), ("name", "roam"), ("weight", 0.9),
                ("source", "perceived"),
                ("weights", OD([("glow", 1.3), ("landmark", 1.2), ("vista", 1.1),
                                ("character", 0.4)])),
                ("minRange", 10.0), ("maxRange", 120.0),
                ("noveltyRadius", 24.0), ("noveltyPenalty", 0.12)]),
            idle_considerer(),
        ]
    raise KeyError(name)


PERCEPTION = {
    "scout": perception(85.0),
    "wader": perception(55.0),
    "drylander": perception(55.0),
    "elder": perception(45.0, capacity=10, hertz=3.0),
    "watcher": perception(120.0, capacity=16, hertz=5.0, fov=260.0),
}
# Decision cadence and hysteresis, per character. A deliberate spread: a body that re-decides twice
# a second reads as twitchy beside one that decides every two, and the difference is personality
# rather than tuning. `dwellTicks` x (1 / hertz) is the shortest an errand can last.
DECIDE = {
    "scout": (1.2, 3.0, 0.06),
    "wader": (0.8, 3.0, 0.05),
    "drylander": (0.8, 3.0, 0.05),
    "elder": (0.6, 4.0, 0.10),
    "watcher": (1.0, 3.0, 0.07),
}

cast_nodes = []
cast_entities = []
for (name, asset, x, gy, z, seed, walk, run, turn) in CAST_SITES:
    cast_nodes.append(OD([
        ("name", name), ("kind", "gltf"),
        ("asset", "../../assets/aliens/%s.glb" % asset),
        ("position", [x, gy, z]),
        ("rotation", [0.0, 0.0, 0.0]),
        ("scale", [CAST_SCALE, CAST_SCALE, CAST_SCALE]),
        ("animation", OD([
            ("state", "Idle"),
            # 0.35 s, the v2 scene's own. ADR-337 §8 records what a blend costs a measurement that
            # reads the first frames of a clip; it costs nothing to a body that is looked at.
            ("blend", 0.35), ("speed", 1.0),
            ("updateHz", 30), ("nearDistance", 25.0), ("farHz", 20.0), ("cullDistance", 360.0),
            ("layers", POSE_LAYERS),
        ])),
        ("visible", True),
    ]))
    hz, dwell, margin = DECIDE[name]
    cast_entities.append(OD([
        ("name", name), ("node", name), ("seed", seed),
        ("fullDetailDistance", 60.0), ("coarseInterval", 0.2), ("cullDistance", 620.0),
        ("gait", OD([
            ("walkSpeed", walk), ("runSpeed", run),
            # Rate matching on: the clip's stride is authored for one speed and the body travels at
            # whatever the route gives it. This is the "locomotion adaptation" half of
            # demonstration 4 and it is a setting, not a clip.
            ("matchRate", True), ("minDwell", 0.35),
        ])),
        ("clips", CLIPS),
        ("behaviors", [
            OD([("kind", "decide"), ("hertz", hz), ("dwellTicks", dwell), ("margin", margin),
                # The two memories the decider keeps (ADR-333): how long a percept is worth
                # anything, and how many places it remembers having been. Both bounded, both
                # rebuilt by a replay.
                ("memorySeconds", 9.0), ("memoryCapacity", 16.0), ("visitedCapacity", 6.0),
                ("considerers", considerers_for(name))]),
            # Look-at publishes the target the `look` pose layer aims at. It is what turns
            # "standing near a mushroom" into "looking at a mushroom".
            OD([("kind", "lookAt"), ("turnRate", turn * 0.7), ("weight", 1.0)]),
            # Breathing, sway and a head nod, scaled per body. Not animation: a sine on the
            # transform, under the clip.
            OD([("kind", "liveliness"), ("bounce", 0.19), ("stride", 5.6),
                ("sway", 4.2), ("nod", 3.0)]),
            # Last, so the terrain has the final word on height and tilt. ADR-337 §5: grounding
            # *assigns* `travel.y`, so anything that wrote a vertical before this is overwritten,
            # which is correct and is why no clip here is opted into root motion.
            OD([("kind", "ground"), ("slopeAlign", 0.5), ("footprint", 1.6)]),
        ]),
        ("perception", PERCEPTION[name]),
    ]))

# The saucer, the beam, the abduction and the farm leave. Each is named rather than filtered by a
# pattern, so a node this script does not know about survives and shows up in the node count.
DROP_NODES = {"visitor", "visitor-beam"} | {
    "%s-%d" % (s, i) for s, i in
    [("bull", 1), ("horse", 2), ("cow", 3), ("sheep", 4), ("goat", 5), ("pig", 6), ("rooster", 7),
     ("chicken", 8), ("bull", 10), ("horse", 11), ("cow", 12), ("sheep", 13), ("goat", 14),
     ("pig", 15), ("rooster", 16), ("chicken", 17)]}
# The five v2 aliens leave too: their bodies come back under new names with new minds, and keeping
# the old names would make "this is the same character with a decider bolted on" a claim nobody
# could check.
DROP_NODES |= {"rook", "tide", "sage", "ember", "vane"}

before_nodes = len(d["nodes"])
d["nodes"] = [n for n in d["nodes"] if n.get("name") not in DROP_NODES] + cast_nodes
d["entities"] = cast_entities
d.pop("staging", None)
print("nodes %d -> %d (%d dropped, %d cast added); entities %d"
      % (before_nodes, len(d["nodes"]), before_nodes - (len(d["nodes"]) - len(cast_nodes)),
         len(cast_nodes), len(d["entities"])))

# The hero table loses the saucer with the saucer. The ten fungi keep their ranks and their
# importances, which ADR-072 requires to be strictly descending.
d["heroes"] = [h for h in d["heroes"] if h.get("name") != "visitor"]
imps = [h["importance"] for h in d["heroes"]]
assert imps == sorted(imps, reverse=True) and len(set(imps)) == len(imps), \
    "hero importances must be strictly descending: %s" % imps

# `composition.focalPoints` named an "elder" at (-1, 4.5, -46). No node has been there since the
# original Glowmere's elder was replaced by `elder-2-cap` at (-12, 3.54, 52) -- it is a dangling
# focal point that resolves to a position rather than to a name, so nothing could report it. It is
# restated against the organism it was always meant to name.
elder_cap = next(n for n in d["nodes"] if n["name"] == "elder-2-cap")
d["composition"] = OD([("focalPoints", [
    OD([("name", "elder-2"), ("position", list(elder_cap["position"])), ("radius", 10.0)]),
    OD([("name", "the-ford"),
        ("position", [round(FORD_X, 1), round(FORD_LEVEL, 2), FORD_Z]), ("radius", 14.0)]),
])])

# =================================================================================================
# 4. THE CAMERAS
# =================================================================================================
#
# Four fixed cameras and one free-roaming director, and a shot list that visits each demonstration.
# The eyes are stated here rather than baked from a director run, so there is no `cameraShotSpans`
# to go stale when a body's radius changes (which is the maintenance ADR-334 had to do by hand on
# fifteen spans).
#
# Distances are set from the subject: a 3.48 m alien filmed as a character wants 8 to 20 m, not the
# 268 m the multicam film's Valley Wide stands off at. ADR-334's closing note says so in as many
# words -- "a native-scale cast is not legible from either [fixed camera] ... the cast now belongs
# to the shots the director composes for it" -- so valley 3 composes for it.
CAMERAS = [
    # id, name, eye, target, fov
    (1, "Director", None, None, None),  # autoDirector
    (2, "The Hollow", [-92.0, 14.0, -40.0], [-70.0, 8.6, -16.0], 42.0),
    (3, "The Ford", [-4.0, 12.5, -30.0], [-18.0, 3.4, 2.0], 38.0),
    (4, "The Causeway", [-2.0, 14.0, 112.0], [-31.0, 1.0, 96.0], 40.0),
    (5, "The Grove", [-4.0, 10.5, 74.0], [-22.0, 5.0, 54.0], 44.0),
]
SHOTS = [
    # camera, start, end, label
    (2, 0.0, 22.0, "awareness: the scout in the hollow"),
    (3, 22.0, 52.0, "navigation: the ford"),
    (4, 52.0, 78.0, "navigation: the causeway"),
    (5, 78.0, 104.0, "character awareness: the watcher and the elder"),
    (1, 104.0, 140.0, "the director, on whatever it finds"),
]

cameras = []
for cid, name, eye, target, fov in CAMERAS:
    cam = OD([("id", cid), ("name", name)])
    if eye is None:
        cam["autoDirector"] = True
    else:
        # `slug` is what a camera's parameters hang off, so a camera without one has no path and
        # the loader refuses the scene by name. Derived rather than authored twice.
        cam["slug"] = "".join(ch for ch in name.lower() if ch.isalnum())
        cam["placement"] = "free"
        cam["position"] = eye
        cam["target"] = target
        cam["fov"] = fov
        cam["autoDirector"] = False
    cameras.append(cam)
d["cameraDirection"] = OD([
    ("cameras", cameras),
    ("shots", [OD([("camera", c), ("start", s), ("end", e), ("transition", "cut"),
                   ("locked", True), ("label", label)]) for c, s, e, label in SHOTS]),
    ("default", 2),
    ("nextId", len(CAMERAS) + 1),
])
# The scene's own camera is shot 1's, so a bare `Composition::loadFile` with no project opens on
# something composed rather than on wherever the last session left the viewport.
d["camera"] = OD([("mode", 1), ("position", CAMERAS[1][2]), ("target", CAMERAS[1][3]),
                  ("fov", CAMERAS[1][4]), ("orbitSpeed", 0.0)])

json.dump(d, open(SCENE, "w"), indent=1)
scene_bytes = open(SCENE, "rb").read()
print("wrote %s: %d nodes, %d entities, %d scatter layers (%d tree), tree line %.2f m"
      % (SCENE, len(d["nodes"]), len(d["entities"]), len(terrain["scatter"]),
         len(TREE_LAYERS), TREE_LINE))

# =================================================================================================
# 5. THE PROJECT
# =================================================================================================
#
# Small, and every key in it is written down here.
#
# ADR-264: a project's `parameters` are applied *over* its scene, so every one of them is a place
# the scene can be contradicted. valley-2-multicam carries 5,455 of them and 41 contradict the
# scene outright. Valley 3 carries the handful below, and none of them names a node -- so there is
# no transform in this file at all, and `nodes/*/position` cannot drift from the scene because it
# is not here to drift.
#
# ADR-271: the scene is referenced by hash, not embedded, and a UI edit lands *here* rather than in
# the scene. That is correct and is not being changed. What it means for hygiene is that this file
# will grow the moment anybody saves from the application -- so the generator is idempotent and
# re-running it is how the project is returned to its authored state.
PARAMETERS = OD([
    # The exposure and grade valley 2 arrived at, restated rather than inherited through 5,455
    # keys. These four are the ones a render of this world is actually judged on.
    ("camera/exposure/mode", 0),
    ("camera/exposure/compensation", 0.35),
    ("post/grade/contrast", 1.04),
    ("post/grade/saturation", 1.06),
])

project = OD([
    ("format", "avgen-project"),
    ("version", 4),
    ("app", OD([("name", "Glowmere Valley 3 - the autonomous cast")])),
    ("assets", OD([("scene", OD([
        ("kind", "composition"),
        ("path", OD([("path", "glowmere-valley-3.scene.json"),
                     ("sha256", hashlib.sha256(scene_bytes).hexdigest()),
                     ("size", len(scene_bytes))])),
    ]))])),
    ("parameters", PARAMETERS),
    ("render", OD([
        ("backend", "gpu"), ("path", "renders/glowmere-valley-3"),
        ("pattern", "frame_%05d.png"), ("width", 1600), ("height", 900),
        ("fps", 30.0), ("start", 0.0), ("end", 140.0),
        ("supersample", 1), ("quality", 1),
    ])),
    # Empty and present, so the shape of the document is the shape the application writes and a
    # save does not reorder the file. Absent keys and empty ones mean the same thing to the loader.
    ("presets", []),
    ("routes", []),
    ("shaders", []),
    ("sources", []),
    ("worldEffects", d["worldEffects"]),
])
json.dump(project, open(PROJECT, "w"), indent=1)
print("wrote %s: %d parameters, %d routes, no timeline, no sequence, no staging"
      % (PROJECT, len(PARAMETERS), len(project["routes"])))
print("scene sha256 %s (%d bytes)" % (hashlib.sha256(scene_bytes).hexdigest()[:16],
                                      len(scene_bytes)))
print("done.")

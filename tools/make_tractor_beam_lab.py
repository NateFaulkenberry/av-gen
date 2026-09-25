#!/usr/bin/env python3
"""Author the Tractor Beam Lab: the abduction, and nothing else.

Why this exists. "The animal does not line up with the beam" has been answered three times from
Glowmere Valley 2, and Glowmere Valley 2 is a 700k-triangle world with a cutting director, a river,
sixteen animals on a hillside and a camera that is usually looking somewhere else. Every one of
those is a reason a measurement can be right and a picture can still be wrong, and none of them is
the abduction.

So: a flat plane, one saucer, the production beam, and a cast laid out so that each of the things
that could be wrong has its own row.

The two rules this file is built to keep:

  * **It is the production scenario.** `STAGING` is imported from `make_abduction_scenario`, which
    is the module Glowmere's own scene is written from. The lab overrides *durations only* -- a
    lab that re-typed the beats would prove things about the lab.
  * **The saucer and the beam are the production nodes.** Both are copied out of
    `glowmere-valley-2.scene.json` at generation time rather than retyped, so a divergence between
    the lab and the film is impossible rather than merely unlikely.

The cast, and what each row is for (the brief's A-H):

  A  centre          one goat under the origin: the reference case.
  B  x offsets       goats at world x = -10, -5, 0, +5, +10. A fixed error reads the same on all
                     five; an error that scales with world position does not.
  C  z offsets       the same on the other horizontal axis.
  D  rotation        four identical cows at yaw 0/90/180/270. This is the row that separates
                     "the code uses the entity origin" from "the code uses the drawn body": a GLB
                     whose mesh is not centred on its own origin puts its body in a different place
                     for each of these four, and the entity origin is identical in all four.
  E  assets          one of every farm species, so an asset-specific model transform shows up as
                     one row differing from the others rather than as a global bias.
  F  static          every row above has no locomotion behaviour: the base transform relationship.
  G  animated        two animals with `wander`, `liveliness` and a walk clip, so the difference
                     between "the transform" and "the pose" is measurable rather than assumed.

H -- the full production sequence -- is not a row. It is what the lab *does*: the scenario runs
acquire -> approach -> beam -> abduct -> depart on every one of them in turn, exactly as the film
does.

Usage:  tools/make_tractor_beam_lab.py [out.scene.json]
"""
import collections
import copy
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_abduction_scenario as prod  # noqa: E402  (the production scenario, not a copy of it)

od = collections.OrderedDict
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(REPO, "examples/world/glowmere-valley-2.scene.json")
OUT = os.path.join(REPO, "examples/world/tractor-beam-lab.scene.json")

# The lab's own timings. Durations only: nothing here changes what a step *does*, only how long it
# takes, so the lab can walk twenty-two cases in three minutes instead of seven.
#
# `beamDrainSeconds` is shortened too, and that one needs saying: it is load bearing for ADR-218's
# third defect (a hidden particle system freezes rather than clears, so the pool must be allowed to
# empty *before* the beam is hidden). The lab is not where that is asserted --
# `test_abduction_alignment.cpp` asserts it against the shipped scene, at the shipped 5.0 s -- and
# twenty-three cases each waiting five seconds for a pool to drain is eighty seconds of test time
# spent on a question the lab does not ask.
LAB_TIMINGS = {
    "beamDrainSeconds": 1.2,
    "searchRadius": 900.0,    # the whole lab, from wherever the saucer is
    "minRange": 0.0,          # nothing is "too close" on a flat plane with nothing on it
    "targetClearance": 0.0,   # there is no canopy
    "travelSpeed": 90.0,
    "approachSeconds": 2.2,
    "aimSeconds": 0.4,
    "hoverSeconds": 0.8,
    "beamSeconds": 0.5,
    "abductSeconds": 3.0,
    "gapSeconds": 0.4,
    "beamFadeSeconds": 0.5,
}

# Every case, in the order the director will take them. `pick: first` walks the candidate list in
# entity order, `claim` stops two cues sharing one, and `retire` takes each one out of the draw as
# it finishes -- so this list *is* the running order, and the test can name the case by frame.
#
# (name, asset, x, z, yaw, animated)
GOAT, COW = "goat", "cow"
CASES = (
    [("A-centre", GOAT, 0.0, 0.0, 0.0, False)]
    + [("B-x%+d" % x, GOAT, float(x), -120.0, 0.0, False) for x in (-10, -5, 0, 5, 10)]
    + [("C-z%+d" % z, GOAT, -120.0, float(z), 0.0, False) for z in (-10, -5, 0, 5, 10)]
    + [("D-yaw%d" % y, COW, 140.0, -60.0 + 50.0 * i, float(y), False)
       for i, y in enumerate((0, 90, 180, 270))]
    + [("E-" + a, a, -160.0 + 64.0 * i, 140.0, 35.0, False)
       for i, a in enumerate(("bull", "horse", "cow", "sheep", "pig", "chicken"))]
    + [("G-animated-cow", COW, -60.0, 220.0, 0.0, True),
       ("G-animated-horse", "horse", 20.0, 220.0, 120.0, True)]
)

# ADR-213 put the farm at 3.6x and the beam was sized for that; the lab has to carry the same scale
# or it is measuring a different animal.
FARM_SCALE = 3.6

ANIMATION = od([("state", "Walk"), ("speed", 1.0), ("blend", 0.3), ("updateHz", 30),
                ("farHz", 15.0), ("nearDistance", 20.0), ("cullDistance", 2000.0)])


def animal_node(name, asset, x, z, yaw):
    return od([
        ("name", name),
        ("kind", "gltf"),
        ("asset", "../../assets/farm/%s.glb" % asset),
        ("position", [x, 0.0, z]),
        ("rotation", [0.0, yaw, 0.0]),
        ("scale", [FARM_SCALE, FARM_SCALE, FARM_SCALE]),
        ("animation", ANIMATION),
        ("visible", True),
    ])


def animal_entity(name, asset, animated, seed):
    # The static rows carry `ground` and nothing else: a body on the surface, with no behaviour of
    # its own adding an offset. That is what makes them the control -- whatever a lift does to them
    # is the director's doing and the asset's, and cannot be a wander or a sway.
    behaviors = [od([("kind", "ground"), ("slopeAlign", 1.0)])]
    if animated:
        behaviors = [
            od([("kind", "liveliness"), ("bounce", 0.04), ("bounceRate", 0.9),
                ("sway", 2.4), ("swayRate", 0.15), ("nod", 1.2)]),
            od([("kind", "wander"), ("speed", 3.0), ("runSpeed", 5.0), ("turnRate", 80.0),
                ("minRange", 4.0), ("maxRange", 16.0), ("homeRadius", 20.0),
                ("pauseMin", 1.0), ("pauseMax", 3.0), ("arrive", 0.85)]),
        ] + behaviors
    return od([
        ("name", name),
        ("node", name),
        ("seed", seed),
        ("tags", ["animal", "farm", asset, "animated" if animated else "static"]),
        # No distance culling anywhere in the lab. ADR-186's cull is a fact about a 700k-triangle
        # world and a camera that is somewhere else; a lab whose animals stopped simulating when
        # the camera looked away would be measuring the cull.
        ("fullDetailDistance", 0.0),
        ("cullDistance", 0.0),
        ("coarseInterval", 0.1),
        ("clips", od([("idle", "Walk"), ("walk", "Walk"), ("run", "Walk"), ("turn", "Walk")])),
        ("gait", od([("walkSpeed", 6.0), ("runSpeed", 6.0), ("rateMin", 0.005), ("rateMax", 1.0),
                     ("matchRate", True), ("idleRate", 0.0), ("accel", 5.0), ("decel", 7.0),
                     ("moveEnter", 0.06), ("moveExit", 0.03), ("runEnter", 5.0),
                     ("runExit", 3.8)])),
        ("behaviors", behaviors),
    ])


# What the abduction said before ADR-262, spelled as a patch over what it says now.
def legacy_staging(staging):
    """Put the scenario back the way it was, so the lab has a control that fails.

    ADR-182: an arm that cannot fail proves nothing. Every number in the report below was produced
    by running *this build's* measurement code against *this* configuration -- so "before" and
    "after" differ in the scenario and in nothing else, which is the only way the difference is
    attributable to the fix rather than to the instrument.

    Four changes, and each one is a defect ADR-262 names:

      1. `clearance` back on `hover` and `hold`. `cruiseClearance` is 34 m against a `hoverHeight`
         of 23, and a clearance is a floor -- so the saucer held station eleven metres higher than
         the shot asked, and the beam's column does not reach that far.
      2. `liftHeight` back to -7.5, measured from the saucer's origin instead of from the beam's
         mouth, which hangs the tallest animals' backs above the emitter disc.
      3. the lift aimed at `actor` instead of `actor.beam`: at the craft that carries the column
         rather than at the column.
      4. `anchor: visual` instead of `drawn`, and no `place` at all: entity arithmetic instead of
         the flattened scene, and the node's origin instead of the body it draws.
    """
    scenario = staging["scenarios"][0]
    for param in scenario["params"]:
        if param["name"] == "liftHeight":
            param["value"] = -7.5
    for beat in scenario["beats"]:
        for cue in beat["cues"]:
            for step in cue["steps"]:
                if step.get("name") in ("hover", "hold"):
                    step["clearance"] = prod.ref("cruiseClearance")
                if step.get("name") == "lift":
                    step["to"] = "actor"
                    step["anchor"] = "visual"
                    step.pop("place", None)
    return staging


def lab_staging():
    """The production scenario with the lab's durations and the lab's tie-break.

    `pick` goes from `nearest` to `first` for one reason and it is not convenience: `nearest`
    makes the running order depend on where the saucer happens to be, which makes the case a frame
    belongs to depend on the frame before it. `first` is the entity order, which is this file's
    CASES list, which is what a report can name.
    """
    staging = copy.deepcopy(prod.STAGING)
    scenario = staging["scenarios"][0]
    scenario["maxCycles"] = len(CASES) + 2
    scenario["seed"] = 20260917
    for param in scenario["params"]:
        if param["name"] in LAB_TIMINGS:
            param["value"] = LAB_TIMINGS[param["name"]]
    for beat in scenario["beats"]:
        for query in beat.get("find", []):
            query["pick"] = "first"
            query["requireNavigable"] = False  # a flat plane is navigable; do not make the lab
            query["clearance"] = 0.0           # depend on the nav grid having been built
    return staging


def build(only=None, legacy=False):
    """The whole lab, or -- with `only` -- one case on its own with the camera framed on it.

    The single-case mode is not a different lab. It is the same ground, the same saucer, the same
    beam and the same scenario with one animal in the cast, which is what makes a *picture* of one
    case cheap: the first abduction is the one you want to look at, six seconds in, instead of the
    twelfth, two minutes in.
    """
    src = json.load(open(SOURCE), object_pairs_hook=od)
    by_name = {n["name"]: n for n in src["nodes"]}
    src_entities = {e["name"]: e for e in src["entities"]}

    visitor = copy.deepcopy(by_name["visitor"])
    visitor["position"] = [0.0, 60.0, 300.0]
    beam = copy.deepcopy(by_name["visitor-beam"])
    beam["visible"] = False

    ground = od([
        ("name", "ground"),
        ("kind", "terrain"),
        ("position", [0.0, 0.0, 0.0]),
        ("rotation", [0.0, 0.0, 0.0]),
        ("scale", [1.0, 1.0, 1.0]),
        # Flat, and flat on purpose: no layers, no features, no sea. `aboveGround` and `clearance`
        # are production code paths and the lab has to run them, but a hillside is a second thing
        # that can move an animal and this experiment is about the first.
        ("world", od([("name", "flat"), ("seed", 1), ("size", [900.0, 900.0]),
                      ("baseHeight", 0.0), ("layers", []), ("features", [])])),
        ("terrain", od([("chunkSize", 60.0), ("resolution", 16), ("lodLevels", 2),
                        ("lodDistance", 200.0), ("viewDistance", 900.0), ("skirtDepth", 1.0),
                        ("groundMottle", False), ("groundGlow", 0.0)])),
        ("material", od([("baseColor", [0.10, 0.11, 0.13]), ("roughness", 0.92),
                         ("metallic", 0.0), ("emissiveIntensity", 0.0)])),
        ("scatter", []),
        ("visible", True),
    ])

    nodes = [ground, visitor, beam]
    entities = [copy.deepcopy(src_entities["visitor"]), copy.deepcopy(src_entities["visitor-beam"])]
    entities[0]["fullDetailDistance"] = 0.0
    entities[0]["cullDistance"] = 0.0
    entities[1]["cullDistance"] = 0.0
    cast = [c for c in CASES if only is None or c[0] == only]
    if not cast:
        raise SystemExit("no such case: %s (have %s)" % (only, ", ".join(c[0] for c in CASES)))
    for i, (name, asset, x, z, yaw, animated) in enumerate(cast):
        nodes.append(animal_node(name, asset, x, z, yaw))
        entities.append(animal_entity(name, asset, animated, 700000 + 37 * i))

    # Square on to the lift and level with the middle of it: the saucer hovers at ground + 34 (the
    # scenario's `cruiseClearance`, which is above its `hoverHeight`) and the animal rises to about
    # 27, so the interesting sixty metres of world are x +- 40 about the case and y 0..40. The
    # camera does not move, on purpose -- a camera that tracked the saucer would hide a drift by
    # following it.
    if only is not None:
        cx, cz = cast[0][2], cast[0][3]
    else:
        cx, cz = 0.0, 0.0
    # Three-quarters rather than square on, and that is not a taste decision. A camera looking down
    # one world axis cannot see an offset along the other: the first frames taken of this lab showed
    # a cow that looked perfectly centred in the column while the probe was reporting 1.2 m, because
    # the whole of the error happened to be pointing at the lens that second. From 45 degrees both
    # horizontal axes project.
    camera = od([("mode", 1), ("position", [cx + 44.0, 18.0, cz + 44.0]),
                 ("target", [cx, 15.0, cz]), ("fov", 46.0), ("orbitSpeed", 0.0)])

    scene = od([
        ("format", "avgen-scene"),
        ("version", 1),
        ("name", "Tractor Beam Lab"),
        # Wide, high and square on to the row, so the beam's column and the animal in it are both
        # whole in frame. The abduction moves; the camera does not, on purpose -- a camera that
        # tracked would hide a drift by following it.
        ("camera", camera),
        ("environment", od([
            ("background", [0.012, 0.016, 0.028]),
            ("intensity", 0.0),
            ("skyIntensity", 0.35),
            ("lightFromEnvironment", True),
            # Enough fog to read a column of light against, and not a metre more: volumetrics are
            # the one thing in this scene that could make a beam look like it is somewhere it is
            # not, and the lab's job is to remove those.
            ("fogColor", [0.02, 0.03, 0.05]),
            ("fogHeight", 40.0),
            ("fogHeightFalloff", 0.05),
            ("volumeDensity", 0.004),
            ("volumeScattering", 0.4),
            ("volumeAnisotropy", 0.1),
            ("volumeSteps", 12),
            ("volumeMaxDistance", 400.0),
            ("shadowCascades", 2),
            ("sky", od([("zenithColor", [0.010, 0.016, 0.034]),
                        ("horizonColor", [0.030, 0.052, 0.090]),
                        ("groundColor", [0.006, 0.008, 0.012]),
                        ("haze", 0.4),
                        ("sunColor", [0.36, 0.42, 0.60]),
                        ("sunIntensity", 1.0), ("sunSize", 1.2), ("sunGlow", 0.2)])),
        ])),
        ("post", od([("tonemap", 1), ("bloomEnabled", True), ("bloomIntensity", 0.12),
                     ("bloomThreshold", 1.2), ("bloomKnee", 0.5), ("bloomRadius", 1.1),
                     ("bloomLevels", 5), ("bloomEmissionWeight", 0.6)])),
        ("nodes", nodes),
        ("entities", entities),
        ("navBodyRadius", 1.2),
        ("staging", legacy_staging(lab_staging()) if legacy else lab_staging()),
    ])
    return prod.apply(scene, staging=scene["staging"])


if __name__ == "__main__":
    argv = sys.argv[1:]
    legacy = "--legacy" in argv
    if legacy:
        argv.remove("--legacy")
    only = None
    if "--case" in argv:
        i = argv.index("--case")
        only = argv[i + 1]
        del argv[i:i + 2]
    out = argv[0] if argv else OUT
    scene = build(only, legacy)
    with open(out, "w") as f:
        json.dump(scene, f, indent=1)
    print("tractor beam lab%s written to %s (%d case%s)"
          % (" [legacy]" if legacy else "", out, 1 if only else len(CASES),
             "" if only else "s"))

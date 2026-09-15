#!/usr/bin/env python3
"""Author the UFO abduction scenario into Glowmere Valley 2 (ADR-209, Part 7).

Every number here is a *director parameter*, and every step is one of the eleven kinds every
scenario shares. There is no UFO in the C++: `saucer` is an actor, `animal` is a tag, and swapping
either produces a different sequence with no code change at all. That is the architectural claim the
POC exists to test, and this file is the test of it.
"""
import collections, json, os, sys

od = collections.OrderedDict
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE = os.path.join(REPO, "examples/world/glowmere-valley-2.scene.json")

def p(name, value, lo, hi):
    return od([("name", name), ("value", value), ("min", lo), ("max", hi)])

def ref(name):
    return od([("param", name)])

PARAMS = [
    # The brief's list, in its own words, as ordinary params::Parameters.
    p("searchRadius",     260.0,  20.0,  600.0),   # Search Radius
    p("minRange",          25.0,   0.0,  200.0),   # not the one it is already on top of
    p("targetClearance",    6.5,   0.0,   40.0),   # max canopy over a reachable target
    p("hoverHeight",       23.0,   5.0,   80.0),   # Preferred Height
    p("cruiseClearance",   34.0,   5.0,  120.0),   # metres of air it keeps under itself en route
    p("travelSpeed",       30.0,   1.0,  120.0),   # Travel Speed
    p("approachSeconds",    7.0,   0.5,   60.0),   # Approach Duration / smoothing
    p("aimSeconds",         0.8,   0.0,   10.0),
    p("hoverSeconds",       2.4,   0.0,   30.0),   # Hover Duration
    p("beamSeconds",        1.1,   0.0,   10.0),   # Beam Activation Time
    p("beamFadeSeconds",    0.9,   0.0,   10.0),
    p("abductSeconds",      4.6,   0.5,   30.0),   # Abduction Duration
    p("liftHeight",        -3.4, -40.0,    0.0),   # Animal Lift Height, under the saucer's belly
    p("animalSpin",       230.0,-1440.0,1440.0),   # Animal Rotation
    p("animalWobble",       0.85,  0.0,   10.0),
    p("animalWobbleRate",   1.35,  0.0,   10.0),
    p("animalGait",         1.30,  0.0,   10.0),   # the speed the gait reads: the legs keep going
    p("craftWobble",        0.30,  0.0,   10.0),
    p("craftWobbleRate",    0.45,  0.0,   10.0),
    p("gapSeconds",         3.2,   0.0,   60.0),   # Time Between Targets
    p("beamSpawnRate",   2900.0,   0.0,20000.0),
    p("beamEmissive",       1.55,  0.0,   10.0),
    p("beamRestRate",    1050.0,   0.0,20000.0),   # what the scene authored
    p("beamRestEmissive",   0.45,  0.0,   10.0),
]

BEATS = [
    od([
        ("name", "acquire"),
        # The decision. Nothing about this names an animal, a species or a coordinate: it is a tag,
        # a radius measured from the actor, a canopy ceiling and a tie-break.
        ("find", [od([
            ("bind", "target"),
            ("tag", "animal"),
            ("from", "actor"),
            ("radius", ref("searchRadius")),
            ("minRadius", ref("minRange")),
            ("clearance", ref("targetClearance")),
            ("pick", "nearest"),
            ("requireNavigable", True),
            ("claim", True),
        ])]),
        ("cues", []),
        ("then", "approach"),
        # No target left, or none reachable: the cycle ends, the scenario goes idle, every claim is
        # dropped, and `start` will run it again. Not a stuck state.
        ("otherwise", ""),
    ]),
    od([
        ("name", "approach"),
        ("cues", [od([
            ("role", "actor"),
            ("steps", [
                od([("kind", "lookAt"), ("name", "aim"), ("to", "target"),
                    ("duration", ref("aimSeconds"))]),
                # `aboveGround`: the hover height is measured from the terrain under the animal,
                # not from the animal. Two cues that each take their height from the other diverge
                # -- the saucer goes to cow + 23, the cow goes to saucer - 3.4, and next frame both
                # read the other's new height. Measured, before this: 488 m of "lift" in four and a
                # half seconds. `setDesc` now refuses a beat shaped like that, and this is the
                # shape that is right.
                od([("kind", "moveTo"), ("name", "approach"), ("to", "target"),
                    ("height", ref("hoverHeight")), ("aboveGround", True),
                    ("speed", ref("travelSpeed")),
                    ("duration", ref("approachSeconds")),
                    ("clearance", ref("cruiseClearance"))]),
            ]),
        ])]),
        ("then", "beam"),
    ]),
    od([
        ("name", "beam"),
        # The parallel: the craft holds station while the beam lights. Two entities, one beat.
        ("cues", [
            od([("role", "actor"), ("steps", [
                od([("kind", "follow"), ("name", "hover"), ("to", "target"),
                    ("height", ref("hoverHeight")), ("aboveGround", True),
                    ("duration", ref("hoverSeconds")),
                    ("clearance", ref("cruiseClearance")),
                    ("wobble", ref("craftWobble")),
                    ("wobbleRate", ref("craftWobbleRate"))]),
            ])]),
            od([("role", "actor.beam"), ("steps", [
                od([("kind", "show"), ("name", "beamOn")]),
                od([("kind", "set"), ("name", "beamRise"), ("target", "spawnRate"),
                    ("to", ref("beamSpawnRate")), ("duration", ref("beamSeconds"))]),
                od([("kind", "set"), ("name", "beamGlow"), ("target", "emissive"),
                    ("to", ref("beamEmissive")), ("duration", 0.2)]),
            ])]),
        ]),
        ("then", "abduct"),
    ]),
    od([
        ("name", "abduct"),
        ("cues", [
            od([("role", "actor"), ("steps", [
                # And the same here, which is the beat the loop actually bit in: the saucer holds
                # station over the *ground* the animal came off while the animal rises to meet it.
                od([("kind", "follow"), ("name", "hold"), ("to", "target"),
                    ("height", ref("hoverHeight")), ("aboveGround", True),
                    ("duration", ref("abductSeconds")),
                    ("clearance", ref("cruiseClearance")),
                    ("wobble", ref("craftWobble")),
                    ("wobbleRate", ref("craftWobbleRate"))]),
            ])]),
            od([("role", "target"), ("steps", [
                # Up the beam: an eased rise toward the saucer's belly, spinning, wobbling, and with
                # a gait speed written so the legs keep going all the way up.
                od([("kind", "moveTo"), ("name", "lift"), ("to", "actor"),
                    ("height", ref("liftHeight")),
                    ("duration", ref("abductSeconds")),
                    ("spin", ref("animalSpin")),
                    ("wobble", ref("animalWobble")),
                    ("wobbleRate", ref("animalWobbleRate")),
                    ("rate", ref("animalGait"))]),
                od([("kind", "retire"), ("name", "vanish")]),
            ])]),
        ]),
        ("then", "depart"),
    ]),
    od([
        ("name", "depart"),
        ("release", True),
        ("cues", [
            od([("role", "actor.beam"), ("steps", [
                od([("kind", "set"), ("name", "beamFall"), ("target", "spawnRate"),
                    ("to", ref("beamRestRate")), ("duration", ref("beamFadeSeconds"))]),
                od([("kind", "set"), ("name", "beamDim"), ("target", "emissive"),
                    ("to", ref("beamRestEmissive")), ("duration", 0.3)]),
                od([("kind", "hide"), ("name", "beamOff")]),
            ])]),
            od([("role", "actor"), ("steps", [
                od([("kind", "wait"), ("name", "beat"), ("duration", ref("gapSeconds"))]),
            ])]),
        ]),
        # No `then`: the beat list runs out, which is a cycle, which is the next abduction.
    ]),
]

STAGING = od([
    ("actors", [od([
        ("name", "saucer"),
        ("body", "visitor"),
        # The transform relationship is the scene's own parenting -- `visitor-beam` already says
        # `"parent": "visitor"`. This is the *name* for the group, which is what was missing.
        ("parts", [od([("name", "beam"), ("entity", "visitor-beam")])]),
    ])]),
    ("scenarios", [od([
        ("name", "abduction"),
        ("actor", "saucer"),
        ("seed", 20260915),
        ("autoStart", True),
        ("maxCycles", 6),          # Maximum Abductions
        ("searchInterval", 0.5),
        ("params", PARAMS),
        ("beats", BEATS),
    ])]),
])

SCENE = sys.argv[1] if len(sys.argv) > 1 else SCENE
scene = json.load(open(SCENE), object_pairs_hook=od)
scene["staging"] = STAGING
# The beam is off until the director lights it. It was authored always-on, which is the right look
# for a craft that never does anything and the wrong one for a craft that abducts things.
for node in scene["nodes"]:
    if node.get("name") == "visitor-beam":
        node["visible"] = False
with open(SCENE, "w") as out:
    json.dump(scene, out, indent=1)
print("staging written to %s; now run tools/refresh_scene_fingerprint.py" % SCENE)

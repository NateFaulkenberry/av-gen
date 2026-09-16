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
    # 0.85 before ADR-218. The sway is deliberate comedy and it is also a metre and a half of beam
    # width: it adds directly to how far off the column's axis the animal can be, and the animal is
    # 3.6x the size it was when 0.85 was chosen (ADR-213).
    p("animalWobble",       0.40,  0.0,   10.0),
    p("animalWobbleRate",   1.35,  0.0,   10.0),
    p("animalGait",         1.30,  0.0,   10.0),   # the speed the gait reads: the legs keep going
    p("craftWobble",        0.30,  0.0,   10.0),
    p("craftWobbleRate",    0.45,  0.0,   10.0),
    p("gapSeconds",         3.2,   0.0,   60.0),   # Time Between Targets
    # 2900 before ADR-218, for a disc of radius 3.6. The disc is now 7.8 -- a 4.7x area -- and a
    # beam that keeps its spawn rate over four times the volume is a haze rather than a beam. The
    # shortfall is made up twice: 2.1x the rate (which the node's capacity had to grow for) and
    # 1.5x the mote, which together restore the coverage at rather less than 4.7x the cost.
    p("beamSpawnRate",   6100.0,   0.0,20000.0),
    p("beamEmissive",       1.55,  0.0,   10.0),
    # Zero rather than the 1050 the scene authored, and the reason is ADR-218: a particle system is
    # *frozen* while it is hidden, not cleared, so a beam that is hidden while it still holds
    # particles resumes them, unaged, wherever the craft is when it is shown again. Emission has to
    # stop and the pool has to be allowed to empty before the beam may be hidden at all.
    p("beamRestRate",       0.0,   0.0,20000.0),
    p("beamRestEmissive",   0.45,  0.0,   10.0),
    # How long the beam is left *enabled*, emitting nothing, before it is hidden. Particles only age
    # while their system is enabled, so this has to be at least the emitter's own `lifetimeMax`,
    # which the scene authors at 5.0 s. It is spent inside the next approach, which lasts at least
    # aimSeconds + approachSeconds = 7.8 s, so it costs the cut nothing.
    p("beamDrainSeconds",   5.0,   0.0,   30.0),
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
        ("cues", [
         od([
            ("role", "actor.beam"),
            # ADR-218. The hide that used to end `depart` happens here instead, once the pool it
            # would have frozen has had `beamDrainSeconds` of enabled time to empty. A beat ends when
            # every one of its cues does, and this one is shorter than the approach it runs beside,
            # so the sequence keeps exactly the timing it had.
            ("steps", [
                od([("kind", "wait"), ("name", "beamDrain"),
                    ("duration", ref("beamDrainSeconds"))]),
                od([("kind", "hide"), ("name", "beamOff")]),
            ]),
         ]),
         od([
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
         ]),
        ]),
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
                    # ADR-218, and `setDesc` now refuses this beat without it. Once the animal takes
                    # its station from where the craft is *drawn*, a craft still taking its own from
                    # the animal closes a loop with the saucer's 2.4 m drift inside it -- the same
                    # shape as ADR-210's 488 m climb, horizontally. A station resolved once has
                    # nothing going round it, and during the lift there is nothing to follow anyway:
                    # the animal is the director's to move.
                    ("hold", True),
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
                    # ADR-218. The beam is a particle node parented to the saucer's *node*, and a
                    # node carries the behaviours' offsets -- hover, drift, bank -- that
                    # `Entity::state().position()` deliberately does not. Measured, the two are up
                    # to 0.92 m apart, so a lift aimed at the simulation's craft rises beside the
                    # column rather than up it. `visual` is the drawn place.
                    ("anchor", "visual"),
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
                # No `hide` here any more: see the note in `approach`. Hiding a system that still
                # holds particles freezes them where they were emitted, and the next `show` -- two
                # hundred metres away and eleven seconds later -- resumes them there, which is
                # exactly the "beam drops from its old position before updating" that was reported.
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
        # Ten, not six. Six was not a find that kept failing -- measured, every cycle succeeds and
        # always did (`abducted 6 of 6`, distinct names). It was simply the scenario retiring: six
        # cycles at roughly 22 s each are spent by about 2:30 of a 3:46 film, after which the saucer
        # goes on flying and never abducts again, which reads as "it rarely picks up animals".
        # At ten it runs the length of the cut and still finishes rather than being cut off, and the
        # targets stay distinct -- so at least ten of the sixteen animals clear the canopy and
        # navigability tests the query applies.
        ("maxCycles", 10),         # Maximum Abductions
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
# ADR-218. The beam has to be able to *contain* what it lifts, and ADR-213 made everything it lifts
# 3.6 times bigger without anybody re-measuring the beam. Measured across the whole cast as authored,
# the widest animal -- a bull -- reaches 5.80 m from the point the director puts on the beam's axis,
# and the residual misalignment after the anchor and hold fixes is 1.42 m; 7.8 m covers both with a
# little to spare and is, not by accident, just inside the saucer's own 7.99 m radius -- so the beam
# now reads as the underside of the craft rather than as a spotlight bolted to it.
BEAM_RADIUS = 7.8
for node in scene["nodes"]:
    if node.get("name") == "visitor-beam":
        node["visible"] = False
        ps = node["particles"]
        ps["extent"] = [BEAM_RADIUS, BEAM_RADIUS, BEAM_RADIUS]
        # The pool has to hold the higher rate for a full lifetime or the beam truncates:
        # 6100/s * 5.0 s = 30500.
        ps["capacity"] = 32768
        ps["spawnRate"] = 0.0   # the rest rate is nothing now; the director lights it
        grow = 1.49             # see beamSpawnRate: the half of the coverage the rate does not do
        ps["sizeStart"] = round(0.44 * grow, 3)
        ps["sizeEnd"] = round(0.05 * grow, 3)
with open(SCENE, "w") as out:
    json.dump(scene, out, indent=1)
print("staging written to %s; now run tools/refresh_scene_fingerprint.py" % SCENE)

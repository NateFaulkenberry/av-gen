"""The cast of Glowmere Valley 3: five aliens, twelve farm animals and two craft.

The revision (the owner's brief §9-12; docs/glowmere-valley-3/revision/03-revision-plan.md §3-4):

  aliens    purposeful, never standing about for long, never walk -> stop -> turn round -> walk back.
            Each has its own longest pause, its own range of paces and a turning circle (ADR-907 to
            909). Those near E4 go and see it; those who hear E5 stop to watch its beam where they
            stand (ADR-930). Grounded: a stride bob of about 0.1 m at a walk, and none on the beat.
  animals   on flat valley ground, walking through their turns. Every animal keeps off ground
            steeper than 10 degrees and turns on a body-sized circle; the five whose homes were on
            17-25 degree flanks are re-homed onto the two flat meadows.
  craft     the saucer (`visitor`) and a smaller second craft, `scout`: the same model at 0.6 scale,
            its own staging actor with its own beam.

The UFO events themselves are no longer written here. The first pass carried one hand-written
scenario, beat by beat, and a set of timeline keys for the horse's light; both are gone. The five
events E1-E5 are set pieces (ADR-928 to 930), placed by a Director plan (`ufo.plan.json`) that the
engine compiles into the project: `ufo.py` is that step, and it runs after the generator has written
the project. What this module owns is what the set pieces need from the scene: the craft, their
beams, the horse's Glow (which the plan's cues drive), and the aliens' ears.

Where an artist finds each part:
  * every alien and animal setting is a labelled parameter: Parameters panel -> entity -> <name>/wander,
    /gait, /decide, /decide/<considerer>; or click the body in the World panel: "How <name> moves and
    behaves" (ADR-907 §8);
  * the scout is a node and an entity of its own (World panel -> Objects -> scout, scout-beam);
  * the set pieces: Director panel -> UFO set pieces, one row each (ADR-929).

The figures behind every value are in docs/glowmere-valley-3/revision/stream-reports/characters.md
("How GV3 should use this") and in the iteration log, revision/phase3/cast.md.
"""

import copy
import math

from . import ufo

# ---- the craft -------------------------------------------------------------------------------------
# Where each craft is while it is not in the film. Every set piece hides its craft at t = 0 and
# takes it with a hidden move to where it appears (ADR-928), so these only need to be off the map.
REST = [140.0, 170.0, 360.0]           # beyond the south rim
SCOUT = "scout"
SCOUT_BEAM = "scout-beam"
SCOUT_SCALE = 0.6
SCOUT_REST = [-140.0, 170.0, -360.0]   # beyond the north rim, where E1 and E3 happen
# A smaller craft is a lighter one (the research's sqrt-of-scale rule, which the saucer's calming
# already follows): its sway is smaller in proportion and its rates quicker by 1/sqrt(scale).
SCOUT_RATE = 1.0 / math.sqrt(SCOUT_SCALE)
# The saucer's hero anchor sits this far below its node (the multicam's authored offset).
HERO_OFFSET = 1.45

# The horse's light (F24). The horse's glTF emits nothing, so an emissiveBoost ramp multiplies zero; a
# Glow's self-glow and rim add light to a surface that has none (FXL, ADR-703). Gold, because it is the
# valley's light the saucer is taking -- the light that comes back, rebuilt, on the drop. It is dark
# at rest: the plan's cues (`horse-glow`, `horse-rim`) raise it on E5's lift and hold it while the
# horse rises and dissolves, so it follows the lift wherever the set piece puts it.
VICTIM = "horse-11"
HORSE_GLOW = {
    "id": "horse-light", "type": "glow", "name": "The horse's light",
    "owner": {"kind": "entity", "name": VICTIM},
    "enabled": True, "order": 0, "style": "", "activation": "always",
    "timing": {"delay": 0.0, "lifetime": 0.0, "fadeIn": 0.0, "fadeOut": 0.0, "windowStart": 0.0,
               "windowSeconds": 6.0, "repeatSeconds": 0.0},
    "parameters": {"gain": 1.0, "tint": [1.0, 0.56, 0.2], "glow": 0.0, "rim": 0.0,
                   "rimColor": [1.0, 0.78, 0.42], "rimPower": 2.5, "spill": False, "recolour": 0.0},
}

# ---- the aliens ------------------------------------------------------------------------------------
ALIENS = ("rook", "tide", "sage", "ember", "vane")
# Every alien turns on a 1.5 m circle while it walks, and its gait may turn as fast as that circle
# allows (ADR-908); a pivot is only ever from rest.
ALIEN_GAIT = {"turnRadius": 1.5, "turnRate": 100.0}
# Each alien its own longest pause and pace range (brief §10: "varied pauses, varied speeds"), in
# multiples of its walk speed (3.07 m/s): the explorer and the restless one quicker, the grove's keeper
# slower. `still` is `maxStillSeconds`: past it, a body that has not moved sets its errand aside and
# walks (ADR-909). A watch of a UFO event is exempt only while the reaction itself holds it.
ALIEN_HABITS = {
    #         longest still (s), the interest considerer, its pace range
    "rook":  {"still": 7.0,  "interest": "roam",  "pace": [0.9, 1.3]},
    "tide":  {"still": 8.0,  "interest": "roam",  "pace": [0.8, 1.2]},
    "sage":  {"still": 8.0,  "interest": "graze", "pace": [0.7, 1.05]},
    "ember": {"still": 6.0,  "interest": "roam",  "pace": [0.9, 1.35]},
    "vane":  {"still": 9.0,  "interest": "watch", "pace": [0.8, 1.15]},
}
# A reaction's own pace range is narrow; its urgency (`urgentSpeed` 1.5, the default) hurries it on top.
REACT_PACE = [0.9, 1.1]
# How long a heard event stays in mind (s): long enough to finish walking to it.
EVENT_MEMORY = 20.0
# The two clips the alien GLBs carry and the first pass never used, as activities: `inspect` a thing on
# the ground, `tinker` with a glow. One-shot clips hold their last frame, so a look lasts about as long
# as its clip: Take_from_floor is 2.33 s and Button_push 1.50 s.
ALIEN_CLIPS = {"inspect": "Take_from_floor", "tinker": "Button_push"}
# Per-alien changes to a considerer: (considerer name) -> {key: value}.
ALIEN_CONSIDERERS = {
    # the explorer crouches to look at what it walked to; the restless one pokes at the glows
    "rook": {"roam": {"activity": "inspect", "dwell": 2.4}},
    "ember": {"roam": {"activity": "tinker", "dwell": 1.6}},
    # tide: no errand to its own feet, and shorter looks
    "tide": {"roam": {"minRange": 14.0, "dwell": 3.5}},
    # sage: the grove is a place it drifts back to, not a leash; 19 m (the audit's tolerance + 10)
    # left `graze` nothing to choose near the grove, 30 m does. Its looks are the longest of the five
    # but not vigils: a 6 s look, decided on at 0.6 Hz, ended five of sage's stands at its 8 s cap
    # (7.2-8.2 s, iteration 2); 4.5 s leaves the next decision inside it.
    "sage": {"grove": {"weight": 0.1, "duration": 4.0}, "graze": {"homeRadius": 30.0, "dwell": 4.5}},
    # vane: shorter looks, at nearer things. And E5 matters more to it than the animals do: the most
    # event-sensitive of the five (0.95) never once faced E5's beam in two films (iterations 2-3,
    # 100 m away), because its `watch` of the animals -- characters weighted 4.8, and the watch's
    # weight raised by `audio.rms` through the loud riser -- outscored a centrepiece worth about
    # weight x 1.04 to it (intensity 0.6 at 100 m, curiosity 1.74). The cut's s28 is "Vane sees it".
    "vane": {"watch": {"dwell": 3.5, "maxRange": 60.0}, "centrepiece": {"weight": 6.0}},
}
# The run band. A faster errand must not cross into a run: sage's runEnter (3.66 m/s) sat below its
# fastest walk plus a hurry, and tide's (3.02) below its plain walk -- the audit's "tide plays Running
# at 0.41x". Both to the band the other three use.
ALIEN_RUN_BAND = {"sage": (4.8, 3.1), "tide": (4.8, 3.1)}

# The stride bob answers the bass on two aliens. The multicam gives ember and vane an entity reaction
# `audio.bass -> liveliness/bounce` (+0.45 on a 0.32 bounce, a 60 ms attack): every bass note lifts the
# drawn body higher off the ground. Measured on iteration 1's film, ember and vane walked 0.18 m
# above the ground on average and up to 0.45 m (0.71 m hurrying) -- about 2.4x their own authored
# bob -- which the evaluator reads, rightly, as floating; with the bounce itself at 0 the reaction
# alone still lifted them 0.22 m at a walk (0.27 m hurrying; build/gv3/cast/float, the `bob0` arm).
# It is the "everything pulses to the beat" the brief's research warns against, on the characters.
# The aliens' other musical reactions (on their interests' weights, their decision rate and tide's
# sway) stay.
ALIEN_REACTIONS_DROPPED = ("liveliness/bounce",)

# The stride bob itself: a style choice, and a reversible one. `liveliness/bounce` is how far the
# drawn body rises from ground contact at the crest of a normal walk (ADR-895: 0.5 (1 - cos 2phi) x
# bounce x speed / stride). The multicam's values made ember and vane rise 0.18 m and rook 0.12 m at
# every step of a 3.07 m/s walk -- measured on iteration 3's film, walking p90 +0.195 (ember), +0.176
# (vane), +0.123 m (rook) -- and the evaluator, whose grounding check reads the drawn root, called
# ember and vane floating in every clip they walk in. Grounded characters with the evaluator in the
# loop is the owner's direction, so the production (the coordinator, 2026-09-27) lowered the three
# springiest to a walk peak of about 0.1 m. Sage (0.18, peak 0.096 m) and tide (0.14, 0.075 m) were
# already there. To put the springier walk back, restore these three values; nothing else depends on
# them. (Old values: ember 0.32, vane 0.32, rook 0.22.)
ALIEN_BOUNCE = {
    #         bounce   walk peak at 3.07 m/s (stride)
    "ember": 0.17,   # 0.098 m (5.35 m)
    "vane":  0.17,   # 0.098 m (5.35 m)
    "rook":  0.18,   # 0.096 m (5.78 m)
}

# ---- what the aliens hear --------------------------------------------------------------------------
# The UFO events the aliens react to (ADR-930: every set-piece moment is a world event), how far each
# carries (intensity 1 at the event, 0 at the radius), and what hearing it makes them do. Two ways,
# one `react` considerer each:
#
#   beam         go and see: walk to within `approach` of the craft, face it, watch. E4 happens on
#                the aliens' own bank of the river, in the meadow they roam, and is heard by the ones
#                near it (80 m: in iteration 1's film, ember, sage and tide).
#   centrepiece  go and see as well: E5's beam is heard across the valley (the characters report's 250
#                m), and every alien that hears it walks to within 18 m of the craft, faces it and
#                watches; a cautious one (sage) steps back first. Until engine-4 this was "stop and
#                watch where you stand" (an `approach` of 250 m, inside which everyone already stands):
#                E5 is on the elder's bank, four aliens are across the river from it, and the action
#                tier's route did not ask whether a goal was on the body's own piece of walkable ground
#                -- iteration 1 sent ember at a goal across the river and it paced the bank for 40 s,
#                walk -> stop -> turn 180 -> walk back eight times through the drop. ADR-932 routes a
#                goal across a divide to the nearest reachable point (the bank) and ADR-933 gives an
#                urgent option one attempt, so an alien across the water walks to its bank and watches
#                from there (navfix's measurement on GV3: ember's reversals 3 -> 0 with go-and-see).
#
# E1 (the far survey), E2 (the flyby) and E3 (the far lift, 250-330 m from every alien) are seen, not
# heard: nobody is near enough to walk to them, and walking 300 m to a light is not purposeful.
REACTIONS = {
    #              the set piece whose beam it hears, its radius (m), and the considerer's settings
    "beam":        ("e4-river-pair", 80.0,
                    {"approach": 18.0, "flee": 10.0, "dwell": 4.0, "fadeSeconds": 12.0, "weight": 1.5}),
    "centrepiece": ("e5-centrepiece", 250.0,
                    {"approach": 18.0, "flee": 10.0, "dwell": 6.0, "fadeSeconds": 10.0, "weight": 1.5,
                     "activity": "observe"}),
}

# Sage's `interest` behaviour (not a considerer: it runs after the decider) stops the body's feet to
# look about, every 1-3 s with some chance. The project restates its dwell as 1.2-3.4 s, but the
# scene's 3-6 s is what runs (a headless save writes 3.0/6.0 back), and at 55 % it chains into
# stands the decider's `maxStillSeconds` cannot break. A glance, not a vigil: brief looks, less often.
SAGE_INTEREST = {"observeChance": 0.25, "minDwell": 1.2, "maxDwell": 3.4, "reactionCooldown": 15.0}

# ---- the animals -----------------------------------------------------------------------------------
# Every animal keeps to gentle ground (the analyser's "steep" is 12 degrees; 10 leaves a margin) and
# walks its turns on a body-sized circle; a turn from rest steps round a small circle with its one clip
# (ADR-907, 908).
ANIMAL_MAX_SLOPE = 10.0
ANIMAL_TURNS = {
    #          wander.turnRadius (m), gait.pivotRadius (m)
    "horse": (2.2, 1.2),
    "cow":   (1.8, 1.0),
    "bull":  (2.0, 1.1),
}
# The flank animals, re-homed onto the two flat meadows near the elder (revision plan §4; the
# characters stream's meadow check: the steepest ground within 16 m of each home is 1.7-5.8 degrees,
# all dry). Each with a smaller territory that sits wholly inside its meadow.
REHOMED = {
    # the 5.1 ha meadow round (74, 13), east of the river
    "horse-2": (74.0, 13.0),
    "horse-20": (90.0, 26.0),
    "horse-22": (60.0, 0.0),
    # the flat half of the 3.9 ha meadow by (-62, 36), west of the river (its edge at (-62, 36) is a bank)
    "cow-12": (-66.0, 0.0),
    "cow-23": (-78.0, 6.0),
}
REHOMED_TERRITORY = {"homeRadius": 16.0, "maxRange": 12.0}


# ---- helpers ---------------------------------------------------------------------------------------
def _entities(scene):
    return {e["name"]: e for e in scene["entities"]}


def _nodes(scene):
    return {n["name"]: n for n in scene["nodes"]}


def _behaviour(entity, kind):
    for b in entity["behaviors"]:
        if b["kind"] == kind:
            return b
    raise KeyError(f"{entity['name']} has no {kind} behaviour")


def _considerer(entity, name):
    for c in _behaviour(entity, "decide")["considerers"]:
        if c.get("name") == name:
            return c
    raise KeyError(f"{entity['name']} has no considerer {name}")


def _species(name):
    return name.split("-")[0]


def _set(project, entity, path, value):
    """Write a setting where it runs: the scene, and the project's parameter when the project restates
    it. The project's `parameters` are applied over the scene (ADR-264), so a value changed in the
    scene alone would be silently undone by the old one in the project."""
    key = f"entity/{entity}/{path}"
    if key in project["parameters"]:
        project["parameters"][key] = value


# ---- the craft -------------------------------------------------------------------------------------
def craft(project, scene):
    """The saucer calmed and at rest, the scout added beside it, and the first pass's hand-written
    scenario taken out: the set pieces are the craft's only direction."""
    nodes = _nodes(scene)
    ents = _entities(scene)
    staging = scene["staging"]
    # The validator refuses a set piece on a craft an autostarting scenario drives (ADR-929).
    staging["scenarios"] = []

    nodes["visitor"]["position"] = list(REST)
    # A massive craft should move like one (the research's sqrt-of-scale rule): no bob on every kick,
    # no spin kicked by every beat. It keeps its slow hover, drift and bank.
    saucer = ents["visitor"]
    saucer["reactions"] = []
    for b in saucer["behaviors"]:
        if b["kind"] == "spin":
            b["signal"] = ""
            b["impulse"] = 0.0
            b["baseRate"] = 0.35

    params = project["parameters"]
    # The first pass's scenario parameters (the multicam's `staging/abduction/*`) restate a scenario
    # that no longer exists; the set pieces register their own under `staging/setpiece/<key>/`.
    for key in [k for k in params if k.startswith("staging/")]:
        del params[key]
    for key in [k for k in params if k.startswith("entity/visitor/spin/")]:
        del params[key]
    # The project's heroes restate the visitor's anchor, and it was 172.6 m from the craft: a hero
    # keeps its authored offset from its node, so the anchor rode two hundred metres off it.
    for hero in project.get("heroes", []):
        if hero.get("name") == "visitor":
            hero["position"] = [REST[0], REST[1] - HERO_OFFSET, REST[2]]

    add_scout(project, scene)


def add_scout(project, scene):
    """The second craft (revision plan §3, "Crafts"): the saucer's model at 0.6 scale, a copy of its
    beam parented to it, an entity for each, and its own staging actor. E1 and E3 are flown by it,
    so the scale of the UFO activity varies and the saucer's own story stays continuous.

    The copy is of what the saucer IS in the film, not only of its scene node: the project restates
    the saucer's and its beam's parameters (the owner set the beam's mouth to 0.42 m in the app), so
    those values are copied to the scout's own paths. Its transform and visibility are the set
    pieces' (ADR-264 strips a scenario body's saved transform), so they are not copied."""
    nodes = _nodes(scene)
    ents = _entities(scene)
    if SCOUT in nodes:
        return

    body = copy.deepcopy(nodes["visitor"])
    body["name"] = SCOUT
    body["position"] = list(SCOUT_REST)
    body["scale"] = [SCOUT_SCALE] * 3
    beam = copy.deepcopy(nodes["visitor-beam"])
    beam["name"] = SCOUT_BEAM
    beam["parent"] = SCOUT
    beam["particles"]["seed"] = 20260927
    order = [n["name"] for n in scene["nodes"]]
    at = order.index("visitor-beam") + 1
    scene["nodes"][at:at] = [body, beam]

    craft_entity = copy.deepcopy(ents["visitor"])
    craft_entity["name"] = SCOUT
    craft_entity["node"] = SCOUT
    craft_entity["seed"] = 20261012
    for b in craft_entity["behaviors"]:
        if b["kind"] == "hover":
            b["amplitude"] = round(b["amplitude"] * SCOUT_SCALE, 4)
            b["rate"] = round(b["rate"] * SCOUT_RATE, 4)
        elif b["kind"] == "drift":
            b["radius"] = round(b["radius"] * SCOUT_SCALE, 4)
            b["rate"] = round(b["rate"] * SCOUT_RATE, 4)
        elif b["kind"] == "spin":
            b["baseRate"] = round(b["baseRate"] * SCOUT_RATE, 4)
    beam_entity = copy.deepcopy(ents["visitor-beam"])
    beam_entity["name"] = SCOUT_BEAM
    beam_entity["node"] = SCOUT_BEAM
    beam_entity["seed"] = 771256
    order = [e["name"] for e in scene["entities"]]
    at = order.index("visitor-beam") + 1
    scene["entities"][at:at] = [craft_entity, beam_entity]

    scene["staging"]["actors"].append(
        {"name": SCOUT, "body": SCOUT, "parts": [{"name": "beam", "entity": SCOUT_BEAM}]})

    # A hero record, as the saucer has one: it is how the project declares a body's size and place to
    # the Director and the evaluator (the Critic's adapter otherwise assumes the saucer's 8.2 m). The
    # saucer's, scaled; less important than the saucer, whose story the film follows. Its anchor is
    # where its node rests, less the saucer's own offset scaled (a hero keeps its authored offset).
    def scout_hero(saucer):
        hero = copy.deepcopy(saucer)
        hero["name"] = SCOUT
        hero["position"] = [SCOUT_REST[0], SCOUT_REST[1] - HERO_OFFSET * SCOUT_SCALE, SCOUT_REST[2]]
        for key in ("radius", "height", "preferredCameraDistance"):
            if key in hero:
                hero[key] = round(hero[key] * SCOUT_SCALE, 3)
        hero["importance"] = round(hero.get("importance", 0.4) * 0.5, 3)
        return hero

    for holder in (scene, project):
        heroes = holder.get("heroes", [])
        saucer = next((h for h in heroes if h.get("name") == "visitor"), None)
        if saucer is not None and not any(h.get("name") == SCOUT for h in heroes):
            heroes.insert(heroes.index(saucer) + 1, scout_hero(saucer))

    # The saucer's restated look, onto the scout's paths. Its motion restated too (hover, drift, bank),
    # scaled as the scene copy is.
    params = project["parameters"]
    skip = ("/position", "/rotation", "/scale", "/visible")
    scaled = {"entity/visitor/hover/amplitude": SCOUT_SCALE, "entity/visitor/hover/rate": SCOUT_RATE,
              "entity/visitor/drift/radius": SCOUT_SCALE, "entity/visitor/drift/rate": SCOUT_RATE}
    for key in sorted(params):
        for old, new in (("nodes/visitor-beam/", f"nodes/{SCOUT_BEAM}/"),
                         ("particles/visitor-beam/", f"particles/{SCOUT_BEAM}/"),
                         ("entity/visitor-beam/", f"entity/{SCOUT_BEAM}/"),
                         ("nodes/visitor/", f"nodes/{SCOUT}/"),
                         ("procedural/visitor/", f"procedural/{SCOUT}/"),
                         ("entity/visitor/", f"entity/{SCOUT}/")):
            if key.startswith(old):
                if key.endswith(skip):
                    break
                value = params[key]
                if key in scaled and isinstance(value, (int, float)):
                    value = round(value * scaled[key], 6)
                params[new + key[len(old):]] = value
                break


# ---- the aliens ------------------------------------------------------------------------------------
def aliens(project, scene):
    """The characters stream's recommended settings, per alien, and the aliens' ears for E4 and E5."""
    ents = _entities(scene)
    for name in ALIENS:
        e = ents[name]
        e["gait"].update(ALIEN_GAIT)
        if name in ALIEN_RUN_BAND:
            e["gait"]["runEnter"], e["gait"]["runExit"] = ALIEN_RUN_BAND[name]
        habits = ALIEN_HABITS[name]
        d = _behaviour(e, "decide")
        d["maxStillSeconds"] = habits["still"]
        d.setdefault("mind", {}).setdefault("memory", {})["eventSeconds"] = EVENT_MEMORY
        for c in d["considerers"]:
            if c["kind"] == "interest":
                c["speedRange"] = list(habits["pace"])
        # The reactions: the scene's one `react` ("beam") becomes the pair above, each hearing one set
        # piece. The new one goes where the old one was, so the considerers keep their order.
        at = next(i for i, c in enumerate(d["considerers"]) if c["kind"] == "react")
        template = d["considerers"][at]
        reactions = []
        for considerer, (key, _radius, settings) in REACTIONS.items():
            c = copy.deepcopy(template)
            c["name"] = considerer
            c["events"] = [ufo.event_name(key, "beam")]
            c["speedRange"] = list(REACT_PACE)
            c.update(settings)
            reactions.append(c)
        d["considerers"][at:at + 1] = reactions
        for considerer, changes in ALIEN_CONSIDERERS.get(name, {}).items():
            c = _considerer(e, considerer)
            for key, value in changes.items():
                c[key] = value
                _set(project, name, f"decide/{considerer}/{key}", value)
        e["clips"].update(ALIEN_CLIPS)
        e["reactions"] = [r for r in e.get("reactions", []) if r.get("target") not in ALIEN_REACTIONS_DROPPED]
        if name in ALIEN_BOUNCE:
            _behaviour(e, "liveliness")["bounce"] = ALIEN_BOUNCE[name]
            _set(project, name, "liveliness/bounce", ALIEN_BOUNCE[name])

    sage = next(b for b in ents["sage"]["behaviors"] if b["kind"] == "interest")
    for key, value in SAGE_INTEREST.items():
        sage[key] = value
        _set(project, "sage", f"interest/{key}", value)

    # How far each heard event carries. The first pass's `abduction/beam` belonged to the hand-written
    # scenario, which is gone; nothing raises it any more.
    events = [w for w in scene.get("worldEvents", []) if not w["name"].startswith("abduction/")]
    for key, radius, _settings in REACTIONS.values():
        events.append({"name": ufo.event_name(key, "beam"), "radius": radius, "magnitude": 1.0})
    scene["worldEvents"] = events


# ---- the animals -----------------------------------------------------------------------------------
def animals(project, scene, ground):
    """Every animal on gentle ground with walk-through turns; the flank animals re-homed onto the
    meadows. `ground` is the engine's own height (gv3.ground), so a re-homed body starts on the
    surface it will stand on."""
    ents = _entities(scene)
    nodes = _nodes(scene)
    for name, e in ents.items():
        species = _species(name)
        if species not in ANIMAL_TURNS:
            continue
        radius, pivot = ANIMAL_TURNS[species]
        w = _behaviour(e, "wander")
        w["maxSlope"] = ANIMAL_MAX_SLOPE
        w["turnRadius"] = radius
        e.setdefault("gait", {})["pivotRadius"] = pivot
        _set(project, name, "wander/maxSlope", ANIMAL_MAX_SLOPE)
        _set(project, name, "wander/turnRadius", radius)
        _set(project, name, "gait/pivotRadius", pivot)
    for name, (x, z) in REHOMED.items():
        nodes[name]["position"] = [x, round(ground.height(x, z), 3), z]
        w = _behaviour(ents[name], "wander")
        for key, value in REHOMED_TERRITORY.items():
            w[key] = value
            _set(project, name, f"wander/{key}", value)


# ---- the horse's light -----------------------------------------------------------------------------
def horse_light(project):
    """The Glow on the horse, dark at rest. The plan's cues raise its self-glow and rim on E5's lift
    (ufo.plan.json, `horse-glow` and `horse-rim`); the first pass's timeline keys are gone, and so is
    any key a previous generation left on its two fields."""
    project["effects"] = [e for e in project.get("effects", []) if e["id"] != HORSE_GLOW["id"]] + [
        copy.deepcopy(HORSE_GLOW)]
    tl = project.setdefault("timeline", {"enabled": True, "cues": [], "tracks": []})
    tl["tracks"] = [t for t in tl.get("tracks", []) if not t["target"].startswith(f"fx/{HORSE_GLOW['id']}/")]


# ---- entry point -----------------------------------------------------------------------------------
def apply(project, scene):
    from .ground import Ground

    world = next(n["world"] for n in scene["nodes"] if "world" in n)
    craft(project, scene)
    aliens(project, scene)
    animals(project, scene, Ground(world))
    horse_light(project)

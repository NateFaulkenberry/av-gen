"""The visitor's story, written as the engine's own staging (src/stage/), and the cast around it.

One scenario carries the whole of the saucer's part in the film, beat by beat against the music:

  rest      hidden, off the map, until the first pull-back
  flyby     26.3 s, the first pull-back: a shape crosses the sky while the music holds its breath --
            the promise the drop keeps two and a half minutes later
  away      hidden again, moved to where it will come from
  hold      until the suspension (148.2 s), when the music strips its shimmer away
  acquire   binds horse-11, the white horse that grazes beside the elder
  approach  a slow, heavy 21-second approach across the break (big things move slowly)
  beam      lights at the riser (170.3 s); the aliens hear `abduction/beam` and come to watch
  abduct    the lift through the riser; the horse glows and dissolves ON the drop (177.7 s)
  depart    the beam drains
  leave     up and away over the north rim, two bars into the drop

Every duration is a scenario parameter so a render can be retimed without touching a beat. The
beats are sequential and each hand-off costs a frame, so times drift by a frame or two per beat;
`tools/cast_trace.cpp` measures where they actually land and the shot plan is cut to that.
"""

from . import music

# Where the saucer is while it is not in the film, and the line it draws across the sky in the
# flyby: from over the east wall to beyond the west rim, high enough to read as sky, low enough to
# pass through a wide lens's frame looking up the valley.
REST = [140.0, 170.0, 360.0]
# Iteration 2: lower again and shorter, so it crosses a camera 135 m away inside the frame, above the
# elder's cap, for about 1.4 s. The walls rise at the ends (ground 39 m at x=220, 49 m at x=-240 on
# this line), so the path stays at least 36 m above them.
FLYBY_FROM = [220.0, 78.0, 70.0]
FLYBY_TO = [-240.0, 86.0, 80.0]
APPROACH_FROM = [60.0, 150.0, 330.0]   # far south, beyond the south rim, where it comes from
VICTIM = "horse-11"
# Iteration 2: straight up at 134 m/s left every frame in a second. Up and away over the north rim, at
# about 76 m/s over 8 s, the saucer climbs beside the elder and shrinks toward the aurora.
EXIT = [-250.0, 420.0, -320.0]

# The horse's light (F24). The source scenario ramped the horse's `emissiveBoost` to 6.5 through the
# lift, and a boost multiplies a material's emission: the horse's glTF emits nothing, so the ramp
# multiplied zero and the horse went up the beam as a pale shape inside a paler column. A Glow's
# self-glow and rim add light to a surface that has none (FXL, ADR-703). Gold, because it is the
# valley's light the saucer is taking -- the light that comes back, rebuilt, on the drop.
LIFT_END = 177.70     # measured by avgen_cast_trace: the horse retires 11 ms before the crash
LIFT_RISE = 1.3       # the glow comes up over the first 1.3 s of the lift
HORSE_GLOW = {
    "id": "horse-light", "type": "glow", "name": "The horse's light",
    "owner": {"kind": "entity", "name": VICTIM},
    "enabled": True, "order": 0, "style": "", "activation": "always",
    "timing": {"delay": 0.0, "lifetime": 0.0, "fadeIn": 0.0, "fadeOut": 0.0, "windowStart": 0.0,
               "windowSeconds": 6.0, "repeatSeconds": 0.0},
    "parameters": {"gain": 1.0, "tint": [1.0, 0.56, 0.2], "glow": 0.0, "rim": 0.0,
                   "rimColor": [1.0, 0.78, 0.42], "rimPower": 2.5, "spill": False, "recolour": 0.0},
}
HORSE_GLOW_PEAK = {"glow": 3.0, "rim": 5.0}

# Musical times the beats are written against (seconds).
FLYBY_START = music.bar(15) + 0.25      # just after the pull-back begins
FLYBY_SECONDS = 3.2
APPROACH_START = music.bar(81) + 0.1    # the suspension
LEAVE_AT = music.bar(99)                # two bars into the drop


def scenario():
    """The staging scenario, as JSON."""
    p = {
        # when
        "restSeconds": round(FLYBY_START, 3),
        "flybySeconds": FLYBY_SECONDS,
        "awaySeconds": 0.1,
        # hold runs from the end of the flyby to the start of the approach
        "holdSeconds": round(APPROACH_START - (FLYBY_START + FLYBY_SECONDS + 0.1), 3),
        # the approach: a slow craft, 21 s from beyond the rim to its station
        "hoverHeight": 23.0, "cruiseClearance": 34.0, "travelSpeed": 30.0,
        "approachSeconds": 21.0, "aimSeconds": 0.8,
        # the beam and the lift, landing the dissolve on the drop
        "hoverSeconds": 2.4, "beamSeconds": 1.1, "beamFadeSeconds": 0.15,
        # the trace put the dissolve at 177.95 with 5.2 s; 4.91 lands it 50 ms before the crash
        "abductSeconds": 4.91, "liftHeight": -3.5502, "animalSpin": 90.0, "animalWobble": 0.0,
        "animalWobbleRate": 1.35, "animalGait": 0.7003, "craftWobble": 0.3, "craftWobbleRate": 0.45,
        "beamSpawnRate": 4200.0, "beamEmissive": 0.7, "beamSize": 1.0, "beamRestRate": 0.0,
        "beamRestEmissive": 0.45, "beamDrainSeconds": 5.0,
        "fadeDelaySeconds": 3.51, "fadeSeconds": 1.4,
        # the exit
        "gapSeconds": 3.7, "leaveSeconds": 8.0,
    }
    params = [{"name": k, "value": v, "min": 0.0, "max": max(1000.0, abs(v) * 2.0)} for k, v in p.items()]
    for q in params:
        if q["name"] == "liftHeight":
            q["min"] = -20.0
            q["max"] = 20.0

    def par(name):
        return {"param": name}

    beats = [
        {"name": "rest", "cues": [{"role": "actor", "steps": [
            {"kind": "hide", "name": "unseen"},
            {"kind": "wait", "name": "rest", "duration": par("restSeconds")}]}], "then": "flyby"},
        {"name": "flyby", "cues": [{"role": "actor", "steps": [
            {"kind": "moveTo", "name": "enter", "point": FLYBY_FROM, "duration": 0.05},
            {"kind": "show", "name": "seen"},
            {"kind": "moveTo", "name": "cross", "point": FLYBY_TO, "duration": par("flybySeconds")},
            {"kind": "hide", "name": "gone"}]}], "then": "away"},
        {"name": "away", "cues": [{"role": "actor", "steps": [
            {"kind": "moveTo", "name": "withdraw", "point": APPROACH_FROM, "duration": par("awaySeconds")}]}],
         "then": "hold"},
        {"name": "hold", "cues": [{"role": "actor", "steps": [
            {"kind": "wait", "name": "hold", "duration": par("holdSeconds")}]}], "then": "acquire"},
        {"name": "acquire", "find": [{"bind": "target", "name": VICTIM, "claim": True}], "cues": [],
         "then": "approach", "otherwise": "acquire"},
        {"name": "approach", "cues": [
            {"role": "actor.beam", "steps": [{"kind": "hide", "name": "beamOff"}]},
            {"role": "actor", "steps": [
                {"kind": "show", "name": "arrive"},
                {"kind": "lookAt", "name": "aim", "to": "target", "duration": par("aimSeconds")},
                {"kind": "moveTo", "name": "approach", "to": "target", "height": par("hoverHeight"),
                 "aboveGround": True, "speed": par("travelSpeed"), "duration": par("approachSeconds"),
                 "clearance": par("cruiseClearance")}]}], "then": "beam"},
        {"name": "beam", "stillRoles": ["actor"], "cues": [
            {"role": "target", "steps": [{"kind": "follow", "name": "caught", "relative": True, "hold": True,
                                          "duration": par("hoverSeconds")}]},
            {"role": "actor", "steps": [{"kind": "follow", "name": "hover", "relative": True, "hold": True,
                                         "duration": par("hoverSeconds"), "wobble": par("craftWobble"),
                                         "wobbleRate": par("craftWobbleRate")}]},
            {"role": "actor.beam", "steps": [
                {"kind": "show", "name": "beamOn"},
                {"kind": "set", "name": "beamRise", "target": "spawnRate", "to": par("beamSpawnRate"),
                 "duration": par("beamSeconds")},
                {"kind": "set", "name": "beamGlow", "target": "emissive", "to": par("beamEmissive"), "duration": 0.2}]},
            {"role": "actor.beam", "steps": [
                {"kind": "set", "name": "beamWidth", "target": "size", "from": 0.0, "to": par("beamSize"),
                 "duration": par("beamSeconds")}]}], "then": "abduct"},
        {"name": "abduct", "cues": [
            {"role": "actor", "steps": [{"kind": "follow", "name": "hold", "relative": True, "hold": True,
                                         "duration": par("abductSeconds"), "wobble": par("craftWobble"),
                                         "wobbleRate": par("craftWobbleRate")}]},
            {"role": "target", "steps": [{"kind": "moveTo", "name": "lift", "to": "actor.beam", "anchor": "drawn",
                                          "place": "drawn", "height": par("liftHeight"),
                                          "duration": par("abductSeconds"), "spin": par("animalSpin"),
                                          "wobble": par("animalWobble"), "wobbleRate": par("animalWobbleRate"),
                                          "rate": par("animalGait")}]},
            {"role": "target", "steps": [
                {"kind": "wait", "name": "rise", "duration": par("fadeDelaySeconds")},
                {"kind": "set", "name": "fade", "target": "opacity", "from": 1.0, "to": 0.0,
                 "duration": par("fadeSeconds"), "ease": True},
                {"kind": "retire", "name": "vanish"}]}], "then": "depart"},
        {"name": "depart", "release": True, "cues": [
            {"role": "actor.beam", "steps": [
                {"kind": "hide", "name": "beamOut"},
                {"kind": "set", "name": "beamFall", "target": "spawnRate", "to": par("beamRestRate"), "duration": 0.0},
                {"kind": "set", "name": "beamCut", "target": "size", "to": 0.0, "duration": par("beamFadeSeconds")},
                {"kind": "set", "name": "beamDim", "target": "emissive", "to": par("beamRestEmissive"),
                 "duration": 0.3}]},
            {"role": "actor", "steps": [{"kind": "wait", "name": "beat", "duration": par("gapSeconds")}]}],
         "then": "leave"},
        {"name": "leave", "cues": [{"role": "actor", "steps": [
            # An absolute point: a `relative` goal is re-resolved from the moving body every frame
            # and would climb away from the saucer as fast as it climbed.
            {"kind": "moveTo", "name": "ascend", "point": EXIT, "duration": par("leaveSeconds")},
            {"kind": "hide", "name": "departed"}]}]},
    ]
    return {"name": "abduction", "actor": "saucer", "seed": 20260915, "autoStart": True, "maxCycles": 1,
            "searchInterval": 0.5, "params": params, "beats": beats}


def apply(project, scene):
    """The scenario into the scene (staging is read from the scene only), the craft calmed, and the
    project's restatement of the old scenario's parameters removed so the scene is the one truth."""
    staging = scene["staging"]
    staging["scenarios"] = [scenario()]
    # The saucer starts where it rests: out of the valley, hidden by its first beat.
    for node in scene["nodes"]:
        if node["name"] == "visitor":
            node["position"] = REST
    # A massive craft should move like one (the research's sqrt-of-scale rule): no bob on every
    # kick, no spin kicked by every beat. It keeps its slow hover, drift and bank.
    for ent in scene["entities"]:
        if ent["name"] == "visitor":
            ent["reactions"] = []
            for b in ent["behaviors"]:
                if b["kind"] == "spin":
                    b["signal"] = ""
                    b["impulse"] = 0.0
                    b["baseRate"] = 0.35
    params = project["parameters"]
    for key in [k for k in params if k.startswith("staging/")]:
        del params[key]
    for key in [k for k in params if k.startswith("entity/visitor/spin/")]:
        del params[key]
    # The project's heroes restate the visitor's anchor, and it was 172.6 m from the craft: a hero
    # keeps its authored offset from its node, so the anchor rode two hundred metres off it.
    for hero in project.get("heroes", []):
        if hero.get("name") == "visitor":
            hero["position"] = [REST[0], REST[1] - 1.45, REST[2]]
    horse_light(project)


def horse_light(project):
    """The Glow on the horse, and its keys: dark until the lift, up over its first 1.3 s, held while
    the horse fades into the saucer, gone with it."""
    from .look import track
    project["effects"] = [e for e in project.get("effects", []) if e["id"] != HORSE_GLOW["id"]] + [HORSE_GLOW]
    lift = scenario_value("abductSeconds")
    start = LIFT_END - lift
    for leaf, peak in HORSE_GLOW_PEAK.items():
        keys = [(0.0, 0.0, "linear"), (start, 0.0, "easeInOut"), (start + LIFT_RISE, peak, "linear"),
                (LIFT_END - 0.05, peak, "linear"), (LIFT_END, 0.0, "linear")]
        project["timeline"]["tracks"].append(track(f"fx/{HORSE_GLOW['id']}/{leaf}", keys))


def scenario_value(name):
    for q in scenario()["params"]:
        if q["name"] == name:
            return q["value"]
    raise KeyError(name)

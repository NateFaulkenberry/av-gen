#!/usr/bin/env python3
"""Musical Geography: assemble the scene and the project from look.py (art direction) and world.py (the land).

    python3 examples/musical-geography/build.py [--state day|sunset|night|dawn] [--interim]
                                                [--camera low|reveal|land|bay|topdown|overview] [--out DIR]

--state      the palette state: pins DayNight's phase there, and bakes that state's terrain colours into the tile
             program (until request 1972 makes them curves).
--interim    the pre-generator stand-in (world.interim_nodes) instead of the "tiles" rings.
--camera     a still camera for look development (the flight comes in Phase 5).

Writes <out>/musical-geography.scene.json and <out>/musical-geography.json (both gitignored). Pure Python.
"""
from __future__ import annotations

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import look   # noqa: E402
import world  # noqa: E402
import arc    # noqa: E402

# Still cameras for look development (engine axes, y up, north = -z). The camera's y and the target's y are ABOVE THE
# GROUND there (terrain_mirror) when "ground" is set. The seed-3 world: a deep bay at the origin opening south, a
# ridge on its west shore, hills to the north-west and north-east (terrain_mirror.py --map).
CAMERAS = {
    "low": {"position": [60.0, 4.0, -300.0], "target": [40.0, -6.0, -700.0], "fov": 50.0},          # over the bay
    "reveal": {"position": [-900.0, 90.0, 300.0], "target": [-200.0, 0.0, -350.0], "fov": 36.0, "ground": True},
    "land": {"position": [-120.0, 7.0, -640.0], "target": [-420.0, 0.0, -880.0], "fov": 40.0, "ground": True},
    "bay": {"position": [60.0, 60.0, -250.0], "target": [-150.0, 0.0, -800.0], "fov": 40.0},
    "topdown": {"position": [-60.0, 150.0, -640.0], "target": [-60.0, 0.0, -641.0], "fov": 40.0, "ground": True},
    "overview": {"position": [0.0, 900.0, 0.5], "target": [0.0, 0.0, 0.0], "fov": 70.0},
    # CP-A composed candidates (frame_preview.py): less sky; the reveal frames coast, hills and path; the coast view
    # sets land against sea
    "revealC": {"position": [60.0, 120.0, -430.0], "target": [-118.0, 0.0, -668.0], "fov": 36.0},
    "coastC": {"position": [-380.0, 22.0, 120.0], "target": [-700.0, -40.0, -260.0], "fov": 40.0},
    "valleyC": {"position": [190.0, 95.0, -400.0], "target": [300.0, -40.0, -800.0], "fov": 36.0},
    "lowC": {"position": [-60.0, 25.0, -600.0], "target": [-330.0, -8.0, -860.0], "fov": 40.0, "ground": True},  # ~25 m up
    "nightC": {"position": [-60.0, 45.0, -560.0], "target": [-330.0, -30.0, -870.0], "fov": 40.0},
}


# Ambient-occlusion fade band per view, metres (engineering's suggestion): the reveal's near ground starts 200 m out.
AO_BANDS = {"revealC": (200.0, 420.0), "valleyC": (120.0, 300.0)}


def camera(name):
    c = dict(CAMERAS[name])
    if c.pop("ground", False):
        import numpy as np
        import terrain_mirror
        for key in ("position", "target"):
            x, y, z = c[key]
            h = float(terrain_mirror.tile_sample(world.TILES, np.array([x]), np.array([z]))["height"][0])
            c[key] = [x, round(h + y, 2), z]
    return c


TILE_SCALE = 4.0   # owner, 2026-11-09: 4 m tiles (--tile-scale overrides)


# ---- the CP-B flight (ADR-1956), in metres: bay -> low over the moving sea -> up the river valley -> climb to the reveal.
# The camera looks where it travels (lookAhead), never above the horizon (pitchMax 0), a little down (pitchBias), so the
# climb keeps the land in frame; banks only from the turns. Tile-scale independent.
FLIGHT = {
    "ground": "mgTiles1", "startTime": 30.0, "duration": 90.0, "speed": 22.0, "fov": 46.0,
    "route": [
        # CP-B r2 (owner): fly like a low plane -- forward horizon view, banked sweeping turns, smooth terrain-following
        # rises and falls, never looking straight down, a 40 m ceiling so the tiles always read (04- §13)
        {"position": [60, -250], "altitude": 12},                              # the bay
        {"position": [60, -430], "altitude": 6},                               # low over the moving sea
        {"snap": "shore", "position": [40, -600], "snapRadius": 40, "altitude": 9},    # cross the shore
        {"follow": "path", "from": 0.0, "to": 0.5, "offset": 0, "altitude": 10},       # along the lit path
        {"position": [-330, -1020], "altitude": 22, "speed": 24},              # rise over the crowns (r3: 28 -> 22)
        {"position": [-180, -1180], "altitude": 17, "speed": 26},              # a banked sweep north-east (22 -> 17)
        {"position": [120, -1060], "altitude": 14, "speed": 26},               # sweep back down toward the river (18 -> 14)
        {"follow": "river", "from": 0.4294, "to": 0.7026, "offset": 0, "altitude": 10},   # up the valley on the river (cpd-r4: the same stretch; the river now runs on to the north sea)
        {"position": [300, -1560], "altitude": 35, "speed": 24},               # a last rise, under the ceiling
    ],
    "minClearance": 4, "clearanceRadius": 10, "amplitudeHeadroom": 2,
    "climbRate": 6, "fallRate": 10, "lookAhead": 3.5, "pitchBias": 0.08,
    "pitchMin": -14, "pitchMax": 0, "bankLimit": 16, "bankGain": 2.2, "loop": False,
}
FLY = False   # set by --flight
FULL = False  # set by --full
CLOUDS = False  # set by --clouds (CP-D r2) (CP-D: the whole song, the arc on the timeline)

# ---- CP-D: the whole song as ONE flight (owner approved CP-C r3 "for now"). The approved CP-B/C legs keep their
# shape and their song time (30 -> ~120 s); the crown leg flies at 14 m (it was 22: the aliens there were small) and the
# sweep at 15. New: an intro run north up the open bay (0-30 s, day), and after the river the night climax and the dawn
# outro: east along the north coast and across the inlet's moving sea (the lift into night), up onto the eastern ridge
# ... no: the ridge is 1.5 km past the river and out of the song's reach. The climax is the north coast's hills,
# the inlet's sea and the far shore under the night lines; the outro banks north down the low peninsula and out over
# the northern sea as the sun comes up. Song times from the scratch flight mirror (04- §13 CP-D).
FLIGHT_FULL = dict(FLIGHT, startTime=0.0, duration=210.5, route=[
    # draft2 (coordinator: 0-36 s was the same open sea in every frame): the bay opens the piece for ~10 s, then the
    # flight banks onto the bay's west shore and runs along its hills before swinging east to the approved route
    {"position": [-190, 200], "altitude": 12, "speed": 24},               # the open bay: the intro
    {"position": [-445, 10], "altitude": 10, "speed": 26},                # onto the west shore by ~11 s
    {"position": [-380, -210], "altitude": 12, "speed": 28},              # along its hills, the bay on the right
    {"position": [60, -250], "altitude": 12},                              # the bay (the approved route from here)
    {"position": [60, -430], "altitude": 6},                               # low over the moving sea
    {"snap": "shore", "position": [40, -600], "snapRadius": 40, "altitude": 9},    # cross the shore
    {"follow": "path", "from": 0.0, "to": 0.85, "offset": 0, "altitude": 5},       # along the lit path (cpd-r4: to the pond, low: close pass 2)
    {"position": [-330, -1020], "altitude": 14, "speed": 24},              # over the crowns (CP-D: 22 -> 14)
    {"position": [-180, -1180], "altitude": 15, "speed": 26},              # the banked sweep north-east (17 -> 15)
    {"position": [-87, -1157], "altitude": 4},                             # cpd-r4 close pass 3 (on the curve, low)
    {"position": [6, -1112], "altitude": 4},
    {"position": [120, -1060], "altitude": 14, "speed": 26},               # sweep back down toward the river
    {"follow": "river", "from": 0.4294, "to": 0.7026, "offset": 0, "altitude": 10},   # up the valley on the river (cpd-r4: the same stretch; the river now runs on to the north sea)
    {"position": [300, -1560], "altitude": 30, "speed": 26},               # rise out of the valley at dusk
    {"position": [356, -1588], "altitude": 8},                             # cpd-r4 close pass 4: drop to the coast
    {"position": [456, -1620], "altitude": 4},
    {"position": [660, -1650], "altitude": 14, "speed": 27},               # east along the north coast
    {"position": [1060, -1580], "altitude": 8, "speed": 27},               # low across the inlet's moving sea: night
    {"position": [1400, -1480], "altitude": 14, "speed": 27},              # the far shore's low hills, the peak
    {"position": [1534, -1635], "altitude": 4},                            # cpd-r4 close pass 5
    {"position": [1560, -1720], "altitude": 6, "speed": 26},               # bank north along the east shore: dawn (12 -> 6)
    {"position": [1500, -2020], "altitude": 10, "speed": 24},              # down the peninsula
    {"position": [1420, -2320], "altitude": 8, "speed": 22},               # out low over the northern sea; the fade
])

# ---- the inhabitants (Phase 6, ADR-1954/1955/1957/1958): Glowmere aliens on the hop behaviour, spread along the whole
# CP-B r2 route -- sea slabs, the shore and path, the crowns, the sweep, the river banks -- so every leg passes some.
# They are passed, never framed (the camera rule). Referenced from assets/aliens (commercial; never copied).
ALIENS_DIR = os.path.join(HERE, "..", "..", "assets", "aliens")
KINDS = ["scout", "ranger", "elder", "pilot", "diver", "trooper"]
# (x, z, habitat): beside the flight line so they cross the lower frame. The sea and path legs (r2, approved): 8-16 m
# off. CP-C r3: the hills, sweep and river legs (clip +43..+79 s) fly higher, so those aliens sit 6 m off the line
# (11 m on the river, off the water) on the INSIDE of each turn -- the side the camera banks toward -- placed on a
# numpy mirror of the flight table (scratch track.py), and held near home (HOME_RADIUS) so they cannot hop away.
# cpd-r4 (owner: the aliens "feel too intentionally placed along the camera path ... they don't ever look big enough"):
# the cast lives in the land, not along the line. Five close-pass aliens sit 6-9 m off the flight line where it flies
# low (~50, 64, 101, 148, 196 s), held near home (3 m) so they cannot walk under the lens; the rest are scattered
# groups (2-4) and loners, 30-250 m off the route, on slopes, ridges and the bay islands, with gaps where nobody is.
# Placed on the flight mirror and terrain_mirror (dry, off the banks); planning: scratch r4/scatter.py (seed 7).
CAST = [
    (68, -440, "pass"),
    (-63, -695, "pass"),
    (-36, -1126, "pass"),
    (426, -1619, "pass"),
    (1556, -1654, "pass"),
    (-225, -952, "wild"),
    (-237, -958, "wild"),
    (62, -1306, "wild"),
    (69, -1301, "wild"),
    (61, -1296, "wild"),
    (290, -1254, "wild"),
    (272, -1280, "wild"),
    (238, -1523, "wild"),
    (203, -1530, "wild"),
    (217, -1530, "wild"),
    (1577, -1489, "wild"),
    (1569, -1491, "wild"),
    (-397, -791, "wild"),
    (-300, -1210, "ridge"),
    (-138, -1147, "wild"),
    (135, -1316, "ridge"),
    (497, -1465, "wild"),
    (603, -1750, "wild"),
    (590, -1555, "ridge"),
    (1484, -1873, "wild"),
    (1689, -1722, "ridge"),
    (60, 10, "sea"),
    (228, -200, "sea"),
]
CAST_FULL = []   # cpd-r4: one cast for every mode (was the route-side additions of the full piece)
HOME_RADIUS = {"sea": 24.0, "pass": 3.0, "wild": 24.0, "ridge": 14.0}   # cpd-r4: a close-pass alien stays by the line, never on it
# night (CP-C r3): the aliens were dark silhouettes on the lit path. Each one carries a small shadowless point light in
# the night accent (the path pink) riding its node above the head -- a pink rim on head and shoulders and a faint pool
# on the slabs it stands on, so it reads off the path -- and its own emissive lamps are lifted a little.
TOSS_GAIN, TOSS_MAX, TOSS_THRESHOLD = 14.0, 10.0, 0.4   # cpd-r3 final: the crest throw on the music (ADR-1998); was 30 / 11 / 0.2
LAND_STRENGTH = 0.5   # cpd-r3 final: the landing spring (was 1.2): the springs, not the swell, moved a resting alien's tile
HOP_CHANCE = 0.0     # cpd-r3 final: no random hops (was 0.05); the wave throws, and the walk still hops up steps
AVOID_BANKS = 0.3     # ADR-1999: about a tile back from the inland water
NIGHT_GLOW = 2.0
NIGHT_HALO = {"color": "#FF4FA8", "intensity": 60.0, "range": 7.0, "height": 1.5}   # height in node units (x1.94 m)


def cast_lights(state):
    if state != "night" and not FULL:
        return []
    h = NIGHT_HALO
    return [{"id": "halo%02d" % i, "name": "halo%02d" % i, "type": "point", "node": "alien%02d" % i,
             "position": [0.0, h["height"], -0.3], "color": look.hexlin(h["color"]), "intensity": h["intensity"] if state == "night" else 0.0,
             "range": h["range"], "castsShadow": False}
            for i in range(len(cast()))]
PREFS = {  # where each habitat's aliens like to go (ADR-1997: nobody stands on water, so no preferWater)
    "sea": {"preferCrowns": 0.2, "preferPath": 0.1},
    "pass": {"preferCrowns": 0.0, "preferPath": 0.0},
    "wild": {"preferCrowns": 0.3, "preferPath": 0.2},
    "ridge": {"preferCrowns": 1.0, "preferPath": 0.0},
}
# the landing spring, sized to read at cruise height on 4 m tiles (CP-C used depth 0.7 / ripple 5 m at 1 m)
# the landing spring: the original (owner, CP-D r2: eng's soft 0.7 Hz / 0.35 arm rejected -- "the other bounces look much
# better"). glowRadius (ADR-1960): the landing glow stays near the tile it lit.
SPRING = {"depth": 1.1, "frequency": 1.4, "damping": 0.16, "rippleSpeed": 16.0, "rippleRadius": 14.0, "glowRadius": 3.0}
CHARACTERS = False   # set by --characters


def cast():
    return CAST + (CAST_FULL if FULL else [])


def cast_nodes(state="day"):
    nodes, ents = [], []
    for i, (x, z, hab) in enumerate(cast()):
        name = "alien%02d" % i
        nodes.append({"name": name, "kind": "gltf",
                      "asset": os.path.abspath(os.path.join(ALIENS_DIR, "alien-%s.glb" % KINDS[i % len(KINDS)])),
                      "position": [float(x), 0.0, float(z)], "rotation": [0.0, (i * 47) % 360 * 1.0, 0.0],
                      "scale": [1.94, 1.94, 1.94], "emissiveBoost": NIGHT_GLOW if state == "night" else 1.0,
                      "animation": {"state": "Idle", "blend": 0.25, "speed": 1.0, "updateHz": 30,
                                    "nearDistance": 40.0, "farHz": 20.0, "cullDistance": 600.0}})
        ents.append({"name": name, "node": name, "seed": 7001 + 97 * i, "tags": ["alien"],
                     "clips": {"idle": "Idle", "walk": "Walking", "run": "Running", "turn": "Idle_turn",
                               "observe": "Idle", "react": "Fight_idle", "jump": "Jumping", "fall": "Floating",
                               "land": "Landing"},
                     "gait": {"walkSpeed": 1.7, "runSpeed": 3.6},
                     # cpd-r3 (ADR-1997): the cast walks (and sometimes runs) between goals, and the swell's crests
                     # THROW them (owner: "throw the aliens into the air"): launch speed = tossGain x the crest's rise,
                     # capped by tossMax; gain 30 / cap 11 m/s throws 2.4-6.2 m (p10-p90, median 3.9; eng's 16 / 9 measured 0.6-2.5, median 1.0)
                     "behaviors": [dict({"kind": "hop", "hopChance": HOP_CHANCE, "tossThreshold": TOSS_THRESHOLD, "tossGain": TOSS_GAIN,
                                         "tossMax": TOSS_MAX, "tossCooldown": 3.0, "homeRadius": HOME_RADIUS[hab],
                                         "goalMin": 2, "goalMax": 6, "pauseMin": 0.8, "pauseMax": 3.0,
                                         "airTime": 1.3, "apex": 3.0, "strength": LAND_STRENGTH,
                                         "avoidBanks": AVOID_BANKS}, **PREFS[hab])],
                     "reactions": [dict(r) for r in look.ALIEN_REACTIONS]})   # Phase 7: hop on the beat
    return nodes, ents


def scene_doc(state, interim, camera_name):
    camera_pose = camera(camera_name)
    s = look.STATES[state]
    env = {
        "intensity": s["amb_int"],
        "background": look.hexlin(s["sky_hor"]),
        "proceduralSkyBackground": True,
        "skybox": True,
        "shadowRange": 480.0,   # the low suns' long shadows reach the reveal's middle ground (+0.7 ms; 220 changed no pixel there)
        "depthPrepass": True,
        "horizonFill": 1.0,   # ADR-1962: below the horizon the sky is the fog at infinity -- no band past the tiles
        "aoRadius": 1.0,   # 4 m tiles (engineering: the gaps are 0.34 m; 0.6 at 1 m); Phase 5: the automatic radius (5% of the look-ahead distance) welled every tile gap
        # GTAO, faded out with distance (ADR-1951): it darkened every far step riser into dark dashes (97fe0568).
        # The band is per view (AO_BANDS): near ground only.
        "ambientOcclusion": True,
        # the remaining dark dashes were screen-space contact shadows marching across far risers (engineering, CP-A r7,
        # canaried): off. The sun's shadow map still shades the hills. Saves about 7.4 ms.
        "contactShadows": False,
        "aoFadeStart": AO_BANDS.get(camera_name, (40.0, 120.0))[0],
        "aoFadeEnd": AO_BANDS.get(camera_name, (40.0, 120.0))[1],
        "sky": look.sky(state),
        "dayNight": look.day_night(phase=s["phase"]),
        "tileDynamics": look.tile_dynamics(state),
    }
    env.update(look.fog_medium(state))
    if interim:
        nodes, programs = world.interim_nodes(), [look.tile_program(state, 1.0, "mgTile")]
    elif FULL:
        # one program per ring for the whole song: every optional op kept, its constants keyed through the arc
        nodes = world.ring_nodes(lambda cell: arc_program_name(cell), look.hexlin(s["under"]), arc_program_name(0),
                                 ARC_SIDE_PX, TILE_SCALE, look.hexlin(s["land"][1]))
        programs = arc_programs(arc.full_state(state))
    else:
        prog = lambda cell: "mgTile%dx_%s" % (int(cell), state)   # noqa: E731
        under = "mgUnder_%s" % state
        nodes = world.ring_nodes(prog, look.hexlin(s["under"]), under, s.get("side_px", 0.0), TILE_SCALE,
                                 look.hexlin(s["land"][1]))
        programs = [look.tile_program(state, c, scale=TILE_SCALE) for c in (1.0, 2.0, 4.0)]
        programs.append(look.tile_program(state, 4.0, under, far=True, shade=look.UNDER_SHADE, border=1.0,
                                          scale=TILE_SCALE, contour=False, ramp_offset=look.UNDER_RAMP * TILE_SCALE))
    doc = {
        "format": "avgen-scene", "version": 1, "name": "musical-geography",
        "camera": dict({"mode": 1, "orbitSpeed": 0.0}, **camera_pose),
        "environment": env,
        "nodes": nodes,
        "lights": [look.sun_light(state), look.moon_light(state)] + ([look.fill_light(state)] if look.STATES[state].get("fill_int", 0) > 0 or FULL else []),
        "materialPrograms": programs,
    }
    doc["camera"]["far"] = 200000.0   # the 64 m far ring reaches 4.5 km
    if CLOUDS:
        cn, cp = look.cloud_nodes(arc.full_state(state) if FULL else state)
        doc["nodes"] += cn
        if not FULL:   # the full build's programs come from arc_programs (keyed through the arc)
            doc["materialPrograms"] += cp
    if FULL:
        doc["lights"][1]["castsShadow"] = True   # the moon's shadows at night (a light's shadow is not keyable)
        doc["flight"] = FLIGHT_FULL
    elif FLY:
        doc["flight"] = FLIGHT
    if CHARACTERS:
        n, e = cast_nodes(state)
        doc["nodes"] += n
        doc["entities"] = e
        doc["lights"] += cast_lights(state)
        env["tileDynamics"]["spring"] = SPRING
    return doc


AUDIO_DIR = os.path.abspath(os.path.join(HERE, "..", "..", "assets", "audio"))
AUDIO = os.path.join(HERE, "..", "..", "assets", "audio", "feline-footwear.wav")

# ---- CP-D: the arc on the timeline (arc.py) ----------------------------------------------------------------------
ARC_SIDE_PX = 12   # slabSidePixels is structural (not keyable): the night's 12 for the whole song (day 0, sunset 26)


def arc_program_name(cell):
    return "mgUnder_arc" if cell == 0 else "mgTile%dx_arc" % int(cell)


def arc_programs(S):  # noqa: C901
    progs = [look.tile_program(S, c, name=arc_program_name(c), scale=TILE_SCALE, always=True) for c in (1.0, 2.0, 4.0)]
    progs.append(look.tile_program(S, 4.0, arc_program_name(0), far=True, shade=look.UNDER_SHADE, border=1.0,
                                   scale=TILE_SCALE, contour=False, always=True,
                                   ramp_offset=look.UNDER_RAMP * TILE_SCALE))
    if CLOUDS:
        progs += look.cloud_nodes(S)[1]
    return progs


def arc_timeline():
    """Every parameter the arc moves, sampled through each transition (arc.sample_times) and keyed linearly."""
    times = arc.sample_times(1.0)
    samples = {}

    def put(target, t, v):
        samples.setdefault(target, []).append((t, v))

    structure = None
    for t in times:
        _, S = arc.at(t)
        progs = arc_programs(S)
        shape = [[op["kind"] for op in p["ops"]] for p in progs]
        if structure is None:
            structure = shape
        assert shape == structure, "the arc's programs changed structure at %.1f s" % t
        for p in progs:
            assert len(p["ops"]) <= 96, (p["name"], len(p["ops"]))   # kMaxMaterialOps (ADR-1863)
            for i, op in enumerate(p["ops"], 1):
                for field in ("constant", "constant2", "constant3", "constant4", "value"):
                    if field in op:
                        v = op[field]
                        v = [round(x, 6) for x in v] if isinstance(v, list) else round(float(v), 6)
                        put("material/%s/op/%d/%s/%s" % (p["name"], i, op["kind"], field), t, v)
        for k, v in look.post(S).items():
            if isinstance(v, (int, float)) and not isinstance(v, bool) and k != "camera/exposure/mode":
                put(k, t, round(float(v), 6))
        put("env/intensity", t, round(S["amb_int"], 6))
        if CLOUDS:   # ADR-1966: the night's cores-only cut, per cell, keyed through the arc
            for name, cell, thick, alt, view, cover in look.CLOUD_LAYERS:
                put("procedural/%s/distribution/generator/omitBelow" % name, t,
                    round(look.cloud_omit_below(S, thick, cover["hillHeight"]), 4))
                put("procedural/%s/material/refraction/opacity" % name, t, look.cloud_gate(S))
        lg, lc = look.landing_glow(S)
        put("env/tiles/glow/landing", t, round(lg, 6))
        put("env/tiles/glow/landingColor", t, [round(v, 6) for v in lc])
        put("lights/fill/intensity", t, round(S["fill_int"], 6))
        put("lights/fill/color", t, look.hexlin(S["fill_col"]))
        put("procedural/mgTiles64/material/baseColor", t, look.hexlin(S["land"][1]))
        for i in range(len(cast())):
            put("nodes/alien%02d/emissiveBoost" % i, t, round(1.0 + (NIGHT_GLOW - 1.0) * S["glow"], 6))
            put("lights/halo%02d/intensity" % i, t, round(NIGHT_HALO["intensity"] * S["glow"], 6))
    tracks = [arc.track("env/dayNight/dayPhase", arc.phase_track())]
    tracks += [arc.keyed(k, v) for k, v in sorted(samples.items()) if not arc.is_static(v)]
    return {"enabled": True, "cues": [], "tracks": tracks}


def project_doc(scene_rel, state):
    return {
        "format": "avgen-project", "version": 4,
        "assets": {"scene": {"kind": "composition", "path": scene_rel},
                   "audio": AUDIO,
                   "stems": [dict(st, file=os.path.join(AUDIO_DIR, st["file"])) for st in look.FELINE_STEMS]},
        "parameters": dict(look.post(state), **{"post/output/dither": 1.0, "env/tiles/rings/ahead": 150.0}),
        "routes": look.audio_routes(),
        # the response bus (response.low/mid/high/onset/level) publishes only with a sonic block: without it four of the
        # seven terrain routes and the loudness hops were dead in draft3 (eng, 10-audio-reactivity-now.md)
        "sonic": {"live": True},
        # Phase 7: Feline's stems as feeds (the beat clock follows the drums, the levels the bass)
        "feeds": dict(look.FELINE_FEEDS),
        **({"timeline": arc_timeline()} if FULL else {}),
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video",
                   "path": "renders/musical-geography.mp4"},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--state", default="day", choices=sorted(look.STATES))
    ap.add_argument("--interim", action="store_true")
    ap.add_argument("--camera", default="low", choices=sorted(CAMERAS))
    ap.add_argument("--out", default=os.path.join(HERE, "scene"))
    ap.add_argument("--tile-scale", type=float, default=4.0)
    ap.add_argument("--flight", action="store_true")
    ap.add_argument("--characters", action="store_true")
    ap.add_argument("--full", action="store_true", help="CP-D: the whole song, one flight, the arc on the timeline")
    ap.add_argument("--clouds", action="store_true", help="CP-D r2: the slab clouds (look.CLOUD_LAYERS)")
    ap.add_argument("--tiltshift", action="store_true", help="cpd-r3: the ADR-079 tilt-shift band (look.TILT)")
    ap.add_argument("--tiles", default=None, help="CP-D r2: JSON overrides for world.TILES (terrain variants)")
    a = ap.parse_args()
    global CLOUDS
    CLOUDS = a.clouds
    look.TILT_SHIFT = a.tiltshift
    if a.tiles:
        world.TILES.update(json.loads(a.tiles))
    global FULL
    FULL = a.full
    if FULL:
        a.state = arc.ARC[0][1]
    global TILE_SCALE
    TILE_SCALE = a.tile_scale
    global FLY
    FLY = a.flight
    global CHARACTERS
    CHARACTERS = a.characters
    os.makedirs(a.out, exist_ok=True)
    scene_path = os.path.join(a.out, "musical-geography.scene.json")
    with open(scene_path, "w") as f:
        json.dump(scene_doc(a.state, a.interim, a.camera), f, indent=1)
    proj_dir = a.out   # look-development projects live beside the scene (gitignored) until the scene is approved
    proj_path = os.path.join(proj_dir, "musical-geography.json")
    with open(proj_path, "w") as f:
        json.dump(project_doc(os.path.relpath(scene_path, proj_dir), a.state), f, indent=1)
    print("wrote", scene_path, "and", proj_path)


if __name__ == "__main__":
    main()

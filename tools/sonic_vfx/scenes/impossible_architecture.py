"""7. IMPOSSIBLE ARCHITECTURE: RELATIVITY COURT (04-brief-abstract-direction.md, direction 7; ABSTRACT-PLAN.md section 7).

Escher's Relativity as Manifold Garden would build it: one module repeated forever in all three directions, fading into
warm haze. The module holds three flights of stairs, each in its own gravity (one climbs along X with the floor below,
one along Y with the floor behind, one along Z with the floor to the side: the same flight turned by the two cyclic
permutations of the axes), each with its landings and a doorway that stands on nothing; at the module's heart sit open
cubes nested inside each other (rooms inside rooms, each turned against the last). De Chirico's light: a low raking
sun, long hard shadows, warm flat colour, ink on every edge.

Construction: one compiled SDF. `repeat` (infinite, three axes) of the cell; `recurse` for the nested rooms (its count is
the number of rooms inside the room); the `stairs` primitive (ADR-1040). Cel lighting on the SDF material (ADR-1071),
the SDF look's own shadow march for the long shadows, the screen-space outline (ADR-1072) for the ink, surface fog for
the haze. The world rolls (the SDF object's rotation), so up keeps changing and the shadows sweep.
"""
import math

from .. import kit
from ..kit import (R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, VERY_SLOW, sd_union, sd_diff, sd_move, sd_rot,
                   sd_box, sd_repeat)

ID = "impossible-architecture"
TITLE = "Impossible Architecture"

TERRACOTTA = "#c8553d"
OCHRE = "#e0a458"
CREAM = "#f3e3c3"
INK = "#2a1b14"
SKY_TOP = "#0d3b3a"
SKY = "#1f6f6a"
HAZE = "#e8b98a"
SUN = "#ffd08a"

CELL = 14.0
RUN, RISE, STEPS = 0.45, 0.32, 14
OFFSET = 3.6              # each flight's distance from the cell's centre line
# Cost (a ray marches the whole cell at every step, plus a shadow march): the rooms' recursion depth at rest, the
# shadow march's steps, the primary march's steps and reach. `SONIC_IA_COST=light` builds the cheap arm for a perf A/B.
import os as _os
LIGHT = _os.environ.get("SONIC_IA_COST") == "light"
ROOMS_AT_REST = 1 if LIGHT else 2
SHADOW_STEPS = 16 if LIGHT else 28
MAX_STEPS = 80 if LIGHT else 110
REACH = 55.0 if LIGHT else 75.0
P1 = (90.0, 0.0, 90.0)    # x->y, y->z, z->x: the flight that climbs Y, its floor behind
P2 = (0.0, 270.0, 270.0)  # x->z, y->x, z->y: the flight that climbs Z, its floor to the side

DESIGN = {
    "category": "impossible architecture",
    "thesis": "Relativity Court: a staircase module in three gravities, repeated forever in every direction, rooms "
              "inside rooms at its heart, in a low golden light. The music breathes the lattice, folds the stairs "
              "into new gravities and turns the whole world.",
    "composition": {
        "background": "the lattice repeating into a warm haze; green-teal sky in the gaps",
        "midground": "flights of stairs in three gravities, landings, doorways on nothing",
        "foreground": "the nearest flight sweeping across the frame",
        "focal": "the nested open cubes at the nearest module's heart",
        "secondary": ["the doorways", "the long shadows"],
        "atmosphere": "warm surface haze swallowing the far repetitions",
        "post": "ink outlines fading with distance, a little grain",
        "camera": "drifting inside the lattice; the world rolls",
    },
    "palette": {"dominant": CREAM, "secondary": TERRACOTTA, "accent": SKY, "highlight": SUN,
                "background_value": "mid", "saturation": "warm flat earth colours against a green-teal sky"},
    "motion": {
        "very_slow": ["the world's roll", "the camera's drift"],
        "medium": ["the lattice's breath", "the shadows' sweep"],
        "fast": ["doorways lighting on notes"],
        "extremely_fast": ["a stair module sliding on the kick"],
    },
    "vocabulary": [
        ["bass", "response.bass", "the lattice breathes apart and together"],
        ["kick", "response.kick", "a flight slides one step"],
        ["snare", "response.snare", "a flight folds over into a new gravity"],
        ["mids", "audio.mid", "the sun sweeps; the shadows move"],
        ["highs", "audio.treble", "the ink sharpens"],
        ["chord", "notes.polyphony", "more rooms nest inside the room"],
        ["note", "notes.lastPitch", "a doorway at the pitch's height lights"],
        ["silence", "(no input)", "the court turns slowly in the light"],
    ],
    "tier": "heavy: one compiled SDF repeated in three axes, a shadow march, the outline pass",
}


def stairs(name=None, m=None):
    o = {"kind": "stairs", "size": [RUN, RISE, 1.1], "count": STEPS, "height": 0.45}
    if name:
        o["name"] = name
    if m is not None:
        o["material"] = int(m)
    return o


def flight(tag, door_m):
    """One flight with its two landings and a doorway on the top landing, climbing +X, floor below, centred on the
    cell's X axis at z = OFFSET."""
    L, H = RUN * STEPS, RISE * STEPS
    st = sd_move((-L * 0.5, -H * 0.5, OFFSET), stairs(m=1))
    low = sd_move((-L * 0.5 - 0.9, -H * 0.5 - 0.2, OFFSET), sd_box((0.9, 0.2, 1.1), m=2))
    high = sd_move((L * 0.5 + 0.9, H * 0.5 - 0.2, OFFSET), sd_box((0.9, 0.2, 1.1), m=2))
    door = sd_move((L * 0.5 + 1.6, H * 0.5 + 1.55, OFFSET),
                   sd_diff(sd_box((0.16, 1.55, 1.05), m=door_m), sd_move((0.0, -0.35, 0.0),
                                                                        sd_box((0.4, 1.25, 0.62)))))
    return sd_union(st, low, high, door, name="flight" + tag)


def rooms():
    """An open cube (a frame with square openings on all six faces), nested in itself by `recurse`."""
    cube = sd_diff(sd_box((2.0, 2.0, 2.0), m=0), sd_box((1.55, 1.55, 2.6)), sd_box((1.55, 2.6, 1.55)),
                   sd_box((2.6, 1.55, 1.55)))
    return {"kind": "recurse", "name": "rooms", "count": ROOMS_AT_REST, "scale": 2.35, "translation": [0.0, 0.0, 0.0],
            "rotation": [0.0, 35.0, 20.0], "size": [0.0, 0.0, 0.0], "children": [cube]}


def lattice():
    cell = sd_union(rooms(), sd_move((0.0, 0.0, 0.0), flight("A", 3), name="slideA"),
                    sd_rot((0.0, 0.0, 0.0), sd_rot(P1, flight("B", 4)), name="foldB"),
                    sd_rot(P2, flight("C", 5)))
    return sd_repeat((CELL, CELL, CELL), cell, count=0, name="lattice")


def instrument(s, sun_dir):
    """The modulation map (ABSTRACT-PLAN.md section 7)."""
    N = "sdf/court/node/%s/"
    # ---- BASS: the lattice breathes apart and together (anchored round the viewer at the origin)
    s.route(R("bass", N % "lattice" + "size", 1.0, op="multiply", gain=0.1, offset=1.0, attackMs=60, decayMs=600))
    # ---- KICK: flight A slides two steps along its climb and springs back
    s.route(R("kick", N % "slideA" + "translation", RUN * 2.0, comp=0, attackMs=0, decayMs=220, springHz=2.2,
              springDamping=0.45))
    # ---- SNARE: flight B folds over a quarter turn into a new gravity and swings back
    s.route(R("snare", N % "foldB" + "rotation", 90.0, comp=2, attackMs=0, decayMs=500, springHz=0.9,
              springDamping=0.55))
    # ---- HIGHS: the ink sharpens and thickens
    s.route(R("audio.treble", "post/outline/width", 1.4, attackMs=20, decayMs=200),
            R("hat", "post/outline/intensity", 0.6, attackMs=0, decayMs=100))
    # ---- MIDS: the sun sweeps, so the long shadows move (the toon light and the shadow march together)
    s.route(R("audio.mid", "lights/sun/azimuth", 28.0, **SLOW),
            R("audio.mid", "sdf/court/look/shadow/direction", -0.45, comp=0, **SLOW))
    # ---- CENTROID: the sky and the haze (teal dusk for a dark timbre, a pale gold noon for a bright one)
    s.route(R("brightnessSlow", "env/sky/horizonColor", 0.5, comp=0, **SLOW),
            R("brightnessSlow", "env/sky/horizonColor", 0.25, comp=1, **SLOW),
            R("brightnessSlow", "scene/fogColor", 0.25, comp=2, **SLOW))
    # ---- TEMPO: the doorways glow faintly on the beat
    for k in (3, 4, 5):
        s.route(R("beat", "sdf/court/surface/%d/emission" % k, 0.5, attackMs=0, decayMs=200))
    # ---- INTENSITY: STRUCTURE -- more rooms nest inside the room
    s.route(R("intensity", N % "rooms" + "count", 2.0, threshold="binary", thresholdLevel=0.5))
    # ---- MIDI: a note lights the doorways of the flight at its register (low A, middle B, high C), as bright as its
    # velocity; a chord twists the nested rooms; a held note warms the sun
    for k, surf in enumerate((3, 4, 5)):
        for comp, val in enumerate(hexrgb(SUN, 9.0)):
            s.route(R("visual.doorHit%d" % k, "sdf/court/surface/%d/emission" % surf, val, comp=comp,
                      depth="lastVelocity", attackMs=0, decayMs=700))
    s.route(R("polyphony", N % "rooms" + "rotation", 60.0, comp=1, attackMs=200, decayMs=1200),
            R("held", "lights/sun/intensity", 1.5, **MEDIUM))
    # ---- MOD WHEEL: the world's roll by hand
    s.route(R(s.modwheel(), "sdf/court/transform/rotation", 90.0, comp=2, attackMs=80, decayMs=80))


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.0, "release": 1.1,
                  "floorDb": -44.0, "rangeDb": 42.0}     # mastered music does not saturate the levels
    s.environment = {
        "intensity": 0.45, "background": hexrgb(SKY), "fogColor": hexrgb(HAZE), "volumeDensity": 0.042,
        "volumeMaxDistance": 0.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb(SKY_TOP), "horizonColor": hexrgb(SKY),
                "groundColor": hexrgb("#0a2a2a"), "haze": 0.4, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    sun_dir = [0.62, -0.42, -0.66]
    n = math.sqrt(sum(c * c for c in sun_dir))
    sun_dir = [c / n for c in sun_dir]
    s.light("sun", "directional", direction=sun_dir, color=hexrgb(SUN), intensity=4.6, castsShadow=False)
    s.light("sky", "directional", direction=[-0.3, 0.8, 0.5], color=hexrgb("#7fd0c8"), intensity=0.5,
            castsShadow=False)
    surfaces = [{"color": hexrgb(CREAM)}, {"color": hexrgb(TERRACOTTA)}, {"color": hexrgb(OCHRE)},
                {"color": hexrgb(CREAM), "emission": [0.0, 0.0, 0.0]}, {"color": hexrgb(CREAM), "emission": [0.0, 0.0, 0.0]},
                {"color": hexrgb(CREAM), "emission": [0.0, 0.0, 0.0]}]
    s.sdf("court", lattice(), (-120.0, -120.0, -120.0), (120.0, 120.0, 120.0), surfaces=surfaces,
          look={"aoStrength": 0.0, "shadowStrength": 0.62, "shadowSoftness": 0.02,
                "shadowDirection": [-c for c in sun_dir], "shadowSteps": SHADOW_STEPS},
          material={"baseColor": [1, 1, 1], "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 0.8,
                    "metallic": 0.0,
                    "toon": {"bands": 2, "softness": 0.02, "terminator": 0.0, "shadowColor": [0.5, 0.62, 0.74],
                             "ambient": 0.72, "rimWidth": 0.0, "specular": 0.0}},
          max_steps=MAX_STEPS, epsilon=0.0012, step_scale=0.9, max_distance=REACH)

    # the world turns slowly about the view axis (a full turn in four minutes)
    s.track("sdf/court/transform/rotation", [
        {"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "linear"},
        {"time": 240.0, "value": [0.0, 0.0, 360.0], "interp": "linear"}], loop=240.0)

    s.params_({"camera/lens/focalLength": 22.0, "post/bloom/intensity": 0.12, "post/bloom/threshold": 1.4,
               "post/output/vignette": 0.4, "post/output/grain": 0.02, "post/tonemap/operator": 3,
               "post/outline/amount": 1.0, "post/outline/color": hexrgb(INK), "post/outline/intensity": 1.0,
               "post/outline/width": 1.6, "post/outline/depthThreshold": 0.05, "post/outline/normalThreshold": 0.35,
               "post/outline/objectEdges": 1.0, "post/outline/fadeStart": 14.0, "post/outline/fadeEnd": 42.0})
    cam = [2.3, 1.1, 8.6]
    tgt = [-6.0, -2.0, -14.0]
    s.camera = {"mode": 1, "position": cam, "target": tgt, "fov": 50.0, "orbitSpeed": 0.0}
    s.drift_camera(cam, tgt, period=56.0, amp=(1.0, 0.6, 1.2), tamp=(2.0, 1.4, 0.0))
    s.places("door", "lastPitch", [0.36, 0.5, 0.64], 0.07, event="noteOn")
    instrument(s, sun_dir)
    s.region("rooms", centre=[0.0, 0.0, 0.0], radius=2.5)
    s.region("lattice", box=[0.0, 0.0, 1.0, 1.0])
    return s

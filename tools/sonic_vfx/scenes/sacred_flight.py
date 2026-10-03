"""1. SACRED GEOMETRY FLIGHT: THE GOLDEN PASSAGE (05-brief-direction-correction.md; ABSTRACT-PLAN.md section 1).

The viewer flies down the axis of an endless luminous sculpture of sacred geometry: a procession of colossal gates drawn
in gold light, alternating two figures -- the STAR gate (a hexagram inscribed in the rim, its inner circle, twelve
spokes) and the ROSE gate (six tangent circles inside the rim round an open heart, the rose window's tracery) -- every
gate with a double rim and twelve beads, strung on twelve helical rails that run the length of the passage. Outside,
enormous rings stand in the haze; at the vanishing point burns a gold light against which the far gates
stand dark. While notes sound, every rose's open heart is a vermilion polygon of the chord.

Coherence: ONE module, the pair of bays (a star gate, then a rose gate, 14 m apart), repeated along the flight axis, and
every element sits on the same twelve-fold radial grid (the rails run through the gates' bead points). The camera holds
the axis: the symmetry is the point. A world twist about the flight axis turns the
grid with depth, so the rails are helices and the gates, which travel toward the camera along them, turn as they come
(rifling).

The flight is a seamless sawtooth: the gates advance one pair of bays per PERIOD seconds and jump back exactly that far,
which leaves every (position, rotation) pair where it was; the outer rings do the same with their own spacing. The rails are
continuous, so they stand still.

The instrument (ABSTRACT-PLAN.md section 1): the twelve rails are the twelve pitch classes (a chord lights its own
polygon of lines down the whole passage); the kick sends a wave of light down the passage; the bass widens it; the mids
turn the whole lattice; the snare blooms the stars.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, VERY_SLOW

ID = "sacred-flight"
TITLE = "Sacred Geometry Flight"

INK = "#06061c"
HAZE = "#0f0c33"            # the air: the fog's colour, the end disc's rim, what a faded line becomes
GOLD = "#f2b84b"
PALE_GOLD = "#ffd98a"
IVORY = "#fff1d0"
VERMILION = "#ff4a24"
SUN = "#ffb54a"

BAY = 14.0                 # metres between gates
BAYS = 22                  # bays from the far end to the near end (even: the gates alternate star and rose)
BAY_SECONDS = 1.9          # the flight: one bay every 1.9 s (7.4 m/s)
PAIR = 2.0 * BAY           # the repeating module: a star gate and a rose gate
PERIOD = 2.0 * BAY_SECONDS
R_OUT = 9.0                # the gates' outer circle, and the rails' radius
R_IN = R_OUT / math.sqrt(3.0)          # the hexagram's inner hexagon (its vertices are the triangles' crossings)
R_CORE = R_IN * math.cos(math.pi / 6)  # the circle inscribed in the inner hexagon (= R_OUT / 2)
TWIST = 0.022              # radians the grid turns per metre of depth
Z_NEAR = 2.0 * BAY         # the nearest gate's resting z (behind the camera; the sawtooth brings it past)
Z_FAR = Z_NEAR - BAYS * BAY
WAVE_REST = -(Z_FAR - 60.0)  # op 3's constant at rest: the light wave parked beyond the far end
RING_EVERY = 4             # an enormous outer ring every 4 bays
R_ROSE = R_OUT / 3.0       # the rose's six circles: radius R/3, centred 2R/3 out, so they touch the rim and each other

DESIGN = {
    "category": "sacred geometry",
    "thesis": "The Golden Passage: you fly through an endless luminous sculpture of sacred geometry, rose-window gates "
              "strung on twelve helical rails; a chord lights its own polygon of rails down the whole passage, the "
              "kick sends a wave of light ahead of you, the bass widens the passage.",
    "composition": {
        "background": "the gold sun at the vanishing point, the far gates dissolving into its glow",
        "midground": "gates receding and turning along the helical rails; enormous rings outside in the haze",
        "foreground": "the nearest gate sweeping past the frame; the inner tunnel of triangles",
        "focal": "the vanishing point",
        "secondary": ["the rails", "the inner tunnel", "the outer rings"],
        "atmosphere": "indigo haze that turns gold toward the sun",
        "post": "bloom; a radial burst on the kick",
        "camera": "flying forward through the passage, a little off its axis, banking slowly",
    },
    "palette": {"dominant": INK, "secondary": HAZE, "accent": VERMILION, "highlight": GOLD,
                "background_value": "near black (indigo), gold at the vanishing point",
                "saturation": "gold and ivory light; vermilion only on the star's bloom"},
    "motion": {
        "very_slow": ["the bank", "the camera's drift"],
        "medium": ["the flight", "the lattice's turn"],
        "fast": ["rails lighting on notes", "the stars' bloom"],
        "extremely_fast": ["the kick's wave of light"],
    },
    "vocabulary": [
        ["bass", "response.bass", "STRUCTURE: the passage widens and narrows; the outer rings expand"],
        ["kick", "response.kick", "a wave of light runs down the passage from you to the sun; a surge forward"],
        ["snare", "response.snare", "the gates' stars bloom open and flash vermilion; sparks stream past"],
        ["hat", "response.hat", "the beads glint"],
        ["highs", "audio.treble", "sparks stream past along the walls"],
        ["mids", "audio.mid", "the whole lattice turns (the twist's phase)"],
        ["brightness", "sonic.brightness", "gold turns toward ivory and white"],
        ["beat", "beat.pulse", "the rails pulse"],
        ["intensity", "response.intensity", "STRUCTURE: the gates double (a gate every 7 m)"],
        ["pitch class", "notes.class.0", "each of the twelve rails is a pitch class: a chord lights its polygon down "
         "the whole passage"],
        ["chord", "notes.polyphony", "STRUCTURE: every rose's open heart becomes a vermilion polygon with as many "
         "sides as notes sounding"],
        ["pitch", "notes.lastPitch", "the sun's colour: deep orange (low) to white gold (high)"],
        ["velocity", "notes.lastVelocity", "how bright a note flares the gates' circles"],
        ["held", "notes.held", "the enormous outer rings glow while notes are held"],
        ["mod wheel", "control.modwheel", "STRUCTURE: the helix tightens"],
        ["silence", "(no input)", "the flight goes on, slow and golden"],
    ],
    "tier": "light: instanced thin tori and tubes, unlit, one material program, analytic fog, bloom",
}


def glow(hexc, intensity, program="sgfWave"):
    m = {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": hexrgb(hexc), "emissiveIntensity": float(intensity),
         "roughness": 1.0, "metallic": 0.0, "unlit": True}
    if program:
        m["program"] = program
    return m


def torus(R_, r, n=192, m=6):
    return {"kind": "torus", "majorRadius": float(R_), "minorRadius": float(r), "majorSegments": int(n),
            "minorSegments": int(m)}


def gates(count=BAYS + 1, z_far=Z_FAR, z_near=Z_NEAR):
    return {"kind": "linear", "count": int(count), "start": [0.0, 0.0, float(z_far)], "end": [0.0, 0.0, float(z_near)]}


def stars():
    """The star gates: every other bay, from the far end."""
    return gates(BAYS // 2 + 1, Z_FAR, Z_NEAR)


def roses():
    """The rose gates: the bays between the stars."""
    return gates(BAYS // 2 + 1, Z_FAR + BAY, Z_NEAR + BAY)


def twist(amount=TWIST):
    """The grid's turn with depth (world space, about the flight axis): angle = amount * z + phase. Slot 1."""
    return [{"kind": "twist", "amount": float(amount), "speed": 0.0, "phase": 0.0, "axis": [0, 0, 1],
             "center": [0, 0, 0], "space": "world"}]


def facing(spin_deg=0.0):
    """A torus lies in its XZ plane; X 90 turns it to face the flight axis, then Z spins it in that plane (the engine's
    Euler order applies X, then Y, then Z, about the world axes)."""
    return {"sourceTransform": {"rotation": [90.0, 0.0, float(spin_deg)]}}


def wave_program():
    """The light wave: each line's own emission plus a band of light round the depth in op 3's constant (minus the
    band's z), as bright as op 7's constant. The kick carries the band from the camera to the far end."""
    w = 7.0
    return {
        "name": "sgfWave",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "swizzle", "dst": 1, "srcA": 0, "constant": [2.0, 2.0, 2.0, 2.0]},
            {"kind": "constant", "dst": 2, "constant": [WAVE_REST] * 4},                    # op 3: -z of the band
            {"kind": "add", "dst": 3, "srcA": 1, "srcB": 2},
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 3},
            {"kind": "gradient", "dst": 4, "srcA": 3, "constant": [1.0, 0.0, 0.0, 1.0], "value": -1.0 / (w * w)},
            {"kind": "constant", "dst": 6, "constant": hexrgb(PALE_GOLD, 7.0) + [1.0]},     # op 7: the band's light
            {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 4},
            {"kind": "input", "dst": 5, "input": "materialEmission"},
            {"kind": "add", "dst": 7, "srcA": 5, "srcB": 6},
        ],
        "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 1.0, "opacity": -1,
    }


def end_light_program(radius):
    """The light at the end of the passage: radial falloff from the disc's centre (its local XZ plane), a broad gold
    glow plus a white-hot core, on the haze's own colour at the rim (fogged: it sits past the last gate)."""
    return {
        "name": "sgfEnd",
        "ops": [
            {"kind": "input", "dst": 0, "input": "localPosition"},
            {"kind": "multiply", "dst": 1, "srcA": 0, "srcB": 0},
            {"kind": "gradient", "dst": 1, "srcA": 1, "constant": [1.0, 0.0, 1.0, 0.0], "value": 1.0 / radius ** 2},
            {"kind": "power", "dst": 1, "srcA": 1, "value": 0.5},
            {"kind": "remap", "dst": 1, "srcA": 1, "value": 1, "constant": [0.0, 1.0, 1.0, 0.0]},   # 1 centre, 0 rim
            {"kind": "power", "dst": 2, "srcA": 1, "value": 8.0},
            {"kind": "constant", "dst": 3, "constant": hexrgb(SUN, 24.0) + [1.0]},
            {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},
            {"kind": "power", "dst": 4, "srcA": 1, "value": 100.0},
            {"kind": "constant", "dst": 5, "constant": hexrgb("#fff6e6", 300.0) + [1.0]},
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},
            {"kind": "add", "dst": 2, "srcA": 2, "srcB": 4},
            {"kind": "power", "dst": 4, "srcA": 1, "value": 1.4},
            {"kind": "constant", "dst": 5, "constant": hexrgb("#7a3a8c", 1.4) + [1.0]},
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},
            {"kind": "add", "dst": 2, "srcA": 2, "srcB": 4},
            {"kind": "constant", "dst": 6, "constant": hexrgb(HAZE) + [1.0]},
            {"kind": "add", "dst": 7, "srcA": 2, "srcB": 6},
        ],
        "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 1.0, "opacity": -1,
    }


# the gates: (name, source, colour, intensity, spin in degrees, which gates). Every gate has the double rim; the star's
# two triangles have their six vertices on the rim at 30 + 60k degrees and its inner circle touches the hexagon their
# crossings make; the rose's inner circle is the open heart its six circles leave.
GATE = [
    ("gateCircle", torus(R_OUT, 0.1, 128, 5), GOLD, 2.4, 0.0, gates),
    ("gateRim", torus(R_OUT + 0.7, 0.035, 128, 3), PALE_GOLD, 1.2, 0.0, gates),
    ("starTriA", torus(R_OUT, 0.05, 3, 4), GOLD, 1.8, 90.0, stars),
    ("starTriB", torus(R_OUT, 0.05, 3, 4), GOLD, 1.8, 30.0, stars),
    ("starInner", torus(R_CORE, 0.035, 96, 3), PALE_GOLD, 1.0, 0.0, stars),
    ("roseInner", torus(R_ROSE, 0.035, 96, 3), PALE_GOLD, 1.0, 0.0, roses),
]
GATE_PARTS = [g[0] for g in GATE] + ["spokes", "beads", "roses"]
STAR_PARTS = ["starTriA", "starTriB", "starInner", "spokes"]
ROSE_PARTS = ["roseInner", "roses"]
TRAVELLING = GATE_PARTS


def rail_angle(k):
    """Pitch class k's rail: C at the top, clockwise in semitones (a clock), so a chord's shape is its intervals."""
    return math.radians(90.0 - 30.0 * k)


def instrument(s):
    """The modulation map (ABSTRACT-PLAN.md section 1). Every audio dimension has its own job."""
    P = "procedural/%s/"
    rails = ["rail%d" % k for k in range(12)]
    lattice = TRAVELLING + rails
    # ---- BASS: STRUCTURE -- the passage widens and narrows (every part scaled about the flight axis); the enormous
    # outer rings expand further
    for node in lattice:
        for comp in (0, 1):
            s.route(R("bass", P % node + "transform/scale", 0.14, comp=comp, attackMs=40, decayMs=420))
    for comp in (0, 1):
        s.route(R("bass", P % "outerRings" + "transform/scale", 0.3, comp=comp, attackMs=60, decayMs=700))
    # ---- KICK: a wave of light runs from the camera to the far end at a constant speed (a binary trigger, then a
    # linear fall: op 3's constant goes from 0, the camera, back to WAVE_REST); a surge forward and a radial burst
    s.route(R("kick", "material/sgfWave/op/3/constant/constant", -WAVE_REST, threshold="binary", thresholdLevel=0.12,
              envelope="linearfall", envelopeHoldMs=0, envelopeFallPerSecond=1.15),
            R("kick", "post/radial/amount", 0.05, attackMs=0, decayMs=220),
            R("kick", "camera/lens/focalLength", -1.6, attackMs=0, decayMs=260))
    # ---- SNARE: the gates bloom (the stars' triangles and the roses' circles swell past the rim and ring back) and
    # flash; a burst of sparks
    for node in ("starTriA", "starTriB", "roses"):
        for comp in (0, 1):
            s.route(R("snare", P % node + "transform/scale", 0.16, comp=comp, attackMs=0, decayMs=300,
                      springHz=2.2, springDamping=0.35))
        s.route(R("snare", P % node + "material/emissive", 5.0, attackMs=0, decayMs=240))
    s.route(R("snare", "particles/sparks/burst", 420.0, threshold="binary", thresholdLevel=0.05))
    # ---- HIGHS: the beads glint; sparks stream past along the walls
    s.route(R("hat", P % "beads" + "material/emissive", 7.0, attackMs=0, decayMs=140),
            R("audio.treble", "particles/sparks/spawnRate", 320.0, attackMs=30, decayMs=300),
            R("audio.highMid", P % "starInner" + "material/emissive", 2.0, attackMs=30, decayMs=300),
            R("audio.highMid", P % "roseInner" + "material/emissive", 2.0, attackMs=30, decayMs=300))
    # ---- MIDS: the whole lattice turns (the twist's phase, integrated so it never jumps); the inner tunnel the other way
    for node in lattice:
        s.route(R("audio.mid", P % node + "deform/1/phase", 0.3, integrate=True, attackMs=120, decayMs=800))
    # ---- CENTROID: gold toward ivory and white
    for node in ["gateCircle", "starTriA", "starTriB", "roses"] + rails:
        s.route(R("brightness", P % node + "material/emissiveColor", 0.1, comp=2, **SLOW),
                R("brightness", P % node + "material/emissiveColor", 0.05, comp=1, **SLOW))
    # ---- TEMPO: the rails pulse on the beat
    for node in rails:
        s.route(R("beat", P % node + "material/emissive", 0.7, attackMs=0, decayMs=180))
    # ---- INTENSITY: STRUCTURE -- the gates double as the piece builds: a rim every 7 m, and every gate both a star
    # and a rose (each figure's count doubles, so each now stands every 14 m)
    for node in ["gateCircle", "gateRim", "beads"]:
        s.route(R("intensity", P % node + "distribution/count", float(BAYS), threshold="binary", thresholdLevel=0.55))
    for node in STAR_PARTS + ROSE_PARTS:
        s.route(R("intensity", P % node + "distribution/count", float(BAYS // 2), threshold="binary",
                  thresholdLevel=0.55))
    # ---- MIDI: the twelve rails are the twelve pitch classes (a chord lights its polygon down the passage); the chord's
    # size is the polygon in every rose's heart; the last pitch is the sun's colour;
    # velocity is how hard a note flares the gates' circles; held notes light the enormous outer rings
    for k in range(12):
        s.route(R("notes.class.%d" % k, P % ("rail%d" % k) + "material/emissive", 10.0, attackMs=0, decayMs=500))
    # the chord's polygon: every rose's open heart is a circle at rest and, while notes sound, a vermilion polygon with
    # as many sides as notes (a triangle for one to three, up to an octagon): 128 segments less 125 while any note
    # sounds, plus (8 x polyphony - 3) clamped to 0..5
    s.route(R("polyphony", P % "roseInner" + "source/majorSegments", -125.0, threshold="binary", thresholdLevel=0.06),
            R("polyphony", P % "roseInner" + "source/majorSegments", 1.0, gain=8.0, offset=-3.0,
              clampEnabled=True, clampMin=0.0, clampMax=5.0),
            R("polyphony", P % "roseInner" + "material/emissiveColor", -0.42, comp=1, threshold="binary",
              thresholdLevel=0.06, attackMs=30, decayMs=600),
            R("polyphony", P % "roseInner" + "material/emissive", 3.0, threshold="binary", thresholdLevel=0.06,
              attackMs=30, decayMs=600),
            R("lastPitch", "env/sky/sunColor", 0.5, comp=2, **MEDIUM),
            R("lastPitch", "env/sky/sunColor", 0.2, comp=1, **MEDIUM),
            R("noteEnv", P % "gateCircle" + "material/emissive", 5.0, depth="lastVelocity", attackMs=0, decayMs=420),
            R("held", P % "outerRings" + "material/emissive", 2.4, **MEDIUM))
    # ---- MOD WHEEL: STRUCTURE -- the helix tightens
    wheel = s.modwheel()
    for node in lattice:
        s.route(R(wheel, P % node + "deform/1/amount", 0.03, attackMs=80, decayMs=80))


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.unshadowed_key()          # every material is unlit: the default key's shadow would be pure cost
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.0, "release": 1.1,
                  "floorDb": -44.0, "rangeDb": 42.0}     # mastered music does not saturate the levels
    # a uniform dark sky (the sky's own sun glows only above its horizon, which drew a horizon across the passage); the
    # light at the end is geometry. The fog dims the far gates, so near the end they stand dark against the light.
    s.environment = {
        "intensity": 0.0, "background": hexrgb(HAZE), "fogColor": hexrgb(HAZE), "volumeDensity": 0.0042,
        "volumeMaxDistance": 0.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb(HAZE), "horizonColor": hexrgb(HAZE),
                "groundColor": hexrgb(HAZE), "haze": 0.35, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.program(wave_program())

    # ---- the gates (a star and a rose in each pair of bays)
    for name, src, col, inten, spin, where in GATE:
        s.proc(name, src, distribution=where(), material=glow(col, inten), deformers=twist(), extra=facing(spin))
    # spokes, beads and the rose's six circles: one gate's radial figures (templates), composed onto the gates
    # (ADR-029). A template
    # must stay visible (a hidden one composes nothing), so it is parked far behind the camera: its own distribution
    # transform does not reach the composed copies.
    s.proc("spokeRing", {"kind": "box", "size": [0.03, 0.03, R_OUT - R_CORE], "subdivisions": 1},
           distribution={"kind": "radial", "count": 12, "radius": (R_OUT + R_CORE) * 0.5, "plane": "xy",
                         "orientation": "outward"},
           material=glow(PALE_GOLD, 1.2))
    s.proc("beadRing", {"kind": "sphere", "radius": 0.2, "segments": 8, "rings": 5},
           distribution={"kind": "radial", "count": 12, "radius": R_OUT, "plane": "xy", "orientation": "outward"},
           material=glow(IVORY, 3.0))
    s.proc("roseRing", torus(R_ROSE, 0.045, 72, 3),
           distribution={"kind": "radial", "count": 6, "radius": 2.0 * R_ROSE, "plane": "xy", "orientation": "outward",
                         "startAngle": math.pi / 6.0, "endAngle": math.pi / 6.0 + 2.0 * math.pi},
           material=glow(GOLD, 1.8))
    s.proc("spokes", {"kind": "procedural", "reference": "spokeRing"}, distribution=stars(),
           material=glow(PALE_GOLD, 1.1), deformers=twist())
    s.proc("roses", {"kind": "procedural", "reference": "roseRing"}, distribution=roses(),
           material=glow(GOLD, 1.8), deformers=twist())
    s.proc("beads", {"kind": "procedural", "reference": "beadRing"}, distribution=gates(),
           material=glow(PALE_GOLD, 2.6), deformers=twist())

    # ---- the twelve rails (the pitch classes): continuous tubes through the bead points, helices under the twist
    for k in range(12):
        a = rail_angle(k)
        x, y = R_OUT * math.cos(a), R_OUT * math.sin(a)
        s.proc("rail%d" % k, {"kind": "tube", "tubeRadius": 0.04, "tubeTaper": 1.0, "tubeSides": 5,
                              "tubeSegments": 260, "tubeTwist": 0.0, "tubeCaps": False,
                              "curve": {"kind": "polyline", "generator": "line", "count": 2,
                                        "start": [x, y, Z_FAR - 20.0], "end": [x, y, Z_NEAR + 20.0],
                                        "samplesPerSegment": 64, "up": [0.0, 1.0, 0.0]}},
               material=glow(GOLD, 0.55), deformers=twist())

    # ---- outside: enormous rings standing in the haze every RING_EVERY bays (their own seamless sawtooth)
    span = RING_EVERY * BAY
    n_rings = int((Z_NEAR - Z_FAR) // span) + 1
    s.proc("outerRings", torus(34.0, 0.3, 160, 4),
           distribution=gates(n_rings, Z_NEAR - (n_rings - 1) * span, Z_NEAR),
           material=glow(GOLD, 1.1), deformers=twist(), extra=facing(0.0))
    # the light at the end: a disc just beyond the last gate, a soft gold glow fading to the haze's colour at its rim
    # (opaque, so its rim must match what is around it), with a white-hot core
    end_r = 120.0         # a full-frame disc ran its program on every pixel (10 ms at 1080p); this one covers the glow
    s.program(end_light_program(end_r))
    s.proc("endLight", {"kind": "cylinder", "radius": end_r, "height": 1.0, "radialSegments": 128, "caps": True},
           material=glow(INK, 1.0, program="sgfEnd"),
           transform={"position": [0.0, 0.0, Z_FAR - 30.0], "rotation": [90.0, 0.0, 0.0], "scale": [1, 1, 1]})

    # ---- sparks streaming past along the walls (the highs, the snare)
    s.particles("sparks", capacity=6000, seed=19, shape="disc", position=[0.0, 0.0, -170.0],
                extent=[8.0, 0.2, 8.0], direction=[0.0, 0.0, 1.0], spawnRate=150.0, lifetimeMin=4.5,
                lifetimeMax=6.0, spread=0.02, speedMin=34.0, speedMax=46.0, gravity=[0, 0, 0], drag=0.0,
                sizeStart=0.05, sizeEnd=0.03, colorStart=hexrgb(IVORY) + [1.0],
                colorEnd=hexrgb(GOLD) + [0.0], emissive=6.0, blend="additive", velocityStretch=2.5, stretchMax=1.6)

    # ---- the flight: every travelling part advances one pair of bays per PERIOD, then jumps back by it (seamless: a
    # pair is two gate spacings, or one star or one rose spacing); the outer rings likewise
    for node in TRAVELLING:
        s.track("procedural/%s/transform/position" % node, [
            {"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "linear"},
            {"time": PERIOD, "value": [0.0, 0.0, PAIR], "interp": "linear"}], loop=PERIOD)
    s.track("procedural/outerRings/transform/position", [
        {"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "linear"},
        {"time": RING_EVERY * BAY_SECONDS, "value": [0.0, 0.0, span], "interp": "linear"}],
        loop=RING_EVERY * BAY_SECONDS)

    # ---- camera: on the axis (the symmetry is the point), drifting a metre about it, banking slowly
    cam = [0.0, 0.0, 0.0]
    tgt = [0.0, 0.0, -200.0]
    s.params_({"camera/lens/focalLength": 20.0, "post/dof/enabled": True, "post/dof/physical": False,
               "post/dof/focusDistance": 34.0, "post/dof/focusRange": 16.0, "post/dof/maxRadius": 8.0,
               "post/bloom/intensity": 0.55, "post/bloom/threshold": 0.75,
               "post/bloom/emissionWeight": 1.0, "post/output/vignette": 0.5, "post/output/grain": 0.01,
               "post/tonemap/operator": 4})
    s.camera = {"mode": 1, "position": cam, "target": tgt, "fov": 50.0, "orbitSpeed": 0.0}
    s.drift_camera(cam, tgt, period=44.0, amp=(1.0, 0.7, 0.0), tamp=(4.0, 3.0, 0.0))
    s.track("camera/roll", [{"time": round(36.0 * i / 8, 3), "value": round(10.0 * math.sin(2.0 * math.pi * i / 8), 3),
                             "interp": "smooth"} for i in range(9)], loop=36.0)

    instrument(s)
    s.region("sun", box=[0.42, 0.38, 0.58, 0.62])
    s.region("passage", box=[0.0, 0.0, 1.0, 1.0])
    return s

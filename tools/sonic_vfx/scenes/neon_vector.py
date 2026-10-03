"""2. NEON VECTOR WORLD: PULSAR PLAIN (04-brief-abstract-direction.md, direction 2; ABSTRACT-PLAN.md section 2).

Graphic design turned into a navigable world: a plain of hidden-line ridgelines (the Unknown Pleasures pulsar plot laid
flat and flown over), a colossal outline circle on the horizon, and flat blocks of one bold colour that the music builds.
White lines on black plus exactly one accent colour at a time.

Construction:
- a ridge is a white line (a thin tube along X, 320 segments) on a black fin (the same tube flattened into a tall
  blade below it), both deformed by the SAME world-space deformers so the fin's top follows the line: each fin hides
  the lines behind it, which is the hidden-line look;
- three row groups (near, middle, far), each its own pair of nodes, so each band of the spectrum swells its own depth;
- the terrain's shape is a noise FIELD whose position travels toward the camera: the lines stay put while their shapes
  advance, which reads as flight without moving a single row (and never runs out of plain);
- the centre carries the peaks: a field that is the noise times a soft stripe along the flight axis.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, VERY_SLOW

ID = "neon-vector"
TITLE = "Neon Vector World"

BLACK = "#000000"
WHITE = "#f4f4f0"
MAGENTA = "#ff2e88"
YELLOW = "#ffe500"
ULTRA = "#3a3dff"

HALF_W = 70.0          # the lines run from x = -70 to 70
SPACING = 2.2          # metres between rows
# (name, nearest z, rows, row spacing, line radius, line intensity): the rows open out with distance, so the far plain
# keeps separate lines on screen instead of merging into a white band
GROUPS = [
    ("near", 2.0, 16, SPACING, 0.022, 2.2),
    ("mid", 2.0 - 16 * SPACING, 18, SPACING * 1.45, 0.034, 1.6),
    ("far", 2.0 - 16 * SPACING - 18 * SPACING * 1.45, 18, SPACING * 2.4, 0.05, 0.75),
]
CAM = [0.0, 6.5, 14.0]

DESIGN = {
    "category": "vector",
    "thesis": "Pulsar Plain: a poster you can fly through. Hidden-line ridgelines ripple to the horizon under a "
              "colossal outline circle; the drums build flat blocks of one bold colour.",
    "composition": {
        "background": "pure black sky, one hairline horizon, a colossal outline circle of concentric rings",
        "midground": "a flat accent-colour triangle standing in the plain, half hidden by the ridges",
        "foreground": "the ridgelines, close and bright, rippling under the camera",
        "focal": "the spiky central peaks leading to the circle on the horizon",
        "secondary": ["the colour block", "the circle's rings"],
        "atmosphere": "(none: lines and planes only)",
        "post": "bloom on the lines; a split on the snare",
        "camera": "low and level, flying forward (the plain's shapes advance toward it)",
    },
    "palette": {"dominant": BLACK, "secondary": WHITE, "accent": MAGENTA, "highlight": YELLOW,
                "background_value": "black", "saturation": "white lines; one accent colour per section"},
    "motion": {
        "very_slow": ["the circle's rings turning"],
        "medium": ["the flight", "the plain's swell"],
        "fast": ["peaks rising under notes"],
        "extremely_fast": ["blocks snapping into place", "line flashes"],
    },
    "vocabulary": [
        ["bass", "response.bass", "the near rows swell"],
        ["kick", "response.kick", "a forward lurch and a rolling wave"],
        ["snare", "response.snare", "a colour block snaps into existence"],
        ["hat", "response.hat", "fine jitter on the lines"],
        ["mids", "audio.mid", "the middle rows swell; the circle breathes"],
        ["highs", "audio.highMid", "the far rows swell"],
        ["note", "notes.lastPitch", "a peak rises at the pitch's place across the plain"],
        ["intensity", "response.intensity", "the peaks grow taller as the piece builds"],
        ["beat", "beat.phase", "the beat sweep: a band of light runs down the plain to the camera once per beat"],
        ["silence", "(no input)", "the plain glides slowly under a still circle"],
    ],
    "tier": "light: six tube nodes (about 70 rows), a few rings and slabs, unlit",
}


def line_mat(intensity, program=None):
    m = {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": hexrgb(WHITE), "emissiveIntensity": float(intensity),
         "roughness": 1.0, "metallic": 0.0, "unlit": True}
    if program:
        m["program"] = program
    return m


SWEEP_FAR = -175.0                  # where the band rests: beyond the plain's far edge (silence shows no band)
SWEEP_NEAR = 2.0
SWEEP_WIDTH = 6.0


def sweep_program():
    """THE BEAT SWEEP: a band of light that travels down the plain toward the camera once per beat. The lines' own
    emission (materialEmission, so every route on their brightness still works) plus a band round the depth in op 3's
    constant (minus z of the band; `beat.phase` carries it from the far edge to the near), as wide as SWEEP_WIDTH,
    as bright as op 7's constant."""
    return {
        "name": "beatSweep",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "swizzle", "dst": 1, "srcA": 0, "constant": [2.0, 2.0, 2.0, 2.0]},
            {"kind": "constant", "dst": 2, "constant": [-SWEEP_FAR] * 4},                       # op 3: -z of the band
            {"kind": "add", "dst": 3, "srcA": 1, "srcB": 2},
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 3},
            {"kind": "gradient", "dst": 4, "srcA": 3, "constant": [1.0, 0.0, 0.0, 1.0],
             "value": -1.0 / (SWEEP_WIDTH * SWEEP_WIDTH)},
            {"kind": "constant", "dst": 6, "constant": hexrgb(WHITE, 1.8) + [1.0]},             # op 7: band brightness
            {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 4},
            {"kind": "input", "dst": 5, "input": "materialEmission"},
            {"kind": "add", "dst": 7, "srcA": 5, "srcB": 6},
        ],
        "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 1.0, "opacity": -1,
    }


BLACK_MAT = {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [0.0, 0.0, 0.0], "emissiveIntensity": 0.0,
             "roughness": 1.0, "metallic": 0.0, "unlit": True}


def accent_mat(hexc, intensity=1.0):
    return {"baseColor": hexrgb(hexc), "emissiveColor": hexrgb(hexc), "emissiveIntensity": float(intensity),
            "roughness": 1.0, "metallic": 0.0, "unlit": True}


def line_source(radius):
    return {"kind": "tube", "tubeRadius": float(radius), "tubeTaper": 1.0, "tubeSides": 4, "tubeSegments": 320,
            "tubeTwist": 0.0, "tubeCaps": False,
            "curve": {"kind": "polyline", "generator": "line", "count": 2, "start": [-HALF_W, 0.0, 0.0],
                      "end": [HALF_W, 0.0, 0.0], "samplesPerSegment": 64, "up": [0.0, 1.0, 0.0]}}


def terrain_deformers():
    """The shared world-space deformer stack (identical on a line and its fin):
    1 the swell (a long sine across the plain), 2 the plain (the travelling noise field), 3 the peaks (the noise times
    the central stripe), 4 the jitter (fine noise, the hats), 5 the note's peak (a field at the pitch's place)."""
    return [
        {"kind": "sine", "amount": 0.25, "frequency": 0.09, "speed": 0.35, "phase": 0.0, "axis": [0.8, 0.0, 0.6],
         "displacementAxis": [0.0, 1.0, 0.0], "space": "world"},
        {"kind": "field", "field": "plain", "amount": 0.9, "axis": [0.0, 1.0, 0.0], "space": "world", "alongNormal": False},
        {"kind": "field", "field": "peaks", "amount": 3.2, "axis": [0.0, 1.0, 0.0], "space": "world", "alongNormal": False},
        {"kind": "noise", "amount": 0.0, "scale": 1.7, "speed": 2.0, "seed": 11, "axisMask": [0.0, 1.0, 0.0],
         "space": "world"},
        {"kind": "field", "field": "notePeak", "amount": 0.0, "axis": [0.0, 1.0, 0.0], "space": "world", "alongNormal": False},
    ]


def instrument(s):
    """The modulation map (ABSTRACT-PLAN.md section 2). A deformer route goes to a group's lines AND its fins (the same
    displacement, or the hidden-line occlusion would come apart)."""
    def both(group, rel, *a, **k):
        for kind in ("lines_", "fins_"):
            s.route(R(a[0], "procedural/%s%s/%s" % (kind, group, rel), *a[1:], **k))
    # ---- BASS: the near rows swell under the camera; the plain heaves on the kick
    both("near", "deform/2/amount", "bass", 2.4, attackMs=30, decayMs=380)
    for g in ("near", "mid", "far"):
        both(g, "deform/1/amount", "kick", 1.1, attackMs=0, decayMs=420)
    # ---- BANDS: the middle rows answer the low mids and mids, the far rows the high mids: the spectrum lies across the
    # landscape in depth
    both("mid", "deform/2/amount", "audio.lowMid", 2.2, attackMs=40, decayMs=420)
    both("mid", "deform/3/amount", "audio.mid", 2.6, attackMs=40, decayMs=420)
    both("far", "deform/2/amount", "audio.highMid", 2.6, attackMs=40, decayMs=420)
    both("far", "deform/3/amount", "audio.highMid", 3.0, attackMs=40, decayMs=420)
    # ---- HIGHS: fine jitter on every line; the hats flick it
    for g in ("near", "mid", "far"):
        both(g, "deform/4/amount", "audio.treble", 0.22, attackMs=15, decayMs=160)
        both(g, "deform/4/amount", "hat", 0.18, attackMs=0, decayMs=90)
    # ---- KICK: a forward lurch (the plain's shapes jump toward the camera); a zoom punch
    for fld in ("plainNoise", "peakNoise"):
        s.route(R("kick", "field/%s/position" % fld, 14.0, comp=2, attackMs=0, decayMs=260, integrate=True))
    s.route(R("kick", "camera/lens/focalLength", 2.0, attackMs=0, decayMs=220))
    # ---- SNARE: construction -- three flat shapes snap into existence in sequence and shrink away; the lines flash
    for k, name in enumerate(("buildBar", "buildSquare", "buildDisc")):
        s.route(R("snare", "procedural/%s/transform/scale" % name, 1.0, attackMs=0, decayMs=650, delayMs=90 * k))
    for g in ("near", "mid", "far"):
        s.route(R("snare", "procedural/lines_%s/material/emissive" % g, 2.5, attackMs=0, decayMs=160))
    # ---- ONSET: the hairline outlines flash
    s.route(R("onset", "procedural/blockEdge/material/emissive", 6.0, attackMs=0, decayMs=200))
    # ---- MIDS: the horizon circle breathes and turns
    for i in range(7):
        s.route(R("audio.mid", "procedural/circle%d/transform/scale" % i, 0.05 + 0.012 * i, attackMs=60, decayMs=500),
                R("audio.mid", "procedural/circle%d/transform/rotation" % i, (12.0 + 6 * i) * (1 if i % 2 else -1),
                  comp=1, integrate=True, attackMs=200, decayMs=900))
    # ---- CENTROID: the section's accent colour (the whole image's hue turns; the white lines stay white)
    s.route(R("brightnessSlow", "post/grade/hueShift", 0.42, **VERY_SLOW))
    # ---- TEMPO: the beat sweep -- a band of light runs down the plain to the camera once per beat, brighter when the
    # music is louder
    s.route(R("beat.phase", "material/beatSweep/op/3/constant/constant", -(SWEEP_NEAR - SWEEP_FAR)),
            R("level", "material/beatSweep/op/7/constant/constant", 2.0, attackMs=60, decayMs=400))
    # ---- INTENSITY: STRUCTURE -- the plain grows denser (more rows) and the peaks taller as the piece builds
    for g, extra in (("near", 8.0), ("mid", 10.0)):
        both(g, "distribution/count", "intensity", extra, threshold="binary", thresholdLevel=0.55)
    for g in ("near", "mid", "far"):
        both(g, "deform/3/amount", "intensity", 2.0, **VERY_SLOW)
    # ---- MIDI: a note raises a peak across the plain at its pitch's place (low left, high right), as tall as its
    # velocity; a held note holds it; a chord turns the horizon circle into a polygon (STRUCTURE)
    s.route(R("lastPitch", "field/notePeak/position", 90.0, comp=0, offset=-0.5, springHz=3.0, springDamping=0.8))
    for g in ("near", "mid", "far"):
        both(g, "deform/5/amount", "noteEnv", 5.0, depth="lastVelocity", attackMs=0, decayMs=700)
        both(g, "deform/5/amount", "held", 2.5, **MEDIUM)
    for i in (0, 2, 4, 6):
        s.route(R("polyphony", "procedural/circle%d/source/majorSegments" % i, -256.0, gain=1.0, threshold="binary",
                  thresholdLevel=0.3),
                R("polyphony", "procedural/circle%d/source/majorSegments" % i, 8.0, attackMs=0, decayMs=500))
    # ---- MOD WHEEL: the plain's wavelength (smooth swells to jagged peaks)
    s.route(R(s.modwheel(), "field/plainNoise/frequency", 0.3, attackMs=60, decayMs=60))


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.6, "sustain": 0.5, "attack": 1.0, "release": 1.0,
                  "floorDb": -44.0, "rangeDb": 42.0}     # mastered music does not saturate the levels
    s.environment = {
        "intensity": 0.0, "background": [0.0, 0.0, 0.0], "fogColor": [0, 0, 0], "volumeDensity": 0.011,
        "volumeMaxDistance": 0.0, "skyIntensity": 0.0,
        "sky": {"enabled": False, "zenithColor": [0, 0, 0], "horizonColor": [0, 0, 0], "groundColor": [0, 0, 0],
                "haze": 0.0, "sunIntensity": 0.0, "intensity": 0.0, "background": True, "useKeyLight": False},
    }

    s.program(sweep_program())
    # ---- the terrain's fields: a travelling noise (the plain), its central stripe (the peaks), the note's peak
    s.nodes.append({"name": "plainNoise", "kind": "field", "field": {
        "kind": "noise", "frequency": 0.11, "seed": 21, "strength": 1.0, "position": [0.0, 0.0, 0.0]}})
    s.nodes.append({"name": "plain", "kind": "field", "field": {
        "kind": "compound", "combine": "multiply", "children": ["plainNoise", "plainBand"], "strength": 1.0}})
    s.nodes.append({"name": "plainBand", "kind": "field", "field": {
        "kind": "radial", "radius": 60.0, "scale": [1.0, 1.0, 1000.0], "strength": 1.0}})
    s.nodes.append({"name": "peakNoise", "kind": "field", "field": {
        "kind": "noise", "frequency": 0.32, "seed": 5, "strength": 1.0, "position": [0.0, 0.0, 0.0]}})
    s.nodes.append({"name": "peakStripe", "kind": "field", "field": {
        "kind": "radial", "radius": 16.0, "scale": [1.0, 1.0, 1000.0], "strength": 1.0}})
    s.nodes.append({"name": "peaks", "kind": "field", "field": {
        "kind": "compound", "combine": "multiply", "children": ["peakNoise", "peakStripe"], "strength": 1.0}})
    s.nodes.append({"name": "notePeak", "kind": "field", "field": {
        "kind": "radial", "radius": 7.0, "scale": [1.0, 1.0, 1000.0], "strength": 1.0, "position": [0.0, 0.0, 0.0]}})

    # ---- the ridgelines: three row groups, each a line node and a fin node
    for name, z_near, rows, spacing, radius, inten in GROUPS:
        z_far = z_near - (rows - 1) * spacing
        dist = {"kind": "linear", "count": rows, "start": [0.0, 0.0, z_far], "end": [0.0, 0.0, z_near]}
        s.proc("lines_" + name, line_source(radius), distribution=dist, material=line_mat(inten, "beatSweep"),
               material_variation={"emissiveGradient": 0.6}, deformers=terrain_deformers())
        s.proc("fins_" + name, line_source(1.0), distribution=dist, material=BLACK_MAT,
               deformers=terrain_deformers(),
               extra={"sourceTransform": {"position": [0.0, -3.4, 0.0], "rotation": [0, 0, 0],
                                          "scale": [1.0, 3.4, 0.004]}})

    # ---- the sky: a colossal outline circle of seven rings floating whole above the horizon on the right third
    # (never cut by the horizon: a cut circle is a sunset), a hairline horizon
    far_z = GROUPS[-1][1] - (GROUPS[-1][2] - 1) * GROUPS[-1][3]
    cz = far_z - 40.0
    ring_c = [65.0, 21.5, cz - 18.0]
    for i in range(7):
        rad = 18.0 - i * 1.5
        s.proc("circle%d" % i, {"kind": "torus", "majorRadius": rad, "minorRadius": 0.2 if i else 0.34,
                                "majorSegments": 256, "minorSegments": 4},
               material=line_mat(5.0 if i else 7.0),
               transform={"position": ring_c, "rotation": [90.0, 0.0, 0.0], "scale": [1, 1, 1]})
    s.proc("horizon", {"kind": "box", "size": [1400.0, 0.25, 0.25], "subdivisions": 1}, material=line_mat(6.0),
           transform={"position": [0.0, 0.0, cz], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- the colour block: a flat magenta triangle standing in the plain on the left third, pointing up, with an
    # offset hairline outline
    tri_pos = [-26.0, 9.0, -75.0]
    s.proc("block", {"kind": "cylinder", "radius": 13.0, "height": 0.2, "radialSegments": 3, "caps": True},
           material=accent_mat(MAGENTA, 1.6),
           transform={"position": tri_pos, "rotation": [90.0, 0.0, 90.0], "scale": [1, 1, 1]})
    s.proc("blockEdge", {"kind": "torus", "majorRadius": 15.0, "minorRadius": 0.16, "majorSegments": 3,
                         "minorSegments": 4},
           material=line_mat(3.0),
           transform={"position": [tri_pos[0] + 1.2, tri_pos[1] + 1.0, tri_pos[2] + 0.6],
                      "rotation": [90.0, 0.0, 90.0], "scale": [1, 1, 1]})

    # ---- camera: low and level, looking down the plain; the horizon on the upper third
    focal = 28.0
    tgt = kit.aim(CAM, [0.0, 0.0, cz], 0.5, 0.33, focal)
    s.params_({"camera/lens/focalLength": focal, "post/bloom/intensity": 0.5, "post/bloom/threshold": 0.7,
               "post/bloom/emissionWeight": 1.0, "post/output/vignette": 0.3, "post/output/grain": 0.01,
               "post/tonemap/operator": 4})
    s.camera = {"mode": 1, "position": CAM, "target": tgt, "fov": 40.0, "orbitSpeed": 0.0}
    s.drift_camera(CAM, tgt, period=48.0, amp=(1.6, 0.35, 0.0), tamp=(3.0, 0.6, 0.0))
    # the flight: the plain's noise travels toward the camera (8 m/s), the peaks' a little faster
    for fld, speed in (("plainNoise", 8.0), ("peakNoise", 9.0)):
        s.track("field/%s/position" % fld, [{"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "linear"},
                                            {"time": 600.0, "value": [0.0, 0.0, 600.0 * speed], "interp": "linear"}],
                loop=600.0)

    # ---- construction: three flat shapes the snare builds, PARKED at scale 0.01 at rest (not 0.001: below a uniform
    # scale of about 0.005 the composed transform's determinant is under float epsilon, glm::decompose fails and the
    # object pops back to full size, unrotated), each a different accent shape
    for name, src, pos, rot, col in (
            ("buildBar", {"kind": "box", "size": [26.0, 3.2, 0.2], "subdivisions": 1}, [14.0, 26.0, -95.0],
             [0.0, 0.0, -12.0], YELLOW),
            ("buildSquare", {"kind": "box", "size": [9.0, 9.0, 0.2], "subdivisions": 1}, [-4.0, 18.0, -82.0],
             [0.0, 0.0, 45.0], ULTRA),
            ("buildDisc", {"kind": "torus", "majorRadius": 7.0, "minorRadius": 0.5, "majorSegments": 96,
                           "minorSegments": 4}, [-44.0, 22.0, -105.0], [90.0, 0.0, 0.0], MAGENTA)):
        s.proc(name, src, material=accent_mat(col, 1.6),
               transform={"position": pos, "rotation": rot, "scale": [0.01, 0.01, 0.01]})   # see PARKED below

    instrument(s)
    s.region("peaks", box=[0.38, 0.35, 0.62, 0.75])
    s.region("circle", box=[0.25, 0.05, 0.75, 0.45])
    s.region("block", box=[0.1, 0.15, 0.4, 0.6])
    return s

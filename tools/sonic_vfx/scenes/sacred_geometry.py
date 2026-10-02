"""1. SACRED GEOMETRY GARDEN: THE ARMILLARY (04-brief-abstract-direction.md, direction 1; ABSTRACT-PLAN.md section 1).

A giant living geometric diagram, frontal and perfectly symmetric: a crown of vesica petals, a 12-pointed star, an
armillary of nested rings that open about their own diameters into a sphere, a well of nested polygons that spirals
away into the depth, a seed of life at the centre, and a flower-of-life lattice as the ground behind it. Gold and ivory
lines on ultramarine black, one vermilion accent.

Grammar: everything is a circle or a regular polygon about one centre, drawn as fine self-luminous lines with beads.
Motion is differential rotation (John Whitney): ring k turns at k times a base rate. Rings are coplanar at rest (a flat
mandala) and the music opens them (an armillary sphere).

Construction: thin unlit emissive tori (low `majorSegments` = polygons). Every ring carries two local twists: slot 1 about
its own normal (the spin: `speed` the base rate, `phase` the music's) and slot 2 about a diameter (the opening: `phase`).
The well is a `linear` distribution of rings along -Z under a world twist about Z, so in the frontal view the deeper
rings read as smaller and turned further: a golden spiral of polygons that is also a tunnel.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, VERY_SLOW

ID = "sacred-geometry"
TITLE = "Sacred Geometry Garden"

INK = "#05061a"
DEEP = "#0d1240"
GOLD = "#f2b84b"
PALE_GOLD = "#ffd98a"
IVORY = "#fff1d0"
VERMILION = "#ff4a24"

CAM_Z = 26.0

DESIGN = {
    "category": "sacred geometry",
    "thesis": "The Armillary: a giant living geometric diagram in gold light. Its rings turn at harmonic rates, open "
              "into a sphere on the music and close back into a flat mandala; chords draw their own polygons.",
    "composition": {
        "background": "a flower-of-life lattice, dim gold on ultramarine black, behind a soft indigo halo",
        "midground": "the armillary rings and the 12-pointed star; the well of nested polygons spiralling away",
        "foreground": "the crown of 24 vesica petals and the bead ring, at the frame's edge",
        "focal": "the seed of life and its vermilion core at the exact centre",
        "secondary": ["the 12-pointed star", "the polygon well", "the crown"],
        "atmosphere": "deeper layers dimmer (an indigo depth)",
        "post": "bloom; light trails from the feedback when the rings turn",
        "camera": "locked frontal on the axis, a slow breathing arc",
    },
    "palette": {"dominant": INK, "secondary": DEEP, "accent": VERMILION, "highlight": GOLD,
                "background_value": "near black (ultramarine)",
                "saturation": "gold and ivory lines; vermilion only at the core and the chord's ring"},
    "motion": {
        "very_slow": ["the base rotation (k turns per period)", "the camera's breath"],
        "medium": ["the armillary opening and closing", "the well's spiral"],
        "fast": ["the petals answering the pitch classes"],
        "extremely_fast": ["the kick's snap open", "bead flashes"],
    },
    "vocabulary": [
        ["bass", "response.bass", "the diagram breathes radially, the inner layers most; the core glows"],
        ["kick", "response.kick", "the armillary rings snap open about their diameters, a ripple inward, and ring back "
         "flat on a loose spring; a zoom punch"],
        ["snare", "response.snare", "the star and the seed's rim flash; sparks fly off the crown"],
        ["hat", "response.hat", "beads run along the armillary"],
        ["mids", "audio.mid", "the differential rotation speeds up (ring k gains k times the turn)"],
        ["bands", "audio.lowMid", "each band lights its own layer, from the seed (bass) to the crown (treble)"],
        ["brightness", "sonic.brightness", "gold turns toward ivory and cyan-white"],
        ["sustained", "response.sustain", "the camera pushes into the well; the light trails lengthen"],
        ["beat", "beat.pulse", "the petals breathe on the beat and flip a half turn each bar"],
        ["intensity", "response.intensity", "STRUCTURE: the crown doubles its petals (24 to 48), the well deepens"],
        ["pitch class", "notes.class.0", "the twelve pitch classes are a clock of diamonds and spokes: a chord lights "
         "its own polygon (an augmented triad a triangle, a diminished seventh a square)"],
        ["chord", "notes.polyphony", "STRUCTURE: a polygon in the heart with as many sides as notes sounding"],
        ["held", "notes.held", "the armillary stays open while a note is held"],
        ["velocity", "notes.lastVelocity", "how hard a note flares the core"],
        ["mod wheel", "control.modwheel", "opens the armillary into a sphere by hand"],
        ["silence", "(no input)", "the diagram turns slowly, flat and complete"],
    ],
    "tier": "light: about 30 procedural objects of thin tori, unlit; bloom; feedback trails",
}


def glow(hexc, intensity, program=None):
    m = {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": hexrgb(hexc), "emissiveIntensity": float(intensity),
         "roughness": 1.0, "metallic": 0.0, "unlit": True}
    if program:
        m["program"] = program
    return m


def torus(R_, r, n=96, m=6):
    return {"kind": "torus", "majorRadius": float(R_), "minorRadius": float(r), "majorSegments": int(n),
            "minorSegments": int(m)}


def spin_open(spin_rate, open_axis=(1, 0, 0)):
    """Slot 1: the spin about the ring's own normal (source +Y), rate in rad/s, the music's turn in `phase`.
    Slot 2: the opening about a diameter (`phase`, radians)."""
    return [{"kind": "twist", "amount": 0.0, "speed": float(spin_rate), "phase": 0.0, "axis": [0, 1, 0],
             "space": "local"},
            {"kind": "twist", "amount": 0.0, "speed": 0.0, "phase": 0.0, "axis": [float(v) for v in open_axis],
             "space": "local"}]


FACE = {"position": [0.0, 0.0, 0.0], "rotation": [90.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]}


def face(z=0.0, scale=1.0):
    return {"position": [0.0, 0.0, float(z)], "rotation": [90.0, 0.0, 0.0], "scale": [float(scale)] * 3}


# The armillary: (name, radius, polygon order, line radius, colour, intensity, depth, spin rad/s (k x base), opening
# axis). Ring k spins at k times the base rate, alternating direction; the opening axis alternates between the X and Z
# diameters (Z in source space is the frame's vertical after the face-on turn).
BASE = 2.0 * math.pi / 96.0
ARMILLARY = [
    ("ring1", 4.30, 192, 0.034, GOLD, 2.6, -0.6, 1, (1, 0, 0)),
    ("ring2", 3.88, 12, 0.022, IVORY, 1.9, -0.9, -2, (0, 0, 1)),
    ("ring3", 3.50, 160, 0.024, GOLD, 2.2, -1.2, 3, (1, 0, 0)),
    ("ring4", 3.14, 6, 0.022, PALE_GOLD, 2.0, -1.5, -4, (0, 0, 1)),
]


def instrument(s):
    """The modulation map (ABSTRACT-PLAN.md section 1). Every audio dimension has its own job."""
    P = "procedural/%s/"
    rings = [a[0] for a in ARMILLARY]
    # ---- BASS: the diagram breathes radially, inner layers more (a pulse from the centre); the core glows
    for node, depth in (("rimOuter", 0.025), ("rimInner", 0.025), ("beads", 0.025), ("petals", 0.03),
                        ("petalsInner", 0.035), ("star", 0.045), ("ring1", 0.05), ("ring2", 0.055), ("ring3", 0.06),
                        ("ring4", 0.065), ("seed", 0.12), ("seedCentre", 0.12), ("seedRim", 0.1)):
        s.route(R("bass", P % node + "transform/scale", depth, attackMs=25, decayMs=320))
    s.route(R("bass", P % "core" + "material/emissive", 14.0, attackMs=20, decayMs=260))
    # ---- KICK: the armillary snaps OPEN about its diameters, a ripple inward (staggered), and rings back flat on a
    # loose spring; the outer rim flashes
    for k, name in enumerate(rings):
        s.route(R("kick", P % name + "deform/2/phase", (1.15 if k % 2 == 0 else -1.15) * (1.0 - 0.12 * k),
                  attackMs=0, decayMs=260, delayMs=55 * k, springHz=1.3, springDamping=0.32))
    s.route(R("kick", P % "rimOuter" + "material/emissive", 5.0, attackMs=0, decayMs=220),
            R("kick", "camera/lens/focalLength", 2.2, attackMs=0, decayMs=240))
    # ---- SNARE: the star and the seed's rim flash vermilion-white; sparks fly off the crown
    s.route(R("snare", P % "star" + "material/emissive", 7.0, attackMs=0, decayMs=200),
            R("snare", P % "seedRim" + "material/emissive", 8.0, attackMs=0, decayMs=260),
            R("snare", "particles/sparks/burst", 420.0, threshold="binary", thresholdLevel=0.05))
    # ---- HIGHS: beads run along the armillary; the bead crown shimmers
    s.route(R("hat", "particles/beadRun/burst", 60.0, threshold="binary", thresholdLevel=0.05),
            R("hat", "particles/beads3/burst", 50.0, threshold="binary", thresholdLevel=0.05),
            R("audio.treble", "particles/beadRun/spawnRate", 220.0, attackMs=30, decayMs=300),
            R("audio.treble", P % "beads" + "material/emissive", 4.0, attackMs=20, decayMs=200))
    # ---- MIDS: the differential rotation speeds up (ring k gains k x the turn), integrated so it never jumps
    for k, name in enumerate(rings, start=1):
        s.route(R("audio.mid", P % name + "deform/1/phase", 0.45 * k * (1 if k % 2 else -1), integrate=True,
                  attackMs=80, decayMs=600))
    s.route(R("response.melodic", P % "star" + "deform/1/phase", 0.6, integrate=True, attackMs=120, decayMs=900),
            R("audio.mid", P % "wellHex" + "deform/1/phase", 0.5, integrate=True, attackMs=120, decayMs=900),
            R("audio.mid", P % "wellSquare" + "deform/1/phase", -0.4, integrate=True, attackMs=120, decayMs=900))
    # ---- CENTROID (brightness): gold toward ivory and pale cyan-white
    for node in ("rimOuter", "petals", "ring1", "ring3", "star"):
        s.route(R("brightness", P % node + "material/emissiveColor", 0.35, comp=2, **SLOW),
                R("brightness", P % node + "material/emissiveColor", 0.12, comp=1, **SLOW))
    # ---- BANDS: each band owns a layer, from the core (bass) to the crown (treble)
    s.route(R("audio.bass", P % "seedCentre" + "material/emissive", 3.0, attackMs=30, decayMs=300),
            R("audio.lowMid", P % "wellHex" + "material/emissive", 1.6, attackMs=40, decayMs=350),
            R("audio.lowMid", P % "wellSquare" + "material/emissive", 1.4, attackMs=40, decayMs=350),
            R("audio.mid", P % "ring2" + "material/emissive", 2.0, attackMs=40, decayMs=350),
            R("audio.mid", P % "ring4" + "material/emissive", 2.0, attackMs=40, decayMs=350),
            R("audio.highMid", P % "petalsInner" + "material/emissive", 2.4, attackMs=30, decayMs=300),
            R("audio.treble", P % "petals" + "material/emissive", 1.8, attackMs=30, decayMs=300))
    # ---- SUSTAIN: the camera pushes into the well; the light trails lengthen
    s.route(R("sustain", "camera/position", -3.5, comp=2, **SLOW),
            R("sustain", "temporal/feedback/decay", 0.22, **SLOW))
    # ---- TEMPO: the petals breathe on the beat; the crown's petals flip about their own long axis on the bar
    s.route(R("beat", P % "petals" + "transform/scale", 0.02, attackMs=0, decayMs=180),
            R("beat.bar", P % "petals" + "deform/1/phase", math.pi, attackMs=0, decayMs=0))
    # ---- INTENSITY: STRUCTURE -- the crown doubles its petals (24 to 48) and the well deepens as the piece builds
    for node, extra in (("petals", 24.0), ("petalsInner", 24.0), ("wellHex", 8.0), ("wellSquare", 8.0)):
        s.route(R("intensity", P % node + "distribution/count", extra, threshold="binary", thresholdLevel=0.55))
    # ---- MIDI: the pitch classes light their diamonds and spokes (a chord draws its polygon); polyphony is the star's
    # polygon order (STRUCTURE); a held note keeps the armillary open; velocity flares the core
    for k in range(12):
        sig = "notes.class.%d" % k
        s.route(R(sig, P % ("pc%d" % k) + "material/emissive", 7.0, attackMs=0, decayMs=420),
                R(sig, P % ("spoke%d" % k) + "material/emissive", 4.5, attackMs=0, decayMs=520))
    # the chord's own polygon in the heart: as many sides as notes sounding (3 to 8), shown only while a chord sounds
    s.route(R("polyphony", P % "chordGon" + "source/majorSegments", 1.0, gain=8.0, offset=-3.0, clampEnabled=True,
              clampMin=0.0, clampMax=5.0),
            R("polyphony", P % "chordGon" + "material/emissive", 5.0, threshold="binary", thresholdLevel=0.3,
              attackMs=40, decayMs=500))
    for k, name in enumerate(rings):
        s.route(R("held", P % name + "deform/2/phase", (0.5 if k % 2 == 0 else -0.5), **MEDIUM))
    s.route(R("noteEnv", P % "core" + "material/emissive", 10.0, depth="lastVelocity", attackMs=0, decayMs=300),
            R("noteEnv", P % "seedRim" + "material/emissive", 3.0, depth="lastVelocity", attackMs=0, decayMs=400))
    # ---- MOD WHEEL: opens the whole armillary into a sphere by hand
    wheel = s.modwheel()
    for k, name in enumerate(rings):
        s.route(R(wheel, P % name + "deform/2/phase", 1.5 if k % 2 == 0 else -1.5, attackMs=60, decayMs=60))


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.0, "release": 1.1,
                  "floorDb": -44.0, "rangeDb": 42.0}     # mastered music does not saturate the levels
    s.environment = {
        "intensity": 0.0, "background": hexrgb(INK), "fogColor": [0, 0, 0], "volumeDensity": 0.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb(INK), "horizonColor": hexrgb(INK),
                "groundColor": hexrgb(INK), "haze": 0.6, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.composition = {"focalPoints": [{"name": "seed", "position": [0.0, 0.0, -2.4], "radius": 1.0, "weight": 1.0}]}

    # ---- the ground: an ultramarine halo disc far behind, and in front of it the flower-of-life lattice, which fades
    # out toward the centre. Opaque lines cannot fade to nothing, so where the lattice fades its lines take the
    # halo's own colour at that pixel (the same radial function, scaled for the 10 m between them) and vanish into it.
    halo_r = 44.0                      # the halo's radius at z = -90 (camera at z = 26: 116 m away)
    lat_z, halo_z = -80.0, -90.0
    k_proj = (CAM_Z - halo_z) / (CAM_Z - lat_z)       # a lattice point at r covers the halo at r * k_proj
    halo_col = hexrgb("#1b2380")

    def halo_ops(dst, scale_r):
        """dst = halo colour at |p.xy| * scale_r (radius halo_r, falloff (1 - r/R)^1.7)."""
        return [
            {"kind": "multiply", "dst": dst, "srcA": 0, "srcB": 0},
            {"kind": "gradient", "dst": dst, "srcA": dst, "constant": [1.0, 1.0, 0.0, 0.0],
             "value": (scale_r / halo_r) ** 2},
            {"kind": "power", "dst": dst, "srcA": dst, "value": 0.5},
            {"kind": "remap", "dst": dst, "srcA": dst, "value": 1, "constant": [0.0, 1.0, 1.0, 0.0]},
            {"kind": "power", "dst": dst, "srcA": dst, "value": 1.7},
            {"kind": "constant", "dst": 7, "constant": halo_col + [1.0]},
            {"kind": "multiply", "dst": dst, "srcA": dst, "srcB": 7},
        ]
    s.program({"name": "sgHalo", "ops": [{"kind": "input", "dst": 0, "input": "worldPosition"}] + halo_ops(3, 1.0),
               "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 3, "emissionIntensity": 1.0,
               "opacity": -1})
    s.proc("halo", {"kind": "cylinder", "radius": 120.0, "height": 0.1, "radialSegments": 96, "caps": True},
           material=glow(INK, 1.0, program="sgHalo"), transform=face(halo_z))
    s.program({
        "name": "sgLattice",
        "ops": [{"kind": "input", "dst": 0, "input": "worldPosition"}] + halo_ops(5, k_proj) + [
            {"kind": "multiply", "dst": 1, "srcA": 0, "srcB": 0},
            {"kind": "gradient", "dst": 1, "srcA": 1, "constant": [1.0, 1.0, 0.0, 0.0], "value": 1.0 / 900.0},
            {"kind": "power", "dst": 1, "srcA": 1, "value": 0.5},                      # |p.xy| / 30
            {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.5, 1.0, 0.0, 0.0]},
            {"kind": "constant", "dst": 2, "constant": hexrgb(GOLD, 0.3) + [1.0]},
            {"kind": "multiply", "dst": 3, "srcA": 2, "srcB": 1},
            {"kind": "add", "dst": 3, "srcA": 3, "srcB": 5},
        ],
        "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 3, "emissionIntensity": 1.0, "opacity": -1,
    })
    # flower of life: circles of radius r on a hexagonal lattice of spacing r (two offset rectangular grids)
    r_l = 2.8
    for k, off in enumerate(((0.0, 0.0), (r_l * 0.5, r_l * math.sqrt(3.0) * 0.5))):
        s.proc("lattice%d" % k, torus(r_l, 0.045, 64, 4),
               distribution={"kind": "grid", "gridCount": [33, 1, 11],
                             "gridSpacing": [r_l, 1.0, r_l * math.sqrt(3.0)]},
               material=glow(GOLD, 0.3, program="sgLattice"),
               transform={"position": [off[0], off[1], lat_z], "rotation": [90.0, 0.0, 0.0], "scale": [1, 1, 1]})

    # ---- the crown (the nearest layer): an outer double rim, 48 beads, 24 vesica petals (ellipses, long radially)
    s.proc("rimOuter", torus(6.85, 0.035, 192, 6), material=glow(GOLD, 2.6), transform=face(0.0))
    s.proc("rimInner", torus(6.55, 0.018, 192, 6), material=glow(PALE_GOLD, 2.0), transform=face(0.0))
    s.proc("beads", {"kind": "sphere", "radius": 0.07, "segments": 12, "rings": 6},
           distribution={"kind": "radial", "count": 48, "radius": 6.7, "plane": "xy", "orientation": "outward"},
           material=glow(IVORY, 3.2), transform={"position": [0, 0, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    # petals: an ellipse in the source's XZ plane (X tangent, Z radial under `outward`), squashed across
    s.proc("petals", torus(0.95, 0.03, 64, 5),
           distribution={"kind": "radial", "count": 24, "radius": 5.55, "plane": "xy", "orientation": "outward"},
           material=glow(GOLD, 2.4), deformers=[{"kind": "twist", "amount": 0.0, "speed": 0.0, "phase": 0.0,
                                                 "axis": [0, 0, 1], "space": "local"}],
           transform={"position": [0, 0, -0.2], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
           extra={"sourceTransform": {"scale": [0.36, 1.0, 1.0]}})
    s.proc("petalsInner", torus(0.62, 0.022, 64, 5),
           distribution={"kind": "radial", "count": 24, "radius": 4.95, "plane": "xy", "orientation": "outward",
                         "startAngle": math.pi / 24.0, "endAngle": math.pi / 24.0 + 2.0 * math.pi},
           material=glow(PALE_GOLD, 1.8),
           transform={"position": [0, 0, -0.3], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
           extra={"sourceTransform": {"scale": [0.42, 1.0, 1.0]}})
    # ---- the 12-pointed star: four triangles turned 30 degrees apart (two hexagrams), one ring of its tips
    s.proc("star", torus(4.30, 0.024, 3, 4),
           distribution={"kind": "radial", "count": 4, "radius": 0.001, "plane": "xy", "orientation": "outward"},
           material=glow(PALE_GOLD, 2.2),
           deformers=[{"kind": "twist", "amount": 0.0, "speed": BASE * 0.5, "phase": 0.0, "axis": [0, 1, 0],
                       "space": "local"}],
           transform={"position": [0, 0, -0.45], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- the armillary rings
    for name, rad, n, lw, col, inten, z, k, axis in ARMILLARY:
        s.proc(name, torus(rad, lw, n, 6), material=glow(col, inten), deformers=spin_open(BASE * k, axis),
               transform=face(z))

    # ---- the well: nested polygons receding along -Z under a world twist (a spiral of squares and hexagons)
    s.proc("wellHex", torus(2.9, 0.026, 6, 4),
           distribution={"kind": "linear", "count": 12, "start": [0.0, 0.0, -48.0], "end": [0.0, 0.0, -3.0]},
           material=glow(GOLD, 1.0),
           material_variation={"emissiveGradient": 2.2},
           deformers=[{"kind": "twist", "amount": 0.055, "speed": BASE * 0.6, "phase": 0.0, "axis": [0, 0, 1],
                       "center": [0, 0, 0], "space": "world"}],
           extra={"sourceTransform": {"rotation": [90.0, 0.0, 0.0]}})
    s.proc("wellSquare", torus(2.75, 0.02, 4, 4),
           distribution={"kind": "linear", "count": 12, "start": [0.0, 0.0, -50.0], "end": [0.0, 0.0, -5.0]},
           material=glow(PALE_GOLD, 0.8),
           material_variation={"emissiveGradient": 2.2},
           deformers=[{"kind": "twist", "amount": -0.04, "speed": -BASE * 0.4, "phase": 0.0, "axis": [0, 0, 1],
                       "center": [0, 0, 0], "space": "world"}],
           extra={"sourceTransform": {"rotation": [90.0, 0.0, 0.0]}})

    # ---- the seed of life: seven circles (six round one) and the core
    s.proc("seed", torus(0.55, 0.02, 96, 5),
           distribution={"kind": "radial", "count": 6, "radius": 0.55, "plane": "xy", "orientation": "outward"},
           material=glow(IVORY, 2.6),
           transform={"position": [0, 0, -2.4], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("seedCentre", torus(0.55, 0.02, 96, 5), material=glow(IVORY, 2.6), transform=face(-2.4))
    s.proc("seedRim", torus(1.1, 0.03, 128, 5), material=glow(VERMILION, 3.0), transform=face(-2.4))
    s.proc("chordGon", torus(1.02, 0.03, 3, 4), material=glow(VERMILION, 0.0), transform=face(-2.35))
    s.proc("core", {"kind": "sphere", "radius": 0.16, "segments": 24, "rings": 12},
           material=glow("#ffe6c2", 9.0), transform={"position": [0, 0, -2.4], "rotation": [0, 0, 0],
                                                      "scale": [1, 1, 1]})

    # ---- camera: frontal on the axis, a slow breathing arc (4 degrees, 64 s)
    s.params_({"camera/lens/focalLength": 50.0, "post/bloom/intensity": 0.55, "post/bloom/threshold": 0.6,
               "post/bloom/emissionWeight": 1.0, "post/output/vignette": 0.45, "post/output/grain": 0.008,
               "post/tonemap/operator": 4, "temporal/feedback/enabled": True, "temporal/feedback/frames": 8,
               "temporal/feedback/amount": 0.7, "temporal/feedback/decay": 0.5, "temporal/feedback/zoom": 1.0,
               "temporal/feedback/rotate": 0.0, "temporal/feedback/hue": 0.0})
    s.camera = {"mode": 1, "position": [0.0, 0.0, CAM_Z], "target": [0.0, 0.0, -2.0], "fov": 30.0,
                "orbitSpeed": 0.0}
    s.drift_camera([0.0, 0.0, CAM_Z], [0.0, 0.0, -2.0], period=64.0, amp=(1.4, 0.5, 0.6), tamp=(0.0, 0.0, 0.0))

    # ---- the twelve pitch classes: a clock of faint diamonds and spokes round the crown (C at the top, clockwise,
    # chromatic, so a chord's interval structure is its shape: an augmented triad is a triangle, a diminished seventh a
    # square). A sounding class lights its diamond and spoke, so a chord draws its own polygon.
    for k in range(12):
        ang = math.radians(90.0 - 30.0 * k)
        c, sn = math.cos(ang), math.sin(ang)
        s.proc("pc%d" % k, torus(0.34, 0.026, 4, 4), material=glow(PALE_GOLD, 0.35),
               transform={"position": [7.55 * c, 7.55 * sn, 0.1], "rotation": [90.0, 0.0, math.degrees(ang) - 90.0],
                          "scale": [0.55, 1.0, 1.0]})
        s.proc("spoke%d" % k, {"kind": "box", "size": [0.035, 0.035, 2.4], "subdivisions": 1},
               material=glow(GOLD, 0.0),
               transform={"position": [5.75 * c, 5.75 * sn, -0.15], "rotation": [0.0, 0.0, 0.0], "scale": [1, 1, 1]},
               extra={"sourceTransform": {"rotation": [math.degrees(-ang) + 90.0, 90.0, 0.0]}})

    # ---- sparks off the crown (the snare) and beads along the armillary (the hats)
    s.nodes.append({"name": "crownPath", "kind": "spline", "spline": {
        "kind": "catmullRom", "generator": "circle", "closed": True, "count": 48, "radius": 5.6,
        "center": [0.0, 0.0, -0.2], "axis": [0.0, 0.0, 1.0], "up": [0.0, 0.0, 1.0], "samplesPerSegment": 8}})
    s.particles("sparks", capacity=4000, seed=17, shape="spline", spline="crownPath", position=[0, 0, 0],
                extent=[0.05, 0.05, 0.05], direction=[-1.0, 0.0, 0.0], spawnRate=0.0, lifetimeMin=0.5,
                lifetimeMax=1.1, spread=0.08, speedMin=2.5, speedMax=5.5, gravity=[0, 0, 0], drag=1.6,
                sizeStart=0.06, sizeEnd=0.0, colorStart=hexrgb("#ffd9a0") + [1.0], colorEnd=hexrgb(VERMILION) + [0.0],
                emissive=6.0, blend="additive", velocityStretch=1.2, stretchMax=0.35)
    for name, rad, *_ in ARMILLARY:
        s.nodes.append({"name": name + "Path", "kind": "spline", "spline": {
            "kind": "catmullRom", "generator": "circle", "closed": True, "count": 48, "radius": rad,
            "center": [0.0, 0.0, 0.0], "axis": [0.0, 0.0, 1.0], "up": [0.0, 0.0, 1.0], "samplesPerSegment": 8}})
    s.particles("beadRun", capacity=3000, seed=23, shape="spline", spline="ring1Path", position=[0, 0, 0],
                extent=[0.02, 0.02, 0.02], direction=[0.0, 0.0, 1.0], spawnRate=0.0, lifetimeMin=0.25,
                lifetimeMax=0.6, spread=0.0, speedMin=0.6, speedMax=1.4, gravity=[0, 0, 0], drag=0.5,
                sizeStart=0.075, sizeEnd=0.0, colorStart=hexrgb("#fffaf0") + [1.0], colorEnd=hexrgb(GOLD) + [0.0],
                emissive=7.0, blend="additive")
    s.particles("beads3", capacity=3000, seed=29, shape="spline", spline="ring3Path", position=[0, 0, -1.2],
                extent=[0.02, 0.02, 0.02], direction=[0.0, 0.0, -1.0], spawnRate=0.0, lifetimeMin=0.25,
                lifetimeMax=0.6, spread=0.0, speedMin=0.6, speedMax=1.4, gravity=[0, 0, 0], drag=0.5,
                sizeStart=0.06, sizeEnd=0.0, colorStart=hexrgb("#fffaf0") + [1.0], colorEnd=hexrgb(PALE_GOLD) + [0.0],
                emissive=7.0, blend="additive")

    instrument(s)
    s.region("seed", centre=[0.0, 0.0, -2.4], radius=1.4)
    s.region("armillary", centre=[0.0, 0.0, -1.2], radius=4.2)
    s.region("crown", box=[0.2, 0.0, 0.8, 1.0])
    return s

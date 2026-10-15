"""FEEDBACK MIRROR (digital, feedback-driven). Howlround: a camera pointed at its own monitor. The screen's frame
recurs inward as a corridor of screens that twists away to a vanishing point, a glyph of light burns in the middle of
it, and the music turns, deepens and recolours the loop, so the world is the instrument's own echo.

Art pass 2026-10 (docs/prototypes/sonic-garden/ART-PASS-2026-10-feedback-mirror.md). The first pass was a small gold
sigil with a short comet-tail of echoes in an empty violet void: the feedback had nothing to recur but the sigil, so
nine tenths of the frame did nothing. The direction now is the analogue video-feedback tunnel (the BBC's "howlround"
Doctor Who titles of 1963-73, Crutchfield's "Space-time dynamics in video feedback", 1984, the Vasulkas' and Nam June
Paik's camera-monitor loops):
- the FRAME is the subject's twin: a glowing monitor bezel with viewfinder brackets at the frame's edge, which the loop
  (ADR-1066's FIR feedback, 24 taps) carries inward as a corridor of nested screens, each a step smaller, turned and a
  step further round the hue circle, falling into a vanishing point;
- the GLYPH (ring, triangle, beads, core) on the right third, whose recent past spirals into the corridor, so a
  melody leaves its contour in the echoes;
- the GLASS: scanlines and a soft bloom, a deep blue-black ground.
"""
import math

from .. import kit
from ..kit import (R, M, hexrgb, scale3, sd_sphere, sd_torus, sd_capsule, sd_union, sd_move, sd_rot, SLOW, MEDIUM,
                   FAST, HIT, SNAP, VERY_SLOW)

ID = "feedback-mirror"
TITLE = "Feedback Mirror"

GLASS = "#05040e"
GOLD = "#ffbf5a"
PALE = "#fff0cf"
MAGENTA = "#ff4f9a"
CYAN = "#5ad8ff"

GLYPH = (1.2, 0.05, 0.0)       # on the right third (the camera sees 3.4 m either side at 8 m)
RING_R = 0.95
# the bezel: a rounded screen outline just inside the frame's edge (the camera sees 6.83 x 3.84 m at z = 0)
BEZ_W, BEZ_H, BEZ_T = 6.2, 3.36, 0.035

DESIGN = {
    "category": "digital",
    "thesis": "Howlround: a camera pointed at its own monitor. The screen's frame recurs inward as a corridor of "
              "screens twisting to a vanishing point, a glyph of light burns in it, and the music turns, deepens and "
              "recolours the loop, so the world is the instrument's own echo.",
    "composition": {
        "background": "the corridor of nested screens falling to the vanishing point at the frame's centre",
        "midground": "the glyph's echoes spiralling into the corridor",
        "foreground": "the glowing monitor bezel and its viewfinder brackets at the frame's edge",
        "focal": "the gold glyph on the right third",
        "secondary": ["the corridor's twist and colour walk", "the vanishing point"],
        "atmosphere": "(none): CRT glass, scanlines",
        "post": "bloom; scanlines; grain on the hats; an RGB split on the snare",
        "camera": "locked off, square to the screen (the loop's camera)",
    },
    "palette": {"dominant": GLASS, "secondary": CYAN, "accent": MAGENTA, "highlight": GOLD,
                "background_value": "near black (blue)",
                "saturation": "the bezel and the glyph are gold and pale; their echoes walk the hue circle, step by "
                              "step, toward cyan and violet"},
    "motion": {
        "very_slow": ["the corridor's breath (the loop's slow zoom)", "the glyph's drift"],
        "medium": ["the corridor's twist (melody)", "its depth (bass, sustain)"],
        "fast": ["the triangle's turn per note"],
        "extremely_fast": ["the kick's zoom punch and bezel flash", "the snare's split"],
    },
    "vocabulary": [
        ["melodic", "notes.lastPitch", "the triangle turns to the pitch, so a melody draws its contour into the echoes"],
        ["interval", "notes.interval", "a rising step twists the corridor one way, a falling one the other"],
        ["sustained", "response.sustain", "the echoes last longer: the corridor reaches further"],
        ["bass", "response.bass", "the corridor deepens and twists: each screen falls further in, turned further"],
        ["kick", "response.kick", "a zoom punch: the corridor jumps inward; the bezel and the core flash"],
        ["snare", "response.snare", "an RGB split across the frame"],
        ["hat", "response.hat", "grain and a flicker in the scanlines"],
        ["chord", "notes.polyphony", "the hue step: a chord colours the corridor faster round the circle, "
         "dissonance further"],
        ["brightness", "sonic.brightness", "a bright timbre also walks the corridor's colour further round"],
        ["velocity", "notes.lastVelocity", "the glyph's brightness on each note"],
        ["silence", "(no input)", "the corridor breathes slowly, the inner ring turns; a quiet gold tunnel"],
    ],
    "tier": "light: one SDF glyph, the bezel as unlit boxes, the feedback FIR (24 taps)",
}


def edge(a, b, r, m):
    """A capsule from point a to point b in the XY plane."""
    mx, my = (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5
    ln = math.hypot(b[0] - a[0], b[1] - a[1])
    ang = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
    return sd_move((mx, my, 0.0), sd_rot((0.0, 0.0, ang - 90.0), sd_capsule(r, ln, m=m)))


def glyph():
    """The sigil, facing +Z: a ring, an inscribed triangle with beads at its vertices (it turns, node `tri`), an
    inner ring of beads (it counter-turns, `inner`), and a core (`core`)."""
    ring = sd_rot((90.0, 0.0, 0.0), sd_torus(RING_R, 0.02, m=0))
    verts = [(RING_R * 0.93 * math.cos(math.radians(90 + 120 * i)),
              RING_R * 0.93 * math.sin(math.radians(90 + 120 * i))) for i in range(3)]
    tri = sd_union(*[edge(verts[i], verts[(i + 1) % 3], 0.014, 1) for i in range(3)],
                   *[sd_move((v[0], v[1], 0.0), sd_sphere(0.05, m=1)) for v in verts])
    tri = sd_rot((0.0, 0.0, 0.0), tri, name="tri")
    inner = sd_union(*[sd_move((0.48 * math.cos(math.radians(a)), 0.48 * math.sin(math.radians(a)), 0.0),
                               sd_sphere(0.024, m=0)) for a in range(0, 300, 12)])
    inner = sd_rot((0.0, 0.0, 0.0), inner, name="inner")
    core = sd_sphere(0.085, name="core", m=2)
    return sd_move(GLYPH, sd_union(ring, tri, inner, core), name="glyph")


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.0, "release": 1.2}
    s.environment = {
        "intensity": 0.0, "background": hexrgb(GLASS), "fogColor": [0, 0, 0], "volumeDensity": 0.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb(GLASS), "horizonColor": hexrgb("#08061a"),
                "groundColor": hexrgb(GLASS), "haze": 0.9, "sunIntensity": 0.0,
                "intensity": 1.0, "background": True, "useKeyLight": False},
    }
    s.composition = {"focalPoints": [{"name": "glyph", "position": list(GLYPH), "radius": RING_R, "weight": 1.0}]}
    s.unshadowed_key()  # (nothing here is lit; authoring a light turns the default shadowed key off)
    root = glyph()
    s.sdf("glyph", root, (-0.15, -1.2, -0.3), (2.55, 1.3, 0.3),
          surfaces=[{"color": [0.0, 0.0, 0.0], "emission": hexrgb(GOLD, 3.0)},
                    {"color": [0.0, 0.0, 0.0], "emission": hexrgb(PALE, 2.4)},
                    {"color": [0.0, 0.0, 0.0], "emission": hexrgb("#ffffff", 4.0)}],
          material={"baseColor": [0, 0, 0], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                    "roughness": 0.5, "metallic": 0.0},
          max_steps=96, epsilon=0.0006, step_scale=0.9, max_distance=30.0)

    # ---- the bezel: four bars and eight viewfinder brackets, unlit and bright
    bez = {"baseColor": [0, 0, 0], "emissiveColor": hexrgb(PALE), "emissiveIntensity": 7.0, "roughness": 1.0,
           "metallic": 0.0, "unlit": True}
    brk = dict(bez, emissiveColor=hexrgb(GOLD), emissiveIntensity=10.0)
    for name, size, pos in (("bezTop", (BEZ_W, BEZ_T, 0.02), (0.0, BEZ_H * 0.5, 0.0)),
                            ("bezBottom", (BEZ_W, BEZ_T, 0.02), (0.0, -BEZ_H * 0.5, 0.0)),
                            ("bezLeft", (BEZ_T, BEZ_H, 0.02), (-BEZ_W * 0.5, 0.0, 0.0)),
                            ("bezRight", (BEZ_T, BEZ_H, 0.02), (BEZ_W * 0.5, 0.0, 0.0))):
        s.proc(name, {"kind": "box", "size": list(size), "subdivisions": 1}, material=bez, position=pos)
    # viewfinder brackets, a little inside the bezel's corners (L shapes: two bars each)
    bx, by, arm, t = BEZ_W * 0.5 - 0.28, BEZ_H * 0.5 - 0.24, 0.42, 0.05
    for i, (sx, sy) in enumerate(((1, 1), (-1, 1), (1, -1), (-1, -1))):
        s.proc("brkH%d" % i, {"kind": "box", "size": [arm, t, 0.02], "subdivisions": 1}, material=brk,
               position=(sx * (bx - arm * 0.5 + t * 0.5), sy * by, 0.0))
        s.proc("brkV%d" % i, {"kind": "box", "size": [t, arm, 0.02], "subdivisions": 1}, material=brk,
               position=(sx * bx, sy * (by - arm * 0.5 + t * 0.5), 0.0))
    # the corridor's slow breath: the loop's zoom swings 0.93 - 0.915 over 24 s (a seamless loop; the routes add to it)
    s.track("temporal/feedback/zoom", [{"time": 0.0, "value": 0.93, "interp": "smooth"},
                                       {"time": 12.0, "value": 0.915, "interp": "smooth"},
                                       {"time": 24.0, "value": 0.93, "interp": "smooth"}], loop=24.0)

    # ---- camera: locked off, square to the screen
    s.camera = {"mode": 1, "position": [0.0, 0.0, 8.0], "target": [0.0, 0.0, 0.0], "fov": 30.0, "orbitSpeed": 0.0}
    # a breath of drift for the glyph so the corridor is never a still image
    s.track("sdf/glyph/node/glyph/translation", [
        {"time": 0.0, "value": list(GLYPH), "interp": "smooth"},
        {"time": 30.0, "value": [GLYPH[0] - 0.1, GLYPH[1] + 0.08, 0.0], "interp": "smooth"},
        {"time": 60.0, "value": list(GLYPH), "interp": "smooth"}], loop=60.0)

    # ---- the loop's resting state: 24 taps falling inward, a slow twist, a walk round the hue circle
    s.params_({
        "temporal/feedback/enabled": True, "temporal/feedback/frames": 24, "temporal/feedback/amount": 1.6,
        "temporal/feedback/decay": 0.9, "temporal/feedback/zoom": 0.93, "temporal/feedback/rotate": 1.6,
        "temporal/feedback/hue": -0.026, "temporal/feedback/driftX": 0.0, "temporal/feedback/driftY": 0.0,
        "post/bloom/intensity": 0.5, "post/bloom/threshold": 0.8, "post/output/vignette": 0.45,
        "post/output/grain": 0.015, "camera/lens/focalLength": 50.0, "post/tonemap/operator": 3,
        "post/split/amount": 0.0, "post/split/angle": 0.0, "post/split/spectral": 1.0,
        "post/display/scanlines": 0.22,
    })

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # melody: the triangle turns to the pitch (a spring, so it swings to each note), the inner ring counter-turns
    s.route(R("lastPitch", "sdf/glyph/node/tri/rotation", 540.0, comp=2, op="add", springHz=2.2, springDamping=0.55),
            R("lastPitch", "sdf/glyph/node/inner/rotation", -300.0, comp=2, op="add", springHz=1.4,
              springDamping=0.7))
    # the slow turn of the inner ring at rest, so silence still moves
    s.track("sdf/glyph/node/inner/rotation", [{"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "linear"},
                                             {"time": 120.0, "value": [0.0, 0.0, 360.0], "interp": "linear"}],
            loop=120.0)
    # interval: a rising step twists the corridor one way, a falling step the other
    s.route(R("interval", "temporal/feedback/rotate", 6.0, attackMs=350, decayMs=350))
    # sustain: the echoes last; bass: the corridor deepens; kick: a zoom punch, the bezel and the core flash
    s.route(R("sustain", "temporal/feedback/decay", 0.06, **SLOW),
            R("bass", "temporal/feedback/zoom", -0.03, attackMs=50, decayMs=700),
            R("bass", "temporal/feedback/rotate", 2.4, attackMs=120, decayMs=900),
            R("kick", "temporal/feedback/zoom", -0.05, attackMs=0, decayMs=200),
            R("kick", "sdf/glyph/surface/2/emission", 8.0, attackMs=0, decayMs=240),
            R("kick", "sdf/glyph/node/core/radius", 0.05, attackMs=0, decayMs=260))
    for name in ("bezTop", "bezBottom", "bezLeft", "bezRight"):
        s.route(R("kick", "procedural/%s/material/emissive" % name, 10.0, attackMs=0, decayMs=220))
    # chord: the hue step (polyphony colours faster, tension further)
    s.map(M("hueStep", [("polyphony", 1.0), ("tension", 1.0), ("brightness", 0.6)], "max"))
    s.route(R("visual.hueStep", "temporal/feedback/hue", -0.03, **MEDIUM))
    # notes: the glyph flares with each note's velocity
    for c in range(3):
        s.route(R("noteEnv", "sdf/glyph/surface/0/emission", 5.0, comp=c, depth="lastVelocity", attackMs=0,
                  decayMs=300),
                R("noteEnv", "sdf/glyph/surface/1/emission", 4.0, comp=c, depth="lastVelocity", attackMs=0,
                  decayMs=260))
    # snare: an RGB split; hat: grain and a scanline flicker
    s.route(R("snare", "post/split/amount", 10.0, attackMs=0, decayMs=160),
            R("hat", "post/output/grain", 0.05, attackMs=0, decayMs=60),
            R("hat", "post/display/scanlines", 0.2, attackMs=0, decayMs=60),
            R("kick", "post/lens/chromaticAberration", 0.012, attackMs=0, decayMs=120),
            R("noteEnv", "post/bloom/intensity", 0.15, attackMs=0, decayMs=300))

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region("glyph", centre=list(GLYPH), radius=RING_R * 1.1)
    s.region("tunnel", box=[0.3, 0.25, 0.7, 0.75])
    s.region("bezel", box=[0.0, 0.0, 1.0, 0.1])
    return s

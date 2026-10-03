"""FEEDBACK MIRROR (digital, feedback-driven). A single glyph of light inside a video feedback loop: the music turns,
zooms and recolours the loop, so the world is the instrument's own echo.

Composition (SCENE-CATALOG.md #15): one gold sigil -- a ring, an inscribed triangle that the melody turns, an inner
ring that counter-turns, a core that the kick pulses -- on the right third of a violet void. The feedback
(ADR-1066: an exact FIR over the last 24 clean frames) carries every frame of it toward the frame's centre, smaller
and turned and a step further round the hue circle, so the glyph's own recent past is a logarithmic spiral of echoes
falling into the vanishing point (Crutchfield's video feedback, MilkDrop's tunnels). Because the echoes are the past,
a melody leaves its shape in them: a rising line winds the spiral one way, a falling line the other.
"""
import math

from .. import kit, signals
from ..kit import (R, M, hexrgb, scale3, sd_sphere, sd_torus, sd_capsule, sd_union, sd_move, sd_rot, SLOW, MEDIUM,
                   FAST, HIT, SNAP, VERY_SLOW)

ID = "feedback-mirror"
TITLE = "Feedback Mirror"

VIOLET = "#140726"
GOLD = "#ffbf5a"
PALE = "#fff0cf"
MAGENTA = "#ff4f9a"

GLYPH = (1.14, 0.12, 0.0)      # on the right third (the camera sees 3.4 m either side at 8 m)
RING_R = 0.82

DESIGN = {
    "category": "digital",
    "thesis": "A single glyph of light inside a video feedback loop: the music turns, zooms and recolours the loop, "
              "so the world is the instrument's own echo.",
    "composition": {
        "background": "a deep violet void, darker at the edges",
        "midground": "the spiral of the glyph's echoes, falling to the vanishing point at the frame's centre",
        "foreground": "(none: negative space)",
        "focal": "the gold glyph on the right third",
        "secondary": ["the echo spiral", "the vanishing point"],
        "atmosphere": "(none)",
        "post": "bloom; grain on the hats; an RGB split on the snare",
        "camera": "locked off",
    },
    "palette": {"dominant": VIOLET, "secondary": MAGENTA, "accent": "#2a0f4f", "highlight": GOLD,
                "background_value": "near black (violet)",
                "saturation": "the glyph is gold; its echoes walk round the hue circle by the feedback's own step"},
    "motion": {
        "very_slow": ["the hue's walk", "the glyph's drift"],
        "medium": ["the spiral's turn (melody)", "the tunnel's depth (bass)"],
        "fast": ["the triangle's turn per note"],
        "extremely_fast": ["the kick's zoom punch", "the snare's split"],
    },
    "vocabulary": [
        ["melodic", "notes.lastPitch", "the triangle turns to the pitch, so a melody draws its contour into the echoes"],
        ["interval", "notes.interval", "a rising step winds the spiral clockwise, a falling one back"],
        ["sustained", "response.sustain", "the echoes last longer: the tunnel deepens"],
        ["bass", "response.bass", "the tunnel deepens: the echoes pour farther in"],
        ["kick", "response.kick", "a zoom punch: the echoes jump inward; the core flares"],
        ["snare", "response.snare", "an RGB split across the frame"],
        ["hat", "response.hat", "grain"],
        ["chord", "notes.polyphony", "the hue step: a chord colours the echoes faster, dissonance "
         "more"],
        ["velocity", "notes.lastVelocity", "the glyph's brightness on each note"],
        ["silence", "(no input)", "the glyph turns slowly; a short gold tunnel"],
    ],
    "tier": "light: one SDF glyph, the feedback FIR (24 taps, about 1 ms at 1080p)",
}


def edge(a, b, r, m):
    """A capsule from point a to point b in the XY plane."""
    mx, my = (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5
    ln = math.hypot(b[0] - a[0], b[1] - a[1])
    ang = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
    return sd_move((mx, my, 0.0), sd_rot((0.0, 0.0, ang - 90.0), sd_capsule(r, ln, m=m)))


def glyph():
    """The sigil, facing +Z: a ring, an inscribed triangle with beads at its vertices (it turns, node `tri`), an
    inner ring with a gap (it counter-turns, `inner`), and a core (`core`)."""
    ring = sd_rot((90.0, 0.0, 0.0), sd_torus(RING_R, 0.016, m=0))
    verts = [(RING_R * 0.93 * math.cos(math.radians(90 + 120 * i)),
              RING_R * 0.93 * math.sin(math.radians(90 + 120 * i))) for i in range(3)]
    tri = sd_union(*[edge(verts[i], verts[(i + 1) % 3], 0.011, 1) for i in range(3)],
                   *[sd_move((v[0], v[1], 0.0), sd_sphere(0.042, m=1)) for v in verts])
    tri = sd_rot((0.0, 0.0, 0.0), tri, name="tri")
    inner = sd_union(*[sd_move((0.42 * math.cos(math.radians(a)), 0.42 * math.sin(math.radians(a)), 0.0),
                               sd_sphere(0.02, m=0)) for a in range(0, 300, 12)])
    inner = sd_rot((0.0, 0.0, 0.0), inner, name="inner")
    core = sd_sphere(0.07, name="core", m=2)
    return sd_move(GLYPH, sd_union(ring, tri, inner, core), name="glyph")


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.0, "release": 1.2}
    s.environment = {
        "intensity": 0.0, "background": hexrgb(VIOLET), "fogColor": [0, 0, 0], "volumeDensity": 0.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": scale3(hexrgb("#0b0418"), 1.0), "horizonColor": hexrgb(VIOLET),
                "groundColor": scale3(hexrgb("#0b0418"), 1.0), "haze": 0.9, "sunIntensity": 0.0,
                "intensity": 1.0, "background": True, "useKeyLight": False},
    }
    s.composition = {"focalPoints": [{"name": "glyph", "position": list(GLYPH), "radius": RING_R, "weight": 1.0}]}
    root = glyph()
    s.sdf("glyph", root, (-0.5, -1.0, -0.3), (2.8, 1.25, 0.3),
          surfaces=[{"color": [0.0, 0.0, 0.0], "emission": hexrgb(GOLD, 3.0)},
                    {"color": [0.0, 0.0, 0.0], "emission": hexrgb(PALE, 2.4)},
                    {"color": [0.0, 0.0, 0.0], "emission": hexrgb("#ffffff", 4.0)}],
          material={"baseColor": [0, 0, 0], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                    "roughness": 0.5, "metallic": 0.0},
          max_steps=96, epsilon=0.0006, step_scale=0.9, max_distance=30.0)

    # ---- camera: locked off, square to the glyph's plane
    s.camera = {"mode": 1, "position": [0.0, 0.0, 8.0], "target": [0.0, 0.0, 0.0], "fov": 30.0, "orbitSpeed": 0.0}
    # a breath of drift so the void is never a still image (its period is the scene's slowest motion)
    s.track("sdf/glyph/node/glyph/translation", [
        {"time": 0.0, "value": list(GLYPH), "interp": "smooth"},
        {"time": 30.0, "value": [GLYPH[0] - 0.08, GLYPH[1] + 0.06, 0.0], "interp": "smooth"},
        {"time": 60.0, "value": list(GLYPH), "interp": "smooth"}], loop=60.0)

    # ---- the loop's resting state: 24 taps falling inward, a slow turn, a slow hue walk
    s.params_({
        "temporal/feedback/enabled": True, "temporal/feedback/frames": 24, "temporal/feedback/amount": 1.0,
        "temporal/feedback/decay": 0.84, "temporal/feedback/zoom": 0.972, "temporal/feedback/rotate": 3.0,
        "temporal/feedback/hue": -0.004, "temporal/feedback/driftX": 0.0, "temporal/feedback/driftY": 0.0,
        "post/bloom/intensity": 0.45, "post/bloom/threshold": 0.8, "post/output/vignette": 0.5,
        "post/output/grain": 0.012, "camera/lens/focalLength": 50.0, "post/tonemap/operator": 3,
        "post/split/amount": 0.0, "post/split/angle": 0.0, "post/split/spectral": 1.0,
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
    # interval: a rising step winds the spiral one way, a falling step the other
    s.route(R("interval", "temporal/feedback/rotate", 9.0, attackMs=350, decayMs=350))
    # sustain: the echoes last; bass: the tunnel stretches; kick: a zoom punch and the core flares
    s.route(R("sustain", "temporal/feedback/decay", 0.1, **SLOW),
            R("bass", "temporal/feedback/zoom", -0.012, attackMs=50, decayMs=700),
            R("kick", "temporal/feedback/zoom", -0.035, attackMs=0, decayMs=200),
            R("kick", "sdf/glyph/surface/2/emission", 8.0, attackMs=0, decayMs=240),
            R("kick", "sdf/glyph/node/core/radius", 0.05, attackMs=0, decayMs=260))
    # chord: the hue step (polyphony colours faster, tension more)
    s.map(M("hueStep", [("polyphony", 1.0), ("tension", 1.0)], "max"))
    s.route(R("visual.hueStep", "temporal/feedback/hue", -0.018, **MEDIUM))   # gold -> rose -> magenta -> violet
    # notes: the glyph flares with each note's velocity
    for c in range(3):
        s.route(R("noteEnv", "sdf/glyph/surface/0/emission", 5.0, comp=c, depth="lastVelocity", attackMs=0,
                  decayMs=300),
                R("noteEnv", "sdf/glyph/surface/1/emission", 4.0, comp=c, depth="lastVelocity", attackMs=0,
                  decayMs=260))
    # snare: an RGB split; hat: grain
    s.route(R("snare", "post/split/amount", 10.0, attackMs=0, decayMs=160),
            R("hat", "post/output/grain", 0.05, attackMs=0, decayMs=60),
            R("kick", "post/lens/chromaticAberration", 0.012, attackMs=0, decayMs=120),
            R("noteEnv", "post/bloom/intensity", 0.15, attackMs=0, decayMs=300))

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region("glyph", centre=list(GLYPH), radius=RING_R * 1.1)
    s.region("tunnel", box=[0.35, 0.3, 0.65, 0.7])
    s.region("void", box=[0.0, 0.0, 0.3, 1.0])
    return s

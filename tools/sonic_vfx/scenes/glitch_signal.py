"""8. GLITCH SIGNAL: PIXEL CANYON (06-brief-glitch-signal.md; ABSTRACT-PLAN.md section 8).

A flight down a canyon made of a broken signal. The walls and the floor are PIXELS: a grid of thousands of dark blocks,
some lit cyan and white in a data pattern, ruled with scanlines, streaked with flowing data ribbons, rained on by falling
pixels, with floating fragments of the signal at the camera's sides; far ahead, a white-hot horizon where the signal comes
from. At rest it is almost monochrome (cyan and white on blue-black) and almost stable. The music corrupts it.

Grammar: everything is a BLOCK on one 6 m grid. Walls and floor are grid distributions of boxes; their colours are a
material program reading each block's own centre (a periodic noise, so the pattern rides with the blocks through the
seamless sawtooth); damage is deformers (a row shear for the drifting sections, a block-scale noise for the fragments) and
the post-glitch vocabulary (split, glitch blocks and tears, sort, shock, pixelate, scanlines).

Glitch at three scales: MICRO (always: scanlines, a pixel of RGB split, a flicker of glitch blocks, the hats' sparks),
MESO (snares tear the frame and shear the rows, hats pop pixels, onsets swap channels), MACRO, the signature: a strong
event sets off a staged COLLAPSE (RGB parts, the rows drift, the walls fragment into blocks, the frame is all but
corrupted, a wave of colour runs through the camera) and the world RECONSTRUCTS IN A NEW CONFIGURATION: turned a quarter
turn about the flight (walls become floor and sky), its palette moved round the hue circle, its data pattern and its
row stagger re-drawn. Reactivity continues from the new state.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, mix, scale3, SLOW, MEDIUM, FAST, HIT, SNAP, VERY_SLOW

ID = "glitch-signal"
TITLE = "Glitch Signal"

INK = "#020309"
DIM = "#0b1c5a"
LIT = "#9ef6ff"
WHITE = "#ffffff"
MAGENTA = "#ff2bd6"
ACID = "#b6ff2e"
CORRUPT = "#ff5a1f"

PITCH = 6.0             # the grid
BOX = 5.2
MODULE = 192.0          # the pattern's period along the flight (32 blocks)
SPEED = 30.0
PERIOD = MODULE / SPEED  # 6.4 s
HALF_W = 24.0           # the canyon's half-width (to the walls' inner faces)
ROWS = 9
COLS = 160              # blocks along the flight: 960 m
Z_NEAR = 18.0
Z_MID = Z_NEAR - COLS * PITCH * 0.5

DESIGN = {
    "category": "glitch",
    "thesis": "Pixel Canyon: you fly down a canyon built of a broken signal, walls and floor of blocks lit in a data "
              "pattern, towards a white-hot horizon. A strong event collapses it (RGB parts, rows drift, the walls "
              "shatter into blocks, a wave of colour passes through you) and it rebuilds turned a quarter turn, "
              "recoloured and re-patterned.",
    "composition": {
        "background": "the white-hot horizon where the signal comes from, the canyon dissolving into the dark",
        "midground": "the pixel walls and floor converging, data ribbons streaming along them",
        "foreground": "floating fragments of the signal at the sides, falling pixels",
        "focal": "the horizon",
        "secondary": ["the lit pixels' pattern", "the ribbons", "the fragments"],
        "atmosphere": "blue-black depth; the far canyon fades out",
        "post": "scanlines and a pixel of split at rest; the whole glitch vocabulary on events",
        "camera": "fast and straight down the canyon; after each collapse, a quarter turn about the axis",
    },
    "palette": {"dominant": INK, "secondary": DIM, "accent": MAGENTA, "highlight": LIT,
                "background_value": "black (blue-black)",
                "saturation": "monochrome cyan and white at rest; magenta, acid green and orange in corruption"},
    "motion": {
        "very_slow": ["the palette between collapses"],
        "medium": ["the flight", "the ribbons' data"],
        "fast": ["rows shearing", "pixels popping"],
        "extremely_fast": ["tears", "glitch blocks", "the collapse's wave"],
    },
    "vocabulary": [
        ["bass", "response.bass", "the RGB split widens; the walls push apart; the horizon flares"],
        ["kick", "response.kick", "a wave of light runs down the canyon towards you; the floor's pixels flash"],
        ["snare", "response.snare", "MESO: the frame tears; the rows shear sideways"],
        ["hat", "response.hat", "MICRO: glitch blocks flicker; sparks of pixels"],
        ["onset", "response.onset", "MESO: channels swap in the glitch blocks"],
        ["mids", "audio.mid", "the data ribbons stream faster; the rows' stagger drifts"],
        ["highs", "audio.treble", "the scanlines deepen; the pixel rain thickens"],
        ["brightness", "sonic.brightness", "the lit pixels brighten"],
        ["tempo", "beat.pulse", "the lit pixels pulse on the beat"],
        ["intensity", "response.intensity", "STRUCTURE: more of the pattern lights as the piece builds"],
        ["strong event", "visual.collapse", "MACRO, the signature: a hard kick at a moment of large spectral change "
         "(or a note at full velocity) collapses the world and rebuilds it in a new configuration (turned about the "
         "flight, a new palette, a new pattern, a new stagger)"],
        ["pitch", "notes.lastPitch", "the floating fragments rise with the pitch"],
        ["velocity", "notes.lastVelocity", "how hard a note glitches the frame; a note at full velocity collapses it"],
        ["held", "notes.held", "the fragments hold their light while notes are held"],
        ["mod wheel", "control.modwheel", "corruption by hand: blocks, split and sort"],
        ["silence", "(no input)", "the canyon streams on, monochrome and nearly stable"],
    ],
    "tier": "light: about 4,000 instanced blocks, one wall program, the post-glitch pass, analytic fog",
}

WALLS = ["wallL", "wallR", "floor"]
TRAVELLING = WALLS + ["ribbons", "fragments"]


def unlit(hexc, intensity, program=None):
    m = {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": hexrgb(hexc), "emissiveIntensity": float(intensity),
         "roughness": 1.0, "metallic": 0.0, "unlit": True}
    if program:
        m["program"] = program
    return m


def pixel_program():
    """The blocks: each block's colour from its own centre (world position less local position, less the sawtooth's
    offset, so the pattern rides with the block), a noise sampled on a cylinder round the flight axis (periodic over
    MODULE, so the sawtooth is seamless), quantised into dark, dim and lit; scanlines across every face; the kick's
    band of light; a fade into the dark with distance. Ops (1-based): 5 the sawtooth's offset (a track), 19 the noise
    (its offset: the pattern), 20/22 the dim and lit thresholds, 24 the dim colour, 27 the lit colour, 33 minus the
    band's distance, 37 the band's colour."""
    tw = 2.0 * math.pi / MODULE
    return {
        "name": "gsPixel",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},                                     # 1
            {"kind": "input", "dst": 1, "input": "localPosition"},                                     # 2
            {"kind": "constant", "dst": 2, "constant": [-1.0, -1.0, -1.0, -1.0]},                      # 3
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},                                      # 4
            {"kind": "constant", "dst": 2, "constant": [0.0, 0.0, 0.0, 0.0]},                          # 5 -offset
            {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},                                           # 6
            {"kind": "add", "dst": 1, "srcA": 0, "srcB": 1},                                           # 7 centre
            # the noise's input: (x and y as they are, and z wrapped onto a circle of radius MODULE / 2 pi / 24)
            {"kind": "swizzle", "dst": 3, "srcA": 1, "constant": [2.0, 2.0, 2.0, 2.0]},               # 8 z
            {"kind": "palette", "dst": 4, "srcA": 3, "value": 0.0, "constant": [0.0, 0.0, 0.0, 0.0],
             "constant2": [1.3, 1.3, 1.3, 1.3], "constant3": [1.0 / MODULE] * 4,
             "constant4": [0.0, 0.0, 0.0, 0.0]},                                                        # 9 R cos
            {"kind": "palette", "dst": 5, "srcA": 3, "value": 0.0, "constant": [0.0, 0.0, 0.0, 0.0],
             "constant2": [1.3, 1.3, 1.3, 1.3], "constant3": [1.0 / MODULE] * 4,
             "constant4": [-0.25, -0.25, -0.25, -0.25]},                                                # 10 R sin
            {"kind": "constant", "dst": 6, "constant": [0.0, 0.0, 1.0, 0.0]},                          # 11
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 6},                                      # 12 (0,0,cos)
            {"kind": "constant", "dst": 6, "constant": [1.0, 0.0, 0.0, 0.0]},                          # 13
            {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 6},                                      # 14 (sin,0,0)
            {"kind": "constant", "dst": 6, "constant": [0.21, 0.37, 0.0, 0.0]},                        # 15 x, y scale
            {"kind": "multiply", "dst": 3, "srcA": 1, "srcB": 6},                                      # 16
            {"kind": "add", "dst": 4, "srcA": 4, "srcB": 5},                                           # 17
            {"kind": "add", "dst": 3, "srcA": 3, "srcB": 4},                                           # 18
            {"kind": "noise", "dst": 3, "srcA": 3, "value": 1.0, "constant": [0.0, 0.0, 0.0, 0.0],
             "seed": 11},                                                                                # 19 pattern
            {"kind": "threshold", "dst": 4, "srcA": 3, "value": 0.58},                                 # 20 dim
            {"kind": "constant", "dst": 6, "constant": hexrgb(INK) + [1.0]},                           # 21
            {"kind": "threshold", "dst": 5, "srcA": 3, "value": 0.7},                                 # 22 lit
            {"kind": "swizzle", "dst": 5, "srcA": 5, "constant": [0.0, 0.0, 0.0, 0.0]},               # 23
            {"kind": "constant", "dst": 7, "constant": hexrgb(DIM, 0.9) + [1.0]},                      # 24 dim colour
            {"kind": "swizzle", "dst": 4, "srcA": 4, "constant": [0.0, 0.0, 0.0, 0.0]},               # 25
            {"kind": "mixBy", "dst": 6, "srcA": 6, "srcB": 7, "srcC": 4},                              # 26
            {"kind": "constant", "dst": 7, "constant": hexrgb(LIT, 2.6) + [1.0]},                      # 27 lit colour
            {"kind": "mixBy", "dst": 6, "srcA": 6, "srcB": 7, "srcC": 5},                              # 28 block colour
            # scanlines across every face (by the fragment's height)
            {"kind": "swizzle", "dst": 3, "srcA": 0, "constant": [1.0, 1.0, 1.0, 1.0]},               # 29 y
            {"kind": "palette", "dst": 3, "srcA": 3, "value": 0.0, "constant": [0.78, 0.78, 0.78, 0.78],
             "constant2": [0.22, 0.22, 0.22, 0.22], "constant3": [1.0 / 0.9] * 4,
             "constant4": [0.0, 0.0, 0.0, 0.0]},                                                        # 30 lines
            {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 3},                                      # 31
            # the kick's band of light, travelling down the canyon towards the camera (decametres)
            {"kind": "input", "dst": 2, "input": "cameraDistance"},                                    # 32
            {"kind": "constant", "dst": 3, "constant": [BAND_REST] * 4},                              # 33 -band dist.
            {"kind": "constant", "dst": 4, "constant": [0.1, 0.1, 0.1, 0.1]},                          # 34
            {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 4},                                      # 35 decametres
            {"kind": "add", "dst": 3, "srcA": 2, "srcB": 3},                                           # 36
            {"kind": "constant", "dst": 7, "constant": hexrgb(WHITE, 3.0) + [1.0]},                    # 37 band
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 3},                                      # 38
            {"kind": "gradient", "dst": 3, "srcA": 3, "constant": [1.0, 0.0, 0.0, 1.0],
             "value": -1.0 / (BAND_W * BAND_W)},                                                        # 39
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 7},                                      # 40
            {"kind": "add", "dst": 6, "srcA": 6, "srcB": 3},                                           # 41
            # the material's own emission on top (the fragments' notes; zero on the walls)
            {"kind": "input", "dst": 3, "input": "materialEmission"},                                  # 42
            {"kind": "add", "dst": 6, "srcA": 6, "srcB": 3},                                           # 43
            # the fade into the dark with distance
            {"kind": "smoothstep", "dst": 3, "srcA": 2, "constant": [12.0, 62.0, 0.0, 0.0]},           # 44
            {"kind": "constant", "dst": 7, "constant": hexrgb(INK) + [1.0]},                           # 45 the dark
            {"kind": "mixBy", "dst": 6, "srcA": 6, "srcB": 7, "srcC": 3},                              # 46
        ],
        "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 1.0, "opacity": -1,
    }


BAND_REST = 30.0      # gsPixel op 33 at rest (decametres): the band parked behind the camera
BAND_W = 2.2


def ribbon_program():
    """The data ribbons: dashes of light streaming along the flight towards the camera (time-driven), faded with
    distance. Ops: 4 the stream's speed, 7 the dashes' density."""
    return {
        "name": "gsRibbon",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},                                     # 1
            {"kind": "swizzle", "dst": 0, "srcA": 0, "constant": [2.0, 2.0, 2.0, 2.0]},               # 2 z
            {"kind": "input", "dst": 1, "input": "time"},                                              # 3
            {"kind": "constant", "dst": 2, "constant": [-60.0, -60.0, -60.0, -60.0]},                  # 4 speed
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},                                      # 5
            {"kind": "add", "dst": 0, "srcA": 0, "srcB": 1},                                           # 6
            {"kind": "noise", "dst": 0, "srcA": 0, "value": 0.09, "constant": [0.0, 0.0, 0.0, 0.0],
             "seed": 3},                                                                                 # 7 dashes
            {"kind": "smoothstep", "dst": 0, "srcA": 0, "constant": [0.52, 0.6, 0.0, 0.0]},            # 8
            {"kind": "input", "dst": 1, "input": "materialEmission"},                                  # 9
            {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 1},                                      # 10
            {"kind": "input", "dst": 2, "input": "cameraDistance"},                                    # 11
            {"kind": "constant", "dst": 3, "constant": [0.1, 0.1, 0.1, 0.1]},                          # 12
            {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},                                      # 13
            {"kind": "smoothstep", "dst": 2, "srcA": 2, "constant": [62.0, 12.0, 0.0, 0.0]},           # 14 near = 1
            {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 2},                                      # 15
        ],
        "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 0, "emissionIntensity": 1.0, "opacity": -1,
    }


def wall_deformers():
    """Slot 1: the row shear (horizontal sections drift: a sine of the height, across the canyon). Slot 2: the
    fragmentation (a block-scale noise in every axis, so each block moves nearly whole)."""
    return [{"kind": "sine", "amount": 0.0, "frequency": 0.52, "speed": 0.0, "phase": 0.0, "axis": [0.0, 1.0, 0.0],
             "displacementAxis": [0.0, 0.0, 1.0], "space": "world"},
            {"kind": "noise", "amount": 0.0, "scale": 0.05, "speed": 0.0, "seed": 17, "axisMask": [1.0, 1.0, 1.0],
             "space": "world"}]


def floor_deformers():
    return [{"kind": "sine", "amount": 0.0, "frequency": 0.52, "speed": 0.0, "phase": 0.0, "axis": [1.0, 0.0, 0.0],
             "displacementAxis": [0.0, 0.0, 1.0], "space": "world"},
            {"kind": "noise", "amount": 0.0, "scale": 0.05, "speed": 0.0, "seed": 23, "axisMask": [1.0, 1.0, 1.0],
             "space": "world"}]


STAGES = {
    # the collapse, staged from one trigger (ms after it): (delay, hold, fall per second, spring Hz)
    "split": (0, 1500, 3.0, 2.5),
    "drift": (250, 1150, 3.0, 2.0),
    "fragment": (520, 820, 3.5, 2.6),
    "corrupt": (820, 480, 5.0, 4.0),
    "wave": (1000, 60, 1.6, 0.0),
    "rebuild": (1120, 120, 1000.0, 0.0),
}


def staged(sig, target, amount, stage, comp=None, integrate=False):
    d, h, f, sp = STAGES[stage]
    chain = dict(threshold="binary", thresholdLevel=0.5, delayMs=d, envelope="peakhold", envelopeHoldMs=h,
                 envelopeFallPerSecond=f)
    if sp:
        chain.update(springHz=sp, springDamping=1.0)
    if integrate:
        chain["integrate"] = True
    return R(sig, target, amount, comp=comp, **chain)


def instrument(s):
    """The modulation map (ABSTRACT-PLAN.md section 8)."""
    P = "procedural/%s/"
    # ---- MICRO (always on, in the parameters): scanlines, a pixel of split, a flicker of glitch blocks
    # ---- BASS: the RGB split widens; the walls push apart; the horizon flares
    s.route(R("bass", "post/split/amount", 5.0, attackMs=20, decayMs=300),
            R("bass", P % "horizon" + "material/emissive", 30.0, attackMs=20, decayMs=400))
    for node, sgn in (("wallL", -1.0), ("wallR", 1.0)):
        s.route(R("bass", P % node + "transform/position", 2.5 * sgn, comp=0, attackMs=30, decayMs=350))
    # ---- KICK: a band of light runs down the canyon towards the camera (op 33: minus its distance in decametres; the
    # envelope falls 1 to 0 in 0.45 s, so the band comes from 900 m, passes the camera and parks behind it)
    s.route(R("kick", "material/gsPixel/op/33/constant/constant", -120.0, threshold="binary",
              thresholdLevel=0.3, envelope="linearfall", envelopeHoldMs=0, envelopeFallPerSecond=2.2))
    # ---- SNARE (MESO): the frame tears; the rows shear sideways
    s.route(R("snare", "post/glitch/tear", 0.35, attackMs=0, decayMs=180),
            R("snare", "post/glitch/tearShift", 90.0, attackMs=0, decayMs=180))
    for node in WALLS:
        s.route(R("snare", P % node + "deform/1/amount", 2.4, attackMs=0, decayMs=260))
    # ---- HAT (MICRO): glitch blocks flicker; ONSET (MESO): their channels swap
    s.route(R("hat", "post/glitch/amount", 0.08, attackMs=0, decayMs=90),
            R("onset", "post/glitch/swap", 0.6, attackMs=0, decayMs=200))
    # ---- MIDS: the data ribbons stream faster; the rows' stagger drifts (the shear's phase, integrated)
    s.route(R("audio.mid", "material/gsRibbon/op/4/constant/constant", -160.0, attackMs=60, decayMs=500))
    for node in WALLS:
        s.route(R("audio.mid", P % node + "deform/1/phase", 0.8, integrate=True, attackMs=100, decayMs=600))
    # ---- HIGHS: the scanlines deepen; the pixel rain thickens
    s.route(R("audio.treble", "post/display/scanlines", 0.35, attackMs=20, decayMs=250),
            R("audio.treble", "particles/rain/spawnRate", 1600.0, attackMs=20, decayMs=250))
    # ---- BRIGHTNESS: the lit pixels brighten; TEMPO: they pulse; INTENSITY (STRUCTURE): more of the pattern lights
    s.route(R("brightness", "material/gsPixel/op/27/constant/constant", 0.6, **SLOW),
            R("beat", "material/gsPixel/op/27/constant/constant", 0.5, attackMs=0, decayMs=150),
            R("intensity", "material/gsPixel/op/22/threshold/value", -0.04, **SLOW),
            R("intensity", "material/gsPixel/op/20/threshold/value", -0.03, **SLOW))
    # ---- MIDI: pitch picks the row a note lights (the lit threshold dips for one band of heights... the fragments
    # carry it: a note flares them); velocity is how hard it glitches; held notes hold the fragments lit
    s.route(R("noteEnv", P % "fragments" + "material/emissive", 3.0, depth="lastVelocity", attackMs=0, decayMs=500),
            R("noteEnv", "post/glitch/amount", 0.25, depth="lastVelocity", attackMs=0, decayMs=160),
            R("held", P % "fragments" + "material/emissive", 1.2, **MEDIUM),
            R("lastPitch", P % "fragments" + "transform/position", 30.0, comp=1, **MEDIUM))
    # ---- MOD WHEEL: corruption by hand
    wheel = s.modwheel()
    s.route(R(wheel, "post/glitch/amount", 0.7, attackMs=40, decayMs=40),
            R(wheel, "post/split/amount", 24.0, attackMs=40, decayMs=40),
            R(wheel, "post/sort/amount", 0.6, attackMs=40, decayMs=40))

    # ==== MACRO, THE SIGNATURE: COLLAPSE AND RECONSTRUCTION, staged from visual.collapse ====
    C = "visual.collapse"
    # 1 RGB separation begins (and grows spectral)
    s.route(staged(C, "post/split/amount", 26.0, "split"),
            staged(C, "post/split/spectral", 0.8, "split"))
    # 2 horizontal sections drift (the rows shear hard, the frame tears)
    for node in WALLS:
        s.route(staged(C, P % node + "deform/1/amount", 9.0, "drift"))
    s.route(staged(C, "post/glitch/tear", 0.6, "drift"),
            staged(C, "post/glitch/tearShift", 160.0, "drift"))
    # 3 the surfaces fragment into blocks (each block moves nearly whole)
    for node in WALLS:
        s.route(staged(C, P % node + "deform/2/amount", 16.0, "fragment"))
    s.route(staged(C, "post/glitch/amount", 0.45, "fragment"),
            staged(C, "post/glitch/block", 40.0, "fragment"))
    # 4 all but total corruption: blocks everywhere, channels swapped, the bright pixels sorted, a coarse mosaic
    s.route(staged(C, "post/glitch/amount", 0.5, "corrupt"),
            staged(C, "post/glitch/swap", 0.9, "corrupt"),
            staged(C, "post/sort/amount", 0.85, "corrupt"),
            staged(C, "post/display/pixelate", 14.0, "corrupt"),
            staged(C, "post/grade/saturation", 0.8, "corrupt"))
    # 5 a wave of colour passes through the camera: a shock ring closing on the centre, a hue flash
    s.route(R(C, "post/shock/amount", 120.0, threshold="binary", thresholdLevel=0.5, delayMs=1000,
              envelope="linearfall", envelopeHoldMs=0, envelopeFallPerSecond=2.0),
            R(C, "post/shock/radius", 1.4, threshold="binary", thresholdLevel=0.5, delayMs=1000,
              envelope="linearfall", envelopeHoldMs=0, envelopeFallPerSecond=2.0),
            staged(C, "post/grade/hueShift", 2.6, "wave"))
    # 6 RECONSTRUCTION in a new configuration (integrated, so it stays): a quarter turn about the flight, the palette
    # moved round the hue circle, a new data pattern, a new row stagger (each a 0.2 s pulse, integrated)
    s.route(staged(C, "camera/roll", 420.0, "rebuild", integrate=True),
            staged(C, "post/grade/hueShift", 5.0, "rebuild", integrate=True),
            staged(C, "material/gsPixel/op/19/noise/constant", 35.0, "rebuild", comp=0, integrate=True))
    for node in WALLS:
        s.route(staged(C, P % node + "deform/1/phase", 11.0, "rebuild", integrate=True))


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.55, "sustain": 0.55, "attack": 1.0, "release": 1.1,
                  "floorDb": -44.0, "rangeDb": 42.0}     # mastered music does not saturate the levels
    s.environment = {
        "intensity": 0.0, "background": hexrgb(INK), "fogColor": hexrgb(INK), "volumeDensity": 0.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb(INK), "horizonColor": hexrgb("#06102e"),
                "groundColor": hexrgb(INK), "haze": 0.2, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.program(pixel_program())
    s.program(ribbon_program())

    # ---- the canyon: two walls and a floor of blocks (grid distributions are centred on their transform)
    box = {"kind": "box", "size": [BOX, BOX, BOX], "subdivisions": 1}
    for name, x in (("wallL", -HALF_W - BOX * 0.5), ("wallR", HALF_W + BOX * 0.5)):
        s.proc(name, box, distribution={"kind": "grid", "gridCount": [1, ROWS, COLS],
                                        "gridSpacing": [PITCH, PITCH, PITCH]},
               material=unlit(INK, 0.0, "gsPixel"), deformers=wall_deformers(),
               transform={"position": [x, PITCH * ROWS * 0.5 - BOX * 0.5, Z_MID], "rotation": [0, 0, 0],
                          "scale": [1, 1, 1]})
    across = int(2.0 * HALF_W / PITCH)
    s.proc("floor", {"kind": "box", "size": [BOX, 0.6, BOX], "subdivisions": 1},
           distribution={"kind": "grid", "gridCount": [across, 1, COLS], "gridSpacing": [PITCH, PITCH, PITCH]},
           material=unlit(INK, 0.0, "gsPixel"), deformers=floor_deformers(),
           transform={"position": [0.0, -0.3, Z_MID], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- data ribbons streaming along the walls (thin rods at several heights)
    s.proc("ribbons", {"kind": "box", "size": [0.35, 0.35, COLS * PITCH], "subdivisions": 1},
           distribution={"kind": "grid", "gridCount": [2, 4, 1], "gridSpacing": [2.0 * HALF_W - 3.0, 11.0, 1.0]},
           material=unlit(LIT, 3.0, "gsRibbon"),
           transform={"position": [0.0, 18.0, Z_MID], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- fragments of the signal floating at the sides (one pair per module), the walls' pattern on them
    s.proc("fragments", {"kind": "box", "size": [0.4, 7.0, 12.0], "subdivisions": 1},
           distribution={"kind": "linear", "count": 5, "start": [-14.0, 22.0, Z_NEAR - 4.0 * MODULE - 40.0],
                         "end": [-14.0, 22.0, Z_NEAR - 40.0]},
           material=unlit(MAGENTA, 0.0, "gsPixel"),
           variation={"seed": 7, "randomPosition": [26.0, 12.0, 30.0], "randomRotation": [0.0, 0.5, 0.15]})

    # ---- the horizon where the signal comes from: a white-hot bar and its glow (static, far)
    s.proc("horizon", {"kind": "box", "size": [260.0, 3.0, 1.0], "subdivisions": 1},
           material=unlit(WHITE, 40.0),
           transform={"position": [0.0, 16.0, -1150.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.proc("horizonCore", {"kind": "box", "size": [36.0, 36.0, 1.0], "subdivisions": 1},
           material=unlit("#d8fbff", 22.0),
           transform={"position": [0.0, 16.0, -1160.0], "rotation": [0, 0, 45.0], "scale": [1, 1, 1]})

    # ---- the pixel rain: square sparks falling along the walls, carried towards the camera with the canyon
    s.particles("rain", capacity=12000, seed=29, shape="box", position=[0.0, 52.0, -300.0],
                extent=[HALF_W + 2.0, 1.0, 300.0], direction=[0.0, -0.35, 1.0], spawnRate=220.0, lifetimeMin=1.5,
                lifetimeMax=2.6, spread=0.0, speedMin=SPEED * 1.05, speedMax=SPEED * 1.25, gravity=[0, -9.0, 0],
                drag=0.0, sizeStart=0.45, sizeEnd=0.3, colorStart=hexrgb(LIT) + [1.0],
                colorEnd=hexrgb(MAGENTA) + [0.0], emissive=6.0, blend="additive", turbulence=0.0,
                velocityStretch=0.6, stretchMax=1.2)

    # ---- the flight: the canyon slides one module per PERIOD towards the camera, then back (seamless: the pattern is
    # periodic in MODULE and rides with the blocks through gsPixel's op 5)
    for node in TRAVELLING:
        s.track("procedural/%s/transform/position" % node, [
            {"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "linear"},
            {"time": PERIOD, "value": [0.0, 0.0, MODULE], "interp": "linear"}], loop=PERIOD, mode="add")
    s.track("material/gsPixel/op/5/constant/constant", [
        {"time": 0.0, "value": [0.0, 0.0, 0.0, 0.0], "interp": "linear"},
        {"time": PERIOD, "value": [0.0, 0.0, -MODULE, 0.0], "interp": "linear"}], loop=PERIOD)

    # ---- the collapse's trigger: a STRONG EVENT -- a hard kick at a moment of large spectral change (the kick's envelope
    # times the flux, above 0.62: measured at 30 fps on the two review excerpts, three to five times in thirty seconds,
    # at the hits and the drops, where either signal alone passes on most beats) -- or a note at full velocity. Envelopes, not one-frame
    # events: a delayed route reads its input interpolated between frames, which can halve a one-frame pulse below its
    # threshold.
    s.map(M("collapseAudio", [("kickEnv", 1.0), ("flux", 1.0)], "product", bias=-0.74, gain=2.0),
          M("collapseMidi", [("noteEnv", 1.0), ("lastVelocity", 1.0)], "product", bias=-1.4, gain=2.0))
    s.map2(M("collapse", [("visual.collapseAudio", 1.0), ("visual.collapseMidi", 1.0)], "max"))

    # ---- camera: down the middle of the canyon, a little above the floor, looking at the horizon
    cam = [0.0, 13.0, 0.0]
    tgt = [0.0, 15.0, -400.0]
    s.params_({"camera/lens/focalLength": 20.0, "post/bloom/intensity": 0.5, "post/bloom/threshold": 0.9,
               "post/bloom/emissionWeight": 1.0, "post/output/vignette": 0.4, "post/output/grain": 0.03,
               "post/tonemap/operator": 4,
               # MICRO, always: scanlines, a pixel of split, a flicker of blocks
               "post/display/scanlines": 0.22, "post/display/lines": 360.0, "post/split/amount": 1.4,
               "post/glitch/amount": 0.015, "post/glitch/block": 24.0, "post/glitch/rate": 9.0,
               "post/glitch/drift": 30.0, "post/sort/threshold": 0.9, "post/sort/length": 180.0,
               "post/sort/angle": 0.0, "post/shock/width": 0.12, "post/shock/chroma": 0.9})
    s.camera = {"mode": 1, "position": cam, "target": tgt, "fov": 50.0, "orbitSpeed": 0.0}
    s.drift_camera(cam, tgt, period=30.0, amp=(2.0, 1.5, 0.0), tamp=(10.0, 6.0, 0.0))

    instrument(s)
    s.region("horizon", box=[0.4, 0.35, 0.6, 0.55])
    s.region("canyon", box=[0.0, 0.0, 1.0, 1.0])
    return s

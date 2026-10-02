"""DATASCAPE (digital, data). A landscape of pure measurement, barcode ridges and numerals to the horizon, written and
erased by the music.

Composition (SCENE-CATALOG.md #16): after Ryoji Ikeda's test patterns, laid over the ridges of a pulsar plot. A
black terrain of heaving ridges runs to a vanishing point; its surface is white data -- barcode strips along the
glide, broken into dashes -- and the highest crests are the one red in the frame. Numerals stand on the plain like
monoliths, receding. The camera glides low and forward, its pace the music's level. Strict monochrome; the only
colour is the red of the peaks.

The risk the catalog names is a user interface. The answer is that nothing here is a readout: no axis, no legend, no
bar that equals a band. The music writes into a world (a note is a bright strip at its place across the plain; the
bass heaves the ridges), it never draws a graph.
"""
import math

from .. import kit, signals
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, SNAP, VERY_SLOW

ID = "datascape"
TITLE = "Datascape"

BLACK = "#000000"
WHITE = "#f2f2f2"
RED = "#ff2a1a"

CAM = (0.0, 2.2, 10.0)

DESIGN = {
    "category": "digital",
    "thesis": "A landscape of pure measurement, barcode ridges and numerals to the horizon, written and erased by the "
              "music.",
    "composition": {
        "background": "black sky; the plain's ridges converging to the vanishing point",
        "midground": "numerals standing on the plain like monoliths, receding",
        "foreground": "the ridged plain's white data strips streaming under the camera",
        "focal": "the vanishing point, where the strips converge, and the newest bright strip",
        "secondary": ["the red crests", "the numerals", "the strips a note writes"],
        "atmosphere": "(none: pure black, no haze)",
        "post": "scanlines faint, grain, a hard contrast; a shock on the kick",
        "camera": "a low forward glide whose pace is the music's level",
    },
    "palette": {"dominant": BLACK, "secondary": WHITE, "accent": "#5a5a5a", "highlight": RED,
                "background_value": "black",
                "saturation": "monochrome; red only on the highest crests"},
    "motion": {
        "very_slow": ["the numerals passing"],
        "medium": ["the ridges' heave (bass)", "the glide (level)"],
        "fast": ["strips written per note"],
        "extremely_fast": ["the kick's shock", "digit flicker (hat)"],
    },
    "vocabulary": [
        ["level", "response.level", "the glide's pace (integrated: louder is faster, silence a slow drift)"],
        ["note", "response.note", "a bright strip is written at the pitch's place across the plain"],
        ["velocity", "notes.lastVelocity", "the strip's brightness"],
        ["bass", "response.bass", "the ridges heave"],
        ["kick", "response.kick", "a shock ring across the frame"],
        ["snare", "response.snare", "a tear through the data"],
        ["hat", "response.hat", "the numerals flicker"],
        ["sustained", "response.sustain", "the strips settle into order (the dashes lengthen)"],
        ["silence", "(no input)", "a dark plain drifting forward; the strips dim to a faint grid"],
    ],
    "tier": "light: one subdivided plane with a 30-op program, a few text procedurals",
}

# 1-based op indices of routed constants (checked below)
OP_NOTE_X, OP_NOTE_GAIN, OP_DASH = 0, 0, 0


def data_program():
    """The plain's surface: barcode strips along Z (widths from a thresholded 1-D noise across X), broken into dashes
    along Z, brighter where a note was written (a band round x = OP_NOTE_X's value), and red on the crests."""
    ops = [
        {"kind": "input", "dst": 0, "input": "worldPosition"},
        # barcode across x: noise of (x, 0, 0) thresholded hard
        {"kind": "constant", "dst": 1, "constant": [1.0, 0.0, 0.0, 0.0]},
        {"kind": "multiply", "dst": 1, "srcA": 0, "srcB": 1},
        {"kind": "noise", "dst": 2, "srcA": 1, "value": 2.6, "seed": 3},
        {"kind": "smoothstep", "dst": 2, "srcA": 2, "constant": [0.6, 0.607, 0.0, 0.0]},
        # dashes along z: noise of (0, 0, z)
        {"kind": "constant", "dst": 3, "constant": [0.0, 0.0, 1.0, 0.0]},
        {"kind": "multiply", "dst": 3, "srcA": 0, "srcB": 3},
        {"kind": "add", "dst": 3, "srcA": 3, "srcB": 1},
        {"kind": "noise", "dst": 3, "srcA": 3, "value": 0.9, "seed": 8},
        {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.5, 0.52, 0.0, 0.0]},        # OP_DASH: edge
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},                                   # strip x dash
        # the note band: 1 within 0.35 m of x = note
        {"kind": "swizzle", "dst": 4, "srcA": 0, "constant": [0.0, 0.0, 0.0, 0.0]},             # x
        {"kind": "constant", "dst": 5, "constant": [0.0, 0.0, 0.0, 0.0]},                       # OP_NOTE_X: -x
        {"kind": "add", "dst": 4, "srcA": 4, "srcB": 5},
        {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 4},
        {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.12, 0.0, 0.0, 0.0]},         # 1 at the band
        {"kind": "constant", "dst": 5, "constant": [0.0, 0.0, 0.0, 0.0]},                       # OP_NOTE_GAIN
        {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},
        {"kind": "swizzle", "dst": 4, "srcA": 4, "constant": [0.0, 0.0, 0.0, 0.0]},             # broadcast
        {"kind": "add", "dst": 2, "srcA": 2, "srcB": 4},
        # fade with distance (the vanishing point stays clean) and a faint base grid everywhere
        {"kind": "input", "dst": 6, "input": "cameraDistance"},
        {"kind": "remap", "dst": 6, "srcA": 6, "value": 1, "constant": [40.0, 270.0, 1.0, 0.0]},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 6},
        # the crests are red: height above 2.2 m
        {"kind": "swizzle", "dst": 7, "srcA": 0, "constant": [1.0, 1.0, 1.0, 1.0]},             # y
        {"kind": "smoothstep", "dst": 7, "srcA": 7, "constant": [2.0, 2.8, 0.0, 0.0]},
        {"kind": "constant", "dst": 1, "constant": hexrgb(WHITE, 1.6) + [0.0]},
        {"kind": "constant", "dst": 3, "constant": hexrgb(RED, 3.0) + [0.0]},
        {"kind": "mixBy", "dst": 1, "srcA": 1, "srcB": 3, "srcC": 7},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},                                   # emission
        {"kind": "constant", "dst": 0, "constant": [0.0, 0.0, 0.0, 1.0]},                       # black body
    ]
    global OP_NOTE_X, OP_NOTE_GAIN, OP_DASH
    OP_DASH = 10
    OP_NOTE_X = 13
    OP_NOTE_GAIN = 17
    assert ops[OP_DASH - 1]["kind"] == "smoothstep" and ops[OP_NOTE_X - 1]["kind"] == "constant"
    assert ops[OP_NOTE_GAIN - 1]["kind"] == "constant" and len(ops) <= 48
    return {"name": "dsData", "ops": ops, "baseColor": 0, "metallic": -1, "roughness": -1, "emission": 1,
            "emissionIntensity": 1.0, "opacity": -1}


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.6, "sustain": 0.5, "attack": 0.8, "release": 0.9}
    s.environment = {
        "intensity": 0.0, "background": [0.0, 0.0, 0.0], "fogColor": [0, 0, 0], "volumeDensity": 0.0,
        "skyIntensity": 0.0, "sky": {"enabled": False, "background": False},
    }
    s.composition = {"focalPoints": [{"name": "vanishing", "position": [0.0, 0.0, -600.0], "radius": 40.0,
                                      "weight": 1.0}]}
    s.program(data_program())

    # ---- the plain: a long subdivided strip of ridges (scaled along z; the ridges are a world-space sine set)
    s.proc("plain", {"kind": "box", "size": [160.0, 0.1, 300.0], "subdivisions": 64},   # rides the glide
           material={"baseColor": [0, 0, 0], "emissiveColor": [0, 0, 0], "emissiveIntensity": 1.0, "roughness": 1.0,
                     "metallic": 0.0, "program": "dsData"},
           deformers=[{"kind": "sine", "amount": 1.1, "axis": [0, 0, 1], "displacementAxis": [0, 1, 0],
                       "frequency": 0.21, "phase": 0.0, "speed": 0.0, "space": "world"},
                      {"kind": "noise", "amount": 1.6, "scale": 0.05, "speed": 0.0, "seed": 4,
                       "axisMask": [0, 1, 0], "space": "world"}],
           transform={"position": [0.0, 0.0, -138.0], "rotation": [0, 0, 0], "scale": [1.0, 1.0, 1.0]})

    # ---- numerals standing on the plain, receding (white, emissive): four digits, each a long receding row
    num = {"baseColor": [0, 0, 0], "emissiveColor": hexrgb(WHITE), "emissiveIntensity": 1.4, "roughness": 1.0,
           "metallic": 0.0}
    rows = [("0", -15.0, -40.0, 9.0), ("1", 13.0, -75.0, 7.0), ("7", -21.0, -110.0, 11.0), ("9", 19.0, -145.0, 8.0)]
    for k, (d, x, z0, size) in enumerate(rows):
        src = {"kind": "text", "text": d, "textSize": size, "textDepth": 0.6,
               "font": {"family": "Helvetica Neue", "weight": 0.25, "italic": False}}
        s.proc("num%d" % k, src, distribution={"kind": "linear", "count": 60, "start": [x, 0.0, z0],
                                                "end": [x, 0.0, z0 - 140.0 * 59]},
               variation={"seed": 40 + k, "position": [3.0, 0.0, 20.0], "rotation": [0.0, 0.15, 0.0]},
               material=num)

    # ---- camera: low, looking down the plain; the glide is routed (integrated level), the drift keyed
    s.camera = {"mode": 1, "position": list(CAM), "target": [0.0, 1.2, -200.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.track("camera/position", [{"time": 0.0, "value": list(CAM), "interp": "smooth"},
                                {"time": 40.0, "value": [1.2, CAM[1] + 0.3, CAM[2]], "interp": "smooth"},
                                {"time": 80.0, "value": list(CAM), "interp": "smooth"}], loop=80.0)
    s.track("camera/target", [{"time": 0.0, "value": [0.0, 1.2, -200.0], "interp": "smooth"},
                              {"time": 40.0, "value": [3.0, 1.0, -200.0], "interp": "smooth"},
                              {"time": 80.0, "value": [0.0, 1.2, -200.0], "interp": "smooth"}], loop=80.0)

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # level: the glide (a pace integrated into a distance; silence still drifts at 2 m/s)
    for target in ("camera/position", "camera/target", "procedural/plain/transform/position"):
        s.route(R("level", target, -1.0, comp=2, attackMs=300, decayMs=900, remapEnabled=True, remapInMin=0.0,
                  remapInMax=1.0, remapOutMin=2.0, remapOutMax=16.0, integrate=True))
    # notes: a bright strip written at the pitch's place across the plain (x = -12..12 m), its brightness the velocity
    s.map(M("noteX", [("lastPitch", 1.0)], "mean", -0.3 / 0.4, 1.0 / 0.4))
    s.route(R("visual.noteX", "material/dsData/op/%d/constant/constant" % OP_NOTE_X, -24.0, comp=0, offset=-0.5,
              attackMs=0, decayMs=0),
            R("noteEnv", "material/dsData/op/%d/constant/constant" % OP_NOTE_GAIN, 2.4, comp=0,
              depth="lastVelocity", attackMs=0, decayMs=700))
    # bass: the ridges heave
    s.route(R("bass", "procedural/plain/deform/1/amount", 1.8, attackMs=150, decayMs=900),
            R("bass", "procedural/plain/deform/2/amount", 1.2, attackMs=200, decayMs=1200))
    # sustain: order -- the dashes lengthen (the dash threshold falls)
    s.route(R("sustain", "material/dsData/op/%d/smoothstep/constant" % OP_DASH, -0.18, comp=0, **SLOW),
            R("sustain", "material/dsData/op/%d/smoothstep/constant" % OP_DASH, -0.18, comp=1, **SLOW))
    # kick: a shock ring; snare: a tear through the data; hat: the numerals flicker
    s.route(R("kickEnv", "post/shock/amount", 70.0, attackMs=0, decayMs=0),
            R("kickEnv", "post/shock/radius", -1.2, op="replace", offset=-1.0, attackMs=0, decayMs=0),
            R("snareEnv", "post/glitch/tear", 0.7, attackMs=0, decayMs=0),
            R("snareEnv", "post/glitch/amount", 0.25, attackMs=0, decayMs=0))
    for k in range(4):
        s.route(R("hat", "procedural/num%d/material/emissive" % k, -1.2 if k % 2 else 2.5, attackMs=0,
                  decayMs=90))

    s.params_({
        "post/bloom/intensity": 0.25, "post/bloom/threshold": 1.0, "post/output/vignette": 0.25,
        "post/output/grain": 0.02, "post/grade/contrast": 1.15, "post/grade/saturation": 1.0,
        "post/display/scanlines": 0.08, "post/display/lines": 540.0,
        "post/shock/amount": 0.0, "post/shock/radius": 1.2, "post/shock/width": 0.08, "post/shock/chroma": 0.4,
        "post/shock/centerX": 0.5, "post/shock/centerY": 0.48,
        "post/glitch/amount": 0.0, "post/glitch/tear": 0.0, "post/glitch/block": 24.0, "post/glitch/rate": 12.0,
        "post/tonemap/operator": 3, "camera/lens/focalLength": 32.0,
    })

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region("vanishing", box=[0.4, 0.38, 0.6, 0.56])
    s.region("plain", box=[0.0, 0.56, 1.0, 1.0])
    s.region("numerals", box=[0.0, 0.2, 1.0, 0.56])
    return s

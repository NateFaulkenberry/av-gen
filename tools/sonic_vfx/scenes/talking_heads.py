"""10. TALKING HEADS (docs/prototypes/talking-heads/00-brief.md; plan 03; the owner's stop-1 notes 05).

A pale porcelain mannequin bust, suspended in a dark indigo ether, that SPEAKS ITS SOURCE like a noise gate (the owner,
stop 1): a head listens to one source -- a stem or a role, e.g. the bass -- and when a note sounds its neck SNAPS back,
its jaw drops open and beams of light SHOOT from its mouth; when the note stops, the neck snaps forward, the jaw shuts
and the beams cut. Distinct on and off, mechanical, fast. Between notes it scans the room, panning its head round in
steady sweeps, pauses and snaps. Spectral copies, colour and signal tears flash with each opening.

The articulated bust (tools/talking_heads/make_bust.py; anchors in meshes/th-bust.anchors.json):
  th-bust (body, rigid)  > th-neck (bent by a Bend deformer: the neck curves back, joined at both ends: no seam opens)
                         > th-head (turned and moved onto the bent neck's top) > th-jaw (hinged) > the mouth beams
Each head is one HEAD spec (`HEADS`), so a later scene can put one head on each channel: its gate, its routes and its
nodes are all named by its prefix.

Systems: Light Beam riding the jaw (ADR-1670); the gate is a `signal` source (ADR-1602) with a Schmitt trigger built
from its own previous value (an input gated on itself) and a short hold; every route is same-frame (no macro.*).
"""
import json
import math
import os

from .. import kit
from ..kit import R, M, hexrgb

ID = "talking-heads"
TITLE = "Talking Heads"
HERO_AT = 20.0

_A = json.load(open(os.path.join(kit.ABSTRACT_DIR, "meshes", "th-bust.anchors.json")))
NECK_FOOT = _A["body"]["neckFoot"]
HEAD_REST = _A["body"]["headRest"]
BEND_LEN = _A["neck"]["bendLength"]
JAW_HINGE = _A["head"]["jawHinge"]
EYE_L = _A["head"]["eyeLeft"]
EYE_R = _A["head"]["eyeRight"]
MOUTH = _A["jaw"]["mouth"]                       # in the JAW's frame: inside the opening
MOUTH_IN_HEAD = [JAW_HINGE[i] + MOUTH[i] for i in range(3)]

# The palette (01-research-assets.md section 6).
INK = "#07051a"
VIOLET_DEEP = "#160a2e"
HAZE = "#2a2458"
MAGENTA = "#ff2bd6"
VIOLET = "#8a3bff"
CORE = "#ffe9f7"
BLUE = "#2b7bff"
PINK = "#ff6fb5"
PALE = "#f4f0ff"
IVORY = (0.86, 0.82, 0.74)

# The gate's pose when open (degrees): the neck's tilt back and the jaw's drop. The head is placed on the bent neck's
# top exactly at this angle (neck_offset), and within a millimetre on the way there.
TILT_OPEN = 24.0
JAW_OPEN = 14.0
SNAP = {"attackMs": 0.0, "decayMs": 0.0, "springHz": 11.0, "springDamping": 0.62}   # a step, then a short mechanical settle

# One head: which source it speaks (the first ACTIVE input wins: a stem's role, else the mix's own band), and its gate's
# open and close levels (a Schmitt trigger) and hold. Tuned on Feline Footwear's Bass stem (05, section 3).
HEADS = [{"prefix": "th", "source": [("role.bass.bass", None), ("audio.bass", None)],
          "open": 0.55, "close": 0.42, "holdMs": 33.0, "rise": 0.20}]

DESIGN = {
    "category": "surreal entity",
    "thesis": "Talking Heads: a pale porcelain mannequin bust that speaks its source like a noise gate -- on a note its "
              "neck snaps back, its jaw drops and beams of light shoot from its mouth; between notes it shuts and scans "
              "the room.",
    "composition": {
        "background": "a near-black indigo to deep violet gradient with a faint blue-purple haze",
        "midground": "nothing: the ether",
        "foreground": "sparse slow motes",
        "focal": "the bust, three-quarter, 35-45% of the frame's height",
        "secondary": ["the mouth beams", "the spectral copies"],
        "atmosphere": "dark, sparse, suspended; no floor, horizon or set",
        "post": "a restrained bloom; glitch only in bursts on the gate",
        "camera": "a slow, restrained drift; never on the music",
    },
    "palette": {"dominant": INK, "secondary": HAZE, "accent": MAGENTA, "highlight": CORE,
                "background_value": "near black", "saturation": "pale porcelain against saturated emitted light"},
    "motion": {
        "very_slow": ["the camera's drift"],
        "medium": ["the scan: steady pans, pauses, snaps"],
        "fast": ["the gate: the neck and jaw snap, the beams cut"],
        "extremely_fast": ["signal tears on the opening"],
    },
    "vocabulary": [
        ["source", "signal.thGate", "the gate: open on a note (the neck snaps back, the jaw drops, the beams shoot), "
                                    "shut between notes"],
        ["source level", "signal.thLevel", "how far the beams reach while the gate is open"],
        ["opening", "signal.thOpen", "a flash on each opening: colour, spectral copies, a tear"],
        ["intensity", "response.intensity", "STRUCTURE: the wash's palette drifts as the piece builds"],
        ["silence", "(no input)", "the head is shut and scans the room"],
    ],
    "tier": "light: one articulated bust (about 45k triangles in six pieces), two ghost copies, seven shell beams",
}

INDEX_ENTRY = {
    "category": kit.ABSTRACT_CATEGORY,
    "description": "Play live audio with a bass source (a stem or input with the bass role; a mix works too, on its "
                   "own bass). A porcelain mannequin bust speaks its source like a noise gate: on each note its neck "
                   "snaps back, its jaw drops and beams of light shoot from its mouth; between notes it shuts and scans "
                   "the room. MIDI: CC 1 master, CC 2 beams, CC 3 gate sensitivity, CC 4 copies, CC 5 tearing, CC 6 "
                   "colour, CC 7 gain, CC 8 palette, CC 9 head tilt (each rests at its middle and picks up); pad 36 "
                   "holds the gate shut, 37 holds it open, 39 bursts. docs/prototypes/talking-heads/.",
    "live": True,
    "name": kit.ABSTRACT_CATEGORY + " - " + TITLE,
    "project": "sonic-abstract/" + ID + ".json",
}


# ================================================================================================ material programs
class Ops:
    """A program's op list with names, so routes name an op rather than a hand-counted index (1-based paths)."""

    def __init__(self):
        self.ops, self.at = [], {}

    def add(self, label, kind, dst, **f):
        op = {"kind": kind, "dst": dst}
        op.update(f)
        self.ops.append(op)
        self.at[label] = len(self.ops)
        return op


def ellipsoid(o, label, dst, centre, radii, src=0):
    """dst = saturate(1 - sum(((p - c) / r)^2)): 1 at the centre, 0 on the ellipsoid. Four ops."""
    o.add(label + "C", "constant", dst, constant=[-centre[0], -centre[1], -centre[2], 0.0])
    o.add(label + "D", "add", dst, srcA=src, srcB=dst)
    o.add(label + "Q", "multiply", dst, srcA=dst, srcB=dst)
    o.add(label, "gradient", dst, srcA=dst, value=-1.0,
          constant=[1.0 / radii[0] ** 2, 1.0 / radii[1] ** 2, 1.0 / radii[2] ** 2, 1.0])


FAR = [9.0, 9.0, 9.0]
PROGRAM_OPS = {}   # program name -> its Ops (every piece's program has the same op list, so one index names an op)


def bust_program(name, mouth=FAR, eye_l=FAR, eye_r=FAR):
    """Porcelain, darkened sockets, the possession wash (concentrated at the mouth and eyes) and the signal bands. The
    masks are points in THIS piece's frame (FAR = the piece has none). Registers: r0 local position, r1 mouth mask, r2
    eye mask, r3 porcelain, r5 wash colour, r6 wash amount, r7 base colour out, r4 emission out."""
    o = Ops()
    o.add("local", "input", 0, input="localPosition")
    ellipsoid(o, "mouth", 1, mouth, [0.030, 0.016, 0.030])
    ellipsoid(o, "eyeL", 2, [eye_l[0], eye_l[1], eye_l[2] - 0.004], [0.021, 0.012, 0.030])
    ellipsoid(o, "eyeR", 3, [eye_r[0], eye_r[1], eye_r[2] - 0.004], [0.021, 0.012, 0.030])
    o.add("eyes", "add", 2, srcA=2, srcB=3)
    o.add("ivory", "constant", 3, constant=list(IVORY) + [1.0])
    o.add("socketDark", "remap", 5, srcA=2, value=1.0, constant=[0.0, 1.0, 1.0, 0.6])
    o.add("socketMul", "multiply", 3, srcA=3, srcB=5)
    o.add("washNoise", "noise", 4, srcA=0, value=7.0, constant=[0.0, 0.0, 0.0, 0.0], seed=5)
    o.add("washSpread", "remap", 4, srcA=4, value=1.0, constant=[0.32, 0.68, 0.0, 1.0])
    o.add("washRamp", "oklabRamp", 5, srcA=4, value=1.0, constant=hexrgb(VIOLET) + [1.0],
          constant2=hexrgb(MAGENTA) + [1.0], constant3=hexrgb(PINK) + [1.0], constant4=hexrgb(BLUE) + [1.0])
    o.add("masks", "add", 6, srcA=1, srcB=2)
    o.add("washAmount", "remap", 6, srcA=6, value=1.0, constant=[0.0, 1.0, 0.0, 0.0])
    o.add("washBase", "mixBy", 7, srcA=3, srcB=5, srcC=6)
    o.add("washGlowAmt", "multiply", 4, srcA=5, srcB=6)
    o.add("washGlow", "constant", 6, constant=[0.35, 0.35, 0.35, 0.0])
    o.add("washGlowMul", "multiply", 4, srcA=4, srcB=6)
    o.add("bandTime", "input", 1, input="time")
    o.add("bandRate", "constant", 2, constant=[0.0, 0.0, 14.0, 0.0])
    o.add("bandEpoch", "multiply", 1, srcA=1, srcB=2)
    o.add("bandY", "constant", 2, constant=[0.0, 1.0, 0.0, 0.0])
    o.add("bandYMul", "multiply", 2, srcA=0, srcB=2)
    o.add("bandCoord", "add", 1, srcA=1, srcB=2)
    o.add("bandCell", "quantize", 1, srcA=1, value=70.0, constant=[0.0, 0.0, 0.0, 0.0], seed=9)
    o.add("bandHash", "swizzle", 1, srcA=1, constant=[3.0, 3.0, 3.0, 3.0])
    o.add("bands", "threshold", 1, srcA=1, value=1.01)  # 1.01 = no band; routes lower it
    o.add("bandGain", "constant", 2, constant=[2.2, 2.2, 2.2, 0.0])
    o.add("bandGainMul", "multiply", 1, srcA=1, srcB=2)
    o.add("bandColour", "multiply", 1, srcA=1, srcB=5)
    o.add("bandAdd", "add", 4, srcA=4, srcB=1)
    assert len(o.ops) <= 48, len(o.ops)
    PROGRAM_OPS[name] = o
    return {"name": name, "ops": o.ops, "baseColor": 7, "metallic": -1, "roughness": -1, "emission": 4,
            "emissionIntensity": 1.0, "opacity": -1}


def ghost_program(name, colour):
    """A spectral copy: black, a fresnel edge in its colour, opacity (constant op 4) routed from zero."""
    return {"name": name, "ops": [
        {"kind": "fresnel", "dst": 0, "value": 1.6},
        {"kind": "constant", "dst": 1, "constant": hexrgb(colour, 1.6) + [1.0]},
        {"kind": "multiply", "dst": 2, "srcA": 0, "srcB": 1},
        {"kind": "constant", "dst": 3, "constant": [0.0, 0.0, 0.0, 0.0]},
        {"kind": "remap", "dst": 4, "srcA": 0, "value": 1.0, "constant": [0.0, 1.0, 0.35, 1.0]},
        {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 4},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 4},
        {"kind": "constant", "dst": 5, "constant": [0.0, 0.0, 0.0, 1.0]},
    ], "baseColor": 5, "metallic": -1, "roughness": -1, "emission": 2, "emissionIntensity": 1.0, "opacity": 3}


# ================================================================================================ the mouth beams
# Jaw frame: the face looks down +Z. The camera sits on the bust's right (-X), three-quarter, so +X and +Z lean toward
# the frame's right: the fan pours right and up (the head's tilt adds to its rise).
MOUTH_RAYS = [
    {"yaw": 14.0, "pitch": 4.0, "angle": 9.0, "length": 2.2, "aperture": 0.012, "coreSharpness": 3.2,
     "edgeSoftness": 0.75, "falloff": 1.1, "dust": 0.12, "color": hexrgb(CORE)},
    {"yaw": -14.0, "pitch": 16.0, "angle": 1.6, "length": 2.4, "aperture": 0.003, "color": hexrgb(MAGENTA)},
    {"yaw": 4.0, "pitch": -10.0, "angle": 0.9, "length": 2.6, "aperture": 0.003, "color": hexrgb(VIOLET)},
    {"yaw": 34.0, "pitch": 20.0, "angle": 2.0, "length": 2.1, "aperture": 0.004, "color": hexrgb(PINK)},
    {"yaw": 40.0, "pitch": -6.0, "angle": 1.1, "length": 2.7, "aperture": 0.003, "color": hexrgb(MAGENTA)},
    {"yaw": 22.0, "pitch": 28.0, "angle": 0.8, "length": 2.2, "aperture": 0.003, "color": hexrgb(BLUE)},
    {"yaw": -2.0, "pitch": 8.0, "angle": 1.3, "length": 2.5, "aperture": 0.003, "color": hexrgb(VIOLET)},
]
RAY_WEIGHT = [0.7, 0.55, 0.35, 0.8, 0.45, 0.3, 0.65]


def neck_offset(deg):
    """Where the bent neck carries the head's origin at a tilt back of `deg` (applyBend: R = F / theta), in the
    neck's frame: (dy, dz) with +Z the face's way. Exact at the open pose; the routes scale it linearly."""
    th = math.radians(deg)
    if th < 1e-9:
        return 0.0, 0.0
    R_ = BEND_LEN / th
    return R_ * math.sin(th) - BEND_LEN, -R_ * (1.0 - math.cos(th))


# ================================================================================================ the scan
def scan_track(s, node, seconds=96.0, seed=11):
    """The head scanning the room: a mechanical yaw about the vertical. Steady pans (linear, 1.2-2.6 s) between
    headings within +-35 degrees, pauses (1.5-4 s) and now and then a SNAP (a step) to a new heading. Deterministic."""
    import random
    rnd = random.Random(seed)
    t, h = 0.0, 0.0
    keys = [{"time": 0.0, "value": 0.0, "interp": "linear"}]
    while t < seconds:
        hold = rnd.uniform(1.5, 4.0)
        t += hold
        keys.append({"time": round(t, 3), "value": round(h, 2), "interp": "linear"})
        nxt = rnd.uniform(-35.0, 35.0)
        if abs(nxt - h) < 14.0:
            nxt = max(-35.0, min(35.0, h + math.copysign(18.0, nxt - h if nxt != h else 1.0)))
        if rnd.random() < 0.25:            # a snap: two keys a frame apart
            t += 1.0 / 30.0
        else:
            t += rnd.uniform(1.2, 2.6)
        h = nxt
        keys.append({"time": round(t, 3), "value": round(h, 2), "interp": "linear"})
    keys.append({"time": round(seconds, 3), "value": 0.0, "interp": "linear"})
    s.tracks.append({"target": "nodes/%s/rotation" % node, "component": 1, "enabled": True, "timeBase": "seconds",
                     "mode": "replace", "loopLength": float(seconds), "keys": [
                         {"time": k["time"], "value": [0.0, k["value"], 0.0], "interp": k["interp"]} for k in keys]})


# ================================================================================================ the scene
def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    s.environment = {
        "intensity": 0.04, "background": hexrgb(INK), "fogColor": hexrgb(HAZE, 0.25),
        "volumeDensity": 0.0, "volumeMaxDistance": 0.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb(VIOLET_DEEP, 0.5), "horizonColor": hexrgb(VIOLET_DEEP, 0.32),
                "groundColor": hexrgb(VIOLET_DEEP, 0.3), "haze": 0.25, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.light("key", "directional", direction=[0.55, -0.55, -0.62], color=hexrgb("#ffe2c0"), intensity=3.4,
            castsShadow=False, contactShadow=False)
    s.light("rim", "directional", direction=[-0.45, -0.25, 0.85], color=hexrgb("#8a6cff"), intensity=2.2,
            castsShadow=False, contactShadow=False)
    s.light("fill", "directional", direction=[-0.6, 0.2, -0.75], color=hexrgb("#5a50c8"), intensity=0.25,
            castsShadow=False, contactShadow=False)
    s.program(ghost_program("th-ghostA", MAGENTA))
    s.program(ghost_program("th-ghostB", BLUE))
    s.particles("motes", capacity=400, seed=17, shape="box", position=[0.0, 0.42, 0.0], extent=[2.4, 1.4, 2.0],
                direction=[0.0, 1.0, 0.0], spawnRate=6.0, lifetimeMin=12.0, lifetimeMax=20.0, spread=1.0,
                speedMin=0.005, speedMax=0.02, gravity=[0, 0.002, 0], drag=0.3, turbulence=0.02,
                turbulenceScale=0.5, sizeStart=0.0035, sizeEnd=0.002, colorStart=hexrgb(PALE) + [0.6],
                colorEnd=hexrgb(VIOLET) + [0.0], emissive=1.4, blend="additive")
    s.params_({"camera/lens/focalLength": 50.0, "post/bloom/intensity": 0.42, "post/bloom/threshold": 0.9,
               "post/output/vignette": 0.45, "post/output/grain": 0.012, "post/glitch/tearShift": 0.0,
               "post/glitch/rate": 18.0, "post/glitch/block": 40.0, "temporal/echo/enabled": True,
               "temporal/echo/frames": 3.0, "temporal/echo/strength": 0.12, "temporal/echo/decay": 0.6})
    s.camera["fov"] = 40.0
    s.drift_camera(centre=[-0.98, 0.50, 2.05], target=[0.0, 0.36, 0.0], period=78.0, amp=(0.06, 0.02, 0.04),
                   tamp=(0.01, 0.006, 0.0), steps=8)
    control(s)
    for spec in HEADS:
        head(s, spec, at=[0.0, 0.0, 0.0])
    return s


def head(s, spec, at):
    P = spec["prefix"]
    shear = {"kind": "sine", "amount": 0.0, "frequency": 240.0, "speed": 0.0, "phase": 0.0,
             "axis": [0.0, 1.0, 0.0], "displacementAxis": [1.0, 0.0, 0.0], "space": "world"}
    # (world space: the pieces have different local origins, and a shear in each one's own frame split the seams)
    bend = {"kind": "bend", "amount": 0.0, "axis": [0.0, 1.0, 0.0], "displacementAxis": [0.0, 0.0, -1.0],
            "center": [0.0, 0.0, 0.0], "falloff": BEND_LEN, "space": "local"}
    pieces = [  # node, mesh, program (its masks in its own frame), parent, position, deformers
        (P + "-bust", "th-body", P + "-body", None, at, [shear]),
        (P + "-neck", "th-neck", P + "-body", P + "-bust", NECK_FOOT, [bend, shear]),
        (P + "-head", "th-head", P + "-head", P + "-bust", HEAD_REST, [shear]),
        (P + "-jaw", "th-jaw", P + "-jaw", P + "-head", JAW_HINGE, [shear]),
    ]
    s.program(bust_program(P + "-body"))
    s.program(bust_program(P + "-head", mouth=MOUTH_IN_HEAD, eye_l=EYE_L, eye_r=EYE_R))
    s.program(bust_program(P + "-jaw", mouth=MOUTH))
    for node, mesh, prog, parent, pos, deformers in pieces:
        s.proc(node, {"kind": "mesh", "asset": "meshes/%s.glb" % mesh},
               material={"baseColor": list(IVORY), "roughness": 0.36, "metallic": 0.0, "program": prog},
               deformers=[dict(d) for d in deformers], position=pos, rotation=[0.0, 0.0, 0.0], scale=[1.0, 1.0, 1.0],
               parent=parent, extra={"castsShadow": False})
        s.entity(node)
    # the mouth's dark interior (own material, no program), and the head and jaw's spectral copies
    for name, mesh, parent, prog in ((P + "-cavity", "th-mouth-cavity", P + "-head", None),
                                     (P + "-floor", "th-jaw-floor", P + "-jaw", None),
                                     (P + "-ghostAh", "th-head-ghost", P + "-head", "th-ghostA"),
                                     (P + "-ghostAj", "th-jaw-ghost", P + "-jaw", "th-ghostA"),
                                     (P + "-ghostBh", "th-head-ghost", P + "-head", "th-ghostB"),
                                     (P + "-ghostBj", "th-jaw-ghost", P + "-jaw", "th-ghostB")):
        n = {"name": name, "kind": "gltf", "asset": "meshes/%s.glb" % mesh, "position": [0.0, 0.0, 0.0],
             "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0], "parent": parent, "castsShadow": False}
        if prog:
            n["program"] = prog
        s.nodes.append(n)
    rays = s.emitter_fan(P + "-mouth", P + "-jaw", MOUTH, MOUTH_RAYS)
    s.effect(P + "-shake", "shake", ("entity", P + "-bust"),
             parameters={"trauma": 0.0, "amplitude": 0.004, "rotation": 1.0, "frequency": 17.0, "decay": 0.0,
                         "seed": 7.0})
    for gid, ent in ((P + "-glowH", P + "-head"), (P + "-glowJ", P + "-jaw")):
        s.effect(gid, "glow", ("entity", ent),
                 parameters={"gain": 1.0, "tint": hexrgb(MAGENTA), "glow": 0.0, "rim": 0.05,
                             "rimColor": hexrgb("#7a6cff"), "rimPower": 3.5, "spill": False, "recolour": 0.0})
    scan_track(s, P + "-bust")
    gate(s, spec)
    routes(s, spec, rays)


# ================================================================================================ the instrument
def sig(name, inputs, combine="firstActive", chain=None, event=False):
    st = {"combine": combine, "inputs": inputs}
    if chain:
        st["chain"] = chain
    if event:
        st["event"] = True
    return {"kind": "signal", "name": name, "settings": st}


CHANNELS = [("master", 1), ("beams", 2), ("sensitivity", 3), ("copies", 4), ("tear", 5), ("chroma", 6), ("gain", 7),
            ("palette", 8), ("tilt", 9)]
PADS = [("padShut", 36), ("padOpen", 37), ("padBurst", 39)]


def control(s):
    s.control = {"midi": {"enabled": True, "filter": "*", "pickup": True,
                          "bindings": [{"source": "*", "channel": -1, "kind": "cc", "number": n, "signal": name}
                                       for name, n in CHANNELS]
                          + [{"source": "*", "channel": -1, "kind": "noteEvent", "number": n, "signal": name}
                             for name, n in PADS]},
                 "channels": [{"name": name, "rest": 0.0 if name == "palette" else 0.5, "pickup": True}
                              for name, _ in CHANNELS]}
    # the knobs as gates: master x the feature's own knob, gain 4 so the rests (0.5 x 0.5) read 1; each gated route
    # maps 0..1 onto depth 0..2 (below), so a knob turned fully up doubles its feature
    s.map(*[M(name + "Gate", [("control.master", 1.0), ("control." + name, 1.0)], "product", gain=2.0)
            for name in ("beams", "copies", "tear", "chroma")])
    s.map(M("tiltGate", [("control.tilt", 1.0)], "mean"))


def gate(s, spec):
    """The head's noise gate on its source (05, section 3). All same-frame except the one-frame memories marked.

      signal.<p>Level   the first ACTIVE input of spec["source"] (a stem's role band, else the mix's band)
      signal.<p>LevelS  that, shifted by CC 3 (sensitivity, bipolar about its rest), so both thresholds move
      signal.<p>Rise    LevelS less its own 150 ms follower: how sharply it is rising (a re-attack)
      signal.<p>Trig    a one-frame latch on Rise > spec["rise"], re-armed when it falls back (a new note's attack)
      signal.<p>Gate    a Schmitt trigger: opens at `open`, stays open while above `close` -- through the previous
                        frame's visual.<p>GateKeep -- and is held holdMs after it falls (an exponential fall cut at 0.4)
      signal.<p>Prev    the gate as it stood LAST frame (visual.<p>GateKeep read before the interprets run)
      signal.<p>Chomp   Trig while it was already open: a new note on a held gate -- one frame shut, so every note is
                        its own snap even when the notes run together
      signal.<p>Open    a one-shot on each opening and each chomp (a latching envelope, re-armed on close)
      visual.<p>GateLive  Gate x (1 - Chomp) x (1 - the shut pad): what the head does

    Pad 37 forces it open, pad 36 shut. Tuned on Feline Footwear's Bass stem (role.bass.bass)."""
    P = spec["prefix"]
    L, LS, RS, TR, GR, GT, PV, CH, OP = ("%s%s" % (P, n) for n in
                                         ("Level", "LevelS", "Rise", "Trig", "GateRaw", "Gate", "Prev", "Chomp", "Open"))
    sens = {"source": "control.sensitivity", "polarity": "bipolar", "amount": 0.16}   # up = more sensitive
    binary = lambda lv: {"threshold": "binary", "thresholdLevel": lv}
    s.sources += [
        sig(L, [{"source": src} for src, _ in spec["source"]]),
        sig(LS, [{"source": "signal." + L}, sens], combine="sum"),
        sig(RS, [{"source": "signal." + LS}, {"source": "signal." + LS, "amount": -1.0,
                                              "chain": {"attackMs": 150.0, "decayMs": 150.0}}], combine="sum"),
        sig(TR, [{"source": "signal." + RS, "chain": dict(binary(spec["rise"]), envelope="peakhold",
                                                          envelopeHoldMs=0.0, envelopeFallPerSecond=30.0,
                                                          envelopeRearm=0.5, envelopeRefractoryMs=33.0)}]),
        sig(PV, [{"source": "visual.%sKeep" % GT}]),                      # one-frame memory
        sig(GR, [{"source": "signal." + LS, "chain": binary(spec["open"])},
                 {"source": "signal." + PV},
                 {"source": "control.padOpen", "chain": {"envelope": "peakhold", "envelopeHoldMs": 600.0,
                                                         "envelopeFallPerSecond": 1000.0}}],
            combine="max", chain={"attackMs": 0.0, "decayMs": spec["holdMs"] / 0.92}),
        sig(GT, [{"source": "signal." + GR, "chain": binary(0.4)}]),
        sig(GT + "Keep", [{"source": "signal." + LS, "chain": binary(spec["close"])}]),
        sig(CH, [{"source": "signal." + TR, "gate": "signal." + PV}]),
        sig(OP, [{"source": "signal." + GT, "chain": {"envelope": "peakhold", "envelopeHoldMs": 0.0,
                                                       "envelopeFallPerSecond": 3.0, "envelopeRearm": 0.5,
                                                       "envelopeRefractoryMs": 60.0}},
                 {"source": "signal." + CH, "chain": {"envelope": "peakhold", "envelopeHoldMs": 0.0,
                                                       "envelopeFallPerSecond": 3.0}}], combine="max"),
        sig("padShutHold", [{"source": "control.padShut", "chain": {
            "envelope": "peakhold", "envelopeHoldMs": 4000.0, "envelopeFallPerSecond": 1000.0}}]),
    ]
    # the keep-level AND the gate as it stands now; the gate reads it next frame (signal.<p>Prev)
    s.map(M(GT + "Keep", [("signal.%sKeep" % GT, 1.0), ("signal." + GT, 1.0)], "min"),
          M(GT + "Live", [("signal." + GT, 1.0), ("signal." + CH, 1.0, True), ("signal.padShutHold", 1.0, True)],
            "product"))


def routes(s, spec, rays):
    P = spec["prefix"]
    G = "visual.%sGateLive" % P            # the gate, 0 or 1, with the shut pad applied
    OPEN = "signal.%sOpen" % P             # a flash on each opening
    LVL = "signal.%sLevel" % P
    K = lambda f: "visual.%sGate" % f      # a knob's gate
    fx = lambda i, leaf: "fx/%s/%s" % (i, leaf)
    clamp = lambda hi: {"clampEnabled": True, "clampMin": 0.0, "clampMax": hi}
    # ---- the pose: the neck snaps back (bend + head placement), the jaw drops; a step with a short mechanical settle
    dy, dz = neck_offset(TILT_OPEN)
    k = math.radians(TILT_OPEN) / BEND_LEN
    s.route(R(G, "procedural/%s-neck/deform/1/amount" % P, k, depth="visual.tiltGate", **SNAP),
            R(G, "nodes/%s-head/rotation" % P, -TILT_OPEN, comp=0, depth="visual.tiltGate", **SNAP),
            R(G, "nodes/%s-head/position" % P, dy, comp=1, depth="visual.tiltGate", **SNAP),
            R(G, "nodes/%s-head/position" % P, dz, comp=2, depth="visual.tiltGate", **SNAP),
            R(G, "nodes/%s-jaw/rotation" % P, JAW_OPEN, comp=0, **SNAP))
    # ---- the beams: shoot out on open (a step), reach with the level, cut on close
    for i, r in enumerate(rays):
        w = RAY_WEIGHT[i]
        s.route(R(G, fx(r, "intensity"), 2.2 * w, depth=K("beams"), attackMs=0, decayMs=0),
                R(LVL, fx(r, "intensity"), 0.8 * w, depth=G, attackMs=10, decayMs=60),
                R(OPEN, fx(r, "intensity"), 1.2 * w, depth=K("beams"), attackMs=0, decayMs=120))
    # ---- the flash on each opening: colour, spectral copies, a tear; the shake on the snap
    s.route(R(OPEN, "fx/%s-shake/trauma" % P, 0.5, depth=K("copies"), attackMs=0, decayMs=140, **clamp(1.0)))
    for colour, sgn in (("A", 1.0), ("B", -1.0)):
        for part in ("h", "j"):
            node = "%s-ghost%s%s" % (P, colour, part)
            s.route(R(OPEN, "nodes/%s/position" % node, 0.014 * sgn, comp=0, depth=K("copies"), attackMs=0,
                      decayMs=220),
                    R(OPEN, "nodes/%s/position" % node, 0.006 * sgn, comp=1, depth=K("copies"), attackMs=0,
                      decayMs=220))
        s.route(R(OPEN, "material/th-ghost%s/op/4/constant/constant" % colour, 0.35, comp=0, depth=K("copies"),
                  attackMs=0, decayMs=260, **clamp(0.7)))
    for part in ("body", "head", "jaw"):
        prog = "%s-%s" % (P, part)
        o = PROGRAM_OPS[prog]
        op = lambda label, kind: "material/%s/op/%d/%s/" % (prog, o.at[label], kind)
        s.route(R(G, op("washAmount", "remap") + "constant", 0.6, comp=3, depth=K("chroma"), attackMs=0,
                  decayMs=0),
                R(OPEN, op("washAmount", "remap") + "constant", 0.08, comp=2, depth=K("chroma"), attackMs=0,
                  decayMs=300),
                R(OPEN, op("washAmount", "remap") + "constant", 0.35, comp=3, depth=K("chroma"), attackMs=0,
                  decayMs=300),
                R("response.intensity", op("washSpread", "remap") + "constant", -0.25, comp=2, attackMs=2500,
                  decayMs=5000),
                R("control.palette", op("washSpread", "remap") + "constant", -0.6, comp=2),
                R(OPEN, op("bands", "threshold") + "value", -0.12, depth=K("tear"), attackMs=0, decayMs=120))
    for gid in (P + "-glowH", P + "-glowJ"):
        s.route(R(OPEN, "fx/%s/glow" % gid, 0.25, depth=K("chroma"), attackMs=0, decayMs=200))
    s.route(R(OPEN, "post/glitch/tear", 0.10, depth=K("tear"), attackMs=0, decayMs=90, **clamp(0.3)),
            R(OPEN, "post/glitch/tearShift", 30.0, depth=K("tear"), attackMs=0, decayMs=90),
            R(OPEN, "post/split/amount", 3.0, depth=K("tear"), attackMs=0, decayMs=90))
    for node in ("bust", "neck", "head", "jaw"):
        d = 2 if node == "neck" else 1
        s.route(R(OPEN, "procedural/%s-%s/deform/%d/amount" % (P, node, d), 0.003, depth=K("tear"), attackMs=0,
                  decayMs=120),
                R(OPEN, "procedural/%s-%s/deform/%d/phase" % (P, node, d), 20.0, integrate=True, attackMs=10,
                  decayMs=140))
    # ---- global gain (CC 7, bipolar about its rest) and the burst pad
    s.route(R("control.gain", "camera/exposure/compensation", 3.0, polarity="bipolar"),
            R("control.gain", "post/bloom/intensity", 0.4, polarity="bipolar"),
            R("control.padBurst", "post/glitch/tear", 0.5, attackMs=0, decayMs=150))
    for r in rays:
        s.route(R("control.padBurst", fx(r, "intensity"), 2.0, attackMs=0, decayMs=400))
    for r in s.routes:
        if r.get("depthSource", "").endswith("Gate") and r.get("depthSource", "").startswith("visual.") \
                and r["depthSource"] != G:
            r["depthMax"] = 2.0
        if r.get("depthSource") == "visual.tiltGate":
            r["depthMax"] = 2.0   # rest 0.5 -> depth 1: the authored tilt; full doubles it

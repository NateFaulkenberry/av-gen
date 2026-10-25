"""10. TALKING HEADS (docs/prototypes/talking-heads/00-brief.md; plan 03; the owner's notes 05 and 07).

Porcelain mannequin busts, suspended in a dark indigo ether, each SPEAKING ITS OWN CHANNEL like a noise gate (the owner,
stops 1-2): a head listens to one source -- a stem or a live input, found by role -- and when it sounds the head's neck
SNAPS back, its jaw drops and rays of light shoot from its mouth, as bright as the mouth is open; when it stops the neck
snaps forward, the jaw shuts and the rays are gone. Between notes the heads scan the room, each on its own: mechanical
pans, pauses and snaps of the whole bust, with eyes that dart ahead of the turn like real eyes (saccades and fixations).

One scene, N heads (`HEADS`): each head is one source spec, and everything a head owns -- its nodes, its gate, its
routes, its scan and its eyes -- is named by its prefix. The shipped project has three heads on Feline Footwear's three
stems (drums, bass, synth), found by ROLE (`role.<role>.*`, ADR-1630), so live inputs with roles replace the stems
unchanged. With no stems or inputs every head falls back to the mix's own band through a stricter, rate-limited path.
A single-head project is the same scene with one spec.

The articulated bust (tools/talking_heads/make_bust.py; anchors in meshes/th-bust.anchors.json):
  <p>-bust (body) > <p>-neck (bent by a Bend deformer)
                  > <p>-head (turned and placed on the bent neck's top) > <p>-jaw (hinged) > the mouth rays
                                                                         > <p>-eyeL / <p>-eyeR (darting)
Every route is same-frame (no macro.*). The rays' brightness and the jaw's angle are ONE route chain apart only in their
amounts (tools/talking_heads/ray_check.py proves they track on every frame).
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
EYEBALL_L = _A["eyes"]["left"]
EYEBALL_R = _A["eyes"]["right"]
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
JAW_OPEN = 10.0
# A step, then a short mechanical settle (the owner approved the snap at stop 2). The rays use the SAME chain.
SNAP = {"attackMs": 0.0, "decayMs": 0.0, "springHz": 11.0, "springDamping": 0.62}

# The heads: where each stands (metres, the body's origin), its resting heading (degrees about +Y; 0 faces the camera's
# side of the room), its scan's own timing (rates in Hz and a seed), its rays' colours, and its source: `role` (found
# by role, ADR-1630) with the band its gate reads, and the mix's fallback band. Thresholds were tuned on Feline
# Footwear's stems (07: "use Feline footwear stems for starter"). The drums head reads the drum stem's LOW band: it
# opens on the kick and shuts between kicks (a hat or snare does not hold it open).
HEADS = [
    {"prefix": "th1", "role": "drums", "band": "bass", "mix": "signal.kickEnv", "mixOpen": 0.45, "open": 0.60, "close": 0.45,
     "at": [-0.92, 0.0, -0.25], "heading": 28.0, "scan": (0.55, 0.13, 3), "colours": (BLUE, PALE, VIOLET)},
    {"prefix": "th2", "role": "bass", "band": "bass", "mix": "audio.bass", "open": 0.55, "close": 0.42,
     "at": [0.0, 0.0, 0.18], "heading": 0.0, "scan": (0.48, 0.11, 7), "colours": (MAGENTA, PINK, VIOLET)},
    {"prefix": "th3", "role": "synth", "band": "mid", "mix": "audio.mid", "open": 0.80, "close": 0.65,
     "at": [0.92, 0.0, -0.25], "heading": -28.0, "scan": (0.52, 0.17, 11), "colours": (VIOLET, PINK, PALE)},
]
MIX_STRICT = 0.15        # the mix path's thresholds sit this much higher than the stem path's
MIX_PULSE_MS = 200.0     # a mix opening is a fixed pulse ...
MIX_REFRACTORY_MS = 450.0  # ... at most once per this long (07: "on a full mix we can limit the amount of times")

DESIGN = {
    "category": "surreal entity",
    "thesis": "Talking Heads: pale porcelain mannequin busts, each speaking its own channel like a noise gate -- on a "
              "note its neck snaps back, its jaw drops and rays of light shoot from its mouth; between notes they shut "
              "and scan the room, eyes darting ahead of each turn.",
    "composition": {
        "background": "a near-black indigo to deep violet gradient with a faint blue-purple haze",
        "midground": "three busts well apart in the dark, at different headings",
        "foreground": "sparse slow motes",
        "focal": "whichever head is speaking",
        "secondary": ["the mouth rays sweeping the room", "darting eyes", "spectral copies on the speaking head"],
        "atmosphere": "dark, sparse, suspended; no floor, horizon or set",
        "post": "a restrained bloom; glitch only in bursts on an opening",
        "camera": "a slow, restrained drift; never on the music",
    },
    "palette": {"dominant": INK, "secondary": HAZE, "accent": MAGENTA, "highlight": PALE,
                "background_value": "near black", "saturation": "pale porcelain against saturated emitted light"},
    "motion": {
        "very_slow": ["the camera's drift"],
        "medium": ["the scans: pans, pauses, snaps"],
        "fast": ["the gates: neck and jaw snap, the rays with the jaw", "saccades"],
        "extremely_fast": ["signal tears on an opening"],
    },
    "vocabulary": [
        ["drums", "role.drums.bass", "head 1 speaks the kick"],
        ["bass", "role.bass.bass", "head 2 speaks the bass line"],
        ["synth", "role.synth.mid", "head 3 speaks the synth"],
        ["a mix", "signal.kickEnv / audio.bass / audio.mid", "with no stems: each head on the mix's band, rate-limited"],
        ["silence", "(no input)", "the heads are shut and scan the room"],
    ],
    "tier": "light: three articulated busts (about 45k triangles each), rays and copies only on a speaking head",
}

INDEX_ENTRY = {
    "category": kit.ABSTRACT_CATEGORY,
    "description": "Play live audio by channel: inputs (or stems) with the roles drums, bass and synth, one per head; a "
                   "plain mix works too, more sparingly. Three porcelain mannequin busts each speak their own channel "
                   "like a noise gate: on a note the neck snaps back, the jaw drops and rays of light shoot from the "
                   "mouth; between notes they scan the room, eyes darting. MIDI: CC 1 master, CC 2 rays, CC 3 gate "
                   "sensitivity, CC 4 copies, CC 5 tearing, CC 6 colour, CC 7 gain, CC 8 palette, CC 9 head tilt (each "
                   "rests at its middle and picks up); pad 36 holds every gate shut, 37 holds them open, 39 bursts. "
                   "docs/prototypes/talking-heads/.",
    "live": True,
    "name": TITLE,
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


# ================================================================================================ the mouth rays
# Rays only (the owner, D5 at stop 2: "try rays only and make them fade as fast as the mouth animation closes").
# Jaw frame: the face looks down +Z; uneven angles, lengths and weights so they never read as debug lines. Short
# enough that the outer heads' rays stay in the frame.
MOUTH_RAYS = [
    {"yaw": -16.0, "pitch": 14.0, "angle": 1.6, "length": 1.55, "aperture": 0.003},
    {"yaw": 6.0, "pitch": -8.0, "angle": 1.0, "length": 1.85, "aperture": 0.003},
    {"yaw": 30.0, "pitch": 20.0, "angle": 2.1, "length": 1.35, "aperture": 0.004},
    {"yaw": 38.0, "pitch": -4.0, "angle": 1.2, "length": 1.70, "aperture": 0.003},
    {"yaw": 18.0, "pitch": 30.0, "angle": 0.8, "length": 1.45, "aperture": 0.003},
    {"yaw": -2.0, "pitch": 7.0, "angle": 1.4, "length": 1.95, "aperture": 0.003},
]
RAY_WEIGHT = [0.55, 0.35, 0.8, 0.45, 0.3, 0.65]
RAY_BRIGHTNESS = 2.6     # a ray's intensity at a fully open jaw, times its weight


def neck_offset(deg):
    """Where the bent neck carries the head's origin at a tilt back of `deg` (applyBend: R = F / theta), in the
    neck's frame: (dy, dz) with +Z the face's way. Exact at the open pose; the routes scale it linearly."""
    th = math.radians(deg)
    if th < 1e-9:
        return 0.0, 0.0
    R_ = BEND_LEN / th
    return R_ * math.sin(th) - BEND_LEN, -R_ * (1.0 - math.cos(th))


# ================================================================================================ the scene
def build(heads=None):
    heads = heads or HEADS
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
    s.particles("motes", capacity=500, seed=17, shape="box", position=[0.0, 0.45, 0.0], extent=[3.6, 1.6, 2.4],
                direction=[0.0, 1.0, 0.0], spawnRate=8.0, lifetimeMin=12.0, lifetimeMax=20.0, spread=1.0,
                speedMin=0.005, speedMax=0.02, gravity=[0, 0.002, 0], drag=0.3, turbulence=0.02,
                turbulenceScale=0.5, sizeStart=0.0035, sizeEnd=0.002, colorStart=hexrgb(PALE) + [0.6],
                colorEnd=hexrgb(VIOLET) + [0.0], emissive=1.4, blend="additive")
    s.params_({"camera/lens/focalLength": 50.0, "post/bloom/intensity": 0.42, "post/bloom/threshold": 0.9,
               "post/output/vignette": 0.45, "post/output/grain": 0.012, "post/glitch/tearShift": 0.0,
               "post/glitch/rate": 18.0, "post/glitch/block": 40.0, "temporal/echo/enabled": False})
    s.camera["fov"] = 40.0
    if len(heads) == 1:   # one head: the stop-2 framing, three-quarter from its right
        s.drift_camera(centre=[-0.98, 0.50, 2.05], target=[0.0, 0.36, 0.0], period=78.0, amp=(0.06, 0.02, 0.04),
                       tamp=(0.01, 0.006, 0.0), steps=8)
    else:                 # the row of heads, a little from the right, all in frame at 1080p
        s.drift_camera(centre=[-0.55, 0.62, 3.25], target=[0.0, 0.34, 0.0], period=90.0, amp=(0.08, 0.02, 0.05),
                       tamp=(0.01, 0.006, 0.0), steps=8)
    control(s)
    for spec in heads:
        head(s, spec)
    return s


def head(s, spec):
    P = spec["prefix"]
    shear = {"kind": "sine", "amount": 0.0, "frequency": 240.0, "speed": 0.0, "phase": 0.0,
             "axis": [0.0, 1.0, 0.0], "displacementAxis": [1.0, 0.0, 0.0], "space": "world"}
    # (world space: the pieces have different local origins, and a shear in each one's own frame split the seams)
    bend = {"kind": "bend", "amount": 0.0, "axis": [0.0, 1.0, 0.0], "displacementAxis": [0.0, 0.0, -1.0],
            "center": [0.0, 0.0, 0.0], "falloff": BEND_LEN, "space": "local"}
    pieces = [  # node, mesh, program (its masks in its own frame), parent, position, rotation, deformers
        (P + "-bust", "th-body", P + "-body", None, spec.get("at", [0.0, 0.0, 0.0]),
         [0.0, spec.get("heading", 0.0), 0.0], [shear]),
        (P + "-neck", "th-neck", P + "-body", P + "-bust", NECK_FOOT, [0.0, 0.0, 0.0], [bend, shear]),
        (P + "-head", "th-head", P + "-head", P + "-bust", HEAD_REST, [0.0, 0.0, 0.0], [shear]),
        (P + "-jaw", "th-jaw", P + "-jaw", P + "-head", JAW_HINGE, [0.0, 0.0, 0.0], [shear]),
    ]
    s.program(bust_program(P + "-body"))
    s.program(bust_program(P + "-head", mouth=MOUTH_IN_HEAD, eye_l=EYE_L, eye_r=EYE_R))
    s.program(bust_program(P + "-jaw", mouth=MOUTH))
    for node, mesh, prog, parent, pos, rot, deformers in pieces:
        s.proc(node, {"kind": "mesh", "asset": "meshes/%s.glb" % mesh},
               material={"baseColor": list(IVORY), "roughness": 0.36, "metallic": 0.0, "program": prog},
               deformers=[dict(d) for d in deformers], position=pos, rotation=rot, scale=[1.0, 1.0, 1.0],
               parent=parent, extra={"castsShadow": False})
        s.entity(node)
    # the mouth's dark interior and the eyes (own materials, no program); the head and jaw's spectral copies, which
    # rest at a scale of nothing and grow only while this head speaks (07: ghosts only on the speaking head)
    for name, mesh, parent, pos, prog, scale in (
            (P + "-cavity", "th-mouth-cavity", P + "-head", [0.0, 0.0, 0.0], None, 1.0),
            (P + "-floor", "th-jaw-floor", P + "-jaw", [0.0, 0.0, 0.0], None, 1.0),
            (P + "-eyeL", "th-eye", P + "-head", EYEBALL_L, None, 1.0),
            (P + "-eyeR", "th-eye", P + "-head", EYEBALL_R, None, 1.0),
            (P + "-ghostAh", "th-head-ghost", P + "-head", [0.0, 0.0, 0.0], "th-ghostA", 0.001),
            (P + "-ghostAj", "th-jaw-ghost", P + "-jaw", [0.0, 0.0, 0.0], "th-ghostA", 0.001),
            (P + "-ghostBh", "th-head-ghost", P + "-head", [0.0, 0.0, 0.0], "th-ghostB", 0.001),
            (P + "-ghostBj", "th-jaw-ghost", P + "-jaw", [0.0, 0.0, 0.0], "th-ghostB", 0.001)):
        n = {"name": name, "kind": "gltf", "asset": "meshes/%s.glb" % mesh, "position": [float(v) for v in pos],
             "rotation": [0.0, 0.0, 0.0], "scale": [scale] * 3, "parent": parent, "castsShadow": False}
        if prog:
            n["program"] = prog
        s.nodes.append(n)
    cols = spec.get("colours", (MAGENTA, PINK, VIOLET))
    rays = s.emitter_fan(P + "-mouth", P + "-jaw", MOUTH,
                         [dict(r, color=hexrgb(cols[i % len(cols)])) for i, r in enumerate(MOUTH_RAYS)])
    s.effect(P + "-shake", "shake", ("entity", P + "-bust"),
             parameters={"trauma": 0.0, "amplitude": 0.004, "rotation": 1.0, "frequency": 17.0, "decay": 0.0,
                         "seed": 7.0})
    for gid, ent in ((P + "-glowH", P + "-head"), (P + "-glowJ", P + "-jaw")):
        s.effect(gid, "glow", ("entity", ent),
                 parameters={"gain": 1.0, "tint": hexrgb(cols[0]), "glow": 0.0, "rim": 0.05,
                             "rimColor": hexrgb("#7a6cff"), "rimPower": 3.5, "spill": False, "recolour": 0.0})
    gate(s, spec)
    scan_and_eyes(s, spec)
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
    # the knobs as gates: master x the feature's own knob, gain 2, so the rests (0.5 x 0.5) read 0.5; each gated route
    # maps 0..1 onto depth 0..2 (below), so at rest a route adds its own amount and a knob turned fully up doubles it
    s.map(*[M(name + "Gate", [("control.master", 1.0), ("control." + name, 1.0)], "product", gain=2.0)
            for name in ("beams", "copies", "tear", "chroma")])
    s.map(M("tiltGate", [("control.tilt", 1.0)], "mean"))
    s.sources.append(sig("padShutHold", [{"source": "control.padShut", "chain": {
        "envelope": "peakhold", "envelopeHoldMs": 4000.0, "envelopeFallPerSecond": 1000.0}}]))


def gate(s, spec):
    """One head's noise gate (05 section 3, 07). Two paths, one gate:

    THE CHANNEL (stems, or live inputs with the head's role) -- the feel the owner approved at stop 2:
      <p>Level   role.<role>.<band>, live only while that role is active; shifted by CC 3 (sensitivity)
      <p>Rise    Level less its 150 ms follower; <p>Trig a one-frame latch on a sharp rise (a re-attack)
      Schmitt    opens at `open`, stays open above `close` (through last frame's visual.<p>GateKeep), held ~33 ms
      <p>Chomp   Trig while already open: one frame shut, so every note is its own snap
    THE MIX (no channel of that role): the mix's own band, MIX_STRICT higher, as a fixed MIX_PULSE_MS opening at most
      once per MIX_REFRACTORY_MS (07: "limit the amount of times it happens").

      <p>Gate      the max of the two; visual.<p>GateLive = Gate x (1 - Chomp) x (1 - the shut pad): what the head does
      <p>Open      a one-shot on each opening and each chomp (the flashes)
    Pad 37 forces every gate open, pad 36 shut."""
    P, role, band = spec["prefix"], spec["role"], spec["band"]
    L, LS, RS, TR, GR, GT, PV, CH, OP, OM, ML, MG = ("%s%s" % (P, n) for n in (
        "Level", "LevelS", "Rise", "Trig", "GateRaw", "Gate", "Prev", "Chomp", "Open", "OnMix", "MixLevel", "MixGate"))
    sens = {"source": "control.sensitivity", "polarity": "bipolar", "amount": 0.16}   # up = more sensitive
    binary = lambda lv: {"threshold": "binary", "thresholdLevel": lv}
    s.sources += [
        sig(L, [{"source": "role.%s.%s" % (role, band)}]),
        sig(LS, [{"source": "signal." + L}, sens], combine="sum"),
        sig(RS, [{"source": "signal." + LS}, {"source": "signal." + LS, "amount": -1.0,
                                              "chain": {"attackMs": 150.0, "decayMs": 150.0}}], combine="sum"),
        sig(TR, [{"source": "signal." + RS, "chain": dict(binary(0.20), envelope="peakhold", envelopeHoldMs=0.0,
                                                          envelopeFallPerSecond=30.0, envelopeRearm=0.5,
                                                          envelopeRefractoryMs=33.0)}]),
        sig(PV, [{"source": "visual.%sKeep" % GT}]),                      # one-frame memory
        # the mix path: on only while the head's role has no active source
        sig(OM, [{"constant": 1.0}, {"source": "role.%s.active" % role, "amount": -1.0}], combine="sum",
            chain={"clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0}),
        sig(ML, [{"source": spec["mix"]}, sens], combine="sum"),
        sig(MG, [{"source": "signal." + ML, "gate": "signal." + OM,
                  "chain": dict(binary(spec.get("mixOpen", spec["open"] + MIX_STRICT)), envelope="peakhold", envelopeHoldMs=MIX_PULSE_MS,
                                envelopeFallPerSecond=1000.0, envelopeRearm=0.5,
                                envelopeRefractoryMs=MIX_REFRACTORY_MS)}], combine="max"),
        sig(GR, [{"source": "signal." + LS, "chain": binary(spec["open"])},
                 {"source": "signal." + PV},
                 {"source": "signal." + MG},
                 {"source": "control.padOpen", "chain": {"envelope": "peakhold", "envelopeHoldMs": 600.0,
                                                         "envelopeFallPerSecond": 1000.0}}],
            combine="max", chain={"attackMs": 0.0, "decayMs": 36.0}),
        sig(GT, [{"source": "signal." + GR, "chain": binary(0.4)}]),
        sig(GT + "Keep", [{"source": "signal." + LS, "chain": binary(spec["close"])}]),
        sig(CH, [{"source": "signal." + TR, "gate": "signal." + PV}]),
        sig(OP, [{"source": "signal." + GT, "chain": {"envelope": "peakhold", "envelopeHoldMs": 0.0,
                                                       "envelopeFallPerSecond": 3.0, "envelopeRearm": 0.5,
                                                       "envelopeRefractoryMs": 60.0}},
                 {"source": "signal." + CH, "chain": {"envelope": "peakhold", "envelopeHoldMs": 0.0,
                                                       "envelopeFallPerSecond": 3.0}}], combine="max"),
    ]
    # what the head does: Gate AND NOT Chomp AND NOT the shut pad (all 0 or 1: Gate - Chomp - Shut, clamped), then the POSE --
    # that through the snap's spring, in a signal, so every pose route and the rays read one value. Values under 0.002
    # are cut to exactly 0 by each route (a Bend deformer at a curvature of 1e-5 loses its normals to float precision:
    # the neck went dark while a spring's tail was still settling).
    s.sources += [
        sig(P + "Live", [{"source": "signal." + GT}, {"source": "signal." + CH, "amount": -1.0},
                         {"source": "signal.padShutHold", "amount": -1.0}],
            combine="sum", chain={"clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0}),
        sig(P + "Pose", [{"source": "signal.%sLive" % P, "chain": dict(SNAP)}]),
    ]
    s.map(M(GT + "Keep", [("signal.%sKeep" % GT, 1.0), ("signal." + GT, 1.0)], "min"))


SCAN_RANGE = 45.0       # degrees either side of a head's resting heading
SCAN_SNAP = 12.0        # the occasional snap's size
SCAN_PAN = {"springHz": 0.9, "springDamping": 1.0}     # a pan: about 0.6-1.2 s, mechanical, no overshoot


def scan_and_eyes(s, spec):
    """The scan (07: "scanning the room", readable): the whole bust's yaw. Its target is a STEPPED noise (a new heading
    every 1/rate seconds, held), followed through a critically damped spring -- a pan of about 0.6-1.2 s -- plus a
    second stepped noise added with no follower: the occasional snap. Each head has its own rates and seed.

    The eyes (07: darting, then "make the movements look more realistic"): conjugate (both eyes one route set), in the
    head's frame, through a spring with a slight overshoot (saccades accelerate and settle in about 40-70 ms):
      - they LEAD the scan: (target - current) heading, so they arrive at a new heading before the head does;
      - saccades: two stepped noises at incommensurate rates (fixations of about 0.3-1.2 s), mostly small moves;
      - downward glances: a stepped pitch biased down (07: "eyes can look down though");
      - micro-drift during a fixation: a slow smooth noise of under a degree.
    Kept within +-27 degrees yaw and -4..+16 pitch so the iris never sinks into the socket."""
    P = spec["prefix"]
    rate, snap_rate, seed = spec.get("scan", (0.4, 0.13, 1))
    noises = [("%sScanT" % P, rate, 0.0, seed), ("%sScanSnap" % P, snap_rate, 0.0, seed + 101),
              ("%sSacA" % P, 1.35, 0.0, seed + 211), ("%sSacB" % P, 2.15, 0.0, seed + 307),
              ("%sSacP" % P, 0.85, 0.0, seed + 401), ("%sDrift" % P, 0.6, 1.0, seed + 503)]
    for name, r, smooth, sd in noises:
        s.sources.append({"kind": "noise", "name": name, "settings": {"seed": sd}})
        s.params_({"sources/%s/rate" % name: r, "sources/%s/smoothness" % name: smooth})
    s.sources += [
        sig("%sScanNow" % P, [{"source": "noise.%sScanT" % P, "chain": dict(SCAN_PAN)}]),
        sig("%sLead" % P, [{"source": "noise.%sScanT" % P}, {"source": "signal.%sScanNow" % P, "amount": -1.0}],
            combine="sum"),
    ]
    bust = "nodes/%s-bust/rotation" % P
    s.route(R("signal.%sScanNow" % P, bust, SCAN_RANGE, comp=1, polarity="bipolar"),
            R("noise.%sScanSnap" % P, bust, SCAN_SNAP, comp=1, polarity="bipolar"))
    sac = {"springHz": 14.0, "springDamping": 0.78}
    for eye in ("eyeL", "eyeR"):
        rot = "nodes/%s-%s/rotation" % (P, eye)
        s.route(R("signal.%sLead" % P, rot, 14.0, comp=1, **sac),                       # lead the turn
                R("noise.%sSacA" % P, rot, 9.0, comp=1, polarity="bipolar", **sac),  # saccades
                R("noise.%sSacB" % P, rot, 3.0, comp=1, polarity="bipolar", **sac),  # small ones
                R("noise.%sSacP" % P, rot, 20.0, comp=0, offset=-0.2, **sac),           # down glances, -4..+16
                R("noise.%sDrift" % P, rot, 1.4, comp=1, polarity="bipolar"),           # fixation drift
                R("noise.%sDrift" % P, rot, 1.0, comp=0, polarity="bipolar"))


def routes(s, spec, rays):
    P = spec["prefix"]
    G = "signal.%sPose" % P                # the gate's pose, 0..1 through the snap (the shut pad applied)
    LIVE = "signal.%sLive" % P             # the gate itself, 0 or 1
    SNAP_CUT = {"threshold": "gate", "thresholdLevel": 0.002}   # exact 0 at rest (see gate())
    OPEN = "signal.%sOpen" % P             # a flash on each opening
    K = lambda f: "visual.%sGate" % f      # a knob's gate
    fx = lambda i, leaf: "fx/%s/%s" % (i, leaf)
    clamp = lambda hi: {"clampEnabled": True, "clampMin": 0.0, "clampMax": hi}
    # ---- the pose: the neck snaps back (bend + head placement), the jaw drops; a step with a short mechanical settle
    dy, dz = neck_offset(TILT_OPEN)
    k = math.radians(TILT_OPEN) / BEND_LEN
    s.route(R(G, "procedural/%s-neck/deform/1/amount" % P, k, depth="visual.tiltGate", **SNAP_CUT),
            R(G, "nodes/%s-head/rotation" % P, -TILT_OPEN, comp=0, depth="visual.tiltGate", **SNAP_CUT),
            R(G, "nodes/%s-head/position" % P, dy, comp=1, depth="visual.tiltGate", **SNAP_CUT),
            R(G, "nodes/%s-head/position" % P, dz, comp=2, depth="visual.tiltGate", **SNAP_CUT),
            R(G, "nodes/%s-jaw/rotation" % P, JAW_OPEN, comp=0, **SNAP_CUT))
    # ---- the rays: a direct function of the jaw's opening (the owner: "mouth fully closed = rays off / fully open =
    # rays fully bright"). The SAME signal (the pose, settle included) and the same route chain as the jaw's route above,
    # so ray = weight x RAY_BRIGHTNESS x (jaw angle / JAW_OPEN) on every frame. CC 2 scales it.
    for i, r in enumerate(rays):
        s.route(R(G, fx(r, "intensity"), RAY_BRIGHTNESS * RAY_WEIGHT[i], depth=K("beams"), **SNAP_CUT))
    # ---- the flash on each opening: colour, spectral copies (grown only on this head), a shake, a restrained tear
    s.route(R(OPEN, "fx/%s-shake/trauma" % P, 0.5, depth=K("copies"), attackMs=0, decayMs=140, **clamp(1.0)))
    for colour, sgn in (("A", 1.0), ("B", -1.0)):
        for part in ("h", "j"):
            node = "%s-ghost%s%s" % (P, colour, part)
            for c in range(3):
                s.route(R(LIVE, "nodes/%s/scale" % node, 0.999, comp=c, attackMs=0, decayMs=250))
            s.route(R(OPEN, "nodes/%s/position" % node, 0.014 * sgn, comp=0, depth=K("copies"), attackMs=0,
                      decayMs=220),
                    R(OPEN, "nodes/%s/position" % node, 0.006 * sgn, comp=1, depth=K("copies"), attackMs=0,
                      decayMs=220))
    for part in ("body", "head", "jaw"):
        prog = "%s-%s" % (P, part)
        o = PROGRAM_OPS[prog]
        op = lambda label, kind: "material/%s/op/%d/%s/" % (prog, o.at[label], kind)
        s.route(R(LIVE, op("washAmount", "remap") + "constant", 0.5, comp=3, depth=K("chroma"), attackMs=0,
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
    s.route(R(OPEN, "post/glitch/tear", 0.06, depth=K("tear"), attackMs=0, decayMs=90, **clamp(0.3)),
            R(OPEN, "post/glitch/tearShift", 12.0, depth=K("tear"), attackMs=0, decayMs=90),
            R(OPEN, "post/split/amount", 1.5, depth=K("tear"), attackMs=0, decayMs=90))
    for node in ("bust", "neck", "head", "jaw"):
        d = 2 if node == "neck" else 1
        s.route(R(OPEN, "procedural/%s-%s/deform/%d/amount" % (P, node, d), 0.003, depth=K("tear"), attackMs=0,
                  decayMs=120),
                R(OPEN, "procedural/%s-%s/deform/%d/phase" % (P, node, d), 20.0, integrate=True, attackMs=10,
                  decayMs=140))
    # ---- global gain (CC 7, bipolar about its rest) and the burst pad (once, on the first head)
    if spec is HEADS[0] or P == "th1":
        s.route(R("control.gain", "camera/exposure/compensation", 1.5, polarity="bipolar"),
                R("control.gain", "post/bloom/intensity", 0.2, polarity="bipolar"),
                R("control.padBurst", "post/glitch/tear", 0.5, attackMs=0, decayMs=150))
    for r in s.routes:
        ds = r.get("depthSource", "")
        if ds.startswith("visual.") and ds.endswith("Gate") and "depthMax" not in r:
            r["depthMax"] = 2.0   # knob gates (and tiltGate: rest 0.5 -> depth 1, the authored amount)

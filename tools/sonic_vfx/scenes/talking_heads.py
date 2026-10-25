"""10. TALKING HEADS (docs/prototypes/talking-heads/00-brief.md; plan 03-implementation-plan.md).

A pale porcelain mannequin bust, suspended in a dark indigo ether, that seems to speak through the music. One hero cone
of light and a few thinner, uneven rays pour from its mouth; narrow rays flick from its eyes. Under sound the bust
shivers into coloured spectral copies, is possessed by washes of colour around its mouth and eyes, and tears like a
bad video signal. It falls silent when the music does. One head; no set, no floor, no horizon.

Systems (02-research-engine.md):
- the bust: a procedural mesh node (tools/talking_heads/make_bust.py -> meshes/th-bust.glb), one material program;
- emitters: Light Beams owned by the bust that ride it (ADR-1670, `kit.Scene.emitter_fan`);
- vibration: a Shake on the bust, and two blended ghost copies (gltf children through their own programs);
- chromatic possession: the program's wash (an OKLab ramp over object-space noise, concentrated by mouth and eye masks)
  plus a Glow on the bust;
- tearing (owner decision D4): full-screen post glitch tear and split in brief bursts, which read as local on the
  near-black ether, plus the program's colour bands and a Sine shear deformer on the bust only;
- life: a Float, two noise sources on the rotation, an onset nod through a spring;
- states: continuous signal blends (signal.awake, signal.overload) with pads that force them.

Every route is same-frame: no macro.* is read.
"""
import json
import os

from .. import kit
from ..kit import R, M, hexrgb

ID = "talking-heads"
TITLE = "Talking Heads"
HERO_AT = 20.0

MESH = "meshes/th-bust.glb"
GHOST = "meshes/th-bust-ghost.glb"
ANCHORS = json.load(open(os.path.join(kit.ABSTRACT_DIR, "meshes", "th-bust.anchors.json")))["gltf"]
MOUTH = ANCHORS["mouthBack"]
MOUTH_LIP = ANCHORS["mouth"]
EYE_L = ANCHORS["eyeLeft"]
EYE_R = ANCHORS["eyeRight"]
HEAD = ANCHORS["headCentre"]

# The palette (01-research-assets.md section 6).
INK = "#07051a"          # environment
VIOLET_DEEP = "#160a2e"
HAZE = "#2a2458"         # secondary atmosphere
MAGENTA = "#ff2bd6"      # primary emission
VIOLET = "#8a3bff"
CORE = "#ffe9f7"         # the hero cone's pale core
BLUE = "#2b7bff"         # secondary emission
PINK = "#ff6fb5"
ACID = "#ff2a1a"         # accents (overload)
PALE = "#f4f0ff"
IVORY = (0.86, 0.82, 0.74)

DESIGN = {
    "category": "surreal entity",
    "thesis": "Talking Heads: a pale porcelain mannequin bust floating in a dark ether that seems to speak through the "
              "music -- a cone of light and thin uneven rays pour from its mouth and narrow rays flick from its eyes, "
              "it shivers into coloured spectral copies on the bass, is possessed by washes of colour on the voice, "
              "tears like a bad video signal on the snare, and falls silent when the music does.",
    "composition": {
        "background": "a near-black indigo to deep violet gradient with a faint blue-purple haze",
        "midground": "nothing: the ether",
        "foreground": "sparse slow motes",
        "focal": "the bust, three-quarter, 35-45% of the frame's height, its mouth on the centre line",
        "secondary": ["the hero mouth cone", "the eye rays", "the spectral copies"],
        "atmosphere": "dark, sparse, suspended; no floor, horizon or set",
        "post": "a restrained bloom; glitch only in bursts",
        "camera": "a slow, restrained drift; never on the music",
    },
    "palette": {"dominant": INK, "secondary": HAZE, "accent": MAGENTA, "highlight": CORE,
                "background_value": "near black", "saturation": "pale porcelain against saturated emitted light"},
    "motion": {
        "very_slow": ["the camera's drift", "the head's own slow turning"],
        "medium": ["colour washes", "the beams' wavering"],
        "fast": ["vibration", "eye bursts"],
        "extremely_fast": ["signal tears", "spectral spikes"],
    },
    "vocabulary": [
        ["presence", "signal.awake", "the whole entity wakes with the music and falls silent with it"],
        ["voice", "signal.voice", "the mouth speaks: the hero cone and the rays; the colour washes"],
        ["sustain", "response.sustain", "a restrained, persistent mouth glow"],
        ["bass", "signal.bassLevel", "the bust vibrates and its spectral copies part"],
        ["kick", "signal.kickEnv", "the fan swells, the vibration spikes"],
        ["hat", "signal.hat", "the eyes flick, left or right"],
        ["highs", "audio.treble", "the eyes shimmer; the fine shear of the surface"],
        ["snare", "signal.snare", "the signal tears; colour bands flash across the face"],
        ["onset", "response.onset", "the glow flashes; a nod; chromatic split on the highs' register"],
        ["intensity", "signal.overload", "OVERLOAD: everything deepens, briefly, then recovers"],
        ["silence", "(no input)", "the bust floats, pale and almost still; the beams are gone"],
    ],
    "tier": "light: one 39k-triangle bust, two ghost copies, 11 shell beams, a bloom and two cheap post passes",
}

INDEX_ENTRY = {
    "category": kit.ABSTRACT_CATEGORY,
    "description": "Play live audio (a mix, or stems or inputs with roles: a vocal or synth source drives the mouth, a "
                   "drums source the hits). A porcelain mannequin bust speaks through the music: a cone of light and "
                   "rays from its mouth, narrow rays from its eyes, spectral copies on the bass, colour washes on the "
                   "voice, signal tears on the snare; it falls silent when the music does. MIDI: CC 1 master, CC 2 "
                   "mouth, CC 3 eyes, CC 4 vibration, CC 5 tearing, CC 6 colour, CC 7 gain, CC 8 palette (each rests "
                   "at its middle and picks up); pads 36 hush, 37 speak, 38 overload, 39 burst. "
                   "docs/prototypes/talking-heads/.",
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

    def path(self, prog, label, field="constant"):
        return "material/%s/op/%d/%s/%s" % (prog, self.at[label], self.ops[self.at[label] - 1]["kind"], field)


def ellipsoid(o, label, dst, centre, radii, src=0):
    """dst = saturate(1 - sum(((p - c) / r)^2)): 1 at the centre, 0 on the ellipsoid. Four ops."""
    o.add(label + "C", "constant", dst, constant=[-centre[0], -centre[1], -centre[2], 0.0])
    o.add(label + "D", "add", dst, srcA=src, srcB=dst)
    o.add(label + "Q", "multiply", dst, srcA=dst, srcB=dst)
    o.add(label, "gradient", dst, srcA=dst, value=-1.0,
          constant=[1.0 / radii[0] ** 2, 1.0 / radii[1] ** 2, 1.0 / radii[2] ** 2, 1.0])


BUST = Ops()


def bust_program():
    """th-bust: porcelain, darkened sockets and mouth, the possession wash, the mouth glow and the signal bands.
    Registers: r0 local position, r1 mouth mask, r2 eye mask, r3 porcelain, r5 wash colour, r6 wash amount,
    r7 base colour out, r4 emission out."""
    o = BUST
    o.add("local", "input", 0, input="localPosition")
    # the masks (object space, so they ride every motion; before the deformers, so a tear does not move them)
    ellipsoid(o, "mouth", 1, [0.0, MOUTH[1], MOUTH[2]], [0.021, 0.0068, 0.024])
    ellipsoid(o, "eyeL", 2, [EYE_L[0], EYE_L[1], EYE_L[2] - 0.004], [0.021, 0.012, 0.030])
    ellipsoid(o, "eyeR", 3, [EYE_R[0], EYE_R[1], EYE_R[2] - 0.004], [0.021, 0.012, 0.030])
    o.add("eyes", "add", 2, srcA=2, srcB=3)
    # porcelain: warm ivory (routable), sockets to 60%, the mouth's cavity to near black
    o.add("ivory", "constant", 3, constant=list(IVORY) + [1.0])
    o.add("socketDark", "remap", 5, srcA=2, value=1.0, constant=[0.0, 1.0, 1.0, 0.6])
    o.add("socketMul", "multiply", 3, srcA=3, srcB=5)
    o.add("mouthDark", "remap", 5, srcA=1, value=1.0, constant=[0.0, 0.3, 1.0, 0.03])
    o.add("mouthMul", "multiply", 3, srcA=3, srcB=5)
    # the wash: object-space noise (its offset scrolls by an integrating route) through the palette's roles
    o.add("washNoise", "noise", 4, srcA=0, value=7.0, constant=[0.0, 0.0, 0.0, 0.0], seed=5)
    o.add("washSpread", "remap", 4, srcA=4, value=1.0, constant=[0.32, 0.68, 0.0, 1.0])
    o.add("washRamp", "oklabRamp", 5, srcA=4, value=1.0, constant=hexrgb(VIOLET) + [1.0],
          constant2=hexrgb(MAGENTA) + [1.0], constant3=hexrgb(PINK) + [1.0], constant4=hexrgb(BLUE) + [1.0])
    # the amount: a global level (constant.z) plus a concentration at the mouth and eyes (constant.w - z)
    o.add("masks", "add", 6, srcA=1, srcB=2)
    o.add("washAmount", "remap", 6, srcA=6, value=1.0, constant=[0.0, 1.0, 0.0, 0.0])
    o.add("washBase", "mixBy", 7, srcA=3, srcB=5, srcC=6)
    # emission: the wash's own glow, the mouth glow, then the signal bands
    o.add("washGlowAmt", "multiply", 4, srcA=5, srcB=6)
    o.add("washGlow", "constant", 6, constant=[0.35, 0.35, 0.35, 0.0])
    o.add("washGlowMul", "multiply", 4, srcA=4, srcB=6)
    o.add("mouthGlow", "constant", 6, constant=[0.0, 0.0, 0.0, 0.0])
    o.add("mouthGlowMul", "multiply", 6, srcA=6, srcB=1)
    o.add("mouthGlowAdd", "add", 4, srcA=4, srcB=6)
    # signal bands (T2): a hash per (height band, time epoch); bands above a threshold flash in the wash colour
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
    return {"name": "th-bust", "ops": o.ops, "baseColor": 7, "metallic": -1, "roughness": -1, "emission": 4,
            "emissionIntensity": 1.0, "opacity": -1}


def ghost_program(name, colour):
    """A spectral copy: black, a fresnel edge in its colour, opacity (constant op `opacity`) routed from zero."""
    return {"name": name, "ops": [
        {"kind": "fresnel", "dst": 0, "value": 1.6},                                          # 1 edge
        {"kind": "constant", "dst": 1, "constant": hexrgb(colour, 1.6) + [1.0]},              # 2 colour
        {"kind": "multiply", "dst": 2, "srcA": 0, "srcB": 1},                                 # 3 edge glow
        {"kind": "constant", "dst": 3, "constant": [0.0, 0.0, 0.0, 0.0]},                     # 4 opacity (routed)
        {"kind": "remap", "dst": 4, "srcA": 0, "value": 1.0, "constant": [0.0, 1.0, 0.35, 1.0]},  # 5 edge weighting
        {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 4},                                 # 6
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 4},                                 # 7
        {"kind": "constant", "dst": 5, "constant": [0.0, 0.0, 0.0, 1.0]},                     # 8 black body
    ], "baseColor": 5, "metallic": -1, "roughness": -1, "emission": 2, "emissionIntensity": 1.0, "opacity": 3}


# ================================================================================================ the emitters
# Local frame: the face looks down +Z, +X is the bust's own left. The camera sits on its right (-X), three-quarter,
# so +X and +Z both lean toward the frame's right: the fan pours right and a little up, away from the cover's diagonal.
MOUTH_RAYS = [
    # the hero cone: wide, soft, a pale core, always the brightest
    {"yaw": 18.0, "pitch": 7.0, "angle": 10.0, "length": 2.4, "aperture": 0.012, "coreSharpness": 3.2,
     "edgeSoftness": 0.75, "falloff": 1.1, "dust": 0.12, "color": hexrgb(CORE)},
    # six thinner rays fanned round it, uneven (their weights live in RAY_WEIGHT)
    {"yaw": -14.0, "pitch": 20.0, "angle": 1.6, "length": 2.6, "aperture": 0.003, "color": hexrgb(MAGENTA)},
    {"yaw": 4.0, "pitch": -9.0, "angle": 0.9, "length": 2.9, "aperture": 0.003, "color": hexrgb(VIOLET)},
    {"yaw": 36.0, "pitch": 24.0, "angle": 2.2, "length": 2.3, "aperture": 0.004, "color": hexrgb(PINK)},
    {"yaw": 42.0, "pitch": -4.0, "angle": 1.1, "length": 3.0, "aperture": 0.003, "color": hexrgb(MAGENTA)},
    {"yaw": 24.0, "pitch": 33.0, "angle": 0.8, "length": 2.4, "aperture": 0.003, "color": hexrgb(BLUE)},
    {"yaw": -2.0, "pitch": 11.0, "angle": 1.3, "length": 2.8, "aperture": 0.003, "color": hexrgb(VIOLET)},
]
RAY_WEIGHT = [1.0, 0.55, 0.3, 0.8, 0.4, 0.25, 0.65]
EYE_RAYS = [
    {"yaw": 9.0, "pitch": 4.0, "angle": 0.55, "length": 3.6},
    {"yaw": 14.0, "pitch": 1.0, "angle": 0.85, "length": 2.8},
]
EYE_LOOK = {"aperture": 0.002, "coreSharpness": 6.0, "edgeSoftness": 0.3, "falloff": 1.0, "dust": 0.05,
            "color": hexrgb(BLUE)}


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
    # light: a warm key high on the camera's side, a cool violet rim behind; neither casts (an unlit ground)
    s.light("key", "directional", direction=[0.55, -0.55, -0.62], color=hexrgb("#ffe2c0"), intensity=3.4,
            castsShadow=False, contactShadow=False)
    s.light("rim", "directional", direction=[-0.45, -0.25, 0.85], color=hexrgb("#8a6cff"), intensity=2.2,
            castsShadow=False, contactShadow=False)
    s.light("fill", "directional", direction=[-0.6, 0.2, -0.75], color=hexrgb("#5a50c8"), intensity=0.25,
            castsShadow=False, contactShadow=False)

    # ---- the bust (a procedural mesh node, for the shear deformer) and its spectral copies
    shear = [{"kind": "sine", "amount": 0.0, "frequency": 240.0, "speed": 0.0, "phase": 0.0,
              "axis": [0.0, 1.0, 0.0], "displacementAxis": [1.0, 0.0, 0.0], "space": "local"}]
    s.proc("th-bust", {"kind": "mesh", "asset": MESH},
           material={"baseColor": list(IVORY), "roughness": 0.36, "metallic": 0.0, "program": "th-bust"},
           deformers=shear, position=[0.0, 0.0, 0.0], rotation=[0.0, 0.0, 0.0], scale=[1.0, 1.0, 1.0],
           extra={"castsShadow": False})
    s.entity("th-bust")
    for name, prog in (("th-ghostA", "th-ghostA"), ("th-ghostB", "th-ghostB")):
        s.nodes.append({"name": name, "kind": "gltf", "asset": GHOST, "position": [0.0, 0.0, 0.0],
                        "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0], "parent": "th-bust",
                        "castsShadow": False, "program": prog})
    s.program(bust_program())
    s.program(ghost_program("th-ghostA", MAGENTA))
    s.program(ghost_program("th-ghostB", BLUE))

    # ---- sparse motes for depth
    s.particles("motes", capacity=400, seed=17, shape="box", position=[0.0, 0.45, 0.0], extent=[2.4, 1.4, 2.0],
                direction=[0.0, 1.0, 0.0], spawnRate=6.0, lifetimeMin=12.0, lifetimeMax=20.0, spread=1.0,
                speedMin=0.005, speedMax=0.02, gravity=[0, 0.002, 0], drag=0.3, turbulence=0.02,
                turbulenceScale=0.5, sizeStart=0.0035, sizeEnd=0.002, colorStart=hexrgb(PALE) + [0.6],
                colorEnd=hexrgb(VIOLET) + [0.0], emissive=1.4, blend="additive")

    # ---- the effects on the bust
    mouth = s.emitter_fan("th-mouth", "th-bust", MOUTH, MOUTH_RAYS)
    eye_l = s.emitter_fan("th-eyeL", "th-bust", EYE_L,
                          [dict(r, yaw=r["yaw"] + 3.0) for r in EYE_RAYS], look=EYE_LOOK)
    eye_r = s.emitter_fan("th-eyeR", "th-bust", EYE_R,
                          [dict(r, yaw=r["yaw"] - 3.0) for r in EYE_RAYS], look=EYE_LOOK)
    s.effect("th-shake", "shake", ("entity", "th-bust"),
             parameters={"trauma": 0.0, "amplitude": 0.004, "rotation": 1.0, "frequency": 17.0, "decay": 0.0,
                         "seed": 7.0})
    s.effect("th-float", "float", ("entity", "th-bust"),
             parameters={"height": 0.007, "period": 9.0, "tilt": 1.2, "yaw": 2.5, "sway": 0.004, "seed": 3.0})
    s.effect("th-glow", "glow", ("entity", "th-bust"),
             parameters={"gain": 1.0, "tint": hexrgb(MAGENTA), "glow": 0.0, "rim": 0.05,
                         "rimColor": hexrgb("#7a6cff"), "rimPower": 3.5, "spill": False, "recolour": 0.0})

    # ---- post: a restrained bloom; the glitch passes rest at 0 and only routes open them
    s.params_({"camera/lens/focalLength": 50.0, "post/bloom/intensity": 0.42, "post/bloom/threshold": 0.9,
               "post/output/vignette": 0.45, "post/output/grain": 0.012, "post/glitch/tearShift": 0.0,
               "post/glitch/rate": 18.0, "post/glitch/block": 40.0, "temporal/echo/enabled": True,
               "temporal/echo/frames": 3.0, "temporal/echo/strength": 0.12, "temporal/echo/decay": 0.6,
               "sources/waverA/rate": 0.7, "sources/waverB/rate": 0.9, "sources/eyeL/rate": 0.8,
               "sources/eyeR/rate": 0.65, "sources/yaw/rate": 0.031, "sources/pitch/rate": 0.047,
               "sources/diverge/rate": 0.11})

    # ---- camera: three-quarter from the bust's right, the mouth near the centre line; a slow drift
    s.camera["fov"] = 40.0
    s.drift_camera(centre=[-1.02, 0.52, 2.18], target=[0.0, 0.40, 0.0], period=78.0, amp=(0.07, 0.02, 0.05),
                   tamp=(0.01, 0.006, 0.0), steps=8)

    signals_and_control(s)
    routes(s, mouth, eye_l, eye_r)
    return s


# ================================================================================================ the instrument
def sig(name, inputs, combine="firstActive", chain=None, event=False):
    st = {"combine": combine, "inputs": inputs}
    if chain:
        st["chain"] = chain
    if event:
        st["event"] = True
    return {"kind": "signal", "name": name, "settings": st}


CHANNELS = [("master", 1), ("mouth", 2), ("eyes", 3), ("vibration", 4), ("tear", 5), ("chroma", 6), ("gain", 7),
            ("palette", 8)]
PADS = [("padHush", 36), ("padSpeak", 37), ("padOverload", 38), ("padBurst", 39)]


def signals_and_control(s):
    hold = lambda ms, fall: {"envelope": "peakhold", "envelopeHoldMs": ms, "envelopeFallPerSecond": fall}
    s.sources += [
        # presence: the music's own loudness, slowly; a pad holds it awake
        sig("awake", [{"source": "response.level", "chain": {"attackMs": 1200.0, "decayMs": 4000.0,
                                                               "remapEnabled": True, "remapInMin": 0.08,
                                                               "remapInMax": 0.45, "remapOutMin": 0.0,
                                                               "remapOutMax": 1.0, "clampEnabled": True,
                                                               "clampMin": 0.0, "clampMax": 1.0}},
                       {"source": "control.padSpeak", "chain": hold(8000.0, 0.5)}], combine="max"),
        sig("hush", [{"source": "control.padHush", "chain": hold(6000.0, 0.4)}], combine="max"),
        # the voice: a vocal stem, else a synth stem, else the mix's mid band
        sig("voice", [{"source": "role.vocal.level"}, {"source": "role.synth.level"}, {"source": "audio.mid"}],
            chain={"attackMs": 40.0, "decayMs": 300.0}),
        sig("snare", [{"source": "role.drums.snare"}, {"source": "response.snare"}], event=True),
        sig("hat", [{"source": "role.drums.hat"}, {"source": "response.hat"}], event=True),
        # overload: macro dynamics high (self-limiting: the 12 s follower falls), or the pad
        sig("overload", [{"source": "response.intensity", "chain": {"remapEnabled": True, "remapInMin": 0.7,
                                                                      "remapInMax": 0.95, "remapOutMin": 0.0,
                                                                      "remapOutMax": 1.0, "clampEnabled": True,
                                                                      "clampMin": 0.0, "clampMax": 1.0,
                                                                      "attackMs": 300.0, "decayMs": 2500.0}},
                          {"source": "control.padOverload", "chain": hold(6000.0, 0.35)}], combine="max"),
    ]
    s.sources += [{"kind": "noise", "name": n, "settings": {}} for n in
                  ("waverA", "waverB", "eyeL", "eyeR", "yaw", "pitch", "diverge")]
    # the gates: presence (less a hush) x master x the feature's own knob, gain 2, so the rests (0.5 x 0.5) read 0.5;
    # every gated route maps its gate 0..1 onto depth 0..2 (depthMax), so at rest a route adds its own amount and a
    # knob turned up doubles it.
    s.map(M("present", [("signal.awake", 1.0), ("signal.hush", 1.0, True)], "product"))
    s.map2(*[M(name + "Gate", [("visual.present", 1.0), ("control.master", 1.0), ("control." + name, 1.0)],
               "product", gain=2.0) for name in ("mouth", "eyes", "vibration", "tear", "chroma")])
    # which eye a hat fires: two slow noises through the eyes' gate
    s.map3(M("eyeLPick", [("noise.eyeL", 1.0), ("visual.eyesGate", 1.0)], "product", gain=1.6),
           M("eyeRPick", [("noise.eyeR", 1.0), ("visual.eyesGate", 1.0)], "product", gain=1.6))
    s.control = {"midi": {"enabled": True, "filter": "*", "pickup": True,
                          "bindings": [{"source": "*", "channel": -1, "kind": "cc", "number": n, "signal": name}
                                       for name, n in CHANNELS]
                          + [{"source": "*", "channel": -1, "kind": "noteEvent", "number": n, "signal": name}
                             for name, n in PADS]},
                 "channels": [{"name": name, "rest": 0.0 if name == "palette" else 0.5, "pickup": True}
                              for name, _ in CHANNELS]}


def routes(s, mouth, eye_l, eye_r):
    G = lambda f: "visual.%sGate" % f
    hero, rays = mouth[0], mouth[1:]
    fx = lambda i, leaf: "fx/%s/%s" % (i, leaf)
    clamp = lambda hi: {"clampEnabled": True, "clampMin": 0.0, "clampMax": hi}
    # ---- mouth (M rows)
    s.route(R("response.sustain", fx(hero, "intensity"), 0.45, depth=G("mouth"), attackMs=120, decayMs=900),
            R("signal.voice", fx(hero, "intensity"), 0.8, depth=G("mouth"), attackMs=40, decayMs=400),
            R("signal.kickEnv", fx(hero, "intensity"), 0.6, depth=G("mouth"), attackMs=0, decayMs=180),
            R("signal.overload", fx(hero, "intensity"), 0.6, depth=G("mouth"), attackMs=0, decayMs=600),
            R("response.onset", fx(hero, "coreSharpness"), 1.6, depth="response.onsetLow", attackMs=0, decayMs=200),
            R("signal.kickEnv", fx(hero, "angle"), 2.0, depth=G("mouth"), attackMs=0, decayMs=220))
    for i, r in enumerate(rays):
        w = RAY_WEIGHT[i + 1]
        s.route(R("signal.voice", fx(r, "intensity"), 2.4 * w, depth=G("mouth"), attackMs=40, decayMs=350),
                R("signal.kickEnv", fx(r, "intensity"), 1.2 * w, depth=G("mouth"), attackMs=0, decayMs=200),
                R("signal.kickEnv", fx(r, "angle"), 1.0, depth=G("mouth"), attackMs=0, decayMs=220),
                R("signal.overload", fx(r, "intensity"), 1.0 * w, depth=G("mouth"), attackMs=0, decayMs=600),
                R("noise.waverA" if i % 2 else "noise.waverB", fx(r, "yaw"), 6.0, polarity="bipolar",
                  depth="signal.voice"),
                R("noise.waverB" if i % 2 else "noise.waverA", fx(r, "pitch"), 4.0, polarity="bipolar",
                  depth="signal.voice"))
    s.route(R("signal.voice", "material/th-bust/op/%d/constant/constant" % BUST.at["mouthGlow"], 3.0, comp=0,
              depth=G("mouth"), attackMs=30, decayMs=300),
            R("signal.voice", "material/th-bust/op/%d/constant/constant" % BUST.at["mouthGlow"], 1.6, comp=1,
              depth=G("mouth"), attackMs=30, decayMs=300),
            R("signal.voice", "material/th-bust/op/%d/constant/constant" % BUST.at["mouthGlow"], 2.6, comp=2,
              depth=G("mouth"), attackMs=30, decayMs=300))
    # ---- eyes (E rows): bursts on the hats, one eye or the other; a quiet shimmer floor on the highs
    for ids, pick in ((eye_l, "visual.eyeLPick"), (eye_r, "visual.eyeRPick")):
        for e in ids:
            s.route(R("signal.hat", fx(e, "intensity"), 3.6, depth=pick, attackMs=0, decayMs=90,
                      threshold="binary", thresholdLevel=0.05),
                    R("audio.treble", fx(e, "intensity"), 0.5, depth=G("eyes"), attackMs=30, decayMs=250),
                    R("response.transient", fx(e, "length"), 1.2, depth=G("eyes"), attackMs=0, decayMs=200))
    for e in eye_r:
        s.route(R("noise.diverge", fx(e, "yaw"), 4.0, polarity="bipolar"))
    # ---- vibration (V rows)
    s.route(R("signal.bassLevel", "fx/th-shake/trauma", 1.0, depth=G("vibration"), attackMs=30, decayMs=350,
              **clamp(1.0)),
            R("signal.kickEnv", "fx/th-shake/trauma", 1.2, depth=G("vibration"), attackMs=0, decayMs=160,
              **clamp(1.0)))
    for ghost, sgn in (("th-ghostA", 1.0), ("th-ghostB", -1.0)):
        s.route(R("signal.bassLevel", "nodes/%s/position" % ghost, 0.012 * sgn, comp=0, depth=G("vibration"),
                  attackMs=20, decayMs=300),
                R("signal.bassLevel", "nodes/%s/position" % ghost, 0.008 * sgn, comp=1, depth=G("vibration"),
                  attackMs=20, decayMs=300),
                R("signal.kickEnv", "nodes/%s/position" % ghost, 0.03 * sgn, comp=0, depth=G("vibration"),
                  attackMs=0, decayMs=180),
                R("signal.overload", "nodes/%s/position" % ghost, 0.02 * sgn, comp=0, attackMs=0, decayMs=800),
                R("signal.bassLevel", "material/%s/op/4/constant/constant" % ghost, 0.5, comp=0, depth=G("vibration"),
                  attackMs=20, decayMs=300, **clamp(0.6)),
                R("signal.kickEnv", "material/%s/op/4/constant/constant" % ghost, 0.5, comp=0, depth=G("vibration"),
                  attackMs=0, decayMs=180, **clamp(0.6)))
    # ---- chromatic possession (C rows)
    wash = "material/th-bust/op/%d/remap/constant" % BUST.at["washAmount"]
    s.route(R("signal.voice", wash, 0.10, comp=2, depth=G("chroma"), attackMs=250, decayMs=2400),
            R("signal.voice", wash, 0.8, comp=3, depth=G("chroma"), attackMs=250, decayMs=2400),
            R("signal.overload", wash, 0.5, comp=3, attackMs=0, decayMs=1500),
            R("response.onset", "fx/th-glow/glow", 0.5, depth=G("chroma"), attackMs=0, decayMs=250),
            R("response.intensity", "material/th-bust/op/%d/remap/constant" % BUST.at["washSpread"], -0.25,
              comp=2, attackMs=2500, decayMs=5000),
            R("control.palette", "material/th-bust/op/%d/remap/constant" % BUST.at["washSpread"], -0.6, comp=2),
            # the wash crawls: an integrating route scrolls the noise's offset while the voice speaks
            R("signal.voice", "material/th-bust/op/%d/noise/constant" % BUST.at["washNoise"], 0.25, comp=1,
              integrate=True, attackMs=100, decayMs=600))
    # ---- tearing (T rows, D4): full-screen bursts, local bands, the shear
    s.route(R("signal.snare", "post/glitch/tear", 0.5, depth=G("tear"), attackMs=0, decayMs=120, **clamp(0.6)),
            R("signal.snare", "post/glitch/tearShift", 90.0, depth=G("tear"), attackMs=0, decayMs=120),
            R("signal.snare", "post/split/amount", 8.0, depth=G("tear"), attackMs=0, decayMs=110),
            R("signal.overload", "post/glitch/amount", 0.025, depth=G("tear"), attackMs=0, decayMs=800),
            R("signal.overload", "post/glitch/tear", 0.15, depth=G("tear"), attackMs=0, decayMs=800),
            R("signal.snare", "material/th-bust/op/%d/threshold/value" % BUST.at["bands"], -0.35, depth=G("tear"),
              attackMs=0, decayMs=150),
            R("audio.treble", "procedural/th-bust/deform/1/amount", 0.0016, depth=G("tear"), attackMs=10,
              decayMs=120),
            R("signal.snare", "procedural/th-bust/deform/1/amount", 0.006, depth=G("tear"), attackMs=0,
              decayMs=140),
            R("signal.snare", "procedural/th-bust/deform/1/phase", 20.0, integrate=True, attackMs=0, decayMs=140))
    # ---- life (L rows; not gated: the head is faintly alive in silence)
    s.route(R("noise.yaw", "procedural/th-bust/transform/rotation", 6.0, comp=1, polarity="bipolar"),
            R("noise.pitch", "procedural/th-bust/transform/rotation", 3.0, comp=0, polarity="bipolar"),
            R("response.onset", "procedural/th-bust/transform/rotation", 1.5, comp=0, depth="response.onsetLow",
              attackMs=0, decayMs=80, springHz=3.5, springDamping=0.5))
    # ---- residual trails in overload
    s.route(R("signal.overload", "temporal/echo/strength", 0.3, attackMs=250, decayMs=900))
    # ---- global gain (CC 7, bipolar about its rest) and the burst pad
    s.route(R("control.gain", "camera/exposure/compensation", 3.0, polarity="bipolar"),
            R("control.gain", "post/bloom/intensity", 0.4, polarity="bipolar"),
            R("control.padBurst", fx(hero, "intensity"), 3.0, attackMs=0, decayMs=400),
            R("control.padBurst", "post/glitch/tear", 0.5, attackMs=0, decayMs=150))
    for e in eye_l + eye_r:
        s.route(R("control.padBurst", fx(e, "intensity"), 3.0, attackMs=0, decayMs=250))
    for r in s.routes:
        if r.get("depthSource", "").endswith("Gate") or r.get("depthSource", "").endswith("Pick"):
            r["depthMax"] = 2.0

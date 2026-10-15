"""CYMATIC PLATE (abstract and scientific). Black sand on a resonating plate: every note sings its own standing-wave
figure into the sand, and a chord is two figures at war.

Composition (art pass 2026-10, the owner's request: "try having the plate be 2d - top down, rather than 3d at an
angle"; docs/prototypes/sonic-garden/ART-PASS-2026-10-cymatic-plate.md): the plate seen straight down, as a flat
picture that fills the frame -- a black steel plate in the frame's own 16:9 proportion, a thin margin of void round its
bevelled edge, the driver bolt at its centre. A long lens from high above keeps the view nearly orthographic. The sand
gathered on the nodal lines of a Chladni figure is the subject; the light pools on the figure's centre and falls away
to the corners (painted in the program: the plate is unlit, which is what keeps a full-frame plate inside the 60 fps
budget). The figure is a material program: the mode numbers (n, m) are op constants the routes move,
so a melody re-forms the sand continuously. The cells stay square on the wide plate: both axes take the plate's depth
as their unit, so the figure simply runs on past the short side's span to the long edges.
"""
from .. import kit, signals
from ..kit import R, M, hexrgb, scale3, SLOW, MEDIUM, FAST, HIT, SNAP

ID = "cymatic-plate"
TITLE = "Cymatic Plate"

STEEL = "#0b0b0c"
SAND = "#d9cdb6"
RIM = "#9cc6ff"
WARM = "#ffd8a8"

DESIGN = {
    "category": "abstract",
    "thesis": "Black sand on a resonating plate: every note sings its own standing-wave figure into the sand, and a "
              "chord is two figures at war.",
    "composition": {
        "background": "a thin margin of black void round the plate's edge",
        "midground": "a black steel plate filling the frame, seen straight down, the sand gathered on the nodal lines",
        "foreground": "(none: a flat, top-down picture)",
        "focal": "the figure's densest crossing",
        "secondary": ["sand leaping off the plate on hits", "glitter on the hats"],
        "atmosphere": "(none)",
        "post": "crisp, everything in focus; a slight bloom on the sand; a warm-and-cold grade",
        "camera": "top-down, nearly orthographic (135 mm from 19 m), breathing a few per cent closer and back over 64 s",
    },
    "palette": {"dominant": STEEL, "secondary": SAND, "accent": RIM, "highlight": "#fff4e2",
                "background_value": "black", "saturation": "nearly none: bone sand on black steel, a cold rim"},
    "motion": {
        "very_slow": ["the camera's breath"],
        "medium": ["the figure re-forming as the melody moves"],
        "fast": ["sand leaping on notes and kicks"],
        "extremely_fast": ["glitter (hat)"],
    },
    "vocabulary": [
        ["melodic", "notes.lastPitch", "the mode numbers: low notes draw simple figures, high notes intricate ones"],
        ["timbre", "sonic.brightness.slow", "with audio alone, the sound's brightness sets the mode: a brighter sound "
         "draws a more intricate figure (a played note takes over when it is higher)"],
        ["chord", "notes.lowest", "a second figure from the lowest note is superposed: the figures "
         "fight"],
        ["sustained", "notes.held", "a held note sharpens the figure (the nodal lines narrow); silence scatters the "
         "sand"],
        ["velocity", "response.noteEnv", "the sand flashes and leaps with the note's strength"],
        ["kick", "response.kick", "the whole bed leaps, scatters and settles back onto its lines"],
        ["snare", "response.snare", "a burst of sand from the nodes"],
        ["hat", "response.hat", "granular glitter"],
        ["bass", "response.bass", "the plate flexes and the sand swells on its lines"],
        ["tension", "notes.tension", "the figure jitters"],
    ],
    "tier": "light: one plate, one program, two particle systems",
}

L = 3.2           # the plate's depth (metres): the figure's unit
W = L * 16.0 / 9.0  # its width: the frame's proportion
CAM_H, FOCAL = 19.0, 135.0  # straight down from 19 m on a 135 mm lens: the frame is 6.0 x 3.4 m at the plate


def chladni_program():
    """f = cos(n pi u) cos(m pi v) - cos(m pi u) cos(n pi v) on the unit square; sand gathers where |f| is small.
    The cosines come from the palette op (k + k2 cos(2 pi (k3 t + k4))) with k3 = (n/2, m/2): ops 2 and 4 hold the
    mode numbers, and op 11's smoothstep the line width. A second figure (ops 14-20) adds a chord's lowest note."""
    def cos_pair(dst, src, n, m):
        return {"kind": "palette", "dst": dst, "srcA": src, "constant": [0.0, 0.0, 0.0, 0.0],
                "constant2": [1.0, 1.0, 1.0, 0.0], "constant3": [n / 2.0, m / 2.0, 0.0, 0.0],
                "constant4": [0.0, 0.0, 0.0, 0.0]}
    ops = [
        {"kind": "input", "dst": 0, "input": "localPosition"},
        {"kind": "remap", "dst": 0, "srcA": 0, "value": 0, "constant": [-L / 2, L / 2, 0.0, 1.0]},     # (u, _, v)
        cos_pair(1, 0, 3.0, 5.0),                                                                     # op 3: x
        {"kind": "swizzle", "dst": 2, "srcA": 0, "constant": [2.0, 2.0, 2.0, 2.0]},                   # v
        cos_pair(2, 2, 3.0, 5.0),                                                                     # op 5: z
        {"kind": "swizzle", "dst": 2, "srcA": 2, "constant": [1.0, 0.0, 2.0, 3.0]},                   # swap
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},       # (cos nu cos mv, cos mu cos nv)
        {"kind": "swizzle", "dst": 2, "srcA": 1, "constant": [1.0, 1.0, 1.0, 1.0]},
        {"kind": "swizzle", "dst": 1, "srcA": 1, "constant": [0.0, 0.0, 0.0, 0.0]},
        {"kind": "constant", "dst": 3, "constant": [-1.0, -1.0, -1.0, -1.0]},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},             # f
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 1},
        {"kind": "power", "dst": 1, "srcA": 1, "value": 0.5},        # |f|
        {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.16, 0.0, 0.0, 0.0]},  # op 15: the line width
        # the second figure (a chord's lowest note), weighted by op 26's constant
        cos_pair(4, 0, 2.0, 3.0),                                                                     # op 16
        {"kind": "swizzle", "dst": 5, "srcA": 0, "constant": [2.0, 2.0, 2.0, 2.0]},
        cos_pair(5, 5, 2.0, 3.0),                                                                     # op 18
        {"kind": "swizzle", "dst": 5, "srcA": 5, "constant": [1.0, 0.0, 2.0, 3.0]},
        {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},
        {"kind": "swizzle", "dst": 5, "srcA": 4, "constant": [1.0, 1.0, 1.0, 1.0]},
        {"kind": "swizzle", "dst": 4, "srcA": 4, "constant": [0.0, 0.0, 0.0, 0.0]},
        {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 3},
        {"kind": "add", "dst": 4, "srcA": 4, "srcB": 5},
        {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 4},
        {"kind": "power", "dst": 4, "srcA": 4, "value": 0.5},
        {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.16, 0.0, 0.0, 0.0]},  # op 27
        {"kind": "constant", "dst": 5, "constant": [0.0, 0.0, 0.0, 0.0]},                 # op 28: the chord weight
        {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 4},            # both figures
        {"kind": "remap", "dst": 1, "srcA": 1, "value": 1, "constant": [0.0, 1.0, 0.0, 1.0]},     # clamp 0..1
        # grain: the sand is grains, not paint -- a random value per 6.7 mm cell (Quantize's hash, ADR-1162), about two
        # pixels from the top-down camera; the fBm this replaced cost 5 ms once the plate filled the frame
        {"kind": "input", "dst": 2, "input": "worldPosition"},
        {"kind": "quantize", "dst": 2, "srcA": 2, "value": 150.0, "seed": 9},
        {"kind": "swizzle", "dst": 2, "srcA": 2, "constant": [3.0, 3.0, 3.0, 3.0]},
        {"kind": "remap", "dst": 2, "srcA": 2, "value": 1, "constant": [0.0, 1.0, 0.5, 1.12]},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},       # the sand mask, grained
        {"kind": "constant", "dst": 6, "constant": hexrgb(STEEL) + [1.0]},
        {"kind": "constant", "dst": 7, "constant": hexrgb(SAND, 0.75) + [1.0]},
        {"kind": "mixBy", "dst": 6, "srcA": 6, "srcB": 7, "srcC": 1},  # base colour
        # the light, painted: the plate is UNLIT (top-down it fills the frame, and the lit path cost 9 ms of shading
        # for a flat picture), so the key's pool is here -- brightest at the centre, falling to 0.3 at the corners
        {"kind": "input", "dst": 2, "input": "worldPosition"},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 2},
        {"kind": "gradient", "dst": 7, "srcA": 2, "value": 1.0 / (3.4 * 3.4), "constant": [1.0, 0.0, 1.0, 0.0]},
        {"kind": "remap", "dst": 7, "srcA": 7, "value": 1, "constant": [0.0, 1.0, 1.25, 0.3]},
        {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
        {"kind": "input", "dst": 3, "input": "materialEmission"},
        {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 1},           # the sand's flash
    ]
    return {"name": "cymatic", "ops": ops, "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 3,
            "emissionIntensity": 1.0, "opacity": -1}


# 1-based op indices of the routed constants (checked by the build, below)
OP_X, OP_Z, OP_WIDTH, OP_X2, OP_Z2, OP_WIDTH2, OP_CHORD = 3, 5, 15, 16, 18, 27, 28


def build():
    prog = chladni_program()
    ops = prog["ops"]
    assert ops[OP_X - 1]["kind"] == "palette" and ops[OP_Z - 1]["kind"] == "palette"
    assert ops[OP_WIDTH - 1]["kind"] == "smoothstep" and ops[OP_WIDTH2 - 1]["kind"] == "smoothstep"
    assert ops[OP_X2 - 1]["kind"] == "palette" and ops[OP_Z2 - 1]["kind"] == "palette"
    assert ops[OP_CHORD - 1]["kind"] == "constant" and len(ops) <= 48

    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.0, "background": [0.0, 0.0, 0.0], "fogColor": [0, 0, 0], "volumeDensity": 0.0,
        "volumeMaxDistance": 0.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": scale3(hexrgb("#14161a"), 0.08),
                "horizonColor": scale3(hexrgb("#0c0d0f"), 0.05), "groundColor": [0.0, 0.0, 0.0], "haze": 0.0,
                "sunIntensity": 0.0, "intensity": 1.0, "background": True, "useKeyLight": False},
    }
    # no stars: the void is a studio's black, not a sky
    s.effect("nostars", "stars", ("world",), parameters={"brightness": 0.0, "density": 0.0})
    s.composition = {"focalPoints": [{"name": "figure", "position": [0.0, 0.03, 0.0], "radius": 1.0, "weight": 1.0}]}
    s.program(prog)
    s.proc("plate", {"kind": "box", "size": [W, 0.04, L], "subdivisions": 48, "bevel": 0.012},
           material={"baseColor": [1, 1, 1], "emissiveColor": hexrgb("#fff1d6"), "emissiveIntensity": 0.0,
                     "roughness": 0.3, "metallic": 0.6, "program": "cymatic", "unlit": True},
           transform={"position": [0.0, 0.0, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
           deformers=[{"kind": "sine", "amount": 0.0, "frequency": 1.8, "speed": 9.0, "axis": [1, 0, 0.6],
                       "displacementAxis": [0, 1, 0]}])
    s.proc("driver", {"kind": "cylinder", "radius": 0.07, "height": 0.08, "radialSegments": 24, "caps": True,
                      "bevel": 0.01},
           material={"baseColor": hexrgb("#8a8d92"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.25, "metallic": 1.0},
           transform={"position": [0.0, 0.04, 0.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # sand leaping off the plate (kick, note) and a burst from the nodes (snare), falling back onto it
    s.particles("leap", capacity=8000, seed=3, shape="box", position=[0.0, 0.03, 0.0],
                extent=[W * 0.5 - 0.1, 0.0, L * 0.5 - 0.1],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.5, lifetimeMax=1.1, spread=0.7, speedMin=0.4,
                speedMax=1.3, gravity=[0, -6.0, 0], drag=0.2, sizeStart=0.008, sizeEnd=0.006, sizeVariance=0.5,
                colorStart=hexrgb(SAND) + [1.0], colorEnd=hexrgb(SAND) + [0.6], emissive=0.9, blend="additive",
                collision="kill", collisionHeight=0.025)
    s.particles("glitter", capacity=1600, seed=11, shape="box", position=[0.0, 0.035, 0.0],
                extent=[W * 0.5 - 0.1, 0.0, L * 0.5 - 0.1],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.05, lifetimeMax=0.12, spread=1.0, speedMin=0.0,
                speedMax=0.0, gravity=[0, 0, 0], drag=0.0, sizeStart=0.012, sizeEnd=0.0,
                colorStart=hexrgb("#ffffff") + [1.0], colorEnd=hexrgb(WARM) + [0.0], emissive=8.0, blend="additive")

    # light: the plate is unlit (its pool of light is painted in its program, above); these two light only the driver
    # bolt -- a warm key high to one side and a cold rim from the other, so the bolt reads as polished metal
    s.light("key", "spot", position=[-5.5, 7.0, 3.0], direction=[0.58, -0.74, -0.34], color=hexrgb(WARM),
            intensity=540.0, range=20.0, innerCone=12.0, outerCone=26.0, castsShadow=False, volumetric=0.0)
    s.light("rim", "directional", direction=[0.9, -0.3, 0.3], color=hexrgb(RIM), intensity=1.2,
            castsShadow=False)

    # camera: straight down (a hair of z so the view's up is defined), breathing a few per cent closer and back (a
    # seamless loop), so the still picture is never quite still
    s.camera = {"mode": 1, "position": [0.0, CAM_H, 0.02], "target": [0.0, 0.0, 0.0], "fov": 10.0,
                "orbitSpeed": 0.0}
    s.track("camera/position", [{"time": 0.0, "value": [0.0, CAM_H, 0.02], "interp": "smooth"},
                                {"time": 32.0, "value": [0.0, CAM_H * 0.96, 0.02], "interp": "smooth"},
                                {"time": 64.0, "value": [0.0, CAM_H, 0.02], "interp": "smooth"}], loop=64.0)
    s.track("camera/target", [{"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "smooth"},
                              {"time": 64.0, "value": [0.0, 0.0, 0.0], "interp": "smooth"}], loop=64.0)

    # ---- the instrument ---------------------------------------------------------------------------------------------
    pre = "material/cymatic/op/%d/"
    # pitch -> the mode numbers: lastPitch 0.30 (D3) -> n 1.6, 0.70 (C6) -> n 8.4; m = n + 1.7 (asymmetric figures)
    # (and with no notes -- audio alone, as from a track -- the timbre's slow brightness draws the figure: max() lets a
    #  played pitch take over whenever it is higher)
    s.map(M("modeN", [("lastPitch", 1.0), ("brightnessSlow", 1.1)], "max", -0.75, 2.5))
    s.map(M("modeLow", [("lowest", 1.0)], "mean", -0.75, 2.5))
    s.map(M("chordW", [("chord", 1.0), ("polyphony", 0.8)], "max"))
    s.map(M("sharp", [("held", 1.0), ("sustain", 0.6)], "max"))
    for op, src in ((OP_X, "visual.modeN"), (OP_Z, "visual.modeN"), (OP_X2, "visual.modeLow"),
                    (OP_Z2, "visual.modeLow")):
        s.route(R(src, (pre % op) + "palette/constant3", 3.4, comp=0, attackMs=180, decayMs=600),
                R(src, (pre % op) + "palette/constant3", 3.4, comp=1, attackMs=180, decayMs=600))
    s.route(R("visual.chordW", (pre % OP_CHORD) + "constant/constant", 0.85, comp=0, **MEDIUM),
            R("visual.chordW", (pre % OP_CHORD) + "constant/constant", 0.85, comp=1, **MEDIUM),
            R("visual.chordW", (pre % OP_CHORD) + "constant/constant", 0.85, comp=2, **MEDIUM),
            R("visual.chordW", (pre % OP_CHORD) + "constant/constant", 0.85, comp=3, **MEDIUM))
    # sustained: the lines narrow; silence widens them into scattered sand
    for op in (OP_WIDTH, OP_WIDTH2):
        s.route(R("visual.sharp", (pre % op) + "smoothstep/constant", -0.1, comp=0, **SLOW))
    s.map(M("silence", [("energySlow", 1.0, True)], "mean", -0.6, 2.5))
    for op in (OP_WIDTH, OP_WIDTH2):
        s.route(R("visual.silence", (pre % op) + "smoothstep/constant", 0.25, comp=0, **SLOW))
    # velocity: the sand flashes and leaps with the note
    s.route(R("noteEnv", "procedural/plate/material/emissive", 1.6, attackMs=0, decayMs=220),
            R("note", "particles/leap/burst", 220.0, attackMs=0, decayMs=40))
    # kick: the whole bed leaps and blurs for a moment
    s.route(R("kick", "particles/leap/burst", 900.0, attackMs=0, decayMs=40))
    for op in (OP_WIDTH, OP_WIDTH2):
        s.route(R("kickEnv", (pre % op) + "smoothstep/constant", 0.12, comp=0, attackMs=0, decayMs=250))
    # snare: a burst from the nodes; hat: glitter
    s.route(R("snare", "particles/leap/burst", 500.0, attackMs=0, decayMs=40),
            R("snare", "post/bloom/intensity", 0.18, attackMs=0, decayMs=200),
            R("hat", "particles/glitter/burst", 40.0, attackMs=0, decayMs=30))
    # bass: the plate flexes, and the flex gathers the sand: the lines swell (from straight above a flex alone is
    # invisible, so the sand carries it)
    s.route(R("bass", "procedural/plate/deform/1/amount", 0.012, attackMs=40, decayMs=300))
    for op in (OP_WIDTH, OP_WIDTH2):
        s.route(R("bass", (pre % op) + "smoothstep/constant", 0.06, comp=0, attackMs=60, decayMs=500))
    # tension: the figure jitters
    s.map(M("jitter", [("tension", 1.0)], "mean"))
    s.route(R("visual.jitter", (pre % OP_X) + "palette/constant3", 0.35, comp=0, attackMs=30, decayMs=60))
    s.route(R("kick", "post/lens/chromaticAberration", 0.02, attackMs=0, decayMs=120))

    s.params_({
        "post/bloom/intensity": 0.22, "post/bloom/threshold": 1.0, "post/output/vignette": 0.6,
        "post/output/grain": 0.015, "post/grade/contrast": 1.12, "post/grade/saturation": 0.9,
        "post/lens/chromaticAberration": 0.006,
        "camera/lens/focalLength": FOCAL, "camera/exposure/compensation": 0.3,
        "post/dof/enabled": False,
    })
    # ---- the evaluator's screen regions, projected through the camera at t = 0, and the performer's baseline
    s.region("plate", box=[0.03, 0.03, 0.97, 0.97])
    s.region("centre", box=[0.35, 0.3, 0.65, 0.7])
    s.response = {"sensitivity": 0.5, "transient": 0.6, "sustain": 0.5, "attack": 0.8, "release": 1.0}
    return s

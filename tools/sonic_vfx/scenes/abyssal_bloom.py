"""ABYSSAL BLOOM (underwater). Two kilometres down, a colony of siphonophores answers the melody in pulses of cold
light while marine snow falls through the hats.

Composition (SCENE-CATALOG.md #10): black-blue water with no surface and no floor. The subject is a siphonophore --
a colony of zooids strung on one long stem -- curving diagonally across the frame from the dark upper left to the
lower right. Each note sends the light along the stem to the pitch's place (the light travels from the last note's
zooid to the new one, so a melody is light running up and down the colony). Five jellyfish hang at different
depths; the one at the pitch's depth answers. Marine snow drifts down through everything. The camera descends.
"""
import math

from .. import kit, signals
from ..kit import R, M, hexrgb, scale3, lathe, strand, SLOW, MEDIUM, FAST, HIT, SNAP, VERY_SLOW

ID = "abyssal-bloom"
TITLE = "Abyssal Bloom"

ABYSS = "#01060f"
TEAL = "#06323a"
MAGENTA = "#ff4fd8"
CYAN = "#4ff6ff"

N_ZOOIDS = 40

# the stem: an S from the upper left (far) to the lower right (near)
CHAIN = [(-9.0, 5.5, -14.0), (-6.0, 4.4, -11.5), (-3.2, 3.0, -9.6), (-0.6, 2.2, -8.4), (1.8, 1.0, -7.6),
         (4.0, -0.4, -6.4), (5.8, -1.6, -5.0), (7.4, -2.8, -3.6)]
_D = [b - a for a, b in zip(CHAIN[0], CHAIN[-1])]
AXIS_LEN = math.sqrt(sum(v * v for v in _D))
AXIS_UNIT = [round(v / AXIS_LEN, 5) for v in _D]
# the jellyfish: (position, pitch centre on notes.lastPitch, bell radius)
JELLIES = [((-6.5, -2.0, -16.0), 0.32, 0.55), ((-2.5, 4.8, -13.0), 0.62, 0.45), ((2.5, 3.6, -15.5), 0.52, 0.7),
           ((6.0, 1.8, -12.0), 0.44, 0.4), ((-4.5, 0.6, -7.0), 0.38, 0.32)]

DESIGN = {
    "category": "underwater",
    "thesis": "Two kilometres down, a colony of siphonophores answers the melody in pulses of cold light while "
              "marine snow falls through the hats.",
    "composition": {
        "background": "black-blue water, no surface, no floor",
        "midground": "jellyfish at different depths, faint until they answer",
        "foreground": "the siphonophore's stem curving across the frame, upper left to lower right",
        "focal": "the zooid the light has run to (the latest note's place)",
        "secondary": ["the answering jellyfish", "the light running along the stem", "marine snow"],
        "atmosphere": "dense, absorbing water: light dies within metres",
        "post": "bloom, a cold grade, fine grain",
        "camera": "a slow descent beside the colony",
    },
    "palette": {"dominant": ABYSS, "secondary": TEAL, "accent": MAGENTA, "highlight": CYAN,
                "background_value": "black",
                "saturation": "the bioluminescence is the only colour: cyan, with magenta in the jellies"},
    "motion": {
        "very_slow": ["the descent", "the snow's fall", "the jellies' drift"],
        "medium": ["the bells' pulsing", "the colony's glow (sustain)"],
        "fast": ["the light running along the stem (notes)"],
        "extremely_fast": ["the snare's flare", "snow glints (hat)", "the kick's pressure wave"],
    },
    "vocabulary": [
        ["melodic", "notes.lastPitch", "the light runs along the stem to the pitch's place"],
        ["note", "response.note", "the zooids at the place flare; the jellyfish at the pitch's depth answers"],
        ["polyphony", "notes.polyphony", "more of the colony lights at once"],
        ["sustained", "response.sustain", "the whole colony glows"],
        ["bass", "response.bass", "the current swells: the snow streams and the colony sways"],
        ["kick", "response.kick", "a pressure wave ripples the water"],
        ["snare", "response.snare", "a flare through the colony"],
        ["hat", "response.hat", "marine snow glints"],
        ["silence", "(no input)", "the dark water, the colony's faint stem, the snow falling"],
    ],
    "tier": "medium: an absorbing water march (24 steps), one instanced colony, five bells",
}


def zooid_program():
    """The zooids' light: a band along the colony (instanceIndex / N against the routed place), its width the
    polyphony, plus the sustain's base glow. OP_PLACE's constant x is the place (0..1), OP_WIDTH's the band's width,
    OP_GAIN's the brightness."""
    return {
        "name": "abZooid",
        "ops": [
            # u along the colony: the world position projected on the stem's axis, first zooid to last (the stem is
            # near enough to straight that this is its arc length; it needs no per-instance index)
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "gradient", "dst": 0, "srcA": 0, "value": 1.0 / AXIS_LEN,
             "constant": AXIS_UNIT + [-sum(a * b for a, b in zip(AXIS_UNIT, CHAIN[0])) / AXIS_LEN]},
            {"kind": "constant", "dst": 1, "constant": [0.0, 0.0, 0.0, 0.0]},             # (kept: op numbering)
            {"kind": "constant", "dst": 1, "constant": [0.0, 0.0, 0.0, 0.0]},             # OP_PLACE: -place
            {"kind": "add", "dst": 0, "srcA": 0, "srcB": 1},
            {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 0},                         # (u - place)^2
            {"kind": "constant", "dst": 2, "constant": [0.004, 0.0, 0.0, 0.0]},           # OP_WIDTH
            {"kind": "add", "dst": 2, "srcA": 2, "srcB": 0},
            {"kind": "power", "dst": 2, "srcA": 2, "value": -1.0},                        # 1 / (d^2 + w)
            {"kind": "constant", "dst": 3, "constant": [0.004, 0.0, 0.0, 0.0]},           # = w (so the peak is 1)
            {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},
            {"kind": "constant", "dst": 3, "constant": [0.4, 0.0, 0.0, 0.0]},             # OP_GAIN
            {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},
            {"kind": "constant", "dst": 3, "constant": [0.04, 0.0, 0.0, 0.0]},            # OP_BASE (sustain)
            {"kind": "add", "dst": 2, "srcA": 2, "srcB": 3},
            {"kind": "swizzle", "dst": 2, "srcA": 2, "constant": [0.0, 0.0, 0.0, 0.0]},
            {"kind": "constant", "dst": 4, "constant": hexrgb(CYAN, 1.0) + [0.0]},
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 2},
            {"kind": "fresnel", "dst": 5, "value": 2.0},                                  # rim glow
            {"kind": "remap", "dst": 5, "srcA": 5, "value": 1, "constant": [0.0, 1.0, 0.6, 1.6]},
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},
            {"kind": "constant", "dst": 6, "constant": hexrgb("#020a12") + [1.0]},
        ],
        "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 4, "emissionIntensity": 1.0, "opacity": -1,
    }


OP_PLACE, OP_WIDTH, OP_GAIN, OP_BASE = 4, 7, 12, 14


def bell(r):
    """A jellyfish bell: a dome with a frilled rim (a lathe), opening downward."""
    return lathe([(0.0, r * 0.98), (0.05 * r, r * 1.02), (0.25 * r, r * 0.95), (0.55 * r, r * 0.78),
                  (0.8 * r, r * 0.5), (0.95 * r, r * 0.22), (1.0 * r, 0.02)], sides=24, samples=3)


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.45, "sustain": 0.6, "attack": 1.2, "release": 1.6}
    s.environment = {
        "intensity": 0.0, "background": hexrgb(ABYSS), "fogColor": hexrgb("#010812"),
        "volumeDensity": 0.09, "volumeMaxDistance": 40.0, "volumeScattering": 0.25, "volumeAbsorption": 1.0,
        "volumeAnisotropy": 0.6, "volumeNoise": 0.2, "volumeNoiseScale": 0.15, "volumeLocalLights": 1.0,
        "skyIntensity": 0.0, "sky": {"enabled": False, "background": False},
    }
    s.composition = {
        "focalPoints": [{"name": "colony", "position": list(CHAIN[4]), "radius": 2.0, "weight": 1.0}],
        "layers": [{"name": "near", "start": 0.0, "end": 12.0, "contrast": 1.0, "saturation": 1.0},
                   {"name": "far", "start": 12.0, "end": 60.0, "contrast": 0.7, "saturation": 0.8}],
    }
    s.program(zooid_program())

    # ---- the colony: a spline, the stem along it, and the zooids distributed along it
    pts = [{"position": list(p), "scale": 1.0, "roll": 0.0} for p in CHAIN]
    s.nodes.append({"name": "chain", "kind": "spline",
                    "spline": {"name": "chain", "kind": "catmullRom", "closed": False, "points": pts}})
    s.proc("stem", {"kind": "tube", "tubeRadius": 0.025, "tubeTaper": 0.6, "tubeSides": 6, "tubeSegments": 240,
                    "tubeTwist": 0.0, "tubeCaps": True,
                    "curve": {"kind": "catmullRom", "generator": "points", "points": pts, "samplesPerSegment": 16}},
           material={"baseColor": hexrgb("#03121a"), "emissiveColor": hexrgb(CYAN), "emissiveIntensity": 0.35,
                     "roughness": 0.3, "metallic": 0.0})
    zooid = lathe([(0.0, 0.02), (0.06, 0.12), (0.16, 0.17), (0.26, 0.12), (0.34, 0.03)], sides=10, samples=3)
    s.proc("zooids", zooid, distribution={"kind": "spline", "spline": "chain", "count": N_ZOOIDS,
                                          "alignToSpline": True},
           material={"baseColor": [1, 1, 1], "emissiveColor": [0, 0, 0], "emissiveIntensity": 1.0, "roughness": 0.3,
                     "metallic": 0.0, "program": "abZooid"},
           variation={"seed": 3, "rotation": [0.5, 0.5, 0.5], "uniformScale": 0.35})

    # ---- the jellyfish: bells with a magenta-cyan rim, a light inside each (it answers its pitch)
    for k, (pos, _, r) in enumerate(JELLIES):
        s.proc("jelly%d" % k, bell(r),
               material={"baseColor": hexrgb("#05101a"), "emissiveColor": hexrgb(MAGENTA if k % 2 else CYAN),
                         "emissiveIntensity": 0.25, "roughness": 0.2, "metallic": 0.0},
               deformers=[{"kind": "sine", "amount": 0.06 * r, "axis": [0, 1, 0], "displacementAxis": [1, 0, 1],
                           "frequency": 3.0, "phase": k * 1.3, "speed": 1.6}],
               transform={"position": list(pos), "rotation": [0.0, 0.0, 9.0 * (k - 2)], "scale": [1, 1, 1]})
        # its tentacles: faint strands hanging from round the rim, drifting
        s.proc("tentacles%d" % k, strand((0.0, 0.0, 0.0), (0.0, -6.0 * r, 0.0), 0.006 + 0.004 * r, taper=0.1,
                                         seed=40 + k, noise=0.35 * r, noise_scale=0.6, count=6, sides=4,
                                         segments=24),
               distribution={"kind": "radial", "count": 9, "radius": r * 0.82, "center": [pos[0], pos[1], pos[2]],
                             "plane": "xz", "orientation": "none"},
               material={"baseColor": hexrgb("#05101a"), "emissiveColor": hexrgb(MAGENTA if k % 2 else CYAN),
                         "emissiveIntensity": 0.12, "roughness": 0.4, "metallic": 0.0},
               variation={"seed": 60 + k, "rotation": [0.15, 3.1, 0.15], "scale": [0.0, 0.35, 0.0]},
               deformers=[{"kind": "noise", "amount": 0.25 * r, "scale": 0.5, "speed": 0.25, "seed": k,
                           "axisMask": [1.0, 0.0, 1.0], "space": "world"}])
        s.light("jellyLight%d" % k, "point", position=[pos[0], pos[1] - 0.3 * r, pos[2]],
                color=hexrgb(MAGENTA if k % 2 else CYAN), intensity=0.6, range=4.0 + 4.0 * r, radius=r,
                castsShadow=False, volumetric=1.0)
    s.light("colonyLight", "point", position=list(CHAIN[4]), color=hexrgb(CYAN), intensity=0.0, range=7.0,
            radius=1.0, castsShadow=False, volumetric=1.0)

    # ---- marine snow (always), and its glints (hats)
    s.particles("snow", capacity=9000, seed=29, shape="box", position=[0.0, 7.0, -9.0], extent=[14.0, 1.0, 9.0],
                direction=[0.05, -1.0, 0.0], spawnRate=220.0, lifetimeMin=14.0, lifetimeMax=22.0, spread=0.3,
                speedMin=0.25, speedMax=0.5, gravity=[0, -0.02, 0], drag=0.2, turbulence=0.12, turbulenceScale=0.4,
                sizeStart=0.012, sizeEnd=0.008, colorStart=hexrgb("#9fc8d8") + [0.55],
                colorEnd=hexrgb("#9fc8d8") + [0.0], emissive=0.35, blend="additive")
    s.particles("glints", capacity=2000, seed=31, shape="box", position=[0.0, 1.0, -9.0], extent=[10.0, 5.0, 6.0],
                direction=[0, -1, 0], spawnRate=0.0, lifetimeMin=0.15, lifetimeMax=0.5, spread=1.0, speedMin=0.0,
                speedMax=0.05, gravity=[0, 0, 0], drag=0.5, sizeStart=0.016, sizeEnd=0.0,
                colorStart=hexrgb("#e8ffff") + [1.0], colorEnd=hexrgb(CYAN) + [0.0], emissive=12.0, blend="additive")

    # ---- camera: a slow descent beside the colony
    s.camera["fov"] = 50.0
    s.track("camera/position", [{"time": 0.0, "value": [-1.0, 3.0, 6.0], "interp": "smooth"},
                                {"time": 60.0, "value": [0.2, 0.6, 5.6], "interp": "smooth"},
                                {"time": 120.0, "value": [-1.0, 3.0, 6.0], "interp": "smooth"}], loop=120.0)
    s.track("camera/target", [{"time": 0.0, "value": [-0.6, 1.6, -9.0], "interp": "smooth"},
                              {"time": 60.0, "value": [0.6, 0.4, -9.0], "interp": "smooth"},
                              {"time": 120.0, "value": [-0.6, 1.6, -9.0], "interp": "smooth"}], loop=120.0)
    s.camera = {"mode": 1, "position": [-1.0, 3.0, 6.0], "target": [-0.6, 1.6, -9.0], "fov": 50.0,
                "orbitSpeed": 0.0}

    # ---- the instrument ---------------------------------------------------------------------------------------------
    prog = "material/abZooid/op/%d/constant/constant"
    # melody: the light runs along the stem to the pitch's place (a spring, so it travels)
    s.map(M("place", [("lastPitch", 1.0)], "mean", -0.28 / 0.44, 1.0 / 0.44))
    s.route(R("visual.place", prog % OP_PLACE, -1.0, comp=0, springHz=1.4, springDamping=0.8))
    # notes flare it; polyphony widens it; sustain is the colony's base glow; the snare flares it all
    s.route(R("noteEnv", prog % OP_GAIN, 2.6, comp=0, depth="lastVelocity", attackMs=0, decayMs=600),
            R("polyphony", prog % OP_WIDTH, 0.03, comp=0, **MEDIUM),
            R("polyphony", "material/abZooid/op/10/constant/constant", 0.03, comp=0, **MEDIUM),
            R("sustain", prog % OP_BASE, 0.5, comp=0, **SLOW),
            R("snare", prog % OP_BASE, 2.5, comp=0, attackMs=0, decayMs=300),
            R("noteEnv", "lights/colonyLight/intensity", 3.0, attackMs=0, decayMs=600))
    # the jellyfish at the pitch's depth answers
    s.places("jl", "lastPitch", [c for _, c, _ in JELLIES], 0.05, event="noteOn")
    for k in range(len(JELLIES)):
        s.route(R("visual.jlHit%d" % k, "lights/jellyLight%d/intensity" % k, 14.0, attackMs=0, decayMs=1400),
                R("visual.jlHit%d" % k, "procedural/jelly%d/material/emissive" % k, 2.5, attackMs=0, decayMs=1400))
    # bass: the current -- the snow streams sideways and the colony light swells
    s.route(R("bass", "particles/snow/gravity", 0.25, comp=0, attackMs=400, decayMs=1600),
            R("bass", "particles/snow/turbulence", 0.3, attackMs=300, decayMs=1200))
    # kick: a pressure wave; hat: snow glints
    s.route(R("kickEnv", "post/shock/amount", 26.0, attackMs=0, decayMs=0),
            R("kickEnv", "post/shock/radius", -1.1, op="replace", offset=-1.0, attackMs=0, decayMs=0),
            R("hat", "particles/glints/burst", 30.0, attackMs=0, decayMs=40),
            R("sustain", "post/bloom/intensity", 0.1, **SLOW))

    s.params_({
        "scene/volumeSteps": 24, "scene/volumeJitter": 0.5,
        "post/bloom/intensity": 0.4, "post/bloom/threshold": 0.8, "post/output/vignette": 0.5,
        "post/output/grain": 0.02, "post/grade/temperature": -0.05, "post/tonemap/operator": 3,
        "post/shock/amount": 0.0, "post/shock/radius": 1.2, "post/shock/width": 0.12, "post/shock/chroma": 0.0,
        "post/shock/centerX": 0.55, "post/shock/centerY": 0.5,
        "camera/lens/focalLength": 30.0,
    })

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region_points("colony", [list(p) for p in CHAIN], pad=0.03)
    s.region("jellies", box=[0.0, 0.0, 1.0, 0.45])
    s.region("water", box=[0.7, 0.0, 1.0, 0.4])
    return s

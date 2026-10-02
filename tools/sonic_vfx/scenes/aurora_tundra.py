"""AURORA TUNDRA (atmospheric, cold). Over a frozen lake the aurora is the pad: chords hang curtains of light, the
melody ripples them, the hats are diamond dust glittering in the air, and the snare cracks the ice with light.

Composition (SCENE-CATALOG.md #12): the camera stands on the lake ice and looks up. The horizon is a black spruce
line on the lower quarter; the aurora fills the sky above it. A pressure ridge of broken ice slabs runs from the
left foreground toward the centre of the far shore, catching the aurora's green; the clear ice between reflects
nothing but the sky's glow, and the snare draws light along its cracks.

The aurora's own spectrum response (`audio/sensitivity`) is OFF: driven by the spectrum it would be a visualizer.
It answers through the routes below, so each class has one visible job.
"""
import math

from .. import kit, signals
from ..kit import R, M, hexrgb, scale3, lathe, SLOW, MEDIUM, FAST, HIT, SNAP, VERY_SLOW

ID = "aurora-tundra"
TITLE = "Aurora Tundra"

NAVY = "#071226"
ICE = "#cfe6f5"
VIOLET = "#b07cff"
GREEN = "#5cff9a"

CAM = (0.0, 1.6, 6.0)

DESIGN = {
    "category": "atmospheric",
    "thesis": "Over a frozen lake the aurora is the pad: chords hang curtains of light, the melody ripples them, the "
              "hats are diamond dust glittering in the air, and the snare cracks the ice with light.",
    "composition": {
        "background": "the aurora filling the sky over a black spruce line; stars",
        "midground": "the far shore's spruce silhouettes on the lower quarter",
        "foreground": "lake ice with frost and cracks; a pressure ridge of broken slabs running toward the shore",
        "focal": "the aurora's brightest fold, above the ridge's end",
        "secondary": ["the pressure ridge catching the green", "the cracks lighting on the snare", "diamond dust"],
        "atmosphere": "cold clear air; diamond dust near the camera",
        "post": "bloom on the curtains, fine grain, a cold grade",
        "camera": "low on the ice, looking up; a slow pan along the shore",
    },
    "palette": {"dominant": NAVY, "secondary": ICE, "accent": VIOLET, "highlight": GREEN,
                "background_value": "dark (a night sky)",
                "saturation": "the aurora is the only saturated colour; the ice takes its green"},
    "motion": {
        "very_slow": ["the camera's pan", "the curtains' drift", "the palette (chords)"],
        "medium": ["the curtains' folds and height (bass)", "the brightness (sustain)"],
        "fast": ["ripples along the curtains (notes)"],
        "extremely_fast": ["cracks of light in the ice (snare)", "diamond dust (hat)", "a surge (kick)"],
    },
    "vocabulary": [
        ["sustained", "response.sustain", "the curtains brighten and lengthen"],
        ["chords / tension", "notes.tension", "the curtain colour moves through the palette: green "
         "(consonant), teal, violet, rose (dissonant)"],
        ["melodic", "response.note", "a ripple runs through the curtains and their edges flare"],
        ["bass", "response.bass", "the curtains reach lower and taller"],
        ["kick", "response.kick", "a surge of brightness through the curtains; the ridge glints"],
        ["snare", "response.snare", "the ice cracks with light"],
        ["hat", "response.hat", "diamond dust glitters in the air"],
        ["velocity", "notes.lastVelocity", "how big a note's ripple is"],
        ["silence", "(no input)", "a faint green arc drifting over the trees, the stars, the still ice"],
    ],
    "tier": "light: the aurora is shells in the sky pass; no march",
}


def ice_program():
    """Black lake ice: clear dark ice with frost patches, and a network of cracks whose light the snare raises
    (op OP_CRACK's constant is the cracks' emission)."""
    return {
        "name": "atIce",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.05, "seed": 21},              # frost patches
            {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [0.5, 0.64, 0.0, 0.0]},
            {"kind": "constant", "dst": 2, "constant": hexrgb("#0c1626", 0.6) + [1.0]},    # clear ice
            {"kind": "constant", "dst": 3, "constant": hexrgb("#a9bccb", 0.55) + [1.0]},   # frost
            {"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 3, "srcC": 1},
            {"kind": "remap", "dst": 4, "srcA": 1, "value": 1, "constant": [0.0, 1.0, 0.06, 0.7]},  # roughness
            {"kind": "noise", "dst": 5, "srcA": 0, "value": 0.11, "seed": 5},               # the crack network
            {"kind": "remap", "dst": 5, "srcA": 5, "value": 0, "constant": [0.5, 1.0, 0.0, 1.0]},
            {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 5},
            {"kind": "smoothstep", "dst": 5, "srcA": 5, "constant": [0.0, 0.0009, 0.0, 0.0]},  # 0 on a crack
            {"kind": "remap", "dst": 5, "srcA": 5, "value": 1, "constant": [0.0, 1.0, 1.0, 0.0]},  # 1 on a crack
            {"kind": "input", "dst": 6, "input": "cameraDistance"},
            {"kind": "remap", "dst": 6, "srcA": 6, "value": 1, "constant": [10.0, 70.0, 1.0, 0.0]},
            {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 6},                            # fade far
            {"kind": "constant", "dst": 7, "constant": hexrgb("#7fe8ff", 0.0) + [0.0]},     # OP_CRACK
            {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 5},
        ],
        "baseColor": 2, "metallic": -1, "roughness": 4, "emission": 7, "emissionIntensity": 1.0, "opacity": -1,
    }


OP_CRACK = 16


def spruce(seed):
    """A spruce silhouette: a trunk and four tiers, as one lathe."""
    return lathe([(0.0, 0.25), (0.9, 0.25), (1.0, 2.4), (3.6, 0.8), (3.7, 1.9), (6.5, 0.6), (6.6, 1.4),
                  (9.0, 0.35), (9.1, 0.95), (11.6, 0.05)], sides=6, samples=1)


def palette():
    def st(name, low, mid, top):
        return {"name": name, "colors": {"low": hexrgb(low), "mid": hexrgb(mid), "top": hexrgb(top)}}
    return {
        "states": [st("green", "#3dff8f", "#3ce0c8", "#7d6bff"),
                   st("teal", "#3cf2c8", "#3ab0ff", "#9a6bff"),
                   st("violet", "#7a8cff", "#b07cff", "#ff6bd2"),
                   st("rose", "#ff6b9a", "#c86bff", "#5a3cff")],
        "bindings": [{"role": "low", "target": "fx/aurora/lowColor"},
                     {"role": "mid", "target": "fx/aurora/midColor"},
                     {"role": "top", "target": "fx/aurora/topColor"}],
        "position": 0.15, "saturation": 1.0, "value": 1.0,
    }


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.6, "attack": 1.3, "release": 1.7}
    s.environment = {
        "intensity": 0.5, "background": hexrgb("#03060d"), "fogColor": hexrgb("#0a1426"), "volumeDensity": 0.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb("#020511"), "horizonColor": hexrgb("#0b1a30"),
                "groundColor": hexrgb("#02040a"), "haze": 0.18, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.palette = palette()
    s.composition = {
        "focalPoints": [{"name": "fold", "position": [380.0, 1400.0, -3800.0], "radius": 600.0, "weight": 1.0}],
        "layers": [
            {"name": "ice", "start": 0.0, "end": 80.0, "contrast": 1.0, "saturation": 0.9},
            {"name": "shore", "start": 80.0, "end": 1500.0, "contrast": 0.9, "saturation": 0.8},
        ],
    }
    s.program(ice_program())

    # ---- the aurora (its own spectrum response off: the routes are its instrument)
    s.effect("aurora", "aurora", ("world",), parameters={
        "appearance": {"edgeBrightness": 2.2, "emission": 1.0, "filaments": 1.1, "horizonGlow": 0.45,
                       "intensity": 2.2, "lowColor": hexrgb("#3dff8f"), "midColor": hexrgb("#3ce0c8"),
                       "topColor": hexrgb("#7d6bff"), "opacity": 0.85, "sparkle": 0.2},
        "audio": {"bass": 0.0, "beat": 0.0, "glints": 0.0, "high": 0.0, "lowMid": 0.0, "mid": 0.0,
                  "sensitivity": 0.0, "spectrumShape": 0.0},
        "rainbow": {"brightness": 1.0, "enabled": False, "hueOffset": 0.0, "saturation": 0.85, "scale": 1.1,
                    "speed": 0.0},
        "shape": {"anchor": "camera", "anchorPosition": [0.0, 0.0, 0.0], "baseHeight": 60.0, "complexity": 30.0,
                  "curtainCount": 3.0, "curtainHeight": 2400.0, "driftSpeed": 0.08, "flowSpeed": 0.04,
                  "layerSpacing": 0.32, "radius": 5200.0, "turbulence": 0.4, "verticalSpeed": 0.05,
                  "waveAmplitude": 0.32, "waveScale": 2.2}},
        extra={"ground": {"color": hexrgb("#3dff8f"), "falloff": 2.0, "intensity": 0.6, "mode": "subtle",
                          "radius": 400.0},
               "flow": {"field": "", "influence": 0.0}},
        timing={"fadeIn": 1.5})
    s.effect("stars", "stars", ("world",), parameters={"brightness": 1.0, "density": 0.35, "magnitudeSlope": 6.0,
                                                        "colorSpread": 0.5, "twinkle": 0.3, "twinkleRate": 1.5,
                                                        "horizonFade": 0.5, "band": 0.25, "bandTilt": 30.0,
                                                        "daylight": 0.0})

    # ---- the lake (scaled: a procedural is at most 1000 m)
    s.proc("ice", {"kind": "box", "size": [1000.0, 0.4, 1000.0], "subdivisions": 2},
           material={"baseColor": [1, 1, 1], "emissiveColor": [0, 0, 0], "emissiveIntensity": 1.0, "roughness": 0.1,
                     "metallic": 0.0, "program": "atIce"},
           transform={"position": [0.0, -0.2, -400.0], "rotation": [0, 0, 0], "scale": [4.0, 1.0, 4.0]})

    # ---- the pressure ridge: three runs of broken slabs, graded large (near) to small (far)
    slab = {"baseColor": hexrgb("#9ab8cf", 0.8), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
            "roughness": 0.18, "metallic": 0.0}
    runs = [((-9.0, 0.1, -4.0), (6.0, 0.1, -60.0), 26, 1.0, 3), ((6.0, 0.1, -60.0), (40.0, 0.1, -180.0), 30, 0.6, 7),
            ((40.0, 0.1, -180.0), (55.0, 0.1, -420.0), 30, 0.35, 9)]
    for k, (a, b, n, sc, seed) in enumerate(runs):
        s.proc("ridge%d" % k, {"kind": "box", "size": [1.6 * sc, 1.1 * sc, 0.22 * sc], "subdivisions": 1},
               distribution={"kind": "linear", "count": n, "start": list(a), "end": list(b), "orientAlong": True},
               material=slab,
               variation={"seed": seed, "position": [0.9 * sc, 0.25 * sc, 0.9 * sc], "rotation": [0.6, 1.2, 0.5],
                          "scale": [0.5, 0.6, 0.4], "uniformScale": 0.3})

    # ---- the far shore: a band of spruce silhouettes
    tree = {"baseColor": hexrgb("#020304"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0, "roughness": 1.0,
            "metallic": 0.0}
    for k, (z, n, seed) in enumerate(((-760.0, 260, 31), (-900.0, 220, 37))):
        s.proc("spruce%d" % k, spruce(seed),
               distribution={"kind": "linear", "count": n, "start": [-1700.0, 0.0, z], "end": [1700.0, 0.0, z]},
               material=tree,
               variation={"seed": seed, "position": [6.0, 0.0, 70.0], "rotation": [0.04, 3.1, 0.04],
                          "scale": [0.2, 0.55, 0.2], "uniformScale": 0.35})

    # ---- diamond dust: tiny glints hanging in the cold air (hats)
    s.particles("dust", capacity=3000, seed=41, shape="box", position=[CAM[0], 2.5, CAM[2] - 6.0],
                extent=[7.0, 2.0, 5.0], direction=[0.1, -0.05, 0.0], spawnRate=6.0, lifetimeMin=0.3, lifetimeMax=0.9,
                spread=1.0, speedMin=0.02, speedMax=0.1, gravity=[0, -0.02, 0], drag=0.5, turbulence=0.05,
                sizeStart=0.006, sizeEnd=0.0, colorStart=hexrgb("#e9fbff") + [1.0], colorEnd=hexrgb("#9ff7ff") + [0.0],
                emissive=9.0, blend="additive")

    # ---- light: the aurora's own green on the ice is the effect's ground light; a faint navy skylight
    s.light("sky", "directional", direction=[0.15, -1.0, -0.2], color=hexrgb("#3a5a8a"), intensity=0.05,
            castsShadow=False)
    s.light("auroraKey", "directional", direction=[-0.1, -0.35, 0.93], color=hexrgb("#4cffa0"), intensity=0.22,
            castsShadow=False)

    # ---- camera: low on the ice looking up; a slow pan along the shore
    s.track("camera/position", [{"time": 0.0, "value": list(CAM), "interp": "smooth"},
                                {"time": 75.0, "value": [CAM[0] + 1.2, CAM[1], CAM[2] - 0.6], "interp": "smooth"},
                                {"time": 150.0, "value": list(CAM), "interp": "smooth"}], loop=150.0)
    s.track("camera/target", [{"time": 0.0, "value": [-60.0, 80.0, -220.0], "interp": "smooth"},
                              {"time": 75.0, "value": [70.0, 84.0, -215.0], "interp": "smooth"},
                              {"time": 150.0, "value": [-60.0, 80.0, -220.0], "interp": "smooth"}], loop=150.0)
    s.camera = {"mode": 1, "position": list(CAM), "target": [-60.0, 80.0, -220.0], "fov": 60.0, "orbitSpeed": 0.0}

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # sustain: brighter, longer curtains; bass: they reach lower and taller
    s.route(R("sustain", "fx/aurora/intensity", 3.2, **SLOW),
            R("sustain", "fx/aurora/curtainHeight", 900.0, **SLOW),
            R("bass", "fx/aurora/baseHeight", -45.0, attackMs=200, decayMs=1200),
            R("bass", "fx/aurora/curtainHeight", 700.0, attackMs=200, decayMs=1200))
    # chords: tension and polyphony move the palette (green -> teal -> violet -> rose)
    s.map(M("mood", [("tension", 1.0), ("polyphony", 0.4)], "mean"))
    s.route(R("visual.mood", "palette/position", 3.0, attackMs=1600, decayMs=4000))
    # melody: each note sends a ripple through the curtains (its size the velocity). (The aurora's drift and flow are
    # rates -- phase = time x rate -- so a route on them jumps the pattern: they stay keyed, never routed.)
    s.route(R("noteEnv", "fx/aurora/waveAmplitude", 0.55, depth="lastVelocity", attackMs=60, decayMs=900),
            R("noteEnv", "fx/aurora/turbulence", 0.5, attackMs=60, decayMs=700),
            R("noteEnv", "fx/aurora/edgeBrightness", 1.4, attackMs=0, decayMs=400))
    # kick: a surge through the curtains; the ridge glints
    s.route(R("kick", "fx/aurora/intensity", 2.4, attackMs=0, decayMs=420),
            R("kick", "fx/aurora/edgeBrightness", 1.6, attackMs=0, decayMs=500))
    # snare: the ice cracks with light
    for c, w in enumerate(hexrgb("#7fe8ff", 1.0)):
        s.route(R("snare", "material/atIce/op/%d/constant/constant" % OP_CRACK, 7.0 * w, comp=c, attackMs=0,
                  decayMs=320))
    # hat: diamond dust
    s.route(R("hat", "particles/dust/burst", 70.0, attackMs=0, decayMs=40),
            R("hatRate", "particles/dust/spawnRate", 260.0, **MEDIUM),
            R("visual.mood", "post/bloom/intensity", 0.1, **SLOW),
            R("kick", "post/lens/chromaticAberration", 0.006, attackMs=0, decayMs=120))

    s.params_({
        "post/bloom/intensity": 0.32, "post/bloom/threshold": 0.9, "post/output/vignette": 0.38,
        "post/output/grain": 0.014, "post/grade/temperature": -0.06, "post/grade/contrast": 1.04,
        "post/lens/chromaticAberration": 0.002, "post/tonemap/operator": 3,
        "camera/lens/focalLength": 18.0, "camera/exposure/compensation": 0.0,
    })

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region("sky", box=[0.0, 0.0, 1.0, 0.62])
    s.region("ridge", box=[0.0, 0.72, 0.6, 1.0])
    s.region("ice", box=[0.4, 0.78, 1.0, 1.0])
    s.region("shore", box=[0.0, 0.62, 1.0, 0.76])
    return s

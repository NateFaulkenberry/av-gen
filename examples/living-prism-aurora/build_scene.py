#!/usr/bin/env python3
"""The Living Prism (aurora revision): write the scene and the projects from layout.py and the generated meshes.

    python3 examples/living-prism-aurora/build_scene.py --assets build/aurora/assets

Phase 2 (the composition prototype, spec 13): the fixed camera, the valley, the river, the far ranges, the seven
crystal formations, the stars and a rough aurora, with placeholder materials. The scene (scene/) is generated and
gitignored; the projects are committed and are pure functions of the scripts.
"""
import argparse
import math
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import layout as L  # noqa: E402

def prog_ground():
    """Phase 2e (the owner: "looks very CG"): moss and rock broken up at two noise scales, the moss edge frayed by the
    mottling, rock on the slopes and up high, creases darkened by the surface's own concavity. Also shades the scans.
    Phase 2f perf (measured: the 41-op 2e program cost ~6 ms of a 1080p frame; compiled programs cost ~0.09 ms per op
    over the whole frame): 21 ops -- two OKLab ramps replace the constant-and-mix chains, the strata reuse the mottling
    noise, and the scree flecks are gone."""
    return {"name": "au_ground", "baseColor": 3, "metallic": -1, "roughness": 7, "emission": 5,
            "emissionIntensity": 1.0, "opacity": -1, "ops": [
                {"kind": "input", "dst": 0, "input": "worldPosition"},
                {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.012, "seed": 3},          # large patches
                {"kind": "noise", "dst": 2, "srcA": 0, "value": 0.09, "seed": 5},           # mottling
                {"kind": "mix", "dst": 3, "srcA": 1, "srcB": 2, "value": 0.35},
                # 5c: a rich, dark, fertile green -- deep moss, lit moss, a cooler green; only a trace of olive
                {"kind": "oklabRamp", "dst": 3, "srcA": 3, "constant": [0.014, 0.050, 0.030, 1],   # cool green
                 "constant2": [0.008, 0.030, 0.012, 1], "constant3": [0.030, 0.105, 0.032, 1],    # deep, lit moss
                 "constant4": [0.045, 0.080, 0.028, 1]},                                          # moss-olive
                {"kind": "oklabRamp", "dst": 4, "srcA": 2, "constant": [0.016, 0.014, 0.012, 1],   # rock, banded,
                 "constant2": [0.026, 0.023, 0.019, 1], "constant3": [0.045, 0.040, 0.034, 1],    # 5c: grey-brown
                 "constant4": [0.062, 0.055, 0.047, 1]},
                # where moss grows: flat-ish ground (edge frayed by the mottling), low ground, less high up
                {"kind": "input", "dst": 6, "input": "normal"},
                {"kind": "gradient", "dst": 6, "srcA": 6, "constant": [0, 1, 0, 0], "value": 1.0},
                {"kind": "remap", "dst": 5, "srcA": 2, "constant": [0, 1, -0.12, 0.12]},
                {"kind": "add", "dst": 6, "srcA": 6, "srcB": 5},
                {"kind": "smoothstep", "dst": 6, "srcA": 6, "constant": [0.72, 0.9, 0, 0]},
                {"kind": "gradient", "dst": 7, "srcA": 0, "constant": [0, 1, 0, 1.5], "value": -0.012},  # fewer up high
                {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
                {"kind": "gradient", "dst": 7, "srcA": 0, "constant": [0, 1, 0, 1.5], "value": -0.07},   # all moss low
                {"kind": "add", "dst": 6, "srcA": 6, "srcB": 7},
                {"kind": "mixBy", "dst": 3, "srcA": 4, "srcB": 3, "srcC": 6},
                # creases: the surface's concavity darkens them
                {"kind": "curvatureMask", "dst": 5, "constant": [0.0, 1.0, 0, 0], "value": -6.0},
                {"kind": "remap", "dst": 5, "srcA": 5, "constant": [0, 1, 1, 0.35], "value": 1.0},
                {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 5},
                {"kind": "remap", "dst": 7, "srcA": 6, "constant": [0, 1, 0.7, 0.95], "value": 1.0},
                # 5b (spec 6.5): the aurora as light on the valley -- sky-facing ground glows softly in the aurora's
                # colours (the two aurora roles mixed by the large patch noise), palette-bound (GROUND_AURORA_*)
                {"kind": "input", "dst": 4, "input": "normal"},
                {"kind": "gradient", "dst": 4, "srcA": 4, "constant": [0, 1, 0, 0], "value": 1.0},
                {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.78, 1.0, 0, 0]},   # 5c: crests only
                {"kind": "constant", "dst": 5, "constant": [c * GROUND_AURORA for c in R0["auroraLow"]] + [1]},
                {"kind": "constant", "dst": 2, "constant": [c * GROUND_AURORA for c in R0["aurora2Low"]] + [1]},
                {"kind": "smoothstep", "dst": 6, "srcA": 1, "constant": [0.35, 0.65, 0, 0]},
                {"kind": "mixBy", "dst": 5, "srcA": 5, "srcB": 2, "srcC": 6},
                {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 4}]}


FOG_AURORA = 0.15               # 5b: the distance haze in the aurora's top colour (the far river carries it)
# ---- ADR-1642 (terrain texture inputs, preview; not merged yet): AU_TERRAIN_TEX=1 builds the textured ground.
# Written to the preview's schema (program `textures`, ops `textureTriplanar` / `textureTriplanarNormal`) and unverified
# until the merge; off by default, so nothing the current engine reads changes. Poly Haven CC0, 2K, in the gitignored
# assets/nature/textures/ (manifest): rock_face on steep ground, mossy_rock between, forest_leaves_02 on the flats.
TERRAIN_TEX = os.environ.get("AU_TERRAIN_TEX", "0") == "1"
TEX_DIR = os.path.join(HERE, "..", "..", "assets", "nature", "textures")
TERRAIN_TEXTURES = [("rock", "rock_face/rock_face_diff_2k.jpg", "srgb"),
                    ("rockN", "rock_face/rock_face_nor_gl_2k.jpg", "linear"),
                    ("mossy", "mossy_rock/mossy_rock_diff_2k.jpg", "srgb"),
                    ("leaves", "forest_leaves_02/forest_leaves_02_diffuse_2k.jpg", "srgb")]


def prog_ground_textured():
    """The ground with real rock and mossy-rock texture on the slopes and a leafy floor (ADR-1642): three triplanar
    colour reads and one triplanar normal, mixed by slope (the normal's up component) and a large patch noise."""
    return {"name": "au_ground", "baseColor": 3, "metallic": -1, "roughness": -1, "emission": -1, "normal": 7,
            "emissionIntensity": 1.0, "opacity": -1,
            "textures": [{"name": n, "path": os.path.basename(pth), "colorSpace": cs} for n, pth, cs in TERRAIN_TEXTURES],
            "ops": [
                {"kind": "input", "dst": 0, "input": "worldPosition"},
                {"kind": "textureTriplanar", "dst": 3, "srcA": 0, "texture": "rock", "value": 0.08},
                {"kind": "textureTriplanar", "dst": 4, "srcA": 0, "texture": "mossy", "value": 0.1},
                {"kind": "textureTriplanar", "dst": 5, "srcA": 0, "texture": "leaves", "value": 0.25},
                {"kind": "textureTriplanarNormal", "dst": 7, "srcA": 0, "texture": "rockN", "value": 0.08},
                {"kind": "input", "dst": 1, "input": "normal"},
                {"kind": "gradient", "dst": 1, "srcA": 1, "constant": [0, 1, 0, 0], "value": 1.0},
                {"kind": "noise", "dst": 2, "srcA": 0, "value": 0.02, "seed": 3},
                {"kind": "remap", "dst": 2, "srcA": 2, "constant": [0, 1, -0.1, 0.1]},
                {"kind": "add", "dst": 1, "srcA": 1, "srcB": 2},
                {"kind": "smoothstep", "dst": 6, "srcA": 1, "constant": [0.55, 0.72, 0, 0]},   # rock -> mossy rock
                {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 4, "srcC": 6},
                {"kind": "smoothstep", "dst": 6, "srcA": 1, "constant": [0.78, 0.9, 0, 0]},    # -> the leafy floor
                {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 6}]}


GROUND_AURORA = 0.008            # 5c: a soft rim on sky-facing crests only (0.035 washed the moss mauve)
GROUND_AURORA_OPS = (24, 25)     # 1-based op indices of its two colour constants in au_ground (palette targets)


class Prog:
    """A material program with labelled ops, so palette bindings name ops, not indices (from the grotto)."""

    def __init__(self, name, intensity=1.0):
        self.name, self.ops, self.labels, self.intensity = name, [], {}, intensity

    def op(self, kind, dst, label=None, **kw):
        o = {"kind": kind, "dst": dst}
        for k, v in kw.items():
            o[{"a": "srcA", "b": "srcB", "c": "srcC", "f": "value", "k": "constant", "k2": "constant2",
               "k3": "constant3", "k4": "constant4"}.get(k, k)] = v
        if label:
            self.labels[label] = len(self.ops)
        self.ops.append(o)
        return len(self.ops) - 1

    def path(self, label, field="constant"):
        i = self.labels[label]   # op paths are 1-based (material_params.cpp opPath, ADR-232)
        return "material/%s/op/%d/%s/%s" % (self.name, i + 1, self.ops[i]["kind"], field)

    def json(self, base=-1, emission=-1, roughness=-1):
        return {"name": self.name, "baseColor": base, "metallic": -1, "roughness": roughness, "emission": emission,
                "emissionIntensity": self.intensity, "opacity": -1, "ops": self.ops}


def c4(rgb, a=1.0):
    return [round(v, 4) for v in rgb] + [a]


# ---- the aurora palette (spec 5.4, 6.3): the shared representation of the aurora's colour --------------------------
# Every system that should answer the aurora reads these roles through palette bindings (palette_json): both aurora
# effects, the crystals' environment, interior, edge and fracture colours, the aurora fill lights, the water's
# reflection tints. One state change moves the whole world; the states are the looks.
ROLES = ["auroraLow", "auroraMid", "auroraTop", "aurora2Low", "aurora2Mid", "aurora2Top", "crystalDeep", "crystalBody",
         "crystalFracture", "crystalBlue"]
LOOKS = [
    ("Green Veil", {"auroraLow": [0.10, 0.95, 0.55], "auroraMid": [0.12, 0.70, 0.75], "auroraTop": [0.55, 0.30, 0.95],
                    "aurora2Low": [0.95, 0.35, 0.75], "aurora2Mid": [0.65, 0.35, 0.95], "aurora2Top": [0.30, 0.25, 0.85],
                    "crystalDeep": [0.06, 0.03, 0.22], "crystalBody": [0.95, 0.22, 0.62],
                    "crystalFracture": [0.72, 1.0, 0.94], "crystalBlue": [0.22, 0.45, 1.0]}),
    ("Rose Veil", {"auroraLow": [0.98, 0.25, 0.62], "auroraMid": [0.85, 0.30, 0.90], "auroraTop": [0.42, 0.22, 0.95],
                   "aurora2Low": [0.20, 0.90, 0.70], "aurora2Mid": [0.22, 0.62, 0.92], "aurora2Top": [0.30, 0.28, 0.88],
                   "crystalDeep": [0.14, 0.02, 0.16], "crystalBody": [1.0, 0.25, 0.55],
                   "crystalFracture": [1.0, 0.78, 0.95], "crystalBlue": [0.25, 0.75, 0.95]}),
]
LOOK = int(os.environ.get("AU_LOOK", "0"))
R0 = dict(LOOKS[LOOK][1])


EDGE_WIDEN = {}   # formation -> its edge prisms' widening (from <assets>/edges.json, written by Blender)
IG = 1.8   # the interior's gain, carried in its ramp stops and their palette bindings (5e)


def crystal_program():
    """Phase 3, reworked in 3c against the reference: facets fall into three classes by a per-facet random (the
    quantised normal), so neighbouring faces differ strongly in value and colour:
      - DARK facets (about a third): near-black, the interior at 5%;
      - MIRROR facets (about a quarter): a strong reflection of the aurora colour they face and a low roughness, so the
        lights give sharp highlights; little interior;
      - NEBULA facets (the rest): the interior -- a steep, high-contrast object-space noise through a navy, pink, blue,
        cyan OKLab ramp, so it breaks into vivid patches rather than an even wash; shifted by the view for depth.
    Over all of them: the environment's Fresnel rim, a network of thin bright fractures (object-space Voronoi
    edges, branching across facets, in runs over about half the stone) and rare inclusions. Colours from the aurora palette roles.
    The glowing edges are geometry (build_edges); the body (baseColor) is near-black."""
    p = Prog("au_crystal", 1.0)
    p.op("input", 0, input="normal")
    p.op("input", 1, input="viewDirection")
    p.op("fresnel", 2, a=0, b=1, f=3.0)                                       # r2: grazing
    p.op("quantize", 3, a=0, f=3.0, k=[0.31, 0.47, 0.23, 0], seed=5)
    p.op("swizzle", 3, a=3, k=[3, 3, 3, 3])                                   # r3: the facet's random
    p.op("gradient", 4, a=0, k=[0, 1, 0, 0.3], f=0.6)                         # side 0.3 (low/mid), up 0.9 (top)
    p.op("remap", 5, a=3, k=[0, 1, -0.3, 0.3])
    p.op("add", 4, a=4, b=5)
    p.op("oklabRamp", 4, a=4, label="env", k=c4(R0["auroraLow"]), k2=c4(R0["auroraMid"]),
         k3=c4(R0["aurora2Low"]), k4=c4(R0["auroraTop"]))                     # r4: the environment colour
    p.op("smoothstep", 6, a=3, k=[0.72, 0.8, 0, 0])                          # r6: the mirror facets
    p.op("remap", 7, a=2, k=[0, 1, 0.03, 0.3])                                # Fresnel rim everywhere
    p.op("remap", 5, a=6, k=[0, 1, 0, 0.9])                                   # + the mirrors' full reflection
    p.op("add", 7, a=7, b=5)
    p.op("multiply", 5, a=4, b=7)                                             # r5: the emission so far
    p.op("input", 7, input="localPosition")                                    # the nebula interior
    p.op("noise", 7, a=7, f=0.07, seed=29)
    p.op("remap", 7, a=7, k=[0, 1, -0.9, 1.9])                                # steep: patches, not a wash
    p.op("remap", 1, a=2, k=[0, 1, 0, 0.35])                                  # the view shifts it: depth
    p.op("add", 7, a=7, b=1)
    p.op("oklabRamp", 7, a=7, label="interior", k=c4([c * IG for c in R0["crystalDeep"]]),
         k2=c4([c * IG for c in R0["crystalBody"]]), k3=c4([c * IG for c in R0["crystalBlue"]]),
         k4=c4([c * IG for c in R0["auroraMid"]]))   # (the interior gain IG is in the stops: 5e freed its ops)
    p.op("remap", 1, a=2, k=[0, 1, 1, 0.25], f=1.0)                           # seen where the facet faces us
    p.op("multiply", 7, a=7, b=1)
    p.op("smoothstep", 1, a=3, k=[0.26, 0.34, 0, 0])                          # the dark facets: 5%
    p.op("remap", 1, a=1, k=[0, 1, 0.05, 1.0])
    p.op("multiply", 7, a=7, b=1)
    p.op("remap", 1, a=6, k=[0, 1, 1, 0.2], f=1.0)                            # the mirrors: little interior
    p.op("multiply", 7, a=7, b=1)
    p.op("add", 5, a=5, b=7)
    p.op("input", 7, input="localPosition")                                    # fractures: dense, thin, branching
    p.op("voronoiEdge", 1, a=7, f=0.16, seed=13)
    p.op("swizzle", 1, a=1, k=[0, 0, 0, 0])            # F2 - F1 into every channel (the others tinted it red)
    # 5e anti-aliasing (the owner: "why do all the crystals look aliased and jagged lines"): the line's width is the
    # authored 0.012 (Voronoi units) or ~1.5 px, whichever is wider (footprint = world units per pixel; x 0.16 / ~2.5
    # scale x 1.5 px ~ 0.1); the edge is a smoothstep over that width, and the line's energy falls as authored/actual,
    # so cracks thin out and fade on distant crystals instead of breaking into stairs
    p.op("input", 6, input="footprint")
    p.op("remap", 6, a=6, k=[0, 1, 0.012, 0.112])                             # width = 0.012 + 0.1 x footprint
    p.op("power", 6, a=6, f=-1.0)                                             # 1 / width
    p.op("multiply", 1, a=1, b=6)
    p.op("smoothstep", 1, a=1, k=[0.0, 1.0, 0, 0])
    p.op("remap", 1, a=1, k=[0, 1, 1, 0], f=1.0)
    p.op("remap", 6, a=6, k=[0, 1, 0, 0.012])                                 # energy: authored / actual width
    p.op("multiply", 1, a=1, b=6)
    p.op("noise", 7, a=7, f=0.05, seed=17)
    p.op("smoothstep", 7, a=7, k=[0.46, 0.54, 0, 0])                         # in runs over about half the stone
    p.op("multiply", 1, a=1, b=7)
    p.op("constant", 7, label="fracture", k=c4([c * 1.4 for c in R0["crystalFracture"]]))
    p.op("multiply", 1, a=1, b=7)
    p.op("add", 5, a=5, b=1)
    # (5e: the inclusions -- hard-thresholded hash cells -- are gone: unfilterable squares, a source of the aliasing)
    p.op("remap", 6, a=6, k=[0, 1, 0.3, 0.025], f=1.0)                        # roughness: the mirrors glossy
    assert len(p.ops) <= 48, len(p.ops)
    return p


XTAL_P = crystal_program()
# the heroes refract (spec 5.3). 17 (offline): every hero formation, stronger distortion, deep coloured absorption (green
# goes first: the stone transmits violet-pink, as the reference's does), less of the surface over the transmission.
# AU_REFRACT=0 builds without. Distant formations stay opaque: a refractive crystal behind another refractive one is
# missing from the single-layer copy (17: an engine change).
REFRACTION = os.environ.get("AU_REFRACT", "1") == "1"
_RF = {"strength": 0.12, "opacity": 0.3, "thickness": 4.0, "absorption": [0.15, 0.6, 0.05],
       "dispersion": 0.2}   # 5d: chromatic dispersion (ADR-1640; 0.1-0.3 at this strength, >0.5 splits thin lines)
REFRACT_HEROES = {n: dict(_RF) for n in ("L1", "L1b", "L2", "L3", "R1", "R2", "R3", "R4")}


def xmat(name):
    m = {"baseColor": [0.012, 0.008, 0.02], "roughness": 0.06, "metallic": 0.0, "program": "au_crystal"}
    if name in REFRACT_HEROES:
        m["thinFilm"] = {"thickness": 420.0, "ior": 1.45}
        if REFRACTION:
            m["refraction"] = dict(REFRACT_HEROES[name])
    return m


def palette_json():
    P = XTAL_P
    b = [{"role": "auroraLow", "target": "fx/auroraMain/lowColor"},
         {"role": "auroraMid", "target": "fx/auroraMain/midColor"},
         {"role": "auroraTop", "target": "fx/auroraMain/topColor"},
         {"role": "aurora2Low", "target": "fx/auroraPink/lowColor"},
         {"role": "aurora2Mid", "target": "fx/auroraPink/midColor"},
         {"role": "aurora2Top", "target": "fx/auroraPink/topColor"},
         {"role": "auroraLow", "target": "lights/auroraFill/color"},
         {"role": "auroraTop", "target": "lights/skyFill/color", "gain": 0.5},   # 5c: half (it washed the moss mauve)
         # the water's own body takes a trace of the aurora (its reflection of the sky is the live aurora already)
         {"role": "auroraMid", "target": "nodes/valley/water/shallowColor", "gain": 0.012},
         {"role": "auroraTop", "target": "nodes/valley/water/deepColor", "gain": 0.004},
         {"role": "auroraLow", "target": P.path("env", "constant")},
         {"role": "auroraMid", "target": P.path("env", "constant2")},
         {"role": "aurora2Low", "target": P.path("env", "constant3")},
         {"role": "auroraTop", "target": P.path("env", "constant4")},
         {"role": "crystalDeep", "target": P.path("interior", "constant"), "gain": IG},
         {"role": "crystalBody", "target": P.path("interior", "constant2"), "gain": IG},
         {"role": "crystalBlue", "target": P.path("interior", "constant3"), "gain": IG},
         {"role": "auroraMid", "target": P.path("interior", "constant4"), "gain": IG},
         {"role": "crystalFracture", "target": P.path("fracture"), "gain": 1.4},
         # 5b (spec 6.5): the aurora lights the valley -- the ground's sky-facing glow, the distance haze, the hero
         # crystals' faint light on the ground near them
         {"role": "auroraLow", "target": "material/au_ground/op/%d/constant/constant" % GROUND_AURORA_OPS[0],
          "gain": GROUND_AURORA},
         {"role": "aurora2Low", "target": "material/au_ground/op/%d/constant/constant" % GROUND_AURORA_OPS[1],
          "gain": GROUND_AURORA},
         {"role": "auroraTop", "target": "scene/fogColor", "gain": FOG_AURORA}]
    if CIRCUITS:
        b.append({"role": "auroraLow", "target": "procedural/circuits/material/emissiveColor"})
    for i, n in enumerate(sorted(REFRACT_HEROES)):
        b.append({"role": "crystalBody" if i % 2 == 0 else "auroraLow", "target": "lights/glow%s/color" % n})
    for f in L.FORMATIONS:   # the glowing edges, in the low aurora colour
        b.append({"role": "auroraLow", "target": "procedural/edgeA_%s/material/emissiveColor" % f[0]})
        b.append({"role": "auroraMid", "target": "procedural/stars_%s/material/emissiveColor" % f[0]})
    states = [{"name": n, "colors": {r: roles[r] for r in ROLES}} for n, roles in LOOKS]
    return {"states": states, "bindings": b, "selector": {"select": LOOK, "transition": 6.0, "lock": False}}


WATER = None   # built per look in water() (Phase 5): its reflection tints are not palette-bindable


def water():
    """Phase 5 (spec 7): the river as a long exposure. A nearly still surface (no chop, faint ripples), so the aurora
    no longer breaks into hundreds of small glints; a broad flow-aligned swell at a long wavelength, which stretches
    the reflection into bands along the river's course; reflections spread along the view (streak) and filtered by
    many rays and by calm and rough regions -- the aurora and the crystals reflect as soft vertical smears; broad
    patches of the second aurora colour over the first. Dark body, dark bed. Offline, so the SSR is long and wide."""
    return {
        "shallowColor": [0.004, 0.006, 0.012], "deepColor": [0.001, 0.002, 0.006], "shallow": 1.0, "clarity": 0.8,
        "edgeFade": 0.5, "maxOpacity": 0.98, "fresnel": 0.3, "reflection": 0.9,
        "reflectionTint": [0.55 + 0.45 * c for c in R0["auroraTop"]],
        "specular": 0.6, "roughness": 0.12, "ripple": 0.05, "rippleScale": 0.15, "rippleSpeed": 0.3, "chop": 0.0,
        "foam": 0.0, "refraction": 0.05, "sparkle": 0.0,
        # luminous flowing bands: the under-surface glow's patches drift with the current, so they follow the river;
        # large and half the channel, in the second aurora colour
        "glow": 1.4, "glowColor": [0.3 + 0.7 * c for c in R0["aurora2Low"]], "glowScale": 0.025,
        "glowCoverage": 0.5, "glowDepth": 0.2,   # (smaller patches, 0.05, read as blotchy camouflage)
        "ssr": 0.5, "ssrDistance": 1500.0, "ssrSteps": 96.0, "ssrGain": 1.3, "streak": 1.6, "reflectionRays": 10.0,
        "transmission": 0.6, "absorption": [2.0, 1.5, 1.0], "undulation": 0.8, "undulationScale": 0.02,
        "roughnessVariation": 0.6, "tintPatches": 1.0, "reflectionTint2": [0.3 + 0.7 * c for c in R0["aurora2Low"]],
        "tintPatchScale": 0.03, "glints": 2.0,
        # (no tears: their seams drew as dashed lines down the river, like road markings)
        }




def aurora(name, low, mid, top, height, base, radius, count, intensity, complexity, flow, waves=6.5, **kw):
    return {"id": name, "name": name, "type": "aurora", "owner": {"kind": "world"}, "enabled": True, "order": 0,
            "activation": "always", "style": "",
            "timing": {"delay": 0.0, "fadeIn": 0.0, "fadeOut": 0.0, "lifetime": 0.0, "repeatSeconds": 0.0,
                       "windowSeconds": 6.0, "windowStart": 0.0},
            "parameters": {
                "appearance": {"edgeBrightness": 3.2, "emission": 0.25, "filaments": 2.0, "horizonGlow": 0.0,
                               "intensity": intensity, "lowColor": low, "midColor": mid, "topColor": top,
                               "opacity": 0.95, "sparkle": 0.15},
                "audio": {"bass": 0.0, "beat": 0.0, "glints": 0.0, "high": 0.0, "lowMid": 0.0, "mid": 0.0,
                          "sensitivity": 0.0, "spectrumShape": 0.0},
                "rainbow": {"brightness": 1.0, "enabled": False, "hueOffset": 0.0, "saturation": 0.85, "scale": 1.1,
                            "speed": 0.0},
                "shape": {"anchor": "camera", "anchorPosition": [0.0, 0.0, 0.0], "baseHeight": base,
                          "complexity": complexity, "curtainCount": count, "curtainHeight": height,
                          "driftSpeed": 0.05, "flowSpeed": flow, "layerSpacing": 0.35, "radius": radius,
                          "turbulence": 0.35, "verticalSpeed": 0.03, "waveAmplitude": 0.6, "waveScale": waves}},
            "ground": {"color": low, "falloff": 2.0, "intensity": 0.15, "mode": "subtle", "radius": 600.0},
            "flow": {"field": "", "influence": 0.0}}


def aurora_fx(name, low, mid, top, height, base, radius, count, intensity, complexity, flow, waves, **kw):
    """aurora() with any appearance/shape field overridden by keyword (Phase 4)."""
    e = aurora(name, low, mid, top, height, base, radius, count, intensity, complexity, flow, waves)
    for k, v in kw.items():
        group = "appearance" if k in e["parameters"]["appearance"] else "shape"
        e["parameters"][group][k] = v
    return e


def mesh_node(name, asset, points, material, shadow=True):
    pr = {"source": {"kind": "mesh", "asset": asset}, "distribution": {"kind": "points", "points": points},
          "material": dict(material)}
    if not shadow:
        pr["castsShadow"] = False
    return {"name": name, "kind": "procedural", "procedural": pr}


def write_glb(path, positions, normals, indices):
    """A minimal GLB (one mesh, positions + normals + uint32 indices), for generated geometry."""
    import struct
    pb = b"".join(struct.pack("<3f", *p) for p in positions)
    nb = b"".join(struct.pack("<3f", *n) for n in normals)
    ib = b"".join(struct.pack("<I", i) for i in indices)
    binb = pb + nb + ib
    binb += b"\0" * (-len(binb) % 4)
    mins = [min(p[k] for p in positions) for k in range(3)]
    maxs = [max(p[k] for p in positions) for k in range(3)]
    gl = {"asset": {"version": "2.0", "generator": "living-prism-aurora build_scene"},
          "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
          "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2}]}],
          "buffers": [{"byteLength": len(binb)}],
          "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(pb), "target": 34962},
                          {"buffer": 0, "byteOffset": len(pb), "byteLength": len(nb), "target": 34962},
                          {"buffer": 0, "byteOffset": len(pb) + len(nb), "byteLength": len(ib), "target": 34963}],
          "accessors": [{"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3",
                         "min": mins, "max": maxs},
                        {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
                        {"bufferView": 2, "componentType": 5125, "count": len(indices), "type": "SCALAR"}]}
    js = json.dumps(gl).encode()
    js += b" " * (-len(js) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binb)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binb), 0x004E4942) + binb)


CIRCUITS = os.environ.get("AU_CIRCUITS", "0") == "1"   # 5c: off (the owner: "lets nix the circuitry lines for now")


def build_circuits(path, placed, seed=2026102320):
    """5b (spec 8): a restrained network of luminous pathways on the near banks, following the river. Each trace runs
    along the river at a set offset from its centreline, jogs sideways at a few points (the circuit idiom) and may
    branch once; it ends in a small node disk. Draped on the engine's ground every 2 m and lifted 0.25 m, kept off
    wet ground and out of crystal footprints. One emissive GLB; the colour is the palette's (auroraLow)."""
    import random
    rng = random.Random(seed)
    traces = []
    for side in (-1, 1):
        for k in range(7):
            z0 = rng.uniform(110.0, -40.0)
            length = rng.uniform(60.0, 160.0)
            off = rng.uniform(70.0, 140.0) + k * 3.0
            pts, z = [], z0
            while z > z0 - length:
                pts.append((L.river_x(z) + side * off, z))
                z -= 2.0
                if rng.random() < 0.03:                         # a jog: a short diagonal step outward
                    d = rng.choice([1, -1]) * rng.uniform(4.0, 9.0)
                    for t in range(1, 5):
                        pts.append((L.river_x(z) + side * (off + d * t / 4), z - t * 1.5))
                    off += d
                    z -= 6.0
            traces.append(pts)
            if rng.random() < 0.5 and len(pts) > 20:            # one branch, out at 45 degrees then along
                i = rng.randrange(5, len(pts) - 10)
                bx, bz = pts[i]
                br = [(bx + side * t * 1.5, bz - t * 1.5) for t in range(1, 9)]
                br += [(br[-1][0], br[-1][1] - t * 2.0) for t in range(1, 10)]
                traces.append(br)
    flat = [p for tr in traces for p in tr]
    hs = L.engine_heights(flat)

    def wet(x, z, h):
        lvl = L.water_level(x, z)
        return lvl is not None and h <= lvl + 0.4

    def in_crystal(x, z):
        return any(min(math.hypot(x - gx, z - gz) for gx, gz in ground) < 4.0 for _f, _p, _q, ground in placed)
    P, N, I = [], [], []
    k = 0
    w = 0.35
    for tr in traces:
        seg = []
        for (x, z) in tr:
            h = hs[k]
            k += 1
            if wet(x, z, h) or in_crystal(x, z):
                seg.append(None)
            else:
                seg.append((x, h + 0.25, z))
        runs, cur = [], []
        for q in seg:
            if q is None:
                if len(cur) > 4:
                    runs.append(cur)
                cur = []
            else:
                cur.append(q)
        if len(cur) > 4:
            runs.append(cur)
        for run in runs:
            for a, b in zip(run, run[1:]):
                dx, dz = b[0] - a[0], b[2] - a[2]
                n = math.hypot(dx, dz) or 1.0
                px, pz = -dz / n * w, dx / n * w
                base = len(P)
                P += [(a[0] + px, a[1], a[2] + pz), (a[0] - px, a[1], a[2] - pz),
                      (b[0] + px, b[1], b[2] + pz), (b[0] - px, b[1], b[2] - pz)]
                N += [(0.0, 1.0, 0.0)] * 4
                I += [base, base + 2, base + 1, base + 1, base + 2, base + 3]
            ex, ey, ez = run[-1]                                 # the node at the end: a small disk
            base = len(P)
            P.append((ex, ey + 0.02, ez))
            N.append((0.0, 1.0, 0.0))
            for t in range(10):
                a = math.tau * t / 10
                P.append((ex + 1.3 * math.cos(a), ey + 0.02, ez + 1.3 * math.sin(a)))
                N.append((0.0, 1.0, 0.0))
            for t in range(10):
                I += [base, base + 1 + (t + 1) % 10, base + 1 + t]
    write_glb(path, P, N, I)
    print("circuits: %d traces, %d triangles" % (len(traces), len(I) // 3))


def crystal_lights(placed):
    """5b (spec 6.5): each hero formation casts a faint light in its own colour onto the ground near it."""
    out = []
    for i, (f, pos, _q, _g) in enumerate(p for p in placed if p[0][0] in REFRACT_HEROES):
        col = R0["crystalBody"] if sorted(REFRACT_HEROES).index(f[0]) % 2 == 0 else R0["auroraLow"]
        out.append({"name": "glow" + f[0], "id": "glow" + f[0], "type": "point",
                    "position": [pos[0], pos[1] + 12.0 * f[4], pos[2]], "color": col, "intensity": 700.0,
                    "range": 90.0, "radius": 6.0, "castsShadow": False, "volumetric": 0.0})
    return out


def scene_json(placed, scans, bushes=None):
    nodes = [{"name": "valley", "kind": "terrain", "position": [0, 0, 0], "world": L.world_json(),
              "terrain": {"chunkSize": 200.0, "resolution": 64, "lodLevels": 3, "lodDistance": 200.0,
                          "viewDistance": 6000.0, "shadowDistance": 300.0, "skirtDepth": 3.0, "groundMottle": False,
                          "water": water()},
              "material": {"baseColor": [0.02, 0.05, 0.02], "roughness": 0.85, "metallic": 0.0,
                           "program": "au_ground"}}]
    for f, pos, q, _ground in placed:
        s = f[4]
        pt = [round(c, 3) for c in pos] + [round(c, 6) for c in q] + [s, s, s]
        nodes.append(mesh_node("xtal_" + f[0], f[5] + ".glb", [pt], xmat(f[0])))
        # the glowing edges: silhouettes and a few ridges (3c). 5e: one mesh per formation, widened to a minimum
        # on-screen width; its glow falls by the same ratio (EDGE_WIDEN from Blender), so a widened line is not brighter
        widen = EDGE_WIDEN.get(f[0], 1.0)
        # 5f: the speckles (stars) under the visible faces, palette-bound like the old inclusions; dimmed by the
        # square of their widening, so a distant star is not a brighter blob
        nodes.append(mesh_node("stars_" + f[0], "%s_stars.glb" % f[0], [pt],
                               {"baseColor": [0, 0, 0], "emissiveColor": R0["auroraMid"],
                                "emissiveIntensity": 3.0 / (widen * widen), "roughness": 1.0, "metallic": 0.0},
                               shadow=False))
        for key, k in (("edgeA", 2.6 / widen),):
            nodes.append(mesh_node("%s_%s" % (key, f[0]), "%s_%s.glb" % (f[0], key), [pt],
                                   {"baseColor": [0, 0, 0], "emissiveColor": R0["auroraLow"], "emissiveIntensity": k,
                                    "roughness": 1.0, "metallic": 0.0}, shadow=False))
    # the Poly Haven scans (phase 2e): cliffs on the rocky left, outcrops on the right, boulders on both banks
    # 17 (offline): the scans carry their own Poly Haven CC0 PBR textures; the material only tints them into the night
    # (a cool, dark lavender) -- no program, so the scanned albedo, normal and roughness detail reads. Shadows on.
    tmat = {"baseColor": [1.1, 1.02, 1.15], "roughness": 1.0, "metallic": 0.0}   # 5f: lighter again: rock must read
    for asset, pts in scans.items():
        nodes.append(mesh_node("scan_" + asset, asset + ".glb", pts, tmat, shadow=True))
    # 5e (the owner: "nix the moss idea go with more of a bushy look"): bush masses on the owner's areas and around
    # every crystal base, shaded by au_bush
    for asset, pts in (bushes or {}).items():
        if pts:   # 5f: the plants' own textures, tinted toward the reference's dark green
            nodes.append(mesh_node("bush_" + asset, asset + ".glb", pts,
                                   {"baseColor": [0.9, 1.15, 0.8], "roughness": 1.0, "metallic": 0.0},
                                   shadow=True))
    # 5b (spec 8): the circuit pathways on the near banks, emissive in the low aurora colour (palette-bound).
    # 5c: off unless AU_CIRCUITS=1
    if CIRCUITS:
        nodes.append(mesh_node("circuits", "circuits.glb", [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]],
                               {"baseColor": [0, 0, 0], "emissiveColor": R0["auroraLow"], "emissiveIntensity": 1.6,
                                "roughness": 1.0, "metallic": 0.0}, shadow=False))
    # the far mountains: three ridges in place, receding in value through the fog (phase 2b)
    rmat = {"baseColor": [0.018, 0.020, 0.030], "roughness": 0.9, "metallic": 0.0}
    for r in ("ridgeNear", "ridgeMid", "ridgeFar"):
        nodes.append(mesh_node(r, r + ".glb", [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]], rmat, shadow=False))
    p0, t0 = L.CAMERA_POS, L.CAMERA_TARGET
    return {
        "format": "avgen-scene", "version": 1, "name": "The Living Prism: Aurora Valley",
        "_note": "Generated by examples/living-prism-aurora/build_scene.py from layout.py. Do not edit.",
        "camera": {"mode": 1, "position": list(p0), "target": list(t0), "fov": L.CAMERA_FOV, "orbitSpeed": 0,
                   "near": 0.5, "far": L.FAR},
        "environment": {
            # the air: a light peach-lavender extinction, so the ridges recede in value toward the horizon glow
            "intensity": 0.25, "lightFromEnvironment": False, "fogColor": [c * FOG_AURORA for c in R0["auroraTop"]],
            "volumeDensity": 0.0006, "volumeAbsorption": 1.0, "volumeScattering": 0.0, "volumeSteps": 4,
            "volumeMaxDistance": 40.0, "volumeLocalLights": 0.0, "horizonDensity": 0.4,
            "fogHeight": 30.0, "fogHeightFalloff": 0.018, "fogUpperDensity": 0.05,
            "fogSky": 0.0, "fogSkyDistance": 4500.0,
            "shadowCascades": 2, "shadowRange": 600.0, "ambientOcclusion": True, "depthPrepass": True,
            "sky": {"enabled": True, "background": True, "useKeyLight": False,
                    "zenithColor": [0.006, 0.010, 0.030], "horizonColor": [0.42, 0.20, 0.18],
                    "groundColor": [0.01, 0.012, 0.02], "sunColor": [0.6, 0.6, 0.9], "sunDirection": [0, -1, 0],
                    "haze": 0.05, "sunIntensity": 0.0, "sunSize": 0.01, "sunGlow": 0.0, "intensity": 1.0}},
        "lights": [
            # the aurora's light on the valley: a broad mint fill from the sky ahead, and a dim violet from above
            {"name": "auroraFill", "id": "auroraFill", "type": "directional", "role": "key",
             "direction": [0.1, -0.45, 0.89], "color": R0["auroraLow"], "intensity": 0.7, "castsShadow": True},
            # phase 2e: the violet fill rakes low across the valley from the right, so the left wall's ledges, spurs
            # and gullies model in light and shade instead of reading as smooth domes
            {"name": "skyFill", "id": "skyFill", "type": "directional", "direction": [-0.85, -0.38, -0.36],
             "color": [0.5 * c + 0.5 * m for c, m in zip(R0["auroraTop"], [0.35, 0.75, 0.45])], "intensity": 0.16,
             "castsShadow": False}] + crystal_lights(placed),
        "effects": [
            {"id": "stars", "name": "stars", "type": "stars", "owner": {"kind": "world"}, "enabled": True, "order": 0,
             "activation": "always", "parameters": {"brightness": 1.6, "density": 0.55, "magnitudeSlope": 6.5,
                                                    "colorSpread": 0.3, "twinkle": 0.2, "twinkleRate": 1.0,
                                                    "horizonFade": 0.6, "band": 0.15, "bandTilt": 30.0,
                                                    "daylight": 0.0}},
            # two ribbon bands with dark, starry sky between and above them (phase 2b): short curtains, undulating tops
            # Phase 4 (spec 6): two layered curtain systems that sweep the sky. Both rise from below the ridgeline (the
            # effect's base is a straight line, so the mountains hide it) and run out of the top of the frame (no rim);
            # the green-teal main curtain has deep folds and fine rays, the pink-violet layer stands further out with
            # its own wave scale, so pink and green regions interleave by azimuth with dark sky between.
            aurora_fx("auroraMain", R0["auroraLow"], R0["auroraMid"], R0["auroraTop"], 1300.0, 120.0, 2600.0, 2.0,
                      2.2, 90.0, 0.02, 6.0, waveAmplitude=1.2, turbulence=0.45, filaments=3.0, edgeBrightness=3.6,
                      opacity=0.75, layerSpacing=0.5),
            aurora_fx("auroraPink", R0["aurora2Low"], R0["aurora2Mid"], R0["aurora2Top"], 2000.0, 60.0, 3300.0, 2.0,
                      1.6, 70.0, 0.015, 3.2, waveAmplitude=1.1, turbulence=0.4, filaments=2.5, edgeBrightness=3.0,
                      opacity=0.6, layerSpacing=0.45)],
        "materialPrograms": [prog_ground_textured() if TERRAIN_TEX else prog_ground(),
                             XTAL_P.json(base=-1, emission=5, roughness=6)],
        "nodes": nodes,
    }


def project_json(scene_rel, offline=False, root=""):
    p = {"format": "avgen-project", "version": 4,
         "app": {"name": "The Living Prism: Aurora Valley" + (" (offline)" if offline else "")},
         "assets": {"scene": {"kind": "composition", "path": scene_rel}},
         "sonic": {"live": not offline},
         "parameters": {"camera/exposure/compensation": 0.0, "post/bloom/enabled": True, "post/bloom/threshold": 1.4,
                        "post/bloom/intensity": 0.3, "post/bloom/radius": 0.8, "post/tonemap/chroma-retention": 0.8,
                        "post/output/vignette": 0.3, "post/grade/saturation": 1.0},
         "sources": [], "routes": [], "palette": palette_json(),
         "render": {"width": 1920, "height": 1080, "fps": 30}}
    if offline:
        p["assets"]["audio"] = {"path": os.path.join(root, "assets/audio/feline-footwear.wav")}
        # each stem says what it is (ADR-1630): impacts will come from role.drums.* / signal.kick (Phase 7)
        p["assets"]["stems"] = [{"key": k, "label": k.title(), "file": os.path.join(
            root, "assets/audio/stems/feline-footwear (%s).wav" % k.title()), "role": k}
            for k in ("drums", "bass", "synth")]
    return p


# ---- phase 2e: the scans. Cliffs: (asset, z, side, setback from the river centre m, yaw deg, scale, sink fraction)
CLIFFS = [("cliffD", 30.0, -1, 150.0, 80.0, 2.4, 0.3), ("cliffA", -70.0, -1, 135.0, 95.0, 3.6, 0.3),
          ("cliffB", 95.0, -1, 120.0, 75.0, 6.0, 0.3), ("cliffA", 40.0, -1, 215.0, 100.0, 4.0, 0.3),
          ("cliffC", -20.0, -1, 175.0, 40.0, 9.0, 0.35), ("cliffB", -250.0, -1, 185.0, 90.0, 6.0, 0.3),
          ("cliffB", -190.0, -1, 120.0, 70.0, 5.5, 0.3), ("cliffA", -300.0, -1, 120.0, 110.0, 3.0, 0.3),
          ("cliffD", -430.0, -1, 125.0, 85.0, 2.0, 0.3), ("cliffC", -110.0, -1, 92.0, 60.0, 7.0, 0.35),
          ("cliffB", -150.0, 1, 165.0, -80.0, 3.2, 0.35), ("cliffC", 10.0, 1, 125.0, -100.0, 5.0, 0.4)]
SCATTER = {  # asset: (count left, count right, scale lo, hi)
    "boulder": (30, 14, 2.0, 8.0), "mossrock": (6, 6, 0.35, 0.9), "rock": (16, 6, 6.0, 16.0),
    "stone": (14, 6, 10.0, 24.0)}


def auto_cliffs(sizes, placed, footprints, count=30, seed=2026102317):
    """17 (richer rock): textured scan cliffs where the camera sees bare canyon wall. Candidates on both walls (every
    35 m down the valley, 150-300 m from the river) are projected through the camera; one is kept only if its
    on-screen box is in frame, clear of every crystal's outline, and its top is visible past the terrain (a ray
    against the engine's heights). Then up to `count`, greedily, none overlapping another by more than a fifth.
    Returns CLIFFS-style rows (asset, z, side, setback, yaw, scale, sink)."""
    import random
    rng = random.Random(seed)
    hulls = []
    for f, pos, q, _g in placed:
        sc = f[4]
        pts = [L._project([pos[i] + v[i] for i in range(3)])
               for v in (L._qrot(q, (sc * a[0], sc * a[1], sc * a[2])) for a in footprints[f[5]])]
        hulls.append((L._hull([(round(x), round(y)) for x, y, _z in pts]), min(z for _x, _y, z in pts)))
    cands = []
    assets = ["cliffA", "cliffB", "cliffD"]
    # 5e (the owner: "start working on making more of the original cliffs look like rocky cliffs"): the walls behind
    # the crystals take rock faces too, larger, closer and further along
    for z in range(60, -900, -30):
        for side in (-1, 1):
            for sb in (120, 150, 190, 230, 280, 330):
                a = rng.choice(assets)
                w, h, d = sizes[a]
                sc = rng.uniform(120.0, 220.0) / w
                yaw = (90.0 if side < 0 else -90.0) + rng.uniform(-25, 25)
                x = L.river_x(z) + side * sb
                cands.append((a, z, side, sb, yaw, sc, x))
    hs = L.engine_heights([(c[6], c[1]) for c in cands])
    boxes = []
    for c, gy in zip(cands, hs):
        a, z, side, sb, yaw, sc, x = c
        w, h, d = (v * sc for v in sizes[a])
        cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        corners = [(x + cy * u + sy * v, gy + hh, z - sy * u + cy * v)
                   for u in (-w / 2, w / 2) for v in (-d / 2, d / 2) for hh in (0.0, h * 0.7)]
        pr = [L._project(p) for p in corners]
        if min(p[2] for p in pr) <= 1:
            continue
        x0, x1 = min(p[0] for p in pr), max(p[0] for p in pr)
        y0, y1 = min(p[1] for p in pr), max(p[1] for p in pr)
        cx, cyy = (x0 + x1) / 2, (y0 + y1) / 2
        if not (60 < cx < 1860 and 120 < cyy < 900) or (x1 - x0) < 40:
            continue
        probe = [(cx, cyy), (x0 + (x1 - x0) * 0.25, cyy), (x0 + (x1 - x0) * 0.75, cyy), (cx, y0 + (y1 - y0) * 0.3)]
        # only a cliff standing IN FRONT of a crystal is refused; one behind it is wall
        depth = min(p[2] for p in pr)
        if any(len(hl) > 2 and L._inside(hl, px, py) and depth < hz for hl, hz in hulls for px, py in probe):
            continue
        boxes.append((c, gy, h, (x0, y0, x1, y1)))
    # the top centre must be visible past the terrain
    cam = L.CAMERA_POS
    rays = []
    for c, gy, h, _b in boxes:
        tx, ty, tz = c[6], gy + h * 0.6, c[1]
        rays.append([(cam[0] + (tx - cam[0]) * t, cam[2] + (tz - cam[2]) * t, cam[1] + (ty - cam[1]) * t)
                     for t in (k / 24 * 0.95 for k in range(1, 25))])
    rh = L.engine_heights([(rx, rz) for r in rays for rx, rz, _y in r])
    visible = [b for i, b in enumerate(boxes) if all(rh[i * 24 + k] <= rays[i][k][2] for k in range(24))]

    def overlap(p, q):
        ix = max(0, min(p[2], q[2]) - max(p[0], q[0]))
        iy = max(0, min(p[3], q[3]) - max(p[1], q[1]))
        return ix * iy / max(1.0, min((p[2] - p[0]) * (p[3] - p[1]), (q[2] - q[0]) * (q[3] - q[1])))
    chosen = []
    for b in sorted(visible, key=lambda b: -(b[3][2] - b[3][0]) * (b[3][3] - b[3][1])):
        if all(overlap(b[3], o[3]) < 0.35 for o in chosen):
            chosen.append(b)
        if len(chosen) >= count:
            break
    return [(c[0], c[1], c[2], c[3], c[4], c[5], 0.25) for c, _gy, _h, _bb in chosen]


def auto_outcrops(sizes, placed, footprints, count=8, seed=2026102318):
    """17 (richer rock): big textured rock outcrops (12-30 m) on the open foreground banks -- the reference's
    foreground is rock and moss -- in frame, in the lower half, clear of every crystal's outline, off wet ground."""
    import random
    rng = random.Random(seed)
    hulls = []
    for f, pos, q, _g in placed:
        sc = f[4]
        pts = [L._project([pos[i] + v[i] for i in range(3)])
               for v in (L._qrot(q, (sc * a[0], sc * a[1], sc * a[2])) for a in footprints[f[5]])]
        hulls.append(L._hull([(round(x), round(y)) for x, y, _z in pts]))
    cands = []
    for z in range(120, -320, -20):
        for side in (-1, 1):
            for off in (60, 85, 110, 140):
                a = rng.choice(["boulder", "mossrock", "boulder"])
                target = rng.uniform(12.0, 30.0)
                sc = target / max(sizes[a][0], sizes[a][2])
                cands.append((a, z, side, off, rng.uniform(0, 360), sc, L.river_x(z) + side * off))
    hs = L.engine_heights([(c[6], c[1]) for c in cands])
    picked, boxes = [], []
    for c, gy in zip(cands, hs):
        lvl = L.water_level(c[6], c[1])
        if lvl is not None and gy <= lvl + 1.0:
            continue
        r = 0.5 * max(sizes[c[0]][0], sizes[c[0]][2]) * c[5]
        px, py, pz = L._project((c[6], gy + r * 0.5, c[1]))
        if pz <= 1 or not (40 < px < 1880 and 560 < py < 1060):
            continue
        if any(len(hl) > 2 and L._inside(hl, px, py) for hl in hulls):
            continue
        rad = r / pz * 1080 / (2 * math.tan(math.radians(L.CAMERA_FOV) / 2))
        if any(math.hypot(px - bx, py - by) < (rad + br) * 1.6 for bx, by, br in boxes):
            continue
        boxes.append((px, py, rad))
        picked.append((c[0], c[1], c[2], c[3], c[4], c[5], 0.3))
        if len(picked) >= count:
            break
    print("outcrops placed by view: %d" % len(picked))
    return picked


# ---- 5d: the owner's marks on the 5c frame (1920x1080 pixels of layout.CAMERA_5C) ----------------------------------
# red boxes: cliff-like rock masses that butt up against and fit between the crystals
ROCK_BOXES = {"a": (507, 537, 742, 687), "b": (1072, 505, 1215, 568), "c": (1265, 587, 1465, 738)}
# blue ellipses: green cover -- 5d moss, 5e bushes (centre x, centre y, half width, half height)
COVER_AREAS = [(465, 812, 175, 100), (875, 530, 70, 90), (1034, 492, 46, 55), (1188, 642, 101, 75),
              (1810, 590, 100, 75), (1675, 851, 225, 149)]


def owner_rocks(sizes, seed=2026102324):
    """Rock masses across each red box: the box's ground footprint (its corners and centre raycast through the 5c
    camera) gets two or three big scans -- cliffs and mossy rock -- side by side, each scaled to about a third of the
    box's width and turned to face the camera, seated into the ground at its centre."""
    import random
    rng = random.Random(seed)
    items = []
    for key, (x0, y0, x1, y1) in sorted(ROCK_BOXES.items()):
        cols = 3 if (x1 - x0) > 180 else 2
        pix = [((x0 + (x1 - x0) * (i + 0.5) / cols), (y0 + y1) / 2 + rng.uniform(-0.15, 0.15) * (y1 - y0))
               for i in range(cols)]
        ends = L.screen_to_ground([(x0, (y0 + y1) / 2), (x1, (y0 + y1) / 2)] + pix)
        if None in ends[:2]:
            continue
        width = math.hypot(ends[1][0] - ends[0][0], ends[1][2] - ends[0][2])
        for hit in ends[2:]:
            if hit is None:
                continue
            a = rng.choice(["cliffA", "cliffD", "cliffB"])
            sc = width / cols * rng.uniform(1.6, 2.1) / max(sizes[a][0], sizes[a][2])   # masses, not rocks
            yaw = math.degrees(math.atan2(L.CAMERA_POS[0] - hit[0], L.CAMERA_POS[2] - hit[2])) + rng.uniform(-30, 30)
            items.append((a, hit[0], hit[2], yaw, sc, 0.12))
    print("owner rocks: %d" % len(items))
    return items


def cover_spots(placed, footprints, sizes=None, seed=2026102325):
    """Ground-cover spots (x, z, radius): over each of the owner's blue areas (pixels inside the ellipse raycast through
    the 5c camera), and ringed around every crystal's base where the stone meets the earth (5d; 5e: bushes)."""
    import random
    rng = random.Random(seed)
    spots = []
    for cx, cy, rx, ry in COVER_AREAS:
        n = int(30 + rx * ry / 350)   # dense: overlapping pads form a carpet
        pix = []
        while len(pix) < n:
            u, v = rng.uniform(-1, 1), rng.uniform(-1, 1)
            if u * u + v * v <= 1:
                pix.append((cx + u * rx, cy + v * ry))
        for hit in L.screen_to_ground(pix):
            if hit is not None:
                spots.append((hit[0], hit[2], rng.uniform(6.0, 13.0)))
    for f, pos, q, _g in placed:                            # around every crystal's base
        s = f[4]
        base = [L._qrot(q, (s * a[0], 0.0, s * a[2])) for a in footprints[f[5]] if a[1] < 0.5]
        for v in rng.sample(base, min(len(base), 24)):
            d = math.hypot(v[0], v[2]) or 1.0
            out = rng.uniform(0.6, 4.0)
            spots.append((pos[0] + v[0] + v[0] / d * out, pos[2] + v[2] + v[2] / d * out, rng.uniform(2.5, 5.5)))
    return spots


BUSHES = ("shrubA", "shrubA", "shrubA", "fernA")   # 5f: Poly Haven shrub_02 masses, fern_02 low clumps


def between_spots(placed, seed=2026102328):
    """5e (the owner: "fill in more in between the crystals with rocky cliffs, moss covered rocks, green bushy
    bushes"): along each bank, spots between neighbouring formations' bases (and a little either side of the line),
    each one a bush cluster, a moss-covered rock or a rock mass -- the crystals rising out of rock and foliage.
    Returns ([bush spots (x, z, r)], [scan items (asset, x, z, yaw, scale, sink)])."""
    import random
    rng = random.Random(seed)
    bush, rocks = [], []
    for side in (-1, 1):
        bank = sorted((p for p in placed if p[0][6] == side), key=lambda p: -p[1][2])
        for p, q in zip(bank, bank[1:]):
            (x0, _y0, z0), (x1, _y1, z1) = p[1], q[1]
            n = int(min(14, max(4, math.hypot(x1 - x0, z1 - z0) / 18)))
            for k in range(n):
                t = (k + 0.5) / n
                x = x0 + (x1 - x0) * t + rng.uniform(-22, 22)
                z = z0 + (z1 - z0) * t + rng.uniform(-14, 14)
                roll = rng.random()
                if roll < 0.5:
                    bush.append((x, z, rng.uniform(6.0, 12.0)))
                elif roll < 0.8:
                    a = rng.choice(["mossrock", "mossrock2"])
                    rocks.append((a, x, z, rng.uniform(0, 360), rng.uniform(0.9, 1.8), 0.25))
                else:
                    a = rng.choice(["cliffB", "cliffC", "boulder"])
                    sc = {"cliffB": rng.uniform(0.5, 0.9), "cliffC": rng.uniform(2.0, 3.5),
                          "boulder": rng.uniform(5.0, 9.0)}[a]
                    rocks.append((a, x, z, rng.uniform(0, 360), sc, 0.2))
    return bush, rocks


SCAN_SIZES = {}
RIVER_ROCKS = []   # (x, z, radius): foliage keeps clear of them, so the shoreline rocks read


def ground_cover(placed=(), footprints=None, step=22, seed=2026102330):
    """5f (the owner: "the entire ground should be covered in bushy texture ... all ground = bushes, rocky features for
    the ledges"): a jittered screen grid (every `step` px of the CURRENT camera) raycast onto the engine's ground. Each
    hit on gentle ground (normal.y >= 0.72) becomes a bush spot sized to its share of the screen at that distance, so
    the cover is even on screen (dense near, larger far); steep hits become rock-ledge spots. Wet ground is skipped.
    Returns ([bush spots (x, z, r)], [ledge spots (x, z, nx, nz, r)])."""
    import random
    rng = random.Random(seed)
    grid = [(gx, gy) for gy in range(int(step / 2), 1080, step) for gx in range(int(step / 2), 1920, step)]
    pix = [(gx + rng.uniform(-0.45, 0.45) * step, gy + rng.uniform(-0.45, 0.45) * step) for gx, gy in grid]
    hits = L.screen_to_ground(pix, camera=(L.CAMERA_POS, L.CAMERA_TARGET), step=3.0, reach=2400.0)
    at = {g: h for g, h in zip(grid, hits)}
    # crystal outlines on screen (current camera), dilated: no ledge rock may stand in one
    hulls = []
    for f, pos, q, _g in placed:
        sc = f[4]
        pr = [L._project([pos[i] + v[i] for i in range(3)])
              for v in (L._qrot(q, (sc * a[0], sc * a[1], sc * a[2])) for a in footprints[f[5]])]
        cx = sum(p[0] for p in pr) / len(pr)
        cy = sum(p[1] for p in pr) / len(pr)
        hulls.append((L._hull([(round(cx + (p[0] - cx) * 1.25), round(cy + (p[1] - cy) * 1.25)) for p in pr]),
                      min(p[2] for p in pr)))
    flags = []
    for (gx, gy), h in zip(grid, hits):
        if h is None:
            continue
        # near the skyline: the ground one or two rows up is sky, or much further away (a crest)
        dh = math.dist(L.CAMERA_POS, h)
        silhouette = False
        for k in (1, 2):
            above = at.get((gx, gy - k * step), "edge")
            if above is None or (above != "edge" and math.dist(L.CAMERA_POS, above) > 1.3 * dh):
                silhouette = True
        # in front of a crystal: inside its outline and not well behind it (rock behind a crystal is wall)
        in_crystal = any(len(hl) > 2 and L._inside(hl, gx, gy) and dh < hz + 40.0 for hl, hz in hulls)
        flags.append((silhouette, in_crystal))
    pts = [h for h in hits if h is not None]
    e = 2.0
    nh = L.engine_heights([q for h in pts for q in ((h[0] - e, h[2]), (h[0] + e, h[2]), (h[0], h[2] - e),
                                                     (h[0], h[2] + e))])
    tanh = math.tan(math.radians(L.CAMERA_FOV) / 2)
    bush, ledge = [], []
    for i, h in enumerate(pts):
        x, y, z = h
        lvl = L.water_level(x, z)
        if lvl is not None and y <= lvl + 0.4:
            continue
        hx0, hx1, hz0, hz1 = nh[i * 4:i * 4 + 4]
        nx, ny, nz = -(hx1 - hx0), 2 * e, -(hz1 - hz0)
        nl = math.sqrt(nx * nx + ny * ny + nz * nz)
        nx, ny, nz = nx / nl, ny / nl, nz / nl
        d = math.dist(L.CAMERA_POS, h)
        r = 1.5 * step * 2.0 * d * tanh / 1080.0   # (overlapping: ~3 spots' width each)
        silhouette, in_crystal = flags[i]
        if ny >= 0.72:
            # (5f, the owner: "try to prevent it from mistakenly adding stuff like this": no foliage on the skyline
            # or the wall tops -- a plant there reads as twigs against the sky)
            if not silhouette and y < 55.0:
                bush.append((x, z, r))
            elif not silhouette and not in_crystal:   # gentle wall tops (above the bush line) are rock too
                ledge.append((x, z, nx, nz, r))
        elif not silhouette and not in_crystal:   # rock on the ledges: never on the skyline, never over a crystal
            ledge.append((x, z, nx, nz, r))
    print("ground cover: %d bush spots, %d ledge spots" % (len(bush), len(ledge)))
    return bush, ledge


def river_rocks(seed=2026102332, groups=16):
    """5f (the owner: "try to add an assortment of smaller rocks along the sides of the river bed in aesthetically
    pleasing areas too"): groups of 4-9 rocks (boulders, stones, mossy rocks, 1-6 m) on the INSIDE of the river's bends
    -- the points and curves -- strung along the waterline, some in the shallows and some on the bank, with open
    stretches between the groups. The waterline is found on the engine's heights along the bend's normal."""
    import random
    rng = random.Random(seed)
    c = [p for p in L._CENTRE if -650.0 < p[2] < 40.0]
    bends = []
    for i in range(2, len(c) - 2):
        a, b, d = c[i - 2], c[i], c[i + 2]
        t1 = (b[0] - a[0], b[2] - a[2]); t2 = (d[0] - b[0], d[2] - b[2])
        cross = t1[0] * t2[1] - t1[1] * t2[0]
        bends.append((abs(cross) / (math.hypot(*t1) * math.hypot(*t2) + 1e-6), i, cross))
    bends.sort(reverse=True)
    picks = []
    for k, i, cross in bends:
        if all(abs(c[i][2] - c[j][2]) > 45.0 for j, _ in picks):
            picks.append((i, cross))
        if len(picks) >= groups:
            break
    probes, meta = [], []
    for i, cross in picks:
        p, q = c[i], c[min(i + 1, len(c) - 1)]
        tx, tz = q[0] - p[0], q[2] - p[2]
        n = math.hypot(tx, tz) or 1.0
        tx, tz = tx / n, tz / n
        side = 1.0 if cross > 0 else -1.0                  # the inside of the bend
        nx, nz = -tz * side, tx * side
        meta.append((p, tx, tz, nx, nz))
        probes += [(p[0] + nx * d, p[2] + nz * d) for d in range(4, 120, 2)]
    width = lambda z: 100.0 if z > -105.0 else (58.0 if z > -900.0 else 44.0)
    hs = L.engine_heights(probes)
    out = []
    for g, (p, tx, tz, nx, nz) in enumerate(meta):
        lvl = L.water_level(p[0], p[2]) or p[1]
        row = hs[g * 58:(g + 1) * 58]
        # the waterline: the first dry ground past ~40% of the reach's width (the raised water leaves a shallow bed
        # inside the channel that a plain first-dry search mistakes for the bank)
        start = 0.4 * width(p[2])
        edge = next((4 + 2 * j for j, h in enumerate(row) if 4 + 2 * j >= start and h > lvl + 0.5), None)
        if edge is None:
            continue
        sx, sy, sz = L._project((p[0] + nx * edge, lvl, p[2] + nz * edge))
        if sz <= 1 or not (0 < sx < 1920 and 0 < sy < 1080):
            continue
        for _ in range(rng.randrange(4, 10)):
            along = rng.gauss(0.0, 9.0)
            across = edge + rng.uniform(-7.0, 3.0)             # in the shallows to the bank's foot
            a = rng.choice(["boulder", "boulder", "stone", "rock", "mossrock2"])
            size = rng.choice([4.0, 5.5, 7.0, 8.5, 10.0, 12.0, 15.0])   # (the owner: larger, so the groups read)
            sc = size / max(SCAN_SIZES[a][0], SCAN_SIZES[a][2])
            out.append((a, p[0] + tx * along + nx * across, p[2] + tz * along + nz * across, rng.uniform(0, 360), sc,
                        0.3))
    print("river rocks: %d in %d groups" % (len(out), len(meta)))
    return out


def ledge_rocks(ledge, seed=2026102331):
    """5f: rock on the steep ledges -- a rock-face or cliff scan per ledge spot, sized to the spot, turned to face down
    the slope (its normal's horizontal direction), seated into it."""
    import random
    rng = random.Random(seed)
    out = []
    for x, z, nx, nz, r in ledge:
        a = rng.choice(["cliffC", "cliffB", "mossrock2", "mossrock", "mossrock2"])   # rock and mossy rock
        sc = 4.5 * r / max(SCAN_SIZES[a][0], SCAN_SIZES[a][2])
        yaw = math.degrees(math.atan2(nx, nz)) + rng.uniform(-20, 20)
        out.append((a, x, z, yaw, sc, 0.25))
    return out


def bush_points(spots, seed=2026102327):
    """Bush masses at the cover spots -- one to three per spot, overlapping into masses, varied in size, squash and turn,
    seated a little into the ground; none on wet ground. 5f: Poly Haven shrub_02 (and fern_02 low clumps), CC0."""
    import random
    rng = random.Random(seed)
    cand = []
    for x, z, r in spots:
        for _ in range(2 if r > 9.0 else rng.choice([2, 2, 3])):
            a = rng.uniform(0, math.tau)
            d = rng.uniform(0, r * 0.6)
            cand.append((x + d * math.cos(a), z + d * math.sin(a), r * rng.uniform(0.55, 0.9)))
    hs = L.engine_heights([(x, z) for x, z, _r in cand])
    pts = {k: [] for k in set(BUSHES) | {"shrubFar"}}
    for (x, z, r), h in zip(cand, hs):
        lvl = L.water_level(x, z)
        if lvl is not None and h <= lvl + 0.4:
            continue
        if any(math.hypot(x - rx, z - rz) < rad + 0.7 * r + 3.0 for rx, rz, rad in RIVER_ROCKS):
            continue
        q = L.quat_y(rng.uniform(0, 360))
        k = rng.choice(BUSHES)
        if k == "shrubA" and math.dist(L.CAMERA_POS, (x, h, z)) > 250.0:
            k = "shrubFar"
        w = max(SCAN_SIZES[k][0], SCAN_SIZES[k][2])
        sc = 2.0 * r / w * (1.6 if k == "fernA" else 1.0)
        sy = sc * rng.uniform(0.9, 1.25)
        pts[k].append([round(x, 3), round(h - 0.08 * SCAN_SIZES[k][1] * sy, 3), round(z, 3)] +
                      [round(c, 6) for c in q] + [round(sc, 3), round(sy, 3), round(sc * rng.uniform(0.85, 1.15), 3)])
    print("bushes: %d" % sum(len(v) for v in pts.values()))
    return pts


def place_scans(sizes, placed=(), footprints=None, extra=(), seated_extra=()):
    """Points for each scan asset: the cliffs, then a sparse, seeded scatter on both banks, denser on the rocky left.
    Everything stands on the engine's ground, and nothing goes on wet ground."""
    import random
    rng = random.Random(2026102302)
    items = []
    cliffs = CLIFFS + (auto_cliffs(sizes, placed, footprints) if footprints else [])
    if footprints:
        cliffs += auto_outcrops(sizes, placed, footprints)
    print("cliffs placed by view: %d" % (len(cliffs) - len(CLIFFS)))
    for asset, z, side, setback, yaw, sc, sink in cliffs:
        items.append((asset, L.river_x(z) + side * setback, z, yaw, sc, sink))
    owner = owner_rocks(sizes) if footprints else []
    # phase 2h: no rock inside a crystal's ground footprint (a boulder half inside a cluster read as a dark notch)
    def clear_of_crystals(x, z, r):
        return all(min(math.hypot(x - gx, z - gz) for gx, gz in ground) > r + 3.0 for _f, _p, _q, ground in placed)
    items = [it for it in items if clear_of_crystals(it[1], it[2], 0.5 * max(sizes[it[0]][0], sizes[it[0]][2]) * it[4])]
    for asset, (nl, nr, lo, hi) in SCATTER.items():
        for side, n in ((-1, nl), (1, nr)):
            for _ in range(n):
                z = rng.uniform(-420.0, 120.0)
                d = rng.uniform(44.0, 120.0) * (1.0 + max(0.0, -z) / 900.0)
                x, sc = L.river_x(z) + side * d, rng.uniform(lo, hi)
                if clear_of_crystals(x, z, 0.5 * max(sizes[asset][0], sizes[asset][2]) * sc):
                    items.append((asset, x, z, rng.uniform(0, 360), sc, 0.25))
    items += owner   # 5d: the owner's rock masses go between the crystals (not filtered by the footprints)
    # 5e: the between-crystal rocks, a step clear of the crystals' footprints
    items += [it for it in extra if clear_of_crystals(it[1], it[2], 1.0)]
    # each piece sinks from the LOWEST ground under its footprint (centre and four corners), so none floats as a
    # shelf off a slope
    probes = []
    for asset, x, z, yaw, sc, _sink in items:
        w, _h, d = (c * sc * 0.45 for c in sizes[asset])
        c, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        probes.append((x, z))
        probes += [(x + c * u + s_ * v, z - s_ * u + c * v) for u, v in ((w, d), (-w, d), (w, -d), (-w, -d))]
    ph = L.engine_heights(probes)
    # a cliff sits on the ground at its centre (it stands proud of the slope; from the lowest corner the uphill side
    # swallowed it); everything else from the lowest corner, so nothing floats
    hs = [ph[i * 5] if items[i][0].startswith("cliff") else min(ph[i * 5:i * 5 + 5]) for i in range(len(items))]
    # 5e/5f support audit (the owner: "make sure it's all representative of actual land scapes" -- and, 5f, "why did
    # you remove some from the left hand side?"): a cliff must be SUPPORTED -- its base buried at its lowest corner and
    # its top tucked into the slope uphill of it. A cliff that stands proud is not dropped but RESEATED: moved up into
    # the wall (outward from the river, 0-90 m) and, if needed, made smaller (x1, 0.8, 0.6), taking the first variant
    # that is supported with at most 80% of its height buried. Only one with no supported variant is dropped (listed).
    VAR = [(dx, f) for f in (1.0, 0.8, 0.6) for dx in (0.0, 25.0, 50.0, 80.0, 115.0, 150.0)]
    cl = [i for i, it in enumerate(items) if it[0].startswith("cliff")]
    probe = []
    for i in cl:
        asset, x, z, yaw, sc, _sink = items[i]
        side = 1.0 if x > L.river_x(z) else -1.0
        for dx, f in VAR:
            xx, s2 = x + side * dx, sc * f
            w, _h, d = (c * s2 * 0.45 for c in sizes[asset])
            c, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
            back = 0.5 * max(sizes[asset][0], sizes[asset][2]) * s2 + 12.0
            probe += [(xx, z)] + [(xx + c * u + s_ * v, z - s_ * u + c * v) for u, v in ((w, d), (-w, d), (w, -d),
                                                                                       (-w, -d))] + [(xx + side * back, z)]
    ph2 = L.engine_heights(probe)
    k = 0
    seated, dropped = {}, []
    for i in cl:
        asset, x, z, yaw, sc, sink = items[i]
        side = 1.0 if x > L.river_x(z) else -1.0
        best = None
        for dx, f in VAR:
            hc, corners, up_h = ph2[k], ph2[k + 1:k + 5], ph2[k + 5]
            k += 6
            if best is not None:
                continue
            height = sizes[asset][1] * sc * f
            base = min(hc - sink * height, min(corners) - 0.3)
            if base + height > up_h + 0.25 * height:
                base = up_h + 0.25 * height - height
            if base >= hc - 0.8 * height:
                best = (asset, x + side * dx, z, yaw, sc * f, (hc - base) / height, hc)
        if best is None:
            dropped.append("%s at (%.0f, %.0f)" % (asset, x, z))
        else:
            seated[i] = best
    # 5f: a seated cliff must also be BACKED by land, not sky -- the ray from the camera through its top must meet
    # the ground behind it (within 600 m); a cliff whose top is a skyline is sunk 30% more or, failing that, dropped
    rays = []
    for i, (asset, x, z, yaw, sc, sink, hc) in seated.items():
        height = sizes[asset][1] * sc
        top = (x, hc - sink * height + height, z)
        d = math.dist(L.CAMERA_POS, top)
        rays.append([(L.CAMERA_POS[0] + (top[0] - L.CAMERA_POS[0]) * t, L.CAMERA_POS[2] + (top[2] - L.CAMERA_POS[2]) * t,
                      L.CAMERA_POS[1] + (top[1] - L.CAMERA_POS[1]) * t)
                     for t in (1.0 + 600.0 / d * k / 20 for k in range(1, 21))])
    rh = L.engine_heights([(rx, rz) for r in rays for rx, rz, _y in r])
    for j, i in enumerate(list(seated)):
        backed = any(rh[j * 20 + k] >= rays[j][k][2] for k in range(20))
        if not backed:
            asset, x, z, yaw, sc, sink, hc = seated[i]
            if sink + 0.3 <= 0.85:
                seated[i] = (asset, x, z, yaw, sc, sink + 0.3, hc)
            else:
                dropped.append("%s at (%.0f, %.0f) [skyline]" % (asset, x, z))
                del seated[i]
    print("cliffs reseated: %d, dropped: %d %s" % (len(seated), len(dropped), dropped))
    keep, hs2 = [], []
    for i, (it, h) in enumerate(zip(items, hs)):
        if not it[0].startswith("cliff"):
            keep.append(it)
            hs2.append(h)
        elif i in seated:
            keep.append(seated[i][:6])
            hs2.append(seated[i][6])
    items, hs = keep, hs2
    # 5f: ledge rocks and river-edge rocks lie on their ground -- seated from the lowest of five probes, no wall audit
    if seated_extra:
        pr = []
        for asset, x, z, yaw, sc, _sink in seated_extra:
            w, _h, d = (c * sc * 0.45 for c in sizes[asset])
            c, s_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
            pr += [(x, z)] + [(x + c * u + s_ * v, z - s_ * u + c * v) for u, v in ((w, d), (-w, d), (w, -d), (-w, -d))]
        eh = L.engine_heights(pr)
        items += list(seated_extra)
        # a rock on a steep ledge sits at its centre's height (from the lowest corner the uphill side swallowed it);
        # a rock on gentle ground or at the waterline from its lowest corner
        for i, it in enumerate(seated_extra):
            g = eh[i * 5:i * 5 + 5]
            hs.append(g[0] - 0.15 * (max(g) - min(g)) if max(g) - min(g) > 0.4 * sizes[it[0]][1] * it[4] else min(g))
    # 5f: every piece -- not only the wall cliffs -- must be backed by land: the ray from the camera through its top
    # meets the ground behind it within 600 m. A piece whose top is a skyline is sunk to put its top 30% lower, or
    # dropped if that would bury more than 85% of it.
    rays = []
    for (asset, x, z, yaw, sc, sink), h in zip(items, hs):
        height = sizes[asset][1] * sc
        top = (x, h - sink * height + height, z)
        d = max(math.dist(L.CAMERA_POS, top), 1.0)
        rays.append([(L.CAMERA_POS[0] + (top[0] - L.CAMERA_POS[0]) * t, L.CAMERA_POS[2] + (top[2] - L.CAMERA_POS[2]) * t,
                      L.CAMERA_POS[1] + (top[1] - L.CAMERA_POS[1]) * t)
                     for t in (1.0 + 600.0 / d * k / 16 for k in range(1, 17))])
    rh = L.engine_heights([(rx, rz) for r in rays for rx, rz, _y in r])
    keep, hs2, sky = [], [], 0
    for j, (it, h) in enumerate(zip(items, hs)):
        if any(rh[j * 16 + k] >= rays[j][k][2] for k in range(16)):
            keep.append(it)
            hs2.append(h)
        elif it[5] + 0.3 <= 0.85:
            keep.append(it[:5] + (it[5] + 0.3,))
            hs2.append(h)
            sky += 1
        else:
            sky += 1
    print("skyline test: %d pieces sunk or dropped" % sky)
    items, hs = keep, hs2
    pts = {}
    for (asset, x, z, yaw, sc, sink), h in zip(items, hs):
        lvl = L.water_level(x, z)
        if lvl is not None and h <= lvl + 0.3:
            continue
        y = h - sink * sizes[asset][1] * sc
        q = L.quat_y(yaw)
        pts.setdefault(asset, []).append([round(x, 3), round(y, 3), round(z, 3)] + [round(c, 6) for c in q] +
                                         [sc, sc, sc])
    return pts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", required=True)
    ap.add_argument("--scene-dir", default=os.path.join(HERE, "scene"))
    ap.add_argument("--project-dir", default=HERE)
    a = ap.parse_args()
    # phase 2d: no crystal may stand in (or lean over) the river; the build fails if one does
    placed, rows, bad = L.check_crystals(json.load(open(os.path.join(a.assets, "footprints.json"))))
    print("river clearance (m, min %.1f):" % L.RIVER_MARGIN, " ".join("%s %.0f" % r for r in rows))
    if bad:
        sys.exit("FAIL: crystals within %.1f m of the river: %s" % (L.RIVER_MARGIN,
                                                                 ", ".join("%s %.1f" % b for b in bad)))
    if CIRCUITS:
        build_circuits(os.path.join(a.assets, "circuits.glb"), placed)
    rows, cover = L.layering(placed, json.load(open(os.path.join(a.assets, "footprints.json"))))
    print("layering (frame % / visible %): " + " ".join("%s %.1f/%.0f" % r for r in rows),
          "| crystals cover %.1f%% of the frame" % cover)
    sweep = L.terrain_sweep(placed, json.load(open(os.path.join(a.assets, "footprints.json"))))
    print("terrain (hidden % / buried %): " + " ".join("%s %.0f/%.0f" % r for r in sweep))
    os.makedirs(a.scene_dir, exist_ok=True)
    EDGE_WIDEN.update(json.load(open(os.path.join(a.assets, "edges.json"))))
    SCAN_SIZES.update(json.load(open(os.path.join(a.assets, "scans.json"))))
    fill_bush, fill_rocks = between_spots(placed)
    cover_bush, ledge = ground_cover(placed, json.load(open(os.path.join(a.assets, "footprints.json"))))
    rr = river_rocks()
    seated = ledge_rocks(ledge) + rr
    RIVER_ROCKS[:] = [(x, z, 0.5 * max(SCAN_SIZES[a][0], SCAN_SIZES[a][2]) * sc) for a, x, z, _y, sc, _s in rr]
    bushes = bush_points(cover_spots(placed, json.load(open(os.path.join(a.assets, "footprints.json")))) + fill_bush +
                         cover_bush)
    sc = scene_json(placed, place_scans(json.load(open(os.path.join(a.assets, "scans.json"))), placed,
                                       json.load(open(os.path.join(a.assets, "footprints.json"))), fill_rocks,
                                       seated), bushes)
    for n in sc["nodes"]:
        src = n.get("procedural", {}).get("source", {})
        if src.get("kind") == "mesh":
            shutil.copy(os.path.join(a.assets, src["asset"]), os.path.join(a.scene_dir, src["asset"]))
    if TERRAIN_TEX:   # the textures beside the scene (ADR-1642 resolves program texture paths there)
        for _n, pth, _cs in TERRAIN_TEXTURES:
            shutil.copy(os.path.join(TEX_DIR, pth), os.path.join(a.scene_dir, os.path.basename(pth)))
    path = os.path.join(a.scene_dir, "aurora-valley.scene.json")
    json.dump(sc, open(path, "w"), indent=1)
    rel = os.path.relpath(path, a.project_dir)
    root = os.path.relpath(os.path.join(HERE, "..", ".."), a.project_dir)
    for name, off in (("aurora-valley.json", False), ("aurora-valley-offline.json", True)):
        with open(os.path.join(a.project_dir, name), "w") as f:
            json.dump(project_json(rel, off, root), f, indent=1)
            f.write("\n")
    print("wrote", path)


if __name__ == "__main__":
    main()

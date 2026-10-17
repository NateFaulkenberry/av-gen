#!/usr/bin/env python3
"""WAVEFORM DIMENSION: the generator (docs/prototypes/waveform-dimension/).

    python3 tools/waveform_dimension/build.py                 # the scene, its meshes and its index entry
    python3 tools/waveform_dimension/build.py --variant --out <project.json> [--camera ...] [--set path=json]
    then: avgen --headless --project <project.json> --audio assets/audio/feline-footwear.wav --render ...

Writes examples/waveform-dimension/{waveform-dimension.json, waveform-dimension.scene.json, meshes/*.glb} and the
scene's entry in examples/index.json (WD_OUT=<dir> writes a look-development copy there instead, with no index entry).

A live scene: one three-dimensional field turned into a sculpture of nested curved sheets. The field has two parts:
  - the CARRIER (levelsets.py): the standing interference of three long spherical waves from far outside the frame,
    one leading. Its level sets, extracted at build time by surface nets, are the sculpture's four nested sheets; a
    second, shorter interference (the APERTURE) cuts a few large openings in them. Static: the sculpture's identity.
  - the RESONANCE (FieldKind::Resonance, ADR-1400): one resonator excited by the audio history, whose modes move the
    sheets on the GPU (bass lobes, low-mid folds in three turned frames, mid bands, a kick's travelling shell) and
    light them; the slow families are baked once a frame into a volume grid (ADR-1440).
The Stage A prototypes this replaced (SDF isosurfaces, designed parametric sheets) are recorded in
docs/prototypes/waveform-dimension/ (PROGRESS.md, 02-report.md) and in git history; WD_NEUTRAL=1 builds the grey
review variant (neutral shading, no post).
"""
from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.sonic_vfx import kit  # noqa: E402
from tools.sonic_vfx.kit import R, hexrgb  # noqa: E402
from tools.waveform_dimension import levelsets  # noqa: E402

SID = "waveform-dimension"
TITLE = "Waveform Dimension"
OUT = os.environ.get("WD_OUT") or os.path.join(REPO, "examples", SID)
MESH_DIR = os.path.join(OUT, "meshes")

NEUTRAL = os.environ.get("WD_NEUTRAL", "0") == "1"   # a review aid: grey material, white light, no emission, no post

# ================================================================================================ palette
# The owner's corrected palette (00-brief.md, appended): red spectral energy trapped inside a vast indigo-violet
# mathematical sculpture. sRGB hex here; hexrgb() converts to linear.
P = {
    "obsidian": "#080713",   # background, deep shadows, negative space
    "indigo": "#21103D",     # main shadow-side surfaces
    "royal": "#482080",      # midtone contours and layered depth
    "purple": "#782A9B",     # resonant bands and interference
    "crimson": "#C32965",    # stronger wavefronts and active folds
    "red": "#F04455",        # selective resonant highlights
    "rose": "#FF8B9A",       # rare high-intensity accents
}


def C(key, scale=1.0):
    return hexrgb(P[key], scale)


# ================================================================================================ the resonance
# One resonator, four families (a source each, so each keeps its own energy budget). The dispersion axis gives every
# band its scale:
#   L  bass      u 0-0.2      point modes, 70-44 m: broad lobes about the sculpture's middle
#   A  low mids  u 0.25-0.45  plane waves, 39-25 m: heard THREE times, in three frames 66-69 degrees apart -- the
#                             readers share A's modes (wavelength and phase), so their sum is a coherent, aperiodic
#                             relative of the triply periodic minimal surfaces: folds in every direction, never a grid
#   B  mids      u 0.45-0.68  plane waves, 24-15 m: travelling interference bands (they drift)
#   C  highs     u 0.7-1      plane waves, 13-7 m: fine ridges and highlights
# A lattice source's modes share one orientation, so a family keeps its direction while the music moves its weight
# between wavelengths. `drive` keeps every family faintly ringing in silence (idle reaches only the lowest modes).
FIELD = "wave"
LAYOUT = 16.0
SOURCES = [
    {"kind": "point", "position": [0.0, 0.0, 0.0], "order": 3, "bandLow": 0.0, "bandHigh": 0.2, "reach": 0.0,
     "gain": 1.0, "amplitude": 0.8, "breath": 0.3, "drift": 6.0, "driftPeriod": 120.0, "spin": 0.01,
     "orders": [0, 2, 3, 0], "focus": 0.1, "focusWidth": 0.06, "focusSweep": 0.08, "focusPeriod": 71.0,
     "focusFloor": 0.25},
    {"kind": "lattice", "position": [0.0, 0.0, 0.0], "order": 1, "bandLow": 0.25, "bandHigh": 0.45, "reach": 0.0,
     "gain": 1.0, "amplitude": 1.0, "breath": 0.25, "drift": 9.0, "driftPeriod": 97.0, "spin": 0.002,
     "orders": [1], "focus": 0.33, "focusWidth": 0.07, "focusSweep": 0.06, "focusPeriod": 61.0, "focusFloor": 0.3,
     "drive": 0.35},
    {"kind": "lattice", "position": [0.0, 0.0, 0.0], "order": 1, "bandLow": 0.45, "bandHigh": 0.68, "reach": 0.0,
     "gain": 1.0, "amplitude": 0.85, "breath": 0.35, "drift": 14.0, "driftPeriod": 53.0, "spin": -0.002,
     "orders": [1], "focus": 0.53, "focusWidth": 0.07, "focusSweep": 0.07, "focusPeriod": 47.0, "focusFloor": 0.3,
     "drive": 0.3},
    {"kind": "lattice", "position": [0.0, 0.0, 0.0], "order": 1, "bandLow": 0.7, "bandHigh": 1.0, "reach": 0.0,
     "gain": 1.0, "amplitude": 0.25, "breath": 0.6, "drift": 4.0, "driftPeriod": 59.0, "spin": 0.003,
     "orders": [1, 2], "focus": 0.8, "focusWidth": 0.08, "focusSweep": 0.1, "focusPeriod": 37.0, "focusFloor": 0.2,
     "drive": 0.2},
]
# name -> (listen band, frame rotation in degrees)
READERS = {
    "a1": ((0.25, 0.45), [0.0, 0.0, 0.0]),
    "a2": ((0.25, 0.45), [-60.0, 0.0, 30.0]),
    "a3": ((0.25, 0.45), [50.0, 0.0, -60.0]),
    "l": ((0.0, 0.2), [0.0, 0.0, 0.0]),
    "b": ((0.45, 0.68), [40.0, 0.0, -30.0]),   # the mids' bands: a live deformer, never baked (see deformers())
}
RELIEF = {"f-a1": 0.55, "f-a2": 0.4, "f-a3": 0.4, "f-l": 5.0}   # metres of normal displacement per unit figure


def resonator(s):
    res = {
        "modes": LAYOUT, "layout": LAYOUT, "fLow": 40.0, "fHigh": 6000.0, "lambdaMax": 70.0, "lambdaMin": 7.0,
        "scale": 1.0, "selectivity": 3.0, "contrast": 0.55, "persistence": 3.0, "buildup": 0.3, "memory": 2.0,
        "sustain": 1.0, "transient": 0.7, "idle": 0.5, "breath": 0.1, "limit": 1.0, "competition": 1.2,
        "attack": [0.3, 0.12, 0.05], "release": [4.0, 2.0, 0.8],
        "sources": SOURCES, "channel": "displacement",
    }
    s.nodes.append({"name": FIELD, "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "resonance", "seed": 11, "strength": 1.0, "falloff": {"kind": "none"},
                              "resonance": res}})


def field(s, name, spec, position=(0.0, 0.0, 0.0), rotation=None, scale=None):
    n = {"name": name, "kind": "field", "position": [float(v) for v in position], "field": spec}
    if rotation:
        n["rotation"] = [float(v) for v in rotation]
    if scale:
        n["scale"] = [float(v) for v in scale]
    s.nodes.append(n)


def reader(s, name, channel, extra=None, rotation=None, strength=1.0, position=(0.0, 0.0, 0.0)):
    r = {"channel": channel}
    r.update(extra or {})
    field(s, name, {"kind": "resonance", "reference": FIELD, "strength": float(strength), "falloff": {"kind": "none"},
                    "resonance": r}, position, rotation)


def compound(s, name, children, strength=1.0):
    assert 1 <= len(children) <= 4, name   # a compound holds four children, one level deep (fields.wgsl)
    field(s, name, {"kind": "compound", "children": list(children), "combine": "add", "strength": float(strength),
                    "falloff": {"kind": "none"}})


def resonance_fields(s):
    """f-a1..a3, f-l, f-b: the families as signed slow figures (plus a little vibration); M = the low-mid triple; en =
    the excitation of the mids and highs (|fast figure|), for light; front = a transient's spherical wavefront."""
    for name, ((lo, hi), rot) in READERS.items():
        reader(s, "f-" + name, "relief", {"reliefSand": 0.0, "reliefFigure": 1.0, "reliefVibration": 0.2,
                                          "listenLow": lo, "listenHigh": hi, "listenWidth": 0.03}, rotation=rot)
    # D: the sheets' displacement, the families summed with their relief as strengths, baked once a frame into a
    # volume (ADR-1440) that the single deformer samples (per vertex, four times with the finite-difference normal)
    compound(s, "D", list(RELIEF), strength=1.0)
    for n in s.nodes:
        if n["name"] in RELIEF:
            n["field"]["strength"] = RELIEF[n["name"]]
    bake_grid(s, "D", D_RES)
    field(s, "one", {"kind": "constant", "strength": 1.0, "falloff": {"kind": "none"}})
    # each low onset launches a spherical shell from the sculpture's middle, travelling out through the sheets and
    # decaying (ADR-1116; exact under seek)
    field(s, "front", {"kind": "onset", "falloff": {"kind": "none"}, "onsetSource": "low", "audioSpeed": 32.0,
                       "onsetWidth": 7.0, "onsetDecay": 1.1, "waveGeometry": "spherical", "point": [0.0, 0.0, 0.0],
                       "strength": 1.0})


# ================================================================================================ meshes
def write_glb(path, pos, nrm, uv, idx, name):
    pb, nb, ub, ib = pos.tobytes(), nrm.tobytes(), uv.tobytes(), idx.tobytes()
    blob = pb + nb + ub + ib
    blob += b"\0" * ((4 - len(blob) % 4) % 4)
    lo, hi = pos.min(axis=0).tolist(), pos.max(axis=0).tolist()
    o1, o2, o3 = len(pb), len(pb) + len(nb), len(pb) + len(nb) + len(ub)
    gltf = {
        "asset": {"version": "2.0", "generator": "waveform_dimension/build.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "materials": [{"name": name, "doubleSided": True,
                       "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0.0,
                                                "roughnessFactor": 1.0}}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                  "indices": 3, "material": 0, "mode": 4}]}],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pb), "target": 34962},
            {"buffer": 0, "byteOffset": o1, "byteLength": len(nb), "target": 34962},
            {"buffer": 0, "byteOffset": o2, "byteLength": len(ub), "target": 34962},
            {"buffer": 0, "byteOffset": o3, "byteLength": len(ib), "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": int(len(pos)), "type": "VEC3", "min": lo, "max": hi},
            {"bufferView": 1, "componentType": 5126, "count": int(len(nrm)), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": int(len(uv)), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": int(len(idx)), "type": "SCALAR"},
        ],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(blob)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + blob)


CELL = 1.4   # the level sheets' surface-nets cell (m): 130k vertices in all


def write_level_meshes():
    sizes = []
    for i, c in enumerate(levelsets.LEVELS):
        pos, nrm, uv, idx = levelsets.level_sheet(c, i, cell=CELL)
        write_glb(os.path.join(MESH_DIR, "wd-level%d.glb" % i), pos, nrm, uv, idx, "level%d" % i)
        sizes.append(len(pos))
    return sizes


# ================================================================================================ programs
class Prog:
    """A material program assembled by label (Cymatic World's pattern)."""

    def __init__(self, name):
        self.name = name
        self.ops = []
        self.at = {}

    def op(self, label, kind, dst, a=None, b=None, c=None, k=None, f=None, inp=None, field=None, k2=None, k3=None,
           k4=None):
        o = {"kind": kind, "dst": dst}
        if kind == "remap" and f is None:
            f = 0.0
        if a is not None:
            o["srcA"] = a
        if b is not None:
            o["srcB"] = b
        if c is not None:
            o["srcC"] = c
        for key, v in (("constant", k), ("constant2", k2), ("constant3", k3), ("constant4", k4)):
            if v is not None:
                o[key] = [round(float(x), 6) for x in (list(v) + [0.0] * 4)[:4]]
        if f is not None:
            o["value"] = float(f)
        if inp is not None:
            o["input"] = inp
        if field is not None:
            o["field"] = field
        self.ops.append(o)
        if label:
            assert label not in self.at, label
            self.at[label] = len(self.ops)
        assert len(self.ops) <= 48
        return len(self.ops)

    def __getitem__(self, label):
        return self.at[label]

    def doc(self, base=-1, emission=-1, roughness=-1, opacity=-1):
        return {"name": self.name, "ops": self.ops, "baseColor": base, "metallic": -1, "roughness": roughness,
                "emission": emission, "emissionIntensity": 1.0, "opacity": opacity}


def surface_program(name):
    """The sheets' surface, fourteen ops (the interpreter costs about 0.7 ms an op at 1080p, Stage B profile).
    Blackened indigo, a step lighter on each nested sheet (related hues, different light); emission only where the
    mids and highs ring hard or a kick's shell passes -- electric purple rising to spectral red; the sheets' cut edges
    traced by a crimson-to-rose line that the same excitation brightens. Lit by the scene's lights."""
    g = Prog(name)   # eight registers, r0..r7
    g.op(None, "input", 7, inp="uv")                              # r7 = (edge distance, sheet index)
    g.op("front", "field", 6, field="front")                     # r6 = the travelling shell
    g.op("sheetTone", "gradient", 1, a=7, k=[0.0, 1.0 / 3.0, 0.0, 0.0], f=1.0)   # r1 = sheet index / 3
    if NEUTRAL:   # grey, one step lighter per nested sheet, and the cut edges drawn as a pale line (shading, no colour)
        g.op(None, "ramp", 4, a=1, k=[0.14, 0.14, 0.15, 1.0], k2=[0.19, 0.19, 0.2, 1.0], k3=[0.25, 0.25, 0.26, 1.0])
        g.op(None, "swizzle", 5, a=7, k=[0, 0, 0, 0])
        g.op(None, "smoothstep", 5, a=5, k=[1.8, 0.4])
        g.op(None, "ramp", 2, a=5, k=[0, 0, 0, 1], k2=[0.05, 0.05, 0.05, 1], k3=[0.12, 0.12, 0.12, 1])
        g.base, g.emit, g.opacity = 4, 2, -1
        return g
    # albedo: each nested sheet its own value of the same indigo-violet (related hues, different light); the
    # outermost the lightest royal, the inner ones sinking to blackened indigo
    g.op("albedo", "ramp", 4, a=1, k=C("royal", 0.95) + [1.0], k2=C("indigo", 1.6) + [1.0], k3=C("indigo", 1.1) + [1.0])
    # the glow follows the form, never painted spots: electric purple rising to spectral red on the CRESTS of the folds
    # (convexity, 1/m) where the families' displacement reinforces (|D| large: constructive interference) -- bands
    # along the sheets' curvature; a kick's shell adds its crimson band as it passes. How bright it is follows the
    # music's level (a route on the ramp)
    g.op("reinf", "field", 3, field="D-grid")
    g.op(None, "multiply", 3, a=3, b=3)                           # r3 = |D|^2
    g.op("purpleMask", "smoothstep", 5, a=3, k=[9.0, 36.0])
    g.op("redMask", "smoothstep", 3, a=3, k=[49.0, 100.0])
    g.op(None, "add", 5, a=5, b=3)                                # r5 in 0..2
    g.op(None, "input", 0, inp="convexity")
    g.op("crest", "smoothstep", 0, a=0, k=[0.01, 0.06])
    g.op(None, "multiply", 5, a=5, b=0)                           # only on the folds' crests
    g.op("glowT", "smoothstep", 5, a=5, k=[0.0, 1.6])
    g.op(None, "add", 5, a=5, b=6)
    g.op("glow", "ramp", 2, a=5, k=[0, 0, 0, 0], k2=C("purple", 0.14) + [0.5], k3=C("red", 0.45) + [1.0])
    # the cut edges: a line on the outer sheets only, fading inward, lit by the glow; rose-white only where a primary
    # edge rings hardest (restrained: the line is narrow and dim at rest)
    g.op(None, "swizzle", 3, a=7, k=[0, 0, 0, 0])
    g.op("edgeLine", "smoothstep", 3, a=3, k=[2.2, 0.5])
    g.op("edgeWeight", "remap", 1, a=1, k=[0.0, 0.85, 1.0, 0.0], f=1.0)   # the outer sheets only
    g.op(None, "multiply", 3, a=3, b=1)
    g.op("edgeGain", "smoothstep", 1, a=5, k=[-0.3, 1.0])
    g.op(None, "multiply", 3, a=3, b=1)
    g.op("edgeTint", "ramp", 3, a=3, k=[0, 0, 0, 0], k2=C("crimson", 0.2) + [0.5], k3=C("rose", 0.85) + [1.0])
    g.op(None, "add", 2, a=2, b=3)
    g.base, g.emit, g.opacity = 4, 2, -1
    return g


def material(prog):
    m = {"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0, "roughness": 0.38,
         "metallic": 0.0, "doubleSided": True, "program": prog}
    return m   # opaque: the sheets are cut as geometry (levelsets.clip_mesh), never by a masked discard


# ================================================================================================ geometry
FRONT_RELIEF = 7.0   # metres a kick's shell lifts the sheets it passes through
B_RELIEF = 0.6       # metres per unit of the mids' bands
SPREAD = 3.2         # metres per unit of bass that each sheet moves away from the nest's middle sheet (spacing)


def deformers():
    # the baked families, and a kick's travelling shell (the onset field, live: eight onsets a vertex is cheap)
    # the mids' bands (15-24 m) evaluated live: baked into 2 m cells their trilinear normals striated the sheets
    # along the grid planes (Stage C review); the spacing term: a constant field, its amount per sheet routed (the
    # nested sheets part and close, keeping their order: the contours shift relative to each other)
    return [{"kind": "field", "field": "D-grid", "amount": 1.0, "space": "world", "alongNormal": True},
            {"kind": "field", "field": "front", "amount": FRONT_RELIEF, "space": "world", "alongNormal": True},
            {"kind": "field", "field": "f-b", "amount": B_RELIEF, "space": "world", "alongNormal": True},
            {"kind": "field", "field": "one", "amount": 0.0, "space": "world", "alongNormal": True}]


def level_sheets(s, prog):
    for i, _ in enumerate(levelsets.LEVELS):
        s.proc("level%d" % i, {"kind": "mesh", "asset": "meshes/wd-level%d.glb" % i}, material=material(prog),
               extra={"normalStep": 3.0}, deformers=deformers())   # 1.5 bake cells: smooth normals across cells


# ADR-1440: the slow families are baked once a frame into a volume grid that the deformer samples (2 m cells)
BAKE_BOUNDS = ([-128.0, -80.0, -128.0], [128.0, 80.0, 128.0])
D_RES = [128, 80, 128]


def bake_grid(s, source, res, strength=1.0):
    """A volume bake `<source>-bake` holding `source` at its cells every frame, and the field `<source>-grid` that reads
    it (ADR-1407, ADR-1440)."""
    if not hasattr(s, "grids"):
        s.grids = []
    s.grids.append({"name": source + "-bake", "enabled": True, "mode": "bake", "wrap": "clamp",
                    "resolution": list(res), "boundsMin": BAKE_BOUNDS[0], "boundsMax": BAKE_BOUNDS[1],
                    "injectField": source})
    field(s, source + "-grid", {"kind": "grid", "reference": source + "-bake", "strength": float(strength),
                                "falloff": {"kind": "none"}})


# ================================================================================================ the scene
def environment(s):
    s.environment = {
        "intensity": 0.03, "background": C("obsidian"), "fogColor": C("obsidian"),
        "volumeDensity": 0.0, "volumeMaxDistance": 0.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0,
        "shadowRange": 200.0, "ambientOcclusion": False, "contactShadows": False,
        # the depth prepass, asked for (ADR-1406): five nested opaque sheets overdraw each other, and with the deformation
        # baked (ADR-1440) a second geometry pass costs ~3 ms and saves ~20 of shading (Stage B profile)
        "depthPrepass": True,
        "sky": {"enabled": True, "zenithColor": C("obsidian", 0.6), "horizonColor": C("indigo", 0.5),
                "groundColor": C("obsidian", 0.5), "haze": 0.3, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    if NEUTRAL:
        s.light("key", "directional", direction=[0.62, -0.35, -0.7], color=[1, 1, 1], intensity=3.2, castsShadow=False)
        s.light("fill", "directional", direction=[-0.4, 0.8, 0.3], color=[1, 1, 1], intensity=0.45, castsShadow=False)
        return
    # a real grazing key from high left (pale royal violet), an electric-purple rim from behind that draws the sheets'
    # silhouettes and offsets against the dark, a faint royal fill from below; the darks stay dark
    s.light("key", "directional", direction=[0.62, -0.35, -0.7], color=hexrgb("#a68bff"), intensity=3.0,
            castsShadow=False)
    s.light("rim", "directional", direction=[-0.35, -0.25, 0.9], color=C("purple"), intensity=1.6, castsShadow=False)
    s.light("fill", "directional", direction=[-0.4, 0.8, 0.3], color=C("royal"), intensity=0.35, castsShadow=False)


POST = {
    "post/motionBlur/amount": 0.0, "post/dof/enabled": False, "post/dof/physical": False,
    "post/tonemap/chroma-retention": 0.85, "post/bloom/radius": 0.7, "post/bloom/intensity": 0.25,
    "post/bloom/threshold": 1.1, "post/output/vignette": 0.4, "post/output/grain": 0.01,
    "post/grade/contrast": 1.15, "post/lens/chromaticAberration": 0.0, "camera/exposure/compensation": 0.0,
    "scene/fogColor": C("obsidian"),
}


def design():
    return {
        "category": "abstract",
        "thesis": "A vast indigo-violet sculpture of nested curved sheets -- the level sets of one standing "
                  "interference field -- that the music bends, folds and lights: red spectral energy gathers where "
                  "its modes reinforce.",
        "composition": {"background": "obsidian violet depth", "midground": "nested curved sheets",
                        "foreground": "the nearest sheet's grazing edge", "focal": "the strongest interference",
                        "camera": "a slow arc through the sculpture"},
        "palette": {"dominant": P["indigo"], "secondary": P["royal"], "accent": P["purple"], "highlight": P["red"],
                    "background_value": "near black", "saturation": "low, red only where modes reinforce"},
        "tier": "one resonance field (ADR-1400) over a static carrier",
    }


# ================================================================================================ the instrument
# The music's hand on the sculpture (brief §7): audio excites the resonance's persistent modes (ADR-1400: the field hears
# the audio history itself, through the Response controls, ADR-1361); the routes below only choose WHICH figure leads
# and how strongly each family moves the sheets -- bounded, smoothed gains, never a raw level on a vertex.
CC = [  # brief §10: the performer's controls, MIDI CC 1-9 through `control.*`
    (1, "resonance", "RESONANCE: how hard the music drives the field (its sustained and struck excitation)"),
    (2, "scale", "FIELD SCALE: every wavelength shrinks -- finer folds and bands for the same music"),
    (3, "density", "CONTOUR DENSITY: the low-mid folds lead with their shorter wavelengths -- tighter nested folds"),
    (4, "interference", "INTERFERENCE: the two turned low-mid frames grow against the first -- more crossing folds"),
    (5, "relief", "SURFACE RELIEF: how far the field moves the sheets"),
    (6, "detail", "DETAIL: the highs' fine ridges"),
    (7, "spectral", "SPECTRAL COLOUR: the purple-to-red glow where the field rings"),
    (8, "light", "LIGHT RESPONSE: the outer sheets' crimson edge line"),
    (9, "motion", "MOTION SCALE: the modes' visible oscillation and the families' drift"),
]
CCR = {"attackMs": 120, "decayMs": 120}


def control_map():
    b = [{"source": "*", "channel": -1, "kind": "cc", "number": n, "signal": name} for n, name, _ in CC]
    return {"midi": {"enabled": True, "filter": "*", "bindings": b}}


def instrument(s, g):
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    F = "field/%s/resonance/" % FIELD
    st = lambda name: "field/%s/strength" % name
    # bass swells the broad lobes; held sound lets the standing figure accumulate (a longer memory) and deepens the
    # turned folds; the sound's brightness moves the low-mid focus (modal exchange: one wavelength leads, then
    # another); spectral change sets the mids' bands travelling further; the song's build lifts the whole relief
    s.route(R("response.bass", st("f-l"), 1.0, op="multiply", gain=1.3, offset=1.0, attackMs=90, decayMs=1100),
            R("response.sustain", F + "memory", 1.5, attackMs=2000, decayMs=5000),
            R("response.sustain", st("f-a2"), 1.0, op="multiply", gain=1.2, offset=1.0, attackMs=1500, decayMs=4000),
            R("response.sustain", st("f-a3"), 1.0, op="multiply", gain=1.2, offset=1.0, attackMs=1500, decayMs=4000),
            R("response.sustain", st("f-a1"), 1.0, op="multiply", gain=0.8, offset=1.0, attackMs=1500, decayMs=4000),
            R("sonic.brightness.slow", F + "source/1/focus", 0.07, attackMs=2500, decayMs=5000),
            R("response.flux", F + "source/2/drift", 8.0, attackMs=600, decayMs=2500),
            R("response.intensity", st("D-grid"), 1.0, op="multiply", gain=0.3, offset=1.0, attackMs=4000,
              decayMs=8000))
    # the nest breathes: bass parts the sheets (each moves along its normal in proportion to its place in the nest,
    # so they keep their order and spacing ratio) and the song's build holds them a little further apart
    n = len(levelsets.LEVELS)
    for i in range(n):
        k = i - 0.5 * (n - 1)
        path = "procedural/level%d/deform/4/amount" % i
        s.route(R("response.bass", path, round(SPREAD * k, 3), attackMs=120, decayMs=1400),
                R("response.intensity", path, round(1.5 * k, 3), attackMs=5000, decayMs=9000))
    s.control = control_map()
    s.route(R("control.resonance", F + "sustain", 1.5, **CCR), R("control.resonance", F + "transient", 1.2, **CCR),
            R("control.scale", F + "scale", 1.2, **CCR),
            R("control.density", F + "source/1/focus", 0.1, **CCR),
            R("control.interference", st("f-a2"), 1.0, op="multiply", gain=1.0, offset=1.0, **CCR),
            R("control.interference", st("f-a3"), 1.0, op="multiply", gain=1.0, offset=1.0, **CCR),
            R("control.relief", st("D-grid"), 1.0, op="multiply", gain=1.5, offset=1.0, **CCR),
            R("control.detail", F + "source/3/amplitude", 0.6, **CCR),
            R("control.motion", F + "breath", 0.3, **CCR))
    if NEUTRAL or not g.at.get("glow"):
        return
    # light follows the music, on separate time scales: the glow's brightness the level (dim in a quiet passage,
    # never off), the edges' the hats and snares (treble: fine highlights only)
    for prog in (g,):
        RP = "material/%s/op/%%d/ramp/%%s" % prog.name
        for k in ("constant2", "constant3"):
            s.route(R("response.level", RP % (prog["glow"], k), 1.0, op="multiply", gain=1.6, offset=0.4,
                      attackMs=150, decayMs=900),
                    R("response.hatEnv", RP % (prog["edgeTint"], k), 1.0, op="multiply", gain=1.0, offset=1.0,
                      attackMs=0, decayMs=200),
                    R("response.snareEnv", RP % (prog["edgeTint"], k), 1.0, op="multiply", gain=0.8, offset=1.0,
                      attackMs=0, decayMs=260),
                    R("control.spectral", RP % (prog["glow"], k), 1.0, op="multiply", gain=2.0, offset=1.0, **CCR),
                    R("control.light", RP % (prog["edgeTint"], k), 1.0, op="multiply", gain=1.5, offset=1.0, **CCR))


# THE CAMERA: an elevated three-quarter arc outside the sculpture's densest part, looking down into the nested sheets,
# so they fill the frame and recede (Stage B): never inside one opening looking down it (no portal), never so close
# that one blank sheet fills the frame
CAM_FOCUS = [0.0, -10.0, -10.0]
CAM_R, CAM_H, CAM_FOCAL, CAM_AZ, CAM_SWEEP = 140.0, 92.0, 24.0, 30.0, 70.0
FOG = 0.0045


def build():
    s = kit.Scene(SID, TITLE, design())
    environment(s)
    resonator(s)
    resonance_fields(s)
    g = surface_program("wdSurface")
    s.program(g.doc(base=g.base, emission=g.emit, opacity=g.opacity))
    write_level_meshes()
    level_sheets(s, g.name)
    s.camera = {"mode": 1, "position": [60.0, 90.0, 110.0], "target": CAM_FOCUS, "fov": 40.0, "orbitSpeed": 0.0}
    s.arc_camera(CAM_FOCUS, CAM_R, CAM_H, period=160.0, centre_deg=CAM_AZ, sweep_deg=CAM_SWEEP, steps=24, side=0.0,
                 lift=0.0, height_bob=6.0)
    s.params_({"camera/lens/focalLength": CAM_FOCAL})
    instrument(s, g)
    s.params_({"scene/volumeDensity": FOG})
    s.params_(POST if not NEUTRAL else {"scene/fogColor": [0.02, 0.02, 0.02],
                                        "post/bloom/enabled": False, "post/output/vignette": 0.0})
    return s


def write_scene():
    os.makedirs(OUT, exist_ok=True)
    s = build()
    pp, sp = s.write(OUT)
    if getattr(s, "grids", None):  # the kit has no grids; they go in the scene file's top level
        with open(sp) as f:
            doc = json.load(f)
        doc["grids"] = s.grids
        with open(sp, "w") as f:
            json.dump(doc, f, indent=1)
            f.write("\n")
    n_fields = sum(1 for n in s.nodes if n["kind"] == "field")
    assert n_fields <= 16, "%d fields: the GPU holds 16" % n_fields
    if os.environ.get("WD_OUT"):
        return pp
    kit.upsert_index_entry({
        "category": TITLE,
        "description": "Play live audio (any mix) and, optionally, a MIDI controller: "
                       + ", ".join("CC %d %s" % (n, d.split(":")[0].lower()) for n, _, d in CC) + ". "
                       + s.design["thesis"] + " See docs/prototypes/waveform-dimension/.",
        "live": True, "name": TITLE, "project": "%s/%s.json" % (SID, SID)})
    return pp


def variant(project, out, camera=None, params=None):
    """A review copy of the live project (its scene by absolute path). It names NO audio: feed one with avgen --audio."""
    with open(project) as f:
        doc = json.load(f)
    if camera:
        p, t = camera.split(":")
        doc["parameters"]["camera/position"] = [float(v) for v in p.split(",")]
        doc["parameters"]["camera/target"] = [float(v) for v in t.split(",")]
        doc["timeline"]["tracks"] = [tr for tr in doc["timeline"]["tracks"] if not tr["target"].startswith("camera/")]
    for k, v in (params or {}).items():
        doc["parameters"][k] = v
    base = os.path.dirname(os.path.abspath(project))
    doc["assets"]["scene"] = {"kind": "composition", "path": os.path.join(base, doc["assets"]["scene"]["path"])}
    with open(out, "w") as f:
        json.dump(doc, f, indent=1)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--camera")
    ap.add_argument("--set", action="append", default=[])
    a = ap.parse_args()
    pp = write_scene()
    print(pp)
    if a.variant:
        params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
        print(variant(pp, a.out, a.camera, params))


if __name__ == "__main__":
    main()

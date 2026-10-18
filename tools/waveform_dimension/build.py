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

NEUTRAL = os.environ.get("WD_NEUTRAL", "0") == "1"
DEBUG = os.environ.get("WD_DEBUG", "")   # review aids: albedo (the chromatic waves alone), normals   # a review aid: grey material, white light, no emission, no post

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


CELL = 1.8   # the level sheets' surface-nets cell (m): ~80k vertices in all (1.4 m cost ~7 ms more per pass live)


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
    # THE CHROMATIC WAVES (resonator-quality, the owner's colour-field addendum): the surface runs through the palette's
    # four gradient roles (g0..g3, a CYCLIC OKLab ramp, ADR-1470) along a coordinate of three layers --
    #   A  travelling bands: the position along WAVE_DIR, WAVE_N cycles across the sculpture, the phase a persistent
    #      integral of the music (it never resets; it drifts in silence) -- long chromatic waves through every sheet
    #   B  the families' signed displacement D: the bands bend and part where the field moves the sheets, so the colour
    #      shares the geometry's rhythm without its crests sitting on the colour boundaries (a phase offset)
    #   D  the sheet's place in the nest: each nested sheet a step along the gradient, so the nesting reads in colour
    g.op(None, "input", 0, inp="worldPosition")
    g.op("waveAxis", "gradient", 4, a=0, k=list(WAVE_DIR) + [0.5], f=1.0 / 300.0)   # 0 .. 1 across the sculpture
    g.op("wavePhase", "remap", 4, a=4, k=[0.0, 1.0, 0.0, WAVE_N])                 # cycles; the phase rides k.z/k.w
    g.op("waveBend", "field", 3, field="D-grid")
    g.op(None, "remap", 5, a=3, k=[0.0, 1.0, 0.0, WAVE_BEND])                       # metres -> cycles
    g.op(None, "add", 4, a=4, b=5)
    g.op(None, "remap", 5, a=1, k=[0.0, 1.0, 0.0, SHEET_STEP])                      # sheet index / 3 -> cycles
    g.op(None, "add", 4, a=4, b=5)
    g.op("albedo", "oklabRamp", 4, a=4, k=C("royal") + [1.0], k2=C("purple") + [1.0], k3=C("crimson") + [1.0],
         k4=C("indigo", 1.6) + [1.0], f=-1.0)
    if DEBUG == "fronts":  # review aid (overhaul stage A): the travelling fronts' displacement as grey bands, unlit
        g.op(None, "field", 3, field=os.environ.get("WD_FRONT_FIELD", "swell"))
        g.op(None, "remap", 4, a=3, k=[-float(os.environ.get("WD_FRONT_RANGE", "1.2")),
                                     float(os.environ.get("WD_FRONT_RANGE", "1.2")), 0.02, 0.9], f=1.0)
        g.op(None, "constant", 2, k=[0, 0, 0, 1])
        g.base, g.emit, g.opacity = 2, 4, -1
        return g
    if DEBUG == "normals":  # review aid (resonator-quality Phase C): the deformed normal as colour, unlit
        g.op(None, "input", 5, inp="normal")
        g.op(None, "remap", 4, a=5, k=[-1.0, 1.0, 0.0, 1.0], f=1.0)
        g.op(None, "constant", 2, k=[0, 0, 0, 1])
        g.base, g.emit, g.opacity = 2, 4, -1
        return g
    if DEBUG == "albedo":   # review aid (resonator-quality §9): the chromatic waves alone, unlit, as emission
        g.op(None, "constant", 2, k=[0, 0, 0, 1])
        g.base, g.emit, g.opacity = 2, 4, -1
        return g
    # the glow follows the field's contours, never painted spots: electric purple in a BAND where the families'
    # displacement reaches a given strength (|D| 3-6 m: a contour of the reinforcement, so it rings each lobe and runs
    # along the folds), spectral red only in the cores where it reinforces hardest (|D| > 9 m); a kick's shell adds its
    # crimson band as it passes. How bright it is follows the music's level (a route on the ramp). (Tried first: the
    # glow on the fold crests by screen-space convexity -- blocky; and |D| as a fill -- round blobs.)
    g.op(None, "multiply", 3, a=3, b=3)                           # r3 = |D|^2 (r3 = D, read above)
    # resonator-quality: the glow is a SMOOTH rise with the reinforcement (|D| from ~3.5 m to ~10 m), not the old
    # contour band (|D| 3-6 m, in and out) with separate cores, whose flat-edged rings read as colour banding beside
    # the chromatic waves; the accent still comes only where the families reinforce hardest (the ramp's top)
    g.op("glowT", "smoothstep", 5, a=3, k=[GLOW_IN ** 2, GLOW_OUT ** 2])
    g.op(None, "add", 5, a=5, b=6)
    g.op("glow", "ramp", 2, a=5, k=[0, 0, 0, 0], k2=C("purple", GLOW_GAIN[0]) + [0.5], k3=C("red", GLOW_GAIN[1]) + [1.0])
    # its brightness: a constant the music's level scales (routes), apart from the colours (the palette owns those)
    g.op("glowGain", "constant", 0, k=[1.0, 1.0, 1.0, 1.0])
    g.op(None, "multiply", 2, a=2, b=0)
    # the cut edges: a line on the outer sheets only, fading inward, lit by the glow; rose-white only where a primary
    # edge rings hardest (restrained: the line is narrow and dim at rest)
    g.op(None, "swizzle", 3, a=7, k=[0, 0, 0, 0])
    g.op("edgeLine", "smoothstep", 3, a=3, k=[2.2, 0.5])
    # every sheet's edge carries a dim royal-violet line, so the nesting reads in colour as it does in grey ...
    g.op("edgeBase", "ramp", 0, a=3, k=[0, 0, 0, 0], k2=C("royal", CONTOUR_GAIN[0]) + [0.5],
         k3=C("royal", CONTOUR_GAIN[1]) + [1.0])
    g.op(None, "add", 2, a=2, b=0)
    # ... and the outer sheets' a crimson one over it
    g.op("edgeWeight", "remap", 1, a=1, k=[0.0, 0.85, 1.0, 0.0], f=1.0)   # the outer sheets only
    g.op(None, "multiply", 3, a=3, b=1)
    g.op("edgeGain", "smoothstep", 1, a=5, k=[-0.3, 1.0])
    g.op(None, "multiply", 3, a=3, b=1)
    g.op("edgeTint", "ramp", 3, a=3, k=[0, 0, 0, 0], k2=C("crimson", EDGE_GAIN[0]) + [0.5],
         k3=C("rose", EDGE_GAIN[1]) + [1.0])
    g.op("edgeLevel", "constant", 1, k=[1.0, 1.0, 1.0, 1.0])     # the edges' brightness (hats, snares, CC 8)
    g.op(None, "multiply", 3, a=3, b=1)
    g.op(None, "add", 2, a=2, b=3)
    # roughness follows the field (stage C): where the families reinforce (the glow's rise, r5) the sheet is glossier,
    # elsewhere broad and soft -- gloss sits on the crests, not uniformly
    g.op("roughField", "remap", 0, a=5, k=[0.0, 1.0, 0.55, 0.25], f=1.0)
    g.base, g.emit, g.opacity, g.rough = 4, 2, -1, 0
    return g


def material(prog):
    m = {"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0, "roughness": 0.38,
         "metallic": 0.0, "doubleSided": True, "program": prog}
    return m   # opaque: the sheets are cut as geometry (levelsets.clip_mesh), never by a masked discard


# ================================================================================================ geometry
FRONT_RELIEF = 7.0
# the travelling fronts: name -> (source point, band, speed m/s, carrier wavelength m, smoothing s) and their relief
# (sources just beyond the sheets' bound: the GPU's audio ring holds 16 s, so the far side of the sculpture must be
# reached in less -- at 300 m / 30 m/s a source 320 m out sampled history the ring no longer had: a constant)
FRONTS = {"swell": ((-170.0, 30.0, -110.0), (0.0, 0.22), 30.0, 52.0, 0.12),
          "cross": ((160.0, -30.0, 120.0), (0.25, 0.55), 40.0, 30.0, 0.08)}
SWELL_RELIEF = float(os.environ.get("WD_SWELL", "5.0"))
CROSS_RELIEF = float(os.environ.get("WD_CROSS", "2.5"))   # metres a kick's shell lifts the sheets it passes through
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
            {"kind": "field", "field": "one", "amount": 0.0, "space": "world", "alongNormal": True},
            # THE TRAVELLING FRONTS (Resonant Visual Worlds, 05-plan §1.3, W1): the music of the last seconds laid out
            # in space as two families of spherical swells from sources far outside the sculpture, crossing it from
            # different sides -- a broad bass swell and a quicker mid one -- so the primary event is propagation
            {"kind": "field", "field": "swell", "amount": SWELL_RELIEF, "space": "world", "alongNormal": True},
            {"kind": "field", "field": "cross", "amount": CROSS_RELIEF, "space": "world", "alongNormal": True}]


# THE DOME (Resonant Visual Worlds, 05-plan §1.3, E1): the world beyond the sculpture is the same wave logic at a larger
# scale and a slower rate -- an inward-facing sphere about the sculpture whose own unlit program runs long chromatic
# bands (a cyclic OKLab ramp of the palette's dome roles) and carries the bass swell's fronts as they pass, dimmed and
# softened by the coloured air. Never a flat backdrop: it has the sculpture's colours, moves with the music, and the
# fronts sweep across it.
DOME_R = float(os.environ.get("WD_DOME_R", "330"))
DOME_ON = os.environ.get("WD_DOME", "1") == "1"
DOME_GAIN = float(os.environ.get("WD_DOME_GAIN", "0.22"))
DOME_DIR = (0.25, 0.9, 0.35)


def dome_mesh(radius, nu=64, nv=32):
    th = np.linspace(0.0, 2.0 * math.pi, nu + 1)
    ph = np.linspace(0.02, math.pi - 0.02, nv)
    T, Pp = np.meshgrid(th, ph, indexing="ij")
    d = np.stack([np.sin(Pp) * np.cos(T), np.cos(Pp), np.sin(Pp) * np.sin(T)], -1).reshape(-1, 3)
    pos = (d * radius + np.array(CAM_FOCUS)).astype(np.float32)
    nrm = (-d).astype(np.float32)                       # inward: the camera is inside
    uv = np.zeros((len(pos), 2), dtype=np.float32)
    idx = []
    for i in range(nu):
        for j in range(nv - 1):
            a, b, c, e = i * nv + j, (i + 1) * nv + j, (i + 1) * nv + j + 1, i * nv + j + 1
            idx += [a, c, b, a, e, c]                   # wound to face inward
    return pos, nrm, uv, np.array(idx, dtype=np.uint32)


def dome_program():
    g = Prog("wdDome")
    g.op(None, "input", 0, inp="worldPosition")
    g.op(None, "gradient", 1, a=0, k=list(_unit3(DOME_DIR)) + [0.5], f=0.5 / DOME_R)   # 0 .. 1 over the dome
    g.op("domePhase", "remap", 1, a=1, k=[0.0, 1.0, 0.0, 2.5])                        # 2.5 bands; phase on k.z/k.w
    g.op(None, "field", 2, field="swell")                                            # the bass fronts passing
    g.op(None, "remap", 2, a=2, k=[0.0, 1.0, 0.0, 0.18])
    g.op(None, "add", 1, a=1, b=2)
    g.op("dome", "oklabRamp", 3, a=1, k=C("indigo") + [1], k2=C("royal") + [1], k3=C("purple") + [1],
         k4=C("indigo", 1.5) + [1], f=-1.0)
    g.op(None, "constant", 4, k=[0, 0, 0, 1])
    g.base, g.emit, g.opacity = 4, 3, -1
    return g


def _unit3(v):
    n = math.sqrt(sum(x * x for x in v))
    return [x / n for x in v]


def level_sheets(s, prog):
    for i, _ in enumerate(levelsets.LEVELS):
        s.proc("level%d" % i, {"kind": "mesh", "asset": "meshes/wd-level%d.glb" % i}, material=material(prog),
               extra={"normalStep": 3.0}, deformers=deformers())   # 1.5 bake cells: smooth normals across cells


# ADR-1440: the slow families are baked once a frame into a volume grid that the deformer samples (2 m cells)
BAKE_BOUNDS = ([-128.0, -80.0, -128.0], [128.0, 80.0, 128.0])
D_RES = [128, 80, 128]


def travelling_fronts(s):
    """Each front family: a spectrum field (the band's level over the last seconds, laid out outward from its
    source at the travel speed: ADR-1116, exact under seek) times a spherical carrier moving at the same speed, so a
    loud moment becomes a crest that sweeps through the sheets and keeps going (05-plan §1.3)."""
    for name, (point, band, speed, wavelength, smooth) in FRONTS.items():
        field(s, name + "Hist", {"kind": "spectrum", "audioBand": "range", "bandLow": band[0], "bandHigh": band[1],
                                  "audioSpeed": speed, "audioSmooth": smooth, "point": list(point),
                                  "waveGeometry": "spherical", "strength": 1.0, "falloff": {"kind": "none"}})
        field(s, name + "Carrier", {"kind": "wave", "waveGeometry": "spherical", "waveShape": "sine",
                                     "point": list(point), "wavelength": wavelength, "waveSpeed": speed,
                                     "waveWidth": 0.0, "amplitude": 1.0, "strength": 1.0, "falloff": {"kind": "none"}})
        field(s, name, {"kind": "compound", "children": [name + "Hist", name + "Carrier"], "combine": "multiply",
                        "strength": 1.0, "falloff": {"kind": "none"}})


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
        # stage C (05-plan §1.3): narrow, cool reflection-only bands (ADR-1151) -- crisp lines that run along the
        # sheets' curvature like light on a wave, cooler and finer than Silk's soft satin box
        "bands": {"phase": 0.0, "rotation": 0.0, "gain": WD_BANDS_GAIN,
                  "softbox": {"intensity": 0.25, "azimuth": 0.9, "elevation": 0.6, "falloff": 3.0, "skyFill": 0.0},
                  "strips": [{"axis": [0.0, 1.0, 0.0], "offset": 0.25, "width": 0.03, "intensity": 2.0, "segments": 0,
                              "warmth": 0.0, "rate": 0.0},
                             {"axis": [0.6, 0.2, -0.77], "offset": 0.0, "width": 0.025, "intensity": 1.4, "segments": 0,
                              "warmth": 0.0, "rate": 0.0}]},
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
    # resonator-quality: the fill is a real second light now (0.35 left every face turned from the key near black):
    # from below and behind the key, in the palette's fill colour, so a turned-away face keeps its own colour
    s.light("fill", "directional", direction=[-0.4, 0.8, 0.3], color=C("royal"), intensity=FILL_I, castsShadow=False)
    # ... and a broad, soft light from the camera's side of the sculpture (along the arc's mean view): every face the
    # camera sees takes some diffuse light, so the chromatic waves read on the faces the key leaves (a restrained
    # fill, white leaning to the palette's key; the key, rim and fill keep the modelling)
    s.light("front", "directional", direction=FRONT_DIR, color=hexrgb("#d8d0ff"), intensity=FRONT_I, castsShadow=False)


POST = {
    "post/motionBlur/amount": 0.0, "post/dof/enabled": False, "post/dof/physical": False,
    "post/tonemap/chroma-retention": 0.95, "post/bloom/radius": 0.7, "post/bloom/intensity": 0.25,
    "post/bloom/threshold": 1.1, "post/output/vignette": float(os.environ.get("WD_VIGNETTE", "0.2")), "post/output/grain": 0.01,
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


# ================================================================================================ the palettes
# Eight designed palettes (ADR-1043 states, ADR-1420 selector): each names the same roles, so a palette is data and
# the program is one. The owner's indigo-violet with crimson is the first, brighter than the Stage D look; the
# others are vivid and clearly different, each a dominant hue family with one contrasting accent for the resonance.
# The selector moves between them on the music's phrases (beat.phrasePulse), never through the states between, in a
# composed order that alternates cool and warm, with a minimum hold and a several-second OKLab blend; with no tempo
# (live input) a large sustained change in the music advances it instead (ADR-1421).
#   bg       background, fog, the sky's zenith         shadow    the inner sheets (and the sky's horizon)
#   secondary the middle sheets                         primary   the outer sheets' lit faces
#   key / rim / fill  the lights                        band      the resonant glow's contour band
#   highlight the glow's reinforced cores               contour   every sheet's dim edge line
#   edge      the outer sheets' edge line               peak      the rarest accent, where a primary edge rings hardest
PALETTES = [
    ("Indigo Crimson", dict(bg="#0B0918", shadow="#2A1450", secondary="#46238A", primary="#6A35C2", key="#C4B2FF",
                            rim="#8A3AC0", fill="#482080", band="#9A3AD0", highlight="#F04455", contour="#7A52D8",
                            edge="#C32965", peak="#FF8B9A")),
    ("Ultramarine Gold", dict(bg="#060B20", shadow="#102266", secondary="#1C3CB0", primary="#3466FF", key="#D2DCFF",
                              rim="#5A86FF", fill="#1B2A70", band="#3D8BFF", highlight="#FFC63D", contour="#6E96FF",
                              edge="#F2A61C", peak="#FFF1C2")),
    ("Ember Rose", dict(bg="#150706", shadow="#44140E", secondary="#842A18", primary="#C94A24", key="#FFD0B0",
                        rim="#FF7444", fill="#5A1A2E", band="#FF5A36", highlight="#FFD166", contour="#E07A52",
                        edge="#FF9F5A", peak="#FFF0D6")),
    ("Teal Amber", dict(bg="#031515", shadow="#09342F", secondary="#0F5E57", primary="#169282", key="#B6FFEC",
                        rim="#2BD8C4", fill="#0E4A6B", band="#1FCDB8", highlight="#FFB347", contour="#3AB8A8",
                        edge="#E8873A", peak="#FFE2A8")),
    ("Magenta Lime", dict(bg="#13051B", shadow="#3F0C4A", secondary="#801A8A", primary="#C734BC", key="#FFC0F4",
                          rim="#FF55DC", fill="#4B1660", band="#E648CE", highlight="#B6FF3D", contour="#D070D8",
                          edge="#8CE62A", peak="#F2FFD0")),
    ("Glacier Violet", dict(bg="#0B0D1C", shadow="#20284E", secondary="#41569C", primary="#7590E0", key="#EEF3FF",
                            rim="#B094FF", fill="#2A2F66", band="#A585FF", highlight="#FF7AC6", contour="#9AB0F0",
                            edge="#C48BFF", peak="#FFE0F4")),
    ("Jade Coral", dict(bg="#04130D", shadow="#0C3826", secondary="#156B47", primary="#22AA6E", key="#D0FFE8",
                        rim="#45FFB6", fill="#0D3D3A", band="#32E8A4", highlight="#FF6F61", contour="#4CC690",
                        edge="#FF8F7A", peak="#FFE3DC")),
    ("Cyan Fuchsia", dict(bg="#06111B", shadow="#0C304A", secondary="#11618F", primary="#1BA8DC", key="#D4F7FF",
                          rim="#3FE6FF", fill="#2A1B5A", band="#26D6F0", highlight="#FF44A8", contour="#4AB8E0",
                          edge="#FF68B8", peak="#FFD8EE")),
]
PALETTE_ORDER = [0, 1, 2, 3, 4, 5, 6, 7]   # cool and warm alternate; the owner's palette opens
GLOW_GAIN = (float(os.environ.get("WD_GLOW0", "0.04")), float(os.environ.get("WD_GLOW1", "0.2")))                   # the glow ramp's middle and end, as fractions of band and highlight
CONTOUR_GAIN = (0.07, 0.14)
EDGE_GAIN = (0.2, 0.85)


# the chromatic waves' four stops per palette, from its own roles (the family stays the palette's): the middle sheets'
# colour, the outer sheets' lit colour, the glow band's hue, and a step toward the accent -- lifted to a common
# lightness and chroma so the body of every sheet carries colour between the light and the dark, blended in OKLab
# (kit.mix_oklab) and cycled by the ramp (g3 returns to g0 through the palette's own neighbours)
WAVE_L = (0.44, 0.54, 0.62, 0.5)      # the stops' OKLab lightness
WAVE_CHROMA = 1.2                      # chroma, relative to the roles'
WAVE_TURN = (-18.0, 0.0, None, 42.0)  # hue offsets (degrees) from the primary's hue; g2 keeps the band's own hue


def gradient_roles(roles):
    """Analogous stops only: the accent (highlight) stays in the glow -- a complementary accent in the ramp gave an
    olive-grey midpoint (Ultramarine Gold), the muddy intermediate the addendum forbids."""
    lin = {k: hexrgb(v) for k, v in roles.items()}
    _, cp, hp = kit.lch_of(lin["primary"])
    _, cs, _ = kit.lch_of(lin["secondary"])
    _, cb, hb = kit.lch_of(lin["band"])
    stops = [(cs, hp + WAVE_TURN[0]), (cp, hp), (cb, hb), (cp, hp + WAVE_TURN[3])]
    out = {}
    for i, (c, h) in enumerate(stops):
        out["g%d" % i] = [round(v, 5) for v in kit.oklch(WAVE_L[i], min(c * WAVE_CHROMA, 0.25), h)]
    # the front light: white leaning a quarter toward the palette's key
    out["front"] = [round(v, 5) for v in kit.mix_oklab([1.0, 1.0, 1.0], lin["key"], 0.35)]
    # the dome's bands: the same four hues, dimmed (it is the distance, and the air covers half of it)
    for i in range(4):
        out["d%d" % i] = [round(v * DOME_GAIN, 5) for v in out["g%d" % i]]
    return out


def palette_block(g, dg=None):
    """The project palette (ADR-1043) with its selector (ADR-1420): the roles bound to the lights, the sky, the fog and
    the surface program's colour constants."""
    op = lambda label, k: ("material/%s/op/%d/%s/%s" % (g.name, g[label], g.ops[g[label] - 1]["kind"], k)
                           if label in g.at else None)   # (a debug program lacks the light ops)
    bindings = [
        ("bg", "scene/fogColor", 1.0), ("bg", "env/sky/zenithColor", 0.6), ("shadow", "env/sky/horizonColor", 0.45),
        ("g0", op("albedo", "constant"), 1.0), ("g1", op("albedo", "constant2"), 1.0),
        ("g2", op("albedo", "constant3"), 1.0), ("g3", op("albedo", "constant4"), 1.0),
        ("key", "lights/key/color", 1.0), ("rim", "lights/rim/color", 1.0), ("fill", "lights/fill/color", 1.0),
        ("front", "lights/front/color", 1.0),
        ("band", op("glow", "constant2"), GLOW_GAIN[0]), ("highlight", op("glow", "constant3"), GLOW_GAIN[1]),
        ("contour", op("edgeBase", "constant2"), CONTOUR_GAIN[0]), ("contour", op("edgeBase", "constant3"), CONTOUR_GAIN[1]),
        ("edge", op("edgeTint", "constant2"), EDGE_GAIN[0]), ("peak", op("edgeTint", "constant3"), EDGE_GAIN[1]),
    ]
    if dg is not None:   # the dome's four dimmed stops
        bindings += [("d%d" % i, "material/%s/op/%d/oklabRamp/%s" % (dg.name, dg["dome"], k), 1.0)
                     for i, k in enumerate(("constant", "constant2", "constant3", "constant4"))]
    states = [{"name": name, "colors": dict({role: hexrgb(h) for role, h in roles.items()}, **gradient_roles(roles))}
              for name, roles in PALETTES]
    return {"states": states,
            "bindings": [{"role": r, "target": t, "gain": gn, "mode": "replace"} for r, t, gn in bindings if t],
            "saturation": 1.0, "value": 1.0,
            "selector": {"select": PALETTE_ORDER[0], "transition": 5.0, "lock": False, "hold": 24.0,
                         "advance": {"signal": "beat.phrasePulse", "threshold": 0.5, "order": PALETTE_ORDER,
                                     # ADR-1421: with no tempo (live input), a large sustained change in the music
                                     # advances instead, and a long uniform passage still evolves (maxHold)
                                     "change": {"signals": ["sonic.energy", "sonic.brightness", "sonic.density"],
                                                "fast": 3.0, "slow": 24.0, "threshold": 0.08, "rearm": 0.04,
                                                "primaryTimeout": 30.0, "maxHold": 75.0}}}}


# ================================================================================================ the instrument
# The music's hand on the sculpture (brief §7): audio excites the resonance's persistent modes (ADR-1400: the field hears
# the audio history itself, through the Response controls, ADR-1361); the routes below only choose WHICH figure leads
# and how strongly each family moves the sheets -- bounded, smoothed gains, never a raw level on a vertex.
CC = [  # brief §10: the performer's controls through `control.*`; CC 9 / 10 pick and lock the palette, as in every
    # Resonator scene, so motion scale sits on CC 11
    (1, "resonance", "RESONANCE: how hard the music drives the field (its sustained and struck excitation)"),
    (2, "scale", "FIELD SCALE: every wavelength shrinks -- finer folds and bands for the same music"),
    (3, "density", "CONTOUR DENSITY: the low-mid folds lead with their shorter wavelengths -- tighter nested folds"),
    (4, "interference", "INTERFERENCE: the two turned low-mid frames grow against the first -- more crossing folds"),
    (5, "relief", "SURFACE RELIEF: how far the field moves the sheets"),
    (6, "detail", "DETAIL: the highs' fine ridges"),
    (7, "spectral", "SPECTRAL COLOUR: the purple-to-red glow where the field rings"),
    (8, "light", "LIGHT RESPONSE: the outer sheets' crimson edge line"),
    (9, "palette", "PALETTE: choose one of the eight palettes"),
    (10, "paletteLock", "PALETTE LOCK: hold the chosen palette (no change on the phrase)"),
    (11, "motion", "MOTION SCALE: the modes' visible oscillation and the families' drift"),
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
    CP = "material/%s/op/%%d/constant/constant" % g.name
    glow, edge = CP % g["glowGain"], CP % g["edgeLevel"]
    s.route(R("response.level", glow, 1.0, op="multiply", gain=1.6, offset=0.4, attackMs=150, decayMs=900),
            R("control.spectral", glow, 1.0, op="multiply", gain=2.0, offset=1.0, **CCR),
            R("response.hatEnv", edge, 1.0, op="multiply", gain=1.0, offset=1.0, attackMs=0, decayMs=200),
            R("response.snareEnv", edge, 1.0, op="multiply", gain=0.8, offset=1.0, attackMs=0, decayMs=260),
            R("control.light", edge, 1.0, op="multiply", gain=1.5, offset=1.0, **CCR))
    # the chromatic waves travel: their phase integrates the music's energy over a floor (ADR-1041) -- never reset,
    # quicker in loud passages, still moving in silence (k.z and k.w of the phase remap move together)
    wp = "material/%s/op/%d/remap/constant" % (g.name, g["wavePhase"])
    s.route(*[R("sonic.energy", wp, WAVE_RATE, comp=c, gain=1.0, offset=0.35, integrate=True) for c in (2, 3)])
    # the palette: CC 9 selects one (0..1 across the states), CC 10 locks it (no automatic change), as in every Resonator scene
    s.route(R("control.palette", "palette/select", float(len(PALETTES) - 1), **CCR),
            R("control.paletteLock", "palette/lock", 1.0, **CCR))


# THE CAMERA: an elevated three-quarter arc outside the sculpture's densest part, looking down into the nested sheets,
# so they fill the frame and recede (Stage B): never inside one opening looking down it (no portal), never so close
# that one blank sheet fills the frame
# the chromatic waves (surface_program): their direction, cycles across the sculpture, how far the displacement bends
# them (cycles per metre of D), the step between nested sheets, and the phase's drift (cycles per second per unit of
# the integrated energy + floor)
FILL_I = float(os.environ.get("WD_FILL_I", "1.2"))
WD_BANDS_GAIN = float(os.environ.get("WD_BANDS", "0.5"))
GLOW_IN, GLOW_OUT = 3.5, 10.0   # metres of |D| over which the glow rises
FRONT_I = float(os.environ.get("WD_FRONT_I", "1.2"))
FRONT_DIR = [-0.4, -0.58, -0.71]   # from the arc's middle (azimuth 30 degrees, 92 m up) toward the focus
WAVE_DIR = (0.55, 0.35, -0.76)
WAVE_N = float(os.environ.get("WD_WAVE_N", "4.0"))
WAVE_BEND = float(os.environ.get("WD_WAVE_BEND", "0.08"))
SHEET_STEP = float(os.environ.get("WD_SHEET_STEP", "0.3"))
WAVE_RATE = float(os.environ.get("WD_WAVE_RATE", "0.02"))
CAM_FOCUS = [0.0, -10.0, -10.0]
CAM_R, CAM_H, CAM_FOCAL, CAM_AZ, CAM_SWEEP = 140.0, 92.0, 24.0, 30.0, 70.0
FOG = 0.0045
FOG_DOME = float(os.environ.get("WD_FOG", "0.0028"))   # thinner with the dome: the air softens it, never hides it


def build():
    s = kit.Scene(SID, TITLE, design())
    environment(s)
    resonator(s)
    resonance_fields(s)
    travelling_fronts(s)
    g = surface_program("wdSurface")
    s.program(g.doc(base=g.base, emission=g.emit, opacity=g.opacity, roughness=getattr(g, "rough", -1)))
    write_level_meshes()
    level_sheets(s, g.name)
    if DOME_ON and not NEUTRAL and not DEBUG:
        dg = dome_program()
        s.program(dg.doc(base=dg.base, emission=dg.emit, opacity=dg.opacity))
        s.dome_program = dg
        dpos, dnrm, duv, didx = dome_mesh(DOME_R)
        write_glb(os.path.join(MESH_DIR, "wd-dome.glb"), dpos, dnrm, duv, didx, "dome")
        s.proc("dome", {"kind": "mesh", "asset": "meshes/wd-dome.glb"},
               material={"baseColor": [0, 0, 0], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0, "roughness": 1.0,
                         "metallic": 0.0, "unlit": True, "doubleSided": True, "program": dg.name})
    s.camera = {"mode": 1, "position": [60.0, 90.0, 110.0], "target": CAM_FOCUS, "fov": 40.0, "orbitSpeed": 0.0}
    s.arc_camera(CAM_FOCUS, CAM_R, CAM_H, period=160.0, centre_deg=CAM_AZ, sweep_deg=CAM_SWEEP, steps=24, side=0.0,
                 lift=0.0, height_bob=6.0)
    s.params_({"camera/lens/focalLength": CAM_FOCAL})
    instrument(s, g)
    if not NEUTRAL:
        s.palette = palette_block(g, getattr(s, "dome_program", None))
    s.params_({"scene/volumeDensity": FOG if not (DOME_ON and not NEUTRAL) else FOG_DOME})
    if getattr(s, "dome_program", None) is not None:   # the dome's bands drift slower than the sheets' waves
        dp = "material/wdDome/op/%d/remap/constant" % s.dome_program["domePhase"]
        s.route(*[R("sonic.energy", dp, WAVE_RATE * 0.35, comp=c, gain=1.0, offset=0.35, integrate=True) for c in (2, 3)])
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
        "category": "Resonator",
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

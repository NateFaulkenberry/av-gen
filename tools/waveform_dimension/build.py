#!/usr/bin/env python3
"""WAVEFORM DIMENSION: the generator (docs/prototypes/waveform-dimension/).

    python3 tools/waveform_dimension/build.py                 # the scene, its meshes and its index entry
    python3 tools/waveform_dimension/build.py --variant --out <project.json> [--camera ...] [--set path=json]
    then: avgen --headless --project <project.json> --audio assets/audio/feline-footwear.wav --render ...

Writes examples/waveform-dimension/{waveform-dimension.json, waveform-dimension.scene.json, meshes/*.glb} and the
scene's entry in examples/index.json (WD_OUT=<dir> writes a look-development copy there instead, with no index entry).

A live scene: one three-dimensional field turned into a sculpture of nested curved sheets. The field has two parts:
  - the CARRIER (levelsets.py): the standing interference of three long spherical waves from far outside the frame.
    Its level sets are the sculpture's large, nested, curved sheets; a second, shorter interference (the APERTURE)
    opens them. Static: the sculpture's identity.
  - the RESONANCE (FieldKind::Resonance, ADR-1400): one resonator excited by the audio history, whose modes move the
    sheets (bass lobes, low-mid folds, mid bands, treble ridges) and light them.
Stage A prototypes (WD_PROTO, build-time):
  A  nested isosurfaces  -- the level sets of carrier + resonance, sphere-traced (SDF), true topology changes
  B  contour sheets      -- designed parametric sheets displaced along their normals by the resonance
  C  hybrid              -- the carrier's level sets extracted as meshes, opened by the aperture, displaced by the
                            resonance on the GPU (fixed topology)
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

PROTO = os.environ.get("WD_PROTO", "C")
NEUTRAL = os.environ.get("WD_NEUTRAL", "0") == "1"   # grey material, white light, no emission, no post
DEBUG = os.environ.get("WD_DEBUG", "")               # slice: planes through the field coloured by the resonance

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
     "gain": 1.0, "amplitude": 0.6, "breath": 0.35, "drift": 14.0, "driftPeriod": 53.0, "spin": -0.002,
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
    "b": ((0.45, 1.0), [40.0, 0.0, -30.0]),
}


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


def resonance_fields(s, only=None):
    """f-a1..a3, f-l, f-b: the families as signed slow figures (plus a little vibration); M = the low-mid triple; en =
    the excitation of the mids and highs (|fast figure|), for light; front = a transient's spherical wavefront.
    `only`: the readers to make (the SDF prototype has no room for the rest within the GPU's 16 fields)."""
    for name, ((lo, hi), rot) in READERS.items():
        if only and name not in only:
            continue
        reader(s, "f-" + name, "relief", {"reliefSand": 0.0, "reliefFigure": 1.0, "reliefVibration": 0.2,
                                          "listenLow": lo, "listenHigh": hi, "listenWidth": 0.03}, rotation=rot)
    compound(s, "M", ["f-a1", "f-a2", "f-a3"])
    reader(s, "en", "amplitude", {"listenLow": 0.45, "listenHigh": 1.0, "listenWidth": 0.03})
    # each low onset launches a spherical shell from the sculpture's middle, travelling out through the sheets and
    # decaying (ADR-1116; exact under seek)
    field(s, "front", {"kind": "onset", "falloff": {"kind": "none"}, "onsetSource": "low", "audioSpeed": 32.0,
                       "onsetWidth": 7.0, "onsetDecay": 1.1, "waveGeometry": "spherical", "point": [0.0, 0.0, 0.0],
                       "strength": 1.0})


# ================================================================================================ the carrier (engine)
def carrier_fields(s):
    """The carrier and aperture as engine `wave` fields (exactly levelsets.py's functions), for the SDF prototype."""
    for prefix, waves in (("w", levelsets.CARRIER), ("o", levelsets.APERTURE)):
        for i, (c, lam, a) in enumerate(waves):
            field(s, "%s%d" % (prefix, i), {"kind": "wave", "waveGeometry": "spherical", "point": list(c),
                                             "wavelength": lam, "amplitude": a, "speed": 0.0, "strength": 1.0,
                                             "falloff": {"kind": "none"}})


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


def sheet(fn, nu, nv, level=0.0):
    """A parametric sheet P = fn(u, v) over [0, 1]^2 on an nu x nv grid. UV.x is the distance to the sheet's edge in
    metres-ish units (the same convention as the level sheets: the material fades the rim), UV.y the sheet's index."""
    u, v = np.meshgrid(np.linspace(0.0, 1.0, nu), np.linspace(0.0, 1.0, nv), indexing="xy")
    pos = np.stack(fn(u, v), axis=-1).reshape(-1, 3)
    j, i = np.meshgrid(np.arange(nv - 1), np.arange(nu - 1), indexing="ij")
    a = (j * nu + i).ravel()
    b, c = a + 1, a + nu
    d = c + 1
    tri = np.concatenate([np.stack([a, c, b], 1), np.stack([b, c, d], 1)], axis=0)
    e1 = pos[tri[:, 1]] - pos[tri[:, 0]]
    e2 = pos[tri[:, 2]] - pos[tri[:, 0]]
    fnrm = np.cross(e1, e2)
    nrm = np.zeros_like(pos)
    for k in range(3):
        np.add.at(nrm, tri[:, k], fnrm)
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-12
    edge = np.minimum(np.minimum(u, 1 - u), np.minimum(v, 1 - v)).ravel() * 120.0
    uv = np.stack([edge, np.full(len(edge), level)], axis=1)
    return (pos.astype(np.float32), nrm.astype(np.float32), uv.astype(np.float32),
            tri.reshape(-1).astype(np.uint32))


def _wing(u, v):
    """The great wing: a saddle 190 m across, lifted at two opposite corners, its far edge curling over."""
    x = (u - 0.5) * 190.0
    z = (v - 0.5) * 150.0 - 10.0
    y = 0.0022 * (x * x - 1.3 * z * z) + 9.0 * np.sin(0.035 * x + 0.6) - 4.0
    y = y + 26.0 * np.clip((-z - 30.0) / 45.0, 0.0, 1.0) ** 2
    return x, y, z


def _arch(u, v):
    """The arch: a tall curved sheet sweeping 150 degrees round the sculpture's west side, leaning in at the top."""
    th = math.radians(100.0) + (u - 0.5) * math.radians(150.0)
    y = (v - 0.5) * 110.0
    r = 62.0 - 0.0016 * y * y + 7.0 * np.sin(2.3 * th + 0.02 * y)
    return r * np.cos(th) - 10.0, y + 4.0, r * np.sin(th) - 6.0


def _veil(u, v):
    """The veil: a broad, gently folded sheet crossing the wing on a diagonal, rising toward the east."""
    a = (u - 0.5) * 170.0
    b = (v - 0.5) * 120.0
    ca, sa = math.cos(math.radians(35.0)), math.sin(math.radians(35.0))
    x = a * ca - b * sa * 0.35
    z = a * sa + b * ca * 0.5 + 8.0
    y = 0.22 * b + 11.0 * np.sin(0.05 * a) * np.cos(0.04 * b) + 0.002 * a * a + 10.0
    return x, y, z


SHEETS = [("wing", _wing, 380, 300), ("arch", _arch, 360, 260), ("veil", _veil, 340, 240)]
CELL = float(os.environ.get("WD_CELL", "1.0"))   # the level sheets' surface-nets cell (m)


def write_parametric_meshes():
    for k, (name, fn, nu, nv) in enumerate(SHEETS):
        pos, nrm, uv, idx = sheet(fn, nu, nv, float(k))
        write_glb(os.path.join(MESH_DIR, "wd-%s.glb" % name), pos, nrm, uv, idx, name)


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


def surface_program(name, energy, cut):
    """The shared surface (one program for all three prototypes): blackened indigo, a step lighter on each nested
    sheet (related hues, different light); electric purple where the mids and highs ring hard, crimson where they ring
    hardest; a crimson wavefront when a kick's shell passes; a thin royal rim. `cut`: the sheet's UV.x is its
    distance to its cut edge -- the edge is cut there with sub-cell precision (opacity, masked) and traced with a
    crimson line that the resonance brightens. Lit by the scene's lights."""
    g = Prog(name)   # eight registers, r0..r7
    g.op(None, "input", 7, inp="uv")                              # r7 = (edge distance, sheet index)
    g.op("energy", "field", 0, field=energy)                     # r0 = excitation
    g.op("front", "field", 6, field="front")                     # r6 = the travelling shell
    if NEUTRAL:
        g.op(None, "constant", 4, k=[0.18, 0.18, 0.19, 1.0])
        g.op(None, "constant", 2, k=[0.0, 0.0, 0.0, 1.0])
        if cut:
            g.op(None, "swizzle", 5, a=7, k=[0, 0, 0, 0])
            g.op(None, "smoothstep", 5, a=5, k=[0.0, 0.6])
        g.base, g.emit, g.opacity = 4, 2, (5 if cut else -1)
        return g
    g.op(None, "swizzle", 1, a=7, k=[1, 1, 1, 1])
    g.op("sheetTone", "remap", 1, a=1, k=[0.0, 4.0, 0.0, 1.0], f=1.0)
    g.op("albedo", "ramp", 4, a=1, k=C("indigo") + [1.0], k2=C("indigo", 1.5) + [1.0], k3=C("royal", 0.9) + [1.0])
    g.op("bandMask", "smoothstep", 1, a=0, k=[0.6, 1.1])       # where it rings hard
    g.op("band", "ramp", 2, a=1, k=[0, 0, 0, 0], k2=C("purple", 0.05) + [0.5], k3=C("purple", 0.12) + [1.0])
    g.op("hotMask", "smoothstep", 3, a=0, k=[1.1, 1.6])         # where it rings hardest
    g.op("hot", "ramp", 3, a=3, k=[0, 0, 0, 0], k2=C("crimson", 0.15) + [0.5], k3=C("red", 0.45) + [1.0])
    g.op(None, "add", 2, a=2, b=3)
    g.op("frontMask", "smoothstep", 3, a=6, k=[0.15, 0.9])
    g.op("frontTint", "ramp", 3, a=3, k=[0, 0, 0, 0], k2=C("crimson", 0.25) + [0.5], k3=C("crimson", 0.6) + [1.0])
    g.op(None, "add", 2, a=2, b=3)                                # r2: emission
    if cut:
        g.op(None, "swizzle", 5, a=7, k=[0, 0, 0, 0])             # r5 = edge distance
        g.op("edgeLine", "smoothstep", 3, a=5, k=[2.6, 0.7])      # a narrow band inside the cut ...
        g.op(None, "smoothstep", 1, a=0, k=[0.1, 0.8])
        g.op(None, "add", 1, a=1, b=6)
        g.op(None, "multiply", 3, a=3, b=1)                       # ... brightened by the excitation and the shell
        g.op("edgeTint", "ramp", 3, a=3, k=[0, 0, 0, 0], k2=C("crimson", 0.35) + [0.5], k3=C("rose", 0.9) + [1.0])
        g.op(None, "add", 2, a=2, b=3)
        g.op("cut", "smoothstep", 5, a=5, k=[0.0, 0.6])           # r5: opacity (masked)
    g.op(None, "input", 6, inp="normal")
    g.op(None, "input", 7, inp="viewDirection")
    g.op("rim", "fresnel", 1, a=6, b=7, f=5.0)
    g.op("rimTint", "ramp", 1, a=1, k=[0, 0, 0, 0], k2=C("royal", 0.03) + [0.5], k3=C("royal", 0.07) + [1.0])
    g.op(None, "add", 2, a=2, b=1)
    g.base, g.emit, g.opacity = 4, 2, (5 if cut else -1)
    return g


def material(prog, cut=False):
    m = {"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0, "roughness": 0.38,
         "metallic": 0.0, "doubleSided": True, "program": prog}
    if cut:
        m["alphaMode"] = "mask"
        m["alphaCutoff"] = 0.5
    return m


# ================================================================================================ geometry, per prototype
RELIEF = {"M": 2.6, "f-l": 3.5, "f-b": 0.9}   # metres of normal displacement per unit figure


def deformers():
    return [{"kind": "field", "field": name, "amount": amount, "space": "world", "alongNormal": True}
            for name, amount in RELIEF.items()]


def parametric_sheets(s, prog):
    for name, _, _, _ in SHEETS:
        s.proc(name, {"kind": "mesh", "asset": "meshes/wd-%s.glb" % name}, material=material(prog, cut=True),
               extra={"normalStep": 0.5}, deformers=deformers())


def level_sheets(s, prog):
    for i, _ in enumerate(levelsets.LEVELS):
        s.proc("level%d" % i, {"kind": "mesh", "asset": "meshes/wd-level%d.glb" % i}, material=material(prog, cut=True),
               extra={"normalStep": 0.5}, deformers=deformers())


# A: the SDF. H = carrier + KAPPA_M M, its level sets sphere-traced; each level is plane + KAPPA (H - c) + lin (the
# plane trick: `lin` = -y - 500 cancels the plane's own y), shelled thin; the aperture cuts them and an outer sphere
# bounds them. The resonance is baked once a frame into a volume grid (ADR-1440) that every level samples (a direct
# march evaluated every mode at every step: 550 ms a frame at 1080p).
KAPPA = 12.0
KAPPA_M = 0.18
SDF_LEVELS = [-0.45, 0.25, 0.95]
BAKE_BOUNDS = ([-130.0, -80.0, -130.0], [130.0, 80.0, 130.0])
BAKE_RES = [128, 64, 128]


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


def iso_sdf(s, prog):
    field(s, "lin", {"kind": "linearGradient", "axis": [0.0, 1.0, 0.0], "length": 1000.0, "strength": -1000.0,
                     "falloff": {"kind": "none"}})
    bake_grid(s, "M", BAKE_RES, strength=KAPPA_M)
    carrier_fields(s)
    compound(s, "H", ["w0", "w1", "w2", "M-grid"])
    compound(s, "Ap", ["o0", "o1"])

    def plane_trick(ref, amount, const):
        """amount * ref + const, exactly (plane y - o, + amount ref, + lin = -y - 500; o = -500 - const)."""
        d = {"kind": "plane", "axis": [0.0, 1.0, 0.0], "offset": -500.0 - const}
        d = kit._unary("displaceField", d, amount=amount, reference=ref)
        return kit._unary("displaceField", d, amount=1.0, reference="lin")

    levels = [kit.sd_shell(1.0, plane_trick("H", KAPPA, -KAPPA * c)) for c in SDF_LEVELS]
    k_a = 12.0
    cut = plane_trick("Ap", -k_a, k_a * levelsets.APERTURE_THRESHOLD)
    tree = kit.sd_inter(kit.sd_union(*levels), cut, kit.sd_sphere(118.0))
    s.sdf("iso", tree, BAKE_BOUNDS[0], BAKE_BOUNDS[1], material=material(prog), max_steps=200, epsilon=0.001,
          step_scale=0.7, normal_epsilon=0.6, max_distance=320.0)


# ================================================================================================ the scene
def environment(s):
    s.environment = {
        "intensity": 0.03, "background": C("obsidian"), "fogColor": C("obsidian"),
        "volumeDensity": 0.0, "volumeMaxDistance": 0.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0,
        "shadowRange": 200.0, "ambientOcclusion": False, "contactShadows": False,
        "sky": {"enabled": True, "zenithColor": C("obsidian", 0.6), "horizonColor": C("indigo", 0.5),
                "groundColor": C("obsidian", 0.5), "haze": 0.3, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    if NEUTRAL:
        s.light("key", "directional", direction=[0.55, -0.45, -0.7], color=[1, 1, 1], intensity=2.4, castsShadow=False)
        s.light("fill", "directional", direction=[-0.6, 0.2, 0.5], color=[1, 1, 1], intensity=0.5, castsShadow=False)
        return
    # the key grazes from high left (a pale violet light), the rim comes from behind (crimson), a low royal fill
    s.light("key", "directional", direction=[0.62, -0.35, -0.7], color=hexrgb("#b9a6ff"), intensity=1.8,
            castsShadow=False)
    s.light("rim", "directional", direction=[-0.3, -0.15, 0.95], color=C("crimson"), intensity=0.25, castsShadow=False)
    s.light("fill", "directional", direction=[-0.4, 0.8, 0.3], color=C("royal"), intensity=0.3, castsShadow=False)


POST = {
    "post/motionBlur/amount": 0.0, "post/dof/enabled": False, "post/dof/physical": False,
    "post/tonemap/chroma-retention": 0.85, "post/bloom/radius": 0.7, "post/bloom/intensity": 0.25,
    "post/bloom/threshold": 1.1, "post/output/vignette": 0.4, "post/output/grain": 0.01,
    "post/grade/contrast": 1.15, "post/lens/chromaticAberration": 0.0, "camera/exposure/compensation": 0.0,
    "scene/volumeDensity": 0.0045, "scene/fogColor": C("obsidian"),
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


def debug_slice(s):
    """A review aid: three orthogonal planes through the middle, unlit, coloured by the low-mid triple M (blue
    negative, red positive)."""
    g = Prog("wdSlice")
    g.op(None, "field", 0, field="M")
    g.op(None, "remap", 1, a=0, k=[-2.0, 2.0, 0.0, 1.0], f=1.0)
    g.op(None, "ramp", 4, a=1, k=[0.05, 0.2, 1.0, 1.0], k2=[0.02, 0.02, 0.02, 1.0], k3=[1.0, 0.25, 0.05, 1.0])
    g.op(None, "constant", 2, k=[0, 0, 0, 1])
    s.program(g.doc(base=4, emission=2))
    pos, nrm, uv, idx = sheet(lambda u, v: ((u - 0.5) * 240.0, (v - 0.5) * 240.0, 0.0 * u), 360, 360)
    write_glb(os.path.join(MESH_DIR, "wd-slice.glb"), pos, nrm, uv, idx, "slice")
    for name, rot in (("sliceXY", [0, 0, 0]), ("sliceZY", [0, 90, 0]), ("sliceXZ", [90, 0, 0])):
        s.proc(name, {"kind": "mesh", "asset": "meshes/wd-slice.glb"}, rotation=rot,
               material={"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                         "roughness": 1.0, "metallic": 0.0, "unlit": True, "doubleSided": True, "program": "wdSlice"})


def build():
    s = kit.Scene(SID, TITLE, design())
    environment(s)
    resonator(s)
    resonance_fields(s, only=("a1", "a2", "a3") if PROTO == "A" and DEBUG != "slice" else None)
    if DEBUG == "slice":
        debug_slice(s)
        s.camera = {"mode": 1, "position": [90.0, 80.0, 170.0], "target": [0.0, 0.0, 0.0], "fov": 40.0,
                    "orbitSpeed": 0.0}
        s.params_({"camera/lens/focalLength": 24.0, "post/bloom/enabled": False})
        return s
    g = surface_program("wdSurface", "en", cut=PROTO != "A")
    s.program(g.doc(base=g.base, emission=g.emit, opacity=g.opacity))
    if PROTO == "A":
        iso_sdf(s, g.name)
    elif PROTO == "B":
        write_parametric_meshes()
        parametric_sheets(s, g.name)
    else:
        write_level_meshes()
        level_sheets(s, g.name)
    s.camera = {"mode": 1, "position": [40.0, 10.0, 150.0], "target": [-10.0, -5.0, 0.0], "fov": 40.0,
                "orbitSpeed": 0.0}
    s.arc_camera([-10.0, -5.0, 0.0], 150.0, 10.0, period=150.0, centre_deg=15.0, sweep_deg=60.0, steps=24, side=0.0,
                 lift=0.0, height_bob=4.0)
    s.params_({"camera/lens/focalLength": 28.0})
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    s.params_(POST if not NEUTRAL else {"scene/volumeDensity": 0.003, "scene/fogColor": [0.02, 0.02, 0.02],
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
        "description": "Play live audio (any mix). " + s.design["thesis"] + " See docs/prototypes/waveform-dimension/.",
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

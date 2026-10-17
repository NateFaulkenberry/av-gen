#!/usr/bin/env python3
"""WAVEFORM DIMENSION: the generator (docs/prototypes/waveform-dimension/).

    python3 tools/waveform_dimension/build.py                 # the scene, its meshes and its index entry
    python3 tools/waveform_dimension/build.py --variant --out <project.json> [--camera ...] [--set path=json]
    then: avgen --headless --project <project.json> --audio assets/audio/feline-footwear.wav --render ...

Writes examples/waveform-dimension/{waveform-dimension.json, waveform-dimension.scene.json, meshes/*.glb} and the
scene's entry in examples/index.json.

A live scene: one three-dimensional resonance field (FieldKind::Resonance, ADR-1400) excited by the audio history,
turned into a sculpture of curved surfaces. Stage A prototypes (WD_PROTO, build-time):
    A  nested isosurfaces  -- the field's level sets, sphere-traced (SDF displaceField over the field)
    B  contour sheets      -- designed curved sheets displaced along their normals by the field
    C  hybrid              -- the designed sheets, plus iso-membranes where the field is strongest
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

SID = "waveform-dimension"
TITLE = "Waveform Dimension"
# WD_OUT: write a look-development copy elsewhere (its own meshes; no index entry)
OUT = os.environ.get("WD_OUT") or os.path.join(REPO, "examples", SID)
MESH_DIR = os.path.join(OUT, "meshes")

PROTO = os.environ.get("WD_PROTO", "A")
ISO = os.environ.get("WD_ISO", "membrane")   # prototype A's level-set family: membrane | lobes | shells
ISO_C = float(os.environ.get("WD_C", "0.2"))
NEUTRAL = os.environ.get("WD_NEUTRAL", "0") == "1"   # grey material, white light, no emission, no post
DEBUG = os.environ.get("WD_DEBUG", "")      # slice: a vertical plane through the field coloured by F (blue -, red +)

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


# ================================================================================================ the field
# ONE RESONATOR, FOUR FAMILIES (a source each, so each keeps its own energy budget: competition and the limiter act per
# source). The dispersion axis gives every band its scale:
#   L   bass       u 0-0.2     point modes, 100-62 m: broad lobes about the chamber's middle -- they move the level
#   A   low mids   u 0.25-0.45 plane waves, 53-38 m   } three families of plane waves, each read in its own turned
#   B   mids       u 0.45-0.68 plane waves, 33-20 m   } frame: their sum is a curved, aperiodic relative of the
#   C   highs      u 0.7-1     plane waves, 17-9 m    } triply periodic minimal surfaces (three wavelengths, three
#                                                        directions, never a cubic grid), and its nodal surface F = 0
#                                                        is one continuous membrane weaving through space
# A lattice source's modes share one orientation, so a family keeps its direction while the music moves its weight
# between wavelengths. `drive` keeps every family faintly ringing in silence (idle reaches only the three lowest
# modes); `spin` turns each family's direction slowly, so the sculpture evolves over a phrase.
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
     "gain": 1.0, "amplitude": 0.45, "breath": 0.35, "drift": 7.0, "driftPeriod": 83.0, "spin": -0.002,
     "orders": [1], "focus": 0.53, "focusWidth": 0.07, "focusSweep": 0.07, "focusPeriod": 47.0, "focusFloor": 0.3,
     "drive": 0.3},
    {"kind": "lattice", "position": [0.0, 0.0, 0.0], "order": 1, "bandLow": 0.7, "bandHigh": 1.0, "reach": 0.0,
     "gain": 1.0, "amplitude": 0.18, "breath": 0.6, "drift": 4.0, "driftPeriod": 59.0, "spin": 0.003,
     "orders": [1, 2], "focus": 0.8, "focusWidth": 0.08, "focusSweep": 0.1, "focusPeriod": 37.0, "focusFloor": 0.2,
     "drive": 0.2},
]
# The readers. F (one compound) = the low-mid family A heard THREE times, in three frames whose wave directions lie
# 66-69 degrees apart (seed 11's lattice orientation), plus the bass lobes. Because the three readers share A's modes
# -- the same wavelength, the same phase -- their sum is coherent: the curved, aperiodic relative of a triply periodic
# minimal surface, whose nodal surface F = 0 is one membrane weaving through space with tunnels, saddles and openings.
# Fd (a second compound) = the mids family B (travelling interference bands, one direction, between the triple's), the
# highs family C (fine ridges), the transient front and the chamber.
# (name, listen band, frame rotation (degrees), strength, position)
CHAMBER = [0.0, 0.0, 0.0]
READERS = [
    ("f-a1", (0.25, 0.45), [0.0, 0.0, 0.0], 1.0, [0.0, 0.0, 0.0]),
    ("f-a2", (0.25, 0.45), [-60.0, 0.0, 30.0], 1.0, [0.0, 0.0, 0.0]),
    ("f-a3", (0.25, 0.45), [50.0, 0.0, -60.0], 1.0, [0.0, 0.0, 0.0]),
    ("f-l", (0.0, 0.2), [0.0, 0.0, 0.0], 1.0, CHAMBER),
]
DETAIL = [
    ("f-b", (0.45, 0.68), [40.0, 0.0, -30.0], 1.0, [0.0, 0.0, 0.0]),
    ("f-c", (0.7, 1.0), [20.0, 0.0, 50.0], 1.0, [0.0, 0.0, 0.0]),
]


def resonator(s):
    res = {
        "modes": LAYOUT, "layout": LAYOUT, "fLow": 40.0, "fHigh": 6000.0, "lambdaMax": 100.0, "lambdaMin": 9.0,
        "scale": 1.0, "selectivity": 3.0, "contrast": 0.55, "persistence": 3.0, "buildup": 0.3, "memory": 2.0,
        "sustain": 1.0, "transient": 0.7, "idle": 0.5, "breath": 0.1, "limit": 1.0, "competition": 1.2,
        "attack": [0.3, 0.12, 0.05], "release": [4.0, 2.0, 0.8],
        "sources": SOURCES, "channel": "displacement",
    }
    s.nodes.append({"name": FIELD, "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "resonance", "seed": 11, "strength": 1.0, "falloff": {"kind": "none"},
                              "resonance": res}})


def reader(s, name, channel, extra=None, rotation=None, strength=1.0, position=(0.0, 0.0, 0.0)):
    r = {"channel": channel}
    r.update(extra or {})
    n = {"name": name, "kind": "field", "position": [float(v) for v in position],
         "field": {"kind": "resonance", "reference": FIELD, "strength": float(strength), "falloff": {"kind": "none"},
                   "resonance": r}}
    if rotation:
        n["rotation"] = [float(v) for v in rotation]
    s.nodes.append(n)


FAMILIES = os.environ.get("WD_FAMILIES", "")  # comma list of reader names to keep (empty = all)
CHAMBER_SIZE = (38.0, 30.0, 3.2)              # radius, softness (m), strength (units of F)


def field_readers(s):
    """F = the triple and the bass (the signed slow figure plus a little vibration). Fd = the mids and highs families,
    the transient front and the chamber, a designed bias that keeps the middle open (the membrane cannot exist where
    |F| is lifted past c)."""
    keep = (lambda r: not FAMILIES or r[0][2:] in FAMILIES.split(","))
    readers = [r for r in READERS if keep(r)]
    detail = [r for r in DETAIL if keep(r)]
    for name, (lo, hi), rot, k, pos in readers + detail:
        reader(s, name, "relief", {"reliefSand": 0.0, "reliefFigure": 1.0, "reliefVibration": 0.2,
                                   "listenLow": lo, "listenHigh": hi, "listenWidth": 0.03}, rotation=rot, strength=k,
               position=pos)
    s.nodes.append({"name": "F", "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "compound", "children": [r[0] for r in readers], "combine": "add",
                              "strength": 1.0, "falloff": {"kind": "none"}}})
    # a transient's wavefront: a spherical shell launched by each low onset from the chamber's middle, travelling out
    # through the membranes and decaying (ADR-1116; exact under seek)
    s.nodes.append({"name": "front", "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "onset", "falloff": {"kind": "none"}, "onsetSource": "low", "audioSpeed": 30.0,
                              "onsetWidth": 6.0, "onsetDecay": 1.2, "waveGeometry": "spherical",
                              "point": CHAMBER, "strength": 0.6}})
    r_, soft, k = CHAMBER_SIZE
    s.nodes.append({"name": "chamber", "kind": "field", "position": CHAMBER, "scale": [1.5, 0.8, 1.1],
                    "field": {"kind": "sphere", "radius": r_, "softness": soft, "strength": k,
                              "falloff": {"kind": "none"}}})
    s.nodes.append({"name": "Fd", "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "compound", "children": [r[0] for r in detail] + ["front", "chamber"],
                              "combine": "add", "strength": 1.0, "falloff": {"kind": "none"}}})
    # the excitation (|fast figure|) of the plane-wave families, for the material's energy
    reader(s, "F-amp", "amplitude", {"listenLow": 0.25, "listenHigh": 1.0, "listenWidth": 0.03})


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


def sheet(fn, nu, nv):
    """A parametric sheet P = fn(u, v) over [0, 1]^2 on an nu x nv grid: positions, vertex normals, uvs, indices."""
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
    uv = np.stack([u.ravel(), v.ravel()], axis=1)
    return (pos.astype(np.float32), nrm.astype(np.float32), uv.astype(np.float32),
            tri.reshape(-1).astype(np.uint32))


def _wing(u, v):
    """The great wing: a saddle 190 m across, lifted at two opposite corners, its far edge curling over."""
    x = (u - 0.5) * 190.0
    z = (v - 0.5) * 150.0 - 10.0
    y = 0.0022 * (x * x - 1.3 * z * z) + 9.0 * np.sin(0.035 * x + 0.6) - 4.0
    y = y + 26.0 * np.clip((-z - 30.0) / 45.0, 0.0, 1.0) ** 2   # the far edge curls up
    return x, y, z


def _arch(u, v):
    """The arch: a tall curved sheet sweeping 150 degrees round the sculpture's west side, leaning in at the top."""
    th = math.radians(100.0) + (u - 0.5) * math.radians(150.0)
    y = (v - 0.5) * 110.0
    r = 62.0 - 0.0016 * y * y * 1.0 + 7.0 * np.sin(2.3 * th + 0.02 * y)
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


def write_meshes():
    for name, fn, nu, nv in SHEETS:
        pos, nrm, uv, idx = sheet(fn, nu, nv)
        write_glb(os.path.join(MESH_DIR, "wd-%s.glb" % name), pos, nrm, uv, idx, name)


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

    def doc(self, base=-1, emission=-1, roughness=-1):
        return {"name": self.name, "ops": self.ops, "baseColor": base, "metallic": -1, "roughness": roughness,
                "emission": emission, "emissionIntensity": 1.0, "opacity": -1}


def surface_program():
    """Stage A's shared surface: dark indigo where the field is quiet, royal violet as it rises; electric purple light
    where it rings, crimson only where it rings hardest. Lit by the scene's lights (the grazing key and the rim)."""
    g = Prog("wdSurface")
    g.op("amp", "field", 0, field="F-amp")                       # r0 = |F| (excitation)
    if NEUTRAL:
        g.op(None, "constant", 4, k=[0.18, 0.18, 0.19, 1.0])
        g.op(None, "constant", 2, k=[0.0, 0.0, 0.0, 1.0])
        g.base, g.emit = 4, 2
        return g
    g.op("albedo", "ramp", 4, a=0, k=C("indigo") + [1.0], k2=C("royal") + [1.0], k3=C("purple") + [1.0])
    g.op("bandMask", "smoothstep", 1, a=0, k=[0.55, 1.1])       # r1: where it rings
    g.op("hotMask", "smoothstep", 3, a=0, k=[1.0, 1.6])         # r3: where it rings hardest
    g.op("band", "ramp", 2, a=1, k=[0, 0, 0, 0], k2=C("purple", 0.25) + [0.5], k3=C("purple", 0.5) + [1.0])
    g.op("hot", "ramp", 5, a=3, k=[0, 0, 0, 0], k2=C("crimson", 0.4) + [0.5], k3=C("red", 0.9) + [1.0])
    g.op(None, "add", 2, a=2, b=5)                                # r2: emission
    g.op(None, "input", 6, inp="normal")
    g.op(None, "input", 7, inp="viewDirection")
    g.op("rim", "fresnel", 5, a=6, b=7, f=4.0)
    g.op("rimTint", "ramp", 5, a=5, k=[0, 0, 0, 0], k2=C("royal", 0.12) + [0.5], k3=C("royal", 0.25) + [1.0])
    g.op(None, "add", 2, a=2, b=5)
    g.base, g.emit = 4, 2
    return g


def material(prog):
    return {"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0, "roughness": 0.42,
            "metallic": 0.0, "doubleSided": True, "program": prog}


# ================================================================================================ geometry, per prototype
BOUND = 130.0
KAPPA = float(os.environ.get("WD_KAPPA", "2.5"))
STEP = float(os.environ.get("WD_STEP", "0.7"))


def iso_sdf(s, name, c, family, bounds=BOUND, clip=None):
    """The field's level sets as a sphere-traced SDF. d0 = KAPPA (F + Fd), by the plane trick: a plane y + 500, plus
    the fields, plus `lin` = -y - 500 (linear_y). Then
        membrane  |d0| - c        solid about the nodal surface F = 0: one thin membrane weaving through space
        lobes     c - |d0|        solid where the figure is strong (antinode lobes, bounded by F = +-c)
        shells    ||d0| - c| - t  thin shells on the level sets F = +-c
    KAPPA scales F toward a unit gradient so the march's steps are near the true distance; c is in F's units."""
    base = {"kind": "plane", "axis": [0.0, 1.0, 0.0], "offset": -500.0}
    d0 = kit._unary("displaceField", base, amount=KAPPA, reference="F")
    d0 = kit._unary("displaceField", d0, amount=KAPPA, reference="Fd")
    d0 = kit._unary("displaceField", d0, amount=1.0, reference="lin")
    c = c * KAPPA
    if family == "lobes":
        tree = kit.sd_diff(kit.sd_sphere(1.0e4), kit.sd_shell(2.0 * c, d0))
    elif family == "membrane":
        tree = kit.sd_shell(2.0 * c, d0)
    else:
        tree = kit.sd_shell(0.6, kit.sd_shell(2.0 * c, d0))
    if clip is not None:
        tree = kit.sd_inter(tree, clip)
    s.sdf(name, tree, [-bounds] * 3, [bounds] * 3, material=material("wdSurface"), max_steps=200,
          epsilon=0.001, step_scale=STEP, normal_epsilon=0.08)


def linear_y(s):
    """lin = -y - 500 for |y| < 500: a linear gradient of length 1000 and strength -1000 (strength (y / L + 0.5); both
    at their parameters' limits). It cancels the SDF plane's own y (iso_sdf)."""
    s.nodes.append({"name": "lin", "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "linearGradient", "axis": [0.0, 1.0, 0.0], "length": 1000.0,
                              "strength": -1000.0, "falloff": {"kind": "none"}}})


SHEET_RELIEF = (4.0, 2.5)   # metres per unit: the triple and bass (F), the mids family (f-b)


def sheets(s, names=None):
    for name, _, _, _ in SHEETS:
        if names and name not in names:
            continue
        s.proc(name, {"kind": "mesh", "asset": "meshes/wd-%s.glb" % name}, material=material("wdSurface"),
               extra={"normalStep": 0.35},
               deformers=[{"kind": "field", "field": "F", "amount": SHEET_RELIEF[0], "space": "world",
                           "alongNormal": True},
                          {"kind": "field", "field": "f-b", "amount": SHEET_RELIEF[1], "space": "world",
                           "alongNormal": True}])


# ================================================================================================ the scene
def environment(s):
    s.environment = {
        "intensity": 0.03, "background": C("obsidian"), "fogColor": C("obsidian"),
        "volumeDensity": 0.0, "skyIntensity": 1.0, "shadowRange": 200.0,
        "ambientOcclusion": False, "contactShadows": False,
        "sky": {"enabled": True, "zenithColor": C("obsidian", 0.6), "horizonColor": C("indigo", 0.5),
                "groundColor": C("obsidian", 0.5), "haze": 0.3, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    if NEUTRAL:
        s.light("key", "directional", direction=[0.55, -0.45, -0.7], color=[1, 1, 1], intensity=2.4, castsShadow=False)
        s.light("fill", "directional", direction=[-0.6, 0.2, 0.5], color=[1, 1, 1], intensity=0.5, castsShadow=False)
        return
    # the key grazes from high left (a pale violet light), the rim comes from behind (crimson), a low fill from below
    s.light("key", "directional", direction=[0.62, -0.35, -0.7], color=hexrgb("#c9b8ff"), intensity=2.6,
            castsShadow=False)
    s.light("rim", "directional", direction=[-0.3, -0.15, 0.95], color=C("crimson"), intensity=1.6, castsShadow=False)
    s.light("fill", "directional", direction=[-0.4, 0.8, 0.3], color=C("royal"), intensity=0.5, castsShadow=False)


POST = {
    "post/motionBlur/amount": 0.0, "post/dof/enabled": False, "post/dof/physical": False,
    "post/tonemap/chroma-retention": 0.85, "post/bloom/radius": 0.7, "post/bloom/intensity": 0.25,
    "post/bloom/threshold": 1.1, "post/output/vignette": 0.4, "post/output/grain": 0.01,
    "post/grade/contrast": 1.15, "post/lens/chromaticAberration": 0.0, "camera/exposure/compensation": 0.0,
    "scene/fogDensity": 0.006, "scene/fogColor": C("obsidian"),
}


def design():
    return {
        "category": "abstract",
        "thesis": "A vast indigo-violet sculpture of curved surfaces drawn by one three-dimensional resonance field: "
                  "the music excites its modes, and red spectral energy gathers where they reinforce.",
        "composition": {"background": "obsidian violet depth", "midground": "nested curved surfaces",
                        "foreground": "the nearest surface's grazing edge", "focal": "the strongest interference",
                        "camera": "a slow arc through the sculpture"},
        "palette": {"dominant": P["indigo"], "secondary": P["royal"], "accent": P["purple"], "highlight": P["red"],
                    "background_value": "near black", "saturation": "low, red only where modes reinforce"},
        "tier": "one resonance field (ADR-1400)",
    }


def debug_slice(s):
    """A review aid: three orthogonal planes through the field's middle, unlit, coloured by F (blue negative, white
    zero, red positive; saturating at +-SLICE), so the figure's level structure can be read directly."""
    lim = float(os.environ.get("WD_SLICE", "1.5"))
    g = Prog("wdSlice")
    g.op(None, "field", 0, field="F")
    g.op(None, "remap", 1, a=0, k=[-lim, lim, 0.0, 1.0], f=1.0)
    g.op(None, "ramp", 4, a=1, k=[0.05, 0.2, 1.0, 1.0], k2=[0.02, 0.02, 0.02, 1.0], k3=[1.0, 0.25, 0.05, 1.0])
    g.op(None, "palette", 5, a=0, k=[0.5, 0.5, 0.5, 0], k2=[0.5, 0.5, 0.5, 0], k3=[1.0 / 0.25] * 3, k4=[0, 0, 0, 0])
    g.op(None, "power", 5, a=5, f=40.0)
    g.op(None, "add", 4, a=4, b=5)
    g.op(None, "constant", 2, k=[0, 0, 0, 1])
    s.program(g.doc(base=4, emission=2))
    pos, nrm, uv, idx = sheet(lambda u, v: ((u - 0.5) * 180.0, (v - 0.5) * 180.0, 0.0 * u), 360, 360)
    write_glb(os.path.join(MESH_DIR, "wd-slice.glb"), pos, nrm, uv, idx, "slice")
    for name, rot in (("sliceXY", [0, 0, 0]), ("sliceZY", [0, 90, 0]), ("sliceXZ", [90, 0, 0])):
        s.proc(name, {"kind": "mesh", "asset": "meshes/wd-slice.glb"}, rotation=rot,
               material={"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                         "roughness": 1.0, "metallic": 0.0, "unlit": True, "doubleSided": True, "program": "wdSlice"})


def build():
    s = kit.Scene(SID, TITLE, design())
    environment(s)
    resonator(s)
    field_readers(s)
    if DEBUG == "slice":
        debug_slice(s)
        s.camera = {"mode": 1, "position": [70.0, 60.0, 130.0], "target": [0.0, 0.0, 0.0], "fov": 40.0,
                    "orbitSpeed": 0.0}
        s.params_({"camera/lens/focalLength": 24.0, "post/bloom/enabled": False})
        return s
    g = surface_program()
    s.program(g.doc(base=g.base, emission=g.emit))
    if PROTO == "A":
        linear_y(s)
        iso_sdf(s, "iso", ISO_C, ISO)
    elif PROTO == "B":
        write_meshes()
        sheets(s)
    else:
        write_meshes()
        sheets(s, ("wing", "arch"))
        linear_y(s)
        iso_sdf(s, "iso", ISO_C, "membrane")
    s.camera = {"mode": 1, "position": [0.0, 12.0, 92.0], "target": [-4.0, 0.0, 0.0], "fov": 40.0,
                "orbitSpeed": 0.0}
    s.arc_camera([-4.0, 0.0, 0.0], 92.0, 12.0, period=150.0, centre_deg=10.0, sweep_deg=70.0, steps=24, side=0.0,
                 lift=0.0, height_bob=4.0)
    s.params_({"camera/lens/focalLength": 28.0})
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    s.params_(POST if not NEUTRAL else {"scene/fogDensity": 0.004, "scene/fogColor": [0.02, 0.02, 0.02],
                                        "post/bloom/enabled": False, "post/output/vignette": 0.0})
    return s


def write_scene():
    os.makedirs(OUT, exist_ok=True)
    s = build()
    pp, sp = s.write(OUT)
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

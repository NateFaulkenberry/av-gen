#!/usr/bin/env python3
"""CYMATIC WORLD: the generator (docs/prototypes/cymatic-world/).

    python3 tools/cymatic_world/build.py                    # the scene, its meshes and its index entry
    python3 tools/cymatic_world/build.py --variant <wav> --out <project.json> [--camera ...] [--set ...]

A live scene of resonant terrain: a basalt basin whose ground's figure is drawn by a resonance field (FieldKind::
Resonance, ADR-1400) excited by the audio history -- sand ridges on the nodal lines, scoured antinodes.

The ground's geography comes from the engine's WorldMap (the terrain system's noise layers and ridge/valley/flat
features), probed through `avgen_world_preview --heights` at every vertex of one screen-density mesh, because the
resonance has to move it on the GPU and a terrain node's chunks take no deformers.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.sonic_vfx import kit, meshes  # noqa: E402
from tools.sonic_vfx.kit import R, hexrgb  # noqa: E402

OUT = os.path.join(REPO, "examples", "cymatic-world")
MESH_DIR = os.path.join(OUT, "meshes")   # the ground

# ================================================================================================ palette
# Night in a basalt basin: blue-black stone, indigo air, a violet rim on the horizon; the only saturated light is
# the field's own (cyan-teal on the nodal lines, violet in the energised hollows), and it is used sparingly.
P = {
    "stone": "#0b0d14",      # basalt (blue-black)
    "stoneLit": "#1b1930",   # its lit face
    "sand": "#232a38",       # the settled grit on the nodal ridges (cool slate)
    "sandHot": "#8fe8ff",    # the nodal line's light (cyan)
    "hollow": "#7a3cff",     # energy in the antinodes (violet)
    "zenith": "#05060f",
    "horizon": "#3b2a78",
    "glowHorizon": "#5b3aa6",
    "fog": "#0c0b1f",
    "moon": "#ffe9d6",       # the key: a cold moon, low, raking the ridges
    "rim": "#a77bff",
    "counter": "#5f8cff",    # the world's cool counter light (blue, a step from the violet rim)
}


def C(key, scale=1.0):
    return hexrgb(P[key], scale)



# ================================================================================================ the world map
def _ring(r, n=24, level=0.0, jitter=0.0, seed=1):
    pts = []
    for i in range(n + 1):
        a = i * 2.0 * math.pi / n
        rr = r * (1.0 + jitter * math.sin(3.0 * a + seed) * math.cos(5.0 * a - seed))
        pts.append([rr * math.cos(a), level, rr * math.sin(a)])
    return pts


def _features():
    f = []
    # an amphitheatre of terraces stepping down to the floor: each a flat ring at its own level, irregular so the
    # steps read as worn stone, not a stadium
    for k, (r, level, width) in enumerate([(0.0, -7.0, 72.0), (52.0, -2.5, 12.0), (68.0, 2.0, 10.0), (84.0, 7.0, 10.0)]):
        path = [[0.0, level, 0.0]] if r == 0.0 else _ring(r, 28, level, 0.06, k)
        f.append({"name": "terrace%d" % k, "kind": "flat", "path": path, "width": width, "amplitude": 0.0,
                  "falloff": 2.2 if r == 0.0 else 1.6, "flatten": 0.97 if r == 0.0 else 0.85,
                  "roughness": 0.05 if r == 0.0 else 0.2, "smoothing": 2 if r else 0})
    # the rim: a broken ring of ridges 105 m out, high behind the basin and low toward the camera's gap
    f.append({"name": "rim", "kind": "ridge", "path": _ring(105.0, 30, 0.0, 0.08, 4), "width": 30.0,
              "amplitude": 16.0, "falloff": 1.4, "flatten": 0.0, "roughness": 1.4, "smoothing": 2})
    f.append({"name": "rimBack", "kind": "ridge", "path": [[-90.0, 0.0, -80.0], [-20.0, 0.0, -125.0], [70.0, 0.0, -95.0]],
              "width": 36.0, "amplitude": 18.0, "falloff": 1.1, "flatten": 0.0, "roughness": 1.8, "smoothing": 3})
    # radial gullies cut through the terraces (folds that break the rings)
    for k, a in enumerate([0.9, 2.3, 3.6, 4.4, 5.6]):
        d = (math.cos(a), math.sin(a))
        f.append({"name": "gully%d" % k, "kind": "valley",
                  "path": [[d[0] * 38.0, 0.0, d[1] * 38.0], [d[0] * 80.0, 0.0, d[1] * 80.0],
                           [d[0] * 125.0, 0.0, d[1] * 125.0]],
                  "width": 9.0 + 2.0 * k % 5, "amplitude": 4.5, "falloff": 1.3, "flatten": 0.0, "roughness": 0.8,
                  "smoothing": 2})
    # outcrops on the floor: low basalt shoulders the figure has to flow around
    for k, (x, z, h) in enumerate([(-24.0, -30.0, 3.0), (33.0, -14.0, 2.5)]):
        f.append({"name": "outcrop%d" % k, "kind": "ridge", "path": [[x, 0.0, z]], "width": 9.0, "amplitude": h,
                  "falloff": 1.8, "flatten": 0.0, "roughness": 2.0, "smoothing": 0})
    # the gap in the rim toward the camera (+z): the way in, and a long view out
    f.append({"name": "gap", "kind": "valley", "path": [[0.0, 0.0, 70.0], [6.0, 0.0, 115.0], [18.0, 0.0, 170.0]],
              "width": 34.0, "amplitude": 12.0, "falloff": 0.9, "flatten": 0.0, "roughness": 0.6, "smoothing": 2})
    # the plateau beyond the rim breaks into a ring of mountains 500-1100 m out: layered silhouettes, every direction
    f.append({"name": "rangeNear", "kind": "ridge", "path": _ring(520.0, 18, 0.0, 0.18, 7), "width": 200.0,
              "amplitude": 70.0, "falloff": 0.9, "flatten": 0.0, "roughness": 2.5, "smoothing": 2})
    f.append({"name": "rangeFar", "kind": "ridge", "path": _ring(1050.0, 16, 0.0, 0.2, 11), "width": 380.0,
              "amplitude": 220.0, "falloff": 0.8, "flatten": 0.0, "roughness": 3.0, "smoothing": 2})
    return f


WORLD = {
    "name": "basin", "seed": 20261016, "size": [6000.0, 6000.0], "erosion": 0.6, "seaLevel": -1000.0,
    "layers": [
        {"frequency": 0.0035, "amplitude": 30.0, "ridged": 0.4, "warp": 40.0},
        {"frequency": 0.013, "amplitude": 8.0, "ridged": 0.8, "warp": 12.0},
        {"frequency": 0.05, "amplitude": 1.6, "ridged": 0.6, "warp": 2.0},
    ],
    "features": _features(),
}


def world_heights(points):
    """WorldMap heights at (x, z) points, from the engine (avgen_world_preview --heights), cached by content."""
    key = hashlib.sha1(json.dumps([WORLD, points.round(4).tolist()[:8], len(points), float(points.sum())],
                                  sort_keys=True).encode()).hexdigest()
    cache_dir = os.path.join(tempfile.gettempdir(), "cymatic-world-heights")
    os.makedirs(cache_dir, exist_ok=True)
    cache = os.path.join(cache_dir, key + ".npy")
    if os.path.exists(cache):
        return np.load(cache)
    wp = os.path.join(cache_dir, key + ".world.json")
    pin = os.path.join(cache_dir, key + ".in.txt")
    pout = os.path.join(cache_dir, key + ".out.txt")
    with open(wp, "w") as f:
        json.dump(WORLD, f)
    np.savetxt(pin, points, fmt="%.4f")
    tool = os.path.join(REPO, "build", "release", "tools", "avgen_world_preview")
    r = subprocess.run([tool, "--heights", pin, pout, wp, os.path.join(cache_dir, key + ".png"), "256"],
                       capture_output=True, text=True)
    if r.returncode not in (0, 3):
        raise SystemExit("world_preview failed: " + r.stderr[-400:])
    h = np.atleast_1d(np.loadtxt(pout, dtype=np.float64))
    assert len(h) == len(points), (len(h), len(points))
    np.save(cache, h)
    return h


# ================================================================================================ the ground mesh
def ring_radii(r_dense=70.0, step=0.36, r_max=2600.0, growth=1.5):
    """Ring radii: `step` apart out to r_dense, then growing as (r / r_dense)^growth (screen density for a camera
    near the basin), to the horizon."""
    rs = [0.0]
    r = 0.0
    while r < r_max:
        dr = step if r < r_dense else step * (r / r_dense) ** growth
        r = min(r + dr, r_max)
        rs.append(r)
    return rs


def ground_mesh():
    """One polar mesh over the whole world: rings `ring_radii` apart, each with a power-of-two vertex count that keeps
    its cells about square (doubling where it must, with a stitched transition row). Heights from the WorldMap."""
    radii = ring_radii()
    counts = []
    for r in radii:
        if r == 0.0:
            counts.append(1)
            continue
        dr = 0.36 if r < 70.0 else 0.36 * (r / 70.0) ** 1.5
        want = 2.0 * math.pi * r / dr
        n = 16
        while n < want and n < 2048:
            n *= 2
        counts.append(n)
    # rings may only keep or double their count, outward
    for i in range(len(counts) - 2, 0, -1):
        counts[i] = min(counts[i], counts[i + 1])
    for i in range(2, len(counts)):
        counts[i] = max(counts[i], counts[i - 1])
        if counts[i] > 2 * counts[i - 1]:
            counts[i] = 2 * counts[i - 1]
    xs, zs, first = [0.0], [0.0], [0]
    for r, n in zip(radii[1:], counts[1:]):
        first.append(len(xs))
        a = np.arange(n) * (2.0 * math.pi / n)
        xs.extend((r * np.cos(a)).tolist())
        zs.extend((r * np.sin(a)).tolist())
    pts = np.stack([np.array(xs), np.array(zs)], axis=1)
    hs = world_heights(pts)
    idx = []
    # the centre fan
    n1 = counts[1]
    for j in range(n1):
        idx += [0, first[1] + (j + 1) % n1, first[1] + j]
    for i in range(1, len(radii) - 1):
        na, nb = counts[i], counts[i + 1]
        fa, fb = first[i], first[i + 1]
        if na == nb:
            for j in range(na):
                a, b = fa + j, fa + (j + 1) % na
                c, d = fb + (j + 1) % nb, fb + j
                idx += [a, b, c, a, c, d]
        else:  # nb == 2 na: each inner edge faces two outer edges
            for j in range(na):
                a, b = fa + j, fa + (j + 1) % na
                o0, o1, o2 = fb + 2 * j, fb + (2 * j + 1) % nb, fb + (2 * j + 2) % nb
                idx += [a, o1, o0, a, b, o1, b, o2, o1]
    pos = np.stack([pts[:, 0], hs, pts[:, 1]], axis=1)
    tri = np.array(idx, dtype=np.int64).reshape(-1, 3)
    # Winding: counter-clockwise seen from above (+Y); flip any triangle that came out the other way.
    e1 = pos[tri[:, 1]] - pos[tri[:, 0]]
    e2 = pos[tri[:, 2]] - pos[tri[:, 0]]
    fn = np.cross(e1, e2)
    flip = fn[:, 1] < 0.0
    tri[flip] = tri[flip][:, [0, 2, 1]]
    fn[flip] = -fn[flip]
    nrm = np.zeros_like(pos)
    for k in range(3):
        np.add.at(nrm, tri[:, k], fn)
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-12
    return pos.astype(np.float32), nrm.astype(np.float32), tri.reshape(-1).astype(np.uint32)


def write_glb(path, pos, nrm, idx, name):
    """numpy arrays straight into meshes.write_glb's layout (POSITION, NORMAL, uint32 indices)."""
    import struct
    pb, nb, ib = pos.tobytes(), nrm.tobytes(), idx.tobytes()
    blob = pb + nb + ib
    blob += b"\0" * ((4 - len(blob) % 4) % 4)
    lo, hi = pos.min(axis=0).tolist(), pos.max(axis=0).tolist()
    gltf = {
        "asset": {"version": "2.0", "generator": "cymatic_world/build.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "materials": [{"name": name, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0.0,
                                                              "roughnessFactor": 1.0}}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2,
                                                  "material": 0, "mode": 4}]}],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pb), "target": 34962},
            {"buffer": 0, "byteOffset": len(pb), "byteLength": len(nb), "target": 34962},
            {"buffer": 0, "byteOffset": len(pb) + len(nb), "byteLength": len(ib), "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": int(len(pos)), "type": "VEC3", "min": lo, "max": hi},
            {"bufferView": 1, "componentType": 5126, "count": int(len(nrm)), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5125, "count": int(len(idx)), "type": "SCALAR"},
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


# ================================================================================================ programs
class Prog:
    """A material program assembled by label (the Digital Ocean generator's pattern): `op(label, ...)` appends one op
    and remembers its 1-based number, which is what a route's `material/<name>/op/<n>/...` path addresses."""

    def __init__(self, name):
        self.name = name
        self.ops = []
        self.at = {}

    def op(self, label, kind, dst, a=None, b=None, c=None, k=None, f=None, inp=None, seed=None, field=None,
           k2=None, k3=None, k4=None):
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
        if seed is not None:
            o["seed"] = int(seed)
        if field is not None:
            o["field"] = field
        self.ops.append(o)
        if label:
            assert label not in self.at, label
            self.at[label] = len(self.ops)
        assert len(self.ops) <= 48, "%s: more than 48 ops" % self.name
        return len(self.ops)

    def __getitem__(self, label):
        return self.at[label]

    def doc(self, base=-1, emission=-1, roughness=-1, metallic=-1, opacity=-1):
        return {"name": self.name, "ops": self.ops, "baseColor": base, "metallic": metallic, "roughness": roughness,
                "emission": emission, "emissionIntensity": 1.0, "opacity": opacity}


def variant(project, wav, out, camera=None, params=None):
    """A file project of the live scene driven by `wav` (the review renders): optionally another camera
    ("x,y,z:tx,ty,tz") and parameter overrides."""
    with open(project) as f:
        doc = json.load(f)
    if camera:
        p, t = camera.split(":")
        doc["parameters"]["camera/position"] = [float(v) for v in p.split(",")]
        doc["parameters"]["camera/target"] = [float(v) for v in t.split(",")]
    for k, v in (params or {}).items():
        doc["parameters"][k] = v
    base = os.path.dirname(os.path.abspath(project))
    doc["assets"]["scene"] = {"kind": "composition", "path": os.path.join(base, doc["assets"]["scene"]["path"])}
    doc["assets"]["audio"] = {"path": os.path.abspath(wav)}
    doc.get("sonic", {}).pop("live", None)
    with open(out, "w") as f:
        json.dump(doc, f, indent=1)
    return out



# ================================================================================================ the scenes
# One live scene: the resonant ground, its figure drawn by a resonance field (FieldKind::Resonance, ADR-1400) excited
# by the audio history. (The table keeps the generator open to more scenes on the same subsystem.)
SCENES = {"world": {"id": "cymatic-world", "title": "Cymatic World"}}
FIELD = "cymatic"
HEART = (0.0, 12.0, 0.0)          # the basin's voice: the low modes' centre, above the floor's middle
SECOND = (-23.0, 0.0, 9.0)        # the world's second centre (interference with the first)
GROUND_REL = "meshes/cw-ground.glb"


def _unit(v):
    n = math.sqrt(sum(x * x for x in v))
    return [x / n for x in v]


def ground_y(x, z):
    return float(world_heights(np.array([[x, z]], dtype=np.float64))[0])


def resonator(s, sources, **over):
    """The scene's resonator: shared defaults, the scene's sources and overrides."""
    res = {
        "modes": 14.0, "fLow": 40.0, "fHigh": 6000.0, "lambdaMax": 34.0, "lambdaMin": 5.0, "scale": 1.0,
        "selectivity": 3.0, "contrast": 0.7, "persistence": 2.8, "buildup": 0.3, "memory": 1.6,
        "sustain": 1.0, "transient": 0.9, "idle": 0.5, "breath": 0.16, "limit": 0.9, "competition": 3.0,
        "sources": sources, "channel": "displacement",
    }
    res.update(over)
    s.nodes.append({"name": FIELD, "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "resonance", "seed": 7, "strength": 1.0, "falloff": {"kind": "none"},
                              "resonance": res}})


def reader(s, name, channel, extra=None, frame=None):
    r = {"channel": channel}
    r.update(extra or {})
    f = {"kind": "resonance", "reference": FIELD, "strength": 1.0, "falloff": {"kind": "none"}, "resonance": r}
    f.update(frame or {})
    s.nodes.append({"name": name, "kind": "field", "position": [0, 0, 0], "field": f})


# THE SHAPE LANGUAGE (08-brief, Stage 2): the ground's height is built per SCALE, one reader and one deformer each,
# every reader listening to its band of the same designed bank (large u < 0.3, medium 0.28-0.7, fine > 0.68). The
# prototypes differ in how a scale's figure becomes geometry:
#   P1 structured modal field -- the signed standing figure F is the height (lobes up, opposite lobes down)
#   P2 nodal boundaries       -- the nodal lines stand as ridges (sand) between antinode basins (-|F|); medium: creases
#   P3 hybrid                 -- large: signed lobes with their nodal ridges; medium: nodal creases; fine: vibration
SCALES = {"large": (0.0, 0.3), "medium": (0.28, 0.7), "fine": (0.68, 1.0)}
LAYOUT = 22.0   # the bank's fixed size (ADR-1404): mode i sits at u = i / (LAYOUT - 1)
# DEBUG VIEWS (08-brief §16), build-time like the palettes: CW_DEBUG=
#   mode:<i>        the geometry of ONE mode (its nodal figure), every scale's reader listening to it alone
#   scale:<name>    only that scale's deformer (large, medium, fine)
#   signed          the material shows the signed standing figure F (blue down, red up) over the real geometry
#   nodal           the material shows the nodal lines (sand) of the whole figure, white on black
#   envelope        the material shows the excitation |F| (the fast figure's amplitude), black to orange
DEBUG = os.environ.get("CW_DEBUG", "")
PROTO = os.environ.get("CW_PROTO", "P2")   # Stage 2: P2 (nodal boundaries) chosen
SHAPES = {
    "P1": {"large": ({"reliefSand": 0.0, "reliefFigure": 1.0, "reliefVibration": 0.35}, 3.2),
           "medium": ({"reliefSand": 0.0, "reliefFigure": 1.0, "reliefVibration": 0.3}, 1.3),
           "fine": ({"reliefSand": 0.0, "reliefVibration": 1.0}, 0.25)},
    "P2": {"large": ({"reliefSand": 1.0, "sharpness": 0.5, "reliefAmplitude": -0.55, "reliefRound": 0.35,
                     "reliefVibration": 0.2}, 3.4),
           "medium": ({"reliefSand": 1.0, "sharpness": 0.5, "reliefAmplitude": -0.35, "reliefRound": 0.3}, 0.8),
           "fine": ({"reliefSand": 0.0, "reliefVibration": 1.0}, 0.25)},
    "P3": {"large": ({"reliefSand": 0.55, "sharpness": 0.7, "reliefFigure": 0.8, "reliefVibration": 0.25}, 3.2),
           "medium": ({"reliefSand": 1.0, "sharpness": 0.45, "reliefAmplitude": -0.3}, 1.2),
           "fine": ({"reliefSand": 0.0, "reliefVibration": 1.0}, 0.25)},
}


def ground_readers(s):
    """The material's shading reader (all scales), and one relief reader per scale. Returns the deformer layers."""
    reader(s, "shade", "shading", {"sharpness": 0.9, "reliefLineSharpness": 0.35})
    if DEBUG in ("signed", "nodal", "envelope"):
        reader(s, "dbgview", {"signed": "relief", "nodal": "sand", "envelope": "amplitude"}[DEBUG],
               {"reliefSand": 0.0, "reliefFigure": 1.0, "sharpness": 0.45} if DEBUG != "envelope" else {})
    layers = []
    only_mode = int(DEBUG.split(":")[1]) if DEBUG.startswith("mode:") else None
    only_scale = DEBUG.split(":")[1] if DEBUG.startswith("scale:") else None
    for name, (lo, hi) in SCALES.items():
        extra, amount = SHAPES[PROTO][name]
        if only_mode is not None:  # one mode's figure, drawn with the large scale's shaping
            u = only_mode / (LAYOUT - 1.0)
            extra, amount = SHAPES[PROTO]["large"]
            lo = hi = u
            if name != "large":
                continue
        if only_scale is not None and name != only_scale:
            continue
        r = {"sharpness": 0.9, "reliefLine": 0.0, "reliefLineSharpness": 0.5, "reliefVibration": 0.0,
             "listenLow": lo, "listenHigh": hi, "listenWidth": 0.04 if only_mode is None else 0.005}
        r.update(extra)
        reader(s, "relief-" + name, "relief", r)
        layers.append(("relief-" + name, amount))
    return layers


# ---- the colour language (the colour pass, 03-brief-color-lighting-art-pass.md) -------------------------------------
# Every palette names the same roles, so a palette is data and the program is one:
#   stone  the scoured basalt (the antinodes, most of the ground)     grit   the settled sand on the nodal ridges
#   key    the raking light (from the moon's quarter, low)            fill   its complement, from the opposite quarter
#   rim    grazing light at silhouettes and ridge lips (Fresnel)      sky    the hemisphere's fill on up-facing ground
#   haze   distance: far ground and walls fade into it (so far = world, never void)
#   line   the nodal line's light (emission, sparse)                  hollow energy in the scoured antinodes (emission)
PALETTES = {
    # A, ultraviolet resonance: ultramarine stone, violet grit, electric-blue key, violet fill, cyan-white light
    "A": {"stone": "#1a1d6b", "grit": "#5b3fd6", "key": "#8fb0ff", "fill": "#8a3dff", "rim": "#bff3ff",
          "sky": "#22206e", "haze": "#1d1757", "line": "#bdf6ff", "hollow": "#ff3dc8",
          "sheen": "#e6f7ff", "sheenI": 1.4, "keyI": 2.31, "fillI": 0.836, "rimI": 0.49, "skyI": 0.122, "lineI": 2.2, "hollowI": 0.087},
    # B, plasma interference: navy stone, teal grit, cyan key, violet-fuchsia fill, pink-white accents
    "B": {"stone": "#081236", "grit": "#0b4458", "key": "#62ecff", "fill": "#c13bff", "rim": "#ff8fe6",
          "sky": "#1a1250", "haze": "#2a2275", "line": "#d4fffb", "hollow": "#ff3fd2", "sheen": "#dffcff",
          "sheenI": 1.1,
          "keyI": 2.3, "fillI": 0.9, "rimI": 0.35, "skyI": 0.07, "lineI": 1.9, "hollowI": 0.5},
    # C, spectral heat: violet stone, indigo grit, electric-blue key, hot-pink fill, amber rim, coral hollows
    "C": {"stone": "#1d1150", "grit": "#3c2a9a", "key": "#6aa8ff", "fill": "#ff3f8f", "rim": "#ffb46b",
          "sky": "#1c1048", "haze": "#24114c", "line": "#a8f2ff", "hollow": "#ff6a3d",
          "sheen": "#fff1e0", "sheenI": 1.4, "keyI": 2.31, "fillI": 0.76, "rimI": 0.42, "skyI": 0.105, "lineI": 2.2, "hollowI": 0.1},
}
# G, GEOMETRY ONLY (a review aid, never shipped: CW_PALETTE=G): one neutral grey material, white light from the same
# key and fill directions, no emission and no colour routes -- the world's shape with its colour ignored
PALETTES["G"] = {"stone": "#5c5f66", "grit": "#5c5f66", "key": "#ffffff", "fill": "#c8ccd4", "rim": "#ffffff",
                 "sky": "#9aa0aa", "haze": "#2c2f36", "line": "#000000", "hollow": "#000000", "sheen": "#ffffff",
                 "sheenI": 0.35, "keyI": 1.5, "fillI": 0.55, "rimI": 0.12, "skyI": 0.18, "lineI": 0.0, "hollowI": 0.0}
PALETTE = os.environ.get("CW_PALETTE", "B")   # chosen in Pass B (plasma interference)
KEY_DIR = (0.93, -0.2, -0.3)        # the direction the key travels: low, from the left-front quarter
FILL_DIR = (-0.8, -0.32, 0.5)       # the fill travels the other way: lights the flanks the key leaves dark


def pc(key, scale=1.0):
    return hexrgb(PALETTES[PALETTE][key], scale)


def ground_program():
    """The basin's ground, UNLIT with its light painted in the program (the lit path cost ~7 ms at 1080p for a
    surface whose light is a few terms). One `field` op reads the resonance's shading channel (sand, crest,
    amplitude) in one pass over the modes. Two materials (scoured stone, settled grit); a raking key and its
    complementary fill from the opposite quarter, so a ridge's two flanks take two colours; a grazing rim; a
    hemisphere sky fill so shadows hold detail; distance haze so the far walls read as world; and two emissions that
    follow the resonance: a thin light on the nodal line where the ground rings, and a low glow in the hollows that
    ring hardest."""
    P_ = PALETTES[PALETTE]
    dbg = os.environ.get("CW_DEBUG", "")
    g = Prog("cwGround")
    if dbg in ("signed", "nodal", "envelope"):  # the debug views' material (08-brief §16): the field, lit by the key
        g.op("view", "field", 0, field="dbgview")
        if dbg == "signed":
            g.op(None, "remap", 1, a=0, k=[-0.6, 0.6, 0.0, 1.0], f=1.0)
            g.op(None, "ramp", 4, a=1, k=[0.1, 0.25, 1.0, 1.0], k2=[0.85, 0.85, 0.85, 1.0], k3=[1.0, 0.2, 0.1, 1.0])
        elif dbg == "nodal":
            g.op(None, "remap", 1, a=0, k=[0.0, 0.6, 0.0, 1.0], f=1.0)
            g.op(None, "ramp", 4, a=1, k=[0.02, 0.02, 0.03, 1.0], k2=[0.3, 0.3, 0.32, 1.0], k3=[1.0, 1.0, 1.0, 1.0])
        else:
            g.op(None, "remap", 1, a=0, k=[0.0, 1.5, 0.0, 1.0], f=1.0)
            g.op(None, "ramp", 4, a=1, k=[0.0, 0.0, 0.0, 1.0], k2=[0.6, 0.15, 0.02, 1.0], k3=[1.0, 0.75, 0.2, 1.0])
        g.op(None, "input", 6, inp="normal")
        g.op(None, "gradient", 5, a=6, k=_unit([-x for x in KEY_DIR]) + [0.0], f=0.6)
        g.op(None, "constant", 7, k=[0.45, 0.45, 0.45, 0.0])
        g.op(None, "add", 5, a=5, b=7)
        g.op(None, "multiply", 4, a=4, b=5)
        g.op(None, "constant", 2, k=[0, 0, 0, 1])
        g.base, g.emit = 4, 2
        return g

    def tint(label, dst, src, key, scale=1.0):
        # scalar (0..1) x colour in ONE op: a ramp from black through half the colour to the colour is exactly
        # t x colour on [0, 1] (the interpreter costs ~0.36 ms per op at 1080p High, so a constant + multiply pair
        # is twice the price). A route scales the colour through constant2 AND constant3 (see ramp_paths).
        c = pc(key, scale)
        g.op(label, "ramp", dst, a=src, k=[0, 0, 0, 0], k2=[x * 0.5 for x in c] + [0.5], k3=c + [1.0])

    g.op("shade", "field", 0, field="shade")                       # r0 = (sand, crest, amplitude, w)
    g.op(None, "swizzle", 2, a=0, k=[1, 1, 1, 1])                   # r2 = crest
    g.op(None, "swizzle", 3, a=0, k=[2, 2, 2, 2])                   # r3 = amplitude
    g.op(None, "swizzle", 1, a=0, k=[0, 0, 0, 0])                   # r1 = sand
    g.op("gritMask", "smoothstep", 1, a=1, k=[0.12, 0.5])          # r1: where grit has settled (the ridges)
    # ---- emission, into r2: the nodal line's light where the ground rings, and a glow in the hardest-ringing hollows
    g.op("lineMask", "smoothstep", 2, a=2, k=[0.25, 0.7])
    g.op(None, "multiply", 2, a=2, b=3)                            # crest x ringing (unbounded: a pair, not a ramp)
    g.op("line", "constant", 7, k=pc("line", P_["lineI"]) + [1.0])
    g.op(None, "multiply", 2, a=2, b=7)
    g.op("stoneMask", "remap", 7, a=1, k=[0.0, 1.0, 1.0, 0.0], f=1.0)   # r7: scoured stone (no grit)
    g.op("hollowRamp", "smoothstep", 3, a=3, k=[0.8, 2.4])          # only where it rings hard
    g.op(None, "multiply", 3, a=3, b=7)
    # ... and only on the inner walls of a cell: a band of the sand field just inside the grit, so the glow's edges
    # follow the relief (the sand's contours ARE the cell walls), never an unshaped wash over flat ground
    g.op(None, "swizzle", 4, a=0, k=[0, 0, 0, 0])
    g.op("wallBand", "smoothstep", 4, a=4, k=[0.035, 0.08])
    g.op(None, "multiply", 3, a=3, b=4)
    tint("hollow", 3, 3, "hollow", P_["hollowI"])
    g.op(None, "add", 2, a=2, b=3)                                 # r2: emission
    # ---- materials: scoured satin stone, matte settled grit (one ramp: stone -> grit by the grit mask) ----
    st, gr = pc("stone"), pc("grit")
    g.op("albedo", "ramp", 4, a=1, k=st + [1.0], k2=[(x + y) * 0.5 for x, y in zip(st, gr)] + [1.0], k3=gr + [1.0])
    # ---- light, into r5 (sheen into r1) ----
    g.op(None, "input", 6, inp="normal")
    g.op("keyDot", "gradient", 1, a=6, k=_unit([-x for x in KEY_DIR]) + [0.0], f=1.0)
    tint("key", 5, 1, "key", P_["keyI"])                           # the raking key
    g.op("sheenLobe", "power", 1, a=1, f=12.0)                     # the key's highlight ...
    g.op(None, "multiply", 1, a=1, b=7)                            # ... on the smooth stone only
    tint("sheen", 1, 1, "sheen", P_["sheenI"])                     # r1: sheen
    g.op("fillDot", "gradient", 3, a=6, k=_unit([-x for x in FILL_DIR]) + [0.0], f=1.0)
    g.op(None, "power", 3, a=3, f=4.0)                             # the fill grazes: a narrow lobe on the far flanks
    tint("fill", 3, 3, "fill", P_["fillI"])
    g.op(None, "add", 5, a=5, b=3)                                 # + fill
    g.op("skyDot", "gradient", 3, a=6, k=[0.0, 0.5, 0.0, 0.5], f=1.0)   # 0.5 + 0.5 N.y: the hemisphere
    tint("sky", 3, 3, "sky", P_["skyI"])
    g.op(None, "add", 5, a=5, b=3)                                 # + sky
    g.op("rimMask", "fresnel", 3, f=3.0)
    tint("rim", 3, 3, "rim", P_["rimI"])
    g.op(None, "add", 5, a=5, b=3)                                 # + rim
    if dbg == "light":
        g.op(None, "add", 4, a=5, b=1)                             # the light alone (on white)
    elif dbg != "albedo":
        g.op(None, "multiply", 4, a=4, b=5)                        # lit colour
        g.op(None, "add", 4, a=4, b=1)                             # + sheen
    # ---- distance haze: the far ground and walls fade into the world's air ----
    g.op(None, "input", 3, inp="cameraDistance")
    g.op("hazeRamp", "remap", 3, a=3, k=[35.0, 280.0, 0.0, 0.7], f=1.0)
    g.op("haze", "constant", 7, k=pc("haze", 1.0) + [1.0])
    g.op(None, "mixBy", 4, a=4, b=7, c=3)
    if dbg in ("albedo", "light"):
        g.op(None, "constant", 2, k=[0, 0, 0, 1])
    g.base, g.emit = 4, 2
    return g


def environment(s):
    s.environment = {
        "intensity": 0.05, "background": C("zenith"), "fogColor": C("fog"),
        "volumeDensity": 0.0, "fogHeight": 12.0, "fogHeightFalloff": 0.04, "skyIntensity": 1.0,
        "shadowRange": 180.0,
        "sky": {"enabled": True, "zenithColor": C("zenith"), "horizonColor": C("horizon"),
                "groundColor": C("fog"), "haze": 0.45, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    # no scene lights: the ground is unlit, its key, fill, sky and rim are painted in its program (ground_program)


def ground(s, layers, write_mesh=False):
    """`layers`: (reader name, metres per unit), one Field deformer each -- the ground's scales, in order."""
    if write_mesh:
        pos, nrm, idx = ground_mesh()
        write_glb(os.path.join(MESH_DIR, "cw-ground.glb"), pos, nrm, idx, "ground")
        print("ground: %d vertices, %d triangles, %.1f MB" % (len(pos), len(idx) // 3,
                                                              os.path.getsize(os.path.join(MESH_DIR, "cw-ground.glb")) / 1e6))
    g = ground_program()
    s.program(g.doc(base=g.base, emission=g.emit))
    s.proc("ground", {"kind": "mesh", "asset": GROUND_REL},
           material={"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                     "roughness": 0.6, "metallic": 0.0, "unlit": True,
                     "program": "" if os.environ.get("CW_NOPROG") else g.name},
           extra={"normalStep": 0.12},  # ADR-1401: the mesh is 5 km wide; its auto step would be 3.7 m
           deformers=[{"kind": "field", "field": name, "amount": amount, "axis": [0.0, 1.0, 0.0], "space": "world",
                       "alongNormal": False} for name, amount in layers])
    return g


def control_map(cc, pads):
    b = [{"source": "*", "channel": -1, "kind": "cc", "number": n, "signal": name} for n, name, _ in cc]
    b += [{"source": "*", "channel": -1, "kind": "noteEvent", "number": n, "signal": name} for n, name, _ in pads]
    return {"midi": {"enabled": True, "filter": "*", "bindings": b}}


CC_FIELD = [  # CC 1-4: the field itself, the same in all three scenes
    (1, "excite", "EXCITATION: how hard the music drives the field (its sustained and struck excitation)"),
    (2, "complexity", "COMPLEXITY: more modes ring at once -- finer, more intricate figures"),
    (3, "persistence", "PERSISTENCE: the field rings longer and remembers longer"),
    (4, "scale", "SPATIAL SCALE: every wavelength shrinks -- the same music draws a finer figure"),
]
PAD_STRIKE = (36, "padStrike", "STRIKE: the field's lowest modes swell, as if struck, and ring down")
CCR = {"attackMs": 120, "decayMs": 120}


def field_instrument(s):
    """What every scene shares: the Response controls, the song's build and held sound on the field, CC 1-4, the
    strike pad. The field hears the audio history itself (ADR-1400, heard through the Response controls, ADR-1361)."""
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    F = "field/%s/resonance/" % FIELD
    s.route(R("intensity", F + "modes", 5.0, attackMs=4000, decayMs=8000))      # the build: more modes ring
    s.route(R("sustain", F + "memory", 1.2, attackMs=2000, decayMs=5000))       # held sound: a longer memory
    s.route(R("control.excite", F + "sustain", 1.5, **CCR), R("control.excite", F + "transient", 1.2, **CCR))
    s.route(R("control.complexity", F + "modes", 10.0, **CCR))
    s.route(R("control.persistence", F + "persistence", 5.0, **CCR),
            R("control.persistence", F + "memory", 3.0, **CCR))
    s.route(R("control.scale", F + "scale", 1.2, **CCR))
    s.route(R("control.padStrike", F + "idle", 2.5, attackMs=30, decayMs=3500))
    return F


POST = {
    "post/motionBlur/amount": 0.0, "post/dof/physical": False,
    # Pass E: AgX keeps more of the cyan and magenta (chroma retention 0.9: the hottest nodal lines stay tinted rather
    # than clip to white); bloom only around what is genuinely bright, tight, so neighbouring cells stay separate
    "post/tonemap/chroma-retention": 0.9, "post/bloom/radius": 0.6,
    "post/bloom/intensity": 0.22, "post/bloom/threshold": 1.2, "post/output/vignette": 0.45,
    "post/output/grain": 0.012, "post/grade/contrast": 1.22, "post/lens/chromaticAberration": 0.0,
    "camera/exposure/compensation": 0.2, "post/dof/enabled": False,
}


def design(thesis, focal, foreground, camera):
    return {
        "category": "abstract", "thesis": thesis,
        "composition": {"background": "none: the camera looks down into the basin, the ground fills the frame",
                        "midground": "the outer rings of the figure", "foreground": foreground, "focal": focal,
                        "camera": camera},
        "palette": {"dominant": P["stone"], "secondary": P["sand"], "accent": P["sandHot"], "highlight": P["hollow"],
                    "background_value": "near black", "saturation": "low, with selective cyan and violet"},
        "tier": "one resonance field (ADR-1400)",
    }


# ------------------------------------------------------------------------------------------------ Cymatic World
def build_world():
    """THE RESONANT GROUND: the basin is the instrument. Seen from high and steep, the ground fills the frame; its
    light is painted in its program -- a raking cyan key and a fuchsia counter-fill from the opposite quarter, so
    every ridge reads on both flanks -- and the nodal lines and the hardest-ringing hollows carry the emission."""
    cc = CC_FIELD + [(5, "relief", "TERRAIN: how deeply the ground's sand relief is carved"),
                     (8, "light", "LIGHT: the fuchsia counter light over the ridges, and the nodal lines' glow")]
    pads = [PAD_STRIKE]
    s = kit.Scene("cymatic-world", "Cymatic World", design(
        "A night basin of basalt whose ground is a resonating plate: the music excites one field of standing waves, "
        "and the grit settles on its nodal lines, carving rings, petals and cells into the ground.",
        "the basin floor's figure", "the basin floor, its sand relief drawing the figure",
        "a slow high arc over the basin, there and back in 160 s"))
    environment(s)
    # THE DESIGNED BANK (08-brief): one plate whose modes share a centre -- so their figures are related and nest --
    # with designed orders (rings, a trefoil, a dipole, four and six lobes; nested rings and finer petals), a second
    # centre off to the west that breaks the symmetry, standing bands, and a faint fine carpet. Every mode is a persistent
    # identity at a fixed place on the dispersion axis (layout); the music moves weight between them (competition lets
    # one or two lead a scale, so its figure is legible), petals turn slowly (some against the others) and the centres
    # wander a little: the figure transforms instead of sliding apart.
    resonator(s, [
        # A, the large figure about an off-centre point (bass): frame-filling rings, stars and lobes; its focus leads one
        # or two modes and sweeps the bank. The centre sits off the frame's middle and wanders, so the frame is never one
        # centred target
        {"kind": "point", "position": [7.0, 3.0, -7.0], "order": 6, "bandLow": 0.0, "bandHigh": 0.15, "reach": 90.0,
         "gain": 1.0, "amplitude": 1.0, "breath": 0.4, "drift": 12.0, "driftPeriod": 100.0, "spin": 0.03,
         "orders": [3, 4, 2, 6],
         "focus": 0.07, "focusWidth": 0.045, "focusSweep": 0.09, "focusPeriod": 47.0, "focusFloor": 0.03},
        # B, a second large figure about a centre off to the south-west: where A and B both ring, their rings interfere
        # (hyperbolic nodal lines, unequal lobes); as the two foci sweep at different rates the large figure moves from
        # A's to B's through their interference
        {"kind": "point", "position": [-20.0, 2.0, 15.0], "order": 6, "bandLow": 0.15, "bandHigh": 0.3, "reach": 90.0,
         "gain": 1.0, "amplitude": 0.42, "breath": 0.5, "drift": 9.0, "driftPeriod": 80.0, "spin": -0.04,
         "orders": [3, 2, 5],
         "focus": 0.23, "focusWidth": 0.045, "focusSweep": 0.09, "focusPeriod": 61.0, "focusFloor": 0.03},
        # M, the medium figure: finer rings and petals about a centre a few metres from A's -- nested inside the large
        # lobes, but eccentric, so the frame is never a bullseye (rings live here, not in the large figure)
        {"kind": "point", "position": [3.0, 3.0, -3.5], "order": 8, "bandLow": 0.3, "bandHigh": 0.66, "reach": 80.0,
         "gain": 0.9, "amplitude": 0.5, "breath": 0.7, "drift": 6.0, "driftPeriod": 100.0, "spin": 0.045,
         "orders": [0, 8, 4, 6, 3, 5],
         "focus": 0.48, "focusWidth": 0.05, "focusSweep": 0.16, "focusPeriod": 29.0, "focusFloor": 0.03},
        # F, the fine layer: standing bands, squares and hexagons, faint and local
        {"kind": "lattice", "position": [0.0, -6.0, 0.0], "order": 3, "bandLow": 0.66, "bandHigh": 1.0,
         "reach": 46.0, "gain": 0.55, "amplitude": 0.1, "breath": 1.5, "depth": 9.0, "drift": 2.0,
         "driftPeriod": 60.0, "spin": 0.02, "orders": [2, 1, 3]},
    ], modes=17.0, lambdaMax=16.0, lambdaMin=2.6, competition=1.5, selectivity=4.0, limit=1.5, breath=0.15,
       # E, transients: every onset launches a travelling ring (kick broad and slow, snare sharp and local)
       ripple=0.5, rippleSpeed=9.0, rippleLife=1.6, rippleWidth=0.9, rippleArea=30.0, rippleCount=2.0, rippleFade=0.35,
       # ADR-1404: fixed mode places (a route on `modes` adds modes, it never re-places them), and per-band response
       layout=LAYOUT, attack=[0.18, 0.08, 0.04], release=[3.0, 1.4, 0.6])
    layers = ground_readers(s)
    g = ground(s, layers, write_mesh=True)
    # LOOKING INTO THE GROUND: high and steep (about 47 degrees down) on a 30 mm lens, so the frame's top edge meets
    # the ground inside r ~60 m all round the arc and the basin's walls (they rise from r ~90 m) never enter it -- the
    # frame is all ground (checked by ray-casting the top edge and corners along the whole loop)
    s.camera = {"mode": 1, "position": [0.0, 32.0, 30.0], "target": [-2.0, -3.0, 0.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.arc_camera([-2.0, -3.0, 0.0], 30.0, 32.0, period=160.0, centre_deg=8.0, sweep_deg=80.0, steps=24, side=0.0,
                 lift=0.0, height_bob=2.0)
    s.params_({"camera/lens/focalLength": 30.0})
    F = field_instrument(s)
    s.control = control_map(cc, pads)
    s.route(R("control.relief", "procedural/ground/deform/1/amount", 3.0, **CCR))
    s.params_(POST)
    if DEBUG in ("signed", "nodal", "envelope"):  # a debug material: none of the colour routes' targets exist
        return s, cc, pads
    # a ramp-tinted term's colour is its constant2 (half) and constant3 (whole); a route scales both
    RP = "material/cwGround/op/%d/ramp/%s"
    def ramp_paths(label):
        return [RP % (g[label], "constant2"), RP % (g[label], "constant3")]
    counter, hollow, sheen = ramp_paths("fill"), ramp_paths("hollow"), ramp_paths("sheen")
    line = "material/cwGround/op/%d/constant/constant" % g["line"]
    grit_mid, grit = RP % (g["albedo"], "constant2"), RP % (g["albedo"], "constant3")
    # THE COLOUR AND LIGHT RESPONSE (the colour pass, Pass D). The field already places the light: the nodal-line
    # glow is crest x the field's own amplitude, the hollow glow lives only on the inner walls of cells that ring hard,
    # so every route below scales a SPATIALLY LOCALISED term, never the whole frame. Colour and brightness move on
    # separate routes, on separate time scales:
    #   persistent (seconds): the song's build (intensity, a 12 s follower) turns the grit from deep teal toward a
    #     brighter cyan -- a HUE/value change of a material, no brightness pulse; held sound (sustain) builds the
    #     hollow glow's presence.
    #   transient (a few hundred ms): kicks flare the hollow glow where the cells already ring; snares brighten the
    #     nodal lines; hats sharpen the stone's sheen (fine highlights only); spectral change (flux) lifts the
    #     fuchsia counter-fill on the far flanks.
    tgt = hexrgb("#0f6b82")
    base = pc("grit")
    colour_routes = PALETTE != "G"
    if colour_routes:
        s.route(*[R("intensity", path, round((tgt[c] - base[c]) * w, 5), comp=c, attackMs=4000, decayMs=9000)
                  for c in range(3) for path, w in ((grit, 1.0), (grit_mid, 0.5))])
        s.route(*[R("sustain", p_, 1.0, op="multiply", gain=1.2, offset=1.0, attackMs=1500, decayMs=4000) for p_ in hollow],
                *[R("kickEnv", p_, 1.0, op="multiply", gain=1.6, offset=1.0, attackMs=0, decayMs=380) for p_ in hollow],
                R("snareEnv", line, 1.0, op="multiply", gain=0.9, offset=1.0, attackMs=0, decayMs=260),
                *[R("hatEnv", p_, 1.0, op="multiply", gain=0.5, offset=1.0, attackMs=0, decayMs=180) for p_ in sheen],
                *[R("flux", p_, 1.0, op="multiply", gain=0.8, offset=1.0, attackMs=300, decayMs=1500) for p_ in counter])
    s.route(*[R("control.light", p_, 1.0, op="multiply", gain=1.5, offset=1.0, **CCR) for p_ in counter],
            R("control.light", line, 1.0, op="multiply", gain=2.0, offset=1.0, **CCR))
    return s, cc, pads


BUILDERS = {"world": build_world}
OUT_DIRS = {k: os.path.join(REPO, "examples", v["id"]) for k, v in SCENES.items()}
DESCRIPTION_TAIL = {"world": "The ground's figure is drawn by one resonance field (ADR-1400)."}


def write_scene(key):
    os.makedirs(OUT_DIRS[key], exist_ok=True)
    s, cc, pads = BUILDERS[key]()
    pp, sp = s.write(OUT_DIRS[key])
    ccs = ", ".join("CC %d %s" % (n, d.split(":")[0].lower()) for n, _, d in cc)
    pds = ", ".join("pad %d %s" % (n, d.split(":")[0].lower()) for n, _, d in pads)
    kit.upsert_index_entry({
        "category": "Cymatic World",
        "description": "Play live audio (any mix) and, optionally, a MIDI controller: " + ccs + "; " + pds + ". "
                       + s.design["thesis"] + " " + DESCRIPTION_TAIL[key] + " See docs/prototypes/cymatic-world/.",
        "live": True,
        "name": SCENES[key]["title"],
        "project": "%s/%s.json" % (SCENES[key]["id"], SCENES[key]["id"])})
    return pp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", default="all", choices=["all", "world"])
    ap.add_argument("--variant", help="also write a file project of --scene driven by this WAV")
    ap.add_argument("--out", help="the variant's path")
    ap.add_argument("--camera", help="x,y,z:tx,ty,tz for the variant")
    ap.add_argument("--set", action="append", default=[], help="path=json value, for the variant")
    a = ap.parse_args()
    keys = ["world"]
    pp = None
    for k in keys:
        pp = write_scene(k)
        print(pp)
    if a.variant:
        params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
        print(variant(pp, a.variant, a.out, a.camera, params))


if __name__ == "__main__":
    main()

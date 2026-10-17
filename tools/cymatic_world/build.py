#!/usr/bin/env python3
"""CYMATIC WORLD: the generator (docs/prototypes/cymatic-world/).

    python3 tools/cymatic_world/build.py                    # the scene, its meshes and its index entry
    python3 tools/cymatic_world/build.py --variant --out <project.json> [--camera ...] [--set ...]
    then: avgen --headless --project <project.json> --audio assets/audio/feline-footwear.wav --render ...

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
FINE_STEP = float(os.environ.get("CW_MESH_STEP", "0.36"))   # 08-brief Stage 7: the visible ground's cell size (m)


def grid_axis(core=36.0, half=88.0, step=None, extent=2600.0, mid=1.025, growth=1.12):
    """One axis of the ground's tensor grid: `step` apart within +-core (the figure's middle, the nearest ground the
    camera sees), then each step `mid` times the last out to +-half (the far ground, at the top of the frame, needs
    fewer vertices for the same pixels), then `growth` times out to +-extent (never in frame: it closes the world)."""
    step = step if step is not None else FINE_STEP
    n = int(round(core / step))
    inner = [i * core / n for i in range(-n, n + 1)]
    outer = []
    x, d = core, core / n
    while x < extent:
        d *= mid if x < half else growth
        x = min(x + d, extent)
        outer.append(x)
    return np.array([-v for v in reversed(outer)] + inner + outer)


def ground_mesh():
    """08-brief Stage 7: a tensor-product grid -- square cells of FINE_STEP over the ground the camera sees, alternate
    diagonals (no preferred direction for a crest to stair-step along), no stitched seams. Heights from the WorldMap.
    (It replaces a polar mesh whose ring-count rule had carried the horizon's 256 vertices to every ring: cells
    ~0.36 x 1 m at 40 m, the sawtooth; and whose doubling rows drew seams.)"""
    xs = grid_axis()
    zs = grid_axis()
    nx, nz = len(xs), len(zs)
    X, Z = np.meshgrid(xs, zs, indexing="xy")
    pts = np.stack([X.ravel(), Z.ravel()], axis=1)
    hs = world_heights(pts)
    pos = np.stack([pts[:, 0], hs, pts[:, 1]], axis=1)
    j, i = np.meshgrid(np.arange(nz - 1), np.arange(nx - 1), indexing="ij")
    a = (j * nx + i).ravel()
    b = a + 1
    c = a + nx
    d = c + 1
    alt = ((i + j) % 2 == 0).ravel()
    t1 = np.where(alt[:, None], np.stack([a, c, b], 1), np.stack([a, c, d], 1))
    t2 = np.where(alt[:, None], np.stack([b, c, d], 1), np.stack([a, d, b], 1))
    tri = np.concatenate([t1, t2], axis=0).astype(np.int64)
    # front to back: the camera stands on the +z side (its arc swings +-40 degrees about it), so the rows nearest it
    # are drawn first and the hardware's early depth test rejects the far cells' hidden fragments before they are
    # shaded (drawn back to front, every nearer ridge overdrew a shaded one: the cost grew with the relief's depth)
    centroid_z = pos[tri, 2].mean(axis=1)
    tri = tri[np.argsort(-centroid_z, kind="stable")]
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


def variant(project, out, camera=None, params=None):
    """A review copy of the live project (its scene by absolute path), optionally with another camera
    ("x,y,z:tx,ty,tz") and parameter overrides. It names NO audio: the scene is a live scene and plays from live input;
    a render or a profile feeds a test track on the command line (`avgen ... --audio assets/audio/feline-footwear.wav`,
    ADR-1408)."""
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
# ADR-1407: the relief is baked into a grid once a frame (CW_BAKE=0: every vertex evaluates the readers, for comparison)
BAKE = os.environ.get("CW_BAKE", "1") != "0"
LINE2 = os.environ.get("CW_LINE2", "0") == "1"  # Stage C's medium crest light (off since Stage G: 4 ops)
WAVE = float(os.environ.get("CW_WAVE", "2.2"))  # Stage D: the travelling light's gain into its grid (0 = off)
BAKE_RES = int(os.environ.get("CW_BAKE_RES", "896"))
BAKE_HALF = 86.0   # metres: the frame's top edge meets the ground inside ~80 m (camera B, 21:9)
# the camera (the correction round, owner: "pull back"): A the shape-language framing, B pulled back (chosen), C further
CAMERAS = {"A": (30.0, 32.0, 30.0), "B": (36.0, 42.0, 28.0), "C": (40.0, 48.0, 26.0)}
CAMERA = CAMERAS[os.environ.get("CW_CAM", "B")]
PROTO = os.environ.get("CW_PROTO", "P2")   # Stage 2: P2 (nodal boundaries) chosen
SHAPES = {
    "P1": {"large": ({"reliefSand": 0.0, "reliefFigure": 1.0, "reliefVibration": 0.35}, 3.2),
           "medium": ({"reliefSand": 0.0, "reliefFigure": 1.0, "reliefVibration": 0.3}, 1.3),
           "fine": ({"reliefSand": 0.0, "reliefVibration": 1.0}, 0.25)},
    # Stage 7: a crest must span a few vertices (0.3 m) or the mesh draws it as a sawtooth: the medium crease (4-7 m
    # waves) at sharpness 0.5 was ~0.3 m wide; 1.0 makes it ~0.7 m. The crisp line itself is the material's (shading)
    # the correction round (owner: "flatter, weaker shapes"): the large figure's ridges keep their height whatever the
    # loudness (normalised sand, ADR-1405) so the quiet state has a deliberate form; the antinodes sink into deep
    # basins with the excitation (-1.2 |F|, crease rounded half as much -- the square grid carries it); the medium net
    # at about half its height so the large lobes, rings and petals lead
    "P2": {"large": ({"reliefSand": 1.0, "sandNormalized": True, "sharpness": 0.75, "reliefAmplitude": -1.2,
                     "reliefRound": 0.4, "reliefVibration": 0.25}, 2.8),
           "medium": ({"reliefSand": 1.0, "sharpness": 1.0, "reliefAmplitude": -0.35, "reliefRound": 0.5}, 0.45),
           "fine": ({"reliefSand": 0.0, "reliefVibration": 1.0}, 0.25)},
    "P3": {"large": ({"reliefSand": 0.55, "sharpness": 0.7, "reliefFigure": 0.8, "reliefVibration": 0.25}, 3.2),
           "medium": ({"reliefSand": 1.0, "sharpness": 0.45, "reliefAmplitude": -0.3}, 1.2),
           "fine": ({"reliefSand": 0.0, "reliefVibration": 1.0}, 0.25)},
}


def ground_readers(s):
    """The material's shading reader (all scales), and one relief reader per scale. Returns the deformer layers."""
    if BAKE:
        # ADR-1407: what the material reads -- the sand ridge, the crest line, the excitation -- baked too, so a pixel
        # samples three grids instead of evaluating every mode (the colour pass's `shade` colour field, split in three)
        # the large and medium scales only (u < 0.7): colour follows the nodal network the geometry draws, not the
        # fine carpet or the rings passing through it
        win = {"listenLow": 0.0, "listenHigh": 0.7, "listenWidth": 0.04}
        large = {"listenLow": 0.0, "listenHigh": 0.3, "listenWidth": 0.04}   # the correction: the large figure leads
        # the sand masks normalised (ADR-1405): the network is marked in a quiet passage too; loudness is |F|'s
        # (the crest line is the sand raised to a power in the program -- exp(-F^2/(s^2 E)) at s = 0.25 is the s = 0.9
        # value to the power 13 -- which frees a field slot: a program samples at most four fields)
        medium = {"listenLow": 0.28, "listenHigh": 0.7, "listenWidth": 0.04}
        for name, channel, extra, res in (("shade-sand", "sand", {"sharpness": 0.9, "sandNormalized": True, **large}, BAKE_RES),
                                          ("shade-amp", "amplitude", dict(win), 512),
                                          # 10-brief Stage B, the colour field: the large figure's signed standing
                                          # pattern F -- each lobe a broad colour region, anchored to the geometry
                                          ("shade-tint", "relief", {"reliefSand": 0.0, "reliefFigure": 1.0, **large}, 640)):
            reader(s, name, channel, extra)
            bake_grid(s, name, res)
        # 10-brief Stage C, the SECONDARY light: the medium network's crests times the excitation, multiplied before
        # the bake (a compound), so the program spends one field slot on it and keeps the fourth for Stage D
        if LINE2:
            reader(s, "shade-sandm", "sand", {"sharpness": 0.6, "sandNormalized": True, **medium})
        # Stage D, the TRAVELLING light (ADR-1409): the onset ripples alone (dark in silence); the program lays it on
        # the large figure's ridges -- a pulse of light that runs along the crests from where a hit landed, and is
        # gone. It shares the secondary light's grid with the opposite sign (a program samples at most four fields,
        # and the GPU holds at most 16): > 0 is the medium crest, < 0 the wave; the excitation multiplies the medium
        # crest in the program
        reader(s, "shade-wave", "amplitude", {"rows": "ripples"}, frame={"strength": -WAVE})
        if LINE2:
            compound(s, "shade-light", ["shade-sandm", "shade-wave"], "add")
            bake_grid(s, "shade-light", 768)
        else:  # Stage G: the travelling light alone (the medium crest light off -- 4 ops at ~0.36 ms each)
            bake_grid(s, "shade-wave", 768)
    else:
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
        reader(s, "relief-" + name, "relief", r, frame={"strength": amount} if BAKE else None)
        layers.append(("relief-" + name, amount))
    if not BAKE:
        return layers
    # ADR-1407: the scales' sum, baked once a frame into a plane grid that the ground's single deformer samples --
    # the per-vertex cost (three readers, three samples each for the normal) becomes one evaluation per grid cell
    s.nodes.append({"name": "relief", "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "compound", "children": [n for n, _ in layers], "combine": "add",
                              "strength": 1.0, "falloff": {"kind": "none"}}})
    bake_grid(s, "relief")
    return [("relief-grid", 1.0)]


def compound(s, name, children, combine, strength=1.0):
    s.nodes.append({"name": name, "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "compound", "children": children, "combine": combine, "strength": strength,
                              "falloff": {"kind": "none"}}})


def bake_grid(s, source, res=None):
    """ADR-1407: a plane grid `<source>-bake` holding `source` sampled at its cells every frame, and the field
    `<source>-grid` that reads it."""
    if not hasattr(s, "grids"):
        s.grids = []
    s.grids.append({"name": source + "-bake", "enabled": True, "mode": "bake", "wrap": "clamp",
                    "resolution": [res or BAKE_RES, 1, res or BAKE_RES], "boundsMin": [-BAKE_HALF, -4.0, -BAKE_HALF],
                    "boundsMax": [BAKE_HALF, -2.0, BAKE_HALF], "injectField": source})
    s.nodes.append({"name": source + "-grid", "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "grid", "reference": source + "-bake", "strength": 1.0,
                              "falloff": {"kind": "none"}}})


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
    "B": {"line2": "#7fb6ff", "line2I": 0.4, "wave": "#9ff0ff", "waveI": float(os.environ.get("CW_WAVE_I", "4.0")), "violet": "#2c0c6e", "blue": "#0a2c84", "stone": "#050a22", "grit": "#0b4458", "key": "#62ecff", "fill": "#c13bff", "rim": "#ff8fe6",
          "sky": "#1a1250", "haze": "#2a2275", "line": "#d4fffb", "hollow": "#ff3fd2", "sheen": "#9fe6ff",
          "sheenI": 0.75,
          "keyI": 2.3, "fillI": 0.9, "rimI": 0.35, "skyI": 0.07, "lineI": 1.9, "hollowI": 0.4},
    # C, spectral heat: violet stone, indigo grit, electric-blue key, hot-pink fill, amber rim, coral hollows
    "C": {"stone": "#1d1150", "grit": "#3c2a9a", "key": "#6aa8ff", "fill": "#ff3f8f", "rim": "#ffb46b",
          "sky": "#1c1048", "haze": "#24114c", "line": "#a8f2ff", "hollow": "#ff6a3d",
          "sheen": "#fff1e0", "sheenI": 1.4, "keyI": 2.31, "fillI": 0.76, "rimI": 0.42, "skyI": 0.105, "lineI": 2.2, "hollowI": 0.1},
}
# M, the ten palettes' grey program: palette B's intensities with every colour white (the bindings colour it)
PALETTES["M"] = {k: ("#ffffff" if isinstance(v, str) else v) for k, v in PALETTES["B"].items()}
# G, GEOMETRY ONLY (a review aid, never shipped: CW_PALETTE=G): one neutral grey material, white light from the same
# key and fill directions, no emission and no colour routes -- the world's shape with its colour ignored
PALETTES["G"] = {"stone": "#5c5f66", "grit": "#5c5f66", "key": "#ffffff", "fill": "#c8ccd4", "rim": "#ffffff",
                 "sky": "#9aa0aa", "haze": "#2c2f36", "line": "#000000", "hollow": "#000000", "sheen": "#ffffff",
                 "sheenI": 0.35, "keyI": 1.5, "fillI": 0.55, "rimI": 0.12, "skyI": 0.18, "lineI": 0.0, "hollowI": 0.0}
# ---- THE TEN PALETTES (12-brief-multi-palette.md) --------------------------------------------------------------------
# The owner's artist-designed palettes, each seven named roles (sRGB hex, verbatim from the brief):
#   background, deep shadow, primary surface, secondary surface, resonant, spectral highlight, rare peak.
# They are ADR-1043 project-palette STATES: the ground's program is authored in grey (palette "M": every colour white,
# every intensity B's), and role bindings MULTIPLY the program's colour constants by the active palette's colours each
# frame -- so the routes that scale a term's brightness keep working, the colour work stays on the GPU (a few uniform
# constants), and palette/position blends any two neighbouring states in OKLab.
PALETTE_ROLES = ("background", "shadow", "primary", "secondary", "resonant", "spectral", "peak")
MULTI_PALETTES = [
    ("Abyssal Resonance",      ["#050711", "#10152B", "#202D59", "#354C88", "#527FE0", "#A3C8FF", "#E5EDFF"]),
    ("Crimson Singularity",    ["#080612", "#180D29", "#301448", "#54205F", "#A52361", "#F04455", "#FF9AA5"]),
    ("Molten Glass",           ["#0B0808", "#26110F", "#51201A", "#87351F", "#D9572B", "#FFAD45", "#FFE3A1"]),
    ("Ultraviolet Bloom",      ["#080611", "#1A1033", "#30205A", "#58318A", "#8A45C6", "#D45AAE", "#F4C7FF"]),
    ("Oxidized Metal",         ["#090B0D", "#1B2022", "#39413F", "#665044", "#AD6847", "#D8A76C", "#F5DDB0"]),
    ("Glacial Spectral",       ["#050A12", "#101B2A", "#1D344C", "#315B79", "#65A9CE", "#B7DCEB", "#F1FCFF"]),
    ("Chlorophyll After Dark", ["#050A08", "#102017", "#193D2A", "#286746", "#39A66A", "#A4D65E", "#E6F5A4"]),
    ("Rose Quartz",            ["#0A0710", "#211522", "#47263F", "#75405E", "#B65D83", "#EAA0BA", "#FFE0EA"]),
    ("Monochrome Silver",      ["#030405", "#101215", "#282C32", "#464D57", "#777F8C", "#C2C9D2", "#FFFFFF"]),
    ("Prismatic Oil",          ["#05070D", "#101426", "#20203F", "#442447", "#813450", "#D77C88", "#F4D4C5"]),
]


# the view layer's colours (facing -> half -> grazing), display colours like the roles; palettes not listed have none
VIEW_LAYERS = {
    # J: oil on petrol -- facing walls barely tinted, turning violet, then burgundy-rose at grazing
    "Prismatic Oil": ["#06141c", "#5a2c9a", "#d06a5a"],
    # E: metal -- no colour facing the camera, copper on the turn, pale gold where the wall grazes the view
    "Oxidized Metal": ["#000000", "#5a3020", "#c89a60"],
}


def _oklab(c):
    l = 0.4122214708 * c[0] + 0.5363325363 * c[1] + 0.0514459929 * c[2]
    m = 0.2119034982 * c[0] + 0.6806995451 * c[1] + 0.1073969566 * c[2]
    s_ = 0.0883024619 * c[0] + 0.2817188376 * c[1] + 0.6299787005 * c[2]
    l, m, s_ = (max(v, 0.0) ** (1.0 / 3.0) for v in (l, m, s_))
    return [0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s_,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s_,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s_]


def _linear(lab):
    l = lab[0] + 0.3963377774 * lab[1] + 0.2158037573 * lab[2]
    m = lab[0] - 0.1055613458 * lab[1] - 0.0638541728 * lab[2]
    s_ = lab[0] - 0.0894841775 * lab[1] - 1.2914855480 * lab[2]
    l, m, s_ = l ** 3, m ** 3, s_ ** 3
    return [max(0.0, 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s_),
            max(0.0, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s_),
            max(0.0, -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s_)]


def _mix(a, b, t):  # OKLab, the space the engine blends palettes in (ADR-1043)
    la, lb = _oklab(a), _oklab(b)
    return _linear([x + (y - x) * t for x, y in zip(la, lb)])


def _hue(c):  # a light's colour: the hue and saturation, at unit peak (its op's intensity carries the brightness)
    m = max(max(c), 1e-6)
    return [round(v / m, 5) for v in c]


# Each palette's CHARACTER (the brief's prose, and the coordinator's review of the first sheet): how much of its
# surface colour shows (alb, x PAL_ALBEDO), how white its raking key is (keyMix: 0 = the source role's hue, 1 =
# white) and from which role, how bright each light term is, and which role the grit and the counter-light take.
# The defaults suit most; the entries make A and F differ in temperature and darkness, and B, H and J three different
# worlds rather than three pinks.
PALETTE_TUNE = {
    # enormous dark forms, cool spectral highlights: the darkest, an ultramarine-tinted key, crests bright
    "Abyssal Resonance": {"alb": 0.6, "keySrc": "resonant", "keyMix": 0.15, "key": 0.75, "sky": 0.6, "hollow": 0.8,
                          "line": 1.1, "gritMix": 0.5},
    # dark indigo-violet sculpture; red ONLY on the ridges: a violet key and fill, violet grit, the red in the
    # crest lines and the travelling light, the magenta glow held low
    "Crimson Singularity": {"alb": 1.2, "keySrc": "secondary", "keyMix": 0.35, "key": 0.8, "fillSrc": "secondary",
                            "gritMix": 0.0, "hollow": 0.5, "line": 1.3, "waveSrc": "spectral", "line2Src": "secondary"},
    # hot colours concentrated in folds and ridges; dark regions rich
    "Molten Glass": {"alb": 0.8, "keyMix": 0.4, "gritMix": 0.15, "hollow": 0.8},
    # violet foundation, fuchsia only as a response: the glow carries the fuchsia, the key stays violet-white
    "Ultraviolet Bloom": {"keySrc": "resonant", "keyMix": 0.45, "hollowSrc": "spectral", "gritMix": 0.15},
    # tactile metal: a warm grazing view layer, sharp bright glints, little glow
    "Oxidized Metal": {"keyMix": 0.55, "hollow": 0.4, "sheen": 1.8, "gritMix": 0.2},
    # crystalline, cold, spacious: an icy near-white key, crisp glints, edges catching light, almost no glow
    "Glacial Spectral": {"alb": 1.5, "keySrc": "peak", "keyMix": 0.9, "key": 1.1, "sky": 1.4, "hollow": 0.25,
                         "sheen": 2.0, "line": 1.0, "gritMix": 0.6, "fillSrc": "spectral"},
    # green emerging from darkness, chartreuse only on resonant peaks
    "Chlorophyll After Dark": {"alb": 0.8, "keySrc": "resonant", "keyMix": 0.5, "gritMix": 0.1, "line": 0.9},
    # soft, smoky, low contrast, pearl: a pearly key, a higher ambient floor, softer crests and glow
    "Rose Quartz": {"alb": 1.0, "keySrc": "peak", "keyMix": 0.6, "key": 0.7, "sky": 2.2, "line": 0.55,
                    "line2": 0.6, "hollow": 0.45, "sheen": 0.7, "gritMix": 0.1},
    # pure form: the geometry alone carries it
    "Monochrome Silver": {"keyMix": 1.0, "hollow": 0.5},
    # petrol and oil: a cool neutral key, the view layer carries the colour, crests rose-gold but low, little glow
    "Prismatic Oil": {"alb": 0.9, "keySrc": "secondary", "keyMix": 0.7, "hollow": 0.35, "line": 0.7, "gritMix": 0.0},
}


def palette_state(name, hexes):
    """One palette as the program's colour roles. Albedo roles are the brief's colours scaled to albedo; light and
    emission roles are hues at unit peak times the palette's gain for that term, so each term keeps its brightness
    from the program (the hierarchy holds in every palette: darks dark, the bright roles rare and on the ridges)."""
    r = dict(zip(PALETTE_ROLES, (hexrgb(h) for h in hexes)))
    t = PALETTE_TUNE.get(name, {})
    white = [1.0, 1.0, 1.0]
    # the brief's colours are what the eye should SEE; the ground shows albedo x (key 2.3 + fill) through AgX with
    # +0.2 exposure, about 4x in linear on a lit flank -- so an albedo role is its display colour over that
    a_ = PAL_ALBEDO * t.get("alb", 1.0)
    alb = lambda c: [v * a_ for v in c]
    light = lambda c, term: [v * t.get(term, 1.0) for v in _hue(c)]
    src = lambda term, default: r[t.get(term + "Src", default)]
    key = _mix(white, src("key", "spectral"), 1.0 - t.get("keyMix", 0.55))
    colors = {
        # the base field (Stage B's colour field): one sign of lobe primary, the other secondary, the nodes deep shadow
        "surfaceA": alb(r["primary"]), "surfaceB": alb(r["secondary"]), "shadow": alb(r["shadow"]),
        # the ridges' settled grit: the secondary surface, leaning resonant
        "grit": alb(_mix(r["secondary"], r["resonant"], t.get("gritMix", 0.3))),
        # the raking key: white tinted by a role (a palette's light, not a colour wash); the ambient floor under it
        # takes the primary surface's hue
        "key": light(key, "key"), "sky": light(r["primary"], "sky"),
        "keymid": [0.5 * (x + y) for x, y in zip(light(key, "key"), light(r["primary"], "sky"))],
        "fill": light(src("fill", "resonant"), "fill"),         # the counter light on the far flanks
        "hollow": light(src("hollow", "resonant"), "hollow"),   # the glow in the excited cells
        "line": light(r["spectral"], "line"),                   # the large crest's light
        "line2": light(_mix(src("line2", "secondary"), r["spectral"], 0.5), "line2"),   # the medium crest
        "sheen": light(r["peak"], "sheen"),                     # the rare peaks: glints ...
        "wave": light(src("wave", "peak"), "wave"),             # ... and the travelling light
    }
    black = [0.0, 0.0, 0.0]
    view = VIEW_LAYERS.get(name)
    colors["view0"], colors["view1"], colors["view2"] = (alb(hexrgb(h)) for h in view) if view else (black,) * 3
    return {"name": name, "colors": {k: [round(v, 5) for v in c] for k, c in colors.items()}}


PALETTE = os.environ.get("CW_PALETTE", "M")   # M: the ten palettes (12-brief); A-C the colour pass's, G grey
PAL_INDEX = float(os.environ.get("CW_PAL", "0"))               # the palette shown (palette/position)
PAL_ALBEDO = float(os.environ.get("CW_PAL_ALBEDO", "0.28"))     # albedo = the brief's display colour x this
PAL_TRANSITION = float(os.environ.get("CW_PAL_TRANSITION", "6.0"))  # seconds of OKLab blend
PAL_HOLD = float(os.environ.get("CW_PAL_HOLD", "24.0"))         # the least time a palette is held
PAL_LOCK = os.environ.get("CW_PAL_LOCK", "0") == "1"            # a review still: hold the selected palette
PAL_ORDER = [0, 2, 5, 1, 6, 7, 4, 3, 8, 9]
KEY_DIR = (0.93, -0.2, -0.3)        # the direction the key travels: low, from the left-front quarter
FILL_DIR = (-0.8, -0.32, 0.5)       # the fill travels the other way: lights the flanks the key leaves dark


def pc(key, scale=1.0):
    pal = PALETTES[PALETTE]
    return hexrgb(pal.get(key, pal["stone"]), scale)  # a palette without a colour role reads its stone there


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

    if BAKE:  # ADR-1407: baked grids, sampled (cheap) -- r0 = sand, r2 = crest (sand^13), r3 = amplitude
        g.op("shade", "field", 0, field="shade-sand-grid")
        g.op(None, "power", 2, a=0, f=13.0)
        g.op(None, "field", 3, field="shade-amp-grid")
        # the colour field (Stage B): the large figure's signed F
        g.op("tintField", "field", 5, field="shade-tint-grid")
        g.op(None, "remap", 5, a=5, k=[-0.55, 0.55, 0.0, 1.0], f=1.0)   # r5: the colour coordinate, 0 .. 1
    else:
        g.op("shade", "field", 0, field="shade")                   # r0 = (sand, crest, amplitude, w)
        g.op(None, "swizzle", 2, a=0, k=[1, 1, 1, 1])               # r2 = crest
        g.op(None, "swizzle", 3, a=0, k=[2, 2, 2, 2])               # r3 = amplitude
    # (a scalar op reads its source's x: r0.x is the sand, so no swizzle)
    g.op("gritMask", "smoothstep", 1, a=0, k=[0.5, 0.92] if BAKE else [0.12, 0.5])          # r1: where grit has settled (the ridges)
    # ---- emission, into r2: the nodal line's light where the ground rings, and a glow in the hardest-ringing hollows
    g.op("lineMask", "smoothstep", 2, a=2, k=[0.7, 0.96] if BAKE else [0.25, 0.7])
    if BAKE:
        # Stage D, the travelling light: the ripples alone (< 0 in the shared grid), on the crest lines only -- dashes
        # of light that run along the ridges from where a hit landed (the ring's carrier makes the dashes), then gone
        g.op("light2", "field", 6, field="shade-light-grid" if LINE2 else "shade-wave-grid")  # > 0 medium, < 0 wave
        g.op("waveMask", "remap", 7, a=6, k=[-0.04, -0.22, 0.0, 1.0], f=1.0)
        g.op(None, "multiply", 7, a=7, b=2)                         # ... on the crest
    g.op(None, "multiply", 2, a=2, b=3)                            # crest x ringing (unbounded: a pair, not a ramp)
    g.op("line", "constant", 4, k=pc("line", P_["lineI"]) + [1.0])
    g.op(None, "multiply", 2, a=2, b=4)
    if BAKE:
        # Stage C, the SECONDARY light: the medium network's crests glow faintly, and only where the figure is excited
        # -- a restrained second tier under the large figure's lines, never an equal outline on every contour
        if LINE2:
            g.op("lineM", "smoothstep", 6, a=6, k=[0.75, 0.98])
            g.op(None, "multiply", 6, a=6, b=3)                     # ... where the figure is excited
            tint("line2", 6, 6, "line2", P_.get("line2I", 0.0))
            g.op(None, "add", 2, a=2, b=6)
        tint("wave", 7, 7, "wave", P_.get("waveI", 0.0))
        g.op(None, "add", 2, a=2, b=7)
    g.op("stoneMask", "remap", 7, a=1, k=[0.0, 1.0, 1.0, 0.0], f=1.0)   # r7: scoured stone (no grit)
    g.op("hollowRamp", "smoothstep", 3, a=3, k=[0.3, 1.0] if BAKE else [0.8, 2.4])          # only where it rings hard
    if not BAKE:  # (baked: the wall band below is all stone already -- sand < 0.14 never carries grit)
        g.op(None, "multiply", 3, a=3, b=7)
    # ... and only on the inner walls of a cell: a band of the sand field just inside the grit, so the glow's edges
    # follow the relief (the sand's contours ARE the cell walls), never an unshaped wash over flat ground
    g.op("wallBand", "smoothstep", 4, a=0, k=[0.02, 0.14] if BAKE else [0.035, 0.08])
    g.op(None, "multiply", 3, a=3, b=4)
    if BAKE:  # the fuchsia glow belongs to the violet lobes: the colour field chooses which cells glow
        g.op("violetLobes", "remap", 4, a=5, k=[0.45, 0.15, 0.0, 1.0], f=1.0)
        g.op(None, "multiply", 3, a=3, b=4)
    tint("hollow", 3, 3, "hollow", P_["hollowI"])
    g.op(None, "add", 2, a=2, b=3)                                 # r2: emission
    # ---- materials: scoured satin stone, matte settled grit (one ramp: stone -> grit by the grit mask) ----
    st, gr = pc("stone"), pc("grit")
    if BAKE:
        # THE COLOUR FIELD (10-brief Stage B): the stone takes its colour from the large figure's signed pattern F --
        # deep violet on one sign of lobe, the navy stone at the nodes, deep blue on the other -- so broad regions of
        # colour fill the lobes and change as the figure changes; quiet music (small F) settles them toward navy
        g.op("tint", "ramp", 4, a=5, k=pc("violet") + [1.0], k2=st + [1.0], k3=pc("blue") + [1.0])
        g.op("grit", "constant", 6, k=gr + [1.0])
        g.op("albedo", "mixBy", 4, a=4, b=6, c=1)                  # r4: the coloured stone, the grit on the ridges
        # Stage C (coordinator, after B): the hollow FLOORS fall back toward black -- the deepest sand (the cell floor
        # inside the walls) keeps a fifth of its colour, so the lobes' violet and blue live on the walls and the frame
        # has real negative space for the light to stand out against
        if PALETTE == "M":
            # THE VIEW LAYER (12-brief, palettes J and E): a colour that turns with the angle a wall shows the camera
            # -- an oil slick's violet-to-burgundy-to-rose sheen (J), copper going gold at grazing (E). One ramp on
            # the Fresnel term whose three colours are palette roles: black in the palettes without one, so the same
            # program serves all ten and a transition fades it in or out
            g.op("viewTerm", "fresnel", 6, f=1.3)
            g.op("view", "ramp", 6, a=6, k=[1, 1, 1, 1], k2=[1, 1, 1, 1], k3=[1, 1, 1, 1])
            g.op(None, "add", 4, a=4, b=6)
        g.op("floorDark", "remap", 6, a=0, k=[0.0, 0.4, 0.1, 1.0], f=1.0)
        g.op(None, "multiply", 4, a=4, b=6)
    else:
        g.op("albedo", "ramp", 4, a=1, k=st + [1.0], k2=[(x + y) * 0.5 for x, y in zip(st, gr)] + [1.0], k3=gr + [1.0])
    # ---- light, into r5 (sheen into r1) ----
    g.op(None, "input", 6, inp="normal")
    g.op("keyDot", "gradient", 1, a=6, k=_unit([-x for x in KEY_DIR]) + [0.0], f=1.0)
    if BAKE:  # the key over an ambient floor (the hemisphere's 3 ops folded into the key's ramp)
        kc, sc = pc("key", P_["keyI"]), pc("sky", P_["skyI"])
        g.op("key", "ramp", 5, a=1, k=sc + [1.0], k2=[(a + b) * 0.5 for a, b in zip(sc, kc)] + [1.0], k3=kc + [1.0])
    else:
        tint("key", 5, 1, "key", P_["keyI"])                       # the raking key
    g.op("sheenLobe", "power", 1, a=1, f=12.0)                     # the key's highlight ...
    g.op(None, "multiply", 1, a=1, b=7)                            # ... on the smooth stone only
    tint("sheen", 1, 1, "sheen", P_["sheenI"])                     # r1: sheen
    g.op("fillDot", "gradient", 3, a=6, k=_unit([-x for x in FILL_DIR]) + [0.0], f=1.0)
    g.op(None, "power", 3, a=3, f=4.0)                             # the fill grazes: a narrow lobe on the far flanks
    tint("fill", 3, 3, "fill", P_["fillI"])
    g.op(None, "add", 5, a=5, b=3)                                 # + fill
    if not BAKE:
        g.op("skyDot", "gradient", 3, a=6, k=[0.0, 0.5, 0.0, 0.5], f=1.0)   # 0.5 + 0.5 N.y: the hemisphere
        tint("sky", 3, 3, "sky", P_["skyI"])
        g.op(None, "add", 5, a=5, b=3)                             # + sky
    # (the Fresnel rim and the distance haze went in the shape-language pass: seen from 35-70 m, straight down, the
    # ground grazes almost nothing and hazes almost nothing -- 7 ops, ~2.5 ms of interpreter at High)
    if dbg == "light":
        g.op(None, "add", 4, a=5, b=1)                             # the light alone (on white)
    elif dbg != "albedo":
        g.op(None, "multiply", 4, a=4, b=5)                        # lit colour
        g.op(None, "add", 4, a=4, b=1)                             # + sheen
    if dbg in ("albedo", "light"):
        g.op(None, "constant", 2, k=[0, 0, 0, 1])
    g.base, g.emit = 4, 2
    return g


def environment(s):
    s.environment = {
        "intensity": 0.05, "background": C("zenith"), "fogColor": C("fog"),
        "volumeDensity": 0.0, "fogHeight": 12.0, "fogHeightFalloff": 0.04, "skyIntensity": 1.0,
        "shadowRange": 180.0,
        # ADR-1406: the ground is unlit and painted -- screen-space AO and contact shadows add nothing to it and cost the
        # depth prepass (every deformer re-run per vertex for a second pass)
        "ambientOcclusion": False, "contactShadows": False,
        # ... and no depth prepass either (ADR-1406's switch). It won at the correction round (35.3 -> 29.8 ms at High),
        # but the light pass re-measured it on loud Feline: with the program at 43 ops the prepass costs 5.8 ms and
        # saves only 3.4 (High 34.2 ms median with it, 31.8 without); CW_PREPASS=1 asks for it again
        "depthPrepass": os.environ.get("CW_PREPASS", "0") == "1",
        "sky": {"enabled": True, "zenithColor": C("zenith"), "horizonColor": C("horizon"),
                "groundColor": C("fog"), "haze": 0.45, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    # the ground is unlit: its key, fill, sky and rim are painted in its program (ground_program). One authored light,
    # dark and casting nothing, stands in for the default key a scene with no lights is given -- which casts, and so
    # costs a shadow mask and the depth prepass (ADR-1406) for a world nothing in which is lit
    s.light("unlit", "directional", direction=[0.3, -1.0, 0.2], color=[1.0, 1.0, 1.0], intensity=0.0, castsShadow=False)


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
           # ADR-1401: the mesh is 5 km wide; its auto step would be 3.7 m. Baked (ADR-1407): one grid cell, so the
           # normal's differences span neighbouring cells instead of reading one bilinear cell's flat facet
           extra={"normalStep": (2.0 * BAKE_HALF / BAKE_RES) if BAKE else 0.12},
           deformers=[] if os.environ.get("CW_NODEFORM") else  # (a profiling switch: the bare mesh)
           [{"kind": "field", "field": name, "amount": amount, "axis": [0.0, 1.0, 0.0], "space": "world",
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
    "post/bloom/intensity": 0.22, "post/bloom/threshold": 1.2, "post/output/vignette": 0.0,
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
                     (8, "light", "LIGHT: the counter light over the ridges, and the nodal lines' glow")]
    if PALETTE == "M":
        cc += [(9, "palette", "PALETTE: choose one of the ten palettes (the knob's range in ten steps)"),
               (10, "paletteLock", "LOCK: above half way the palette holds; the phrases stop changing it")]
    pads = [PAD_STRIKE]
    s = kit.Scene("cymatic-world", "Cymatic World", design(
        "A night basin whose ground is a resonating plate: the music excites one designed bank of standing waves, and "
        "its nodal lines stand as ridges between sunken antinodes, carving rings, petals and cells that wander and "
        "re-form with the music.",
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
         "focus": 0.07, "focusWidth": 0.032, "focusSweep": 0.09, "focusPeriod": 47.0, "focusFloor": 0.0},
        # B, a second large figure about a centre off to the south-west. At rest silent: its amplitude follows the
        # sound's roughness (a route), so interference is an EVENT -- a rough, turbulent passage bends A's clean figure
        # into crossing, hyperbolic nodal lines; a clean or sustained passage shows A's figure itself
        {"kind": "point", "position": [-20.0, 2.0, 15.0], "order": 6, "bandLow": 0.15, "bandHigh": 0.3, "reach": 90.0,
         "gain": 1.0, "amplitude": 0.0, "breath": 0.5, "drift": 9.0, "driftPeriod": 80.0, "spin": -0.04,
         "orders": [3, 2, 5],
         "focus": 0.23, "focusWidth": 0.032, "focusSweep": 0.09, "focusPeriod": 61.0, "focusFloor": 0.0},
        # M, the medium figure: finer rings and petals about a centre a few metres from A's -- nested inside the large
        # lobes, but eccentric, so the frame is never a bullseye (rings live here, not in the large figure)
        {"kind": "point", "position": [3.0, 3.0, -3.5], "order": 8, "bandLow": 0.3, "bandHigh": 0.66, "reach": 80.0,
         "gain": 0.9, "amplitude": 0.5, "breath": 0.7, "drift": 6.0, "driftPeriod": 100.0, "spin": 0.045,
         "orders": [0, 8, 4, 6, 3, 5],
         "focus": 0.48, "focusWidth": 0.04, "focusSweep": 0.16, "focusPeriod": 29.0, "focusFloor": 0.0},
        # F, the fine layer: standing bands, squares and hexagons, faint and local
        {"kind": "lattice", "position": [0.0, -6.0, 0.0], "order": 3, "bandLow": 0.66, "bandHigh": 1.0,
         "reach": 46.0, "gain": 0.55, "amplitude": 0.1, "breath": 1.5, "depth": 9.0, "drift": 2.0,
         "driftPeriod": 60.0, "spin": 0.02, "orders": [2, 1, 3]},
    ], modes=17.0, lambdaMax=16.0, lambdaMin=2.6, competition=1.5, selectivity=4.0, limit=1.5, breath=0.15,
       # E, transients: every onset launches a travelling ring (kick broad and slow, snare sharp and local)
       ripple=0.5, rippleSpeed=9.0, rippleLife=1.6, rippleWidth=0.9, rippleArea=30.0, rippleCount=2.0, rippleFade=0.35, rippleU=[0.36, 0.5, 0.85],
       # ADR-1404: fixed mode places (a route on `modes` adds modes, it never re-places them), and per-band response
       layout=LAYOUT, attack=[0.18, 0.08, 0.04], release=[3.0, 1.4, 0.6])
    layers = ground_readers(s)
    g = ground(s, layers, write_mesh=True)
    # LOOKING INTO THE GROUND: high and steep (about 47 degrees down) on a 30 mm lens, so the frame's top edge meets
    # the ground inside r ~60 m all round the arc and the basin's walls (they rise from r ~90 m) never enter it -- the
    # frame is all ground (checked by ray-casting the top edge and corners along the whole loop)
    cam_r, cam_h, cam_f = CAMERA
    s.camera = {"mode": 1, "position": [0.0, cam_h, cam_r], "target": [-2.0, -3.0, 0.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.arc_camera([-2.0, -3.0, 0.0], cam_r, cam_h, period=160.0, centre_deg=8.0, sweep_deg=80.0, steps=24, side=0.0,
                 lift=0.0, height_bob=2.0)
    s.params_({"camera/lens/focalLength": cam_f})
    F = field_instrument(s)
    # THE MUSIC'S HAND ON THE FIGURE (08-brief §8, Stage 5): the music chooses WHICH figure leads and how hard each scale
    # is excited -- never the finished height directly.
    #   the sound's brightness moves the large focus (a brighter sound leads higher-order lobes; slow, so the figure
    #     morphs over seconds) and, faster, the medium focus (petal subdivision follows the timbre)
    #   bass excites the large figure's relief, held sound the medium's intricacy, hats the fine vibration -- each a
    #     bounded, smoothed gain on its own scale; kicks and snares also strike the modes and launch rings (the field's
    #     own response, ADR-1403/1404); the song's build fills the bank (intensity -> modes, ADR-1404's layout)
    # a scale's height: its reader's strength when the relief is baked (ADR-1407), else its own deformer's amount
    def D(n):
        return ("field/relief-%s/strength" % ["large", "medium", "fine"][n - 1]) if BAKE else \
            "procedural/ground/deform/%d/amount" % n
    s.route(R("sonic.brightness.slow", F + "source/0/focus", 0.08, attackMs=2000, decayMs=4000),
            R("sonic.brightness.slow", F + "source/1/focus", 0.06, attackMs=2500, decayMs=5000),
            R("sonic.brightness", F + "source/2/focus", 0.14, attackMs=400, decayMs=1200),
            R("sonic.roughness.slow", F + "source/1/amplitude", 0.45, attackMs=1500, decayMs=3000),
            R("response.bass", D(1), 1.0, op="multiply", gain=0.35, offset=1.0, attackMs=80, decayMs=900),
            R("response.sustain", D(2), 1.0, op="multiply", gain=0.3, offset=1.0, attackMs=1200, decayMs=3000),
            R("response.hat", D(3), 1.0, op="multiply", gain=1.5, offset=1.0, attackMs=10, decayMs=220))
    s.control = control_map(cc, pads)
    s.route(R("control.relief", "procedural/ground/deform/1/amount", 3.0, **CCR))
    if PALETTE == "M" and BAKE:
        s.route(R("control.palette", "palette/select", 9.0, attackMs=0, decayMs=0),
                R("control.paletteLock", "palette/lock", 1.0, attackMs=0, decayMs=0))
    s.params_(POST)
    if DEBUG in ("signed", "nodal", "envelope"):  # a debug material: none of the colour routes' targets exist
        return s, cc, pads
    # a ramp-tinted term's colour is its constant2 (half) and constant3 (whole); a route scales both
    RP = "material/cwGround/op/%d/ramp/%s"
    def ramp_paths(label):
        return [RP % (g[label], "constant2"), RP % (g[label], "constant3")]
    counter, hollow, sheen = ramp_paths("fill"), ramp_paths("hollow"), ramp_paths("sheen")
    line = "material/cwGround/op/%d/constant/constant" % g["line"]
    if BAKE:  # the grit is a constant of its own beside the colour field (light pass): the build turns it, whole
        grit, grit_mid = "material/cwGround/op/%d/constant/constant" % g["grit"], None
    else:
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
    if colour_routes and PALETTE != "M":  # (an absolute hue shift: the ten palettes own the hues instead)
        s.route(*[R("intensity", path, round((tgt[c] - base[c]) * w, 5), comp=c, attackMs=4000, decayMs=9000)
                  for c in range(3) for path, w in ((grit, 1.0), (grit_mid, 0.5)) if path is not None])
    if PALETTE == "M" and BAKE:
        s.palette = palette_block(g)
        # THE MUSIC WITHIN A PALETTE (12-brief §3, intents, not channel mappings): every route moves a LOCALISED or
        # REGIONAL term, never the frame's brightness, and none of them touches a hue -- the palette owns the hues
        tf = "material/cwGround/op/%d/remap/constant" % g["tintField"]
        s.route(
            # bass: the extent of the broad colour regions -- the colour field's span narrows, so a lobe's colour
            # reaches further from its centre toward the nodes
            *[R("response.bass", tf, 1.0, comp=c, op="multiply", gain=-0.45, offset=1.0, attackMs=120, decayMs=1200)
              for c in (0, 1)],
            # held sound (the low-mid body of a mix): the dominant surface shifts -- the colour coordinate leans from
            # the primary surface toward the secondary over seconds
            *[R("response.sustain", tf, 0.22, comp=c, attackMs=2500, decayMs=5000) for c in (2, 3)],
            # mids (snares, the body of the beat): the medium folds' light (with LINE2; otherwise the snare's route
            # onto the crest lines, above, carries the mids)
            *[R("snareEnv", p_, 1.0, op="multiply", gain=1.4, offset=1.0, attackMs=0, decayMs=420)
              for p_ in ([RP % (g["line2"], "constant2"), RP % (g["line2"], "constant3")] if LINE2 else [])])
        s.route(*[R("sustain", p_, 1.0, op="multiply", gain=1.2, offset=1.0, attackMs=1500, decayMs=4000) for p_ in hollow],
                *[R("kickEnv", p_, 1.0, op="multiply", gain=1.6, offset=1.0, attackMs=0, decayMs=380) for p_ in hollow],
                R("snareEnv", line, 1.0, op="multiply", gain=0.9, offset=1.0, attackMs=0, decayMs=260),
                *[R("hatEnv", p_, 1.0, op="multiply", gain=0.5, offset=1.0, attackMs=0, decayMs=180) for p_ in sheen],
                *[R("flux", p_, 1.0, op="multiply", gain=0.8, offset=1.0, attackMs=300, decayMs=1500) for p_ in counter])
    s.route(*[R("control.light", p_, 1.0, op="multiply", gain=1.5, offset=1.0, **CCR) for p_ in counter],
            R("control.light", line, 1.0, op="multiply", gain=2.0, offset=1.0, **CCR))
    return s, cc, pads


def palette_block(g):
    """ADR-1043: the ten palettes as states, and the bindings that multiply the grey program's colour constants by the
    active palette's roles."""
    M = "material/cwGround/op/%d/%s/%s"
    binds = []
    for label in (("line2",) if LINE2 else ()) + ("wave", "hollow", "sheen", "fill"):    # tint ramps: the colour is constant2 and constant3
        binds += [(label, M % (g[label], "ramp", "constant2")), (label, M % (g[label], "ramp", "constant3"))]
    binds += [("line", M % (g["line"], "constant", "constant")), ("grit", M % (g["grit"], "constant", "constant")),
              ("surfaceA", M % (g["tint"], "ramp", "constant")), ("shadow", M % (g["tint"], "ramp", "constant2")),
              ("surfaceB", M % (g["tint"], "ramp", "constant3")),
              ("sky", M % (g["key"], "ramp", "constant")), ("keymid", M % (g["key"], "ramp", "constant2")),
              ("key", M % (g["key"], "ramp", "constant3")),
              ("view0", M % (g["view"], "ramp", "constant")), ("view1", M % (g["view"], "ramp", "constant2")),
              ("view2", M % (g["view"], "ramp", "constant3"))]
    # ADR-1420: the selector -- the palette changes at phrase scale, on a phrase boundary at least PAL_HOLD seconds
    # after the last change, to the next palette of a COMPOSED order (each step a contrast of temperature and
    # darkness: deep blue -> molten -> glacial -> crimson -> green -> rose -> metal -> ultraviolet -> silver -> oil),
    # blended straight in OKLab over PAL_TRANSITION seconds; CC 9 selects, CC 10 locks
    return {"states": [palette_state(n, h) for n, h in MULTI_PALETTES],
            "bindings": [{"role": r, "target": t, "mode": "multiply"} for r, t in binds],
            "position": PAL_INDEX, "saturation": 1.0, "value": 1.0,
            "selector": {"select": int(PAL_INDEX), "transition": PAL_TRANSITION, "lock": PAL_LOCK, "hold": PAL_HOLD,
                         "advance": {"signal": "beat.phrasePulse", "threshold": 0.5, "order": PAL_ORDER}}}


BUILDERS = {"world": build_world}
OUT_DIRS = {k: os.path.join(REPO, "examples", v["id"]) for k, v in SCENES.items()}
DESCRIPTION_TAIL = {"world": "The ground's figure is drawn by one resonance field (ADR-1400, ADR-1403 to 1407); the scene carries no track of its own."}


def write_scene(key):
    os.makedirs(OUT_DIRS[key], exist_ok=True)
    s, cc, pads = BUILDERS[key]()
    nf = sum(1 for n in s.nodes if n.get("kind") == "field")
    # the GPU uploads 16 fields (spatial::kMaxGpuFields); the rest sample 0 with one log line -- the relief went flat
    assert nf <= 16, "%d fields: the GPU holds 16" % nf
    pp, sp = s.write(OUT_DIRS[key])
    if getattr(s, "grids", None):  # the kit has no grids; they go in the scene file's top level
        with open(sp) as f:
            doc = json.load(f)
        doc["grids"] = s.grids
        with open(sp, "w") as f:
            json.dump(doc, f, indent=1)
            f.write("\n")
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
    ap.add_argument("--variant", action="store_true",
                    help="also write a review copy of the live project at --out (no audio: feed it with avgen --audio)")
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
        print(variant(pp, a.out, a.camera, params))


if __name__ == "__main__":
    main()

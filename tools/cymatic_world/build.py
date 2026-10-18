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

OUT = os.environ.get("CW_OUT") or os.path.join(REPO, "examples", "cymatic-world")   # CW_OUT: a look-dev copy
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


# the overhaul (stage A review): the oblique camera frames the floor out to ~90 m, and the terraces, gullies and
# outcrops there read as lumpy blobs competing with the figure -- the amphitheatre is widened by BASIN (the floor a
# clean plate to ~95 m, the terraces and rim behind it as the frame's middle distance) and the floor's outcrops go
BASIN = float(os.environ.get("CW_BASIN", "1.35"))


def _features():
    f = []
    # an amphitheatre of terraces stepping down to the floor: each a flat ring at its own level, irregular so the
    # steps read as worn stone, not a stadium
    for k, (r, level, width) in enumerate([(0.0, -7.0, 72.0 * BASIN), (52.0 * BASIN, -2.5, 12.0), (68.0 * BASIN, 2.0, 10.0),
                                           (84.0 * BASIN, 7.0, 10.0)]):
        path = [[0.0, level, 0.0]] if r == 0.0 else _ring(r, 28, level, 0.06, k)
        f.append({"name": "terrace%d" % k, "kind": "flat", "path": path, "width": width, "amplitude": 0.0,
                  "falloff": 2.2 if r == 0.0 else 1.6, "flatten": 0.97 if r == 0.0 else 0.85,
                  "roughness": 0.05 if r == 0.0 else 0.2, "smoothing": 2 if r else 0})
    # the rim: a broken ring of ridges 105 m out, high behind the basin and low toward the camera's gap
    f.append({"name": "rim", "kind": "ridge", "path": _ring(105.0 * BASIN, 30, 0.0, 0.08, 4), "width": 30.0,
              "amplitude": 16.0, "falloff": 1.4, "flatten": 0.0, "roughness": 1.4, "smoothing": 2})
    f.append({"name": "rimBack", "kind": "ridge", "path": [[-90.0 * BASIN, 0.0, -80.0 * BASIN], [-20.0 * BASIN, 0.0, -125.0 * BASIN],
                                                      [70.0 * BASIN, 0.0, -95.0 * BASIN]],
              "width": 36.0, "amplitude": 18.0, "falloff": 1.1, "flatten": 0.0, "roughness": 1.8, "smoothing": 3})
    # radial gullies cut through the terraces (folds that break the rings)
    for k, a in enumerate([0.9, 2.3, 3.6, 4.4, 5.6]):
        d = (math.cos(a), math.sin(a))
        f.append({"name": "gully%d" % k, "kind": "valley",
                  "path": [[d[0] * 38.0 * BASIN, 0.0, d[1] * 38.0 * BASIN], [d[0] * 80.0 * BASIN, 0.0, d[1] * 80.0 * BASIN],
                           [d[0] * 125.0 * BASIN, 0.0, d[1] * 125.0 * BASIN]],
                  "width": 9.0 + 2.0 * k % 5, "amplitude": 4.5, "falloff": 1.3, "flatten": 0.0, "roughness": 0.8,
                  "smoothing": 2})
    # outcrops on the floor: low basalt shoulders the figure has to flow around
    for k, (x, z, h) in enumerate([] if BASIN > 1.0 else [(-24.0, -30.0, 3.0), (33.0, -14.0, 2.5)]):
        f.append({"name": "outcrop%d" % k, "kind": "ridge", "path": [[x, 0.0, z]], "width": 9.0, "amplitude": h,
                  "falloff": 1.8, "flatten": 0.0, "roughness": 2.0, "smoothing": 0})
    # the gap in the rim toward the camera (+z): the way in, and a long view out
    f.append({"name": "gap", "kind": "valley", "path": [[0.0, 0.0, 70.0 * BASIN], [6.0, 0.0, 115.0 * BASIN], [18.0, 0.0, 170.0 * BASIN]],
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


MESH_CORE = float(os.environ.get("CW_MESH_CORE", "36"))
MESH_MID = float(os.environ.get("CW_MESH_MID", "1.025"))


def grid_axis(core=None, half=88.0, step=None, extent=2600.0, mid=None, growth=1.12):
    """One axis of the ground's tensor grid: `step` apart within +-core (the figure's middle, the nearest ground the
    camera sees), then each step `mid` times the last out to +-half (the far ground, at the top of the frame, needs
    fewer vertices for the same pixels), then `growth` times out to +-extent (never in frame: it closes the world)."""
    step = step if step is not None else FINE_STEP
    core = core if core is not None else MESH_CORE
    mid = mid if mid is not None else MESH_MID
    n = int(round(core / step))
    inner = [i * core / n for i in range(-n, n + 1)]
    outer = []
    x, d = core, core / n
    while x < extent:
        d *= mid if x < half else growth
        x = min(x + d, extent)
        outer.append(x)
    return np.array([-v for v in reversed(outer)] + inner + outer)


MESH_HEX = os.environ.get("CW_MESH_HEX", "1") == "1"


def hex_ground_mesh():
    """resonator-quality, Phase D: an offset-lattice triangulation of the same graded grid. Rows run along x; every
    other row's vertices sit at the midpoints of the row below's (an offset, "hexagonal" lattice), so no triangle
    pair forms a quad with a diagonal. The square grid's alternating diagonals put a two-cell
    zigzag along any steep lip that crosses them at an angle (the "serrated crest" of the earlier reports: it shows
    through every xz-dependent mask on a near-vertical wall); the offset lattice has no preferred diagonal, and its
    triangles are near-isotropic."""
    xs = grid_axis()
    zs = grid_axis()   # rows a full cell apart: the same vertex count as the square grid (rows sqrt(3)/2 apart
    # made the triangles equilateral but cost 3 ms more at High for 9% more vertices; at a full cell they are
    # isosceles, still with no alternating diagonal, and cost ~1 ms)
    mids = 0.5 * (xs[:-1] + xs[1:])
    rows, index = [], []
    pts = []
    for j, z in enumerate(zs):
        row = xs if j % 2 == 0 else mids
        index.append(len(pts))
        pts.extend((x, z) for x in row)
        rows.append(len(row))
    pts = np.array(pts, dtype=np.float64)
    hs = world_heights(pts)
    pos = np.stack([pts[:, 0], hs, pts[:, 1]], axis=1)
    tris, strip = [], []
    n = len(xs)
    for j in range(len(zs) - 1):
        a0, b0 = index[j], index[j + 1]
        i = np.arange(n - 1)
        k = np.arange(n - 2)
        if j % 2 == 0:   # full row below (n), midpoint row above (n - 1)
            up = np.stack([a0 + i, b0 + i, a0 + i + 1], 1)
            down = np.stack([a0 + k + 1, b0 + k, b0 + k + 1], 1)
        else:            # midpoint row below (n - 1), full row above (n)
            up = np.stack([a0 + i, b0 + i, b0 + i + 1], 1)
            down = np.stack([a0 + k, b0 + k + 1, a0 + k + 1], 1)
        # left to right, the two kinds interleaved, so each vertex is reused while it is still in the post-transform
        # cache (all of one kind then all of the other cost ~1 ms at High)
        both = np.empty((2 * n - 3, 3), dtype=np.int64)
        both[0::2] = up
        both[1::2] = down
        tris.append(both)
        strip.append(np.full(2 * n - 3, 0.5 * (zs[j] + zs[j + 1])))
    tri = np.concatenate(tris, 0).astype(np.int64)
    # front to back by STRIP, each strip's triangles left to right in the order built: sorting by each triangle's own
    # centroid split every strip into its up- and its down-pointing triangles, which halved the vertex cache's reuse
    # (measured: +3 ms at High for 9% more vertices)
    return _finish_ground(pos, tri, np.concatenate(strip))


def _finish_ground(pos, tri, order_z=None):
    """Front-to-back order, upward winding, area-weighted vertex normals (shared by both triangulations)."""
    centroid_z = pos[tri, 2].mean(axis=1) if order_z is None else order_z
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


def ground_mesh():
    if MESH_HEX:
        return hex_ground_mesh()
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
FIELD_FINE = "cymaticFine"   # the medium and fine scales' resonator (the overhaul: the large one holds the four families)
HEART = (0.0, 12.0, 0.0)          # the basin's voice: the low modes' centre, above the floor's middle
SECOND = (-23.0, 0.0, 9.0)        # the world's second centre (interference with the first)
GROUND_REL = "meshes/cw-ground.glb"


def _unit(v):
    n = math.sqrt(sum(x * x for x in v))
    return [x / n for x in v]


def ground_y(x, z):
    return float(world_heights(np.array([[x, z]], dtype=np.float64))[0])


def resonator(s, sources, name=None, **over):
    """A resonator: shared defaults, its sources and overrides (the scene has two: the large families, and the medium
    and fine scales -- a resonator holds at most four sources)."""
    res = {
        "modes": 14.0, "fLow": 40.0, "fHigh": 6000.0, "lambdaMax": 34.0, "lambdaMin": 5.0, "scale": 1.0,
        "selectivity": 3.0, "contrast": 0.7, "persistence": 2.8, "buildup": 0.3, "memory": 1.6,
        "sustain": 1.0, "transient": 0.9, "idle": 0.5, "breath": 0.16, "limit": 0.9, "competition": 3.0,
        "sources": sources, "channel": "displacement",
    }
    res.update(over)
    s.nodes.append({"name": name or FIELD, "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "resonance", "seed": 7, "strength": 1.0, "falloff": {"kind": "none"},
                              "resonance": res}})


# the figure's extent (the overhaul, stage A review): the relief and its shading fade out between FIGURE_INNER and
# FIGURE_OUTER metres from the basin's middle, so the far ground the oblique camera sees at a grazing angle -- in cells
# too coarse to carry a ridge -- holds the plain basin, not a serrated figure (a normalised sand ridge ignores a
# source's reach, so the fade has to be the reader's)
FIGURE_INNER = float(os.environ.get("CW_FIG_INNER", "55"))
FIGURE_OUTER = float(os.environ.get("CW_FIG_OUTER", "95"))
FIGURE_FALLOFF = {"kind": "smoothstep", "inner": FIGURE_INNER, "outer": FIGURE_OUTER}


def reader(s, name, channel, extra=None, frame=None, ref=None):
    r = {"channel": channel}
    r.update(extra or {})
    faded = name.startswith("relief-") or name.startswith("shade-")
    f = {"kind": "resonance", "reference": ref or FIELD, "strength": 1.0,
         "falloff": dict(FIGURE_FALLOFF) if faded else {"kind": "none"}, "resonance": r}
    f.update(frame or {})
    s.nodes.append({"name": name, "kind": "field", "position": [0, 0, 0], "field": f})


# THE SHAPE LANGUAGE (08-brief, Stage 2): the ground's height is built per SCALE, one reader and one deformer each,
# every reader listening to its band of the same designed bank (large u < 0.3, medium 0.28-0.7, fine > 0.68). The
# prototypes differ in how a scale's figure becomes geometry:
#   P1 structured modal field -- the signed standing figure F is the height (lobes up, opposite lobes down)
#   P2 nodal boundaries       -- the nodal lines stand as ridges (sand) between antinode basins (-|F|); medium: creases
#   P3 hybrid                 -- large: signed lobes with their nodal ridges; medium: nodal creases; fine: vibration
SCALES = {"large": (0.0, 0.32), "medium": (0.3, 0.7), "fine": (0.68, 1.0)}
LAYOUT = 24.0   # the bank's fixed size (ADR-1404): mode i sits at u = i / 23 (24, the most a resonator holds: room for
# four large families, the overhaul; 22 before) -- the large band's modes at u = 0, .043, .087, .13 (plate), .174
# (hexagonal), .217, .261 (radial) and .304 (arcs)
MODES = 22.0
LAMBDA_MAX = float(os.environ.get("CW_LAMBDA_MAX", "26.0"))
# the families (05-plan §1.1): P plate, H hexagonal, R radial, A arcs -- the plate's (n, m) list (ADR-1471), each family's
# base amplitude, its LFO (name, period s, phase) and the LFO's reach
PLATES = [[3, 2], [5, 1], [4, 3], [6, 1], [5, 2]]
FAM_BASE = [0.35, 0.0, 0.0, 0.0]
# the families' reach (m): the figure fills the basin floor and calms toward the walls, where the oblique camera sees
# the ground at a grazing angle in cells too coarse to carry it (the far teeth of stage A) -- the air takes over there
FAM_REACH = float(os.environ.get("CW_FAM_REACH", "62"))
FAM_GAIN = [0.85, 0.9, 0.8, 0.9]
FAM_ONLY = os.environ.get("CW_FAM_ONLY")   # look development: one family alone at full amplitude, no director
if FAM_ONLY is not None:
    FAM_BASE = [1.0 if i == int(FAM_ONLY) else 0.0 for i in range(4)]
    FAM_GAIN = [0.0] * 4
FAM_LFO = [("famPlate", 97.0, 0.0), ("famHex", 131.0, 0.45), ("famRadial", 151.0, 0.7), ("famArcs", 173.0, 0.2)]
# DEBUG VIEWS (08-brief §16), build-time like the palettes: CW_DEBUG=
#   mode:<i>        the geometry of ONE mode (its nodal figure), every scale's reader listening to it alone
#   scale:<name>    only that scale's deformer (large, medium, fine)
#   signed          the material shows the signed standing figure F (blue down, red up) over the real geometry
#   nodal           the material shows the nodal lines (sand) of the whole figure, white on black
#   envelope        the material shows the excitation |F| (the fast figure's amplitude), black to orange
# the light pass's LAYER views (10-brief), on the real program and palette:
#   albedo          the base colour field and the grit alone, unlit (layers 1-2: base + resonant colour variation)
#   light           the painted light alone, on white (the key, its ambient floor, the fill, the glints)
#   emission        the crest lines, the travelling light and the hollow glow alone (layers 3-4)
# the quality pass's geometry views (resonator-quality, Phase C):
#   normals         the deformed surface normal as colour (0.5 + 0.5 n), unlit
#   wire            the mesh's square grid (the core's FINE_STEP cells) in red over grey lit by the key alone
DEBUG = os.environ.get("CW_DEBUG", "")
# ADR-1407: the relief is baked into a grid once a frame (CW_BAKE=0: every vertex evaluates the readers, for comparison)
BAKE = os.environ.get("CW_BAKE", "1") != "0"
LINE2 = os.environ.get("CW_LINE2", "0") == "1"  # Stage C's medium crest light (off since Stage G: 4 ops)
WAVE = float(os.environ.get("CW_WAVE", "2.2"))  # Stage D: the travelling light's gain into its grid (0 = off)
BAKE_RES = int(os.environ.get("CW_BAKE_RES", "1024"))
# ART PASS II (08-plan §2.3-2.4): the excitation from the colour field (frees a reader, its grid and a program slot),
# a travelling SCAN of light on each kick, onset RIPPLES back in the geometry (riding the figure, low), and a BASS
# WAVEFRONT: the bass of the last seconds laid out outward from the basin's heart, in the relief
AMP_FROM_TINT = os.environ.get("CW_AMP_FROM_TINT", "0") == "1"
AMP_GAIN = float(os.environ.get("CW_AMP_GAIN", "2.2"))         # tint^2 x this ~ the old excitation's scale
SCAN = float(os.environ.get("CW_SCAN", "0"))                    # the scan's light gain (0 = off)
SCAN_SPEED = float(os.environ.get("CW_SCAN_SPEED", "45"))       # m/s across the basin
SCAN_WIDTH = float(os.environ.get("CW_SCAN_WIDTH", "4"))        # m, the band's half width
SCAN_DECAY = float(os.environ.get("CW_SCAN_DECAY", "1.1"))      # 1/s
SCAN_FROM = (-95.0, 0.0, 30.0)                                  # upstream of the frame's left edge
SCAN_AXIS = (0.96, 0.0, -0.28)                                  # across the frame, a little away from the camera
RIPPLE_RELIEF = float(os.environ.get("CW_RIPPLE_RELIEF", "0"))  # metres of the onset rings in the geometry (0 = off)
# the snare's flash on every nodal line at once (a frame-wide event, brief §4.2 "not the same full-screen flash"): the
# snare's energy now travels (the ripples, the scan's glimmer); its flash on the lines comes down
SNARE_LINE = float(os.environ.get("CW_SNARE_LINE", "0.9"))
BASS_FRONT = float(os.environ.get("CW_BASS_FRONT", "0"))        # metres of the bass wavefront (0 = off)
BASS_SPEED = float(os.environ.get("CW_BASS_SPEED", "18"))       # m/s outward from the heart
NORMAL_CELLS = float(os.environ.get("CW_NORMAL_CELLS", "1"))   # the normal's finite-difference step, in bake cells
BAKE_HALF = float(os.environ.get("CW_BAKE_HALF", "130"))   # metres (86 before the oblique camera)   # metres: the frame's top edge meets the ground inside ~80 m (camera B, 21:9)
# the camera (the correction round, owner: "pull back"): A the shape-language framing, B pulled back (chosen), C further
CAMERAS = {"A": (30.0, 32.0, 30.0), "B": (36.0, 42.0, 28.0), "C": (40.0, 48.0, 26.0),
           # the overhaul's C2: an oblique view, ~30 degrees down, the plate in the lower two thirds and the basin's
           # walls, the ranges and the sky above (05-plan §1.1)
           "D": (78.0, 44.0, 26.0)}
CAMERA = CAMERAS[os.environ.get("CW_CAM", "D")]
PROTO = os.environ.get("CW_PROTO", "P2")   # Stage 2: P2 (nodal boundaries) chosen
LARGE_SHARP = float(os.environ.get("CW_LARGE_SHARP", "0.45"))
# how deep the antinodes sink under the large figure (-1.2 before the overhaul: a field of deep pits that hid the
# nodal network; shallower basins let the plate's crossing lines read as the figure)
LARGE_CREASE = float(os.environ.get("CW_LARGE_CREASE", "0.0"))
GEOM_RIPPLES = os.environ.get("CW_GEOM_RIPPLES", "0") == "1"
LARGE_FIGURE = float(os.environ.get("CW_LARGE_FIGURE", "0.15"))   # the signed lobes (domes and dips), per unit relief
LARGE_HEIGHT = float(os.environ.get("CW_LARGE_HEIGHT", "1.5"))    # metres per unit (2.8 before)
LARGE_ROUND = float(os.environ.get("CW_LARGE_ROUND", "0.4"))
MEDIUM_ROUND = float(os.environ.get("CW_MEDIUM_ROUND", "0.5"))
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
    # (the overhaul, stage A review: the families' signatures must read in grey -- the nodal lines as THIN ridges over
    # broad, shallow, signed lobes, the Chladni plate's look, instead of 2.8 m walls between pits: a lower, narrower
    # sand line, the figure's own sign as gentle domes and dips, and no antinode pits)
    "P2": {"large": ({"reliefSand": 1.0, "sandNormalized": True, "sharpness": LARGE_SHARP, "reliefAmplitude": LARGE_CREASE,
                     "reliefRound": LARGE_ROUND, "reliefFigure": LARGE_FIGURE, "reliefVibration": 0.15}, LARGE_HEIGHT),
           "medium": ({"reliefSand": 1.0, "sharpness": 1.0, "reliefAmplitude": 0.0, "reliefRound": MEDIUM_ROUND}, 0.22),
           "fine": ({"reliefSand": 0.0, "reliefVibration": 1.0}, 0.15)},
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
                                          *([] if AMP_FROM_TINT else [("shade-amp", "amplitude", dict(win), 512)]),
                                          # 10-brief Stage B, the colour field: the large figure's signed standing
                                          # pattern F -- each lobe a broad colour region, anchored to the geometry
                                          ("shade-tint", "relief", {"reliefSand": 0.0, "reliefFigure": 1.0, **large}, 640)):
            reader(s, name, channel, extra,
                   frame={"strength": TINT_GAIN} if name == "shade-tint" and PALETTE == "M" else None)
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
    if BAKE and SCAN > 0.0:
        # the SCAN (art pass II, layer C): every kick launches a narrow band of light that crosses the basin at
        # SCAN_SPEED and fades (an Onset field, planar: exact under seek, ADR-1116); the program lays it on the ridges
        s.nodes.append({"name": "scan", "kind": "field", "position": list(SCAN_FROM),   # (planar: from the node)
                        "field": {"kind": "onset", "onsetSource": "low", "axis": _unit(SCAN_AXIS),
                                  "waveGeometry": "planar", "audioSpeed": SCAN_SPEED, "onsetWidth": SCAN_WIDTH,
                                  "onsetDecay": SCAN_DECAY, "strength": 1.0, "falloff": {"kind": "none"}}})
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
        if name == "large" and not GEOM_RIPPLES:
            # (the overhaul, stage A: the onset ripples' rings drew concentric circles over EVERY family, so a plate
            # or a hex figure read as radial; the rings now live in the light -- the travelling light -- not the relief)
            r["rows"] = "modes"
        reader(s, "relief-" + name, "relief", r, frame={"strength": amount} if BAKE else None,
               ref=FIELD if name == "large" else FIELD_FINE)
        layers.append(("relief-" + name, amount))
    if not BAKE:
        return layers
    if RIPPLE_RELIEF > 0.0 and only_mode is None and only_scale is None:
        # layer B (art pass II): the onset rings ride the figure as low travelling ripples (ADR-1409 ripples only)
        reader(s, "relief-ripple", "displacement", {"rows": "ripples"}, frame={"strength": RIPPLE_RELIEF})
        layers.append(("relief-ripple", RIPPLE_RELIEF))
    if BASS_FRONT > 0.0 and only_mode is None and only_scale is None:
        # layer A (art pass II): the bass history laid out outward from the heart (ADR-1116, smoothed, ADR-1370): a
        # loud low moment becomes a broad ring that lifts the figure as it travels out, and is gone
        s.nodes.append({"name": "relief-bass", "kind": "field", "position": [0, 0, 0],
                        "field": {"kind": "spectrum", "audioBand": "range", "bandLow": 0.02, "bandHigh": 0.12,
                                  "audioSpeed": BASS_SPEED, "audioSmooth": 0.05, "point": [HEART[0], -7.0, HEART[2]],
                                  "waveGeometry": "radial", "strength": BASS_FRONT, "falloff": {"kind": "none"}}})
        layers.append(("relief-bass", BASS_FRONT))
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
    "B": {"line2": "#7fb6ff", "line2I": 0.4, "wave": "#9ff0ff", "waveI": float(os.environ.get("CW_WAVE_I", "2.5")), "violet": "#2c0c6e", "blue": "#0a2c84", "stone": "#050a22", "grit": "#0b4458", "key": "#62ecff", "fill": "#c13bff", "rim": "#ff8fe6",
          "sky": "#1a1250", "haze": "#2a2275", "line": "#d4fffb", "hollow": "#ff3fd2", "sheen": "#9fe6ff",
          "sheenI": float(os.environ.get("CW_SHEEN_I", "0.3")),
          "keyI": float(os.environ.get("CW_KEY_I", "2.0")), "fillI": 0.9, "rimI": 0.35, "skyI": float(os.environ.get("CW_SKY_I", "0.6")),
          "lineI": float(os.environ.get("CW_LINE_I", "0.7")),
          "hollowI": 0.4},
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


_oklab, _linear = kit.oklab, kit.oklab_linear   # OKLab (kit), the space the engine blends palettes in (ADR-1043)


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
    "Abyssal Resonance": {"alb": 1.05, "keySrc": "resonant", "keyMix": 0.15, "key": 1.15, "sky": 0.8, "hollow": 0.8,
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
    "Glacial Spectral": {"alb": 1.15, "keySrc": "peak", "keyMix": 0.9, "key": 1.1, "sky": 0.9, "hollow": 0.25,
                         "sheen": 2.0, "line": 1.0, "gritMix": 0.6, "fillSrc": "spectral"},
    # green emerging from darkness, chartreuse only on resonant peaks
    "Chlorophyll After Dark": {"alb": 0.8, "keySrc": "resonant", "keyMix": 0.5, "gritMix": 0.1, "line": 0.9},
    # soft, smoky, low contrast, pearl: a pearly key, a higher ambient floor, softer crests and glow (the quality pass's
    # brighter floor made the old 2.2 milky: 1.25)
    "Rose Quartz": {"alb": 0.9, "keySrc": "peak", "keyMix": 0.6, "key": 0.7, "sky": 1.25, "line": 0.55,
                    "line2": 0.6, "hollow": 0.45, "sheen": 0.7, "gritMix": 0.1},
    # pure form: the geometry alone carries it
    "Monochrome Silver": {"keyMix": 1.0, "hollow": 0.5},
    # petrol and oil: a cool neutral key, the view layer carries the colour, crests rose-gold but low, little glow
    "Prismatic Oil": {"alb": 0.9, "keySrc": "secondary", "keyMix": 0.7, "hollow": 0.35, "line": 0.7, "gritMix": 0.0},
}


# THE CHROMATIC FIELD'S GRADIENTS (resonator-quality): four OKLCH stops (L, C, hue in degrees) per palette, the gradient
# the surface runs through (ADR-1470 blends them in OKLab). Each keeps its palette's family and widens it into a
# multi-hue journey of related colours: lighter and more chromatic than the old surface roles, so the body of a cell
# carries colour between the light and the dark. A ridge sits one step along (RIDGE_SHIFT), so g3 is where ridges and
# the strongest lobes end up. Display colours, scaled to albedo like the roles (PAL_ALBEDO).
GRADIENTS = {
    # indigo -> cobalt -> violet -> a narrow fuchsia (the crest lines keep the icy cyan)
    "Abyssal Resonance":      [(0.36, 0.15, 272), (0.50, 0.18, 250), (0.58, 0.22, 308), (0.68, 0.25, 348)],
    # indigo-violet sculpture, plum, and the red only where the gradient ends (ridges, the hottest lobes)
    "Crimson Singularity":    [(0.32, 0.13, 300), (0.38, 0.16, 322), (0.44, 0.18, 350), (0.54, 0.20, 18)],
    "Molten Glass":           [(0.40, 0.12, 26), (0.50, 0.15, 38), (0.60, 0.17, 52), (0.72, 0.15, 74)],
    # violet foundation turning to fuchsia and magenta
    "Ultraviolet Bloom":      [(0.40, 0.14, 288), (0.48, 0.18, 305), (0.54, 0.21, 330), (0.64, 0.18, 352)],
    # verdigris grey into copper and brass
    "Oxidized Metal":         [(0.42, 0.04, 190), (0.48, 0.07, 62), (0.56, 0.10, 48), (0.68, 0.10, 76)],
    "Glacial Spectral":       [(0.48, 0.07, 258), (0.58, 0.09, 238), (0.67, 0.10, 214), (0.78, 0.07, 198)],
    "Chlorophyll After Dark": [(0.40, 0.08, 168), (0.50, 0.11, 155), (0.58, 0.14, 140), (0.72, 0.16, 120)],
    "Rose Quartz":            [(0.44, 0.07, 330), (0.52, 0.09, 346), (0.60, 0.11, 2), (0.72, 0.08, 22)],
    # pure form: a whisper of cool-to-warm, never a hue
    "Monochrome Silver":      [(0.30, 0.012, 255), (0.40, 0.015, 248), (0.50, 0.012, 236), (0.64, 0.010, 70)],
    # petrol into violet, burgundy and rose
    "Prismatic Oil":          [(0.36, 0.07, 248), (0.42, 0.10, 300), (0.50, 0.13, 345), (0.64, 0.12, 22)],
}


def _oklch(L, C, h):
    a = math.radians(h)
    return _linear([L, C * math.cos(a), C * math.sin(a)])


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
    # resonator-quality: the light complements the chromatic field instead of re-tinting it -- a palette-coloured key
    # and ambient floor multiplied every hue of the gradient back into the palette's one hue (the albedo view showed
    # indigo, violet and fuchsia; the lit frame was all ultramarine). Both lean toward white by LIGHT_WHITE
    key = _mix(key, white, LIGHT_WHITE)
    sky_hue = _mix(r["primary"], white, LIGHT_WHITE)
    colors = {
        # the base field (Stage B's colour field): one sign of lobe primary, the other secondary, the nodes deep shadow
        "surfaceA": alb(r["primary"]), "surfaceB": alb(r["secondary"]), "shadow": alb(r["shadow"]),
        # the ridges' settled grit: the secondary surface, leaning resonant
        "grit": alb(_mix(r["secondary"], r["resonant"], t.get("gritMix", 0.3))),
        # the raking key: white tinted by a role (a palette's light, not a colour wash); the ambient floor under it
        # takes the primary surface's hue
        "key": light(key, "key"), "sky": light(sky_hue, "sky"),
        "keymid": [0.5 * (x + y) for x, y in zip(light(key, "key"), light(sky_hue, "sky"))],
        "fill": light(src("fill", "resonant"), "fill"),         # the counter light on the far flanks
        "hollow": light(src("hollow", "resonant"), "hollow"),   # the glow in the excited cells
        "line": light(r["spectral"], "line"),                   # the large crest's light
        "line2": light(_mix(src("line2", "secondary"), r["spectral"], 0.5), "line2"),   # the medium crest
        "sheen": light(r["peak"], "sheen"),                     # the rare peaks: glints ...
        # ... and the travelling light (the quality pass: the spectral hue, not the near-white peak -- in a loud
        # passage the dashes covered the whole ridge network and read as a white outline)
        "wave": light(src("wave", "spectral"), "wave"),
    }
    for i, (L, C_, h) in enumerate(GRADIENTS[name]):
        colors["g%d" % i] = [v * a_ * GRAD_ALBEDO for v in _oklch(L, C_, h)]
    if LIGHT_PAIR:
        # art pass II (the gradient restored, 08-plan §2.1): the colour pass's gradient was LIGHT, not albedo -- a
        # saturated key on one flank of every ridge and a complementary grazing fill on the other, over dark floors.
        # Each palette now lights its ground with a pair from its own gradient: the counter light and the hollow glow
        # take the gradient's far end (g3's hue, at full chroma), the key stays the palette's spectral hue
        _, c3, h3 = GRADIENTS[name][3]
        warm = _hue(_oklch(0.72, max(c3, 0.012) * LIGHT_PAIR_CHROMA, h3))
        colors["fill"] = [v * t.get("fill", 1.0) for v in warm]
        colors["hollow"] = [v * t.get("hollow", 1.0) for v in warm]
    # the environment (the overhaul, stage B): the sky's zenith deep in the palette's background hue, its horizon a
    # lifted, saturated mix of shadow and primary (so the distance glows with the palette, never grey), the fog the
    # horizon's colour a step darker, and the sky's glow the palette's resonant hue
    _, cg, hg = kit.lch_of(_mix(r["shadow"], r["primary"], 0.6))
    colors["skyZenith"] = [v * 0.5 for v in r["background"]]
    colors["skyHorizon"] = kit.oklch(0.36, min(cg * 1.6, 0.14), hg)
    colors["skyGround"] = kit.oklch(0.22, min(cg * 1.3, 0.1), hg)
    colors["fogC"] = kit.oklch(0.38, min(cg * 2.0, 0.15), hg)
    colors["sunC"] = _hue(r["resonant"])
    return {"name": name, "colors": {k: [round(v, 5) for v in c] for k, c in colors.items()}}


PALETTE = os.environ.get("CW_PALETTE", "M")   # M: the ten palettes (12-brief); A-C the colour pass's, G grey
PAL_INDEX = float(os.environ.get("CW_PAL", "0"))               # the palette shown (palette/position)
PAL_ALBEDO = float(os.environ.get("CW_PAL_ALBEDO", "0.28"))     # albedo = the brief's display colour x this
GRAD_ALBEDO = float(os.environ.get("CW_GRAD_ALBEDO", "1.0"))    # the chromatic field's stops, relative to it
LIGHT_WHITE = float(os.environ.get("CW_LIGHT_WHITE", "0.45"))    # how far the key and ambient floor lean to white
LIGHT_PAIR = os.environ.get("CW_LIGHT_PAIR", "0") == "1"           # art pass II: fill + glow from the gradient's end
LIGHT_PAIR_CHROMA = float(os.environ.get("CW_LIGHT_PAIR_CHROMA", "1.6"))
PAL_TRANSITION = float(os.environ.get("CW_PAL_TRANSITION", "6.0"))  # seconds of OKLab blend
PAL_HOLD = float(os.environ.get("CW_PAL_HOLD", "24.0"))         # the least time a palette is held
PAL_LOCK = os.environ.get("CW_PAL_LOCK", "0") == "1"            # a review still: hold the selected palette
# the journey: neighbours never complementary (a straight OKLab line between opposite hues passes through grey -- the
# first order's green -> rose read as silver mid-blend in validation), a temperature arc around the wheel instead:
# abyssal blue -> chlorophyll -> glacial -> silver -> oxidized -> molten -> crimson -> rose -> oil -> ultraviolet -> blue
PAL_ORDER = [0, 6, 5, 8, 4, 2, 1, 7, 9, 3]
# the primary advance: phrase boundaries (CW_PAL_PRIMARY=section.change stands in for "no tempo" in a review render:
# a replayable signal that never fires without authored sections)
PAL_PRIMARY = os.environ.get("CW_PAL_PRIMARY", "beat.phrasePulse")
# ADR-1421, the fallback when no phrase arrives (live with no tempo): a large, sustained change in the music. The sum
# of |fast - slow| (3 s vs 24 s averages) over three Sonic descriptors -- energy alone is nearly flat on mastered
# tracks (0.71-0.78 for most of Feline), so brightness and density carry the builds, drops and breakdowns -- fires at
# 0.08 and re-arms below 0.04; heard once the phrases have been quiet 30 s; and at the latest every 75 s, so a long
# uniform groove still evolves. sonic.* is in the seek replay's prefix, so a render still lands where a play does.
# Chosen from Feline, Trench and Hyperborea traces (11-report, the fallback).
PAL_CHANGE = {"signals": ["sonic.energy", "sonic.brightness", "sonic.density"], "fast": 3.0, "slow": 24.0,
              "threshold": 0.08, "rearm": 0.04, "primaryTimeout": 30.0, "maxHold": 75.0}
# the chromatic field's layers (resonator-quality): B's band (cycles across the bake, its weight), D's ridge shift,
# and how much colour the hollow floors keep (Stage C's 0.1 crushed them to black)
BAND_N = float(os.environ.get("CW_BAND_N", "1.3"))
BAND_W = float(os.environ.get("CW_BAND_W", "0.45"))
RIDGE_SHIFT = float(os.environ.get("CW_RIDGE_SHIFT", "0.18"))
# the crest line's width: sand^13 drew a line ~0.25 m wide, thinner than a mesh cell, which zigzagged along every
# steep lip (resonator-quality Phase C); sand^7 is ~0.35 m and softer-edged
CREST_POWER = float(os.environ.get("CW_CREST_POWER", "7"))
# where the hollow glow starts and saturates in the excitation (the overhaul's four families ring harder: 0.3..1.0 lit
# every hollow)
HOLLOW_RAMP = [float(v) for v in os.environ.get("CW_HOLLOW", "1.4,3.2").split(",")]
TINT_GAIN = float(os.environ.get("CW_TINT_GAIN", "1.6"))   # F into the ramp about its middle: |F| ~ 0.55 -> an end
BAND_RATE = float(os.environ.get("CW_BAND_RATE", "1.6"))   # the band's phase, metres a second per unit of the integral
FLOOR_MIN = float(os.environ.get("CW_FLOOR_MIN", "0.35"))
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
    if dbg in ("normals", "wire"):  # the quality pass's geometry views: the deformed normal as colour; the mesh's square
        # grid (the 0.36 m core only: beyond +-36 m the cells grow) drawn over a grey surface lit by the key alone
        g.op(None, "input", 6, inp="normal")
        if dbg == "normals":
            g.op(None, "remap", 4, a=6, k=[-1.0, 1.0, 0.0, 1.0], f=1.0)
        else:
            g.op(None, "input", 0, inp="worldPosition")
            g.op(None, "quantize", 1, a=0, f=1.0 / FINE_STEP)                 # the cell's centre
            g.op(None, "remap", 0, a=0, k=[0.0, 1.0, 0.0, -1.0])              # -p
            g.op(None, "add", 1, a=1, b=0)                                    # centre - p, +-step/2
            g.op(None, "multiply", 1, a=1, b=1)
            g.op(None, "smoothstep", 1, a=1, k=[(0.36 * FINE_STEP) ** 2, (0.46 * FINE_STEP) ** 2])
            g.op(None, "swizzle", 3, a=1, k=[2, 2, 2, 2])
            g.op(None, "add", 1, a=1, b=3)                                    # x or z edge
            g.op(None, "gradient", 5, a=6, k=_unit([-x for x in KEY_DIR]) + [0.0], f=0.7)
            g.op(None, "remap", 5, a=5, k=[0.0, 1.0, 0.25, 0.95], f=1.0)
            g.op(None, "ramp", 4, a=1, k=[0.5, 0.5, 0.52, 1.0], k2=[0.9, 0.2, 0.1, 1.0], k3=[1.0, 0.1, 0.05, 1.0])
            g.op(None, "multiply", 4, a=4, b=5)
        g.op(None, "constant", 2, k=[0, 0, 0, 1])
        g.base, g.emit = 4, 2
        return g
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
        g.op(None, "power", 2, a=0, f=CREST_POWER)
        # the colour field (Stage B): the large figure's signed F
        g.op("tintField", "field", 5, field="shade-tint-grid")
        if AMP_FROM_TINT:   # art pass II: the excitation is the large figure's |F|^2 (the colour field squared)
            g.op("ampFromTint", "multiply", 3, a=5, b=5)
            g.op(None, "remap", 3, a=3, k=[0.0, 1.0, 0.0, AMP_GAIN])
        else:
            g.op(None, "field", 3, field="shade-amp-grid")
        if PALETTE != "M":   # (the chromatic field reads F itself: its baked strength is the gain, its ramp is cyclic)
            g.op("tintMap", "remap", 5, a=5, k=[-0.55, 0.55, 0.0, 1.0], f=1.0)   # r5: the colour coordinate, 0 .. 1
    else:
        g.op("shade", "field", 0, field="shade")                   # r0 = (sand, crest, amplitude, w)
        g.op(None, "swizzle", 2, a=0, k=[1, 1, 1, 1])               # r2 = crest
        g.op(None, "swizzle", 3, a=0, k=[2, 2, 2, 2])               # r3 = amplitude
    # (a scalar op reads its source's x: r0.x is the sand, so no swizzle)
    if not (BAKE and PALETTE == "M"):   # (the chromatic field has no grit: the stone mask below reads the sand itself)
        g.op("gritMask", "smoothstep", 1, a=0, k=[0.5, 0.92] if BAKE else [0.12, 0.5])          # r1: where grit has settled (the ridges)
    # ---- emission, into r2: the nodal line's light where the ground rings, and a glow in the hardest-ringing hollows
    g.op("lineMask", "smoothstep", 2, a=2, k=[0.7, 0.96] if BAKE else [0.25, 0.7])
    if BAKE:
        # Stage D, the travelling light: the ripples alone (< 0 in the shared grid), on the crest lines only -- dashes
        # of light that run along the ridges from where a hit landed (the ring's carrier makes the dashes), then gone
        g.op("light2", "field", 6, field="shade-light-grid" if LINE2 else "shade-wave-grid")  # > 0 medium, < 0 wave
        g.op("waveMask", "remap", 7, a=6, k=[-0.04, -0.22, 0.0, 1.0], f=1.0)
        g.op(None, "multiply", 7, a=7, b=2)                         # ... on the crest
        if SCAN > 0.0:
            # the scan (layer C): a band crossing the basin, lighting the whole ridge it passes (the sand mask, wider
            # than the crest) in the travelling light's colour, the crest hottest
            g.op("scan", "field", 6, field="scan")
            g.op(None, "multiply", 6, a=6, b=0)
            g.op("scanGain", "remap", 6, a=6, k=[0.0, 1.0, 0.0, SCAN], f=0.0)
            g.op(None, "add", 7, a=7, b=6)
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
    if BAKE and PALETTE == "M":
        g.op("stoneMask", "remap", 7, a=0, k=[0.5, 0.92, 1.0, 0.0], f=1.0)   # r7: scoured stone (off the ridges)
    else:
        g.op("stoneMask", "remap", 7, a=1, k=[0.0, 1.0, 1.0, 0.0], f=1.0)   # r7: scoured stone (no grit)
    g.op("hollowRamp", "smoothstep", 3, a=3, k=HOLLOW_RAMP if BAKE else [0.8, 2.4])          # only where it rings hard
    if not BAKE:  # (baked: the wall band below is all stone already -- sand < 0.14 never carries grit)
        g.op(None, "multiply", 3, a=3, b=7)
    # ... and only on the inner walls of a cell: a band of the sand field just inside the grit, so the glow's edges
    # follow the relief (the sand's contours ARE the cell walls), never an unshaped wash over flat ground
    g.op("wallBand", "smoothstep", 4, a=0, k=[0.02, 0.14] if BAKE else [0.035, 0.08])
    g.op(None, "multiply", 3, a=3, b=4)
    if BAKE:  # the fuchsia glow belongs to the violet lobes: the colour field chooses which cells glow
        g.op("violetLobes", "remap", 4, a=5, k=[-0.05 * TINT_GAIN, -0.35 * TINT_GAIN, 0.0, 1.0] if PALETTE == "M"
             else [0.45, 0.15, 0.0, 1.0], f=1.0)
        g.op(None, "multiply", 3, a=3, b=4)
    tint("hollow", 3, 3, "hollow", P_["hollowI"])
    g.op(None, "add", 2, a=2, b=3)                                 # r2: emission
    # ---- materials: scoured satin stone, matte settled grit (one ramp: stone -> grit by the grit mask) ----
    st, gr = pc("stone"), pc("grit")
    if BAKE:
        # THE COLOUR FIELD (10-brief Stage B): the stone takes its colour from the large figure's signed pattern F --
        # deep violet on one sign of lobe, the navy stone at the nodes, deep blue on the other -- so broad regions of
        # colour fill the lobes and change as the figure changes; quiet music (small F) settles them toward navy
        if PALETTE == "M":
            # THE CHROMATIC FIELD (resonator-quality, the addendum): the surface carries a continuous four-colour
            # gradient between the active palette's gradient roles (g0..g3, OKLab stops, ADR-1470), on a coordinate
            # built from three layers --
            #   A  the large figure's signed F (r5, baked at TINT_GAIN) about the gradient's middle (the band's
            #      constant 0.5): one sign of lobe runs toward g0, the other toward g3, the nodes sit between g1 and
            #      g2 -- broad regions that follow the lobes and re-form with the figure
            #   B  a broad travelling band across the basin (a cosine of the position along x, its phase a persistent
            #      integral of the music, never reset): it crosses the cells, so colour boundaries do not all sit on
            #      the geometry, and the regions stretch, bend and migrate
            #   D  the nodal ridges (sand) push the colour a step BACK along the gradient (the overhaul, stage C: nodal
            #      boundaries darker and differently hued, never a white or cyan outline, brief §5.7)
            # (Eight ops at the interpreter's ~0.8 ms an op at High: they replaced the tint remap, the grit's three and
            # the view layer's three, so the program is two ops SHORTER than before the pass; J's oil and E's metal
            # now live in their own gradients)
            g.op(None, "input", 6, inp="worldPosition")
            g.op("band", "palette", 4, a=6, k=[0.5, 0.5, 0.5, 0], k2=[BAND_W] * 3 + [0], k3=[BAND_N / (2.0 * BAKE_HALF)] * 3
                 + [0], k4=[0, 0, 0, 0], f=0.0)
            g.op(None, "add", 5, a=5, b=4)
            g.op("ridgeShift", "remap", 4, a=0, k=[0.0, 1.0, 0.0, RIDGE_SHIFT])
            g.op(None, "add", 5, a=5, b=4)
            g.op("tint", "oklabRamp", 4, a=5, k=[1, 1, 1, 1], k2=[1, 1, 1, 1], k3=[1, 1, 1, 1], k4=[1, 1, 1, 1])
        else:
            g.op("tint", "ramp", 4, a=5, k=pc("violet") + [1.0], k2=st + [1.0], k3=pc("blue") + [1.0])
            g.op("grit", "constant", 6, k=gr + [1.0])
            g.op("albedo", "mixBy", 4, a=4, b=6, c=1)                  # r4: the coloured stone, the grit on the ridges
        # Stage C (coordinator, after B): the hollow FLOORS fall back toward black -- the deepest sand (the cell floor
        # inside the walls) keeps a fifth of its colour, so the lobes' violet and blue live on the walls and the frame
        # has real negative space for the light to stand out against
        g.op("floorDark", "remap", 6, a=0, k=[0.0, 0.6, FLOOR_MIN, 1.0] if PALETTE == "M" else [0.0, 0.4, 0.1, 1.0],
             f=1.0)
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
    g.op("sheenLobe", "power", 1, a=1, f=18.0 if PALETTE == "M" else 12.0)   # the key's highlight (narrower: white
    # sheen must not swallow the chromatic field) ...
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
    if dbg == "emission":  # the light pass's layer view: the crest lines, the travelling light and the glow alone
        g.op(None, "constant", 4, k=[0, 0, 0, 1])
    g.base, g.emit = 4, 2
    return g


FOG_DENSITY = float(os.environ.get("CW_FOG", "0.0035"))
SUN_GLOW = float(os.environ.get("CW_SUN", "1.5"))   # the sky's soft glow behind the basin (its colour a palette role)


def environment(s):
    s.environment = {
        "intensity": 0.05, "background": C("zenith"), "fogColor": C("fog"),
        # the overhaul's air (05-plan §1.1): a thin coloured fog (palette-bound below), so the far plate, the basin's
        # walls and the ranges dissolve into the sky's colour -- distance, not a void
        "volumeMaxDistance": 0.0, "volumeDensity": FOG_DENSITY, "fogHeight": 12.0, "fogHeightFalloff": 0.04, "skyIntensity": 1.0,
        "shadowRange": 180.0,
        # ADR-1406: the ground is unlit and painted -- screen-space AO and contact shadows add nothing to it and cost the
        # depth prepass (every deformer re-run per vertex for a second pass)
        "ambientOcclusion": False, "contactShadows": False,
        # ... and no depth prepass either (ADR-1406's switch). It won at the correction round (35.3 -> 29.8 ms at High),
        # but the light pass re-measured it on loud Feline: with the program at 43 ops the prepass costs 5.8 ms and
        # saves only 3.4 (High 34.2 ms median with it, 31.8 without); CW_PREPASS=1 asks for it again
        # (the overhaul, stage E: the oblique camera sees ridges behind ridges, so the prepass wins again -- 53 -> 43 ms
        # at High, measured; CW_PREPASS=0 turns it off)
        "depthPrepass": os.environ.get("CW_PREPASS", "1") == "1",
        "sky": {"enabled": True, "zenithColor": C("zenith"), "horizonColor": C("horizon"),
                "groundColor": C("fog"), "haze": 0.45, "sunIntensity": SUN_GLOW, "intensity": 1.0,
                "sunSize": 0.015, "sunGlow": 0.16, "sunDirection": [-0.55, 0.12, -0.83],
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
           extra={"normalStep": (NORMAL_CELLS * 2.0 * BAKE_HALF / BAKE_RES) if BAKE else 0.12},
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
        # THE MODE FAMILIES (Resonant Visual Worlds, 05-plan §1.1): the large scale is four families, each a source over
        # its own stretch of the large band, so the figure's TOPOLOGY changes when a different family leads -- not only
        # its amplitude. Their amplitudes are set by the family director (slow LFOs, routes below) and the music.
        # P, the hero: square-plate figures (ADR-1471) -- Chladni networks of crossing curves, loops and lobes; the focus
        # sweeps its (n, m) list, so neighbouring figures crossfade into compound, asymmetric ones
        {"kind": "plate", "position": [6.0, 0.0, -6.0], "plates": PLATES, "bandLow": 0.0, "bandHigh": 0.135,
         "reach": FAM_REACH, "gain": 1.0, "amplitude": FAM_BASE[0], "breath": 0.35, "drift": 8.0, "driftPeriod": 110.0,
         "spin": 0.008, "focus": 0.065, "focusWidth": 0.035, "focusSweep": 0.065, "focusPeriod": 53.0, "focusFloor": 0.0},
        # H, hexagonal cells: three plane waves (a lattice of order 3), its own orientation and turn
        {"kind": "lattice", "position": [-10.0, 0.0, 8.0], "order": 3, "orders": [3], "bandLow": 0.15, "bandHigh": 0.2,
         "reach": FAM_REACH, "gain": 1.0, "amplitude": FAM_BASE[1], "breath": 0.4, "drift": 6.0, "driftPeriod": 90.0,
         "spin": -0.01},
        # R, radial: the earlier figure (rings, stars, lobes about an off-centre point) -- now one family of four
        {"kind": "point", "position": [7.0, 3.0, -7.0], "order": 6, "bandLow": 0.2, "bandHigh": 0.28, "reach": FAM_REACH,
         "gain": 1.0, "amplitude": FAM_BASE[2], "breath": 0.4, "drift": 12.0, "driftPeriod": 100.0, "spin": 0.03,
         "orders": [3, 4, 2, 6]},
        # A, arcs: a low-order point source far off the frame, so its rings cross the basin as broad sweeping arcs;
        # it also carries the sound's roughness (interference as an event, a route)
        {"kind": "point", "position": [-150.0, 0.0, 70.0], "order": 2, "bandLow": 0.28, "bandHigh": 0.32, "reach": 0.0,
         "gain": 1.0, "amplitude": FAM_BASE[3], "breath": 0.5, "drift": 15.0, "driftPeriod": 80.0, "spin": -0.01,
         "orders": [1, 2]},
    ], modes=MODES, lambdaMax=LAMBDA_MAX, lambdaMin=2.6, competition=1.5, selectivity=4.0, limit=1.5, breath=0.15,
       # E, transients: every onset launches a travelling ring (kick broad and slow, snare sharp and local)
       ripple=0.5, rippleSpeed=9.0, rippleLife=1.6, rippleWidth=0.9, rippleArea=30.0, rippleCount=2.0, rippleFade=0.35, rippleU=[0.36, 0.5, 0.85],
       # ADR-1404: fixed mode places (a route on `modes` adds modes, it never re-places them), and per-band response
       layout=LAYOUT, attack=[0.18, 0.08, 0.04], release=[3.0, 1.4, 0.6])
    # the medium and fine scales' own resonator (same bank, so their wavelengths sit where they did), no ripples
    resonator(s, [
        # M, the medium scale: an IRREGULAR family at low relief (the coordinator's review of the proofs: a regular
        # medium grid read as a woven mat over the figure) -- point modes about a centre far off the other side, whose
        # arcs cross the large figure at their own angle
        {"kind": "point", "position": [130.0, 0.0, -100.0], "order": 3, "bandLow": 0.3, "bandHigh": 0.66, "reach": 0.0,
         "gain": 0.9, "amplitude": 0.5, "breath": 0.7, "drift": 10.0, "driftPeriod": 100.0, "spin": 0.02,
         "orders": [1, 2, 3, 1, 2],
         "focus": 0.48, "focusWidth": 0.04, "focusSweep": 0.16, "focusPeriod": 29.0, "focusFloor": 0.0},
        # F, the fine layer: standing bands, squares and hexagons, faint and local
        {"kind": "lattice", "position": [0.0, -6.0, 0.0], "order": 3, "bandLow": 0.66, "bandHigh": 1.0,
         "reach": 46.0, "gain": 0.55, "amplitude": 0.1, "breath": 1.5, "depth": 9.0, "drift": 2.0,
         "driftPeriod": 60.0, "spin": 0.02, "orders": [2, 1, 3]},
    ], name=FIELD_FINE, modes=MODES, lambdaMax=LAMBDA_MAX, lambdaMin=2.6, competition=1.5, selectivity=4.0,
       limit=1.5, breath=0.15, layout=LAYOUT, attack=[0.18, 0.08, 0.04], release=[3.0, 1.4, 0.6])
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
    # THE FAMILY DIRECTOR: each large family's amplitude rises and falls on its own slow LFO (incommensurate periods,
    # pure functions of the clock, so a seek lands where a play does): one or two families lead at a time and the
    # leader changes over phrases. Bass lifts the hero plate; the sound's roughness brings the arcs in (interference
    # as an event); the brightness moves the plate's focus (which (n, m) leads) and the medium's
    for i, (name, period, phase) in enumerate(FAM_LFO):
        s.sources.append({"kind": "lfo", "name": name, "settings": {"shape": "sine"}})
        s.params_({"sources/%s/rate" % name: round(1.0 / period, 6), "sources/%s/phase" % name: phase})
        s.route(R("lfo." + name, F + "source/%d/amplitude" % i, FAM_GAIN[i]))
    s.route(R("response.bass", F + "source/0/amplitude", 0.35, attackMs=150, decayMs=1500),
            R("sonic.brightness.slow", F + "source/0/focus", 0.06, attackMs=2000, decayMs=4000),
            R("sonic.brightness", "field/%s/resonance/source/0/focus" % FIELD_FINE, 0.14, attackMs=400,
              decayMs=1200),
            R("sonic.roughness.slow", F + "source/3/amplitude", 0.45, attackMs=1500, decayMs=3000),
            R("response.bass", D(1), 1.0, op="multiply", gain=0.35, offset=1.0, attackMs=80, decayMs=900),
            R("response.sustain", D(2), 1.0, op="multiply", gain=0.3, offset=1.0, attackMs=1200, decayMs=3000),
            R("response.hat", D(3), 1.0, op="multiply", gain=1.5, offset=1.0, attackMs=10, decayMs=220))
    s.control = control_map(cc, pads)
    s.route(R("control.relief", "procedural/ground/deform/1/amount", 3.0, **CCR))
    if PALETTE == "M" and BAKE:
        s.route(R("control.palette", "palette/select", 9.0, attackMs=0, decayMs=0),
                R("control.paletteLock", "palette/lock", 1.0, attackMs=0, decayMs=0))
    s.params_(POST)
    if DEBUG in ("signed", "nodal", "envelope", "normals", "wire"):  # a debug material: none of the colour routes' targets exist
        return s, cc, pads
    # a ramp-tinted term's colour is its constant2 (half) and constant3 (whole); a route scales both
    RP = "material/cwGround/op/%d/ramp/%s"
    def ramp_paths(label):
        return [RP % (g[label], "constant2"), RP % (g[label], "constant3")]
    counter, hollow, sheen = ramp_paths("fill"), ramp_paths("hollow"), ramp_paths("sheen")
    line = "material/cwGround/op/%d/constant/constant" % g["line"]
    if BAKE and "grit" not in g.at:  # the chromatic field (palette M): the ridges take the gradient's next hue
        grit, grit_mid = None, None
    elif BAKE:  # the grit is a constant of its own beside the colour field (light pass): the build turns it, whole
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
        band = "material/cwGround/op/%d/palette/value" % g["band"]
        s.route(
            # bass: the extent of the broad colour regions -- the colour field's gain rises, so a lobe's colour
            # reaches further from its centre toward the nodes (these routes named the field op, not its remap,
            # before the quality pass: they never bound, and the log said so)
            R("response.bass", "field/shade-tint/strength", 1.0, op="multiply", gain=0.8, offset=1.0, attackMs=120,
              decayMs=1200),
            # held sound (the low-mid body of a mix): the dominant surface shifts -- the band's phase leans over
            # seconds, so the whole field slides a step along the gradient and settles back
            R("response.sustain", band, 20.0, attackMs=2500, decayMs=5000),
            # mids (snares, the body of the beat): the medium folds' light (with LINE2; otherwise the snare's route
            # onto the crest lines, above, carries the mids)
            *[R("snareEnv", p_, 1.0, op="multiply", gain=1.4, offset=1.0, attackMs=0, decayMs=420)
              for p_ in ([RP % (g["line2"], "constant2"), RP % (g["line2"], "constant3")] if LINE2 else [])])
        # the chromatic field's band (layer B) migrates across the basin: its phase is the integral of the music's
        # energy plus a slow floor (ADR-1041), so it never resets, quickens in loud passages and still drifts in
        # silence -- about one band in 60-90 s
        s.route(R("sonic.energy", band, BAND_RATE, gain=1.0, offset=0.4, integrate=True))
        s.route(*[R("sustain", p_, 1.0, op="multiply", gain=1.2, offset=1.0, attackMs=1500, decayMs=4000) for p_ in hollow],
                *[R("kickEnv", p_, 1.0, op="multiply", gain=1.6, offset=1.0, attackMs=0, decayMs=380) for p_ in hollow],
                R("snareEnv", line, 1.0, op="multiply", gain=SNARE_LINE, offset=1.0, attackMs=0, decayMs=260),
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
    binds += [("line", M % (g["line"], "constant", "constant")),
              ("g0", M % (g["tint"], "oklabRamp", "constant")), ("g1", M % (g["tint"], "oklabRamp", "constant2")),
              ("g2", M % (g["tint"], "oklabRamp", "constant3")), ("g3", M % (g["tint"], "oklabRamp", "constant4")),
              ("sky", M % (g["key"], "ramp", "constant")), ("keymid", M % (g["key"], "ramp", "constant2")),
              ("key", M % (g["key"], "ramp", "constant3"))]
    # ADR-1420: the selector -- the palette changes at phrase scale, on a phrase boundary at least PAL_HOLD seconds
    # after the last change, to the next palette of a COMPOSED order (PAL_ORDER: a temperature arc, never across),
    # blended straight in OKLab over PAL_TRANSITION seconds; CC 9 selects, CC 10 locks
    return {"states": [palette_state(n, h) for n, h in MULTI_PALETTES],
            "bindings": [{"role": r, "target": t, "mode": "multiply"} for r, t in binds] + [
                {"role": r, "target": t, "mode": "replace"} for r, t in (
                    ("skyZenith", "env/sky/zenithColor"), ("skyHorizon", "env/sky/horizonColor"),
                    ("skyGround", "env/sky/groundColor"), ("fogC", "scene/fogColor"), ("sunC", "env/sky/sunColor"))],
            "position": PAL_INDEX, "saturation": 1.0, "value": 1.0,
            "selector": {"select": int(PAL_INDEX), "transition": PAL_TRANSITION, "lock": PAL_LOCK, "hold": PAL_HOLD,
                         "advance": {"signal": PAL_PRIMARY, "threshold": 0.5, "order": PAL_ORDER,
                                     "change": PAL_CHANGE}}}



BUILDERS = {"world": build_world}
OUT_DIRS = {k: (OUT if os.environ.get("CW_OUT") else os.path.join(REPO, "examples", v["id"])) for k, v in SCENES.items()}
DESCRIPTION_TAIL = {"world": "Ten artist palettes take turns at phrase boundaries (or on a big change in the music when there is no tempo), blended in OKLab; CC 9 picks one, CC 10 holds it. The ground's figure is drawn by one resonance field (ADR-1400, ADR-1403 to 1409); the scene carries no track of its own."}


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
    if os.environ.get("CW_OUT"):   # a look-development copy: no index entry
        return pp
    kit.upsert_index_entry({
        "category": "Resonator",
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

#!/usr/bin/env python3
"""CYMATIC WORLD, CYMATIC CRYSTALS, CYMATIC CREATURE: the generator (docs/prototypes/cymatic-world/).

    python3 tools/cymatic_world/build.py                    # all three scenes, their meshes and index entries
    python3 tools/cymatic_world/build.py --scene crystals   # one (world | crystals | creature)
    python3 tools/cymatic_world/build.py --scene world --variant <wav> --out <project.json> [--camera ...] [--set ...]

Three live scenes, the brief's three prototypes, each its own composition on the same subsystem: a resonance field
(FieldKind::Resonance, ADR-1400) excited by the audio history.
  Cymatic World     examples/cymatic-world/     the resonant ground (owns the shared ground and spire meshes)
  Cymatic Crystals  examples/cymatic-crystals/  the tuned crystal druse on a resonating floor
  Cymatic Creature  examples/cymatic-creature/  the medusa of grains on nodal shells, with its veil

The ground's geography comes from the engine's WorldMap (the terrain system's noise layers and ridge/valley/flat
features), probed through `avgen_world_preview --heights` at every vertex of one screen-density mesh, because the
resonance has to move it on the GPU and a terrain node's chunks take no deformers. One ground mesh serves all three.
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
MESH_DIR = os.path.join(OUT, "meshes")   # the shared ground and spires

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
    "crystal": "#2c3040",    # smoky dark quartz-steel under a thin film (the film makes the colour)
    "crystalGlow": "#6fdcff",# a ringing crystal's inner light
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


# ================================================================================================ the crystals
def crystal_tris():
    """One crystal, centred on the origin, 1 m long on +Y, radius 1: a three-stage stepped hexagonal prism (each stage
    a little narrower, with a sloped ledge between) and a six-face pyramidal termination."""
    def ring(y, r, twist=0.0):
        return [(r * math.cos(twist + k * math.pi / 3.0), y, r * math.sin(twist + k * math.pi / 3.0)) for k in range(6)]
    rings = [ring(-0.5, 1.0), ring(-0.12, 1.0), ring(-0.08, 0.86), ring(0.18, 0.86), ring(0.22, 0.72), ring(0.36, 0.72)]
    tip = (0.0, 0.5, 0.0)
    tris = []
    for a, b in zip(rings[:-1], rings[1:]):
        for k in range(6):
            p0, p1, q0, q1 = a[k], a[(k + 1) % 6], b[k], b[(k + 1) % 6]
            tris += [(p0, q1, p1), (p0, q0, q1)]
    top = rings[-1]
    for k in range(6):
        tris.append((top[k], tip, top[(k + 1) % 6]))
    base = rings[0]
    for k in range(1, 5):
        tris.append((base[0], base[k + 1], base[k]))
    return tris




def druse_layout():
    """[(dir (unit), length, radius)]: a crown of crystals about a slight lean. Deterministic."""
    rnd = np.random.default_rng(1407)
    away = np.array([-0.6, 0.0, 0.8])   # the crown leans a little off vertical (a druse is never plumb)
    lean = np.array([away[0] * 0.35, 1.0, away[2] * 0.35])
    lean /= np.linalg.norm(lean)
    side = np.cross(lean, [0.0, 0.0, 1.0])
    side /= np.linalg.norm(side)
    up2 = np.cross(side, lean)
    out = []
    n = 34
    for k in range(n):
        # golden-angle spiral over a cap: the core near the lean axis, the fringe spread to ~70 degrees
        t = (k + 0.5) / n
        polar = math.radians(8.0 + 64.0 * t ** 0.8) * (0.85 + 0.3 * rnd.random())
        az = k * 2.39996 + 0.4 * rnd.random()
        d = math.cos(polar) * lean + math.sin(polar) * (math.cos(az) * side + math.sin(az) * up2)
        d /= np.linalg.norm(d)
        length = (15.0 * (1.0 - t) ** 1.3 + 2.2) * (0.8 + 0.4 * rnd.random())
        radius = 0.32 + 0.10 * length ** 0.75
        out.append((d, length, radius))
    return out


def _quat_from_y(d):
    """The rotation taking +Y to unit d, as (x, y, z, w)."""
    y = np.array([0.0, 1.0, 0.0])
    c = float(np.dot(y, d))
    if c > 0.99999:
        return (0.0, 0.0, 0.0, 1.0)
    axis = np.cross(y, d)
    axis /= np.linalg.norm(axis)
    a = math.acos(max(-1.0, min(1.0, c)))
    sa = math.sin(a / 2.0)
    return (axis[0] * sa, axis[1] * sa, axis[2] * sa, math.cos(a / 2.0))


def druse_points(gy):
    root = np.array([CRYSTAL_ROOT[0], gy - 1.2, CRYSTAL_ROOT[1]])
    pts = []
    for d, length, radius in druse_layout():
        c = root + d * (length * 0.5)
        q = _quat_from_y(d)
        pts.append([round(float(c[0]), 4), round(float(c[1]), 4), round(float(c[2]), 4),
                    round(q[0], 6), round(q[1], 6), round(q[2], 6), round(q[3], 6),
                    round(radius, 4), round(length, 4), round(radius, 4)])
    return pts, root


def crystal_program():
    """The crystal's light from inside: growth bands along its length (the stepped stages read as layers of a
    phantom crystal), lit by the instance's own emission -- which is zero until the field rings that crystal."""
    c = Prog("cwCrystal")
    c.op(None, "input", 0, inp="localPosition")
    c.op(None, "swizzle", 0, a=0, k=[1, 1, 1, 1])                   # local y, -0.5..0.5
    c.op("bands", "palette", 1, a=0, k=[0.5, 0.5, 0.5, 0], k2=[0.5, 0.5, 0.5, 0], k3=[1.5, 1.5, 1.5, 0],
         k4=[0.1, 0.1, 0.1, 0])                                     # 0.5 + 0.5 cos(2 pi (5 y + 0.1)): bands
    c.op(None, "power", 1, a=1, f=40.0)                              # thin bright layers
    c.op(None, "remap", 0, a=0, k=[-0.5, 0.5, 0.25, 1.0], f=1.0)    # brighter toward the tip
    c.op(None, "multiply", 1, a=1, b=0)
    c.op("core", "constant", 2, k=[0.03, 0.03, 0.03, 1.0])          # a faint core glow under the bands
    c.op(None, "add", 1, a=1, b=2)
    c.op(None, "input", 3, inp="materialEmission")
    c.op(None, "multiply", 3, a=3, b=1)
    c.emit = 3
    return c


# ================================================================================================ the spires
def spire_tris(base, radius, height, sides, lean, seed):
    """An eroded basalt pinnacle: five drums, each narrower and a little offset (a weathered column), drawn to a
    blunt point; faceted, so the moon picks out its planes."""
    rnd = np.random.default_rng(seed + 77)
    a0 = rnd.random() * 2.0 * math.pi
    levels = []
    n = 5
    for k in range(n + 1):
        u = k / n
        r = radius * (1.0 - 0.82 * u ** 1.25) * (0.92 + 0.16 * rnd.random())
        y = base[1] + height * u
        off = (lean[0] * height * u + (rnd.random() - 0.5) * 0.15 * radius,
               lean[1] * height * u + (rnd.random() - 0.5) * 0.15 * radius)
        levels.append([(base[0] + off[0] + r * math.cos(a0 + 2 * math.pi * j / sides), y,
                        base[2] + off[1] + r * math.sin(a0 + 2 * math.pi * j / sides)) for j in range(sides)])
    tris = []
    for lo, hi in zip(levels[:-1], levels[1:]):
        for j in range(sides):
            j1 = (j + 1) % sides
            tris += [(lo[j], hi[j], hi[j1]), (lo[j], hi[j1], lo[j1])]
    top = levels[-1]
    cx = sum(p[0] for p in top) / sides
    cz = sum(p[2] for p in top) / sides
    apex = (cx, top[0][1] + radius * 0.5, cz)
    for j in range(sides):
        tris.append((top[j], apex, top[(j + 1) % sides]))
    return tris


# ================================================================================================ the world beyond
# Basalt spires: columns of the same dark stone standing on the rim and in ranks across the plateau, so every
# direction has layered silhouettes against the violet horizon (no edge, no void) -- atmosphere, never a hero.
def spires(s):
    rnd = np.random.default_rng(20261016)
    places = []
    for ring_r, n, hmin, hmax, rmin, rmax in ((112.0, 26, 9.0, 26.0, 2.0, 4.5), (190.0, 34, 14.0, 42.0, 3.0, 7.0),
                                              (330.0, 40, 25.0, 70.0, 5.0, 11.0), (620.0, 36, 50.0, 130.0, 9.0, 20.0)):
        for k in range(n):
            a = (k + rnd.random() * 0.7) * 2.0 * math.pi / n
            r = ring_r * (0.85 + 0.3 * rnd.random())
            x, z = r * math.cos(a), r * math.sin(a)
            # keep the camera's gap (toward +z, |x| small near the rim) open
            if ring_r < 150.0 and z > 40.0 and abs(x) < 45.0:
                continue
            places.append((x, z, hmin + (hmax - hmin) * rnd.random() ** 1.6, rmin + (rmax - rmin) * rnd.random(),
                           (rnd.random() - 0.5) * 0.12, (rnd.random() - 0.5) * 0.12, int(rnd.integers(5, 8))))
    ys = world_heights(np.array([[p[0], p[1]] for p in places], dtype=np.float64))
    tris = []
    for k, ((x, z, h, r, lx, lz, sides), y) in enumerate(zip(places, ys)):
        tris += spire_tris((x, y - 3.0, z), r, h + 3.0, sides, (lx, lz), k)
    pos, nrm = meshes.faceted(tris)
    meshes.write_glb(os.path.join(MESH_DIR, "cw-spires.glb"), pos, nrm, None, "spires")
    s.proc("spires", {"kind": "mesh", "asset": "meshes/cw-spires.glb"},
           material={"baseColor": C("stone", 1.6), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.7, "metallic": 0.0})


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
# Three live scenes, three prototypes of one idea, each its own composition on the SAME subsystem: one resonance field
# (FieldKind::Resonance, ADR-1400) per scene, excited by the audio history, read by that scene's forms.
#   world     the resonant ground alone (Prototype A)
#   crystals  the tuned crystal druse on a resonating floor (Prototype B)
#   creature  the medusa of grains on nodal shells, with its veil (Prototype C)
SCENES = {
    "world": {"id": "cymatic-world", "title": "Cymatic World"},
    "crystals": {"id": "cymatic-crystals", "title": "Cymatic Crystals"},
    "creature": {"id": "cymatic-creature", "title": "Cymatic Creature"},
}
FIELD = "cymatic"
HEART = (0.0, 12.0, 0.0)          # the creature floats here; the world's low modes are centred on it too
SECOND = (-23.0, 0.0, 9.0)        # the world's second centre (interference with the first)
MOON_DIR = (0.93, -0.2, -0.3)     # the direction the moonlight travels (normalised in use)
LIGHT_RANGE = 200.0               # metres: a painted lamp's normalising distance
GROUND_REL = "../cymatic-world/meshes/cw-ground.glb"   # one ground mesh, shared by all three scenes
SPIRES_REL = "../cymatic-world/meshes/cw-spires.glb"


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


def ground_readers(s):
    reader(s, "shade", "shading", {"sharpness": 0.9, "reliefLineSharpness": 0.35})
    reader(s, "relief", "relief", {"sharpness": 0.9, "reliefLine": 0.08, "reliefLineSharpness": 0.6,
                                   "reliefVibration": 0.23})


def ground_program(lamp=None, counter=0.0, line=0.35, moon=6.0, sky=0.08):
    """The basin's ground, UNLIT with its light painted in the program (the lit path cost ~7 ms at 1080p for a
    surface whose light is three terms). One `field` op reads the resonance's shading channel (sand, crest,
    amplitude) in a single pass over the modes. Basalt where the field has scoured it, settled grit on the nodal
    ridges; a raking warm moon (Lambert on the deformed normal: the relief draws itself); optionally a cool counter
    light from the other side and a lamp (inverse square from `lamp` = (position, intensity)); a dim sky; and a thin
    cold light only on the nodal line where the ground rings."""
    g = Prog("cwGround")
    g.op("shade", "field", 0, field="shade")                       # r0 = (sand, crest, amplitude, w)
    g.op(None, "swizzle", 1, a=0, k=[0, 0, 0, 0])                   # r1 = sand
    g.op(None, "swizzle", 2, a=0, k=[1, 1, 1, 1])                   # r2 = crest
    g.op(None, "swizzle", 3, a=0, k=[2, 2, 2, 2])                   # r3 = amplitude
    g.op("gritMask", "smoothstep", 1, a=1, k=[0.06, 0.4])
    g.op("lineMask", "smoothstep", 2, a=2, k=[0.25, 0.7])
    g.op(None, "multiply", 2, a=2, b=3)                            # crest x ringing
    g.op("lineCol", "constant", 3, k=C("sandHot", line) + [1.0])
    g.op(None, "multiply", 2, a=2, b=3)                            # r2: emission (an accent)
    g.op("stone", "constant", 3, k=C("stone") + [1.0])
    g.op("grit", "constant", 4, k=C("sand") + [1.0])
    g.op(None, "mixBy", 1, a=3, b=4, c=1)                          # r1: albedo
    L = _unit([-x for x in MOON_DIR])
    g.op(None, "input", 3, inp="normal")
    g.op("moonDot", "gradient", 3, a=3, k=L + [0.0], f=1.0)
    g.op("moon", "constant", 4, k=C("moon", moon) + [1.0])
    g.op(None, "multiply", 3, a=3, b=4)                            # r3: moonlight
    if counter > 0.0:
        # a cool counter light low from the opposite quarter: the ridges' other flanks, so the relief reads both ways
        Lc = _unit([-MOON_DIR[0], 0.28, -MOON_DIR[2] * 0.4 + 0.6])
        g.op(None, "input", 4, inp="normal")
        g.op("counterDot", "gradient", 4, a=4, k=Lc + [0.0], f=1.0)
        g.op(None, "power", 4, a=4, f=2.0)                          # a tighter, grazing lobe
        g.op("counter", "constant", 5, k=C("counter", counter) + [1.0])
        g.op(None, "multiply", 4, a=4, b=5)
        g.op(None, "add", 3, a=3, b=4)
    if lamp is not None:
        (lx, ly, lz), intensity = lamp
        # (N . v) / |v|^3 from the lamp, through two saturating dots normalised by LIGHT_RANGE
        g.op(None, "input", 4, inp="worldPosition")
        g.op(None, "constant", 5, k=[-1.0, -1.0, -1.0, 0.0])
        g.op(None, "multiply", 4, a=4, b=5)
        g.op(None, "constant", 5, k=[lx, ly, lz, 0.0])
        g.op(None, "add", 4, a=4, b=5)                             # r4 = v, toward the lamp
        g.op(None, "multiply", 5, a=4, b=4)
        g.op(None, "gradient", 5, a=5, k=[1.0, 1.0, 1.0, 2e-4], f=1.0 / (LIGHT_RANGE * LIGHT_RANGE))
        g.op(None, "input", 6, inp="normal")
        g.op(None, "multiply", 6, a=6, b=4)
        g.op(None, "gradient", 6, a=6, k=[1.0, 1.0, 1.0, 0.0], f=1.0 / LIGHT_RANGE)
        g.op(None, "power", 5, a=5, f=-1.5)
        g.op(None, "multiply", 6, a=6, b=5)
        g.op("lamp", "constant", 5, k=C("sandHot", intensity / (LIGHT_RANGE * LIGHT_RANGE)) + [1.0])
        g.op(None, "multiply", 6, a=6, b=5)                        # r6: lamplight
        g.op(None, "add", 3, a=3, b=6)
    g.op("sky", "constant", 4, k=C("horizon", sky) + [1.0])
    g.op(None, "add", 3, a=3, b=4)                                 # r3: all the light
    g.op(None, "multiply", 1, a=1, b=3)                            # r1: lit colour (unlit output)
    g.base, g.emit = 1, 2
    return g


def environment(s, rim=1.2):
    s.environment = {
        "intensity": 0.05, "background": C("zenith"), "fogColor": C("fog"),
        "volumeDensity": 0.0, "fogHeight": 12.0, "fogHeightFalloff": 0.04, "skyIntensity": 1.0,
        "shadowRange": 180.0,
        "sky": {"enabled": True, "zenithColor": C("zenith"), "horizonColor": C("horizon"),
                "groundColor": C("fog"), "haze": 0.45, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    # the moon: low and to the side, so it rakes the ridges and every hollow holds a shadow (it lights what is not
    # painted: the spires, the crystals)
    s.light("moon", "directional", direction=list(MOON_DIR), color=C("moon"), intensity=6.0,
            castsShadow=False, contactShadow=False, softness=0.4)
    s.light("rim", "directional", direction=[-0.25, -0.12, 0.96], color=C("rim"), intensity=rim, castsShadow=False)


def ground(s, relief, lamp=None, counter=0.0, line=0.35, write_mesh=False, moon=6.0, sky=0.08):
    if write_mesh:
        pos, nrm, idx = ground_mesh()
        write_glb(os.path.join(MESH_DIR, "cw-ground.glb"), pos, nrm, idx, "ground")
        print("ground: %d vertices, %d triangles, %.1f MB" % (len(pos), len(idx) // 3,
                                                              os.path.getsize(os.path.join(MESH_DIR, "cw-ground.glb")) / 1e6))
    g = ground_program(lamp, counter, line, moon, sky)
    s.program(g.doc(base=g.base, emission=g.emit))
    s.proc("ground", {"kind": "mesh", "asset": GROUND_REL},
           material={"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                     "roughness": 0.6, "metallic": 0.0, "unlit": True,
                     "program": "" if os.environ.get("CW_NOPROG") else g.name},
           extra={"normalStep": 0.12},  # ADR-1401: the mesh is 5 km wide; its auto step would be 3.7 m
           deformers=[
               # ONE pass over the modes (the relief channel): the sand's ridges along the nodal lines, a crisp crest
               # on the line itself, and the vibration that makes the hollows breathe
               {"kind": "field", "field": "relief", "amount": relief, "axis": [0.0, 1.0, 0.0], "space": "world",
                "alongNormal": False},
           ])
    return g


def spire_node(s):
    s.proc("spires", {"kind": "mesh", "asset": SPIRES_REL},
           material={"baseColor": C("stone", 1.6), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                     "roughness": 0.7, "metallic": 0.0})


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
    "post/bloom/intensity": 0.18, "post/bloom/threshold": 1.0, "post/output/vignette": 0.45,
    "post/output/grain": 0.012, "post/grade/contrast": 1.22, "post/lens/chromaticAberration": 0.0,
    "camera/exposure/compensation": 0.2, "post/dof/enabled": False,
}


def design(thesis, focal, foreground, camera):
    return {
        "category": "abstract", "thesis": thesis,
        "composition": {"background": "a ring of basalt spires and far mountains under an indigo sky",
                        "midground": "the basin's terraced rim", "foreground": foreground, "focal": focal,
                        "camera": camera},
        "palette": {"dominant": P["stone"], "secondary": P["sand"], "accent": P["sandHot"], "highlight": P["hollow"],
                    "background_value": "near black", "saturation": "low, with selective cyan and violet"},
        "tier": "one resonance field (ADR-1400)",
    }


# ------------------------------------------------------------------------------------------------ Cymatic World
def build_world():
    """THE RESONANT GROUND, alone: the basin is the instrument. Without the creature's lamp the ground is lit by a
    raking warm moon and a cool counter light from the opposite quarter, so every ridge reads on both flanks and the
    hollows stay dark; the nodal lines carry a thin cold light only where the ground rings."""
    cc = CC_FIELD + [(5, "relief", "TERRAIN: how deeply the ground's sand relief is carved"),
                     (8, "light", "LIGHT: the counter light over the ridges, and the nodal lines' glow")]
    pads = [PAD_STRIKE]
    s = kit.Scene("cymatic-world", "Cymatic World", design(
        "A night basin of basalt whose ground is a resonating plate: the music excites one field of standing waves, "
        "and the grit settles on its nodal lines, carving rings, petals and cells into the ground.",
        "the basin floor's figure", "the basin floor, its sand relief drawing the figure",
        "a slow high arc over the basin, there and back in 160 s"))
    environment(s, rim=0.8)
    resonator(s, [
        # the basin's voice: the low modes -- broad rings and petals about its centre
        {"kind": "point", "position": list(HEART), "order": 4, "bandLow": 0.0, "bandHigh": 0.45, "reach": 75.0,
         "gain": 1.0},
        # a second centre off to the west: middle modes whose rings interfere with the first's
        {"kind": "point", "position": list(SECOND), "order": 3, "bandLow": 0.3, "bandHigh": 0.7, "reach": 50.0,
         "gain": 0.8},
        # a Faraday carpet for the fine modes on the floor (hexagons), a layer that fades up the terraces
        {"kind": "lattice", "position": [0.0, -6.0, 0.0], "order": 3, "bandLow": 0.62, "bandHigh": 1.0,
         "reach": 46.0, "gain": 0.7, "depth": 9.0},
    ])
    ground_readers(s)
    g = ground(s, relief=2.6, counter=0.9, line=0.45, write_mesh=True, moon=11.0, sky=0.03)
    spires(s)
    s.camera = {"mode": 1, "position": [0.0, 16.0, 44.0], "target": [-2.0, -3.0, 0.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.arc_camera([-2.0, -3.0, 0.0], 44.0, 16.0, period=160.0, centre_deg=8.0, sweep_deg=80.0, steps=24, side=0.0,
                 lift=0.0, height_bob=2.0)
    s.params_({"camera/lens/focalLength": 26.0})
    F = field_instrument(s)
    counter = "material/cwGround/op/%d/constant/constant" % g["counter"]
    line = "material/cwGround/op/%d/constant/constant" % g["lineCol"]
    # the music's loudness lifts the counter light a little (a breath of light over the ridges, slow)
    s.route(R("level", counter, 1.0, op="multiply", gain=0.5, offset=1.0, attackMs=600, decayMs=2000))
    s.control = control_map(cc, pads)
    s.route(R("control.relief", "procedural/ground/deform/1/amount", 3.0, **CCR))
    s.route(R("control.light", counter, 1.0, op="multiply", gain=1.5, offset=1.0, **CCR),
            R("control.light", line, 1.0, op="multiply", gain=2.0, offset=1.0, **CCR))
    s.params_(POST)
    return s, cc, pads


# ------------------------------------------------------------------------------------------------ Cymatic Crystals
CRYSTAL_ROOT = (0.0, 0.0)        # the druse stands at the basin's centre


def build_crystals():
    """THE TUNED DRUSE, the hero: thirty-odd stepped quartz-habit crystals radiating from one root on the basin
    floor, tuned by length (the long core hears the low modes, the short fringe the high). Its setting is the basin,
    whose floor RINGS AT ITS FOOT: the floor's own low modes draw rings round the root, and -- sympathetic resonance,
    ADR-1402, kept because in this scene it is the reason the floor is there -- the floor's ringing at the root drives
    the druse's own source, so a bass note rings the floor and the crystals answer in their higher voice."""
    cc = CC_FIELD + [(5, "relief", "FLOOR: how deeply the resonating floor at the druse's foot is carved"),
                     (6, "druse", "CRYSTALS: how strongly the druse quivers and lights"),
                     (7, "sympathy", "SYMPATHY: how much the floor's ringing drives the crystals (the coupling)"),
                     (8, "light", "LIGHT: the druse's own light on the floor and in its heart")]
    pads = [PAD_STRIKE]
    s = kit.Scene("cymatic-crystals", "Cymatic Crystals", design(
        "A crystal druse on a basalt floor that rings at its foot: each crystal is tuned by its length to its own band "
        "of the field, a bass note rings the floor and the crystals answer in their higher voice.",
        "the druse", "the resonating floor at the druse's foot",
        "a slow orbit round the druse, a little below its crown"))
    environment(s, rim=1.4)
    gy = ground_y(*CRYSTAL_ROOT)
    resonator(s, [
        # the floor: the low modes, rings about the root
        {"kind": "point", "position": [CRYSTAL_ROOT[0], gy, CRYSTAL_ROOT[1]], "order": 3, "bandLow": 0.0,
         "bandHigh": 0.3, "reach": 60.0, "gain": 1.0},
        # the druse's own voice: everything above, about the cluster (driven by the music AND by the floor)
        {"kind": "point", "position": [CRYSTAL_ROOT[0], gy + 6.0, CRYSTAL_ROOT[1]], "order": 4, "bandLow": 0.32,
         "bandHigh": 1.0, "reach": 30.0, "gain": 0.9, "scale": 1.4},
    ], idle=0.45)
    ground_readers(s)
    # the coupling's ear: the floor's low modes at the root, published as `field.druseFoot.probe`
    reader(s, "druseFoot", "amplitude", {"listenLow": 0.0, "listenHigh": 0.26, "listenWidth": 0.02,
                                         "probe": [CRYSTAL_ROOT[0], gy + 0.5, CRYSTAL_ROOT[1]]})
    glow = ([CRYSTAL_ROOT[0], gy + 3.5, CRYSTAL_ROOT[1]], 520.0)
    g = ground(s, relief=1.4, lamp=glow, line=0.3)
    druse(s, gy)
    s.light("heart", "point", position=glow[0], color=C("crystalGlow"), intensity=90.0, range=40.0, castsShadow=False)
    spire_node(s)
    tgt = [CRYSTAL_ROOT[0], gy + 4.5, CRYSTAL_ROOT[1]]
    s.camera = {"mode": 1, "position": [0.0, gy + 10.0, 22.0], "target": tgt, "fov": 40.0, "orbitSpeed": 0.0}
    s.orbit_camera(tgt, 22.0, gy + 10.0, period=200.0, start_deg=0.0, sweep_deg=360.0, steps=24, height_bob=1.5)
    s.params_({"camera/lens/focalLength": 32.0})
    F = field_instrument(s)
    lamp = "material/cwGround/op/%d/constant/constant" % g["lamp"]
    s.route(R("level", lamp, 1.0, op="multiply", gain=0.8, offset=1.0, attackMs=400, decayMs=1800),
            R("level", "lights/heart/intensity", 70.0, attackMs=400, decayMs=1800))
    # THE COUPLING: the floor's ringing at the root drives the druse's own source
    s.route(R("field.druseFoot.probe", F + "source/1/drive", 2.0, attackMs=250, decayMs=2000))
    s.control = control_map(cc, pads)
    s.route(R("control.relief", "procedural/ground/deform/1/amount", 2.0, **CCR))
    s.route(R("control.druse", "procedural/druse/effector/1/strength", 0.15, **CCR),
            R("control.druse", "procedural/druse/effector/2/strength", 50.0, **CCR))
    s.route(R("control.sympathy", F + "source/1/drive", 1.5, depth="field.druseFoot.probe", **CCR))
    s.route(R("control.light", "lights/heart/intensity", 200.0, **CCR),
            R("control.light", lamp, 1.0, op="multiply", gain=1.8, offset=1.0, **CCR))
    s.params_(POST)
    return s, cc, pads


def druse(s, gy):
    pos, nrm = meshes.faceted(crystal_tris())
    meshes.write_glb(os.path.join(OUT_CRYSTALS, "meshes", "cw-crystal.glb"), pos, nrm, None, "crystal")
    pts, root = druse_points(gy)
    top = max(p[1] for p in pts)
    span = top - float(root[1])
    for name, channel in (("tuned", "displacement"), ("tunedRing", "amplitude")):
        # down the cluster: the highest crystal centres (the long core) hear the low modes
        reader(s, name, channel, {"listenAlongAxis": True, "listenLow": 0.0, "listenHigh": 1.0, "listenWidth": 0.14},
               {"axis": [0.0, -1.0, 0.0], "point": [CRYSTAL_ROOT[0], float(root[1]) + span * 0.5, CRYSTAL_ROOT[1]],
                "length": span})
    c = crystal_program()
    s.program(c.doc(emission=c.emit))
    s.proc("druse", {"kind": "mesh", "asset": "meshes/cw-crystal.glb"},
           distribution={"kind": "points", "points": pts},
           material={"baseColor": C("crystal"), "emissiveColor": C("crystalGlow"), "emissiveIntensity": 0.025,
                     "roughness": 0.12, "metallic": 0.85, "thinFilm": {"thickness": 420.0, "ior": 1.7},
                     "program": c.name},
           extra={"effectors": [
               {"field": "tuned", "op": "rotation", "strength": 0.06, "axis": [1.0, 0.0, 0.0]},
               {"field": "tunedRing", "op": "emission", "blend": "multiply", "strength": 22.0},
           ]})


# ------------------------------------------------------------------------------------------------ Cymatic Creature
def build_creature():
    """THE MEDUSA, the hero: grains held by the field's radiation force on the nodal shells about the heart, woven
    into rings by a slow swirl, a veil of strands let fall from its rim through the same force. It is the basin's
    lamp: its cold light pools on the floor below, where its low modes draw faint rings. Its own resonator has a long
    memory and strong competition, so its shells hold one figure and move slowly: grains settle on a shell instead of
    streaming between shells (the combined scene's 'hairy ball' in dense passages)."""
    cc = CC_FIELD + [(5, "relief", "FLOOR: how deeply the floor below the medusa is carved"),
                     (7, "swirl", "SWIRL: the grains circle faster along the medusa's shells"),
                     (8, "light", "LIGHT: the medusa's own light over the basin")]
    pads = [PAD_STRIKE, (37, "padShed", "SHED: the medusa lets fall a curtain of strands")]
    s = kit.Scene("cymatic-creature", "Cymatic Creature", design(
        "A medusa of grains held on the nodal shells of a sound field above a basalt basin: the music re-forms its "
        "body, its veil falls through the same field, and its cold light pools on the floor below.",
        "the medusa", "the lamplit floor below the medusa", "a slow arc round the medusa, level with its rim"))
    environment(s, rim=1.0)
    resonator(s, [
        # the floor below: the low modes, faint rings in the lamplight
        {"kind": "point", "position": list(HEART), "order": 3, "bandLow": 0.0, "bandHigh": 0.3, "reach": 60.0,
         "gain": 0.8},
        # the medusa's own voice: everything above, a fine body bounded to it
        {"kind": "point", "position": list(HEART), "order": 5, "bandLow": 0.32, "bandHigh": 1.0, "reach": 16.0,
         "gain": 1.6, "scale": 2.2},
    ], memory=3.5, competition=4.5, idle=0.45)
    ground_readers(s)
    reader(s, "force", "force", {"listenLow": 0.32, "listenHigh": 1.0, "listenWidth": 0.04})
    lampPos = [HEART[0], HEART[1] - 1.0, HEART[2]]
    g = ground(s, relief=1.0, lamp=(lampPos, 1600.0), line=0.25)
    medusa(s)
    s.light("heart", "point", position=lampPos, color=C("sandHot"), intensity=1600.0, range=140.0, castsShadow=False)
    spire_node(s)
    tgt = [HEART[0], HEART[1] - 2.0, HEART[2]]
    s.camera = {"mode": 1, "position": [0.0, 17.0, 32.0], "target": tgt, "fov": 40.0, "orbitSpeed": 0.0}
    s.arc_camera(tgt, 32.0, 17.0, period=150.0, centre_deg=10.0, sweep_deg=90.0, steps=24, side=0.0, lift=0.0,
                 height_bob=1.5)
    s.params_({"camera/lens/focalLength": 30.0})
    F = field_instrument(s)
    lamp = "material/cwGround/op/%d/constant/constant" % g["lamp"]
    s.route(R("level", "lights/heart/intensity", 1400.0, attackMs=400, decayMs=1800),
            R("kickEnv", "lights/heart/intensity", 500.0, attackMs=0, decayMs=350),
            R("level", lamp, 1.0, op="multiply", gain=0.9, offset=1.0, attackMs=400, decayMs=1800),
            R("kickEnv", lamp, 1.0, op="multiply", gain=0.3, offset=1.0, attackMs=0, decayMs=350))
    s.route(R("kick", "particles/veil/burst", 160.0, attackMs=0, decayMs=40))   # kicks shed strands
    s.control = control_map(cc, pads)
    s.route(R("control.relief", "procedural/ground/deform/1/amount", 2.0, **CCR))
    s.route(R("control.swirl", "particles/matter/orbit", 25.0, **CCR))
    s.route(R("control.light", "lights/heart/intensity", 3000.0, **CCR),
            R("control.light", lamp, 1.0, op="multiply", gain=1.8, offset=1.0, **CCR))
    s.route(R("control.padShed", "particles/veil/burst", 1500.0, attackMs=0, decayMs=40))
    s.params_(POST)
    return s, cc, pads


def medusa(s):
    """Grains floating in the heart's sound field, the way acoustic levitation traps them: critically damped, pushed
    by the field's radiation force (-grad F^2 of the slow figure) onto the nodal shells, swirled along them, each
    drawing a short ribbon; a plane field cuts the bell; a veil of strands falls from its rim through the same force."""
    s.nodes.append({"name": "bellCut", "kind": "field", "position": [HEART[0], HEART[1] - 3.0, HEART[2]],
                    "field": {"kind": "plane", "axis": [0.0, -1.0, 0.0], "softness": 0.01, "strength": 1.0,
                              "falloff": {"kind": "none"}}})
    s.particles("matter", capacity=22000, seed=5, shape="sphere", position=list(HEART), extent=[10.0, 10.0, 10.0],
                direction=[0, 1, 0], spawnRate=260.0, lifetimeMin=70.0, lifetimeMax=100.0, spread=1.0, speedMin=0.0,
                speedMax=0.1, gravity=[0, 0, 0], drag=14.0, turbulence=0.0, turbulenceScale=0.3, turbulenceSpeed=0.1,
                attractorPosition=list(HEART), attractorStrength=1.5, attractorRadius=11.0, orbit=14.0,
                fieldForces=[{"field": "force", "mode": "force", "strength": 100.0, "mix": 1.0, "axis": [0, 1, 0]},
                             {"field": "bellCut", "mode": "kill", "strength": 1.0, "mix": 1.0, "axis": [0, 1, 0]}],
                sizeStart=0.05, sizeEnd=0.04, sizeVariance=0.4,
                trailEnabled=True, trailLength=18, trailStride=6, trailWidth=0.45, trailTaper=0.15, trailFade=0.05,
                colorStart=C("sandHot") + [0.85], colorEnd=C("hollow") + [0.0], emissive=1.3, blend="additive")
    s.particles("veil", capacity=6000, seed=9, shape="disc", position=[HEART[0], HEART[1] - 3.2, HEART[2]],
                extent=[8.5, 8.5, 8.5], direction=[0, -1, 0], spawnRate=420.0, lifetimeMin=10.0, lifetimeMax=14.0,
                spread=0.1, speedMin=0.2, speedMax=0.5, gravity=[0, -2.5, 0], drag=6.0, turbulence=0.0,
                orbit=0.0, attractorStrength=0.0,
                fieldForces=[{"field": "force", "mode": "force", "strength": 100.0, "mix": 1.0, "axis": [0, 1, 0]}],
                sizeStart=0.04, sizeEnd=0.015, sizeVariance=0.3,
                trailEnabled=True, trailLength=32, trailStride=5, trailWidth=0.5, trailTaper=0.05, trailFade=0.0,
                colorStart=C("sandHot") + [0.7], colorEnd=C("hollow") + [0.0], emissive=1.1, blend="additive")


BUILDERS = {"world": build_world, "crystals": build_crystals, "creature": build_creature}
OUT_DIRS = {k: os.path.join(REPO, "examples", v["id"]) for k, v in SCENES.items()}
OUT_CRYSTALS = OUT_DIRS["crystals"]
DESCRIPTION_TAIL = {
    "world": "The resonant ground alone (Prototype A of the brief).",
    "crystals": "The tuned crystal druse (Prototype B); the floor's ringing drives the crystals (sympathetic "
                "resonance, ADR-1402).",
    "creature": "The resonant matter (Prototype C): a medusa of grains on the field's nodal shells.",
}


def write_scene(key):
    os.makedirs(OUT_DIRS[key], exist_ok=True)
    s, cc, pads = BUILDERS[key]()
    pp, sp = s.write(OUT_DIRS[key])
    ccs = ", ".join("CC %d %s" % (n, d.split(":")[0].lower()) for n, _, d in cc)
    pds = ", ".join("pad %d %s" % (n, d.split(":")[0].lower()) for n, _, d in pads)
    kit.upsert_index_entry({
        "category": "Cymatic World",
        "description": "Play live audio (any mix) and, optionally, a MIDI controller: " + ccs + "; " + pds + ". "
                       + s.design["thesis"] + " " + DESCRIPTION_TAIL[key] + " One resonance field (ADR-1400). See "
                       "docs/prototypes/cymatic-world/.",
        "live": True,
        "name": SCENES[key]["title"],
        "project": "%s/%s.json" % (SCENES[key]["id"], SCENES[key]["id"])})
    return pp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", default="all", choices=["all", "world", "crystals", "creature"])
    ap.add_argument("--variant", help="also write a file project of --scene driven by this WAV")
    ap.add_argument("--out", help="the variant's path")
    ap.add_argument("--camera", help="x,y,z:tx,ty,tz for the variant")
    ap.add_argument("--set", action="append", default=[], help="path=json value, for the variant")
    a = ap.parse_args()
    keys = ["world", "crystals", "creature"] if a.scene == "all" else [a.scene]
    if "world" not in keys and not os.path.exists(os.path.join(MESH_DIR, "cw-ground.glb")):
        keys = ["world"] + keys  # the shared ground and spire meshes live with Cymatic World
    pp = None
    for k in keys:
        pp = write_scene(k)
        print(pp)
    if a.variant:
        if a.scene == "all":
            raise SystemExit("--variant needs one --scene")
        params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
        print(variant(pp, a.variant, a.out, a.camera, params))


if __name__ == "__main__":
    main()

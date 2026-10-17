"""Shared pieces of the generative R&D generators (docs/prototypes/generative-rd/).

Each experiment's generator (`oscillators.py`, `flow.py`, `reaction_diffusion.py`) builds its configurations with
these: a `kit.Scene` that also writes grids, a material program builder, mesh builders and a GLB writer, the review
variant writer (a file project with a WAV and overrides, as Resonant Silk's), and the index entry under the
"Experiments" category.

Conventions: metres, +Y up, colours linear RGB (`hexrgb` converts sRGB references).
"""
from __future__ import annotations

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
from tools.sonic_vfx.kit import R, hexrgb  # noqa: E402,F401

EXAMPLES = os.path.join(REPO, "examples", "experiments")
CATEGORY = "Experiments"


def mix(a, b, t):
    return [x + (y - x) * t for x, y in zip(a, b)]


def scale(a, s):
    return [x * s for x in a]


# ================================================================================================ scene
class Scene(kit.Scene):
    """kit.Scene plus simulated grids (`grids` in the scene file)."""

    def __init__(self, sid, title, design):
        super().__init__(sid, title, design)
        self.grids = []

    def grid(self, **g):
        self.grids.append(g)
        return g

    def field(self, name, field, position=(0.0, 0.0, 0.0)):
        f = {"strength": 1.0, "falloff": {"kind": "none"}}
        f.update(field)
        n = {"name": name, "kind": "field", "position": [float(v) for v in position], "field": f}
        self.nodes.append(n)
        return n

    def grid_reader(self, name, grid, channel=None, strength=1.0):
        f = {"kind": "grid", "reference": grid, "strength": strength}
        if channel is not None:
            f["channel"] = channel
        return self.field(name, f)

    def scene_doc(self):
        doc = super().scene_doc()
        if self.grids:
            doc["grids"] = self.grids
        return doc


# ================================================================================================ material programs
class Prog:
    """A material program assembled by label: `op(label, ...)` appends one op and remembers its 1-based number, which
    is what a route's `material/<name>/op/<n>/<kind>/<field>` path addresses (Cymatic World's pattern)."""

    def __init__(self, name):
        self.name = name
        self.ops = []
        self.at = {}

    def op(self, label, kind, dst, a=None, b=None, c=None, k=None, f=None, inp=None, field=None, k2=None, k3=None,
           k4=None):
        o = {"kind": kind, "dst": dst}
        if kind == "remap" and f is None:
            f = 0.0
        for key, v in (("srcA", a), ("srcB", b), ("srcC", c)):
            if v is not None:
                o[key] = v
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
        assert len(self.ops) <= 48, "%s: more than 48 ops" % self.name
        return len(self.ops)

    def __getitem__(self, label):
        return self.at[label]

    def path(self, label, field="constant"):
        return "material/%s/op/%d/%s/%s" % (self.name, self[label], self.ops[self[label] - 1]["kind"], field)

    def doc(self, base=-1, emission=-1, roughness=-1, metallic=-1):
        fields = {o["field"] for o in self.ops if o["kind"] == "field"}
        assert len(fields) <= 4, "%s names %d fields (4 at most)" % (self.name, len(fields))
        return {"name": self.name, "ops": self.ops, "baseColor": base, "metallic": metallic, "roughness": roughness,
                "emission": emission, "emissionIntensity": 1.0, "opacity": -1}


# ================================================================================================ meshes
def write_glb(path, pos, nrm, uv, idx, name):
    pos = np.ascontiguousarray(pos, dtype=np.float32)
    nrm = np.ascontiguousarray(nrm, dtype=np.float32)
    uv = np.ascontiguousarray(uv, dtype=np.float32)
    idx = np.ascontiguousarray(idx, dtype=np.uint32)
    pb, nb, ub, ib = pos.tobytes(), nrm.tobytes(), uv.tobytes(), idx.tobytes()
    blob = pb + nb + ub + ib
    blob += b"\0" * ((4 - len(blob) % 4) % 4)
    lo, hi = pos.min(axis=0).tolist(), pos.max(axis=0).tolist()
    o1, o2, o3 = len(pb), len(pb) + len(nb), len(pb) + len(nb) + len(ub)
    gltf = {
        "asset": {"version": "2.0", "generator": "tools/generative_rd"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "materials": [{"name": name, "doubleSided": True,
                       "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0.0,
                                                "roughnessFactor": 0.5}}],
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


def grid_indices(nu, nv, wrap_u=False):
    """Two triangles per quad of an nu x nv vertex grid (row-major, u fastest), counter-clockwise seen from +normal."""
    cols = nu if wrap_u else nu - 1
    idx = []
    for j in range(nv - 1):
        for i in range(cols):
            a = j * nu + i
            b = j * nu + (i + 1) % nu
            c = a + nu
            d = b + nu
            idx += [a, c, b, b, c, d]
    return np.array(idx, dtype=np.uint32)


def plane_mesh(sx, sz, nx, nz, y=0.0):
    """A flat sheet in XZ centred on the origin, normal +Y, uv over [0, 1]^2."""
    xs = np.linspace(-sx / 2, sx / 2, nx)
    zs = np.linspace(-sz / 2, sz / 2, nz)
    X, Z = np.meshgrid(xs, zs)
    pos = np.stack([X.ravel(), np.full(X.size, y), Z.ravel()], axis=1)
    nrm = np.tile([0.0, 1.0, 0.0], (len(pos), 1))
    U, V = np.meshgrid(np.linspace(0, 1, nx), np.linspace(0, 1, nz))
    uv = np.stack([U.ravel(), V.ravel()], axis=1)
    # (x, z) grid: triangles (a, c, b) have normal (c - a) x (b - a) = +Y, counter-clockwise seen from above
    idx = grid_indices(nx, nz)
    return pos, nrm, uv, idx


def sphere_mesh(radius, nu, nv):
    """A UV sphere (poles closed by a fan of degenerate quads)."""
    th = np.linspace(0, 2 * math.pi, nu, endpoint=False)
    ph = np.linspace(0, math.pi, nv)
    T, P = np.meshgrid(th, ph)
    n = np.stack([np.sin(P) * np.cos(T), np.cos(P), np.sin(P) * np.sin(T)], axis=-1).reshape(-1, 3)
    pos = n * radius
    U, V = np.meshgrid(np.arange(nu) / nu, np.linspace(0, 1, nv))
    uv = np.stack([U.ravel(), V.ravel()], axis=1)
    idx = grid_indices(nu, nv, wrap_u=True)
    return pos, n, uv, idx


def merge(*meshes):
    P, N, U, I = [], [], [], []
    base = 0
    for pos, nrm, uv, idx in meshes:
        P.append(pos)
        N.append(nrm)
        U.append(uv)
        I.append(np.asarray(idx) + base)
        base += len(pos)
    return np.concatenate(P), np.concatenate(N), np.concatenate(U), np.concatenate(I)


# ================================================================================================ files
def write_example(s, out_dir):
    pp, sp = s.write(out_dir)
    return pp, sp


def index_entry(name, project, description, live=True, hidden=False):
    e = {"category": CATEGORY, "description": description, "live": live, "name": name, "project": project}
    if hidden:
        e["hidden"] = True
    kit.upsert_index_entry(e)


def variant(project, out, wav=None, camera=None, params=None, tracks=None):
    """A file project of a scene for review renders: optionally a WAV (else silent), another camera
    ("x,y,z:tx,ty,tz"), parameter overrides and stepped timeline keys ("path=v0@t0,v1@t1")."""
    with open(project) as f:
        doc = json.load(f)
    if camera:
        p, t = camera.split(":")
        doc["parameters"]["camera/position"] = [float(v) for v in p.split(",")]
        doc["parameters"]["camera/target"] = [float(v) for v in t.split(",")]
        tl = doc.get("timeline", {})
        tl["tracks"] = [tr for tr in tl.get("tracks", []) if not tr["target"].startswith("camera/")]
    for k, v in (params or {}).items():
        doc["parameters"][k] = v
    base = os.path.dirname(os.path.abspath(project))
    doc["assets"]["scene"] = {"kind": "composition", "path": os.path.join(base, doc["assets"]["scene"]["path"])}
    if wav:
        doc["assets"]["audio"] = {"path": os.path.abspath(wav)}
    doc.get("sonic", {}).pop("live", None)
    for spec in tracks or []:
        path, keys = spec.split("=", 1)
        kk = []
        for item in keys.split(","):
            v, t = item.split("@")
            kk.append({"time": float(t), "value": [float(v)], "interp": "step"})
        doc.setdefault("timeline", {"enabled": True, "cues": [], "tracks": []})["tracks"].append(
            {"target": path, "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
             "loopLength": 0.0, "keys": kk})
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w") as f:
        json.dump(doc, f, indent=1)
    return out


def lissajous_track(s, target, centre, amp, period, ratio=(1.0, 1.0), phase=(0.0, 0.5), steps=24):
    """A looping smooth path on XZ about `centre` (a field's or anything's position): x = A sin(2 pi rx t / T + px),
    z = B sin(2 pi rz t / T + pz). `ratio` must be whole numbers for the loop to close."""
    keys = []
    for i in range(steps + 1):
        t = period * i / steps
        x = centre[0] + amp[0] * math.sin(2 * math.pi * (ratio[0] * t / period + phase[0]))
        z = centre[2] + amp[1] * math.sin(2 * math.pi * (ratio[1] * t / period + phase[1]))
        keys.append({"time": round(t, 4), "value": [round(x, 4), float(centre[1]), round(z, 4)], "interp": "smooth"})
    s.track(target, keys, loop=period)

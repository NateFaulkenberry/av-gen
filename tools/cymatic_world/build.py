#!/usr/bin/env python3
"""CYMATIC WORLD: the generator (docs/prototypes/cymatic-world/).

    python3 tools/cymatic_world/build.py            # writes examples/cymatic-world/ (scene, project, meshes)
    python3 tools/cymatic_world/build.py --variant <wav> --out <project.json> [--prototype a|b|c|all]

One shared resonance field (FieldKind::Resonance, ADR-1400) drives everything: the audio history excites its modes;
the ground's sand relief and vibration, the sculpture's tiers and the floating matter each read it their own way.

The ground's geography comes from the engine's WorldMap (the terrain system's noise layers and ridge/valley/flat
features), probed through `avgen_world_preview --heights` at every vertex of one screen-density mesh, because the
resonance has to move it on the GPU and a terrain node's chunks take no deformers.
"""
from __future__ import annotations

import argparse
import copy
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
from tools.sonic_vfx.kit import R, M, hexrgb  # noqa: E402

ID = "cymatic-world"
TITLE = "Cymatic World"
OUT = os.path.join(REPO, "examples", "cymatic-world")
MESH_DIR = os.path.join(OUT, "meshes")
CATEGORY = "Cymatic World"

PROTOTYPE = "all"  # a | b | c | all (set by --prototype)

# ================================================================================================ palette
# Night in a basalt basin: blue-black stone, indigo air, a violet rim on the horizon; the only saturated light is
# the field's own (cyan-teal on the nodal lines, violet in the energised hollows), and it is used sparingly.
P = {
    "stone": "#0d0c16",      # basalt
    "stoneLit": "#1b1930",   # its lit face
    "sand": "#4a4766",       # the settled grit on the nodal ridges (cool grey-violet)
    "sandHot": "#8fe8ff",    # the nodal line's light (cyan)
    "hollow": "#7a3cff",     # energy in the antinodes (violet)
    "zenith": "#05060f",
    "horizon": "#2a1d52",
    "glowHorizon": "#5b3aa6",
    "fog": "#0c0b1f",
    "moon": "#dfe4ff",       # the key: a cold moon, low, raking the ridges
    "rim": "#a77bff",
}


def C(key, scale=1.0):
    return hexrgb(P[key], scale)


# ================================================================================================ the field
FIELD = "cymatic"
HEART = (0.0, 9.0, 0.0)          # the singing source above the basin's centre (the matter floats here)
SPIRE = (-34.0, 0.0, 12.0)       # the sculpture's foot: a second, local source


def fields(s):
    """The resonator and its readers. Everything that answers the music reads one of these."""
    res = {
        "modes": 11.0, "fLow": 40.0, "fHigh": 6000.0, "lambdaMax": 34.0, "lambdaMin": 5.0, "scale": 1.0,
        "selectivity": 3.0, "contrast": 0.45, "persistence": 2.8, "buildup": 0.3, "memory": 1.6,
        "sustain": 1.0, "transient": 0.9, "idle": 0.5, "breath": 0.16, "limit": 0.9, "competition": 3.0,
        "sources": [
            # the heart: low and middle modes, rings and petals about the basin's centre, heard everywhere
            {"kind": "point", "position": list(HEART), "order": 5, "bandLow": 0.0, "bandHigh": 0.7, "reach": 0.0,
             "gain": 1.0},
            # the sculpture's own local source: middle modes, three-fold at most
            {"kind": "point", "position": list(SPIRE), "order": 3, "bandLow": 0.2, "bandHigh": 0.85, "reach": 55.0,
             "gain": 0.8},
            # a Faraday carpet for the fine modes: a five-fold quasicrystal, bounded so the far ground stays calm
            {"kind": "lattice", "position": [0.0, 0.0, 0.0], "order": 3, "bandLow": 0.5, "bandHigh": 1.0,
             "reach": 95.0, "gain": 0.75},
        ],
        "channel": "displacement",
    }
    s.nodes.append({"name": FIELD, "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "resonance", "seed": 7, "strength": 1.0, "falloff": {"kind": "none"},
                              "resonance": res}})
    for name, channel, extra in (("sand", "sand", {"sharpness": 0.9}),
                                 ("line", "sand", {"sharpness": 0.3}),
                                 ("glow", "amplitude", {}),
                                 ("force", "force", {})):
        r = {"channel": channel}
        r.update(extra)
        s.nodes.append({"name": name, "kind": "field", "position": [0, 0, 0],
                        "field": {"kind": "resonance", "reference": FIELD, "strength": 1.0,
                                  "falloff": {"kind": "none"}, "resonance": r}})


# ================================================================================================ the world map
WORLD = {
    "name": "basin", "seed": 20261016, "size": [6000.0, 6000.0], "erosion": 0.55, "seaLevel": -1000.0,
    "layers": [
        {"frequency": 0.0035, "amplitude": 30.0, "ridged": 0.35, "warp": 40.0},
        {"frequency": 0.012, "amplitude": 9.0, "ridged": 0.75, "warp": 12.0},
        {"frequency": 0.045, "amplitude": 1.8, "ridged": 0.5, "warp": 2.0},
    ],
    "features": [
        # the basin floor: a broad, smooth bowl where the figure plays
        {"name": "floor", "kind": "flat", "path": [[0.0, -4.0, 0.0]], "width": 78.0, "amplitude": 0.0,
         "falloff": 0.8, "flatten": 0.92, "roughness": 0.08, "smoothing": 0},
        # its rim: a ring ridge 95 m out, lower toward the camera's side so the eye passes over it
        {"name": "rim", "kind": "ridge", "path": [[95.0 * math.cos(a), 0.0, 95.0 * math.sin(a)]
                                                 for a in [i * 2.0 * math.pi / 24 for i in range(25)]],
         "width": 34.0, "amplitude": 10.0, "falloff": 1.2, "flatten": 0.0, "roughness": 1.0, "smoothing": 2},
        # a gap in the rim toward the south (+z): the camera's way in, and a long view out of the basin
        {"name": "gap", "kind": "valley", "path": [[0.0, 0.0, 60.0], [6.0, 0.0, 110.0], [18.0, 0.0, 170.0]],
         "width": 40.0, "amplitude": 9.0, "falloff": 0.9, "flatten": 0.0, "roughness": 0.6, "smoothing": 2},
        # far mountains: a ring of ridges 900 m out fills the horizon in every direction
        {"name": "range", "kind": "ridge", "path": [[900.0 * math.cos(a), 0.0, 900.0 * math.sin(a)]
                                                   for a in [i * 2.0 * math.pi / 16 for i in range(17)]],
         "width": 520.0, "amplitude": 160.0, "falloff": 0.7, "flatten": 0.0, "roughness": 2.2, "smoothing": 2},
    ],
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
    h = np.loadtxt(pout, dtype=np.float64)
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


def ground_program():
    """The basin's ground. Basalt where the field has scoured it, settled grit on the nodal ridges (the sand field),
    a cold light along the nodal line only where the ground is ringing (sand x amplitude), and a faint violet in the
    hollows that ring hardest. Emission stays a minor term: the relief and the raking moon carry the figure."""
    g = Prog("cwGround")
    g.op("sand", "field", 0, field="sand")                         # r0: sand (0 .. ~1)
    g.op("glow", "field", 1, field="glow")                         # r1: the local excitation (fast)
    # the grit mask: where sand has settled (a soft step of the sand field)
    g.op("gritMask", "smoothstep", 2, a=0, k=[0.10, 0.55])
    # stone with a mottle so a hillside is not one flat colour (a world-space fbm, cheap, low contrast)
    g.op(None, "input", 3, inp="worldPosition")
    g.op("mottle", "noise", 3, a=3, f=0.11, seed=5)
    g.op(None, "remap", 3, a=3, k=[0.2, 0.8, 0.75, 1.2], f=1.0)
    g.op("stone", "constant", 4, k=C("stone") + [1.0])
    g.op(None, "multiply", 4, a=4, b=3)
    g.op("grit", "constant", 5, k=C("sand") + [1.0])
    g.op(None, "mixBy", 4, a=4, b=5, c=2)                          # r4: base colour
    # the nodal line's light: sand that is sharply on the line (a tighter step) x how hard the ground rings there
    g.op("line", "field", 6, field="line")
    g.op("lineMask", "smoothstep", 6, a=6, k=[0.35, 0.8])
    g.op(None, "multiply", 6, a=6, b=1)
    g.op("lineCol", "constant", 7, k=C("sandHot", 0.55) + [1.0])
    g.op(None, "multiply", 6, a=6, b=7)                            # r6: line emission
    # the hollows: the excitation where there is no sand (antinodes), violet, faint
    g.op(None, "remap", 2, a=2, k=[0.0, 1.0, 1.0, 0.0], f=1.0)     # 1 - grit
    g.op(None, "multiply", 2, a=2, b=1)
    g.op(None, "multiply", 2, a=2, b=1)                            # ~ amplitude^2 in the hollows
    g.op("hollowCol", "constant", 7, k=C("hollow", 0.03) + [1.0])
    g.op(None, "multiply", 2, a=2, b=7)
    g.op(None, "add", 6, a=6, b=2)                                 # r6: emission
    # roughness: grit is matte, scoured basalt a little polished (it catches the moon as a sheen)
    g.op("rough", "constant", 3, k=[0.82, 0.82, 0.82, 1.0])
    g.op("roughGrit", "constant", 5, k=[0.86, 0.86, 0.86, 1.0])
    g.op(None, "smoothstep", 7, a=0, k=[0.10, 0.55])
    g.op(None, "mixBy", 3, a=3, b=5, c=7)
    g.base, g.emit, g.rough = 4, 6, 3
    return g


# ================================================================================================ the scene
def environment(s):
    s.environment = {
        "intensity": 0.22, "background": C("zenith"), "fogColor": C("fog"),
        "volumeDensity": 0.0, "fogHeight": 12.0, "fogHeightFalloff": 0.04, "skyIntensity": 1.0,
        "shadowRange": 180.0,
        "sky": {"enabled": True, "zenithColor": C("zenith"), "horizonColor": C("horizon"),
                "groundColor": C("fog"), "haze": 0.45, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    # the moon: low and to the side, so it rakes the ridges and every hollow holds a shadow
    s.light("moon", "directional", direction=[0.62, -0.32, -0.72], color=C("moon"), intensity=4.5,
            castsShadow=False, contactShadow=False)
    # a violet rim from behind the basin
    s.light("rim", "directional", direction=[-0.2, -0.15, 0.97], color=C("rim"), intensity=0.45, castsShadow=False)


def ground(s):
    pos, nrm, idx = ground_mesh()
    write_glb(os.path.join(MESH_DIR, "cw-ground.glb"), pos, nrm, idx, "ground")
    print("ground: %d vertices, %d triangles" % (len(pos), len(idx) // 3))
    g = ground_program()
    s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
    s.proc("ground", {"kind": "mesh", "asset": "meshes/cw-ground.glb"},
           material={"baseColor": [1, 1, 1], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0,
                     "roughness": 0.6, "metallic": 0.0, "program": g.name},
           deformers=[
               # the sand: ridges rise along the nodal lines, the antinodes are scoured (the figure is in the relief)
               {"kind": "field", "field": "sand", "amount": 1.3, "axis": [0.0, 1.0, 0.0], "space": "world",
                "alongNormal": False},
               # ... with a crisp crest on the nodal line itself (the narrow reading of the same sand)
               {"kind": "field", "field": "line", "amount": 0.55, "axis": [0.0, 1.0, 0.0], "space": "world",
                "alongNormal": False},
               # the vibration itself: the hollows breathe (the standing wave's own motion)
               {"kind": "field", "field": FIELD, "amount": 0.3, "axis": [0.0, 1.0, 0.0], "space": "world",
                "alongNormal": False},
           ])
    return g


def camera(s):
    pos, tgt = [6.0, 17.0, 66.0], [0.0, 1.0, 0.0]
    s.camera = {"mode": 1, "position": pos, "target": tgt, "fov": 40.0, "orbitSpeed": 0.0}
    s.params_({"camera/lens/focalLength": 28.0})


def instrument(s, g):
    """The performer's surface. The field reads the audio history itself (ADR-1400, response-shaped by ADR-1361);
    these routes add the Response controls' hits and levels where the field's own excitation is not enough."""
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}


DESIGN = {
    "category": "abstract",
    "thesis": "A night basin of basalt whose ground is a resonating plate: the music excites one field of standing "
              "waves, the grit settles on its nodal lines, and the world's forms each answer the same field.",
    "composition": {
        "background": "a ring of far mountains under an indigo sky",
        "midground": "the basin's terraced rim",
        "foreground": "the basin floor, its sand relief drawing the figure",
        "focal": "the basin's centre",
        "camera": "an elevated three-quarter view from the rim's gap",
    },
    "palette": {"dominant": P["stone"], "secondary": P["sand"], "accent": P["sandHot"], "highlight": P["hollow"],
                "background_value": "near black", "saturation": "low, with selective cyan and violet"},
    "tier": "one field, three consumers",
}


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    environment(s)
    fields(s)
    g = ground(s)
    camera(s)
    instrument(s, g)
    s.params_({
        "post/bloom/intensity": 0.18, "post/bloom/threshold": 1.0, "post/output/vignette": 0.45,
        "post/output/grain": 0.012, "post/grade/contrast": 1.08, "post/lens/chromaticAberration": 0.0,
        "camera/exposure/compensation": 0.9, "post/dof/enabled": False
    })
    return s


def write(s, out_dir=OUT):
    pp, sp = s.write(out_dir)
    return pp, sp


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", help="also write a file project driven by this WAV")
    ap.add_argument("--out", help="the variant's path")
    ap.add_argument("--prototype", default="all")
    ap.add_argument("--camera", help="x,y,z:tx,ty,tz for the variant")
    ap.add_argument("--set", action="append", default=[], help="path=json value, for the variant")
    a = ap.parse_args()
    global PROTOTYPE
    PROTOTYPE = a.prototype
    s = build()
    pp, sp = write(s)
    print(pp)
    if a.variant:
        params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
        print(variant(pp, a.variant, a.out, a.camera, params))


if __name__ == "__main__":
    main()

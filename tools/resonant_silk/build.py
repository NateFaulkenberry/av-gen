#!/usr/bin/env python3
"""RESONANT SILK: the generator (docs/prototypes/resonant-silk/).

    python3 tools/resonant_silk/build.py                         # writes examples/resonant-silk/ (scene, project, mesh)
    python3 tools/resonant_silk/build.py --composition a|b|c     # a research-board composition (written to --out-dir)
    python3 tools/resonant_silk/build.py --variant --out <v.json> [--camera x,y,z:tx,ty,tz] [--set path=json]

One continuous sheet of silk, gathered at invisible anchors and spreading broad between them, is authored here as a
static mesh (the sculpture: centreline, transported frame, width, billow and pleats). Everything that moves it is on the
GPU: slow world-space deformers evolve the silhouette, and the music excites it through the engine's own fields -- the
resonance field (ADR-1400) for persistent modes, a travelling Spectrum field (ADR-1116) for the bass swell along the
length, Onset fields (ADR-1116) for wavefronts.
"""
from __future__ import annotations

import argparse
import copy
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

ID = "resonant-silk"
TITLE = "Resonant Silk"
OUT = os.path.join(REPO, "examples", "resonant-silk")
CATEGORY = "Resonant Silk"

COMPOSITION = "a"
MUSIC = not os.environ.get("RS_NOMUSIC")
SMOOTH = float(os.environ.get("RS_SMOOTH", "0.05"))   # spectrum fields' smoothing window (s); 32 taps at most
MESH_SCALE = float(os.environ.get("RS_MESH", "1.0"))  # look development: the sheet's tessellation
SAIL = float(os.environ.get("RS_SAIL", "-0.8"))  # look development: the sail's extra roll

# ================================================================================================ palette (sRGB refs)
P = {
    "abyss": "#080B20",
    "indigo": "#171D55",
    "cyan": "#39E6F2",
    "teal": "#48CDBB",
    "violet": "#7857F5",
    "magenta": "#F04CCF",
    "pearl": "#D9F7FF",
}


def C(key, scale=1.0):
    return hexrgb(P[key], scale)


# ================================================================================================ the palettes
# The owner's eight Silk palettes (03-brief-palettes.md), sRGB references, seven roles each. Converted to linear light
# here; the engine's project palette (ADR-1043) blends them in OKLab and its selector (ADR-1420) chooses, locks and
# advances them. Roles land on geometry and light, never as a painted gradient:
#   environment  the void: the sky dome's zenith and ground, the fog
#   shadow       what the unlit side and the horizon sink to: the fill light, the sky's horizon band
#   base         the silk's albedo where it faces the eye
#   fold         its albedo where it turns away (the pleats' flanks, a fold's inner wall: Fresnel-masked)
#   resonant     light the music puts on it: the counter light, the fronts, the hardest-ringing folds
#   grazing      the key light (the satin highlights that slide along the pleats) and the hems
#   peak         narrow, rare: the gather's seam, the treble's sheen on the folds' turning edges
PALETTES = [
    # name, environment, shadow, base, fold, resonant, grazing, peak, tuning
    ("Midnight Aurora", "#070711", "#15152C", "#29264F", "#51477F", "#8F87D5", "#C5C4F3", "#F0EDFF",
     {"rough": 0.22, "film": 1.0, "key": 5.0, "emit": 1.0, "fold": 0.75, "irid": 0.02, "filmIor": 1.7}),
    ("Garnet Veil", "#09050C", "#1C0D20", "#3B1735", "#712447", "#B83A60", "#F06B83", "#FFC0CB",
     {"rough": 0.19, "film": 0.9, "key": 4.6, "emit": 0.9, "fold": 0.9, "irid": 0.0, "filmIor": 1.5}),
    ("Liquid Champagne", "#0C0908", "#261C17", "#57402F", "#8A6544", "#C69B65", "#F0D5A0", "#FFF4D8",
     {"rough": 0.26, "film": 0.8, "key": 3.6, "emit": 0.7, "fold": 0.65, "irid": 0.0, "filmIor": 1.5}),
    ("Opal Dream", "#0B0710", "#241827", "#51364F", "#845B79", "#C58AA8", "#E9C4D8", "#FFF0F3",
     {"rough": 0.23, "film": 1.25, "key": 3.8, "emit": 0.8, "fold": 0.6, "irid": 0.18, "filmIor": 2.0}),
    ("Deep Emerald Satin", "#050A08", "#10221A", "#1C4535", "#307456", "#4BA783", "#91D6B0", "#DFFFEA",
     {"rough": 0.20, "film": 0.9, "key": 4.4, "emit": 0.8, "fold": 0.8, "irid": 0.0, "filmIor": 1.5}),
    ("Electric Cobalt", "#050711", "#10162D", "#1B3470", "#2856A0", "#478CE0", "#8DC7FF", "#E2F3FF",
     {"rough": 0.17, "film": 1.0, "key": 5.2, "emit": 1.1, "fold": 0.85, "irid": 0.02, "filmIor": 1.6}),
    ("Burnished Copper", "#0B0707", "#261411", "#51251D", "#843C2B", "#C56343", "#F0A071", "#FFD9BA",
     {"rough": 0.24, "film": 0.8, "key": 4.0, "emit": 0.7, "fold": 0.8, "irid": 0.0, "filmIor": 1.5}),
    ("Oil-Slick Iridescence", "#05050A", "#121020", "#25203C", "#49304F", "#80405D", "#C46C83", "#F2C4B6",
     {"rough": 0.16, "film": 0.95, "key": 4.6, "emit": 0.9, "fold": 0.9, "irid": 0.45, "filmIor": 2.4}),
]
ROLES = ("environment", "shadow", "base", "fold", "resonant", "grazing", "peak")
# The automatic journey (ADR-1420 advance order): neighbours share a temperature, so a phrase change turns the material
# rather than jumping across the wheel -- Aurora, Cobalt, Emerald, Champagne, Copper, Garnet, Oil-Slick, Opal.
JOURNEY = [0, 5, 4, 2, 6, 1, 7, 3]
START_PALETTE = int(os.environ.get("RS_PALETTE", "0"))


def palette_states():
    out = []
    for row in PALETTES:
        name, cols, tune = row[0], row[1:8], row[8]
        out.append({"name": name, "colors": {r: hexrgb(h) for r, h in zip(ROLES, cols)},
                    "scalars": {k: float(v) for k, v in tune.items()}})
    return out


# ================================================================================================ curve helpers
def catmull(points, n):
    """A centripetal Catmull-Rom curve through `points` (k x 3), sampled at n points evenly in parameter."""
    pts = np.asarray(points, dtype=np.float64)
    ext = np.vstack([2 * pts[0] - pts[1], pts, 2 * pts[-1] - pts[-2]])
    segs = len(pts) - 1
    out = []
    for i in range(n):
        s = i / (n - 1) * segs
        k = min(int(s), segs - 1)
        t = s - k
        p0, p1, p2, p3 = ext[k], ext[k + 1], ext[k + 2], ext[k + 3]

        def tj(ti, a, b):
            return ti + max(np.linalg.norm(b - a), 1e-6) ** 0.5
        t0 = 0.0
        t1 = tj(t0, p0, p1)
        t2 = tj(t1, p1, p2)
        t3 = tj(t2, p2, p3)
        tt = t1 + (t2 - t1) * t
        a1 = (t1 - tt) / (t1 - t0) * p0 + (tt - t0) / (t1 - t0) * p1
        a2 = (t2 - tt) / (t2 - t1) * p1 + (tt - t1) / (t2 - t1) * p2
        a3 = (t3 - tt) / (t3 - t2) * p2 + (tt - t2) / (t3 - t2) * p3
        b1 = (t2 - tt) / (t2 - t0) * a1 + (tt - t0) / (t2 - t0) * a2
        b2 = (t3 - tt) / (t3 - t1) * a2 + (tt - t1) / (t3 - t1) * a3
        out.append((t2 - tt) / (t2 - t1) * b1 + (tt - t1) / (t2 - t1) * b2)
    return np.array(out)


def resample_arclength(curve, n):
    d = np.r_[0.0, np.cumsum(np.linalg.norm(np.diff(curve, axis=0), axis=1))]
    s = np.linspace(0.0, d[-1], n)
    return np.stack([np.interp(s, d, curve[:, k]) for k in range(3)], axis=1), d[-1]


def smooth_profile(knots, u):
    """A smooth (cosine-eased) piecewise profile through (u_k, value_k) knots."""
    ks = np.array([k[0] for k in knots])
    vs = np.array([k[1] for k in knots])
    out = np.empty_like(u)
    for i, x in enumerate(u):
        j = np.searchsorted(ks, x) - 1
        j = int(np.clip(j, 0, len(ks) - 2))
        t = np.clip((x - ks[j]) / (ks[j + 1] - ks[j]), 0.0, 1.0)
        t = 0.5 - 0.5 * math.cos(math.pi * t)
        out[i] = vs[j] + (vs[j + 1] - vs[j]) * t
    return out


def transported_frame(c):
    """Tangent and a parallel-transported normal along the polyline c (no sudden flips)."""
    t = np.gradient(c, axis=0)
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    ref = np.array([0.0, 1.0, 0.0]) if abs(t[0][1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    n0 = ref - t[0] * np.dot(ref, t[0])
    n0 /= np.linalg.norm(n0)
    ns = [n0]
    for i in range(1, len(c)):
        n = ns[-1] - t[i] * np.dot(ns[-1], t[i])
        n /= np.linalg.norm(n)
        ns.append(n)
    n = np.array(ns)
    b = np.cross(t, n)
    return t, n, b


# ================================================================================================ the sheet
class SheetSpec:
    """One sheet: centreline control points, roll (radians) along u, span width along u (the distance the cloth is
    stretched across), the cloth's own width (the excess gathers into pleats), billow and pleat counts."""

    def __init__(self, name, points, roll, span, cloth, billow, pleats, nu=520, nv=112, pleat_wander=0.6, seed=1,
                 curl=0.0, edge_wave=0.0, end_round=0.0):
        self.name, self.points, self.roll, self.span, self.cloth = name, points, roll, span, cloth
        self.billow, self.pleats, self.nu, self.nv = billow, pleats, nu, nv
        self.pleat_wander, self.seed, self.curl, self.edge_wave = pleat_wander, seed, curl, edge_wave
        self.end_round = end_round


def sheet_mesh(spec):
    spec.nu, spec.nv = int(round(spec.nu * MESH_SCALE)), int(round(spec.nv * MESH_SCALE))
    """P(u, v) = C(u) + v w(u) W(u) + N(u) D(u, v): a centreline, a transported frame turned by an authored roll, a
    span profile, and a displacement along the frame normal made of a billow (the sail between two anchors) and pleats
    (the cloth the span cannot hold, gathered into folds that fan out from the anchors)."""
    nu, nv = spec.nu, spec.nv
    curve = catmull(spec.points, 4000)
    c, length = resample_arclength(curve, nu)
    u = np.linspace(0.0, 1.0, nu)
    v = np.linspace(-1.0, 1.0, nv)
    t, n, b = transported_frame(c)
    roll = smooth_profile(spec.roll, u)
    cr, sr = np.cos(roll)[:, None], np.sin(roll)[:, None]
    W = b * cr + n * sr          # the width direction
    N = -b * sr + n * cr         # the sheet normal (before displacement)
    span = smooth_profile(spec.span, u)          # half width, metres
    cloth = smooth_profile(spec.cloth, u)        # the cloth's half width, metres (>= span)
    billow = smooth_profile(spec.billow, u)      # metres at the centre of the span
    excess = np.maximum(cloth - span, 0.0)
    rng = np.random.default_rng(spec.seed)
    V = v[None, :]
    U = u[:, None]
    # pleats: a few families of folds across the width, their phase wandering slowly along u so the folds fan, merge
    # and part instead of running as parallel corrugation; depth from the excess cloth (deep where gathered)
    D = billow[:, None] * (1.0 - V * V)
    for k, (count, share) in enumerate(spec.pleats):
        ph0 = rng.random() * 2 * math.pi
        w1, w2 = 0.7 + 1.3 * rng.random(), 1.9 + 1.7 * rng.random()
        wander = spec.pleat_wander * (np.sin(2 * math.pi * w1 * U + ph0) + 0.5 * np.sin(2 * math.pi * w2 * U + 2 * ph0))
        amp = share * np.sqrt(excess * span + 1e-6)[:, None] / count * 1.6
        # pleats die out toward the free edges a little (the edge is held by its own hem) and are strongest mid-span
        env = 0.55 + 0.45 * np.cos(0.5 * math.pi * V) ** 2
        D = D + amp * env * np.cos(count * math.pi * 0.5 * (V + 1.0) + wander)
    if spec.curl:
        D = D + spec.curl * span[:, None] * V ** 3
    pos = c[:, None, :] + (V * span[:, None])[..., None] * W[:, None, :] + D[..., None] * N[:, None, :]
    if spec.edge_wave:
        # the free edges ripple along their length (a hem that is longer than the span)
        ew = spec.edge_wave * np.abs(V) ** 4 * np.sin(2 * math.pi * 9.0 * U + 3.0 * V) * (1.0 + 3.0 * np.clip(
            (U - 0.7) / 0.3, 0.0, 1.0))
        pos = pos + (ew * span[:, None])[..., None] * N[:, None, :]
    if spec.end_round:
        # the free end is cloth, not a cut: its hem is an arc (the corners fall back along u) that flutters
        ramp = np.clip((U - 0.8) / 0.2, 0.0, 1.0) ** 2
        flutter = 1.0 + 0.35 * np.sin(5.0 * math.pi * V + 1.3) * 0.5
        u2 = U * (1.0 - spec.end_round * ramp * (V * V) * flutter)
        u2 = np.broadcast_to(u2, (nu, nv))
        for j in range(nv):
            for k in range(3):
                pos[:, j, k] = np.interp(u2[:, j], u, pos[:, j, k])
    pos = pos.reshape(-1, 3)
    uv = np.stack(np.meshgrid(u, 0.5 * (v + 1.0), indexing="ij"), axis=-1).reshape(-1, 2)
    idx = []
    for i in range(nu - 1):
        r0, r1 = i * nv, (i + 1) * nv
        a = np.arange(nv - 1)
        q = np.stack([r0 + a, r1 + a, r1 + a + 1, r0 + a, r1 + a + 1, r0 + a + 1], axis=1)
        idx.append(q.reshape(-1))
    idx = np.concatenate(idx).astype(np.uint32)
    tri = idx.reshape(-1, 3)
    fn = np.cross(pos[tri[:, 1]] - pos[tri[:, 0]], pos[tri[:, 2]] - pos[tri[:, 0]])
    nrm = np.zeros_like(pos)
    for k in range(3):
        np.add.at(nrm, tri[:, k], fn)
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-12
    return pos.astype(np.float32), nrm.astype(np.float32), uv.astype(np.float32), idx, length


def write_glb(path, pos, nrm, uv, idx, name):
    pb, nb, ub, ib = pos.tobytes(), nrm.tobytes(), uv.tobytes(), idx.tobytes()
    blob = pb + nb + ub + ib
    blob += b"\0" * ((4 - len(blob) % 4) % 4)
    lo, hi = pos.min(axis=0).tolist(), pos.max(axis=0).tolist()
    o1, o2, o3 = len(pb), len(pb) + len(nb), len(pb) + len(nb) + len(ub)
    gltf = {
        "asset": {"version": "2.0", "generator": "resonant_silk/build.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "materials": [{"name": name, "doubleSided": True,
                       "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0.0,
                                                "roughnessFactor": 0.4}}],
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


# ================================================================================================ compositions
def sheets_for(comp):
    """The research board's compositions. Metres; the camera sits near +z looking toward -z."""
    if comp == "a":
        # THE GATHER: one sheet enters as a broad wall of pleats from the left foreground, the pleats running away
        # from the camera and converging on an invisible ring right of centre (the focal gather); beyond it the same
        # cloth opens again into a tall sail that rises, billows and turns over into the dark above and behind. Not
        # mirrored: a long low wall on one side of the gather, a tall turning sail on the other.
        main = SheetSpec(
            "silk",
            points=[(-84, 10, 4), (-48, 4, -28), (-24, 0, -66), (-10, 2, -115), (0, 4, -170), (18, 10, -174),
                    (42, 20, -164), (68, 34, -146), (90, 52, -134), (102, 74, -138)],
            roll=[(0.0, 1.45), (0.3, 1.65), (0.62, 2.1), (0.7, 2.6 + SAIL), (0.84, 3.2 + SAIL), (1.0, 3.5 + SAIL)],
            span=[(0.0, 30.0), (0.3, 26.0), (0.52, 10.0), (0.62, 2.5), (0.68, 12.0), (0.84, 48.0), (0.95, 58.0), (1.0, 50.0)],
            cloth=[(0.0, 40.0), (0.52, 44.0), (0.66, 30.0), (0.84, 60.0), (0.95, 72.0), (1.0, 64.0)],
            billow=[(0.0, 6.0), (0.33, 9.0), (0.56, 0.0), (0.64, 0.0), (0.82, -6.0), (1.0, -3.0)],
            pleats=[(6.0, 0.8), (13.0, 0.35), (17.0, 0.25)], seed=5, curl=0.06, edge_wave=0.035, nu=470, nv=110,
            end_round=0.4)
        cam = {"position": [4.0, 2.0, 60.0], "target": [-16.0, 8.0, -60.0], "focal": 26.0, "drift": True}
        return [main], cam
    if comp == "b":
        # THE PASSAGE: two broad sheets bow toward each other and form a curved corridor the eye travels down; they
        # touch far away, where the gathered crease is the focal light.
        left = SheetSpec(
            "silk",
            points=[(-60, 10, 40), (-34, 4, 0), (-20, 0, -50), (-10, 2, -110), (0, 4, -170)],
            roll=[(0.0, 1.45), (0.5, 1.75), (1.0, 2.2)],
            span=[(0.0, 30.0), (0.5, 26.0), (0.85, 10.0), (1.0, 4.0)],
            cloth=[(0.0, 40.0), (1.0, 34.0)],
            billow=[(0.0, 6.0), (0.5, 9.0), (1.0, 0.0)],
            pleats=[(6.0, 0.8), (13.0, 0.25)], seed=5, curl=0.1, edge_wave=0.03)
        right = SheetSpec(
            "silk2",
            points=[(60, -6, 30), (36, -2, -10), (20, 2, -60), (8, 4, -115), (0, 4, -170)],
            roll=[(0.0, -1.5), (0.5, -1.8), (1.0, -2.3)],
            span=[(0.0, 28.0), (0.5, 24.0), (0.85, 9.0), (1.0, 4.0)],
            cloth=[(0.0, 36.0), (1.0, 32.0)],
            billow=[(0.0, -6.0), (0.5, -8.0), (1.0, 0.0)],
            pleats=[(5.0, 0.8), (12.0, 0.25)], seed=9, curl=-0.1, edge_wave=0.03)
        cam = {"position": [0.0, 2.0, 60.0], "target": [0.0, 3.0, -60.0], "focal": 26.0}
        return [left, right], cam
    # c -- THE CANOPY: one vast sheet overhead seen from below and to the side, gathered at a single anchor high in
    # the upper left and spreading as a sail whose free hem falls and curls into the dark at the lower right.
    canopy = SheetSpec(
        "silk",
        points=[(-60, 40, -40), (-30, 26, -50), (0, 14, -55), (30, 2, -60), (60, -16, -70), (80, -40, -90)],
        roll=[(0.0, 0.2), (0.5, 0.6), (1.0, 1.1)],
        span=[(0.0, 3.0), (0.25, 24.0), (0.6, 44.0), (1.0, 50.0)],
        cloth=[(0.0, 50.0), (1.0, 56.0)],
        billow=[(0.0, 0.0), (0.4, 10.0), (0.8, 14.0), (1.0, 6.0)],
        pleats=[(7.0, 0.8), (15.0, 0.25)], seed=21, pleat_wander=0.9, curl=-0.25, edge_wave=0.05, nv=140)
    cam = {"position": [-10.0, -18.0, 40.0], "target": [8.0, 6.0, -55.0], "focal": 24.0}
    return [canopy], cam



class Prog:
    """A material program assembled by label (Cymatic World's pattern): `op(label, ...)` appends one op and remembers
    its 1-based number, which is what a route's `material/<name>/op/<n>/...` path addresses."""

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

    def doc(self, base=-1, emission=-1, roughness=-1, metallic=-1):
        return {"name": self.name, "ops": self.ops, "baseColor": base, "metallic": metallic, "roughness": roughness,
                "emission": emission, "emissionIntensity": 1.0, "opacity": -1}


def silk_program():
    """The silk's colour, on the lit path: the lights draw the satin highlights (the key in the palette's grazing
    colour, the counter light in its resonant colour); the program sets what they land on and adds the little light
    the sheet gives off. Every colour constant here is a palette role (bound in `palette()`), every gain a route's.
    - Albedo: base facing the eye, fold where the cloth turns away (Fresnel, so the pleats' flanks and the folds'
      inner walls carry it); bass broadens that band (the exponent), low mids deepen the separation (the mix).
    - Hems: a thin line of the grazing colour on the free edges, so the silhouette draws itself in the void.
    - The gather: a narrow seam of peak light at the ring (a few metres, not a blob).
    - Music: treble as a fine peak sheen on the folds' turning edges; a kick's front as a travelling band of resonant
      light; the hardest-ringing folds (resonance amplitude) in the resonant colour."""
    g = Prog("rsSilk")
    g.op(None, "input", 0, inp="uv")                                   # r0 = (u, v)
    g.op(None, "swizzle", 1, a=0, k=[1, 1, 1, 1])                      # r1 = v
    g.op("hem", "palette", 1, a=1, k=[0.5, 0.5, 0.5, 0], k2=[0.5, 0.5, 0.5, 0], k3=[1, 1, 1, 0], k4=[0, 0, 0, 0])
    g.op("hemWidth", "power", 1, a=1, f=500.0)                          # r1: 1 on the hems, falling fast inward
    g.op("hemCol", "constant", 3, k=C("cyan", 0.9) + [1])              # role: grazing (x 0.9, x palette emit)
    g.op(None, "multiply", 1, a=1, b=3)                                # r1: emission (the hems)
    g.op("foldMask", "fresnel", 4, f=2.6)                               # r4: turned away (bass lowers the exponent)
    g.op("body", "constant", 5, k=C("indigo") + [1])                    # role: base
    g.op("foldCol", "constant", 6, k=C("violet", 0.4) + [1])            # role: fold
    g.op("foldMix", "constant", 7, k=[0.75, 0.75, 0.75, 1])             # x palette fold, low mids
    g.op(None, "multiply", 7, a=4, b=7)
    g.op(None, "mixBy", 5, a=5, b=6, c=7)                              # r5: albedo
    # view-dependent colour (the palette's iridescence): a thin spectral sheen that cycles with the grazing angle,
    # only where the cloth turns away -- strong in Oil-Slick, a pastel shimmer in Opal, a trace elsewhere
    g.op("spectrum", "palette", 6, a=4, k=[0.5, 0.38, 0.5, 0], k2=[0.5, 0.3, 0.5, 0], k3=[1.4, 1.4, 1.4, 0],
         k4=[0.0, 0.33, 0.67, 0])
    g.op(None, "multiply", 6, a=6, b=4)
    g.op(None, "multiply", 6, a=6, b=4)                                # only near true grazing
    g.op("irid", "constant", 7, k=[0.6, 0.6, 0.6, 1])                   # x palette irid
    g.op(None, "multiply", 6, a=6, b=7)
    g.op(None, "add", 1, a=1, b=6)                                     # + the sheen (emission)
    # the gather's seam: peak light within a few metres of the ring
    g.op(None, "input", 6, inp="worldPosition")
    g.op(None, "constant", 7, k=[-GATHER[0], -GATHER[1], -GATHER[2], 0.0])
    g.op(None, "add", 6, a=6, b=7)
    g.op(None, "multiply", 6, a=6, b=6)
    g.op(None, "gradient", 6, a=6, k=[1.0, 1.0, 1.0, 0.0], f=1.0 / (9.0 * 9.0))     # |d|^2 / R^2, saturated
    g.op(None, "remap", 6, a=6, k=[0.0, 1.0, 1.0, 0.0], f=1.0)                     # 1 at the ring, 0 at 9 m
    g.op("glowCol", "constant", 7, k=C("pearl", 0.5) + [1])            # role: peak (x 0.5, x palette emit)
    g.op(None, "multiply", 6, a=6, b=7)
    g.op(None, "add", 1, a=1, b=6)
    if MUSIC and not os.environ.get("RS_NOMATMUSIC"):
        g.op(None, "power", 6, a=4, f=2.5)                             # r6: the folds' turning edges (narrow)
        # treble as a uniform (the analysis' treble band): a per-pixel field read cost 16 ms with the others at 1080p
        g.op("treble", "input", 2, inp="audio")
        g.op(None, "swizzle", 2, a=2, k=[3, 3, 3, 3])
        g.op(None, "multiply", 2, a=2, b=6)
        g.op("trebleCol", "constant", 3, k=C("pearl") + [1])           # role: peak
        g.op(None, "multiply", 2, a=2, b=3)
        g.op("trebleGain", "constant", 3, k=[0.5, 0.5, 0.5, 1])        # x Light Response
        g.op(None, "multiply", 2, a=2, b=3)
        g.op(None, "add", 1, a=1, b=2)
        # resonant light: a kick's front travelling out across the cloth
        g.op("kick", "field", 2, field="kickFront")
        g.op(None, "multiply", 2, a=2, b=4)                            # on the turning faces only
        g.op("resCol", "constant", 3, k=C("cyan") + [1])               # role: resonant
        g.op(None, "multiply", 2, a=2, b=3)
        g.op("resGain", "constant", 3, k=[0.4, 0.4, 0.4, 1])           # x Light Response
        g.op(None, "multiply", 2, a=2, b=3)
        g.op(None, "add", 1, a=1, b=2)
    g.base, g.emit = 5, 1
    return g


def palette_block(g):
    """The project palette (ADR-1043) with its selector (ADR-1420): the eight states, the role bindings, and the
    automatic journey on phrase boundaries (at most one change in a `hold`)."""
    def op(label, field="constant"):
        return "material/%s/op/%d/%s/%s" % (g.name, g[label], g.ops[g[label] - 1]["kind"], field)
    b = [
        {"role": "environment", "target": "env/sky/zenithColor", "gain": 0.6},
        {"role": "environment", "target": "env/sky/groundColor", "gain": 0.7},
        {"role": "environment", "target": "env/sky/horizonColor", "gain": 1.0},
        {"role": "environment", "target": "scene/fogColor", "gain": 1.0},
        {"role": "shadow", "target": "lights/fill/color"},
        {"role": "base", "target": op("body")},
        {"role": "fold", "target": op("foldCol")},
        {"role": "grazing", "target": "lights/key/color"},
        {"role": "grazing", "target": op("hemCol"), "gain": 0.9},
        {"role": "resonant", "target": "lights/rim/color"},
        {"role": "peak", "target": op("glowCol"), "gain": 0.5},
        # tuning: highlight width (roughness), iridescence (the film), highlight intensity, emission, fold contrast
        {"role": "rough", "target": "procedural/silk/material/roughness"},
        {"role": "film", "target": "procedural/silk/material/thinFilm/thickness", "mode": "multiply"},
        {"role": "key", "target": "lights/key/intensity"},
        {"role": "emit", "target": op("hemCol"), "mode": "multiply"},
        {"role": "irid", "target": op("irid"), "mode": "multiply"},
        {"role": "filmIor", "target": "procedural/silk/material/thinFilm/ior"},
        {"role": "resonant", "target": "lights/sail/color"},
        {"role": "grazing", "target": "lights/sailLow/color"},
        {"role": "emit", "target": op("glowCol"), "mode": "multiply"},
        {"role": "fold", "target": op("foldMix"), "mode": "multiply", "component": 0},
    ]
    if MUSIC and not os.environ.get("RS_NOMATMUSIC"):
        b += [{"role": "peak", "target": op("trebleCol")},
              {"role": "resonant", "target": op("resCol")}]
    # 'fold' is both a colour role and a scalar role name in the tuning: the scalar is renamed to keep them apart
    states = palette_states()
    for st in states:
        st["scalars"]["foldContrast"] = st["scalars"].pop("fold")
    for x in b:
        if x["target"] == op("foldMix"):
            x["role"] = "foldContrast"
            x.pop("component", None)
    return {"states": states, "bindings": b, "position": 0.0, "saturation": 1.0, "value": 1.0,
            "selector": {"select": START_PALETTE, "transition": 6.0, "lock": False, "hold": 40.0,
                         "advance": {"signal": "beat.phrasePulse", "threshold": 0.5, "order": JOURNEY}}}


# ================================================================================================ the music's fields
GATHER = (0.0, 4.0, -170.0)     # the invisible ring: where everything converges, and where every wave starts
WALL_MID = (-22.0, 3.0, -80.0)  # the middle of the wall (where the snare's fronts start)
NEAR_CALM = (-70.0, 6.0, 4.0)  # the wall's near end, by the camera: the music's displacement is calm here
FIELD = "resonator"
WALL_RUN = [-0.435, 0.03, 0.90]   # from the gather back along the wall, toward the camera
SAIL_RUN = [0.81, 0.57, 0.13]    # from the gather out along the sail
WALL_N = [-0.96, -0.094, -0.263] # the wall's mean normal (of the unpleated sheet)
SAIL_N = [0.536, -0.244, -0.809] # the sail's


def fields(s):
    """Everything that hears the music: one resonator (ADR-1400), one travelling bass swell (ADR-1116 spectrum x a
    carrier wave), onset fronts (ADR-1116), a treble trace for the light.

    - The resonator: a point source behind the wall owns the low modes (40-~400 Hz): broad bows whose rings cross
      the sheet as long arcs, building under held notes and ringing down; a second point source below the sheet owns
      the middle band: curved creases across the pleats. Fixed layout and per-band response (ADR-1404).
    - The swell: the bass of the last seconds laid out outward from the gather at `audioSpeed` (what was loud two
      seconds ago is 50 m out), times a carrier of 90 m wavelength moving with it: a kick or a bass note sends a bow
      out along both fans over several beats.
    - Fronts: a kick launches a broad slow front from the gather; a snare a quicker, narrower one from the wall.

    The cloth moves as cloth: every displacement is along the sheet's own mean normal (the wall's, or the sail's),
    never along the pleated surface's normal (which pushes a pleat's two faces apart and inflates it into pillows).
    The two halves are told apart by plane masks through the gather, so the ring itself stays still."""
    res = {
        "modes": float(os.environ.get("RS_MODES", "8")), "layout": float(os.environ.get("RS_LAYOUT", "12")), "fLow": 40.0, "fHigh": 4000.0, "lambdaMax": 220.0, "lambdaMin": 18.0,
        "selectivity": 2.5, "contrast": 0.5, "memory": 2.0, "competition": 1.5, "sustain": 1.0, "transient": 0.4,
        "idle": 0.5, "breath": 0.1, "limit": 1.0,
        "attack": [0.8, 0.3, 0.12], "release": [4.0, 1.8, 0.7],
        "sources": [
            {"kind": "point", "position": [-80.0, -10.0, -110.0], "order": 2, "bandLow": 0.0, "bandHigh": 0.5,
             "gain": 1.0, "amplitude": 1.0},
            {"kind": "point", "position": [20.0, -40.0, -70.0], "order": 3, "bandLow": 0.5, "bandHigh": 1.0,
             "gain": 1.0, "amplitude": 0.16, "drift": 14.0, "driftPeriod": 40.0, "spin": 0.05},
        ],
        "channel": "displacement",
    }
    s.nodes.append({"name": FIELD, "kind": "field", "position": [0, 0, 0],
                    "field": {"kind": "resonance", "seed": 17, "strength": 1.0, "falloff": {"kind": "none"},
                              "resonance": res}})

    def fnode(name, field, position=(0.0, 0.0, 0.0)):
        f = {"strength": 1.0, "falloff": {"kind": "none"}}
        f.update(field)
        s.nodes.append({"name": name, "kind": "field", "position": list(position), "field": f})

    # the halves: 0 at the gather, 1 thirty metres out along each fan
    fnode("wallSide", {"kind": "plane", "axis": WALL_RUN, "softness": 35.0}, GATHER)
    fnode("sailSide", {"kind": "plane", "axis": SAIL_RUN, "softness": 30.0}, GATHER)
    # the near wall keeps its pleats crisp: the music's displacement fades out over the 110 m nearest the camera
    # (displacement there, along the wall's mean normal, slides the cloth within itself and smears the pleats)
    fnode("nearCalm", {"kind": "distance", "point": [0.0, 0.0, 0.0], "radius": 110.0}, NEAR_CALM)
    fnode("bassHistory", {"kind": "spectrum", "audioBand": "range", "bandLow": 0.0, "bandHigh": 0.22,
                          "audioSpeed": 26.0, "audioSmooth": SMOOTH, "point": list(GATHER),
                          "waveGeometry": "spherical"})
    fnode("bassCarrier", {"kind": "wave", "waveGeometry": "spherical", "waveShape": "sine", "point": list(GATHER),
                          "wavelength": 90.0, "waveSpeed": 26.0, "waveWidth": 0.0, "amplitude": 1.0})
    fnode("kickFront", {"kind": "onset", "onsetSource": "low", "point": list(GATHER), "waveGeometry": "spherical",
                        "audioSpeed": 34.0, "onsetWidth": 14.0, "onsetDecay": 0.9})
    fnode("snareFront", {"kind": "onset", "onsetSource": "mid", "point": list(WALL_MID),
                         "waveGeometry": "spherical", "audioSpeed": 55.0, "onsetWidth": 5.0, "onsetDecay": 2.2})
    for side in ("wall", "sail"):
        mask = side + "Side"
        calm = ["nearCalm"] if side == "wall" else []
        fnode(side + "Res", {"kind": "compound", "combine": "multiply", "children": [FIELD, mask] + calm})
        fnode(side + "Kick", {"kind": "compound", "combine": "multiply", "children": ["kickFront", mask] + calm})
    # the swell runs along the wall only (the sail answers the gather's fronts and its own modes)
    fnode("wallSwell", {"kind": "compound", "combine": "multiply", "children": ["bassHistory", "bassCarrier",
                                                                               "wallSide", "nearCalm"]})


def music_deformers():
    """The music's displacement, along each half's mean normal (amounts in metres per unit of field)."""
    def fd(field, amount, axis):
        return {"kind": "field", "field": field, "amount": amount, "space": "world", "alongNormal": False,
                "axis": axis}
    return [fd("wallRes", 6.0, WALL_N), fd("sailRes", 6.0, SAIL_N),
            fd("wallSwell", 3.0, WALL_N),
            fd("wallKick", 3.0, WALL_N), fd("sailKick", 3.5, SAIL_N),
            ] + ([] if os.environ.get("RS_NOSNARE") else [fd("snareFront", 0.9, WALL_N)])

# ================================================================================================ world
def environment(s):
    s.environment = {
        "intensity": 0.04, "background": C("abyss", 0.6), "fogColor": C("abyss", 0.8),
        "volumeDensity": 0.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0,
        "shadowRange": 200.0,
        # no screen-space AO or contact shadows: a thin sheet in a void gains nothing from them, and either would ask
        # for a depth prepass that re-runs every deformer per vertex (ADR-1406; 39 ms measured)
        "ambientOcclusion": False, "contactShadows": False,
        "sky": {"enabled": True, "zenithColor": C("abyss", 0.3), "horizonColor": C("indigo", 0.22),
                "groundColor": C("indigo", 0.22), "haze": 1.0, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    # a narrow, cold, grazing key from high behind-right: strips of light across the folds
    s.light("key", "directional", direction=[-0.55, -0.35, 0.75], color=kit.mix(C("pearl"), C("cyan"), 0.75),
            intensity=5.0,
            castsShadow=False, contactShadow=False, softness=0.2)
    # a violet counter-light from the lower left, behind the sheet: rims and the far side's colour
    s.light("rim", "directional", direction=[0.6, 0.45, 0.65], color=C("violet"), intensity=2.2,
            castsShadow=False, contactShadow=False)
    # the sail's grazing lamp: high above the gather, almost in the sail's plane, so its billows and hem catch strips
    # of light (the wall's top edge is out of its range)
    s.light("sail", "point", position=[7.0, 115.0, -174.0], color=C("violet"), intensity=45000.0, range=160.0,
            castsShadow=False)
    # and a second, low on the other side of its plane: the billows' undersides and the lower hem
    s.light("sailLow", "point", position=[110.0, -25.0, -80.0], color=C("cyan"), intensity=30000.0, range=170.0,
            castsShadow=False)
    # broad cool fill from the camera side, low
    s.light("fill", "directional", direction=[0.1, -0.2, -1.0], color=C("teal"), intensity=0.6,
            castsShadow=False, contactShadow=False)


def silk_material(name, program="rsSilk"):
    return {"program": program, "baseColor": C("indigo", 2.0), "emissiveColor": C("cyan"), "emissiveIntensity": 0.0,
            "roughness": 0.28, "metallic": 0.35, "doubleSided": True,
            "thinFilm": {"thickness": 400.0, "ior": 1.6}}


def sheets(s, specs, mesh_dir):
    g = silk_program()
    s.program(g.doc(base=g.base, emission=g.emit))
    s.silk_program = g
    for spec in specs:
        pos, nrm, uv, idx, length = sheet_mesh(spec)
        write_glb(os.path.join(mesh_dir, "rs-%s.glb" % spec.name), pos, nrm, uv, idx, spec.name)
        print("%s: %d vertices, %d triangles, %.0f m long" % (spec.name, len(pos), len(idx) // 3, length))
        s.proc(spec.name, {"kind": "mesh", "asset": "meshes/rs-%s.glb" % spec.name},
               material=silk_material(spec.name), extra={"normalStep": 0.15}, deformers=drift_deformers() + (music_deformers() if MUSIC else []))


def drift_deformers():
    """The sculpture's own slow life, independent of the music: a very slow, broad, coherent drift that keeps the
    silhouette evolving without ever repeating (the resonator's idle adds a breathing bow on top)."""
    return ([] if os.environ.get("RS_NONOISE") else [
        {"kind": "noise", "amount": 7.0, "space": "world", "scale": 0.009, "speed": 0.06, "seed": 11,
         "axisMask": [1.0, 1.0, 0.6]}]) + [
        # a long, slow swell travelling along the wall into the gather (its phase also takes the performer's
        # Motion Scale through an integrating route, ADR-1372, so the rate changes without a jump)
        {"kind": "sine", "amount": 2.2, "space": "world", "axis": [0.435, -0.03, -0.90],
         "frequency": 2 * math.pi / 70.0, "speed": -0.5, "displacementAxis": WALL_N},
    ]


def camera(s, cam):
    """A slow drift about the framing (never following the music): 120 s, a few metres."""
    s.camera = {"mode": 1, "position": cam["position"], "target": cam["target"], "fov": 40.0, "orbitSpeed": 0.0}
    if cam.get("drift"):
        s.drift_camera(cam["position"], cam["target"], period=120.0, amp=(6.0, 2.5, 5.0), tamp=(4.0, 2.0, 0.0),
                       steps=12)
    s.params_({"camera/lens/focalLength": cam["focal"]})


MIDI_CC = [
    (1, "resonance", "RESONANCE: how hard the music drives the sheet's modes (held and struck)"),
    (2, "foldDepth", "FOLD DEPTH: the broad bows and creases the modes draw"),
    (3, "waveTravel", "WAVE TRAVEL: the bass swell and the onset fronts running out from the gather"),
    (4, "detail", "SURFACE DETAIL: the middle band's fine creases"),
    (5, "iridescence", "IRIDESCENCE: the thin film thickens and the violet sheen widens"),
    (6, "light", "LIGHT RESPONSE: the shimmer, the fronts' light and the magenta in the hardest-ringing folds"),
    (7, "colorDrift", "COLOR DRIFT: the palette journey changes sooner and turns faster"),
    (8, "motion", "MOTION SCALE: the travelling swell quickens and the drift widens"),
    (9, "palette", "PALETTE: choose one of the eight (a direct blend to it)"),
    (10, "paletteLock", "PALETTE LOCK: hold the chosen palette (no automatic changes)"),
]


def control_map():
    return {"midi": {"enabled": True, "filter": "*", "bindings": [
        {"source": "*", "channel": -1, "kind": "cc", "number": n, "signal": name} for n, name, _ in MIDI_CC]}}


def instrument(s, g):
    """The performer's surface. The fields hear the audio history themselves (ADR-1400/1116, through the Response
    controls, ADR-1361); routes add what they do not: the build of a song rings more modes, held sound deepens the
    bows, the lights follow the level, and eight MIDI controls (each 0 = the authored look, turning up adds)."""
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    F = "field/%s/resonance/" % FIELD
    D = "procedural/silk/deform/%d/"
    def op(label):
        return "material/%s/op/%d/%s/" % (g.name, g[label], g.ops[g[label] - 1]["kind"])
    # music -> instrument (slow, never a strobe; no route flashes a whole surface)
    s.route(R("intensity", F + "modes", 4.0, attackMs=4000, decayMs=8000))      # 8 modes at rest, 12 in a build
    s.route(R("sustain", F + "sustain", 0.6, attackMs=1500, decayMs=4000))
    # bass broadens the fold band (a lower Fresnel exponent), low mids deepen the base/fold separation
    s.route(R("bass", op("foldMask") + "value", -1.1, attackMs=200, decayMs=900))
    s.route(R("audio.lowMid", op("foldMix") + "constant", 1.0, op="multiply", gain=0.5, offset=1.0, attackMs=300,
              decayMs=1200))
    # MIDI
    s.control = control_map()
    CC = {"attackMs": 150, "decayMs": 150}
    s.route(R("control.resonance", F + "sustain", 1.5, **CC), R("control.resonance", F + "transient", 1.0, **CC))
    for i in (3, 4):
        s.route(R("control.foldDepth", D % i + "amount", 1.0, op="multiply", gain=1.5, offset=1.0, **CC))
    for i in (5, 6, 7, 8):
        s.route(R("control.waveTravel", D % i + "amount", 1.0, op="multiply", gain=1.5, offset=1.0, **CC))
    s.route(R("control.detail", F + "source/1/amplitude", 0.4, **CC))
    s.route(R("control.iridescence", "procedural/silk/material/thinFilm/thickness", 320.0, **CC),
            R("control.iridescence", op("irid") + "constant", 1.0, op="multiply", gain=2.0, offset=1.0, **CC))
    for label in [x for x in ("trebleGain", "resGain") if x in g.at]:
        s.route(R("control.light", op(label) + "constant", 1.0, op="multiply", gain=2.0, offset=1.0, **CC))
    # Color Drift: the palette journey moves sooner and turns faster
    s.route(R("control.colorDrift", "palette/hold", 1.0, op="multiply", gain=-0.8, offset=1.0, **CC),
            R("control.colorDrift", "palette/transition", 1.0, op="multiply", gain=-0.5, offset=1.0, **CC))
    # the palette, chosen by hand (CC 9 sweeps the eight; each step a direct blend) and locked (CC 10)
    s.route(R("control.palette", "palette/select", float(len(PALETTES) - 1), **CC))
    s.route(R("control.paletteLock", "palette/lock", 1.0, **CC))
    # Motion Scale: the swell's phase integrates the control (ADR-1372: a rate change never jumps) ...
    s.route(R("control.motion", D % 2 + "phase", -0.9, attackMs=300, decayMs=300, integrate=True))
    # ... and the drift widens
    s.route(R("control.motion", D % 1 + "amount", 1.0, op="multiply", gain=0.8, offset=1.0, **CC))

DESIGN = {
    "category": "abstract",
    "thesis": "One immense sheet of iridescent silk, gathered at invisible anchors and spreading broad between them, "
              "hangs in a dark void; the music excites it as a resonator: bass bows the sheet, mids crease it, highs "
              "run light along its folds, and onsets send wavefronts across it.",
    "composition": {
        "background": "designed darkness: a faint indigo depth behind the focal crease",
        "midground": "the drape falling away and turning into the dark",
        "foreground": "the broad sheet entering huge from the lower left",
        "focal": "the gathered pinch, upper right of centre",
        "camera": "a slow drift, never following the music",
    },
    "palette": {"dominant": P["indigo"], "secondary": P["teal"], "accent": P["cyan"], "highlight": P["magenta"],
                "background_value": "near black", "saturation": "cool, with rare magenta"},
    "tier": "one sculpture, three ways of exciting it",
}


def mean_normals(spec, gather_u):
    """The unpleated sheet's mean normal over the wall (u < gather) and over the sail (u > gather)."""
    curve = catmull(spec.points, 4000)
    c, _ = resample_arclength(curve, spec.nu)
    u = np.linspace(0.0, 1.0, spec.nu)
    t, n, b = transported_frame(c)
    roll = smooth_profile(spec.roll, u)
    N = -b * np.sin(roll)[:, None] + n * np.cos(roll)[:, None]
    out = []
    for m in ((u > 0.05) & (u < gather_u - 0.08), (u > gather_u + 0.12)):
        v = N[m].mean(axis=0)
        out.append([round(float(x), 4) for x in v / np.linalg.norm(v)])
    return out


def build(comp, mesh_dir):
    global WALL_N, SAIL_N
    s = kit.Scene(ID, TITLE, DESIGN)
    if comp == "a":
        WALL_N, SAIL_N = mean_normals(sheets_for("a")[0][0], 0.63)
    environment(s)
    if MUSIC:
        fields(s)
    specs, cam = sheets_for(comp)
    sheets(s, specs, mesh_dir)
    camera(s, cam)
    s.palette = palette_block(s.silk_program)
    if MUSIC:
        instrument(s, s.silk_program)
    else:
        s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    s.params_({
        "post/bloom/intensity": 0.22, "post/bloom/threshold": 1.0, "post/output/vignette": 0.4,
        "post/output/grain": 0.008, "post/grade/contrast": 1.1, "post/lens/chromaticAberration": 0.0,
        "camera/exposure/compensation": 0.8, "post/dof/enabled": False,
        "post/motionBlur/amount": 0.0,
    })
    return s


def variant(project, out, wav=None, camera=None, params=None, tracks=None):
    """A file project of the scene (the review renders): optionally a WAV (else silent: avgen --audio feeds one),
    another camera ("x,y,z:tx,ty,tz") and parameter overrides."""
    with open(project) as f:
        doc = json.load(f)
    if camera:
        p, t = camera.split(":")
        doc["parameters"]["camera/position"] = [float(v) for v in p.split(",")]
        doc["parameters"]["camera/target"] = [float(v) for v in t.split(",")]
        tl = doc.get("timeline", {})
        tl["tracks"] = [tr for tr in tl.get("tracks", []) if not tr["target"].startswith("camera/")]
    for k, v in (params or {}).items():
        # the palette's own parameters are registered after a project's parameter values are read, so the selector's
        # authored values go in its block (palette/select, palette/lock, palette/transition, palette/hold)
        if k.startswith("palette/") and "selector" in doc.get("palette", {}):
            key = k.split("/", 1)[1]
            doc["palette"]["selector"][key] = bool(v) if key == "lock" else (int(v) if key == "select" else v)
            continue
        doc["parameters"][k] = v
    base = os.path.dirname(os.path.abspath(project))
    doc["assets"]["scene"] = {"kind": "composition", "path": os.path.join(base, doc["assets"]["scene"]["path"])}
    if wav:
        doc["assets"]["audio"] = {"path": os.path.abspath(wav)}
    doc.get("sonic", {}).pop("live", None)
    for spec in tracks or []:
        # "path=v0@t0,v1@t1,...": stepped keys (a palette change at t1 is a select change, blended by the selector)
        path, keys = spec.split("=", 1)
        kk = []
        for item in keys.split(","):
            v, t = item.split("@")
            kk.append({"time": float(t), "value": [float(v)], "interp": "step"})
        doc.setdefault("timeline", {"enabled": True, "cues": [], "tracks": []})["tracks"].append(
            {"target": path, "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
             "loopLength": 0.0, "keys": kk})
    with open(out, "w") as f:
        json.dump(doc, f, indent=1)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--composition", default=None, help="a | b | c: a research-board composition")
    ap.add_argument("--out-dir", default=None, help="where a composition is written (default examples/resonant-silk)")
    ap.add_argument("--variant", action="store_true", help="also write a file project (review renders)")
    ap.add_argument("--wav", help="the variant's audio (default: none; feed with avgen --audio)")
    ap.add_argument("--out", help="the variant's path")
    ap.add_argument("--camera", help="x,y,z:tx,ty,tz for the variant")
    ap.add_argument("--set", action="append", default=[], help="path=json value, for the variant")
    ap.add_argument("--track", action="append", default=[], help="path=v0@t0,v1@t1 stepped timeline keys")
    a = ap.parse_args()
    comp = a.composition or COMPOSITION
    out_dir = a.out_dir or OUT
    s = build(comp, os.path.join(out_dir, "meshes"))
    pp, sp = s.write(out_dir)
    print(pp)
    if out_dir == OUT and comp == COMPOSITION and MUSIC:
        cc = ", ".join("CC %d %s" % (n, d.split(":")[0].lower()) for n, _, d in MIDI_CC)
        kit.upsert_index_entry({
            "category": CATEGORY,
            "description": "Play live audio (any mix) and, optionally, a MIDI controller: " + cc + ". "
                           + DESIGN["thesis"] + " See docs/prototypes/resonant-silk/02-report.md.",
            "live": True,
            "name": TITLE,
            "project": "resonant-silk/resonant-silk.json"})
    if a.variant:
        params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
        print(variant(pp, a.out, a.wav, a.camera, params, a.track))


if __name__ == "__main__":
    main()

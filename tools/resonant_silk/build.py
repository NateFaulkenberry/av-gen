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
                 curl=0.0, edge_wave=0.0):
        self.name, self.points, self.roll, self.span, self.cloth = name, points, roll, span, cloth
        self.billow, self.pleats, self.nu, self.nv = billow, pleats, nu, nv
        self.pleat_wander, self.seed, self.curl, self.edge_wave = pleat_wander, seed, curl, edge_wave


def sheet_mesh(spec):
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
        ew = spec.edge_wave * np.abs(V) ** 4 * np.sin(2 * math.pi * 9.0 * U + 3.0 * V)
        pos = pos + (ew * span[:, None])[..., None] * N[:, None, :]
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
        # THE GATHER: one sheet enters as a broad fan from the lower-left foreground, is drawn through an invisible
        # ring upper right of centre (the focal gather, where every pleat converges), and opens again into a second,
        # wider fan that rises, turns over and falls away into the dark behind. A smaller second layer is gathered
        # through the same ring (one object, not two ribbons).
        g = (14.0, 10.0, -52.0)
        main = SheetSpec(
            "silk",
            points=[(-95, -50, 30), (-50, -22, 0), (-12, 0, -30), g, (40, 18, -70), (78, 34, -105),
                    (92, 20, -150), (70, -14, -200)],
            roll=[(0.0, 1.35), (0.3, 1.25), (0.46, 1.0), (0.62, 1.4), (0.8, 2.3), (1.0, 2.9)],
            span=[(0.0, 26.0), (0.22, 30.0), (0.4, 7.0), (0.46, 2.8), (0.52, 7.0), (0.68, 34.0), (0.86, 30.0),
                  (1.0, 12.0)],
            cloth=[(0.0, 38.0), (1.0, 40.0)],
            billow=[(0.0, 3.0), (0.22, 8.0), (0.42, 0.0), (0.5, 0.0), (0.7, -10.0), (0.9, -6.0), (1.0, 0.0)],
            pleats=[(6.0, 0.8), (13.0, 0.25)], seed=3, curl=0.1, edge_wave=0.02, nu=600, nv=128)
        under = SheetSpec(
            "silk2",
            points=[(40, -70, -10), (26, -28, -36), g, (0, 30, -80), (-30, 44, -130), (-70, 30, -180)],
            roll=[(0.0, 1.2), (0.45, 1.5), (1.0, 2.4)],
            span=[(0.0, 14.0), (0.3, 8.0), (0.42, 2.2), (0.55, 8.0), (0.8, 22.0), (1.0, 10.0)],
            cloth=[(0.0, 22.0), (1.0, 24.0)],
            billow=[(0.0, 2.0), (0.3, 3.0), (0.42, 0.0), (0.75, 6.0), (1.0, 0.0)],
            pleats=[(5.0, 0.8), (11.0, 0.3)], seed=11, nu=420, nv=80, curl=-0.1)
        cam = {"position": [-4.0, 0.0, 58.0], "target": [10.0, 4.0, -60.0], "focal": 28.0}
        return ([main, under] if os.environ.get('RS_LAYER2') else [main]), cam
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


# ================================================================================================ world
def environment(s):
    s.environment = {
        "intensity": 0.04, "background": C("abyss", 0.6), "fogColor": C("abyss", 0.8),
        "volumeDensity": 0.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0,
        "shadowRange": 200.0,
        "sky": {"enabled": True, "zenithColor": C("abyss", 0.35), "horizonColor": C("indigo", 0.55),
                "groundColor": C("abyss", 0.25), "haze": 0.6, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    # a narrow, cold, grazing key from high behind-right: strips of light across the folds
    s.light("key", "directional", direction=[-0.55, -0.35, 0.75], color=C("pearl"), intensity=4.0,
            castsShadow=False, contactShadow=False, softness=0.2)
    # a violet counter-light from the lower left, behind the sheet: rims and the far side's colour
    s.light("rim", "directional", direction=[0.6, 0.45, 0.65], color=C("violet"), intensity=2.2,
            castsShadow=False, contactShadow=False)
    # broad cool fill from the camera side, low
    s.light("fill", "directional", direction=[0.1, -0.2, -1.0], color=C("teal"), intensity=0.35,
            castsShadow=False, contactShadow=False)


def silk_material(name):
    return {"baseColor": C("indigo", 1.0), "emissiveColor": C("cyan"), "emissiveIntensity": 0.0,
            "roughness": 0.28, "metallic": 0.35, "doubleSided": True,
            "thinFilm": {"thickness": 520.0, "ior": 1.45}}


def sheets(s, specs, mesh_dir):
    for spec in specs:
        pos, nrm, uv, idx, length = sheet_mesh(spec)
        write_glb(os.path.join(mesh_dir, "rs-%s.glb" % spec.name), pos, nrm, uv, idx, spec.name)
        print("%s: %d vertices, %d triangles, %.0f m long" % (spec.name, len(pos), len(idx) // 3, length))
        s.proc(spec.name, {"kind": "mesh", "asset": "meshes/rs-%s.glb" % spec.name},
               material=silk_material(spec.name), extra={"normalStep": 0.15})


def camera(s, cam):
    s.camera = {"mode": 1, "position": cam["position"], "target": cam["target"], "fov": 40.0, "orbitSpeed": 0.0}
    s.params_({"camera/lens/focalLength": cam["focal"]})


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


def build(comp, mesh_dir):
    s = kit.Scene(ID, TITLE, DESIGN)
    environment(s)
    specs, cam = sheets_for(comp)
    sheets(s, specs, mesh_dir)
    camera(s, cam)
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    s.params_({
        "post/bloom/intensity": 0.22, "post/bloom/threshold": 1.0, "post/output/vignette": 0.4,
        "post/output/grain": 0.008, "post/grade/contrast": 1.1, "post/lens/chromaticAberration": 0.0,
        "camera/exposure/compensation": 0.0, "post/dof/enabled": False,
    })
    return s


def variant(project, out, wav=None, camera=None, params=None):
    """A file project of the scene (the review renders): optionally a WAV (else silent: avgen --audio feeds one),
    another camera ("x,y,z:tx,ty,tz") and parameter overrides."""
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
    if wav:
        doc["assets"]["audio"] = {"path": os.path.abspath(wav)}
    doc.get("sonic", {}).pop("live", None)
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
    a = ap.parse_args()
    comp = a.composition or COMPOSITION
    out_dir = a.out_dir or OUT
    s = build(comp, os.path.join(out_dir, "meshes"))
    pp, sp = s.write(out_dir)
    print(pp)
    if a.variant:
        params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
        print(variant(pp, a.out, a.wav, a.camera, params))


if __name__ == "__main__":
    main()

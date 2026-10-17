#!/usr/bin/env python3
"""EXPERIMENT A, coupled oscillators and phase fields: the generator (docs/prototypes/generative-rd/A-oscillators.md).

    python3 tools/generative_rd/oscillators.py                     # every configuration
    python3 tools/generative_rd/oscillators.py --config membrane   # one
    python3 tools/generative_rd/oscillators.py --config membrane --variant --out <v.json> [--wav <w>] [--set k=v]

An oscillator grid (ADR-1450) on the GPU holds the network: a Kuramoto-Sakaguchi lattice whose cells carry
(cos theta, sin theta, local order R, activity). The scenes read those four channels as geometry and light.
Writes examples/experiments/osc-<config>/.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
from tools.generative_rd import common  # noqa: E402
from tools.generative_rd.common import R, hexrgb  # noqa: E402

# ================================================================================================ network regimes
# Surveyed in numpy (A-oscillators.md §3) with the kernel's own model. K in rad/s, lag in rad, r in cells.
REGIMES = {
    # concentric waves from pacemakers (cells tuned faster by the frequency field), colliding in cusps
    "targets": {"coupling": 10.0, "phaseLag": 1.0, "couplingRadius": 3, "spread": 0.03, "seedAmount": 0.0},
    # rotating phase vortices: curled spiral arms, a few dozen cores
    "spirals": {"coupling": 8.0, "phaseLag": 0.6, "couplingRadius": 4, "spread": 0.05, "seedAmount": 1.0},
    # partial synchrony: islands with phase defects between them (no lag)
    "islands": {"coupling": 4.0, "phaseLag": 0.3, "couplingRadius": 4, "spread": 0.15, "seedAmount": 1.0},
    # global synchrony (the whole field breathes as one)
    "unison": {"coupling": 12.0, "phaseLag": 0.0, "couplingRadius": 4, "spread": 0.03, "seedAmount": 0.5},
}

PALETTES = {
    "bronze": {  # bronze crests over a teal-black ground; the disagreement lights in cold cyan
        "trough": "#04100f", "crest": "#9c7a4e", "accent": "#5ff2e0", "accentGain": 1.2, "defect": "#ff7a3d",
        "defectGain": 0.0, "sky": "#020506", "horizon": "#081614", "key": "#ffe0b8", "keyI": 5.0, "rim": "#3fa7b8",
        "rimI": 1.2, "rough": 0.34, "metal": 0.7,
    },
    "nacre": {  # pale nacre on blue-grey, non-luminous
        "trough": "#2a313c", "crest": "#e8e4dc", "accent": "#000000", "accentGain": 0.0, "defect": "#000000",
        "defectGain": 0.0, "sky": "#0d1014", "horizon": "#2a3038", "key": "#fff3e3", "keyI": 5.0, "rim": "#8fa8c8",
        "rimI": 1.0, "rough": 0.5, "metal": 0.1,
    },
    "nocturne": {  # black lacquer; only the phase defects glow (amber) and the fronts (violet)
        "trough": "#030305", "crest": "#16131f", "accent": "#7a5cff", "accentGain": 0.9, "defect": "#ffb04a",
        "defectGain": 2.0, "sky": "#010102", "horizon": "#05040a", "key": "#c8c4e0", "keyI": 2.0, "rim": "#6a52d8",
        "rimI": 0.8, "rough": 0.2, "metal": 0.4,
    },
}


def C(pal, key, s=1.0):
    return hexrgb(pal[key], s)


def osc_grid(s, name, size, res, regime, seed=7, frequency=0.25, inject=None, inject_rate=0.0, freq_field=None,
             mask=None, links=0, extra=None):
    sx, sz = (size, size) if not isinstance(size, tuple) else size
    rx, rz = (res, res) if not isinstance(res, tuple) else res
    g = {"name": name, "mode": "oscillator", "wrap": "clamp", "resolution": [rx, 1, rz],
         "boundsMin": [-sx / 2, -40.0, -sz / 2], "boundsMax": [sx / 2, 40.0, sz / 2],
         "frequency": frequency, "simRate": 60.0, "maxSubSteps": 4, "seed": seed, "persistence": 0.6,
         "links": links, "linkRange": 48.0, "checkpointInterval": 5.0}
    g.update(REGIMES[regime])
    if inject:
        g.update({"injectField": inject, "injectRate": inject_rate})
    if freq_field:
        g["frequencyField"] = freq_field
    if mask:
        g["conductivityField"] = mask
    if extra:
        g.update(extra)
    s.grid(**g)
    return g


def readers(s, grid, prefix="", position=None, rotation=None):
    for name, ch in (("phase", 1), ("order", 2), ("activity", 3)):  # sin theta, local order R, strain + kicks
        n = s.grid_reader(prefix + (name.capitalize() if prefix else name), grid, channel=ch)
        if position is not None:
            n["position"] = [float(v) for v in position]
            n["field"]["position"] = [0.0, 0.0, 0.0]
        if rotation is not None:
            n["rotation"] = [float(v) for v in rotation]


def pacemakers(s, centres, gain=0.22, radius=5.0, period=0.0):
    """Cells tuned faster in a few small disks: each becomes the source of a train of concentric waves."""
    names = []
    for i, c in enumerate(centres):
        n = "pace%d" % i
        s.field(n, {"kind": "radial", "radius": radius, "strength": gain}, position=c)
        names.append(n)
        if period:
            common.lissajous_track(s, "field/%s/position" % n, c, (6.0, 5.0), period * (1 + 0.37 * i), ratio=(1, 1),
                                   phase=(0.13 * i, 0.25 + 0.21 * i))
    s.field("tune", {"kind": "compound", "combine": "add", "children": names})
    return "tune"


def phase_program(pal, name, prefix=""):
    g = common.Prog(name)
    g.op("phase", "field", 0, field=prefix + "phase")                    # r0 = sin theta
    g.op(None, "smoothstep", 1, a=0, k=[-0.7, 0.9, 0, 0])                # crest mask
    g.op("trough", "constant", 2, k=C(pal, "trough") + [1])
    g.op("crest", "constant", 3, k=C(pal, "crest") + [1])
    g.op(None, "mixBy", 4, a=2, b=3, c=1)                                # r4 = albedo
    g.op("activity", "field", 5, field=prefix + "activity")              # r5 = activity
    g.op(None, "smoothstep", 5, a=5, k=[0.8, 3.0, 0, 0])               # only real strain: defects, kicks, cusps
    g.op("accent", "constant", 6, k=C(pal, "accent", pal["accentGain"]) + [1])
    g.op(None, "multiply", 6, a=5, b=6)                                  # r6 = front light
    g.op("order", "field", 5, field=prefix + "order")                    # r5 = R
    g.op(None, "remap", 5, a=5, k=[0.85, 0.2, 0.0, 1.0], f=1.0)          # 0 when in step, 1 at a defect
    g.op(None, "power", 5, a=5, f=3.0)
    g.op("defect", "constant", 7, k=C(pal, "defect", pal["defectGain"]) + [1])
    g.op(None, "multiply", 5, a=5, b=7)
    g.op(None, "add", 6, a=6, b=5)                                       # r6 = emission
    g.op("rough", "constant", 7, k=[pal["rough"]] * 3 + [1])
    g.base, g.emit, g.rough = 4, 6, 7
    return g


def environment(s, pal, key_dir, fog=0.0):
    s.environment = {
        "intensity": 0.06, "background": C(pal, "sky"), "fogColor": C(pal, "horizon"),
        "volumeDensity": fog, "fogSky": 1.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0,
        "shadowRange": 120.0, "ambientOcclusion": False, "contactShadows": False,
        "sky": {"enabled": True, "zenithColor": C(pal, "sky"), "horizonColor": C(pal, "horizon"),
                "groundColor": C(pal, "sky"), "haze": 1.0, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.light("key", "directional", direction=key_dir, color=C(pal, "key"), intensity=pal["keyI"], castsShadow=True,
            contactShadow=False, softness=0.3)
    s.light("rim", "directional", direction=[0.5, -0.35, 0.8], color=C(pal, "rim"), intensity=pal["rimI"],
            castsShadow=False, contactShadow=False)


def common_palette(name):
    return dict(PALETTES[name], name=name)


DESIGNS = {
    "loom": ("Phase Loom",
             "Nine long ribbons strung across two oscillator networks: one lifts them in travelling rings from two "
             "pacemakers, the other sways them in slow spirals, and light gathers wherever the two fall into phase."),
    "flaps": ("Phase Wall",
              "A kinetic wall of six thousand metal flaps, each turned by its own oscillator: when they fall into step "
              "the whole wall turns and flashes as one; as the coupling loosens it breaks into islands that turn "
              "against each other, and travelling waves sweep bands of light across it."),
    "membrane": ("Phase Membrane",
                 "An amphitheatre whose floor is a living membrane of coupled oscillators: a few pacemakers send "
                 "concentric waves across it, the waves meet in cusps and annihilate, and wherever neighbours "
                 "disagree the surface lights."),
}


def design(key):
    return {"category": "experiment", "thesis": DESIGNS[key][1], "experiment": "A coupled oscillators"}


def membrane(pal_name="bronze", regime="targets", autonomous=False):
    """A1, the phase-wave sculpture: sin theta raises the floor of a bowl; the fronts and the defects light."""
    pal = common_palette(pal_name)
    s = common.Scene("osc-membrane", DESIGNS["membrane"][0], design("membrane"))
    radius, height = 52.0, 22.0
    tune = pacemakers(s, [(-14.0, 0.0, -8.0), (12.0, 0.0, 6.0), (-2.0, 0.0, 24.0)], period=70.0)
    osc_grid(s, "net", 2 * radius, 384, regime, freq_field=tune, extra={"noise": 0.02})
    readers(s, "net")
    mesh_dir = os.path.join(common.EXAMPLES, "osc-membrane", "meshes")
    pos, nrm, uv, idx = common.bowl_mesh(radius, height, 260, 420)
    common.write_glb(os.path.join(mesh_dir, "osc-bowl.glb"), pos, nrm, uv, idx, "bowl")
    g = phase_program(pal, "oscMembrane")
    s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
    s.proc("membrane", {"kind": "mesh", "asset": "meshes/osc-bowl.glb"},
           material={"program": g.name, "baseColor": [1, 1, 1], "roughness": pal["rough"],
                     "metallic": pal["metal"], "doubleSided": True},
           deformers=[{"kind": "field", "field": "phase", "amount": 1.1, "space": "world", "alongNormal": True}],
           extra={"normalStep": 0.1})
    environment(s, pal, key_dir=[0.7, -0.45, -0.55])
    s.camera = {"mode": 1, "position": [6.0, 22.0, 32.0], "target": [-2.0, 0.0, -8.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=90.0, amp=(4.0, 1.0, 2.0), tamp=(2.0, 0.5, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 24.0, "post/bloom/intensity": 0.2, "post/output/vignette": 0.45,
               "post/grade/contrast": 1.08})
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    return s


def flaps(pal_name="bronze", regime="islands", autonomous=False):
    """A2, the synchronisation sculpture: a kinetic wall of 6,000 metal flaps, each turned about its vertical axis by
    its oscillator's phase. In step, the wall turns as one and flashes; in islands, patches turn together and
    disagree at their seams; travelling waves sweep bands of light across it. Autonomously the coupling breathes
    (a slow timeline sweep) so the wall passes through disorder, islands, synchrony and back."""
    pal = common_palette(pal_name)
    s = common.Scene("osc-flaps", DESIGNS["flaps"][0], design("flaps"))
    W, H, nx, ny = 120.0, 50.0, 120, 50
    osc_grid(s, "net", (W, H), (240, 100), regime, seed=3, frequency=0.18, extra={"noise": 0.05})
    # the readers turn the grid's XZ plane up into the wall's XY plane (world y = grid z), centred on the wall
    readers(s, "net", position=(0.0, H / 2, 0.0), rotation=(-90.0, 0.0, 0.0))
    g = phase_program(pal, "oscFlaps")
    s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
    sp = W / nx
    s.proc("flaps", {"kind": "box", "size": [0.86 * sp, 0.86 * sp, 0.035]},
           distribution={"kind": "grid", "gridCount": [nx, ny, 1], "gridSpacing": [sp, sp, 1.0]},
           position=(0.0, H / 2, 0.0),
           material={"program": g.name, "baseColor": [1, 1, 1], "roughness": pal["rough"], "metallic": pal["metal"]},
           extra={"effectors": [{"field": "phase", "op": "rotation", "blend": "add", "strength": 0.9,
                                 "axis": [0.0, 1.0, 0.0]}]})
    # the coupling breathes: disorder (K 1.5) -> islands -> unison (K 14) and back, over 96 s
    s.track("grid/net/coupling", [{"time": 0.0, "value": [1.5], "interp": "smooth"},
                                  {"time": 40.0, "value": [14.0], "interp": "smooth"},
                                  {"time": 56.0, "value": [14.0], "interp": "smooth"},
                                  {"time": 96.0, "value": [1.5], "interp": "smooth"}], loop=96.0)
    environment(s, pal, key_dir=[0.35, -0.4, -0.85])
    s.environment["shadowRange"] = 160.0
    s.camera = {"mode": 1, "position": [-14.0, 16.0, 36.0], "target": [6.0, 26.0, 0.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=110.0, amp=(5.0, 1.5, 2.0), tamp=(3.0, 1.0, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 26.0, "post/bloom/intensity": 0.22, "post/output/vignette": 0.4,
               "post/grade/contrast": 1.08})
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    return s


def ribbon_mesh(n, length, width, nu=420, nv=10):
    """`n` long ribbons across x, fanned in depth and height: a loom of strips, each a gentle catenary with a slow
    roll, so the layers overlap in perspective and the phase waves read as travelling along and between them."""
    meshes = []
    for i in range(n):
        f = i / max(n - 1, 1)
        z0 = -30.0 + 60.0 * f
        y0 = 6.0 + 7.0 * math.sin(math.pi * f) + 2.0 * f
        u = np.linspace(-0.5, 0.5, nu)
        x = u * length
        y = y0 + 6.0 * (4 * u * u - 1.0) * (0.6 + 0.4 * math.cos(3.0 * f))
        z = z0 + 5.0 * np.sin(2 * math.pi * (u * 0.8 + 0.3 * f))
        roll = 0.5 * math.pi + 0.45 * np.sin(2 * math.pi * (u * 1.3 + 0.5 * f))  # upright curtains
        v = np.linspace(-0.5, 0.5, nv) * width
        U, V = np.meshgrid(np.arange(nu), v)
        P = np.zeros((nv, nu, 3))
        P[..., 0] = x[None, :]
        P[..., 1] = y[None, :] + V * np.sin(roll)[None, :]
        P[..., 2] = z[None, :] + V * np.cos(roll)[None, :]
        pos = P.reshape(-1, 3)
        nrm = np.stack([np.zeros(nu * nv), np.repeat(np.cos(roll)[None, :], nv, 0).ravel(),
                        -np.repeat(np.sin(roll)[None, :], nv, 0).ravel()], axis=1)
        uv = np.stack([np.tile(np.linspace(0, 1, nu), nv), np.repeat(np.linspace(0, 1, nv), nu)], axis=1)
        idx = common.grid_indices(nu, nv)
        meshes.append((pos, nrm, uv, idx))
    return common.merge(*meshes)


def interference_program(pal, name):
    """Two networks on one strip: A's crest/trough sets the albedo; where A and B are in phase (sA sB near 1) the
    strip lights -- constructive interference drawn as light, destructive as dark."""
    g = common.Prog(name)
    g.op("phaseA", "field", 0, field="aPhase")
    g.op(None, "smoothstep", 1, a=0, k=[-0.7, 0.9, 0, 0])
    g.op("trough", "constant", 2, k=C(pal, "trough") + [1])
    g.op("crest", "constant", 3, k=C(pal, "crest") + [1])
    g.op(None, "mixBy", 4, a=2, b=3, c=1)                                # albedo
    g.op("phaseB", "field", 5, field="bPhase")
    g.op(None, "multiply", 5, a=0, b=5)                                  # sA sB
    g.op(None, "smoothstep", 5, a=5, k=[0.55, 0.95, 0, 0])               # in phase
    g.op("accent", "constant", 6, k=C(pal, "accent", pal["accentGain"]) + [1])
    g.op(None, "multiply", 6, a=5, b=6)
    g.op("activity", "field", 5, field="aActivity")
    g.op(None, "smoothstep", 5, a=5, k=[0.8, 3.0, 0, 0])
    g.op("defect", "constant", 7, k=C(pal, "defect", max(pal["defectGain"], 0.8)) + [1])
    g.op(None, "multiply", 5, a=5, b=7)
    g.op(None, "add", 6, a=6, b=5)
    g.op("rough", "constant", 7, k=[pal["rough"]] * 3 + [1])
    g.base, g.emit, g.rough = 4, 6, 7
    return g


def loom(pal_name="nacre", regime="spirals", autonomous=False):
    """A3 + A4, interference geometry and the Resonant Silk extension: nine long ribbons across two networks. Network A
    (pacemakers, travelling rings) lifts them; network B (slower, coarser spirals) sways them sideways; light marks
    where the two agree. Ribbons in one wave rise together; a seam between domains splits a ribbon's run."""
    pal = common_palette(pal_name)
    s = common.Scene("osc-loom", DESIGNS["loom"][0], design("loom"))
    size = 140.0
    tune = pacemakers(s, [(-30.0, 0.0, -10.0), (25.0, 0.0, 12.0)], gain=0.2, radius=6.0, period=80.0)
    osc_grid(s, "netA", size, 320, regime, seed=5, frequency=0.22, freq_field=tune, extra={"noise": 0.02})
    osc_grid(s, "netB", size, 160, "islands", seed=9, frequency=0.12, extra={"noise": 0.02})
    readers(s, "netA", prefix="a")
    readers(s, "netB", prefix="b")
    mesh_dir = os.path.join(common.EXAMPLES, "osc-loom", "meshes")
    pos, nrm, uv, idx = ribbon_mesh(9, size * 0.9, 5.0)
    common.write_glb(os.path.join(mesh_dir, "osc-ribbons.glb"), pos, nrm, uv, idx, "ribbons")
    g = interference_program(pal, "oscLoom")
    s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
    s.proc("ribbons", {"kind": "mesh", "asset": "meshes/osc-ribbons.glb"},
           material={"program": g.name, "baseColor": [1, 1, 1], "roughness": pal["rough"], "metallic": pal["metal"],
                     "doubleSided": True, "thinFilm": {"thickness": 380.0, "ior": 1.5}},
           deformers=[{"kind": "field", "field": "aPhase", "amount": 2.6, "space": "world", "alongNormal": False,
                       "axis": [0.0, 1.0, 0.0]},
                      {"kind": "field", "field": "bPhase", "amount": 3.0, "space": "world", "alongNormal": False,
                       "axis": [0.0, 0.0, 1.0]}],
           extra={"normalStep": 0.12})
    environment(s, pal, key_dir=[-0.45, -0.35, -0.82])
    s.camera = {"mode": 1, "position": [26.0, 18.0, 58.0], "target": [-10.0, 6.0, -4.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=100.0, amp=(4.0, 1.5, 3.0), tamp=(3.0, 1.0, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 30.0, "post/bloom/intensity": 0.3, "post/output/vignette": 0.45,
               "post/grade/contrast": 1.08})
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    return s


CONFIGS = {"membrane": membrane, "flaps": flaps, "loom": loom}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=None)
    ap.add_argument("--palette", default=None)
    ap.add_argument("--regime", default=None)
    ap.add_argument("--autonomous", action="store_true")
    ap.add_argument("--variant", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--wav")
    ap.add_argument("--camera")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--track", action="append", default=[])
    a = ap.parse_args()
    for name in ([a.config] if a.config else list(CONFIGS)):
        kw = {"autonomous": a.autonomous}
        if a.palette:
            kw["pal_name"] = a.palette
        if a.regime:
            kw["regime"] = a.regime
        s = CONFIGS[name](**kw)
        pp, sp = common.write_example(s, os.path.join(common.EXAMPLES, s.id))
        print(pp)
        if a.variant:
            params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
            print(common.variant(pp, a.out, a.wav, a.camera, params, a.track))


if __name__ == "__main__":
    main()

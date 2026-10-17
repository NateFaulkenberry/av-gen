#!/usr/bin/env python3
"""EXPERIMENT C, reaction-diffusion: the generator (docs/prototypes/generative-rd/C-reaction-diffusion.md).

    python3 tools/generative_rd/reaction_diffusion.py                  # every configuration + index entries
    python3 tools/generative_rd/reaction_diffusion.py --config relief  # one
    python3 tools/generative_rd/reaction_diffusion.py --config relief --variant --out <v.json> [--wav <w>]
           [--set path=json] [--camera x,y,z:tx,ty,tz] [--palette <name>] [--autonomous]

A Gray-Scott plane (ADR-1451) on the GPU is the pattern; the scene turns it into structure. The pattern field is one
grid; the palettes are separate (a configuration can wear any of them: `--palette`). Writes
examples/experiments/rd-<config>/ (project, scene, meshes).
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
from tools.generative_rd.common import R, hexrgb, mix  # noqa: E402

# ================================================================================================ pattern regimes
# Named artistic modes: (feed, kill) pairs for D_A = 0.3, D_B = 0.15 on the 9-point plane stencil, one model unit per
# iteration. Surveyed in numpy at 160^2 x 8000 steps (C-reaction-diffusion.md §3): which form, which freeze, which die.
REGIMES = {
    "coral": (0.055, 0.062),    # thick worms and rings -> a labyrinth (fills ~60%)
    "maze": (0.029, 0.057),     # soft, fine labyrinth (~53%), settles fastest
    "cells": (0.0367, 0.0649),  # mitosis: spots that divide until they tile the plane (~25%)
    "worms": (0.046, 0.063),    # broken worms and loops
    "holes": (0.039, 0.058),    # the negative: a filled plane with holes (~84%)
    "spots": (0.030, 0.062),    # small dense spots, hexagonal packing (~29%)
    "fingers": (0.022, 0.051),  # branching, never quite settles
    "erase": (0.010, 0.075),    # nothing survives: B dies (used to clear a region)
}
DA, DB = 0.3, 0.15
# In a volume (6-neighbour stencil, D_A = 0.16, D_B = 0.08, a unit step): the same names, re-measured on the GPU.
REGIMES3 = {
    "worms": (0.030, 0.060),
    "cells": (0.026, 0.064),
}

# ================================================================================================ palettes
# Pattern generation is separate from the palette: every configuration's program reads its colours from these roles.
#   ground  the low (U-rich) surface      ridge  the raised pattern (V-rich)
#   line    the pattern's boundary light  sky / fog / key / rim  the light
PALETTES = {
    "ivory": {  # warm ivory and dark mineral; non-luminous: the relief is drawn by a raking light
        "ground": "#1e1915", "ridge": "#c9bb9e", "line": "#000000", "lineGain": 0.0,
        "sky": "#0b0a0c", "horizon": "#2a2420", "key": "#ffe7c4", "keyI": 6.0, "rim": "#5a6b8c", "rimI": 0.7,
        "rough": 0.62, "metal": 0.0,
    },
    "garnet": {  # high-contrast red and indigo
        "ground": "#120c2e", "ridge": "#b3122e", "line": "#ff5a3c", "lineGain": 0.25,
        "sky": "#05040e", "horizon": "#0a0720", "key": "#ffd2c4", "keyI": 3.2, "rim": "#4c3cff", "rimI": 1.4,
        "rough": 0.38, "metal": 0.0,
    },
    "violet": {  # deep violet with restrained gold
        "ground": "#140a22", "ridge": "#3a1d5c", "line": "#ffc45a", "lineGain": 0.9,
        "sky": "#06030b", "horizon": "#1e1030", "key": "#e9d8ff", "keyI": 2.4, "rim": "#ffb347", "rimI": 0.6,
        "rough": 0.3, "metal": 0.6,
    },
    "ember": {  # black surfaces with selective emissive boundaries
        "ground": "#050505", "ridge": "#0e0d0d", "line": "#ff6a1a", "lineGain": 1.1,
        "sky": "#020202", "horizon": "#0d0806", "key": "#a0a8b8", "keyI": 0.8, "rim": "#ff8a3a", "rimI": 0.3,
        "rough": 0.5, "metal": 0.2,
    },
    "celadon": {  # low saturation with controlled luminous regions
        "ground": "#3b4440", "ridge": "#9fb3a8", "line": "#d8fff0", "lineGain": 0.35,
        "sky": "#1a1f1e", "horizon": "#56625c", "key": "#fff6e8", "keyI": 2.6, "rim": "#a8c4d8", "rimI": 0.8,
        "rough": 0.7, "metal": 0.0,
    },
}


def C(pal, key, s=1.0):
    return hexrgb(pal[key], s)


# ================================================================================================ the pattern
def rd_grid(s, name, size, res, regime_a, regime_b, regime_field, seed=11, speed_iters=16, inject=None,
            inject_rate=0.0, y=(-3.0, 3.0), wrap="wrap"):
    fa, ka = REGIMES[regime_a]
    fb, kb = REGIMES[regime_b]
    g = {
        "name": name, "mode": "reactionDiffusion", "wrap": wrap,
        "resolution": [res, 1, res],
        "boundsMin": [-size / 2, y[0], -size / 2], "boundsMax": [size / 2, y[1], size / 2],
        "diffusionA": DA, "diffusionB": DB, "feed": fa, "kill": ka,
        "simRate": 60.0, "maxSubSteps": 4,
        "speed": 60.0 * speed_iters, "iterations": speed_iters,  # one model unit per iteration
        "seed": seed, "seedAmount": 1.0,
        "checkpointInterval": 5.0,
    }
    if regime_field:
        g.update({"regimeField": regime_field, "feedB": fb, "killB": kb})
    if inject:
        g.update({"injectField": inject, "injectRate": inject_rate})
    s.grid(**g)
    return g


# ================================================================================================ configurations
DESIGNS = {
    "pins": {
        "title": "RD Pins",
        "thesis": "A dark floor of forty thousand pins; the pattern lifts them and lights only those on its boundary, "
                  "so contour lines of a living maze hang in the air and wander as the pattern grows.",
    },
    "bloom": {
        "title": "RD Bloom",
        "thesis": "A single closed form, a seed-pod or a planet, whose skin is grown by a reaction-diffusion volume: "
                  "cells on one cap, worms over the rest, and a frontier that creeps between them.",
    },
    "relief": {
        "title": "RD Relief",
        "thesis": "A vast mineral plain whose surface is grown, not carved: a reaction-diffusion pattern raises ridges "
                  "and cells out of the stone, and the frontier between two pattern families wanders across it, "
                  "rewriting the relief as it passes.",
    },
}


def relief(pal_name="ivory", autonomous=False):
    """C1: the pattern as relief on a plain seen from low, raked by one light; the regime frontier wanders."""
    pal = common_palette(pal_name)
    s = common.Scene("rd-relief", DESIGNS["relief"]["title"], design("relief"))
    size, res = 96.0, 512
    # the regime map: a slow fbm drifting across the plain (coral where it is low, cells where it is high)
    # the regime map: a pool of cells in the middle distance (a radial field), its shore made irregular and kept
    # wandering by a slow fbm: coral outside, cells inside, and the frontier between them never still
    s.field("regimePool", {"kind": "radial", "radius": 26.0, "strength": 1.1}, position=(-6.0, 0.0, -2.0))
    s.field("regimeDrift", {"kind": "noise", "frequency": 0.05, "speed": 0.12, "seed": 5, "strength": 0.55})
    s.field("regime", {"kind": "compound", "combine": "add", "children": ["regimePool", "regimeDrift"]})
    common.lissajous_track(s, "field/regimePool/position", (-6.0, 0.0, -2.0), (14.0, 9.0), 96.0, ratio=(1, 2))
    # the brush: a slow, wandering seed of B that scars the settled pattern and lets it heal behind it (the
    # autonomous disturbance; the music adds its own)
    s.field("brush", {"kind": "sphere", "radius": 1.6, "softness": 0.8}, position=(0.0, 0.0, 6.0))
    common.lissajous_track(s, "field/brush/position", (-2.0, 0.0, 8.0), (16.0, 12.0), 61.0, ratio=(2, 3))
    rd_grid(s, "pattern", size, res, "coral", "cells", "regime", inject="brush", inject_rate=1.5)
    s.grid_reader("relief", "pattern")
    # the mesh: an XZ sheet, denser near the camera (z > 0)
    mesh_dir = os.path.join(common.EXAMPLES, "rd-relief", "meshes")
    # an XZ sheet three tiles wide (the grid wraps, so the pattern tiles out to the fog), its rows and columns packed
    # densest under the camera's view (about z = 0..30) and sparse toward the horizon
    nx, nz = 400, 380
    half = 1.5 * size
    pos, nrm, uv, idx = common.plane_mesh(2.0, 2.0, nx, nz)
    u, w = pos[:, 0], pos[:, 2]
    pos[:, 0] = np.sign(u) * np.abs(u) ** 2.2 * half
    pos[:, 2] = 18.0 + np.sign(w) * np.abs(w) ** 2.2 * (half + 18.0)
    common.write_glb(os.path.join(mesh_dir, "rd-plain.glb"), pos, nrm, uv, idx, "plain")
    g = relief_program(pal)
    s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
    s.proc("plain", {"kind": "mesh", "asset": "meshes/rd-plain.glb"},
           material={"program": g.name, "baseColor": [1, 1, 1], "roughness": pal["rough"],
                     "metallic": pal["metal"], "doubleSided": True},
           deformers=[{"kind": "field", "field": "relief", "amount": 1.6, "space": "world", "alongNormal": False,
                       "axis": [0.0, 1.0, 0.0]}],
           extra={"normalStep": 0.08})
    lights(s, pal, key_dir=[0.82, -0.30, -0.48])
    s.camera = {"mode": 1, "position": [8.0, 14.0, 44.0], "target": [-4.0, -2.0, -6.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=90.0, amp=(3.0, 0.6, 2.0), tamp=(2.0, 0.3, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 32.0, "post/bloom/intensity": 0.12, "post/output/vignette": 0.45,
               "post/grade/contrast": 1.08, "camera/exposure/compensation": 0.3})
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    return s


def relief_program(pal, name="rdRelief", tip=False):
    g = common.Prog(name)
    g.op("b", "field", 0, field="relief")                                   # r0 = B
    g.op(None, "smoothstep", 1, a=0, k=[0.04, 0.20, 0, 0])                  # r1 = raised mask
    g.op("ground", "constant", 2, k=C(pal, "ground") + [1])
    g.op("ridge", "constant", 3, k=C(pal, "ridge") + [1])
    g.op(None, "mixBy", 4, a=2, b=3, c=1)                                   # r4 = albedo
    # the boundary: a narrow band where B crosses ~0.17 (the pattern's edge)
    g.op(None, "remap", 5, a=0, k=[0.17, 0.27, 0.0, 1.0])                   # (B - 0.17) / 0.1
    g.op(None, "multiply", 5, a=5, b=5)                                     # squared distance from the edge
    g.op(None, "remap", 5, a=5, k=[0.0, 1.0, 1.0, 0.0], f=1.0)              # 1 on the edge, 0 a band away
    g.op(None, "power", 5, a=5, f=6.0)
    g.op("line", "constant", 6, k=C(pal, "line", pal["lineGain"]) + [1])
    g.op(None, "multiply", 6, a=5, b=6)                                     # r6 = emission
    if tip:  # a pin glows at its tip (local y > 0), not along its shaft
        g.op(None, "input", 7, inp="localPosition")
        g.op(None, "swizzle", 7, a=7, k=[1, 1, 1, 1])
        g.op(None, "smoothstep", 7, a=7, k=[0.0, 0.1, 0, 0])
        g.op(None, "multiply", 6, a=6, b=7)
    g.op("rough", "constant", 7, k=[pal["rough"]] * 3 + [1])
    g.base, g.emit, g.rough = 4, 6, 7
    return g


def lights(s, pal, key_dir, fog=0.004):
    s.environment = {
        "intensity": 0.05, "background": C(pal, "sky"), "fogColor": C(pal, "horizon", 0.8),
        "volumeDensity": fog, "fogSky": 1.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0,
        "shadowRange": 120.0, "ambientOcclusion": False, "contactShadows": False,
        "sky": {"enabled": True, "zenithColor": C(pal, "sky"), "horizonColor": C(pal, "horizon"),
                "groundColor": C(pal, "sky"), "haze": 1.0, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.light("key", "directional", direction=key_dir, color=C(pal, "key"), intensity=pal["keyI"], castsShadow=True,
            contactShadow=False, softness=0.3)
    s.light("rim", "directional", direction=[0.6, -0.3, 0.75], color=C(pal, "rim"), intensity=pal["rimI"],
            castsShadow=False, contactShadow=False)


def common_palette(name):
    return dict(PALETTES[name], name=name)


def design(key):
    return {"category": "experiment", "thesis": DESIGNS[key]["thesis"], "experiment": "C reaction-diffusion"}


def bloom(pal_name="garnet", autonomous=False):
    """C2: a closed form. A 3D Gray-Scott volume around a sphere; the surface reads the volume where it passes, so
    the pattern covers the closed mesh without a seam or a pole, and the relief rises along the normal."""
    pal = common_palette(pal_name)
    s = common.Scene("rd-bloom", DESIGNS["bloom"]["title"], design("bloom"))
    R0 = 12.0
    half = 14.5
    # the volume regime: cells near the 'south' cap, worms over the rest; the frontier creeps with a slow fbm
    s.field("regimeCap", {"kind": "linearGradient", "axis": [0.3, -1.0, 0.2], "length": 30.0, "strength": 1.0})
    s.field("regimeDrift", {"kind": "noise", "frequency": 0.08, "speed": 0.1, "seed": 9, "strength": 0.45})
    s.field("regime", {"kind": "compound", "combine": "add", "children": ["regimeCap", "regimeDrift"]})
    s.field("brush", {"kind": "sphere", "radius": 1.4, "softness": 0.7}, position=(0.0, 0.0, R0))
    common.lissajous_track(s, "field/brush/position", (0.0, 0.0, 0.0), (R0, R0), 53.0, ratio=(1, 1),
                           phase=(0.0, 0.25))
    s.grid(name="pattern", mode="reactionDiffusion", wrap="clamp", resolution=[100, 100, 100],
           boundsMin=[-half] * 3, boundsMax=[half] * 3, diffusionA=0.16, diffusionB=0.08,
           feed=REGIMES3["worms"][0], kill=REGIMES3["worms"][1], regimeField="regime",
           feedB=REGIMES3["cells"][0], killB=REGIMES3["cells"][1], injectField="brush", injectRate=1.2,
           simRate=60.0, maxSubSteps=4, speed=60.0 * 10, iterations=20, seed=13, seedAmount=1.0,
           checkpointInterval=5.0)
    s.grid_reader("relief", "pattern")
    mesh_dir = os.path.join(common.EXAMPLES, "rd-bloom", "meshes")
    pos, nrm, uv, idx = common.sphere_mesh(R0, 320, 161)
    common.write_glb(os.path.join(mesh_dir, "rd-sphere.glb"), pos, nrm, uv, idx, "sphere")
    g = relief_program(pal, "rdBloom")
    s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
    s.proc("form", {"kind": "mesh", "asset": "meshes/rd-sphere.glb"},
           material={"program": g.name, "baseColor": [1, 1, 1], "roughness": pal["rough"],
                     "metallic": pal["metal"], "doubleSided": False},
           deformers=[{"kind": "field", "field": "relief", "amount": 3.2, "space": "world", "alongNormal": True}],
           extra={"normalStep": 0.06})
    lights(s, pal, key_dir=[0.75, -0.45, -0.48], fog=0.0)
    s.camera = {"mode": 1, "position": [9.0, 7.0, 25.0], "target": [-2.5, 1.0, 0.0], "fov": 40.0, "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=80.0, amp=(3.0, 1.5, 1.0), tamp=(1.0, 0.6, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 35.0, "post/bloom/intensity": 0.18, "post/output/vignette": 0.5,
               "post/grade/contrast": 1.1})
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    return s


def pins(pal_name="ember", autonomous=False):
    """C3: pattern boundaries as geometry. A field of 40,000 thin pins over a dark floor; each pin rises with the
    pattern under it and glows only where it stands on the pattern's boundary, so the contour is drawn in the air
    by the pins' tips."""
    pal = common_palette(pal_name)
    s = common.Scene("rd-pins", DESIGNS["pins"]["title"], design("pins"))
    size, res, n = 64.0, 384, 200
    s.field("regimeDrift", {"kind": "noise", "frequency": 0.06, "speed": 0.1, "seed": 21, "strength": 1.2})
    s.field("brush", {"kind": "sphere", "radius": 1.5, "softness": 0.8}, position=(0.0, 0.0, 0.0))
    common.lissajous_track(s, "field/brush/position", (0.0, 0.0, 2.0), (18.0, 14.0), 57.0, ratio=(2, 1))
    rd_grid(s, "pattern", size, res, "maze", "spots", "regimeDrift", inject="brush", inject_rate=1.5, seed=17)
    s.grid_reader("relief", "pattern")
    g = relief_program(pal, "rdPins", tip=True)
    s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
    spacing = size / n
    s.proc("pins", {"kind": "box", "size": [0.4 * spacing, 0.2, 0.4 * spacing]},
           distribution={"kind": "grid", "gridCount": [n, 1, n], "gridSpacing": [spacing, 1.0, spacing]},
           material={"program": g.name, "baseColor": [1, 1, 1], "roughness": pal["rough"], "metallic": pal["metal"]},
           extra={"effectors": [{"field": "relief", "op": "scale", "blend": "add", "strength": 16.0,
                                 "scaleAxis": [0.0, 1.0, 0.0]}]})
    gf = relief_program(pal, "rdFloor")
    s.program(gf.doc(base=gf.base, emission=gf.emit, roughness=gf.rough))
    mesh_dir = os.path.join(common.EXAMPLES, "rd-pins", "meshes")
    pos, nrm, uv, idx = common.plane_mesh(3 * size, 3 * size, 64, 64)
    common.write_glb(os.path.join(mesh_dir, "rd-floor.glb"), pos, nrm, uv, idx, "floor")
    s.proc("floor", {"kind": "mesh", "asset": "meshes/rd-floor.glb"},
           material={"program": gf.name, "baseColor": [1, 1, 1], "roughness": 0.8, "metallic": 0.0})
    lights(s, pal, key_dir=[0.55, -0.5, -0.67], fog=0.002)
    s.camera = {"mode": 1, "position": [-10.0, 24.0, 24.0], "target": [2.0, 0.0, -2.0], "fov": 40.0,
                "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=80.0, amp=(4.0, 1.0, 2.0), tamp=(2.0, 0.3, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 35.0, "post/bloom/intensity": 0.3, "post/output/vignette": 0.45,
               "post/grade/contrast": 1.1})
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}
    return s


CONFIGS = {"relief": relief, "bloom": bloom, "pins": pins}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=None)
    ap.add_argument("--palette", default=None)
    ap.add_argument("--autonomous", action="store_true", help="no audio routes or audio fields")
    ap.add_argument("--variant", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--wav")
    ap.add_argument("--camera")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--track", action="append", default=[])
    a = ap.parse_args()
    names = [a.config] if a.config else list(CONFIGS)
    for name in names:
        kwargs = {"autonomous": a.autonomous}
        if a.palette:
            kwargs["pal_name"] = a.palette
        s = CONFIGS[name](**kwargs)
        out_dir = os.path.join(common.EXAMPLES, s.id)
        pp, sp = common.write_example(s, out_dir)
        print(pp)
        if a.variant:
            params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
            print(common.variant(pp, a.out, a.wav, a.camera, params, a.track))


if __name__ == "__main__":
    main()

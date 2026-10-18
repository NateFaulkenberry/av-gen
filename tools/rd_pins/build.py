#!/usr/bin/env python3
"""RD PINS: the generator (docs/prototypes/generative-rd/C-scene-plan.md).

    python3 tools/rd_pins/build.py                                  # writes examples/rd-pins/ + its index entry
    python3 tools/rd_pins/build.py --variant --out <v.json> [--wav <w>] [--set path=json] [--camera ...]

A bowl of copper pins in the dark. A Gray-Scott plane (ADR-1451) grows a labyrinth and spots over the bowl's plan;
each pin rises with the pattern under it, its head turns from oxidised to bright metal on the pattern, and a thin ring
of light marks the pattern's boundary. The music edits the regime map (labyrinth -> spots) rather than the pattern
itself, so the pattern's own growth is the animation.

Proof switches (environment, review only): RP_PIN=box|round, RP_KICK=regime|growth, RP_RES=1 (resonance regime layer),
RP_FLOOR=0 (no floor tint), RP_PALETTE=<n>.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.generative_rd import common  # noqa: E402
from tools.generative_rd.common import R, hexrgb  # noqa: E402

ID = "rd-pins"
TITLE = "RD Pins"
OUT = os.path.join(REPO, "examples", "rd-pins")
PIN = os.environ.get("RP_PIN", "round")
KICK = os.environ.get("RP_KICK", "regime")
RES = bool(os.environ.get("RP_RES"))
FLOOR = os.environ.get("RP_FLOOR", "1") != "0"
KICK_GAIN = float(os.environ.get("RP_KICKGAIN", "1.0"))  # proofs; Stage 2 routes it through Audio Influence
RING = float(os.environ.get("RP_RING", "1.0"))            # proofs: x the palette's ring gain

# ================================================================================================ the world (metres)
SIZE = 88.0            # the pattern's square domain, centred on the bowl
RES_RD = 320           # RD cells across (0.275 m)
N_PINS = int(os.environ.get("RP_N", "220"))  # pins across (0.4 m pitch at 220)
BOWL_R, BOWL_H, BOWL_P = 44.0, 18.0, 2.4   # y = H (r / R)^P: flat floor, walls rising to the rim
CENTRE = (0.0, 0.0, 0.0)


def bowl(x, z):
    r = np.sqrt(np.asarray(x) ** 2 + np.asarray(z) ** 2)
    return BOWL_H * (r / BOWL_R) ** BOWL_P


PIN_H = 0.25           # a pin's rest height (m); the pattern scales it

# ================================================================================================ regimes
REGIMES = {"maze": (0.029, 0.057), "spots": (0.030, 0.062), "coral": (0.055, 0.062), "cells": (0.0367, 0.0649)}
DA, DB = 0.3, 0.15


# ================================================================================================ palettes
ROLES = ["environment", "shadow", "base", "pattern", "ring", "key", "rim"]
PALETTES = [
    # name, environment, shadow, base, pattern, ring, key, rim, tuning
    ("Copper Night", "#120708", "#2a1410", "#3a1d14", "#c8743c", "#ff8a3a", "#ffe2c4", "#6a3cff",
     {"metal": 0.55, "rough": 0.3, "ringGain": 1.4, "keyI": 7.0}),
    ("Verdigris", "#06100f", "#103430", "#1d4a42", "#d08a50", "#5ff2d0", "#fff0dc", "#3fa7b8",
     {"metal": 0.75, "rough": 0.38, "ringGain": 1.1, "keyI": 4.0}),
    ("Garnet and Indigo", "#06051a", "#14103a", "#1d1650", "#c01a36", "#ff5a6a", "#ffd8d0", "#4c3cff",
     {"metal": 0.6, "rough": 0.3, "ringGain": 1.2, "keyI": 3.6}),
    ("Violet and Gold", "#0c0614", "#22123a", "#2e1a4e", "#6a3fa0", "#ffc45a", "#f0e4ff", "#ffb347",
     {"metal": 0.7, "rough": 0.28, "ringGain": 1.0, "keyI": 3.4}),
    ("Bone and Basalt", "#0e0e0f", "#1c1c1e", "#2a2a2c", "#d9cdb4", "#000000", "#fff4e4", "#8fa8c8",
     {"metal": 0.1, "rough": 0.55, "ringGain": 0.0, "keyI": 5.0}),
    ("Ember", "#050404", "#0c0a0a", "#141111", "#2a2220", "#ff6a1a", "#a0a8b8", "#ff8a3a",
     {"metal": 0.5, "rough": 0.4, "ringGain": 2.0, "keyI": 1.4}),
]
START_PALETTE = int(os.environ.get("RP_PALETTE", "0"))
JOURNEY = [0, 1, 2, 3, 4, 5]
CHANGE = {"signals": ["sonic.energy", "sonic.brightness", "sonic.density"], "fast": 3.0, "slow": 24.0,
          "threshold": 0.06, "rearm": 0.03, "primaryTimeout": 30.0, "maxHold": 90.0}
P0 = dict(zip(ROLES, PALETTES[START_PALETTE][1:8]), **PALETTES[START_PALETTE][8])


def C(role, s=1.0):
    return hexrgb(P0[role], s)


def palette_states():
    return [{"name": row[0], "colors": {r: hexrgb(h) for r, h in zip(ROLES, row[1:8])},
             "scalars": {k: float(v) for k, v in row[8].items()}} for row in PALETTES]


# ================================================================================================ the pattern
def pattern(s):
    """The RD plane and its regime map (baked once a frame, ADR-1407)."""
    # the regime: a slow wandering pool of spots inside the labyrinth, its shore kept moving by a drifting fbm
    s.field("pool", {"kind": "radial", "radius": 22.0, "strength": 0.9}, position=(6.0, 0.0, -6.0))
    common.lissajous_track(s, "field/pool/position", (4.0, 0.0, -4.0), (14.0, 10.0), 110.0, ratio=(1, 2))
    s.field("drift", {"kind": "noise", "frequency": 0.05, "speed": 0.1, "seed": 21, "strength": 0.7})
    children = ["pool", "drift"]
    if KICK == "regime":
        # a kick's ring flips the regime it passes toward spots for as long as it lasts: spots spread in rings
        s.field("kickRegime", {"kind": "onset", "onsetSource": "low", "point": [0.0, 0.0, 0.0],
                               "waveGeometry": "spherical", "audioSpeed": 7.0, "onsetWidth": 3.0,
                               "onsetDecay": 0.35, "strength": KICK_GAIN})
        children.append("kickRegime")
    if RES:
        # option (proof Q5): the resonance field's settled figure (sand, ADR-1405) as a slower regime layer, so the
        # music's standing figure is written into the pattern family
        s.field("resonator", {"kind": "resonance", "seed": 7, "resonance": {
            "modes": 10.0, "fLow": 40.0, "fHigh": 4000.0, "lambdaMax": 30.0, "lambdaMin": 6.0, "selectivity": 3.0,
            "contrast": 0.6, "persistence": 2.8, "buildup": 0.3, "memory": 2.5, "sustain": 1.0, "transient": 0.6,
            "idle": 0.5, "breath": 0.1, "limit": 1.2, "competition": 2.0,
            "sources": [{"kind": "point", "position": [0.0, 0.0, 0.0], "order": 4, "bandLow": 0.0, "bandHigh": 0.6,
                         "reach": 90.0, "gain": 1.0, "amplitude": 1.0, "drift": 8.0, "driftPeriod": 90.0,
                         "spin": 0.02}],
            "channel": "displacement"}})
        s.field("sand", {"kind": "resonance", "reference": "resonator", "strength": 0.8, "resonance": {
            "channel": "sand", "sharpness": 0.8, "sandNormalized": True}})
        children.append("sand")
    s.field("regime", {"kind": "compound", "combine": "add", "children": children})
    s.grid(name="regimeBake", mode="bake", wrap="wrap", resolution=[128, 1, 128],
           boundsMin=[-SIZE / 2, -2.0, -SIZE / 2], boundsMax=[SIZE / 2, 2.0, SIZE / 2], injectField="regime")
    s.grid_reader("regimeMap", "regimeBake")
    # the seeds: a slow brush, four MIDI seed spheres and (growth mode) the kick's ring of B
    s.field("brush", {"kind": "sphere", "radius": 1.5, "softness": 0.8}, position=(0.0, 0.0, 0.0))
    common.lissajous_track(s, "field/brush/position", (0.0, 0.0, 0.0), (24.0, 18.0), 67.0, ratio=(2, 1))
    seeds = ["brush"]
    for i, (x, z) in enumerate(((-18.0, -14.0), (16.0, -20.0), (20.0, 12.0), (-14.0, 18.0))):
        n = "seed%d" % i
        s.field(n, {"kind": "sphere", "radius": 1.8, "softness": 0.8, "strength": 0.0}, position=(x, 0.0, z))
        seeds.append(n)
    if KICK == "growth":
        s.field("kickGrowth", {"kind": "onset", "onsetSource": "low", "point": [0.0, 0.0, 0.0],
                               "waveGeometry": "spherical", "audioSpeed": 10.0, "onsetWidth": 0.9,
                               "onsetDecay": 0.5, "strength": KICK_GAIN})
        seeds.append("kickGrowth")
    s.field("sources", {"kind": "compound", "combine": "add", "children": seeds[:4]})
    if len(seeds) > 4:
        s.field("sources2", {"kind": "compound", "combine": "add", "children": seeds[4:]})
        s.field("sourcesAll", {"kind": "compound", "combine": "add", "children": ["sources", "sources2"]})
    fa, ka = REGIMES["maze"]
    fb, kb = REGIMES["spots"]
    s.grid(name="pattern", mode="reactionDiffusion", wrap="wrap", resolution=[RES_RD, 1, RES_RD],
           boundsMin=[-SIZE / 2, -2.0, -SIZE / 2], boundsMax=[SIZE / 2, 2.0, SIZE / 2],
           diffusionA=DA, diffusionB=DB, feed=fa, kill=ka, regimeField="regimeMap", feedB=fb, killB=kb,
           injectField="sourcesAll" if len(seeds) > 4 else "sources", injectRate=1.5,
           simRate=60.0, maxSubSteps=4, speed=60.0 * 12, iterations=12, seed=17, seedAmount=1.0,
           checkpointInterval=5.0)
    s.grid_reader("vee", "pattern")
    # the bowl's height for the pins (an authored plane grid, ADR-1282/1452)
    n = 256
    xs = (np.arange(n) + 0.5) / n * SIZE - SIZE / 2
    X, Z = np.meshgrid(xs, xs)
    s.grid(name="bowlHeight", mode="scalar", wrap="clamp", resolution=[n, 1, n],
           boundsMin=[-SIZE / 2, -1.0, -SIZE / 2], boundsMax=[SIZE / 2, 1.0, SIZE / 2], simRate=60.0,
           maxSubSteps=1, initial=[round(float(v), 3) for v in bowl(X, Z).reshape(-1)])
    s.grid_reader("bowlY", "bowlHeight")


# ================================================================================================ the programs
def pin_program():
    """A pin: oxidised (shadow -> base) where the pattern is low, bright metal (pattern) where it is high; a thin ring of
    light at the head where the pin stands on the pattern's boundary; the kick's ring as a passing glint."""
    g = common.Prog("rpPin")
    g.op("vee", "field", 0, field="vee")                                # r0 = B
    g.op(None, "smoothstep", 1, a=0, k=[0.06, 0.24, 0, 0])               # r1 = on the pattern
    g.op("base", "constant", 2, k=C("base") + [1])
    g.op("pattern", "constant", 3, k=C("pattern") + [1])
    g.op(None, "mixBy", 4, a=2, b=3, c=1)                                # head colour
    g.op(None, "input", 5, inp="localPosition")
    g.op(None, "swizzle", 5, a=5, k=[1, 1, 1, 1])                        # local y: -PIN_H/2 .. PIN_H/2 along the pin
    g.op(None, "smoothstep", 6, a=5, k=[0.1 * PIN_H, 0.45 * PIN_H, 0, 0])  # r6 = the head
    g.op("shadow", "constant", 7, k=C("shadow") + [1])
    g.op(None, "mixBy", 4, a=7, b=4, c=6)                                # shaft dark, head lit
    # the ring: B near the boundary isoline, at the head
    g.op(None, "remap", 1, a=0, k=[0.17, 0.27, 0.0, 1.0])
    g.op(None, "multiply", 1, a=1, b=1)
    g.op(None, "remap", 1, a=1, k=[0.0, 1.0, 1.0, 0.0], f=1.0)
    g.op(None, "power", 1, a=1, f=6.0)
    g.op(None, "multiply", 1, a=1, b=6)
    g.op("ring", "constant", 2, k=C("ring", P0["ringGain"] * RING) + [1])
    g.op(None, "multiply", 1, a=1, b=2)                                  # r1 = emission
    if KICK == "regime" and os.environ.get("RP_GLINT", "1") != "0":
        g.op("kick", "field", 2, field="kickRegime")                    # the passing ring: a glint on the heads
        g.op(None, "multiply", 2, a=2, b=6)
        g.op("kickCol", "constant", 3, k=C("ring", 0.35) + [1])
        g.op(None, "multiply", 2, a=2, b=3)
        g.op(None, "add", 1, a=1, b=2)
    g.op("rough", "constant", 7, k=[P0["rough"]] * 3 + [1])
    g.base, g.emit, g.rough = 4, 1, 7
    return g


def floor_program():
    """The floor between the pins: the environment colour, lifted toward the pattern colour where the pattern is (a dim
    reflection of the field above it), so the darks carry the pattern instead of black."""
    g = common.Prog("rpFloor")
    g.op(None, "field", 0, field="vee")
    g.op(None, "smoothstep", 0, a=0, k=[0.05, 0.3, 0, 0])
    g.op("env", "constant", 1, k=C("environment", 2.0) + [1])
    g.op("tint", "constant", 2, k=C("base") + [1])
    g.op(None, "mixBy", 3, a=1, b=2, c=0)
    g.op("glow", "constant", 4, k=C("ring", 0.04) + [1])
    g.op(None, "multiply", 4, a=4, b=0)
    g.base, g.emit = 3, 4
    return g


# ================================================================================================ the scene
DESIGN = {
    "category": "experiment",
    "thesis": "A bowl of copper pins in the dark. A reaction-diffusion pattern grows over it and lifts every pin it "
              "touches; a labyrinth wanders into spots and back; a thin ring of light runs along the pattern's edges. "
              "The music never draws on it directly: kicks send rings that turn the labyrinth to spots, held sound "
              "widens the spots, bass lifts the field.",
    "experiment": "C reaction-diffusion (production)",
}


def environment(s):
    s.environment = {
        "intensity": 0.08, "background": C("environment"), "fogColor": C("environment"),
        "volumeDensity": 0.0, "fogSky": 0.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0,
        "shadowRange": 120.0, "ambientOcclusion": False, "contactShadows": False,
        "depthPrepass": os.environ.get("RP_PREPASS", "0") == "1",
        "sky": {"enabled": True, "zenithColor": C("environment", 1.5), "horizonColor": C("shadow", 1.2),
                "groundColor": C("environment"), "haze": 1.0, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    # a warm key grazing across the bowl, a cool rim from behind, no shadows (the pins' own darkening does depth)
    s.light("key", "directional", direction=[0.62, -0.55, -0.56], color=C("key"), intensity=P0["keyI"],
            castsShadow=False, contactShadow=False, softness=0.3)
    s.light("rim", "directional", direction=[-0.45, -0.3, 0.84], color=C("rim"), intensity=1.4,
            castsShadow=False, contactShadow=False)


def build():
    s = common.Scene(ID, TITLE, DESIGN)
    pattern(s)
    g = pin_program()
    s.program(g.doc(base=g.base, emission=g.emit, roughness=g.rough))
    s.pin_program = g
    sp = SIZE / N_PINS
    if PIN == "round":
        src = {"kind": "cylinder", "radius": 0.36 * sp, "height": PIN_H, "radialSegments": 10, "heightSegments": 1,
               "caps": True, "bevel": 0.05, "bevelSegments": 2}
    else:
        src = {"kind": "box", "size": [0.72 * sp, PIN_H, 0.72 * sp]}
    s.proc("pins", src,
           distribution={"kind": "grid", "gridCount": [N_PINS, 1, N_PINS], "gridSpacing": [sp, 1.0, sp]},
           material={"program": g.name, "baseColor": [1, 1, 1], "roughness": P0["rough"], "metallic": P0["metal"]},
           extra={"effectors": [{"field": "bowlY", "op": "positionOffset", "blend": "add", "strength": 1.0,
                                 "axis": [0.0, 1.0, 0.0]},
                                {"field": "vee", "op": "scale", "blend": "add", "strength": 6.0,
                                 "scaleAxis": [0.0, 1.0, 0.0]}]})
    gf = floor_program()
    s.program(gf.doc(base=gf.base, emission=gf.emit))
    pos, nrm, uv, idx = common.bowl_mesh(BOWL_R * 1.45, BOWL_H * 1.45 ** BOWL_P, 160, 240, power=BOWL_P)
    common.write_glb(os.path.join(OUT, "meshes", "rp-bowl.glb"), pos, nrm, uv, idx, "bowl")
    s.proc("floor", {"kind": "mesh", "asset": "meshes/rp-bowl.glb"}, position=(0.0, -0.05, 0.0),
           material={"program": gf.name if FLOOR else None, "baseColor": C("environment", 2.0), "roughness": 0.6,
                     "metallic": 0.0})
    if not FLOOR:
        s.nodes[-1]["procedural"]["material"].pop("program")
    environment(s)
    # on the rim, looking across and down: the far wall rises above the eye line, so the frame is all pins
    s.camera = {"mode": 1, "position": [8.0, 34.0, 40.0], "target": [-4.0, 2.0, -8.0], "fov": 40.0,
                "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=90.0, amp=(4.0, 1.0, 2.0), tamp=(2.0, 0.5, 0.0),
                   steps=8)
    s.params_({"camera/lens/focalLength": 28.0, "post/bloom/intensity": 0.28, "post/output/vignette": 0.4,
               "post/grade/contrast": 1.06, "post/output/grain": 0.01})
    s.response = dict(common.RESPONSE)
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--wav")
    ap.add_argument("--camera")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--track", action="append", default=[])
    a = ap.parse_args()
    s = build()
    pp, sp = s.write(OUT)
    print(pp)
    if a.variant:
        params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
        print(common.variant(pp, a.out, a.wav, a.camera, params, a.track))


if __name__ == "__main__":
    main()

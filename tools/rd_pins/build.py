#!/usr/bin/env python3
"""RD PINS: the generator (docs/prototypes/generative-rd/C-scene-plan.md; the art pass:
docs/prototypes/generative-pass/01-plan.md).

    python3 tools/rd_pins/build.py                                  # writes examples/rd-pins/ + its index entry
    python3 tools/rd_pins/build.py --variant --out <v.json> [--wav <w>] [--set path=json] [--camera ...]

A bowl of pins in the dark: a living, illuminated topographic interference field. A Gray-Scott plane (ADR-1451) grows a
labyrinth and spots over the bowl's plan; each pin rises with the pattern under it, and the pattern's boundary is a
contour of light: a fine hot core and a softer coloured halo, over dark channels. On top of the pattern the field is
excited by waves of different scales and times:
  - a broad swell travelling across the bowl (a sine heave, the bass level its height) and a second, crossing it;
  - ripples (a shorter, faster wave) that move the contours themselves: they bunch and open as it passes, and where
    its crest crosses a contour a bead of light runs along it (the hats sharpen it);
  - the bass history rolling out from the bowl's heart (a spectrum field laid out at 14 m/s): a lift and a pool of
    light where it is loud, so loud passages reshape the field and quiet ones let it settle;
  - a kick: a ring of impact from the heart (the pins jump, the contours swell, a band of light runs ahead of it),
    which also turns the labyrinth toward spots as it passes; a snare: a quicker ring from off-centre.

Proof switches (environment, review only): RP_PIN=box|round, RP_RES=1 (resonance regime layer), RP_FLOOR=1 (the floor
tint), RP_PALETTE=<n>.
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
PIN = os.environ.get("RP_PIN", "box")  # round pins cost 2.2x and look the same at this distance (plan, Q2)
RES = bool(os.environ.get("RP_RES"))
FLOOR = os.environ.get("RP_FLOOR", "0") != "0"   # off: the pins heave now, and a floor carrying the pattern
# beneath them showed through as a ghost of the contours at the wrong height
KICK_GAIN = float(os.environ.get("RP_KICKGAIN", "1.0"))  # proofs; Stage 2 routes it through Audio Influence
RING = float(os.environ.get("RP_RING", "1.0"))            # proofs: x the palette's ring gain

# ================================================================================================ the world (metres)
SIZE = 88.0            # the pattern's square domain, centred on the bowl
RES_RD = 320           # RD cells across (0.275 m)
N_PINS = int(os.environ.get("RP_N", "190"))  # pins across (0.46 m pitch)
BOWL_R, BOWL_H, BOWL_P = 44.0, 18.0, 2.4   # y = H (r / R)^P: flat floor, walls rising to the rim
CENTRE = (0.0, 0.0, 0.0)


def bowl(x, z):
    r = np.sqrt(np.asarray(x) ** 2 + np.asarray(z) ** 2)
    return BOWL_H * (r / BOWL_R) ** BOWL_P


PIN_H = 0.25           # a pin's rest height (m); the pattern scales it
KICK_AT = (0.0, 0.0, -2.0)     # the bowl's heart, where a kick lands and the bass rolls out from (in frame)
SNARE_AT = (15.0, 0.0, -17.0)  # the snare's ring: off-centre, upper right of the frame

# The waves (world space): (axis, wavelength m, speed m/s, phase). A is the broad swell (the bass level is its height),
# B crosses it; RIPPLE is shorter and faster, and the pin program reproduces it exactly (the same sine of the same
# world position and clock) to move the contours and run beads of light along them.
WAVE_A = ((0.8, 0.0, 0.6), 38.0, 4.2, 0.0)
WAVE_B = ((-0.45, 0.0, 0.89), 27.0, 2.9, 1.3)
RIPPLE = ((0.28, 0.0, -0.96), 9.5, 3.6, 0.4)
GRAD_SCALE = 1.0 / 200.0       # the program's gradient op saturates: world dot axis * this + 0.5 stays inside 0..1

# ================================================================================================ regimes
REGIMES = {"maze": (0.029, 0.057), "spots": (0.030, 0.062), "coral": (0.055, 0.062), "cells": (0.0367, 0.0649)}
# the labyrinth family: coral, not maze. Maze and spots differ by kill only, and spots survive in maze territory, so
# once the front had passed, the spots stayed and the frame went to one field of rings (measured over 96 s); coral
# stripes invade spots, so the two families trade ground both ways as the front sweeps.
LABYRINTH = os.environ.get("RP_LAB", "coral")
DA, DB = 0.3, 0.15


# ================================================================================================ palettes
ROLES = ["environment", "shadow", "base", "pattern", "ring", "hot", "key", "rim"]
PALETTES = [
    # name, environment, shadow, base, pattern, ring (the contour's halo: the dominant colour), hot (its core and the
    # glints: the controlled highlight), key, rim, tuning (the finish, the lights, the bloom's character). Each is a
    # coordinated set: a dark foundation in its own hue (never black), a dominant contour, a brighter accent, a
    # highlight that stays in the family, and its own bloom.
    ("Copper Night", "#2a0c16", "#3e1420", "#46181a", "#6a2e1a", "#ff8a30", "#fff0cc", "#ffe2c4", "#7a4cff",
     {"metal": 0.55, "rough": 0.3, "ringGain": 1.6, "hotGain": 4.0, "keyI": 3.6, "rimI": 1.4, "bloomI": 0.5}),
    ("Verdigris Reef", "#021a1e", "#03282c", "#063a3a", "#126858", "#4cffc8", "#e8fff6", "#e4fff6", "#ff6a8a",
     {"metal": 0.45, "rough": 0.34, "ringGain": 1.3, "hotGain": 3.2, "keyI": 3.4, "rimI": 1.6, "bloomI": 0.36}),
    ("Garnet and Indigo", "#120c46", "#1a125e", "#1c0e66", "#701030", "#ff3a5e", "#ffdce6", "#ffd8e4", "#3a7cff",
     {"metal": 0.5, "rough": 0.3, "ringGain": 1.5, "hotGain": 3.6, "keyI": 3.2, "rimI": 1.8, "bloomI": 0.44}),
    ("Violet and Gold", "#1e0838", "#2c1052", "#30125c", "#58248a", "#ffbe3a", "#fff4c8", "#f6ecff", "#ff4ac0",
     {"metal": 0.6, "rough": 0.28, "ringGain": 1.4, "hotGain": 3.6, "keyI": 3.0, "rimI": 1.6, "bloomI": 0.42}),
    ("Electric Lagoon", "#01142c", "#022440", "#03365e", "#085a94", "#d8ff2a", "#f8ffd0", "#e6f6ff", "#ff3ad0",
     {"metal": 0.4, "rough": 0.32, "ringGain": 1.5, "hotGain": 3.4, "keyI": 3.4, "rimI": 1.8, "bloomI": 0.4}),
    ("Rose Quartz", "#2a0c1e", "#3c142c", "#4c1c3a", "#96466a", "#ffb0c8", "#fff8ec", "#fff0f4", "#5ac8ff",
     {"metal": 0.3, "rough": 0.4, "ringGain": 1.3, "hotGain": 3.0, "keyI": 3.6, "rimI": 1.6, "bloomI": 0.32}),
]
START_PALETTE = int(os.environ.get("RP_PALETTE", "0"))
JOURNEY = [0, 4, 3, 1, 5, 2]
CHANGE = {"signals": ["sonic.energy", "sonic.brightness", "sonic.density"], "fast": 3.0, "slow": 24.0,
          "threshold": 0.06, "rearm": 0.03, "primaryTimeout": 30.0, "maxHold": 90.0}
P0 = dict(zip(ROLES, PALETTES[START_PALETTE][1:9]), **PALETTES[START_PALETTE][9])


def C(role, s=1.0):
    return hexrgb(P0[role], s)


def palette_states():
    return [{"name": row[0], "colors": {r: hexrgb(h) for r, h in zip(ROLES, row[1:9])},
             "scalars": {k: float(v) for k, v in row[9].items()}} for row in PALETTES]


# ================================================================================================ the pattern
PREROLL = os.environ.get("RP_PREROLL", "1") != "0"


def preroll(front_at, steps=3000, seed=17):
    """The pattern the scene opens on (ADR-1453): the same Gray-Scott model (9-point plane stencil, D 0.3 / 0.15, unit
    steps, wrap) run in numpy from seeded blobs under the regime at t = 0 (the front where its track starts; the pool
    and fbm left out). About 3000 steps: what the live scene would show after ~4 s of its own growth past formation.
    Deterministic in `seed`; written to 3 decimals (about 1.3 MB)."""
    n = RES_RD
    xs = (np.arange(n) + 0.5) / n * SIZE - SIZE / 2
    X, Z = np.meshgrid(xs, xs)  # rows are z (k), columns x (i): the grid's storage order for a plane
    ax = np.array([0.8, 0.0, 0.6])
    d = (X - front_at[0]) * ax[0] + (Z - front_at[2]) * ax[2]
    w = np.clip(d / 30.0 + 0.5, 0.0, 1.0)
    fa, ka = REGIMES[LABYRINTH]
    fb, kb = REGIMES["spots"]
    F = fa + (fb - fa) * w
    K = ka + (kb - ka) * w
    rng = np.random.default_rng(seed)
    A = np.ones((n, n))
    B = np.zeros((n, n))
    for _ in range(120):
        i, k = rng.integers(0, n, 2)
        A[np.ix_((np.arange(k - 6, k + 6)) % n, (np.arange(i - 6, i + 6)) % n)] = 0.5
        B[np.ix_((np.arange(k - 6, k + 6)) % n, (np.arange(i - 6, i + 6)) % n)] = 0.25

    def lap(x):
        a = np.roll(x, 1, 0) + np.roll(x, -1, 0) + np.roll(x, 1, 1) + np.roll(x, -1, 1)
        dg = (np.roll(np.roll(x, 1, 0), 1, 1) + np.roll(np.roll(x, 1, 0), -1, 1) + np.roll(np.roll(x, -1, 0), 1, 1)
              + np.roll(np.roll(x, -1, 0), -1, 1))
        return (4 * a + dg - 20 * x) / 6

    for _ in range(steps):
        r = A * B * B
        A, B = (np.clip(A + (DA * lap(A) - r + F * (1 - A)), 0, 1),
                np.clip(B + (DB * lap(B) + r - (K + F) * B), 0, 1))
    return [round(float(v), 3) for v in np.stack([A, B], axis=-1).reshape(-1)]


def pattern(s):
    """The RD plane and its regime map (baked once a frame, ADR-1407)."""
    # the regime (0 labyrinth .. 1 spots): the boundary between the two families is the interesting part, so it is
    # designed to stay in frame and keep moving (coordinator, 2026-10-17):
    #   front  a soft linear front (30 m wide) that sweeps back and forth across the bowl's middle over 96 s, so
    #          labyrinth and spot territory trade places every phrase or two
    #   pool   an island of spots orbiting through the labyrinth side (and of labyrinth through the spot side, where
    #          it adds past 1 and clamps: it reads only on the labyrinth side)
    #   drift  a slow fbm that keeps the front's line irregular
    ax = (0.8, 0.0, 0.6)
    s.field("front", {"kind": "linearGradient", "axis": list(ax), "length": 30.0, "strength": 1.0},
            position=(0.0, 0.0, 0.0))
    keys = []
    for i in range(13):
        t = 96.0 * i / 12
        d = 20.0 * math.sin(2 * math.pi * t / 96.0) + 4.0 * math.sin(2 * math.pi * 3 * t / 96.0)
        keys.append({"time": round(t, 3), "value": [round(-3.0 + d * ax[0], 3), 0.0, round(-6.0 + d * ax[2], 3)],
                     "interp": "smooth"})
    s.track("field/front/position", keys, loop=96.0)
    s.field("pool", {"kind": "radial", "radius": 12.0, "strength": 0.7}, position=(0.0, 0.0, 0.0))
    common.lissajous_track(s, "field/pool/position", (-2.0, 0.0, -4.0), (18.0, 14.0), 70.0, ratio=(1, 2))
    s.field("drift", {"kind": "noise", "frequency": 0.06, "speed": 0.12, "seed": 21, "strength": 0.4})
    children = ["front", "pool", "drift"]
    # THE KICK: a ring of impact travelling out from the bowl's heart. One field, three uses: it flips the regime it
    # passes toward spots (spots spread in rings), it lifts the pins it passes (an effector) and it swells the contours
    # and carries a band of light (the pin program)
    s.field("kick", {"kind": "onset", "onsetSource": "low", "point": list(KICK_AT), "waveGeometry": "spherical",
                     "audioSpeed": 18.0, "onsetWidth": 3.0, "onsetDecay": 1.2, "strength": KICK_GAIN})
    children.append("kick")
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
    # three MIDI seeds (the GPU has 16 field slots; four seeds and a second compound pushed the pins' own fields out)
    for i, (x, z) in enumerate(((-18.0, -14.0), (16.0, -20.0), (14.0, 14.0))):
        n = "seed%d" % i
        s.field(n, {"kind": "sphere", "radius": 1.8, "softness": 0.8, "strength": 0.0}, position=(x, 0.0, z))
        seeds.append(n)
    s.field("sources", {"kind": "compound", "combine": "add", "children": seeds[:4]})
    if len(seeds) > 4:
        s.field("sources2", {"kind": "compound", "combine": "add", "children": seeds[4:]})
        s.field("sourcesAll", {"kind": "compound", "combine": "add", "children": ["sources", "sources2"]})
    fa, ka = REGIMES[LABYRINTH]
    fb, kb = REGIMES["spots"]
    s.grid(name="pattern", mode="reactionDiffusion", wrap="wrap", resolution=[RES_RD, 1, RES_RD],
           boundsMin=[-SIZE / 2, -2.0, -SIZE / 2], boundsMax=[SIZE / 2, 2.0, SIZE / 2],
           diffusionA=DA, diffusionB=DB, feed=fa, kill=ka, regimeField="regimeMap", feedB=fb, killB=kb,
           injectField="sourcesAll" if len(seeds) > 4 else "sources", injectRate=1.5,
           simRate=60.0, maxSubSteps=4, speed=60.0 * 12, iterations=12, seed=17, seedAmount=1.0,
           checkpointInterval=5.0)
    if PREROLL:
        s.grids[-1]["initial"] = preroll(keys[0]["value"])  # ADR-1453: open on a formed pattern
    s.grid_reader("vee", "pattern")
    # the bowl's height for the pins (an authored plane grid, ADR-1282/1452)
    n = 256
    xs = (np.arange(n) + 0.5) / n * SIZE - SIZE / 2
    X, Z = np.meshgrid(xs, xs)
    s.grid(name="bowlHeight", mode="scalar", wrap="clamp", resolution=[n, 1, n],
           boundsMin=[-SIZE / 2, -1.0, -SIZE / 2], boundsMax=[SIZE / 2, 1.0, SIZE / 2], simRate=60.0,
           maxSubSteps=1, initial=[round(float(v), 3) for v in bowl(X, Z).reshape(-1)])
    s.grid_reader("bowlY", "bowlHeight")
    # THE SNARE: a quicker, narrower ring from off-centre (light and a small lift)
    s.field("snare", {"kind": "onset", "onsetSource": "mid", "point": list(SNARE_AT), "waveGeometry": "spherical",
                      "audioSpeed": 16.0, "onsetWidth": 1.6, "onsetDecay": 1.3, "strength": 1.0})
    # THE BASS HISTORY: the bass band's last seconds laid out from the heart at 14 m/s (ADR-1116), smoothed over a third
    # of a second (ADR-1370): a broad, music-shaped disturbance travelling outward, metres of lift where it is loud
    s.field("swell", {"kind": "spectrum", "audioBand": "range", "bandLow": 0.0, "bandHigh": 0.16,
                      "point": list(KICK_AT), "waveGeometry": "spherical", "audioSpeed": 14.0, "audioSmooth": 0.15,
                      "strength": 1.0})


# ================================================================================================ the programs
def ripple_palette():
    """The pin program's ripple: cos(2 pi (k3 (g + c t) + k4)) with g = saturate(dot(p, axis) * GRAD_SCALE + 0.5) is
    exactly sin(dot(p, axis) * 2 pi / wavelength + speed_rad * t + phase), the deformer's sine (procedural.cpp,
    applySine): k3 = freq / (2 pi s), c = speed_rad s / freq, k4 = (phase - pi / 2) / (2 pi) - k3 / 2."""
    axis, wl, v, ph = RIPPLE
    freq = 2 * math.pi / wl
    k3 = freq / (2 * math.pi * GRAD_SCALE)
    c = (freq * v) * GRAD_SCALE / freq
    k4 = (ph - math.pi / 2) / (2 * math.pi) - 0.5 * k3
    return axis, k3, c, k4


def pin_program():
    """A pin. The pattern's boundary is a contour of light; everything else is the dark it sits in.
    - S = B + the ripple (a shift of the contour level, so contours bunch and open as the ripple passes) + the kick's
      ring (the contours swell as it passes).
    - The contour: d = (S - 0.2) / 0.09; a halo (1 - d^2)^2 in the palette's `ring` and a fine core (1 - d^2)^12 in
      its `hot`: a bright, fine line with a softer edge, sharp at native resolution, on the pin heads only.
    - The contour's light is not uniform: a floor, where the ripple's crest meets the bass history's pool a hot spot
      (the two wave systems interfering), the kick's ring a band of light, the snare's a thinner one, and the ripple's
      sharp crest running beads of light along every contour it crosses, gated by the hats.
    - Heads on the pattern are dark polished `pattern` metal, off it low `base`; shafts fall to `shadow`."""
    g = common.Prog("rpPin")
    ax, k3, c, k4 = ripple_palette()
    g.op("vee", "field", 0, field="vee")                                 # r0 = B
    g.op(None, "input", 5, inp="worldPosition")
    g.op(None, "gradient", 1, a=5, k=[ax[0], ax[1], ax[2], 0.5], f=GRAD_SCALE)
    g.op(None, "input", 6, inp="time")
    g.op(None, "remap", 6, a=6, k=[0.0, 1.0, 0.0, c])                    # c t
    g.op(None, "add", 1, a=1, b=6)
    g.op("ripple", "palette", 1, a=1, k=[0, 0, 0, 0], k2=[1, 1, 1, 0], k3=[k3, k3, k3, 0], k4=[k4, k4, k4, 0])  # r1 = sine
    g.op(None, "field", 2, field="kick")                                 # r2 = the kick's ring
    g.op(None, "field", 3, field="swell")                                # r3 = the bass history (0..1-ish)
    g.op(None, "field", 4, field="snare")                                # r4 = the snare's ring
    # S = B + shift
    g.op("rippleShift", "constant", 6, k=[0.035, 0.035, 0.035, 0])       # the ripple's reach into the contours (mids)
    g.op(None, "multiply", 6, a=1, b=6)
    g.op(None, "remap", 7, a=2, k=[0.0, 0.5, 0.0, 0.03], f=1.0)          # the kick swells the contours (bounded)
    g.op(None, "add", 6, a=6, b=7)
    g.op(None, "add", 6, a=0, b=6)                                       # r6 = S
    # the contour
    g.op(None, "remap", 7, a=6, k=[0.11, 0.29, -1.0, 1.0])               # d
    g.op(None, "multiply", 7, a=7, b=7)
    g.op(None, "remap", 7, a=7, k=[0.0, 1.0, 1.0, 0.0], f=1.0)           # 1 - d^2, 0 outside
    g.op(None, "power", 6, a=7, f=12.0)                                  # r6 = core
    g.op(None, "multiply", 7, a=7, b=7)                                  # r7 = halo
    g.op(None, "input", 5, inp="localPosition")
    g.op(None, "gradient", 5, a=5, k=[0.0, 1.0, 0.0, -0.1 / 0.35], f=1.0 / (0.35 * PIN_H))  # r5 = the head
    g.op(None, "multiply", 6, a=6, b=5)
    g.op(None, "multiply", 7, a=7, b=5)
    # the light along the contour: r1 becomes the intensity
    g.op(None, "remap", 1, a=1, k=[0.3, 1.0, 0.0, 1.0], f=1.0)           # the ripple's crest
    g.op(None, "power", 0, a=1, f=10.0)                                  # its sharp crest (r0 = B is spent below first)
    g.op("glint", "constant", 2, k=[0.25, 0.25, 0.25, 0])                # (routes: the hats, CC 11) -- r2 kick reused below
    g.op(None, "multiply", 0, a=0, b=2)                                  # r0 = beads (gated)
    g.op(None, "remap", 3, a=3, k=[0.15, 0.75, 0.0, 1.0], f=1.0)         # the bass pool
    g.op(None, "multiply", 1, a=1, b=3)                                  # crest x pool: the interference
    g.op(None, "remap", 1, a=1, k=[0.0, 1.0, 0.22, 3.2])                 # floor 0.22: most contours stay low
    g.op(None, "add", 1, a=1, b=0)
    g.op(None, "field", 2, field="kick")
    g.op(None, "remap", 2, a=2, k=[0.02, 0.45, 0.0, 2.6], f=1.0)         # the kick's band of light (onset rings are
                                                                         # 0.1-0.5 at a kick's crest, measured)
    g.op(None, "add", 1, a=1, b=2)
    g.op(None, "remap", 4, a=4, k=[0.02, 0.4, 0.0, 1.4], f=1.0)          # the snare's
    g.op(None, "add", 1, a=1, b=4)                                       # r1 = intensity
    g.op("ring", "constant", 2, k=C("ring", P0["ringGain"] * RING) + [1])  # the halo: palette ring x ringGain
    g.op(None, "multiply", 7, a=7, b=2)
    g.op("hot", "constant", 3, k=C("hot", P0["hotGain"]) + [1])          # the core: palette hot x hotGain
    g.op(None, "multiply", 6, a=6, b=3)
    g.op(None, "add", 6, a=6, b=7)
    g.op(None, "multiply", 6, a=6, b=1)                                  # r6 = emission
    # the base: B again (r0 was spent)
    g.op(None, "field", 0, field="vee")
    g.op(None, "smoothstep", 1, a=0, k=[0.06, 0.24, 0, 0])               # on the pattern
    g.op("baseRamp", "ramp", 2, a=1, k=C("base") + [1], k2=C("base") + [1], k3=C("pattern") + [1])
    g.op("shadow", "constant", 3, k=C("shadow") + [1])
    g.op(None, "mixBy", 2, a=3, b=2, c=5)                                # shaft dark, head lit
    g.base, g.emit, g.rough = 2, 6, -1
    return g


def floor_program():
    """The floor between the pins: the environment colour, lifted toward the pattern colour where the pattern is (a dim
    reflection of the field above it), so the darks carry the pattern instead of black."""
    g = common.Prog("rpFloor")
    g.op(None, "field", 0, field="vee")
    g.op(None, "smoothstep", 0, a=0, k=[0.05, 0.3, 0, 0])
    g.op("env", "constant", 1, k=C("environment", 1.6) + [1])
    g.op("tint", "constant", 2, k=C("base") + [1])
    g.op(None, "mixBy", 3, a=1, b=2, c=0)
    g.op("glow", "constant", 4, k=C("ring", 0.03) + [1])
    g.op(None, "multiply", 4, a=4, b=0)
    g.base, g.emit = 3, 4
    return g


# ================================================================================================ the scene
DESIGN = {
    "category": "experiment",
    "thesis": "A living, illuminated topographic interference field: a bowl of pins in the dark, a reaction-"
              "diffusion labyrinth growing over it, its edges drawn as contours of light (a fine hot core, a softer "
              "halo, dark channels between). Broad swells heave the field and cross each other, ripples bunch and open "
              "the contours and run beads of light along them on the hats, the bass history rolls out from the heart "
              "as a lift and a pool of light, and every kick sends a ring of impact across it that swells the "
              "contours, carries a band of light and turns the labyrinth toward spots.",
    "experiment": "C reaction-diffusion (production)",
}


def environment(s):
    s.environment = {
        "intensity": 0.28, "background": C("environment"), "fogColor": C("environment"),
        "volumeDensity": 0.0, "fogSky": 0.0, "fogHeight": 400.0, "fogHeightFalloff": 0.0, "skyIntensity": 1.0,
        "shadowRange": 120.0, "ambientOcclusion": False, "contactShadows": False,
        "depthPrepass": os.environ.get("RP_PREPASS", "1") == "1",  # 75 -> 41 ms (plan, Q2): pins overdraw
        "sky": {"enabled": True, "zenithColor": C("environment", 1.5), "horizonColor": C("shadow", 1.2),
                "groundColor": C("environment"), "haze": 1.0, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    # a warm key grazing across the bowl, a cool rim from behind, no shadows (the pins' own darkening does depth)
    s.light("key", "directional", direction=[0.62, -0.55, -0.56], color=C("key"), intensity=P0["keyI"],
            castsShadow=False, contactShadow=False, softness=0.3)
    s.light("rim", "directional", direction=[-0.45, -0.3, 0.84], color=C("rim"), intensity=P0["rimI"],
            castsShadow=False, contactShadow=False)


MIDI_CC = [
    (1, "pattern", "PATTERN: pushes the field toward spots (the spot pool widens over the labyrinth)"),
    (2, "seeding", "SEEDING: the wandering brush seeds new growth harder"),
    (3, "height", "PIN HEIGHT: how far the pattern lifts the pins"),
    (4, "influence", "AUDIO INFLUENCE: how hard the music drives the field (half at rest)"),
    (5, "erase", "ERASE: starves the pattern (feed falls): regions collapse, and regrow on release"),
    (6, "light", "LIGHT: the contours' light (core and halo)"),
    (7, "waves", "WAVES: how high the broad swells heave the field"),
    (8, "impact", "IMPACT: how hard the kick and snare rings strike"),
    (9, "palette", "PALETTE: choose one of the six (a direct blend to it)"),
    (10, "paletteLock", "PALETTE LOCK: hold the chosen palette"),
    (11, "shimmer", "SHIMMER: the beads of light the ripple runs along the contours (with the hats)"),
]
MIDI_NOTES = [(36, "seed0"), (37, "seed1"), (38, "seed2")]


def instrument(s, g):
    """Each band of the music has its own job, at its own scale and its own time:
      - bass level: the broad swell's height (a slow attack, a long release) and the pattern's lift
      - the bass history: a lift and a pool of light rolling out from the heart (the spectrum field, no route)
      - mids: the ripple's reach into the contours (how far they bunch and open) and its small heave
      - kicks: the ring of impact (lift, contour swell, a band of light, the regime toward spots)
      - snares: a quicker ring of light from off-centre, and a burst of growth at the wandering brush
      - hats: the beads of light the ripple runs along the contours (8 ms in, 260 ms out), never the whole field
      - held sound: the spot pool widens
    Every audio route's depth is Audio Influence (CC 4, half at rest). MIDI: eight handles, the palette pair, three
    seed notes."""
    s.response = dict(common.RESPONSE)
    common.instrument(s, [(n, sig, d) for n, sig, d in MIDI_CC], MIDI_NOTES)
    E = "procedural/pins/effector/2/strength"
    D = "procedural/pins/deform/"
    G = "grid/pattern/"
    ring, hot, glint, shift = g.path("ring"), g.path("hot"), g.path("glint"), g.path("rippleShift")
    A = common.audio
    s.route(R(common.INFLUENCE, "field/kick/strength", 1.0, op="multiply", gain=2.0, offset=0.0,
              attackMs=200, decayMs=200),
            R(common.INFLUENCE, "field/snare/strength", 1.0, op="multiply", gain=2.0, offset=0.0,
              attackMs=200, decayMs=200),
            R(common.INFLUENCE, "field/swell/strength", 1.0, op="multiply", gain=2.0, offset=0.0,
              attackMs=200, decayMs=200),
            A("response.sustain", "field/pool/strength", 0.6, attackMs=3000, decayMs=6000),
            A("response.bass", E, 3.0, attackMs=250, decayMs=1200),
            A("response.bass", D + "1/amount", 0.7, attackMs=400, decayMs=2500),
            A("audio.mid", shift, 1.0, op="multiply", gain=1.6, offset=0.4, attackMs=120, decayMs=700),
            A("audio.mid", D + "3/amount", 0.12, attackMs=120, decayMs=700),
            A("response.hat", glint, 1.0, op="multiply", gain=3.0, offset=0.12, attackMs=8, decayMs=260),
            A("response.hat", ring, 1.0, op="multiply", gain=0.25, offset=1.0, attackMs=30, decayMs=300),
            A("response.snare", "field/brush/strength", 2.0, attackMs=10, decayMs=600))
    CC = common.CC
    s.route(R("control.pattern", "field/pool/strength", 1.2, **CC),
            R("control.seeding", "field/brush/strength", 3.0, **CC),
            R("control.height", E, 6.0, **CC),
            R("control.erase", G + "feed", -0.03, **CC),
            R("control.erase", G + "feedB", -0.02, **CC),
            R("control.light", ring, 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.light", hot, 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.waves", D + "1/amount", 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.waves", D + "2/amount", 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.impact", "field/kick/strength", 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.impact", "field/snare/strength", 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.shimmer", glint, 1.0, op="multiply", gain=1.5, offset=1.0, **CC),
            R("control.palette", "palette/select", float(len(PALETTES) - 1), **CC),
            R("control.paletteLock", "palette/lock", 1.0, **CC))
    for i in range(3):
        s.route(R("control.seed%d" % i, "field/seed%d/strength" % i, 1.0, attackMs=60, decayMs=3000))


def palette_block(g, gf):
    """The project palette (ADR-1043) and its selector (ADR-1420), with the change fallback (ADR-1421). Roles land on
    geometry and light: the pins' program, the floor's, the two lights, the sky and the material's finish."""
    def op(prog, label):
        return prog.path(label)
    b = [
        {"role": "environment", "target": "env/sky/zenithColor", "gain": 1.5},
        {"role": "environment", "target": "env/sky/groundColor", "gain": 1.0},
        {"role": "shadow", "target": "env/sky/horizonColor", "gain": 1.2},
        {"role": "environment", "target": "scene/fogColor"},
        {"role": "environment", "target": op(gf, "env"), "gain": 1.6},
        {"role": "environment", "target": "procedural/floor/material/baseColor", "gain": 0.5},
        {"role": "base", "target": op(gf, "tint")},
        {"role": "ring", "target": op(gf, "glow"), "gain": 0.03},
        {"role": "base", "target": g.path("baseRamp", "constant")},
        {"role": "base", "target": g.path("baseRamp", "constant2")},
        {"role": "pattern", "target": g.path("baseRamp", "constant3")},
        {"role": "shadow", "target": op(g, "shadow")},
        {"role": "ring", "target": op(g, "ring")},
        {"role": "ringGain", "target": op(g, "ring"), "mode": "multiply"},
        {"role": "hot", "target": op(g, "hot")},
        {"role": "hotGain", "target": op(g, "hot"), "mode": "multiply"},
        {"role": "bloomI", "target": "post/bloom/intensity"},
        {"role": "key", "target": "lights/key/color"},
        {"role": "rim", "target": "lights/rim/color"},
        {"role": "keyI", "target": "lights/key/intensity"},
        {"role": "rimI", "target": "lights/rim/intensity"},
        {"role": "metal", "target": "procedural/pins/material/metallic"},
        {"role": "rough", "target": "procedural/pins/material/roughness"},
    ]
    return {"states": palette_states(), "bindings": b, "position": 0.0, "saturation": 1.0, "value": 1.0,
            "selector": {"select": START_PALETTE, "transition": 6.0, "lock": False, "hold": 40.0,
                         "advance": {"signal": "beat.phrasePulse", "threshold": 0.5, "order": JOURNEY,
                                     "change": CHANGE}}}


def waves():
    """The heave: three sine deformers in world space (procedural/pins/deform/1..3): the broad swell A (the bass level
    is its height), B crossing it, and the ripple (the pin program reproduces this one exactly, ripple_palette)."""
    out = []
    for (axis, wl, v, ph), amount in ((WAVE_A, 0.3), (WAVE_B, 0.32), (RIPPLE, 0.05)):
        freq = 2 * math.pi / wl
        out.append({"kind": "sine", "amount": amount, "frequency": round(freq, 6), "speed": round(freq * v, 6),
                    "phase": ph, "axis": list(axis), "displacementAxis": [0.0, 1.0, 0.0], "space": "world",
                    "center": [0.0, 0.0, 0.0]})
    return out


def build():
    s = common.Scene(ID, TITLE, DESIGN)
    pattern(s)
    g = pin_program()
    s.program(g.doc(base=g.base, emission=g.emit))
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
           deformers=waves(),
           extra={"effectors": [{"field": "bowlY", "op": "positionOffset", "blend": "add", "strength": 1.0,
                                 "axis": [0.0, 1.0, 0.0]},
                                {"field": "vee", "op": "scale", "blend": "add", "strength": 6.0,
                                 "scaleAxis": [0.0, 1.0, 0.0]},
                                # the bass history's lift, the kick's and the snare's rings (m)
                                {"field": "swell", "op": "positionOffset", "blend": "add", "strength": 1.4,
                                 "axis": [0.0, 1.0, 0.0]},
                                {"field": "kick", "op": "positionOffset", "blend": "add", "strength": 1.0,
                                 "axis": [0.0, 1.0, 0.0]},
                                {"field": "snare", "op": "positionOffset", "blend": "add", "strength": 0.4,
                                 "axis": [0.0, 1.0, 0.0]}]})
    s.floor_program = gf = floor_program()
    s.program(gf.doc(base=gf.base, emission=gf.emit))
    pos, nrm, uv, idx = common.bowl_mesh(BOWL_R * 1.45, BOWL_H * 1.45 ** BOWL_P, 160, 240, power=BOWL_P)
    common.write_glb(os.path.join(OUT, "meshes", "rp-bowl.glb"), pos, nrm, uv, idx, "bowl")
    s.proc("floor", {"kind": "mesh", "asset": "meshes/rp-bowl.glb"}, position=(0.0, -0.05, 0.0),
           material={"program": gf.name if FLOOR else None, "baseColor": C("environment", 0.5), "roughness": 0.6,
                     "metallic": 0.0})
    if not FLOOR:
        s.nodes[-1]["procedural"]["material"].pop("program")
    environment(s)
    s.palette = palette_block(g, gf)
    # on the rim, looking across and down: the far wall rises above the eye line, so the frame is all pins
    s.camera = {"mode": 1, "position": [10.0, 31.0, 31.0], "target": [-1.0, 0.0, -5.0], "fov": 40.0,
                "orbitSpeed": 0.0}
    s.drift_camera(s.camera["position"], s.camera["target"], period=90.0, amp=(4.0, 1.0, 2.0), tamp=(2.0, 0.5, 0.0),
                   steps=8)
    # bloom on the hot cores only (exposed units: the halo stays under the threshold), a tight radius so the glow
    # spreads past a contour without filling the channel beside it; a light vignette, no grain
    s.params_({"camera/lens/focalLength": 28.0, "post/bloom/intensity": P0["bloomI"], "post/bloom/threshold": 1.1,
               "post/bloom/knee": 0.6, "post/bloom/radius": 0.75, "post/output/vignette": 0.22,
               "post/grade/contrast": 1.08, "post/output/grain": 0.0})
    instrument(s, g)
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
    pp, sp = common.write_example(s, OUT)
    print(pp)
    cc = common.describe_ccs(MIDI_CC, "notes 36-38 hold seeds")
    common.index_entry(TITLE, "rd-pins/rd-pins.json",
                       "Play live audio (any mix) and, optionally, a MIDI controller: " + cc + ". " + DESIGN["thesis"]
                       + " See docs/prototypes/generative-rd/C-scene-plan.md.", live=True, category="Generative")
    if a.variant:
        params = {kv.split("=", 1)[0]: json.loads(kv.split("=", 1)[1]) for kv in a.set}
        print(common.variant(pp, a.out, a.wav, a.camera, params, a.track))


if __name__ == "__main__":
    main()

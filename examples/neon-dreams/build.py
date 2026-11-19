#!/usr/bin/env python3
"""Neon Dreams: write the scene and the project (blockout, iteration 1).

    python3 examples/neon-dreams/build.py [--out DIR] [--camera ride|far|mid|near|second] [--audio DIR]

A night cosmic sky, a cloud ocean to the horizon (ADR-1289 cloud lobes), one primary cloud formation with ~32
emissive-windowed blocks rising out of it, one distant secondary formation with ~12 blocks, a far ring anomaly and a
glowing cosmic floor under the sea, and a slow spline camera (camera mode 2) driven by a timeline track on
camera/splineT. Writes <out>/neon-dreams.scene.json and <out>/neon-dreams.json (out defaults to scene/, gitignored).

Units are metres, y up, the approach runs toward -z. Pure Python, deterministic (seeded).
"""
from __future__ import annotations

import argparse
import json
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
SABERTOOTH = os.path.expanduser("~/Desktop/sabertooth")
SONG = 70.18  # seconds

# ---------------------------------------------------------------------------------------------------------------
# palette (linear RGB): Ultraviolet Dream as the base mood
INDIGO = [0.010, 0.008, 0.040]
VIOLET = [0.30, 0.08, 1.00]
MAGENTA = [1.00, 0.06, 0.55]
PINK = [1.00, 0.25, 0.60]
EBLUE = [0.10, 0.30, 1.00]
AMBER = [1.00, 0.45, 0.08]
LAVENDER = [0.70, 0.60, 1.00]

PRIMARY = (0.0, -1600.0)     # x, z of the primary formation's centre
SECONDARY = (2600.0, -5600.0)


def r3(v):
    return [round(float(x), 3) for x in v]


# ---------------------------------------------------------------------------------------------------------------
# clouds (ADR-1289 lobes + ADR-2100 v2): an analytic sea to the horizon, lobe formations under the settlements, and
# glow emitters that light the cloud with the settlements' neon
SEA_BASE, SEA_TOP = -420.0, -40.0


def formation_lobes(rng, cx, cz, radius, top, count):
    """A cumulus mass rising out of the sea: a wide skirt low down, stacked billows toward the top, denser at the core,
    a few outriders so the silhouette is irregular."""
    lobes = []
    for k in range(count):
        u = k / max(count - 1, 1)
        a = rng.uniform(0, 2 * math.pi)
        d = radius * math.sqrt(rng.uniform(0.0, 1.0)) * (1.15 - 0.6 * u)
        y = SEA_TOP - 60.0 + (top - SEA_TOP + 60.0) * (u ** 0.9) * rng.uniform(0.75, 1.0)
        r = radius * rng.uniform(0.2, 0.34) * (1.0 - 0.45 * u)
        flat = rng.uniform(0.55, 0.8) if u < 0.4 else rng.uniform(0.7, 0.95)
        lobes.append({"c": r3([cx + d * math.cos(a), y, cz + d * math.sin(a)]), "r": round(r, 1),
                      "scale": r3([rng.uniform(0.9, 1.3), flat, rng.uniform(0.9, 1.3)]),
                      "density": round(rng.uniform(0.8, 1.0), 3), "soft": 0.45})
    for k in range(count // 6):  # outriders: lower, further out, thinner
        a = rng.uniform(0, 2 * math.pi)
        d = radius * rng.uniform(1.0, 1.6)
        lobes.append({"c": r3([cx + d * math.cos(a), SEA_TOP + rng.uniform(-40, 40), cz + d * math.sin(a)]),
                      "r": round(radius * rng.uniform(0.18, 0.3), 1),
                      "scale": r3([1.3, rng.uniform(0.4, 0.6), 1.1]), "density": 0.6, "soft": 0.7})
    return lobes


# where the neon is: the glow emitters (ADR-2100). Low in the settlements, so they light the cloud they stand in from
# inside and below; one broad underglow per settlement colours the whole formation faintly.
GLOWS = [
    {"name": "primaryUnder", "position": [0.0, -110.0, -1600.0], "color": VIOLET, "radius": 420.0, "intensity": 0.18},
    {"name": "primaryMagenta", "position": [-150.0, 10.0, -1480.0], "color": MAGENTA, "radius": 140.0, "intensity": 1.2},
    {"name": "primaryBlue", "position": [170.0, 20.0, -1720.0], "color": EBLUE, "radius": 140.0, "intensity": 1.1},
    {"name": "primaryAmber", "position": [30.0, -10.0, -1420.0], "color": AMBER, "radius": 110.0, "intensity": 0.9},
    {"name": "primaryPink", "position": [-60.0, 0.0, -1820.0], "color": PINK, "radius": 130.0, "intensity": 0.9},
    {"name": "secondaryUnder", "position": [SECONDARY[0], -60.0, SECONDARY[1]], "color": MAGENTA, "radius": 380.0,
     "intensity": 0.5},
    {"name": "secondaryBlue", "position": [SECONDARY[0] + 120.0, 10.0, SECONDARY[1] - 80.0], "color": EBLUE,
     "radius": 160.0, "intensity": 1.0},
]


def clouds_block(rng):
    lobes = formation_lobes(rng, PRIMARY[0], PRIMARY[1], 560.0, 90.0, 120)
    lobes += formation_lobes(rng, SECONDARY[0], SECONDARY[1], 400.0, 80.0, 60)
    return {
        "lobes": lobes,
        "sea": {"base": SEA_BASE, "top": SEA_TOP, "coverage": 0.72, "coverageScale": 0.00032, "density": 1.8},
        "glows": GLOWS, "glowGain": 1.0, "glowOcclusion": 2.5,
        "extinction": 0.011, "density": 1.0, "swell": 0.0,
        "steps": 64, "lightSteps": 0, "lightDistance": 220.0,
        "resolution": 0.5, "temporal": 0.85, "minStep": 6.0, "stepGrowth": 0.035, "maxDistance": 30000.0,
        # night: the moon is a faint violet key, the sky ambient barely there; the neon does the lighting
        "lightDirection": [0.35, 0.35, -0.85], "lightColor": [0.45, 0.38, 1.0], "lightIntensity": 0.12,
        "anisotropy": 0.45,
        "ambientTop": [0.012, 0.009, 0.045], "ambientBottom": [0.0015, 0.0008, 0.006],
        "warmColor": [0.02, 0.003, 0.025], "warmDirection": [0.0, 0.0, -1.0], "warmHeight": 900.0,
        "noiseScale": 0.0028, "noiseAmount": 0.6, "detailScale": 0.018, "detailAmount": 0.4,
        "drift": [3.0, 0.0, -1.5],
        "hazeColor": [0.006, 0.003, 0.02], "hazeDistance": 14000.0, "powder": 0.5, "seed": 7,
    }


# ---------------------------------------------------------------------------------------------------------------
# architecture: unit boxes placed by points, a window material program in world space
def quat_y(angle):
    return [0.0, math.sin(angle / 2), 0.0, math.cos(angle / 2)]


def block_points(rng, cx, cz, count, spread, hmin, hmax, base):
    pts = []
    placed = []
    tries = 0
    while len(pts) < count and tries < 5000:
        tries += 1
        a = rng.uniform(0, 2 * math.pi)
        d = spread * math.sqrt(rng.uniform(0, 1))
        x, z = cx + d * math.cos(a), cz + d * math.sin(a)
        w = rng.uniform(18, 42)
        dpt = rng.uniform(18, 42)
        if any(math.hypot(x - px, z - pz) < (w + pw) * 0.62 for px, pz, pw in placed):
            continue
        # taller toward the core, with a few needles
        core = 1.0 - d / spread
        h = hmin + (hmax - hmin) * (core ** 1.4) * rng.uniform(0.55, 1.0)
        if rng.random() < 0.12:
            h *= 1.5
            w *= 0.6
            dpt *= 0.6
        placed.append((x, z, max(w, dpt)))
        y = base + h / 2.0
        pts.append([round(x, 2), round(y, 2), round(z, 2)] + quat_y(rng.choice([0.0, 0.3, -0.25, 0.785])) +
                   [round(w, 2), round(h, 2), round(dpt, 2)])
    return pts


def windows_program(name, cell, lit, colors, intensity):
    """Lit windows on the vertical faces of world-space boxes: a cell grid (cell = [w, h, w] metres), a window inset in
    each cell, a per-cell noise picking which are lit, and a per-cell colour from a four-stop OKLab ramp."""
    sx, sy, sz = cell
    ops = [
        {"kind": "input", "input": "worldPosition", "dst": 0},
        {"kind": "constant", "dst": 1, "constant": [1.0 / sx, 1.0 / sy, 1.0 / sz, 1.0]},
        {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 1},                       # r0 = position in cells
        {"kind": "quantize", "dst": 2, "srcA": 0, "value": 1.0},                   # r2 = cell centre (cells)
        {"kind": "constant", "dst": 1, "constant": [-1.0, -1.0, -1.0, 0.0]},
        {"kind": "multiply", "dst": 3, "srcA": 2, "srcB": 1},
        {"kind": "add", "dst": 3, "srcA": 0, "srcB": 3},                           # r3 = offset in the cell (-.5..5)
        {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 3},                      # squared
        {"kind": "gradient", "dst": 4, "srcA": 3, "constant": [4.0, 0.0, 0.0, 0.0]},   # x offset^2 (0..1)
        {"kind": "gradient", "dst": 5, "srcA": 3, "constant": [0.0, 0.0, 4.0, 0.0]},   # z offset^2
        {"kind": "input", "input": "normal", "dst": 6},
        {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 6},                      # n^2
        {"kind": "mixBy", "dst": 4, "srcA": 4, "srcB": 5, "srcC": 6},              # the face's horizontal offset^2
        {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.42, 0.30, 0, 0]},   # 1 inside the window width
        {"kind": "gradient", "dst": 5, "srcA": 3, "constant": [0.0, 4.0, 0.0, 0.0]},   # y offset^2
        {"kind": "smoothstep", "dst": 5, "srcA": 5, "constant": [0.36, 0.22, 0, 0]},
        {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},                      # the window mask
        {"kind": "gradient", "dst": 5, "srcA": 6, "constant": [0.0, 1.0, 0.0, 0.0]},   # ny^2: 1 on roofs
        {"kind": "smoothstep", "dst": 5, "srcA": 5, "constant": [0.5, 0.2, 0, 0]},
        {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},                      # walls only
        {"kind": "noise", "dst": 5, "srcA": 2, "value": 0.71, "seed": 3},          # per cell, constant
        {"kind": "smoothstep", "dst": 6, "srcA": 5, "constant": [1.0 - lit, 1.0 - lit + 0.04, 0, 0]},
        {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 6},                      # lit windows
        {"kind": "noise", "dst": 6, "srcA": 2, "value": 0.37, "seed": 11},
        {"kind": "oklabRamp", "dst": 6, "srcA": 6, "constant": colors[0] + [1.0], "constant2": colors[1] + [1.0],
         "constant3": colors[2] + [1.0], "constant4": colors[3] + [1.0], "value": 1.0},
        {"kind": "multiply", "dst": 7, "srcA": 6, "srcB": 4},                      # emission
        {"kind": "constant", "dst": 1, "constant": [0.018, 0.016, 0.03, 1.0]},     # the facade: near-black indigo
    ]
    return {"name": name, "ops": ops, "baseColor": 1, "metallic": -1, "roughness": -1, "emission": 7,
            "emissionIntensity": intensity, "opacity": -1}


def emissive_program(name, colors, scale, intensity, speed=0.0):
    """A world-space noise glow through an OKLab ramp (the cosmic floor, the ring)."""
    ops = [
        {"kind": "input", "input": "worldPosition", "dst": 0},
        {"kind": "input", "input": "time", "dst": 1},
        {"kind": "constant", "dst": 2, "constant": [speed, 0.0, speed * 0.6, 0.0]},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
        {"kind": "add", "dst": 0, "srcA": 0, "srcB": 1},
        {"kind": "noise", "dst": 3, "srcA": 0, "value": scale, "seed": 5},
        {"kind": "noise", "dst": 4, "srcA": 0, "value": scale * 4.1, "seed": 9},
        {"kind": "mix", "dst": 3, "srcA": 3, "srcB": 4, "value": 0.35},
        {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.3, 0.8, 0, 0]},
        {"kind": "oklabRamp", "dst": 5, "srcA": 3, "constant": colors[0] + [1.0], "constant2": colors[1] + [1.0],
         "constant3": colors[2] + [1.0], "constant4": colors[3] + [1.0], "value": 1.0},
        {"kind": "constant", "dst": 6, "constant": [0.0, 0.0, 0.0, 1.0]},
    ]
    return {"name": name, "ops": ops, "baseColor": 6, "metallic": -1, "roughness": -1, "emission": 5,
            "emissionIntensity": intensity, "opacity": -1}


def box_node(name, pts, program, shadow=True):
    return {"name": name, "kind": "procedural", "position": [0, 0, 0], "procedural": {
        "source": {"kind": "box", "size": [1, 1, 1]},
        "distribution": {"kind": "points", "points": pts},
        "castsShadow": shadow,
        "material": {"baseColor": [0.02, 0.02, 0.035], "roughness": 0.45, "metallic": 0.2, "program": program}}}


def sign_points(rng, blocks, count):
    """Thin vertical neon blades on the faces of some blocks."""
    pts = []
    for b in rng.sample(blocks, min(count, len(blocks))):
        x, y, z = b[0], b[1], b[2]
        w, h, d = b[7], b[8], b[9]
        qy = b[4]
        ang = 2 * math.atan2(qy, b[6])
        side = rng.choice([1, -1])
        # on the block's local +x/-x face
        ox = side * (w / 2 + 1.0)
        px = x + ox * math.cos(ang)
        pz = z - ox * math.sin(ang)
        sh = min(h * 0.4, rng.uniform(14, 34))
        py = y + rng.uniform(-0.1, 0.35) * h
        pts.append([round(px, 2), round(py, 2), round(pz, 2)] + quat_y(ang) + [0.8, round(sh, 2), 2.6])
    return pts


def nodes_and_programs(rng):
    prim = block_points(rng, PRIMARY[0], PRIMARY[1], 34, 330.0, 50.0, 300.0, 20.0)
    sec = block_points(rng, SECONDARY[0], SECONDARY[1], 14, 220.0, 40.0, 180.0, 10.0)
    warm = [AMBER, [1.0, 0.70, 0.35], PINK, LAVENDER]
    cool = [EBLUE, VIOLET, MAGENTA, LAVENDER]
    programs = [
        windows_program("ndWindowsWarm", [3.2, 3.6, 3.2], 0.42, warm, 6.0),
        windows_program("ndWindowsCool", [3.0, 3.4, 3.0], 0.35, cool, 7.0),
        emissive_program("ndCosmicFloor", [[0.0, 0.0, 0.0], [0.08, 0.02, 0.35], [0.6, 0.08, 0.7], [1.0, 0.5, 0.9]],
                         0.0009, 3.0, speed=6.0),
        emissive_program("ndRing", [[0.3, 0.05, 1.0], [0.9, 0.1, 0.6], [1.0, 0.4, 0.8], [1.0, 0.8, 0.95]],
                         0.0004, 2.5),
        emissive_program("ndMoon", [[0.35, 0.30, 0.55], [0.55, 0.50, 0.80], [0.75, 0.70, 0.95], [0.9, 0.85, 1.0]],
                         0.004, 1.6),
        {"name": "ndSignMagenta", "ops": [{"kind": "constant", "dst": 0, "constant": MAGENTA + [1.0]},
                                          {"kind": "constant", "dst": 1, "constant": [0, 0, 0, 1.0]}],
         "baseColor": 1, "metallic": -1, "roughness": -1, "emission": 0, "emissionIntensity": 14.0, "opacity": -1},
        {"name": "ndSignBlue", "ops": [{"kind": "constant", "dst": 0, "constant": EBLUE + [1.0]},
                                       {"kind": "constant", "dst": 1, "constant": [0, 0, 0, 1.0]}],
         "baseColor": 1, "metallic": -1, "roughness": -1, "emission": 0, "emissionIntensity": 14.0, "opacity": -1},
    ]
    half = len(prim) // 2
    signs = sign_points(rng, prim, 12)
    nodes = [
        box_node("primaryA", prim[:half], "ndWindowsWarm"),
        box_node("primaryB", prim[half:], "ndWindowsCool"),
        box_node("secondary", sec, "ndWindowsCool", shadow=False),
        box_node("signsMagenta", signs[:6], "ndSignMagenta", shadow=False),
        box_node("signsBlue", signs[6:], "ndSignBlue", shadow=False),
        # the cosmic floor, far under the sea: seen only through the breaks
        # (400 m thick: a 2 m slab's top and bottom faces z-fought kilometres away and drew 32 px black tiles)
        {"name": "cosmicFloor", "kind": "procedural", "position": [0, -1600, -2500], "procedural": {
            "source": {"kind": "box", "size": [1000, 400, 1000]},
            "distribution": {"kind": "single"}, "castsShadow": False,
            "distributionTransform": {"scale": [60.0, 1.0, 60.0]},
            "material": {"baseColor": [0, 0, 0], "roughness": 1.0, "metallic": 0.0, "program": "ndCosmicFloor"}}},
        # the moon: a far sphere (the sky's analytic disc drew as a square at this size)
        {"name": "moon", "kind": "procedural", "position": r3([-0.35 * 30000, 0.32 * 30000, -0.88 * 30000]),
         "procedural": {"source": {"kind": "sphere", "radius": 1000.0}, "distribution": {"kind": "single"},
                        "castsShadow": False, "distributionTransform": {"scale": [1.3, 1.3, 1.3]},
                        "material": {"baseColor": [0, 0, 0], "roughness": 1.0, "metallic": 0.0,
                                     "program": "ndMoon"}}},
        # the ring anomaly: far, tilted, huge
        {"name": "ring", "kind": "procedural", "position": [-9000, 5200, -26000],
         "rotation": [1.2, 0.35, 0.2], "procedural": {
             "source": {"kind": "torus", "majorRadius": 900.0, "minorRadius": 18.0, "majorSegments": 256,
                        "minorSegments": 12},
             "distribution": {"kind": "single"}, "castsShadow": False,
             "distributionTransform": {"scale": [7.0, 7.0, 7.0]},
             "material": {"baseColor": [0, 0, 0], "roughness": 1.0, "metallic": 0.0, "program": "ndRing"}}},
    ]
    return nodes, programs, prim, sec


def city_lights(rng, prim, sec):
    """Coloured point lights among the blocks, low, so they reach the facades and the air (volumeLocalLights)."""
    lights = []
    cols = [MAGENTA, EBLUE, VIOLET, AMBER, PINK, MAGENTA, EBLUE, VIOLET]
    for k, b in enumerate(rng.sample(prim, 10)):
        lights.append({"name": "city%02d" % k, "type": "point", "position": r3([b[0] + rng.uniform(-20, 20), 30.0 +
                                                                                   rng.uniform(0, 40), b[2] + rng.uniform(-20, 20)]),
                       "color": cols[k % len(cols)], "intensity": 9000.0, "range": 260.0, "castsShadow": False,
                       "volumetric": 1.0})
    for k, b in enumerate(rng.sample(sec, 4)):
        lights.append({"name": "far%02d" % k, "type": "point", "position": r3([b[0], 40.0, b[2]]),
                       "color": cols[(k + 3) % len(cols)], "intensity": 9000.0, "range": 240.0, "castsShadow": False,
                       "volumetric": 1.0})
    return lights


# ---------------------------------------------------------------------------------------------------------------
# camera: a slow approach from the open sea, a low pass along the primary's flank, a rise to see the secondary
RIDE = [
    [-700.0, 230.0, 1400.0],
    [-520.0, 210.0, 600.0],
    [-380.0, 170.0, -300.0],
    [-260.0, 130.0, -1000.0],     # arriving at the flank of the primary
    [-60.0, 120.0, -1450.0],      # the close pass between towers
    [180.0, 150.0, -1850.0],
    [420.0, 210.0, -2400.0],
    [700.0, 260.0, -3100.0],      # out over the sea, the secondary ahead
    [1100.0, 240.0, -3800.0],
]

STILLS = {
    "far": {"position": [-700, 240, 1400], "target": [0, 90, -1600], "fov": 50},
    "mid": {"position": [-420, 190, -500], "target": [0, 110, -1600], "fov": 50},
    "near": {"position": [-150, 110, -1250], "target": [60, 130, -1700], "fov": 55},
    "second": {"position": [500, 230, -2600], "target": [2600, 60, -5600], "fov": 50},
}


def scene_doc(camera_name, rng):
    nodes, programs, prim, sec = nodes_and_programs(rng)
    env = {
        "background": [0.002, 0.001, 0.008],
        "intensity": 0.25, "skyIntensity": 1.0, "lightFromEnvironment": False,
        "sky": {"enabled": True, "background": True, "useKeyLight": False,
                "sunDirection": [-0.35, 0.32, -0.88],
                "zenithColor": [0.002, 0.002, 0.010], "horizonColor": [0.024, 0.006, 0.042],
                "groundColor": [0.02, 0.006, 0.04], "haze": 0.18,
                "sunColor": [0.85, 0.80, 1.0], "sunIntensity": 0.0, "sunSize": 0.012, "sunGlow": 0.03,
                "intensity": 1.0},
        "fogColor": [0.03, 0.012, 0.07], "fogSky": 0.6, "fogSkyDistance": 12000.0,
        "volumeDensity": 0.00012, "volumeAbsorption": 1.0, "volumeScattering": 1.0, "volumeAnisotropy": 0.3,
        "volumeSteps": 24, "volumeMaxDistance": 900.0, "volumeLocalLights": 1.0,
        "volumeNoise": 0.4, "volumeNoiseScale": 0.004, "volumeNoiseSpeed": 0.02,
        "fogHeight": 60.0, "fogHeightFalloff": 0.004,
    }
    lights = [{"name": "moon", "type": "directional", "role": "key", "direction": [0.35, -0.32, 0.88],
               "color": [0.55, 0.50, 1.0], "intensity": 0.6, "castsShadow": True, "volumetric": 0.0},
              {"name": "skyfill", "type": "directional", "role": "fill", "direction": [-0.3, -0.5, -0.4],
               "color": [0.35, 0.20, 0.8], "intensity": 0.15, "castsShadow": False, "volumetric": 0.0}]
    lights += city_lights(rng, prim, sec)
    if camera_name == "ride":
        camera = {"mode": 2, "spline": "ride", "fov": 50.0, "orbitSpeed": 0.0, "near": 0.5, "far": 80000.0}
    else:
        camera = dict({"mode": 1, "orbitSpeed": 0.0, "near": 0.5, "far": 80000.0}, **STILLS[camera_name])
    return {
        "format": "avgen-scene", "version": 1, "name": "neon-dreams",
        "_note": "Generated by examples/neon-dreams/build.py. Do not edit.",
        "camera": camera,
        "environment": env,
        "lights": lights,
        "effects": [{"id": "stars", "name": "stars", "type": "stars", "owner": {"kind": "world"}, "enabled": True,
                     "order": 0, "activation": "always",
                     "parameters": {"brightness": 3.0, "density": 0.6, "magnitudeSlope": 5.5, "colorSpread": 0.45,
                                    "twinkle": 0.2, "twinkleRate": 1.0, "horizonFade": 0.25, "band": 0.55,
                                    "bandTilt": 40.0, "daylight": 0.0}}],
        "nodes": nodes + [{"name": "ride", "kind": "spline", "spline": {
            "name": "ride", "kind": "catmullRom", "closed": False, "tension": 0.5, "generator": "points",
            "samplesPerSegment": 32, "points": [{"position": p, "roll": 0.0, "scale": 1.0} for p in RIDE]}}],
        "materialPrograms": programs,
        "clouds": clouds_block(rng),
    }


def cloud_routes():
    """ADR-2100: the clouds through ordinary routes. The bass stem swells the sea and fattens the formations, slowly;
    the kick lifts one district's glow at a time (the magenta quarter), not the whole sky."""
    def r(source, target, amount, **chain):
        out = {"source": source, "target": target, "amount": amount, "op": "add", "polarity": "unipolar"}
        if chain:
            out["chain"] = chain
        return out
    return [
        r("stem.bass.level", "clouds/swell", 0.45, attackMs=350.0, decayMs=1800.0),
        r("stem.kick.onset", "clouds/glow/1/intensity", 1.6, attackMs=15.0, decayMs=450.0),
        r("stem.snare.onset", "clouds/glow/2/intensity", 1.2, attackMs=15.0, decayMs=350.0),
    ]


def project_doc(scene_rel, audio_dir):
    stems = [("kick", "Kick", "drums"), ("snare", "Snare", "drums"), ("hh", "HH", "drums"), ("bass", "BASS", "bass"),
             ("moog", "Moog", "synth"), ("cloud", "Cloud Diving", "synth"), ("crash", "Crash", "drums"),
             ("toms", "Toms", "drums"), ("block", "tone block", "other")]
    return {
        "format": "avgen-project", "version": 4,
        "assets": {"scene": {"kind": "composition", "path": scene_rel},
                   "audio": os.path.join(audio_dir, "Sabertooth.wav"),
                   "stems": [{"key": k, "label": lbl, "role": role, "file": os.path.join(audio_dir, lbl + ".wav")}
                             for k, lbl, role in stems]},
        "sonic": {"live": False},
        "feeds": {"beat": "stem.kick", "hits": "stem.kick", "levels": "audio"},
        "parameters": {"camera/lookAhead": 120.0, "camera/splineBank": 6.0, "post/output/dither": 1.0},
        "routes": cloud_routes(),
        "timeline": {"enabled": True, "cues": [], "tracks": [
            {"target": "camera/splineT", "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
             "loopLength": 0.0, "keys": [{"time": 0.0, "value": 0.0, "interp": "linear"},
                                          {"time": SONG, "value": 1.0, "interp": "linear"}]}]},
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/neon-dreams.mp4"},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "scene"))
    ap.add_argument("--camera", default="ride", choices=["ride"] + sorted(STILLS))
    ap.add_argument("--audio", default=SABERTOOTH)
    ap.add_argument("--name", default="neon-dreams")
    ap.add_argument("--no-clouds", action="store_true", help="the cloud block disabled (for an A/B of its cost)")
    a = ap.parse_args()
    rng = random.Random(20261119)
    os.makedirs(a.out, exist_ok=True)
    scene_path = os.path.join(a.out, a.name + ".scene.json")
    with open(scene_path, "w") as f:
        doc = scene_doc(a.camera, rng)
        if a.no_clouds:
            doc["clouds"]["enabled"] = False
        json.dump(doc, f, indent=1)
    proj_path = os.path.join(a.out, a.name + ".json")
    with open(proj_path, "w") as f:
        json.dump(project_doc(os.path.relpath(scene_path, a.out), a.audio), f, indent=1)
    print("wrote", scene_path, "and", proj_path)


if __name__ == "__main__":
    main()

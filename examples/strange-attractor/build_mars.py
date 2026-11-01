"""Writes strange-attractor-mars.scene.json: the Strange Attractor's Mars, Cydonia (Art Pass 2, M2; ADR-1895, 1896).

    python3 examples/strange-attractor/build_mars.py

The canyon's second portal (an outer cliff behind the dusk camp) opens onto Mars: out of a mesa's wall onto a dusty
plain under a butterscotch sky. Mesas and knobs stand around the plain (ADR-1896: the lunar generator's Mars terms),
yardangs run along the wind, a haze lies over the distance. Ahead, three pyramids in the Giza arrangement (the offset
diagonal and the three sizes; pass2/build_pyramids.py), ancient and eroded, partly buried in sand. The camera crosses
the plain toward them, their scale growing against the mesas, and flies into the south face of the middle one, where
the casing has fallen and the stone fractures into the Strange Attractor's computational language: the portal home to
the canyon, at night (the canyon's look from crossing 3).

Mars coordinates: the arrival heads -z; the pyramids stand 2 km ahead.
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from canyon_scale import WORLD_SCALE as WS, canyon as canyon_frame  # noqa: E402


def unit(v):
    n = math.sqrt(sum(c * c for c in v))
    return [c / n for c in v]


def direction(az_deg, el_deg):
    """Toward azimuth az (degrees, 0 = -z, the arrival heading; positive to the right) and elevation el."""
    az, el = math.radians(az_deg), math.radians(el_deg)
    return [math.cos(el) * math.sin(az), math.sin(el), -math.cos(el) * math.cos(az)]


def canyon(d, lat):
    """d real metres down-canyon of the dusk exit, lat real metres toward the camp bank (x, z), scaled world."""
    return canyon_frame(d / WS, lat / WS)


# --- the light: an afternoon sun from behind and to the right, a dusty sky
SUN = direction(140.0, 21.0)          # toward the sun
SKY_ZENITH = [0.2, 0.12, 0.075]
SKY_HORIZON = [0.6, 0.41, 0.26]        # butterscotch
HAZE = [0.52, 0.36, 0.23]

# --- the ground (ADR-1896): mesas and knobs, yardangs along the wind (from the north-east), a few old craters
MESAS = [
    # the arrival mesa: the camera comes out of its north wall
    dict(x=0.0, z=420.0, rx=300.0, rz=200.0, angle=4.0, height=115.0, terraces=3),
    dict(x=-1350.0, z=-700.0, rx=520.0, rz=260.0, angle=28.0, height=160.0, terraces=4),   # the great west mesa
    dict(x=1250.0, z=-1100.0, rx=380.0, rz=240.0, angle=-18.0, height=135.0, terraces=3),  # east
    dict(x=-200.0, z=-2950.0, rx=700.0, rz=220.0, angle=8.0, height=180.0, terraces=5),    # the far wall behind the pyramids
    dict(x=780.0, z=-300.0, rx=90.0, rz=70.0, angle=40.0, height=55.0, terraces=2),        # knobs
    dict(x=-700.0, z=-1600.0, rx=80.0, rz=60.0, angle=-30.0, height=48.0, terraces=2),
    dict(x=900.0, z=-2100.0, rx=110.0, rz=80.0, angle=12.0, height=62.0, terraces=2),
    dict(x=-520.0, z=150.0, rx=70.0, rz=55.0, angle=60.0, height=38.0, terraces=2),
    dict(x=-480.0, z=-420.0, rx=160.0, rz=110.0, angle=-12.0, height=85.0, terraces=3),    # close beside the route
    dict(x=520.0, z=-1380.0, rx=150.0, rz=95.0, angle=22.0, height=90.0, terraces=3),
]
CRATERS = [dict(x=520.0, z=-650.0, diameter=260.0, fresh=0.15), dict(x=-420.0, z=-1050.0, diameter=180.0, fresh=0.25)]

# --- the pyramids: the Giza diagonal, south-west to north-east (the largest first), 2 km ahead
PYRAMIDS = [("pyramid-a", -260.0, -1780.0, 300.0, 8.0),
            ("pyramid-b", 160.0, -2080.0, 280.0, 0.0),
            ("pyramid-c", 470.0, -2380.0, 140.0, 14.0)]
# each pyramid stands on a pad (ADR-1896: the ground eased to a level under its footing, 1.5 half-bases across), so its
# sand plain meets the ground; the level is before the planet's curvature, which drops the ground by d^2 / 2R
CURVATURE = 3389.5e3
PAD_LEVEL = {"pyramid-a": 0.0, "pyramid-b": 2.0, "pyramid-c": 12.0}


def curved(level, x, z):
    return level - (x * x + z * z) / (2.0 * CURVATURE)


PYRAMID_GROUND = {name: curved(PAD_LEVEL[name], x, z) for name, x, z, _, _ in PYRAMIDS}
PYRAMID_SINK = 3.0

# --- the arrival: out of the arrival mesa's north wall, heading -z (the exit's y is over the ground)
EXIT = [0.0, 22.0, 196.0]
EXIT_AXIS = [0.0, 0.0, -1.0]

# --- the pyramid portal: in pyramid-b's south face, where the casing has fallen, 30 m up (absolute y set by measure)
PB = PYRAMIDS[1]
FACE_HEIGHT = 32.0
# (a corruption's focus y over a generated ground is a height over that ground: measured 2.7 m at the face's foot)
FOCUS_GROUND = curved(PAD_LEVEL["pyramid-b"], PYRAMIDS[1][1], PYRAMIDS[1][2] + PYRAMIDS[1][3] * 0.5 - 25.2)
PORTAL_FOCUS = [PB[1], PYRAMID_GROUND["pyramid-b"] - 3.0 + FACE_HEIGHT - FOCUS_GROUND,
                PB[2] + PB[3] * 0.5 - FACE_HEIGHT / math.tan(math.radians(51.8))]

# --- home: out of the canyon's outer cliff, at night (the cliff 66-116 m at d 46, lat 100 real), down to the camp
NIGHT_EXIT_DL = (46.0, 99.0)
NIGHT_EXIT = [*canyon(*NIGHT_EXIT_DL)[:1], 86.0, canyon(*NIGHT_EXIT_DL)[1]]
_o = [canyon(1.0, 0.0)[i] - canyon(0.0, 0.0)[i] for i in (0, 1)]
_r = [canyon(0.0, 1.0)[i] - canyon(0.0, 0.0)[i] for i in (0, 1)]
OUT_OF_CLIFF = unit([-_r[0], 0.0, -_r[1]])   # toward the river (-lat)
NIGHT_FLIGHT = [[*canyon(d, lat)[:1], h, canyon(d, lat)[1], t] for d, lat, h, t in
                ((42.0, 80.0, 40.0, 103.6), (34.0, 54.0, 22.0, 106.4), (26.0, 33.0, 8.0, 109.2), (22.0, 25.0, 2.8, 112.0),
                 (21.0, 23.0, 2.3, 115.0), (20.5, 22.5, 2.3, 118.0))]
CAMP = canyon(16.0, 16.0)
NIGHT_AIMS = [[CAMP[0], 1.76 + 0.8, CAMP[1], 104.5, 116.0]]

# --- the Mars flight (x, height over the ground, z, t): crossing 3 at 69.69 s (bar 36), crossing 4 at 101.4 s (bar 52)
MARS_FLIGHT = [
    [0.0, 20.0, 120.0, 71.2],       # out of the wall over the apron, the plain opening ahead
    [-40.0, 38.0, -150.0, 74.5],    # establish: the mesas, the haze, the pyramids small on the far plain
    [-90.0, 55.0, -520.0, 79.0],
    [-60.0, 62.0, -900.0, 84.0],    # the pyramids grow against the far wall
    [30.0, 55.0, -1250.0, 89.0],
    [120.0, 48.0, -1560.0, 93.5],   # between the great pyramid and the middle one: their scale
    [PORTAL_FOCUS[0] - 10.0, 40.0, PORTAL_FOCUS[2] + 220.0, 97.0],
    [PORTAL_FOCUS[0], 34.0, PORTAL_FOCUS[2] + 70.0, 99.6],
    [PORTAL_FOCUS[0], None, PORTAL_FOCUS[2], 101.4],          # the crossing (y: the focus)
    [PORTAL_FOCUS[0], None, PORTAL_FOCUS[2] - 25.0, 102.2],
]

# a dusty regolith: rust and ochre by the generator's albedo and a broad noise, grit up close
regolith = [
    {"kind": "input", "dst": 0, "input": "uv"},
    {"kind": "swizzle", "dst": 1, "srcA": 0, "constant": [0, 0, 0, 0]},
    {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.07, 0.2, 0.62, 1.3], "value": 1.0},
    {"kind": "input", "dst": 2, "input": "worldPosition"},
    {"kind": "microDetail", "dst": 3, "srcA": 2, "value": 0.7, "seed": 5},
    {"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, 0.84, 1.16]},
    {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 3},
    {"kind": "noise", "dst": 4, "srcA": 2, "value": 0.0025, "seed": 41},
    {"kind": "remap", "dst": 4, "srcA": 4, "constant": [0.25, 0.75, 0, 1], "value": 1.0},
    {"kind": "constant", "dst": 5, "constant": [0.34, 0.14, 0.065, 1.0]},    # rust
    {"kind": "constant", "dst": 6, "constant": [0.46, 0.28, 0.14, 1.0]},     # ochre
    {"kind": "mixBy", "dst": 5, "srcA": 5, "srcB": 6, "srcC": 4},
    {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 1},
    {"kind": "constant", "dst": 0, "constant": [0, 0, 0, 0]},
    {"kind": "constant", "dst": 1, "constant": [0.95, 0.95, 0.95, 0.95]},
]

GOLD = [1.0, 0.62, 0.3]
A = "../../assets/strange-attractor/build/"


def write():
    flight = []
    for x, h, z, t in MARS_FLIGHT:
        if h is None:  # through the face: hold the focus's height over the ground at its foot
            h = PORTAL_FOCUS[1]
        flight.append([x, h, z, t])
    scene = {
        "format": "avgen-scene", "version": 1, "name": "the-strange-attractor-mars",
        "_note": "The Strange Attractor's Mars, Cydonia (Art Pass 2, M2), written by build_mars.py; reached by the canyon's "
                 "second portal (ADR-1895), left by the pyramid portal home to the canyon at night.",
        "camera": {"mode": 1, "position": [0, 60, 300], "target": [0, 40, -1000], "fov": 44.0, "orbitSpeed": 0.0, "far": 60000.0},
        "lights": [
            {"name": "sun", "type": "directional", "role": "key", "direction": [-c for c in SUN], "color": [1.0, 0.9, 0.78],
             "intensity": 2.3, "castsShadow": True, "shadowStrength": 1.0, "softness": 0.25},
            {"name": "sky-fill", "type": "directional", "role": "fill", "direction": [0.1, -1.0, 0.2], "color": [0.95, 0.72, 0.5],
             "intensity": 0.22, "castsShadow": False, "shadowStrength": 0.0},
            {"name": "ground-bounce", "type": "directional", "role": "fill", "direction": [-c for c in unit([-SUN[0], -0.4, -SUN[2]])],
             "color": [0.85, 0.5, 0.3], "intensity": 0.18, "castsShadow": False, "shadowStrength": 0.0},
        ],
        "environment": {"intensity": 0.08, "background": SKY_HORIZON, "fogColor": HAZE, "volumeDensity": 0.00016,
                        "sky": {"enabled": True, "background": True, "useKeyLight": True, "zenithColor": SKY_ZENITH,
                                "horizonColor": SKY_HORIZON, "groundColor": [0.25, 0.14, 0.08], "haze": 0.3,
                                "sunColor": [1.0, 0.86, 0.7], "sunIntensity": 3.0, "sunSize": 0.025, "sunGlow": 0.6,
                                "intensity": 1.0},
                        "shadowRange": 2400.0, "shadowCascades": 4, "ambientOcclusion": False, "shadowCasterReach": 5000.0,
                        "contactShadows": False, "shadowSplitLambda": 0.9},
        "post": {"bloomIntensity": 0.04, "tonemap": 0},
        "materialPrograms": [{"name": "mars_regolith", "ops": regolith, "baseColor": 5, "metallic": 0, "roughness": 1}],
        "lunarWorld": {"seed": 11, "extent": 6000, "spacing": 2.5, "skirtExtent": 48000, "skirtSpacing": 48, "craters": 0.4,
                       "rocks": 0.008, "relief": 70, "program": "mars_regolith", "curvature": CURVATURE,
                       "heroes": CRATERS, "mesas": MESAS, "yardangs": 7.0, "wind": 35.0,
                       "pads": [{"x": x, "z": z, "radius": base * 0.75, "level": PAD_LEVEL[name]} for name, x, z, base, _ in PYRAMIDS]},
        # the pyramid portal: pyramid-b's south face cracks and opens, the same corruption as the canyon's, warmer
        "corruption": {"targets": ["pyramid-b"], "focus": PORTAL_FOCUS, "axis": [0.0, 0.0, -1.0], "amount": 1.0,
                       "radius": 70.0, "inner": 0.2, "fieldNear": 40.0, "fieldFar": 420.0, "openNear": 30.0, "openFar": 170.0,
                       "seam": 0.3, "width": 22.0, "height": 17.0, "depth": 12.0, "block": 4.0, "share": 0.22, "slip": 2.5,
                       "rate": 0.5, "strip": 3.5, "stripSlip": 1.5, "slide": 0.0, "pull": 6.0, "lift": 1.2, "fold": 0.8,
                       "lag": 0.7, "glyph": 5.5, "glyphSize": 1.6, "glow": 4.0, "echoes": 1, "echoOffset": 1.0,
                       "glowColor": GOLD, "light": 2400.0},
        # home: the canyon, at night (its look from crossing 3), out of the outer cliff and down toward the camp's embers
        "portal": {"world": "@root", "exit": NIGHT_EXIT, "exitAxis": OUT_OF_CLIFF, "entryUp": [0.0, 1.0, 0.0],
                   "exitUp": [0.0, 1.0, 0.0], "flight": NIGHT_FLIGHT, "aims": NIGHT_AIMS},
        "nodes": [{"name": name, "kind": "gltf", "asset": A + f"{name}.glb",
                   "position": [x, PYRAMID_GROUND[name] - PYRAMID_SINK, z], "rotation": [0, yaw, 0], "scale": [1, 1, 1],
                   "visible": True} for name, x, z, _, yaw in PYRAMIDS],
    }
    path = os.path.join(HERE, "strange-attractor-mars.scene.json")
    json.dump(scene, open(path, "w"), indent=2)
    return path, flight


if __name__ == "__main__":
    print(write()[0])

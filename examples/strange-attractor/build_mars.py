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
from canyon_scale import WORLD_SCALE as WS, R, PREFIX, site, near, knot, O_UNIT, L_UNIT, CAMP_SITE, GATHER_AT  # noqa: E402
from sample_ground import heights  # noqa: E402


def unit(v):
    n = math.sqrt(sum(c * c for c in v))
    return [c / n for c in v]


def direction(az_deg, el_deg):
    """Toward azimuth az (degrees, 0 = -z, the arrival heading; positive to the right) and elevation el."""
    az, el = math.radians(az_deg), math.radians(el_deg)
    return [math.cos(el) * math.sin(az), math.sin(el), -math.cos(el) * math.cos(az)]


# --- the light: an afternoon sun from behind and to the right, a dusty sky
SUN = direction(128.0, 14.0)          # toward the sun (M3c: lower, for longer shadows over the ripples and rocks)
SKY_ZENITH = [0.2, 0.12, 0.075]
SKY_HORIZON = [0.6, 0.41, 0.26]        # butterscotch
HAZE = [0.52, 0.36, 0.23]

# --- the ground (ADR-1896): mesas and knobs, yardangs along the wind (from the north-east), a few old craters
MESAS = [
    # the arrival mesa: the camera comes out of its north wall (M3c: nearer the pyramids, so the rover and the pyramids
    # share one run across the plain)
    dict(x=0.0, z=-500.0, rx=300.0, rz=200.0, angle=4.0, height=115.0, terraces=3),   # M3d: nearer again
    dict(x=-1350.0, z=-700.0, rx=520.0, rz=260.0, angle=28.0, height=160.0, terraces=4),   # the great west mesa
    dict(x=1250.0, z=-1100.0, rx=380.0, rz=240.0, angle=-18.0, height=135.0, terraces=3),  # east
    dict(x=-200.0, z=-2950.0, rx=700.0, rz=220.0, angle=8.0, height=180.0, terraces=5),    # the far wall behind the pyramids
    dict(x=780.0, z=-300.0, rx=90.0, rz=70.0, angle=40.0, height=55.0, terraces=2),        # knobs
    dict(x=-700.0, z=-1600.0, rx=80.0, rz=60.0, angle=-30.0, height=48.0, terraces=2),
    dict(x=900.0, z=-2100.0, rx=110.0, rz=80.0, angle=12.0, height=62.0, terraces=2),
    dict(x=-520.0, z=150.0, rx=70.0, rz=55.0, angle=60.0, height=38.0, terraces=2),
    dict(x=-480.0, z=-420.0, rx=160.0, rz=110.0, angle=-12.0, height=85.0, terraces=3),    # close beside the route
    dict(x=760.0, z=-1050.0, rx=150.0, rz=95.0, angle=22.0, height=90.0, terraces=3),   # M3c: clear of the drifts
]
CRATERS = [dict(x=520.0, z=-650.0, diameter=260.0, fresh=0.15), dict(x=-420.0, z=-1050.0, diameter=180.0, fresh=0.25),
           # M3: young craters beside the route, their rims strewn with boulders (the plain's foreground)
           dict(x=-70.0, z=-1170.0, diameter=60.0, fresh=0.9), dict(x=-130.0, z=-800.0, diameter=34.0, fresh=1.0),
           # M3b: more young craters strewing boulders near the route (M3d: clear of the mesa, the rover and the drifts)
           dict(x=170.0, z=-860.0, diameter=26.0, fresh=1.0), dict(x=-230.0, z=-930.0, diameter=40.0, fresh=0.95)]

# --- the pyramids: the Giza diagonal, south-west to north-east (the largest first), 2 km ahead
PYRAMIDS = [("pyramid-a", -260.0, -1380.0, 300.0, 8.0),       # M3c: 400 m nearer
            ("pyramid-b", 160.0, -1680.0, 280.0, 0.0),
            ("pyramid-c", 470.0, -1980.0, 140.0, 14.0)]
# each pyramid stands on a pad (ADR-1896: the ground eased to a level under its footing, 1.5 half-bases across), so its
# sand plain meets the ground; the level is before the planet's curvature, which drops the ground by d^2 / 2R
CURVATURE = 3389.5e3
PAD_LEVEL = {"pyramid-a": -1.7, "pyramid-b": 5.6, "pyramid-c": 19.4}   # M3c: the mean ground round each (sampled)


def curved(level, x, z):
    return level - (x * x + z * z) / (2.0 * CURVATURE)


PYRAMID_GROUND = {name: curved(PAD_LEVEL[name], x, z) for name, x, z, _, _ in PYRAMIDS}
PYRAMID_SINK = 3.0

# --- the arrival: out of the arrival mesa's north wall, heading -z (the exit's y is over the ground)
EXIT = [0.0, 9.0, -724.0]          # M3d (owner): lower, near the ground
EXIT_AXIS = [0.0, 0.0, -1.0]

# --- the pyramid portal: in pyramid-b's south face, where the casing has fallen, 30 m up (absolute y set by measure)
PB = PYRAMIDS[1]
FACE_HEIGHT = 74.0                  # M3b: above the deeper sand (it buries the base to about 46 m)
# (a corruption's focus y over a generated ground is a height over that ground: measured 2.7 m at the face's foot)
FOCUS_GROUND = curved(PAD_LEVEL["pyramid-b"], PYRAMIDS[1][1], PYRAMIDS[1][2] + PYRAMIDS[1][3] * 0.5 - 25.2)
PORTAL_FOCUS = [PB[1], PYRAMID_GROUND["pyramid-b"] - 3.0 + FACE_HEIGHT - FOCUS_GROUND,
                PB[2] + PB[3] * 0.5 - FACE_HEIGHT / math.tan(math.radians(51.8))]

# --- home: out of the canyon's outer cliff, at night (the cliff 66-116 m at d 46, lat 100), down to the camp
NIGHT_EXIT_DL = (46.0, 99.0)
NIGHT_EXIT = [*site(*NIGHT_EXIT_DL)[:1], 86.0 * R, site(*NIGHT_EXIT_DL)[1]]
OUT_OF_CLIFF = unit([-L_UNIT[0], 0.0, -L_UNIT[1]])   # toward the river (-lat)
CANYON_SCENE = PREFIX + "strange-attractor-flythrough.scene.json"
CAMP = site(*CAMP_SITE)
CAMP_GROUND = 4.4 * WS
# M3b (owner: the night needs a real pass; a lower, closer framing, not tiny figures from high up): down from the cliff
# over the benches, then low -- eye height, 6-9 m from the fire -- in a slow arc round the gathering on the river side,
# the bank, the cave mouth and the parked craft behind them; then away toward the river and ONE continuous glide down
# into the riverbed portal (owner: the water portal felt like a step): a straight line at a constant speed, sloping
# 25 degrees; the exit frame is tilted to match, so the crossing changes nothing in the motion (crossing 5 at 121.2 s).
GATHER = [*near(CAMP_SITE, *GATHER_AT)[:1], CAMP_GROUND + 1.3, near(CAMP_SITE, *GATHER_AT)[1]]
RIVER_PORTAL = site(10.0, -12.0)
RIVER_Y = -0.5 * R
_tw = [RIVER_PORTAL[0] - CAMP[0], RIVER_PORTAL[1] - CAMP[1]]
_tn = math.hypot(*_tw)
TOWARD_RIVER = (_tw[0] / _tn, _tw[1] / _tn)
GLIDE_SLOPE = math.radians(25.0)
GLIDE_SPEED = 3.2                      # m/s along the slope
T_CROSS = 121.2


def glide_point(s_back):
    """The glide's point s_back metres (along the slope) before the portal: x, absolute y, z."""
    h = s_back * math.cos(GLIDE_SLOPE)
    return [RIVER_PORTAL[0] - TOWARD_RIVER[0] * h, RIVER_Y + s_back * math.sin(GLIDE_SLOPE), RIVER_PORTAL[1] - TOWARD_RIVER[1] * h]


def night_flight():
    # absolute heights near the camp (eye height over the camp's ground); the glide in absolute terms; both turned into
    # heights over the sampled ground (the portal's flight is authored over the ground)
    eye = CAMP_GROUND + 1.85
    near_knots = [(-1.0, -9.5, eye + 0.9, 110.2), (-5.5, -7.5, eye + 0.2, 112.0), (-8.0, -3.5, eye, 113.8)]
    glide_len = (eye + 0.3 - RIVER_Y) / math.sin(GLIDE_SLOPE)
    pts = []
    for dd, dl, y, t in near_knots:
        x, z = near(CAMP_SITE, dd, dl)
        pts.append((x, y, z, t))
    for frac in (1.0, 0.66, 0.33):
        sb = glide_len * frac
        x, y, z = glide_point(sb)
        pts.append((x, y, z, T_CROSS - sb / GLIDE_SPEED))
    x, y, z = glide_point(0.0)
    pts.append((x, y, z, T_CROSS))
    x, y, z = glide_point(-GLIDE_SPEED * 0.8)
    pts.append((x, y, z, T_CROSS + 0.8))
    try:
        gr = heights(CANYON_SCENE, [(p[0], p[2]) for p in pts])
    except Exception:
        gr = [CAMP_GROUND] * len(pts)
    out = [knot(42.0, 80.0, 40.0, 103.6), knot(35.0, 56.0, 20.0, 106.4), knot(28.0, 34.0, 7.0, 108.4)]
    out += [[x, y - g, z, t] for (x, y, z, t), g in zip(pts, gr)]
    return out


NIGHT_AIMS = [[GATHER[0], GATHER[1], GATHER[2], 105.2, 116.4]]

# --- the Mars flight (x, height over the ground, z, t): crossing 3 at 69.69 s (bar 36), crossing 4 at 101.4 s (bar 52)
# M3d (owner): one continuous fly-over -- out of the wall low, over the rover as it drives (no orbit), straight on to
# the pyramids and up the buried face into the portal. Times from a smooth speed profile (canyon_scale.pace).
_MARS_PATH = [(2.0, 8.0, -750.0), (16.0, 7.0, -840.0), (34.0, 6.0, -925.0), (52.0, 5.5, -985.0),
              (64.0, 6.5, -1030.0), (86.0, 14.0, -1140.0), (110.0, 30.0, -1300.0),
              (PORTAL_FOCUS[0] - 6.0, 50.0, PORTAL_FOCUS[2] + 150.0), (PORTAL_FOCUS[0], 66.0, PORTAL_FOCUS[2] + 55.0)]
MARS_FLIGHT = [list(k) for k in pace(_MARS_PATH, 71.4, 99.6, 22.0, 18.0, ramp_in=0.15, ramp_out=0.2)[0]] + [
    [PORTAL_FOCUS[0], None, PORTAL_FOCUS[2], 101.4],          # the crossing (y: the focus)
    [PORTAL_FOCUS[0], None, PORTAL_FOCUS[2] - 25.0, 102.2],
]

# a dusty regolith: rust and ochre by the generator's albedo and a broad noise, grit up close; M3: strata banding on the
# steep faces (bands of uneven width, warped), wind ripples on the flats near the camera
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
    # strata: a warped height into two cosine bands of unrelated period, weighted by the steepness
    {"kind": "noise", "dst": 3, "srcA": 2, "value": 0.012, "seed": 43},
    {"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, -9.0, 9.0]},
    {"kind": "swizzle", "dst": 4, "srcA": 2, "constant": [1, 1, 1, 1]},
    {"kind": "add", "dst": 4, "srcA": 4, "srcB": 3},
    {"kind": "palette", "dst": 6, "srcA": 4, "constant": [0.92, 0.92, 0.92, 0], "constant2": [0.14, 0.15, 0.17, 0],
     "constant3": [0.137, 0.137, 0.137, 0], "constant4": [0.0, 0.0, 0.0, 0], "value": 0.0},
    {"kind": "palette", "dst": 7, "srcA": 4, "constant": [1.0, 0.97, 0.95, 0], "constant2": [0.07, 0.08, 0.09, 0],
     "constant3": [0.41, 0.41, 0.41, 0], "constant4": [0.3, 0.3, 0.3, 0], "value": 0.0},
    {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
    {"kind": "input", "dst": 7, "input": "normal"},
    {"kind": "swizzle", "dst": 7, "srcA": 7, "constant": [1, 1, 1, 1]},
    {"kind": "smoothstep", "dst": 7, "srcA": 7, "constant": [0.82, 0.45, 0, 0]},     # 0 flat .. 1 steep
    {"kind": "constant", "dst": 3, "constant": [1.0, 1.0, 1.0, 1.0]},
    {"kind": "mixBy", "dst": 6, "srcA": 3, "srcB": 6, "srcC": 7},
    {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 6},
    # wind ripples on the flats, close to the camera: stripes across x, warped, faded by distance and on slopes
    {"kind": "noise", "dst": 3, "srcA": 2, "value": 0.05, "seed": 47},
    {"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, -2.5, 2.5]},
    {"kind": "swizzle", "dst": 4, "srcA": 2, "constant": [0, 0, 0, 0]},
    {"kind": "add", "dst": 4, "srcA": 4, "srcB": 3},
    {"kind": "palette", "dst": 6, "srcA": 4, "constant": [0.97, 0.97, 0.97, 0], "constant2": [0.06, 0.06, 0.06, 0],
     "constant3": [0.62, 0.62, 0.62, 0], "constant4": [0.0, 0.0, 0.0, 0], "value": 0.0},
    {"kind": "input", "dst": 4, "input": "cameraDistance"},
    {"kind": "remap", "dst": 4, "srcA": 4, "constant": [60.0, 220.0, 1.0, 0.0], "value": 1.0},
    {"kind": "remap", "dst": 7, "srcA": 7, "constant": [0.0, 1.0, 1.0, 0.0], "value": 1.0},
    {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 7},
    {"kind": "constant", "dst": 3, "constant": [1.0, 1.0, 1.0, 1.0]},
    {"kind": "mixBy", "dst": 6, "srcA": 3, "srcB": 6, "srcC": 4},
    {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 6},
    {"kind": "constant", "dst": 0, "constant": [0, 0, 0, 0]},
    {"kind": "constant", "dst": 1, "constant": [0.95, 0.95, 0.95, 0.95]},
]

# M3b: Curiosity (NASA, public domain; pass2/prepare_rover.py), driving slowly toward the pyramids to photograph them,
# its tracks behind it, its mast camera flashing. It drives on a pad (the yardangs eased flat under its route).
# M3c (owner: the rover is the arrival's subject): it is just ahead of the portal's exit, and the camera comes down to it
ROVER_START = (58.0, -990.0)          # M3d (owner): nearer the pyramids, a beat on the way, not the subject
_rd = unit([PYRAMIDS[1][1] - ROVER_START[0], 0.0, PYRAMIDS[1][2] - ROVER_START[1]])
ROVER_DIR = (_rd[0], _rd[2])
ROVER_YAW = math.degrees(math.atan2(ROVER_DIR[0], ROVER_DIR[1]))
ROVER_LEVEL = 3.2                      # the mean ground around the pad (sampled without it)
ROVER_SPEED = 0.4                      # m/s (faster than Curiosity's own: slow, but visibly moving up close)
ROVER_T0, ROVER_T1 = 69.0, 102.0
ROVER_MAST = (-0.418, 2.110, 0.466)    # the mast camera in the rover's frame (prepare_rover.py)
ROVER_PAD = dict(x=ROVER_START[0] + ROVER_DIR[0] * -10.0, z=ROVER_START[1] + ROVER_DIR[1] * -10.0, radius=60.0, level=ROVER_LEVEL)


def rover_at(t):
    d = ROVER_SPEED * (min(max(t, ROVER_T0), ROVER_T1) - ROVER_T0)
    return (ROVER_START[0] + ROVER_DIR[0] * d, ROVER_START[1] + ROVER_DIR[1] * d)


def mast_world(t):
    x, z = rover_at(t)
    a = math.radians(ROVER_YAW)
    mx, my, mz = ROVER_MAST
    return [x + mx * math.cos(a) + mz * math.sin(a), ROVER_LEVEL + my, z - mx * math.sin(a) + mz * math.cos(a)]


TRACK_BACK = 38                        # metres of track laid before the shot; then a segment per metre as it drives
TRACK_AHEAD = int(ROVER_SPEED * (ROVER_T1 - ROVER_T0))


def rover_tracks():
    """[(node, t_visible or None, keys)] for the project's timeline: the rover, its flash, the track segments."""
    keys = []
    pos = [(t, [*rover_at(t)[:1], ROVER_LEVEL, rover_at(t)[1]]) for t in (ROVER_T0, ROVER_T1)]
    keys.append({"target": "mars/nodes/rover/position", "keys": [{"time": t, "value": p, "interp": "linear"} for t, p in pos]})
    keys.append({"target": "mars/nodes/rover-flash/position",
                 "keys": [{"time": t, "value": mast_world(t), "interp": "linear"} for t in (ROVER_T0, ROVER_T1)]})
    flashes = []
    t = ROVER_T0 + 1.3
    while t < ROVER_T1:
        flashes += [(t, 0.0), (t + 0.0005, 1.0), (t + 0.12, 1.0), (t + 0.1205, 0.0)]
        t += 3.1
    keys.append({"target": "mars/nodes/rover-flash/opacity", "keys": [{"time": 0.0, "value": [0.0], "interp": "step"}] +
                 [{"time": a, "value": [b], "interp": "step"} for a, b in flashes]})
    keys.append({"target": "mars/lights/rover-flash-light/intensity", "keys": [{"time": 0.0, "value": [0.0], "interp": "step"}] +
                 [{"time": a, "value": [b * 900.0], "interp": "step"} for a, b in flashes]})
    keys.append({"target": "mars/lights/rover-flash-light/position",
                 "keys": [{"time": t_, "value": mast_world(t_), "interp": "linear"} for t_ in (ROVER_T0, ROVER_T1)]})
    for i in range(TRACK_AHEAD):   # segment i (i-1.6 to i-0.6 m along the drive) appears as the rear wheels leave it
        tv = ROVER_T0 + (i + 1.0) / ROVER_SPEED if i > 0 else 0.001
        keys.append({"target": f"mars/nodes/track-new-{i}/visible",
                     "keys": [{"time": 0.0, "value": [0.0], "interp": "step"}, {"time": tv, "value": [1.0], "interp": "step"}]})
    return keys


GOLD = [1.0, 0.62, 0.3]
A = "../../assets/strange-attractor/build/"


def rover_rocks():
    """M3c: rocks strewn round the rover's ground at the scale the camera sees them (the kit's basalt), clear of its
    drive and of the camera's low pass; on the rover's pad, so their ground is the pad's level."""
    import random
    rng = random.Random(1901)
    cam = [(k[0], k[2]) for k in MARS_FLIGHT[:7]]
    out = []
    while len(out) < 46:
        a, r = rng.uniform(0, 2 * math.pi), 5.0 + 48.0 * math.sqrt(rng.random())
        x, z = ROVER_START[0] + r * math.cos(a), ROVER_START[1] + r * math.sin(a)
        # off the drive (a band 4 m wide ahead and behind) and 3 m off the camera's path
        along = (x - ROVER_START[0]) * ROVER_DIR[0] + (z - ROVER_START[1]) * ROVER_DIR[1]
        across = abs(-(x - ROVER_START[0]) * ROVER_DIR[1] + (z - ROVER_START[1]) * ROVER_DIR[0])
        if across < 4.0 and -45.0 < along < 30.0:
            continue
        if min(math.hypot(x - cx, z - cz) for cx, cz in cam) < 3.0:
            continue
        k = len(out)
        sc = rng.uniform(0.6, 1.6) * (1.6 if rng.random() < 0.15 else 1.0)
        out.append({"name": f"rock-{k}", "kind": "gltf", "asset": A + f"basalt-{'abc'[k % 3]}.glb",
                    "position": [x, ROVER_LEVEL - 0.08 * sc, z], "rotation": [0, rng.uniform(0, 360), 0], "scale": [sc] * 3,
                    "visible": True})
    return out


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
            {"name": "rover-flash-light", "type": "point", "position": mast_world(ROVER_T0), "color": [1.0, 0.97, 0.9],
             "intensity": 0.0, "range": 12.0, "castsShadow": False, "volumetric": 0.0},
        ],
        "environment": {"intensity": 0.08, "background": SKY_HORIZON, "fogColor": HAZE, "volumeDensity": 0.00024,
                        "sky": {"enabled": True, "background": True, "useKeyLight": True, "zenithColor": SKY_ZENITH,
                                "horizonColor": SKY_HORIZON, "groundColor": [0.25, 0.14, 0.08], "haze": 0.3,
                                "sunColor": [1.0, 0.86, 0.7], "sunIntensity": 3.0, "sunSize": 0.025, "sunGlow": 0.6,
                                "intensity": 1.0},
                        "shadowRange": 2400.0, "shadowCascades": 4, "ambientOcclusion": False, "shadowCasterReach": 5000.0,
                        "contactShadows": False, "shadowSplitLambda": 0.9},
        "post": {"bloomIntensity": 0.04, "tonemap": 0},
        "materialPrograms": [{"name": "mars_regolith", "ops": regolith, "baseColor": 5, "metallic": 0, "roughness": 1}],
        "lunarWorld": {"seed": 11, "extent": 6000, "spacing": 2.5, "skirtExtent": 48000, "skirtSpacing": 48, "craters": 0.4,
                       "rocks": 0.095, "relief": 70, "program": "mars_regolith", "curvature": CURVATURE,
                       "heroes": CRATERS, "mesas": MESAS, "yardangs": 7.0, "wind": 35.0,
                       "pads": [{"x": x, "z": z, "radius": base * 1.4, "level": PAD_LEVEL[name]} for name, x, z, base, _ in PYRAMIDS] +
                               [ROVER_PAD]},
        # the pyramid portal: pyramid-b's south face cracks and opens, the same corruption as the canyon's, warmer
        "corruption": {"targets": ["pyramid-b"], "focus": PORTAL_FOCUS, "axis": [0.0, 0.0, -1.0], "amount": 1.0,
                       "radius": 120.0, "inner": 0.2, "fieldNear": 40.0, "fieldFar": 1700.0, "openNear": 30.0, "openFar": 520.0,
                       "seam": 0.3, "width": 22.0, "height": 17.0, "depth": 12.0, "block": 4.0, "share": 0.22, "slip": 2.5,
                       "rate": 0.5, "strip": 3.5, "stripSlip": 1.5, "slide": 0.0, "pull": 6.0, "lift": 1.2, "fold": 0.8,
                       "lag": 0.7, "glyph": 5.5, "glyphSize": 1.6, "glow": 4.0, "echoes": 1, "echoOffset": 1.0,
                       "glowColor": GOLD, "light": 2400.0},
        # home: the canyon, at night (its look from crossing 3), out of the outer cliff and down toward the camp's embers
        "portal": {"world": "@root", "exit": NIGHT_EXIT, "exitAxis": OUT_OF_CLIFF, "entryUp": [0.0, 1.0, 0.0],
                   "exitUp": [0.0, 1.0, 0.0], "flight": night_flight(), "aims": NIGHT_AIMS},
        "nodes": [{"name": name, "kind": "gltf", "asset": A + f"{name}.glb",
                   "position": [x, PYRAMID_GROUND[name] - PYRAMID_SINK, z], "rotation": [0, yaw, 0], "scale": [1, 1, 1],
                   "visible": True} for name, x, z, _, yaw in PYRAMIDS] + [
            {"name": "rover", "kind": "gltf", "asset": A + "rover.glb", "position": [*rover_at(ROVER_T0)[:1], ROVER_LEVEL, rover_at(ROVER_T0)[1]],
             "rotation": [0, ROVER_YAW, 0], "scale": [1, 1, 1], "visible": True},
            {"name": "rover-flash", "kind": "gltf", "asset": A + "flash.glb", "position": mast_world(ROVER_T0), "rotation": [0, 0, 0],
             "scale": [1, 1, 1], "visible": True},
        ] + [{"name": f"track-old-{i}", "kind": "gltf", "asset": A + "rover-tracks.glb",
              "position": [ROVER_START[0] + ROVER_DIR[0] * (i - 1.6), ROVER_LEVEL, ROVER_START[1] + ROVER_DIR[1] * (i - 1.6)],
              "rotation": [0, ROVER_YAW, 0], "scale": [1, 1, 1], "visible": True} for i in range(-TRACK_BACK, 0)] + [
             {"name": f"track-new-{i}", "kind": "gltf", "asset": A + "rover-tracks.glb",
              "position": [ROVER_START[0] + ROVER_DIR[0] * (i - 1.6), ROVER_LEVEL, ROVER_START[1] + ROVER_DIR[1] * (i - 1.6)],
              "rotation": [0, ROVER_YAW, 0], "scale": [1, 1, 1], "visible": False} for i in range(TRACK_AHEAD)] + rover_rocks(),
    }
    path = os.path.join(HERE, PREFIX + "strange-attractor-mars.scene.json")
    json.dump(scene, open(path, "w"), indent=2)
    return path, flight, []    # M3d: no aim -- the fly-over looks ahead, to the pyramids


if __name__ == "__main__":
    print(write()[0])

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
from canyon_scale import GAZE, WORLD_SCALE as WS, R, site, near, knot, O_UNIT, L_UNIT, CAMP_SITE, GATHER_AT, pace, line_points  # noqa: E402
from sample_ground import heights  # noqa: E402
CANYON_ROCK_OPS = 85   # the canyon rock program's length before build_journey adds its close-up detail (M3d)


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
# M3d (owner: Mars on the canyon's terrain pipeline, ADR-1902): the layout is carried onto the generated world, whose
# plain opens south of a mesa wall at (562, 2318) -- found by a search of the height field for a wall with 1.6 km of
# open plain in front of it; every place below is authored as before and offset by (OX, OZ)
OX, OZ = 562.0, 3042.0
PYRAMIDS = [("pyramid-a", -260.0 + OX, -1380.0 + OZ, 300.0, 8.0),
            ("pyramid-b", 160.0 + OX, -1680.0 + OZ, 280.0, 0.0),
            ("pyramid-c", 470.0 + OX, -1980.0 + OZ, 140.0, 14.0)]
# each pyramid stands on a pad (ADR-1896: the ground eased to a level under its footing, 1.5 half-bases across), so its
# sand plain meets the ground; the level is before the planet's curvature, which drops the ground by d^2 / 2R
CURVATURE = 3389.5e3
PLAIN = 35.0                          # M3d: the plain's level (sampled: 33.6-35.6 along the route)
PAD_LEVEL = {"pyramid-a": PLAIN, "pyramid-b": PLAIN, "pyramid-c": PLAIN}


def curved(level, x, z):   # M3d: the canyon pipeline's ground is flat (no curvature): absolute levels
    return level


def _curved_unused(level, x, z):
    return level - (x * x + z * z) / (2.0 * CURVATURE)


PYRAMID_GROUND = {name: curved(PAD_LEVEL[name], x, z) for name, x, z, _, _ in PYRAMIDS}
PYRAMID_SINK = 3.0

# --- the arrival: out of the arrival mesa's north wall, heading -z (the exit's y is over the ground)
# M3e (owner: the arrival clipped a bulge): the old exit sat in the butte's talus apron (the slope at 61 m just ahead
# of a 56.6 m exit). Now out of the butte's steep face itself, 4 m in front of it at 95 m (the apron below at 72-75 m),
# then down over the apron, which a pad clears to the plain ahead (MARS_CORRIDOR)
EXIT = [OX - 2.0, 95.0, -699.0 + OZ]  # absolute
EXIT_AXIS = [0.0, 0.0, -1.0]

# --- the pyramid portal: in pyramid-b's south face, where the casing has fallen, 30 m up (absolute y set by measure)
PB = PYRAMIDS[1]
FACE_HEIGHT = 74.0                  # M3b: above the deeper sand (it buries the base to about 46 m)
# (a corruption's focus y over a generated ground is a height over that ground: measured 2.7 m at the face's foot)
FOCUS_GROUND = 0.0     # M3d: over the canyon pipeline the focus's y is absolute
PORTAL_FOCUS = [PB[1], PYRAMID_GROUND["pyramid-b"] - 3.0 + FACE_HEIGHT - FOCUS_GROUND,
                PB[2] + PB[3] * 0.5 - FACE_HEIGHT / math.tan(math.radians(51.8))]

# --- home, at night. M3g (owner 54- §4: not the high swoop down; a level pass-by): out of the low rock face of the camp's
# own bank, well down-canyon (d 68, where the face climbs from 1.9 m at lat 31 to 8.4 m at lat 34), at 5 m, 1.5 m in
# front of it, facing out over the river and up-canyon; then level, 3.5-5 m up, along the river past the camp (the
# figures glimpsed in passing), and on into the river portal's dive
NIGHT_EXIT_DL = (68.0, 30.9)
# Owner 2026-11-08 (67- §5): the entrance off the rock (the camera came out through the far bank's face), out over the
# water on the river's far half, so nothing of the bank is in the arrival's volume
NIGHT_EXIT = [38.0, 3.5, 1212.0]
CANYON_SCENE = "strange-attractor-flythrough.scene.json"
CAMP = site(*CAMP_SITE)
CAMP_GROUND = 4.4 * WS
# M3b (owner: the night needs a real pass; a lower, closer framing, not tiny figures from high up): down from the cliff
# over the benches, then low -- eye height, 6-9 m from the fire -- in a slow arc round the gathering on the river side,
# the bank, the cave mouth and the parked craft behind them; then away toward the river and ONE continuous glide down
# into the riverbed portal (owner: the water portal felt like a step): a straight line at a constant speed, sloping
# 25 degrees; the exit frame is tilted to match, so the crossing changes nothing in the motion (crossing 5 at 121.2 s).
GATHER = [*near(CAMP_SITE, *GATHER_AT)[:1], CAMP_GROUND + 1.3, near(CAMP_SITE, *GATHER_AT)[1]]
RIVER_PORTAL = [28.0, 1418.0]   # owner 2026-11-08 (67- §5): centred in the river, beyond the camp
RIVER_Y = -0.5 * R
# 67- §5: one gentle curve from the entrance, bowed toward the camp's bank (the camp on the left, about 20 m off; in frame 12.4 s), then
# a Moon-style rise and dive into the portal at the river's centre. NIGHT_CTRL is the curve's control point; the rise
# starts at RISE_FROM of the way along, tops out at RISE_TOP m and dives into the opening on the curve's last heading
NIGHT_CTRL = (76.0, 1372.0)
RISE_FROM, RISE_PEAK, RISE_TOP = 0.66, 0.84, 16.0
LEVEL_Y = 3.5


def _night_xz(f):
    (ax, az), (cx, cz), (bx, bz) = (NIGHT_EXIT[0], NIGHT_EXIT[2]), NIGHT_CTRL, RIVER_PORTAL
    return ((1 - f) ** 2 * ax + 2 * (1 - f) * f * cx + f * f * bx, (1 - f) ** 2 * az + 2 * (1 - f) * f * cz + f * f * bz)


def _night_y(f):
    if f <= RISE_FROM:
        return LEVEL_Y
    if f <= RISE_PEAK:
        u = (f - RISE_FROM) / (RISE_PEAK - RISE_FROM)
        return LEVEL_Y + (RISE_TOP - LEVEL_Y) * u * u * (3 - 2 * u)
    u = (f - RISE_PEAK) / (1.0 - RISE_PEAK)
    return RISE_TOP - (RISE_TOP - RIVER_Y) * u * u     # eased in from the top, steepest into the opening


_x0, _x1 = _night_xz(0.0), _night_xz(0.02)
_on = math.hypot(_x1[0] - _x0[0], _x1[1] - _x0[1])
OUT_OF_CLIFF = [(_x1[0] - _x0[0]) / _on, 0.0, (_x1[1] - _x0[1]) / _on]   # the exit faces along the curve's start
_e0, _e1 = _night_xz(0.985), _night_xz(1.0)
_tn = math.hypot(_e1[0] - _e0[0], _e1[1] - _e0[1])
TOWARD_RIVER = ((_e1[0] - _e0[0]) / _tn, (_e1[1] - _e0[1]) / _tn)        # the dive's heading at the opening
GLIDE_SLOPE = math.atan2(_night_y(0.985) - RIVER_Y, _tn)                  # the dive's slope at the opening
GLIDE_SPEED = 6.0                      # m/s along the slope at the opening (the ocean's arrival speed)
T_CROSS = 121.2 + 1.0 / 60.0   # M3d (ADR-1903): half a frame off the 30 fps grid, so no frame lands on the crossing


def glide_point(s_back):
    """The point s_back metres (along the dive's slope at the opening) before the portal: x, absolute y, z."""
    h = s_back * math.cos(GLIDE_SLOPE)
    return [RIVER_PORTAL[0] - TOWARD_RIVER[0] * h, RIVER_Y + s_back * math.sin(GLIDE_SLOPE), RIVER_PORTAL[1] - TOWARD_RIVER[1] * h]


def night_flight():
    """The night pass (67- §5): out over the water, one gentle curve down the river past the camp (on the left), then
    up and over into a dive into the river portal at the river's centre, like the Moon's. Knots are heights over the
    ground; the whole line is over water."""
    n = 60
    pts = [(*_night_xz(i / n), _night_y(i / n)) for i in range(n + 1)]
    pts = [[x, y, z] for x, z, y in pts]
    # (no knot in the first 14 m: the arrival covers them in the first second, and a nearer knot makes the spline
    # overshoot and come back)
    keep = [pts[0]] + [p for p in pts[1:] if math.dist((p[0], p[2]), (NIGHT_EXIT[0], NIGHT_EXIT[2])) > 14.0]
    knots, _ = pace(keep[1:], T_PYR + 1.0, T_CROSS, 10.0, GLIDE_SPEED, ramp_in=0.04, ramp_out=0.12)
    after = glide_point(-GLIDE_SPEED * 0.8)
    gr = heights(CANYON_SCENE, [(k[0], k[2]) for k in knots] + [(after[0], after[2])])
    out = [[k[0], k[1] - g, k[2], k[3]] for k, g in zip(knots, gr)]
    out.append([after[0], after[1] - gr[-1], after[2], T_CROSS + 0.8])
    return out



# --- the Mars flight (x, height over the ground, z, t): crossing 3 at 69.69 s (bar 36), crossing 4 at 101.4 s (bar 52)
# M3d (owner): one continuous fly-over -- out of the wall low, over the rover as it drives (no orbit), straight on to
# the pyramids and up the buried face into the portal. Times from a smooth speed profile (canyon_scale.pace).
_MARS_PATH = [(2.0 + OX, 48.0, -750.0 + OZ), (16.0 + OX, 22.0, -840.0 + OZ), (34.0 + OX, 6.0, -925.0 + OZ),
              (52.0 + OX, 5.5, -985.0 + OZ), (64.0 + OX, 6.5, -1030.0 + OZ), (86.0 + OX, 14.0, -1140.0 + OZ),
              (110.0 + OX, 30.0, -1300.0 + OZ),
              (PORTAL_FOCUS[0] - 6.0, 50.0, PORTAL_FOCUS[2] + 150.0), (PORTAL_FOCUS[0], 66.0, PORTAL_FOCUS[2] + 55.0)]
# M3e (owner: a mound hid the pyramid, and the arrival clipped the apron): a corridor along the fly-over is eased to the
# plain (ADR-1897 pads, 34 m apart, 58 m across each), from beyond the butte's apron to pyramid-b's own pad, so
# no dune, yardang or drift stands between the camera and the pyramids or under the camera
def _corridor():
    # (from the second knot: the camera clears the butte's apron from above, and a pad there cut it into a flat slab)
    pts = [(x, z) for x, _, z in _MARS_PATH[1:7]] + [(PORTAL_FOCUS[0] - 6.0, PB[2] + 330.0)]
    pads = []
    for (x0, z0), (x1, z1) in zip(pts, pts[1:]):
        n = max(1, int(math.hypot(x1 - x0, z1 - z0) / 34.0))
        for i in range(n):
            t = i / n
            pads.append({"x": round(x0 + (x1 - x0) * t, 2), "z": round(z0 + (z1 - z0) * t, 2), "radius": 58.0,
                         "level": PLAIN})
    return pads


MARS_CORRIDOR = _corridor()
# M3e (owner, reversing M3d's "sooner"): the pyramid's code starts only once the camera has passed the rover -- the field
# reaches out to the camera's distance from the focus at the knot over the rover, less 15 m; the tear starts 200 m nearer
# (M3d: 1700 m and 520 m)
_over_rover = _MARS_PATH[3]
PYR_FIELD_FAR = round(math.dist((_over_rover[0], PLAIN + _over_rover[1], _over_rover[2]), PORTAL_FOCUS) - 15.0, 1)
PYR_OPEN_FAR = round(PYR_FIELD_FAR - 200.0, 1)
# M3g (owner 54- §3: shorter or faster): four bars shorter -- the pyramid crossing moves from bar 52 (101.4 s) to bar
# 48 (93.46 s; 120.97 bpm, a bar 1.984 s), so the same line is flown about a third faster; the night leg gains the 7.9 s
T_PYR = 101.4 - 4 * 4 * 60.0 / 120.97
# Pass 3 (61- §3, the A-to-B rule; owner OK 2026-11-05): the fly-over on one straight line from the exit's first knot
# to the pyramid's face (the M3d path wandered 2% off it), its heights as before by distance along, at an even pace
_mars_h = []
_acc = [0.0]
for _a, _b in zip(_MARS_PATH, _MARS_PATH[1:]):
    _acc.append(_acc[-1] + math.hypot(_b[0] - _a[0], _b[2] - _a[2]))
_MARS_LINE = line_points((_MARS_PATH[0][0], _MARS_PATH[0][2]), (_MARS_PATH[-1][0], _MARS_PATH[-1][2]),
                         [(a / _acc[-1], p[1]) for a, p in zip(_acc, _MARS_PATH)], n=14)
MARS_FLIGHT = [list(k) for k in pace([list(p) for p in _MARS_LINE], 71.4, T_PYR - 1.8, 24.0, 20.0, ramp_in=0.06, ramp_out=0.08)[0]] + [
    [PORTAL_FOCUS[0], None, PORTAL_FOCUS[2], T_PYR + 1.0 / 60.0],   # the crossing, half a frame off the grid (ADR-1903)
    [PORTAL_FOCUS[0], None, PORTAL_FOCUS[2] - 25.0, T_PYR + 0.8],
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
ROVER_START = (58.0 + OX, -990.0 + OZ)          # M3d (owner): nearer the pyramids, a beat on the way, not the subject
_rd = unit([PYRAMIDS[1][1] - ROVER_START[0], 0.0, PYRAMIDS[1][2] - ROVER_START[1]])
ROVER_DIR = (_rd[0], _rd[2])
ROVER_YAW = math.degrees(math.atan2(ROVER_DIR[0], ROVER_DIR[1]))
ROVER_LEVEL = 35.2                     # the mean ground around the pad (sampled without it)
ROVER_SPEED = 0.4                      # m/s (faster than Curiosity's own: slow, but visibly moving up close)
ROVER_T0, ROVER_T1 = 69.0, 94.0
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
        # M3d (owner: the rocks read as dark lumps): photoscanned boulders (Poly Haven, CC0), rust-tinted, sunk a
        # little into the sand
        ph = ["namaqualand_boulder_02", "namaqualand_boulder_03", "namaqualand_boulder_04", "namaqualand_boulder_05"][k % 4]
        sc *= 0.45
        out.append({"name": f"rock-{k}", "kind": "gltf", "asset": A + f"ph-{ph}.glb",
                    "position": [x, ROVER_LEVEL - 0.12 * sc, z], "rotation": [0, rng.uniform(0, 360), 0], "scale": [sc] * 3,
                    "visible": True, "tint": [1.0, 0.58, 0.4]})
    return out


def mars_rock_program():
    """M3d: the canyon's rock program (strata, ledges, the contour carrier), its palette turned to Mars's -- rust,
    ochre and butterscotch for the canyon's reds and violets."""
    f = json.load(open(os.path.join(HERE, "strange-attractor-flythrough.scene.json")))
    prog = json.loads(json.dumps([p for p in f["materialPrograms"] if p["name"] == "canyonRock"][0]))
    prog["ops"] = prog["ops"][:CANYON_ROCK_OPS]     # the canyon's own ops, without its close-up detail (M3d)
    # Pass 3, 67- §4: without the wall glow's last 4 ops (the `glow` key is 0 on Mars and nothing routes it), for the
    # op budget the sand below needs
    assert [o["kind"] for o in prog["ops"][81:85]] == ["swizzle", "multiply", "multiply", "add"]
    prog["ops"] = prog["ops"][:81]
    prog.pop("textures", None)
    prog["name"] = "marsRock"
    # M3d (owner: a proper sand with ripples, photoscanned close up): a rippled sand (Poly Haven `aerial_sand`, CC0;
    # never committed), triplanar in world space at 5 m a tile, rust-tinted, laid on the flats near the camera and faded
    # out by 220 m, where the program's own strata and colour carry the wide shots
    prog["textures"] = [{"name": "sand", "path": "../../assets/strange-attractor/polyhaven/textures/aerial_sand/aerial_sand_diff_2k.jpg"}]
    prog["ops"] += [
        # M3e (owner: no visible tiling; ADR-1905): hex-tiled reads, a second larger layer, macro variation over 30 m
        {"kind": "textureTriplanar", "dst": 0, "texture": "sand", "value": 0.2, "constant3": [1.0, 0.5, 6.0, 0.7]},
        {"kind": "constant", "dst": 1, "constant": [1.05, 0.6, 0.42, 1.0]},
        {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 1},
        {"kind": "input", "dst": 3, "input": "normal"},
        {"kind": "swizzle", "dst": 3, "srcA": 3, "constant": [1, 1, 1, 1]},
        # Pass 3, 67- §4: the sand-to-rock line broken by a noise (9 m), not drawn along one slope contour
        {"kind": "input", "dst": 1, "input": "worldPosition"},
        {"kind": "noise", "dst": 1, "srcA": 1, "value": 1.0 / 9.0, "seed": 67},
        {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.0, 1.0, -0.12, 0.12]},
        {"kind": "add", "dst": 3, "srcA": 3, "srcB": 1},
        # M3g (owner 54- §3: one sand family; the drifts, the plain and the dunes were three looks): the same sand as the
        # drifts (marsDrift) on everything but the steep rock -- at every distance, not faded out by 220 m to the
        # program's flat colour -- blended to the rock over a wide band of slopes (normal.y 0.45-0.75), so an apron or
        # a dune's flank is sand, not a pale pink slab
        # Pass 3, 67- §4 (the owner's screenshot: a pale, smooth slab beside the darker ground): sand up to much
        # steeper slopes (normal.y 0.2-0.45, about 63-78 degrees, was 0.45-0.75), so dune flanks, the yardangs' sides
        # and banks are the ground's sand and only cliffs and mesa walls take the rock palette
        {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.2, 0.45, 0, 0]},
        {"kind": "mixBy", "dst": 4, "srcA": 4, "srcB": 0, "srcC": 3},
        # Pass 3, 67- §4 (owner: the piles and the ground are two surfaces): the sand's ripple normals and roughness
        # where it is sand, exactly as the drifts (marsDrift) draw them, in place of the canyon's strata relief (the
        # "scaly" dunes of the owner's screenshot); the rock keeps its own relief
        SAND_NORMAL,
        {"kind": "mixBy", "dst": 6, "srcA": 6, "srcB": 1, "srcC": 3},
        SAND_ROUGHNESS,
    ]
    prog["textures"].append(SAND_NORMAL_TEXTURE)
    prog["roughness"] = 5
    return prog


# Pass 3, 67- §4: one sand. The drifts and the plain share the photoscan, its tint, its hex tiling (ADR-1905), its
# ripple normals and its roughness. The drift also takes the terrain's occlusion output: marsRock's occlusion register
# (r0) carries the sand texture times its tint (the M3d ops write it there), so the plain's ambient has been scaled by
# it since M3d; the drift had none, which made it the pale, flat slab of the owner's screenshot. Matching it keeps the
# plain's approved brightness and makes the two lit alike.
SAND_NORMAL_TEXTURE = {"name": "sandN", "path": "../../assets/strange-attractor/polyhaven/textures/aerial_sand/aerial_sand_nor_gl_2k.jpg",
                       "colorSpace": "linear"}
SAND_NORMAL = {"kind": "textureTriplanarNormal", "dst": 1, "texture": "sandN", "value": 0.2, "constant3": [1.0, 0.0, 0.0, 0.0]}
SAND_ROUGHNESS = {"kind": "constant", "dst": 5, "constant": [0.95, 0.95, 0.95, 1.0]}


def mars_drift_program():
    """M3e (owner: no visible tiling): the pyramids' sand drifts drawn through the plain's own sand (ADR-1572), so the
    drift and the plain are one sand -- the same photoscan, tint and hex tiling (ADR-1905) -- with its ripple normals."""
    return {"name": "marsDrift", "baseColor": 0, "metallic": -1, "roughness": 5, "emission": -1, "opacity": -1,
            "normal": 2, "occlusion": 0,
            "textures": [{"name": "sand", "path": "../../assets/strange-attractor/polyhaven/textures/aerial_sand/aerial_sand_diff_2k.jpg"},
                         {"name": "sandN", "path": "../../assets/strange-attractor/polyhaven/textures/aerial_sand/aerial_sand_nor_gl_2k.jpg",
                          "colorSpace": "linear"}],
            "ops": [
                {"kind": "textureTriplanar", "dst": 0, "texture": "sand", "value": 0.2, "constant3": [1.0, 0.5, 6.0, 0.7]},
                {"kind": "constant", "dst": 1, "constant": [1.05, 0.6, 0.42, 1.0]},
                {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 1},
                {"kind": "textureTriplanarNormal", "dst": 2, "texture": "sandN", "value": 0.2, "constant3": [1.0, 0.0, 0.0, 0.0]},
                SAND_ROUGHNESS,
            ]}


def write():
    flight = []
    for x, h, z, t in MARS_FLIGHT:
        if h is None:  # through the face: hold the focus's height over the ground at its foot (the plain)
            h = PORTAL_FOCUS[1] - PLAIN
        flight.append([x, h, z, t])
    scene = {
        "format": "avgen-scene", "version": 1, "name": "the-strange-attractor-mars",
        "_note": "The Strange Attractor's Mars, Cydonia (Art Pass 2, M2), written by build_mars.py; reached by the canyon's "
                 "second portal (ADR-1895), left by the pyramid portal home to the canyon at night.",
        "camera": {"mode": 1, "position": [0, 60, 300], "target": [0, 40, -1000], "fov": 44.0, "orbitSpeed": 0.0, "far": 60000.0},
        "lights": [
            # (M3d: the key is the canyon pipeline's own sun, steered by the look)
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
        "post": {"bloomIntensity": 0.04, "tonemap": 1},
        "materialPrograms": [mars_rock_program(), mars_drift_program()],
        # M3d (owner; ADR-1902): the canyon's own terrain pipeline in its Mars style -- wind-eroded mesas and buttes in
        # layered sediment, yardangs, dune fields, the same erosion, strata and terraces -- and its rock program
        "attractorWorld": {"seed": 11, "resolution": 4096, "extent": 16000, "worldScale": 0.5, "camera": "scene",
                           "terrainScale": 1.0, "roughness": 0.6, "ridgeStrength": 0.6, "domainWarp": 0.5, "terraces": 0.6,
                           "erosion": 0.6, "strataFold": 0.5, "branchingDensity": 0.0, "meshSpacing": 1.0,
                           "meshError": 0.003, "meshMinError": 0.2, "terrainProgram": "marsRock", "sunLux": 7.0,
                           "sunShadowBias": 1.6, "look": {"sunAzimuth": 128.0, "sunElevation": 14.0},
                           "mars": {"mesaCover": 0.45, "yardangs": 10.0, "wind": 30.0, "dunes": 8.0},
                           "pads": [{"x": x, "z": z, "radius": base * 1.2, "level": PAD_LEVEL[name]}
                                    for name, x, z, base, _ in PYRAMIDS] + MARS_CORRIDOR + [ROVER_PAD]},
        # the pyramid portal: pyramid-b's south face cracks and opens, the same corruption as the canyon's, warmer
        "corruption": {"targets": ["pyramid-b"], "focus": PORTAL_FOCUS, "axis": [0.0, 0.0, -1.0], "amount": 1.0,
                       "radius": 120.0, "inner": 0.2, "fieldNear": 40.0, "fieldFar": PYR_FIELD_FAR, "openNear": 30.0, "openFar": PYR_OPEN_FAR,
                       "seam": 0.3, "width": 22.0, "height": 17.0, "depth": 12.0, "block": 4.0, "share": 0.22, "slip": 2.5,
                       "rate": 0.5, "strip": 3.5, "stripSlip": 1.5, "slide": 0.0, "pull": 6.0, "lift": 1.2, "fold": 0.8,
                       "lag": 0.7, "glyph": 5.5, "glyphSize": 1.6, "glow": 4.0, "echoes": 1, "echoOffset": 1.0,
                       "glowColor": GOLD, "light": 2400.0},
        # home: the canyon, at night (its look from crossing 3), out of the outer cliff and down toward the camp's embers
        # M3d (owner: the Moon portal is the reference look): the mesh wire on the portal's own surface -- its
        # scan, its rim, and the shards that break off the lips and fly into the opening (the pyramid had none)
        "meshWire": {"targets": ["pyramid-b"], "color": [1.0, 0.62, 0.3], "intensity": 0.0, "width": 1.0, "creaseLow": 2.0,
                     "creaseHigh": 12.0, "reveal": 1.0, "fadeNear": 60.0, "fadeFar": 900.0, "minPixels": 14.0, "wave": 0.0,
                     "waveRadius": 300.0, "waveWidth": 40.0, "waveGain": 1.6, "waveAhead": 200.0, "clipBelow": -1000.0,
                     "fromFocus": 1.0, "pulse": 1.2, "pulseSpacing": 60.0, "pulseSpeed": 40.0, "pulseWidth": 6.0,
                     "focusGain": 0.5, "rimGain": 6.0, "frontColor": [1.0, 0.82, 0.5], "shards": 0.3, "shardRate": 0.35},
        # 67- §5: climbLook 0.4, so the rise before the dive is not a stare up at the sky
        "portal": {**GAZE, "climbLook": 0.4, "world": "@root", "exit": NIGHT_EXIT, "exitAxis": OUT_OF_CLIFF,
                   "entryUp": [0.0, 1.0, 0.0],
                   "exitUp": [0.0, 1.0, 0.0], "flight": night_flight()},
        "nodes": [{"name": name, "kind": "gltf", "asset": A + f"{name}-m3g.glb",
                   "position": [x, PYRAMID_GROUND[name] - PYRAMID_SINK, z], "rotation": [0, yaw, 0], "scale": [1, 1, 1],
                   "visible": True} for name, x, z, _, yaw in PYRAMIDS] + [
            # M3e: each pyramid's sand drift, drawn through the plain's sand (marsDrift; ADR-1572, ADR-1905)
            # Pass 3, 67- §4: the drifts laid on the real ground (hero/build_mars_sand.py), meeting it at a thin, nearly
            # tangent, noise-broken toe
            {"name": f"{name}-sand", "kind": "gltf", "asset": "../../assets/strange-attractor/hero/mars/" + f"{name}-sand-p3.glb",
             "position": [x, PYRAMID_GROUND[name] - PYRAMID_SINK, z], "rotation": [0, yaw, 0], "scale": [1, 1, 1],
             "visible": True, "program": "marsDrift"} for name, x, z, _, yaw in PYRAMIDS] + [
            {"name": "rover", "kind": "gltf", "asset": A + "rover.glb", "position": [*rover_at(ROVER_T0)[:1], ROVER_LEVEL, rover_at(ROVER_T0)[1]],
             "rotation": [0, ROVER_YAW, 0], "scale": [1, 1, 1], "visible": True},
            {"name": "rover-flash", "kind": "gltf", "asset": A + "flash.glb", "position": mast_world(ROVER_T0), "rotation": [0, 0, 0],
             "scale": [1, 1, 1], "visible": True},
        ] + [{"name": f"track-old-{i}", "kind": "gltf", "asset": A + "rover-tracks.glb",
              "position": [ROVER_START[0] + ROVER_DIR[0] * (i - 1.6), ROVER_LEVEL, ROVER_START[1] + ROVER_DIR[1] * (i - 1.6)],
              "rotation": [0, ROVER_YAW, 0], "scale": [1, max(0.15, 1.0 + i / TRACK_BACK), 1], "visible": True,
              "opacity": max(0.0, min(1.0, 1.15 + 1.1 * i / TRACK_BACK))} for i in range(-TRACK_BACK, 0)] + [
             {"name": f"track-new-{i}", "kind": "gltf", "asset": A + "rover-tracks.glb",
              "position": [ROVER_START[0] + ROVER_DIR[0] * (i - 1.6), ROVER_LEVEL, ROVER_START[1] + ROVER_DIR[1] * (i - 1.6)],
              "rotation": [0, ROVER_YAW, 0], "scale": [1, 1, 1], "visible": False} for i in range(TRACK_AHEAD)] + rover_rocks(),
    }
    path = os.path.join(HERE, "strange-attractor-mars.scene.json")
    json.dump(scene, open(path, "w"), indent=2)
    # M3e (owner §3: the pyramid reads over the rover for the whole fly-over): the eye held on pyramid-b's upper courses
    # from the arrival until the climb to the portal, released 4 s before the crossing so the camera lines up with it
    return path, flight, [[PB[1], PLAIN + 75.0, PB[2], 69.9, T_PYR - 4.0]]


if __name__ == "__main__":
    print(write()[0])

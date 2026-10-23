"""The Living Prism (aurora revision): the valley's layout, AV Gen coordinates (metres, +Y up, the camera looks -Z).

One source of truth for the Blender assets, the scene and the project. The terrain's relief comes from world
features only (no noise octaves), so `height()` below is the engine's own height for this world
(src/world/world_map.cpp: featureWeight, the ridge/valley raise, the river cut down to its level, Chaikin smoothing),
and a crystal placed "on the ground" here stands on the ground the engine draws.
"""
import math

# ---- the camera: static, high, looking down the valley (spec 4.1) ------------------------------------------------
CAMERA_POS = (4.0, 34.0, 60.0)
CAMERA_TARGET = (-2.0, 20.0, -300.0)
CAMERA_FOV = 50.0
FAR = 4000.0

# ---- the river: one winding water feature, descending gently into the distance (spec 4.3) ------------------------
RIVER_PATH = [(-6.0, 0.0, 70.0), (8.0, -0.4, 10.0), (14.0, -0.9, -50.0), (-14.0, -1.6, -120.0), (-20.0, -2.4, -190.0),
              (12.0, -3.4, -280.0), (18.0, -4.4, -360.0), (-8.0, -6.0, -470.0), (2.0, -8.0, -620.0),
              (0.0, -10.0, -820.0)]
RIVER = {"name": "river", "kind": "river", "path": RIVER_PATH, "width": 24.0, "amplitude": 2.6, "falloff": 0.7,
         "smoothing": 3}
FLOOR = 1.2   # the valley floor's base height, above the water

# ---- the valley: two walls following the river at a distance, and the far mountains ------------------------------
def _offset(path, d):
    """The river path shifted sideways by d metres (left is negative x relative to the flow)."""
    out = []
    for i, p in enumerate(path):
        a = path[max(i - 1, 0)]
        b = path[min(i + 1, len(path) - 1)]
        tx, tz = b[0] - a[0], b[2] - a[2]
        n = math.hypot(tx, tz) or 1.0
        out.append((p[0] - tz / n * d, 0.0, p[2] + tx / n * d))
    return out


WALLS = [  # (name, path, amplitude, width, falloff): flat banks within ~55 m of the river, then low walls and high
    # ground -- kept low so the sky keeps the upper 45% of the frame (the reference's horizon sits at ~43%)
    ("wallL", _offset(RIVER_PATH, 150.0), 34.0, 105.0, 1.0),
    ("wallR", _offset(RIVER_PATH, -150.0), 38.0, 105.0, 1.0),
    ("highL", _offset(RIVER_PATH, 280.0), 80.0, 150.0, 1.0),
    ("highR", _offset(RIVER_PATH, -280.0), 86.0, 150.0, 1.0),
    ("outerL", _offset(RIVER_PATH, 470.0), 140.0, 220.0, 1.0),
    ("outerR", _offset(RIVER_PATH, -470.0), 150.0, 220.0, 1.0),
    ("benchL", _offset(RIVER_PATH, 40.0), 3.5, 24.0, 1.0),    # a low shelf the crystals stand on
    ("benchR", _offset(RIVER_PATH, -40.0), 4.0, 24.0, 1.0),
]
MOUNTAINS = [  # far ridges, layered, converging on the river's vanishing point, leaving a gap of horizon glow there
    ("rangeNear", [(-900.0, 0.0, -800.0), (-230.0, 0.0, -1100.0)], 135.0, 260.0, 1.0),
    ("rangeNearR", [(230.0, 0.0, -1150.0), (900.0, 0.0, -850.0)], 145.0, 260.0, 1.0),
    ("rangeMid", [(-1300.0, 0.0, -1500.0), (-200.0, 0.0, -1800.0), (300.0, 0.0, -1750.0), (1300.0, 0.0, -1450.0)],
     185.0, 320.0, 1.0),
    ("rangeFar", [(-1600.0, 0.0, -2300.0), (0.0, 0.0, -2600.0), (1600.0, 0.0, -2300.0)], 260.0, 400.0, 1.0),
]
WORLD_SIZE = (4000.0, 6000.0)



def _chaikin(pts, iters):
    for _ in range(iters):
        if len(pts) < 3:
            break
        out = [pts[0]]
        for a, b in zip(pts, pts[1:]):
            out.append(tuple(0.75 * a[k] + 0.25 * b[k] for k in range(3)))
            out.append(tuple(0.25 * a[k] + 0.75 * b[k] for k in range(3)))
        out.append(pts[-1])
        pts = out
    return pts


_RIVER_S = _chaikin(RIVER_PATH, RIVER["smoothing"])


def _closest(path, x, z):
    best = (1e18, 0.0)
    for a, b in zip(path, path[1:]):
        dx, dz = b[0] - a[0], b[2] - a[2]
        L2 = dx * dx + dz * dz
        t = 0.0 if L2 <= 0 else min(max(((x - a[0]) * dx + (z - a[2]) * dz) / L2, 0.0), 1.0)
        px, pz = a[0] + t * dx, a[2] + t * dz
        d = math.hypot(x - px, z - pz)
        if d < best[0]:
            best = (d, a[1] + t * (b[1] - a[1]))
    return best


def _w(d, width, falloff):
    if width <= 0:
        return 0.0
    u = min(max(1.0 - d / width, 0.0), 1.0)
    s = u * u * (3.0 - 2.0 * u)
    return s if falloff == 1.0 else s ** max(falloff, 0.01)


def height(x, z):
    h = FLOOR
    for _, path, amp, width, fo in WALLS + MOUNTAINS:
        d, _l = _closest(path, x, z)
        h += amp * _w(d, width, fo)
    d, level = _closest(_RIVER_S, x, z)
    w = _w(d, RIVER["width"], RIVER["falloff"])
    if w > 0:
        h = min(h, h + (min(h, level - RIVER["amplitude"]) - h) * w)
    return h


def world_json():
    feats = [{"name": "river", "kind": "river", "path": [list(p) for p in RIVER_PATH], "width": RIVER["width"],
              "amplitude": RIVER["amplitude"], "falloff": RIVER["falloff"], "roughness": 0.0,
              "smoothing": RIVER["smoothing"], "water": True, "waterDepth": 0.0}]
    for name, path, amp, width, fo in WALLS + MOUNTAINS:
        feats.append({"name": name, "kind": "ridge", "path": [list(p) for p in path], "width": width,
                      "amplitude": amp, "falloff": fo, "roughness": 0.0, "smoothing": 2})
    return {"name": "living-prism-aurora", "seed": 20261023, "size": list(WORLD_SIZE), "baseHeight": FLOOR,
            "erosion": 0.0, "seaLevel": -1000.0, "layers": [{"frequency": 0.01, "amplitude": 0.0}],
            "features": feats}


# NB: ridge paths are smoothed in the engine (smoothing 2); height() uses the raw polyline for them, which differs by
# centimetres at these widths. The river, whose level matters, is smoothed identically.

# ---- the crystal formations: about seven, about twenty-two tips (spec 4.4) -----------------------------------------
# (name, x, z, yaw degrees, scale, family). Families are built in Blender: cluster (tall leaning shards), pair (wide
# tilted prisms), spire (one or two tall), small (distant tips). Positions sit on the benches beside the river.
FORMATIONS = [
    ("L1", -44.0, -30.0, 20.0, 1.25, "cluster"),     # the hero: near left, leaning out over the river
    ("R1", 50.0, -72.0, -30.0, 1.9, "pair"),        # the wide right pair
    ("L2", -48.0, -165.0, 40.0, 1.35, "spire"),     # the first bend, left
    ("R2", 58.0, -250.0, -10.0, 1.5, "spire2"),     # the second bend, right
    ("C1", -36.0, -330.0, 15.0, 1.2, "small"),      # mid-distance, left
    ("C2", 42.0, -420.0, -25.0, 1.3, "small2"),     # mid-distance, right
    ("D", -44.0, -640.0, 0.0, 1.5, "far"),          # the horizon group
]


def quat_y(deg):
    h = math.radians(deg) / 2
    return (0.0, math.sin(h), 0.0, math.cos(h))

"""The Living Prism (aurora revision): the valley's layout, AV Gen coordinates (metres, +Y up, the camera looks -Z).

One source of truth for the Blender assets, the scene and the project. The terrain's relief comes from world
features only (no noise octaves), so `height()` below is the engine's own height for this world
(src/world/world_map.cpp: featureWeight, the ridge/valley raise, the river cut down to its level, Chaikin smoothing),
and a crystal placed "on the ground" here stands on the ground the engine draws.
"""
import math

# ---- the camera: static, high, looking down the canyon (spec 4.1; phase 2b: higher and a little more down-looking,
# so the wide near river fills the lower frame as the reference's does)
CAMERA_POS = (0.0, 80.0, 170.0)
CAMERA_TARGET = (0.0, 22.0, -250.0)
CAMERA_FOV = 52.0
FAR = 4000.0

# ---- the river (spec 4.3): wide and swirling near the camera, narrowing as it winds away. Two water features: the
# near reach (wide, tight swings) and the far river; they share the point where they meet, at one level.
# phase 2d (the owner: "make the valley and the river wider"): the near reach is much wider and swings harder (an
# S through the lower frame, as the reference's), the far river wider too
RIVER_NEAR_PATH = [(-10.0, 0.0, 240.0), (-26.0, 0.0, 150.0), (22.0, -0.2, 75.0), (38.0, -0.4, 10.0),
                   (-8.0, -0.7, -55.0), (-28.0, -1.0, -105.0)]
RIVER_PATH = [(-28.0, -1.0, -105.0), (-8.0, -1.5, -165.0), (20.0, -2.2, -225.0), (16.0, -3.0, -295.0),
              (-12.0, -4.0, -365.0), (-16.0, -5.0, -445.0), (6.0, -6.5, -555.0), (2.0, -8.0, -710.0),
              (0.0, -10.0, -900.0)]
RIVERS = [{"name": "riverNear", "path": RIVER_NEAR_PATH, "width": 72.0, "amplitude": 2.6, "falloff": 0.6,
           "smoothing": 3},
          {"name": "river", "path": RIVER_PATH, "width": 34.0, "amplitude": 2.4, "falloff": 0.7, "smoothing": 3}]
RIVER = RIVERS[1]
FLOOR = 1.0   # the canyon floor's base height, above the water
_ALL_RIVER = RIVER_NEAR_PATH + RIVER_PATH[1:]
_CANYON = [p for p in _ALL_RIVER if p[2] <= 110.0]   # the walls start just ahead of the camera


# ---- the canyon (phase 2b): steep rock walls close to the river, moss on the benches and the tops ------------------
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


WALLS = [  # (name, path, amplitude, width, falloff). Phase 2c (the owner): the banks roll down smoothly and gradually
    # to the water through mossy benches; the walls rise behind them, steep (rock) only higher up
    # phase 2d: every bank feature pushed out (a wider valley floor)
    ("benchL", _offset(_ALL_RIVER, 85.0), 4.0, 40.0, 1.0),
    ("benchR", _offset(_ALL_RIVER, -85.0), 4.0, 40.0, 1.0),
    ("bankL", _offset(_CANYON, 145.0), 26.0, 75.0, 1.0),     # the rolling green bank between bench and wall
    ("bankR", _offset(_CANYON, -145.0), 28.0, 75.0, 1.0),
    ("cliffL", _offset(_CANYON, 210.0), 62.0, 65.0, 0.6),
    ("cliffR", _offset(_CANYON, -210.0), 66.0, 65.0, 0.6),
    ("highL", _offset(_CANYON, 285.0), 85.0, 95.0, 0.7),
    ("highR", _offset(_CANYON, -285.0), 90.0, 95.0, 0.7),
    ("outerL", _offset(_CANYON, 420.0), 105.0, 160.0, 0.8),
    ("outerR", _offset(_CANYON, -420.0), 110.0, 160.0, 0.8),
]
MOUNTAINS = []   # the far ridges are meshes now (blender/aurora_assets.py: ridges), layered in the fog
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


_RIVERS_S = [(_chaikin(r["path"], r["smoothing"]), r) for r in RIVERS]
_RIVER_S = _RIVERS_S[1][0]


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
    cut = h   # each water cuts with its own weight; the terrain takes the deepest (ADR-830)
    for path, r in _RIVERS_S:
        d, level = _closest(path, x, z)
        w = _w(d, r["width"], r["falloff"])
        if w > 0:
            cut = min(cut, h + (min(h, level - r["amplitude"]) - h) * w)
    return cut


def world_json():
    feats = [{"name": r["name"], "kind": "river", "path": [list(p) for p in r["path"]], "width": r["width"],
              "amplitude": r["amplitude"], "falloff": r["falloff"], "roughness": 0.0, "smoothing": r["smoothing"],
              "water": True, "waterDepth": 0.0} for r in RIVERS]
    for name, path, amp, width, fo in WALLS + MOUNTAINS:
        feats.append({"name": name, "kind": "ridge", "path": [list(p) for p in path], "width": width,
                      "amplitude": amp, "falloff": fo, "roughness": 0.0, "smoothing": 2})
    return {"name": "living-prism-aurora", "seed": 20261023, "size": list(WORLD_SIZE), "baseHeight": FLOOR,
            "erosion": 0.35, "seaLevel": -1000.0,
            # rocky irregularity on the walls (a few metres; the formations sink enough to cover it)
            "layers": [{"frequency": 0.018, "amplitude": 7.0}, {"frequency": 0.06, "amplitude": 2.5}],
            "features": feats}


# NB: ridge paths are smoothed in the engine (smoothing 2); height() uses the raw polyline for them, which differs by
# centimetres at these widths. The river, whose level matters, is smoothed identically.

# ---- the crystal formations: about seven, about twenty-two tips (spec 4.4) -----------------------------------------
# (name, x, z, yaw degrees, scale, family). Families are built in Blender: cluster (tall leaning shards), pair (wide
# tilted prisms), spire (one or two tall), small (distant tips). Positions sit on the benches beside the river.
# phase 2b: placed by depth and setback from the river's centreline, so each stands on its bench. The near
# formations dominate. The right bank follows the reference: its crystals lean outward (away from the river, to the
# right) and are set back and staggered in depth, so the river corridor and the far crystals stay visible past them.
# (name, z, side -1 left / +1 right, setback m, yaw deg, scale, family)
_FORMATION_SPEC = [   # phase 2d (the owner: "the foreground crystals smaller"): the near clumps are smaller and
    # stand toward the frame edges, so the river owns the lower centre; setbacks follow the wider river
    ("L1", -15.0, -1, 105.0, 10.0, 1.7, "fan"),     # the hero fan, left of the river, rising into the aurora
    ("R1", -60.0, 1, 100.0, -10.0, 1.65, "pairR"),  # the right pair: upright, leaning outward, one behind
    ("L2", -140.0, -1, 70.0, 30.0, 1.5, "spire"),
    ("R2", -195.0, 1, 66.0, -10.0, 1.45, "spire2"),
    ("L3", -270.0, -1, 58.0, 20.0, 1.4, "small"),    # mid-ground spires, receding down both banks
    ("R3", -340.0, 1, 56.0, -25.0, 1.35, "spire2"),
    ("C1", -420.0, -1, 54.0, 15.0, 1.3, "spire"),
    ("C2", -505.0, 1, 54.0, -20.0, 1.3, "small2"),
    ("D", -690.0, -1, 60.0, 0.0, 1.6, "far"),       # the horizon group
]
_CENTRE = _chaikin(_ALL_RIVER, 3)


def river_x(z):
    """The river centreline's x at depth z (linear between the smoothed path's points)."""
    for a, b in zip(_CENTRE, _CENTRE[1:]):
        if (a[2] - z) * (b[2] - z) <= 0 and a[2] != b[2]:
            t = (z - a[2]) / (b[2] - a[2])
            return a[0] + t * (b[0] - a[0])
    return min(_CENTRE, key=lambda p: abs(p[2] - z))[0]


FORMATIONS = [(n, round(river_x(z) + side * setback, 2), z, yaw, sc, fam)
              for n, z, side, setback, yaw, sc, fam in _FORMATION_SPEC]


# ---- the river clearance check (phase 2d, the owner: "ensure the crystals are not rendered in the river") -------
RIVER_MARGIN = 4.0   # metres between any crystal vertex's ground footprint and the water's edge


def _wet_edge(px, pz, nx, nz, level, width):
    """How far the water reaches from the centreline point (px, pz) along the unit normal (nx, nz): the last
    distance at which the ground (height()) is at or below the water level, bisected in 0..width."""
    lo, hi = 0.0, width
    if height(px + nx * hi, pz + nz * hi) <= level:
        return hi
    for _ in range(24):
        mid = 0.5 * (lo + hi)
        if height(px + nx * mid, pz + nz * mid) <= level:
            lo = mid
        else:
            hi = mid
    return lo


def _closest_point(path, x, z):
    best = None
    for a, b in zip(path, path[1:]):
        dx, dz = b[0] - a[0], b[2] - a[2]
        L2 = dx * dx + dz * dz
        t = 0.0 if L2 <= 0 else min(max(((x - a[0]) * dx + (z - a[2]) * dz) / L2, 0.0), 1.0)
        px, pz = a[0] + t * dx, a[2] + t * dz
        d = math.hypot(x - px, z - pz)
        if best is None or d < best[0]:
            best = (d, px, pz, a[1] + t * (b[1] - a[1]))
    return best


def river_clearance(x, z):
    """Signed metres from (x, z) to the nearest water edge (negative: in the water)."""
    out = 1e18
    for path, r in _RIVERS_S:
        d, px, pz, level = _closest_point(path, x, z)
        if d > r["width"] + 50.0:
            continue
        nx, nz = ((x - px) / d, (z - pz) / d) if d > 1e-6 else (1.0, 0.0)
        out = min(out, d - _wet_edge(px, pz, nx, nz, level, r["width"]))
    return out


def check_crystals(footprints, margin=RIVER_MARGIN):
    """Every formation's every vertex (its ground projection, so a leaning tip over the water counts too) must sit at
    least `margin` from the water. footprints: {family: [[x, z], ...]} in the family's own frame (AV Gen metres).
    Returns [(formation, worst clearance m)] and the list of failures."""
    rows, bad = [], []
    for name, x, z, yaw, s, fam in FORMATIONS:
        c, sn = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        worst = min(river_clearance(x + s * (c * fx + sn * fz), z + s * (-sn * fx + c * fz))
                    for fx, fz in footprints[fam])
        rows.append((name, worst))
        if worst < margin:
            bad.append((name, worst))
    return rows, bad


def quat_y(deg):
    h = math.radians(deg) / 2
    return (0.0, math.sin(h), 0.0, math.cos(h))

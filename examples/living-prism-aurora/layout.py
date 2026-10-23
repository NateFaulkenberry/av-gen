"""The Living Prism (aurora revision): the valley's layout, AV Gen coordinates (metres, +Y up, the camera looks -Z).

One source of truth for the Blender assets, the scene and the project. The terrain's relief comes from world
features only (no noise octaves), so `height()` below is the engine's own height for this world
(src/world/world_map.cpp: featureWeight, the ridge/valley raise, the river cut down to its level, Chaikin smoothing),
and a crystal placed "on the ground" here stands on the ground the engine draws.
"""
import json
import math
import os
import subprocess
import tempfile

# ---- the camera: static, high, looking down the canyon (spec 4.1; phase 2b: higher and a little more down-looking,
# so the wide near river fills the lower frame as the reference's does)
CAMERA_POS = (0.0, 80.0, 170.0)
CAMERA_TARGET = (0.0, 22.0, -250.0)
CAMERA_FOV = 52.0
FAR = 6000.0

# ---- the river (spec 4.3): wide and swirling near the camera, narrowing as it winds away. Two water features: the
# near reach (wide, tight swings) and the far river; they share the point where they meet, at one level.
# phase 2d (the owner: "make the valley and the river wider"): the near reach is much wider and swings harder (an
# S through the lower frame, as the reference's), the far river wider too
# phase 2f (the owner: "river is still not wide enough and looks like it just comes to an abrupt end"): wider again,
# and three reaches now -- near, mid, and a distant reach that winds on to z = -1760, narrowing by perspective into
# the haze under the horizon glow, to the foot of the near ridge (now at 1800 m) so it never ends at a wall
# phase 2g (the owner: "brining water level of river up a tad"): every level +1.5 m, so the water spreads wider
RIVER_NEAR_PATH = [(-10.0, 1.5, 240.0), (-30.0, 1.5, 150.0), (26.0, 1.3, 75.0), (44.0, 1.1, 10.0),
                   (-10.0, 0.8, -55.0), (-32.0, 0.5, -105.0)]
RIVER_PATH = [(-32.0, 0.5, -105.0), (-8.0, 0.0, -165.0), (24.0, -0.7, -230.0), (18.0, -1.5, -300.0),
              (-14.0, -2.5, -370.0), (-18.0, -3.5, -450.0), (8.0, -5.0, -560.0), (10.0, -6.5, -720.0),
              (-6.0, -8.5, -900.0)]
RIVER_FAR_PATH = [(-6.0, -8.5, -900.0), (-24.0, -10.5, -1080.0), (8.0, -12.5, -1280.0), (30.0, -14.5, -1500.0),
                  (24.0, -16.5, -1760.0)]
RIVERS = [{"name": "riverNear", "path": RIVER_NEAR_PATH, "width": 100.0, "amplitude": 2.6, "falloff": 0.6,
           "smoothing": 3},
          {"name": "river", "path": RIVER_PATH, "width": 58.0, "amplitude": 2.4, "falloff": 0.7, "smoothing": 3},
          {"name": "riverFar", "path": RIVER_FAR_PATH, "width": 44.0, "amplitude": 2.4, "falloff": 0.7,
           "smoothing": 3}]
RIVER = RIVERS[1]
FLOOR = 1.0   # the canyon floor's base height, above the water
_ALL_RIVER = RIVER_NEAR_PATH + RIVER_PATH[1:] + RIVER_FAR_PATH[1:]
_CANYON = [p for p in _ALL_RIVER if p[2] <= 110.0]   # the walls start just ahead of the camera
# phase 2e: the walls follow a damped canyon axis (a third of the river's swing), not every bend of the water --
# an offset copy of a winding path knots into round knobs at each bend, the "bulbous hills"
_AXIS = [(0.33 * p[0], p[1], p[2]) for p in _CANYON]


# ---- the canyon (phase 2b): steep rock walls close to the river, moss on the benches and the tops ------------------
def _offset(path, d, wobble=0.0, phase=0.0, freq=0.011):
    """The river path shifted sideways by d metres: d > 0 is +x (screen right, the camera looking -Z)."""
    out = []
    for i, p in enumerate(path):
        a = path[max(i - 1, 0)]
        b = path[min(i + 1, len(path) - 1)]
        tx, tz = b[0] - a[0], b[2] - a[2]
        n = math.hypot(tx, tz) or 1.0
        dd = d + wobble * (math.sin(p[2] * freq + phase) + 0.5 * math.sin(p[2] * freq * 2.3 + 1.7 * phase))
        out.append((p[0] - tz / n * dd, 0.0, p[2] + tx / n * dd))
    return out


WALLS = [  # (name, path, amplitude, width, falloff, roughness). Phase 2e (the owner: "reads too much as bulbous hills
    # ... too symmetrical"): the two sides differ. LEFT is the high, steep, rocky side: a narrow mossy toe, a rock
    # ledge, then cliffs stepping up to a high wall. RIGHT is the lower, greener side: a broad bench, a gentle bank, a
    # lower wall far back. A feature's roughness lets the rocky noise layers (world_json) through: 0 is smooth.
    ("toeL", _offset(_ALL_RIVER, -100.0), 5.0, 30.0, 1.0, 0.35),
    ("ledgeL", _offset(_AXIS, -115.0, 18.0, 0.4), 22.0, 30.0, 0.45, 1.0),      # a flat-topped rock step (falloff < 1)
    ("cliffL", _offset(_AXIS, -175.0, 26.0, 2.1), 70.0, 45.0, 0.4, 1.0),
    ("highL", _offset(_AXIS, -250.0, 34.0, 4.0), 120.0, 75.0, 0.55, 1.0),
    ("outerL", _offset(_AXIS, -400.0, 50.0, 1.1), 140.0, 160.0, 0.8, 1.0),
    ("benchR", _offset(_ALL_RIVER, 130.0), 7.0, 60.0, 0.5, 0.3),  # the broad green bench
    ("bankR", _offset(_AXIS, 185.0, 24.0, 3.3), 24.0, 80.0, 1.3, 0.7),       # a gentle green rise
    ("wallR", _offset(_AXIS, 270.0, 30.0, 5.2), 62.0, 70.0, 0.6, 1.0),
    ("highR", _offset(_AXIS, 380.0, 45.0, 0.7), 85.0, 130.0, 0.8, 1.0),
]


def _axis_x(z):
    for a, b in zip(_AXIS, _AXIS[1:]):
        if (a[2] - z) * (b[2] - z) <= 0 and a[2] != b[2]:
            return a[0] + (z - a[2]) / (b[2] - a[2]) * (b[0] - a[0])
    return min(_AXIS, key=lambda p: abs(p[2] - z))[0]


# phase 2e: buttress spurs running down from the high walls toward the water, gullies between them -- what breaks a
# canyon wall's crest into rock instead of one smooth hump. (z at the wall, side, from m, to m, skew z, amp, width)
_SPURS = [(60.0, -1, 270.0, 120.0, 25.0, 55.0, 22.0), (-40.0, -1, 280.0, 135.0, -15.0, 70.0, 26.0),
          (-150.0, -1, 270.0, 120.0, 30.0, 60.0, 24.0), (-260.0, -1, 290.0, 130.0, -20.0, 75.0, 28.0),
          (-380.0, -1, 260.0, 120.0, 20.0, 55.0, 24.0), (-500.0, -1, 280.0, 140.0, 0.0, 60.0, 26.0),
          (-90.0, 1, 330.0, 200.0, -30.0, 40.0, 26.0), (-230.0, 1, 340.0, 190.0, 25.0, 45.0, 28.0),
          (-420.0, 1, 320.0, 180.0, -20.0, 40.0, 26.0)]
WALLS += [("spur%d" % i, [(_axis_x(z) + side * d0, 0.0, z), (_axis_x(z + sk) + side * d1, 0.0, z + sk)], amp * 0.4, w, 0.7,
           1.0) for i, (z, side, d0, d1, sk, amp, w) in enumerate(_SPURS)]
MOUNTAINS = []   # the far ridges are meshes now (blender/aurora_assets.py: ridges), layered in the fog
WORLD_SIZE = (4000.0, 8000.0)



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
    for _, path, amp, width, fo, _r in WALLS + MOUNTAINS:
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
    for name, path, amp, width, fo, rough in WALLS + MOUNTAINS:
        feats.append({"name": name, "kind": "ridge", "path": [list(p) for p in path], "width": width,
                      "amplitude": amp, "falloff": fo, "roughness": rough, "smoothing": 2})
    return {"name": "living-prism-aurora", "seed": 20261023, "size": list(WORLD_SIZE), "baseHeight": FLOOR,
            "erosion": 0.35, "seaLevel": -1000.0,
            # phase 2e: rocky relief. The engine scales these by each feature's roughness (the river's is 0, so
            # the water stays in its channel): warped, mostly ridged octaves give spurs, gullies and breaks
            # instead of domes; the multifractal erosion keeps the fine detail on the crests and off the hollows.
            "layers": LAYERS, "features": feats}


LAYERS = [{"frequency": 0.004, "amplitude": 30.0, "ridged": 0.3, "warp": 80.0},
          {"frequency": 0.011, "amplitude": 26.0, "ridged": 0.85, "warp": 25.0},
          {"frequency": 0.028, "amplitude": 20.0, "ridged": 0.9, "warp": 8.0},   # spurs and gullies
          {"frequency": 0.07, "amplitude": 8.0, "ridged": 0.8, "warp": 3.0},    # rocky breaks
          {"frequency": 0.2, "amplitude": 1.2, "ridged": 0.0, "warp": 0.0}]


# NB: ridge paths are smoothed in the engine (smoothing 2); height() uses the raw polyline for them, which differs by
# centimetres at these widths. The river, whose level matters, is smoothed identically.

# ---- the crystal formations: about seven, about twenty-two tips (spec 4.4) -----------------------------------------
# (name, x, z, yaw degrees, scale, family). Families are built in Blender: cluster (tall leaning shards), pair (wide
# tilted prisms), spire (one or two tall), small (distant tips). Positions sit on the benches beside the river.
# phase 2b: placed by depth and setback from the river's centreline, so each stands on its bench. The near
# formations dominate. The right bank follows the reference: its crystals lean outward (away from the river, to the
# right) and are set back and staggered in depth, so the river corridor and the far crystals stay visible past them.
# (name, z, side -1 left / +1 right, setback m, yaw deg, scale, family)
_FORMATION_SPEC = [   # (name, z, side, setback m, yaw deg, scale, family, tilt out deg, tilt fwd deg)
    # phase 2f (the owner: "mid ground crystals on left side of river blocking background crystals, crystals on right
    # side of river can be made larger - crystals should fill up more of the scene ... right bank more crystals too"):
    # twelve formations, larger, alternating near the water and out on the slopes so each depth layer's silhouette
    # stands clear of the one in front (layering() measures it). Each leans away from the river at its own angle.
    # phase 2g (the owner: "left hand side foreground crystals need to be out of the canyon wall a bit more,
    # crystals need to dominate that side of the river more - vary the foreground crystals a bit more on the right"):
    # the left fan stands out on the bank near the water, free of the wall; a big full mass (massL) stands on the
    # wall behind it; the right bank mixes a pair, a needle, broken stubs, a leaning pair and a fan.
    ("L1", -15.0, -1, 120.0, 10.0, 2.4, "fan", 4.0, 3.0),
    ("L1b", -150.0, -1, 180.0, -20.0, 2.8, "massL", 6.0, -4.0),
    ("L2", -235.0, -1, 75.0, 40.0, 2.8, "spire", 14.0, 6.0),
    ("L3", -300.0, -1, 120.0, 115.0, 3.0, "cluster", 8.0, -10.0),
    ("L4", -440.0, -1, 180.0, -35.0, 2.7, "spire2", 20.0, 9.0),
    ("L5", -700.0, -1, 100.0, 160.0, 3.4, "far", 8.0, 5.0),
    ("L6", -1050.0, -1, 150.0, 60.0, 4.0, "spire", 12.0, -6.0),
    ("R1", -60.0, 1, 145.0, -10.0, 2.4, "pairR", 3.0, -2.0),
    ("R2", -185.0, 1, 90.0, -65.0, 2.6, "needle", 7.0, -8.0),
    ("R3", -270.0, 1, 160.0, 25.0, 3.0, "stub", 4.0, 4.0),
    ("R4", -390.0, 1, 75.0, 70.0, 2.6, "leanPair", 10.0, -12.0),
    ("R5", -560.0, 1, 75.0, -20.0, 2.8, "fanR", 12.0, 6.0),
    ("R6", -850.0, 1, 75.0, 100.0, 3.4, "small", 10.0, -4.0),
]
# A family whose shards lean to +x leans toward the river on the left bank: turn it round there (yaw + 180).
FAMILY_OUTWARD = {"needle": 1, "stub": 1, "leanPair": 1, "fanR": 1, "massL": -1, "fan": -1, "pairR": 1, "cluster": 1, "pair": 1, "spire": 1, "spire2": 1, "small": 1, "small2": 1,
                  "far": 1}
_CENTRE = _chaikin(_ALL_RIVER, 3)


def river_x(z):
    """The river centreline's x at depth z (linear between the smoothed path's points)."""
    for a, b in zip(_CENTRE, _CENTRE[1:]):
        if (a[2] - z) * (b[2] - z) <= 0 and a[2] != b[2]:
            t = (z - a[2]) / (b[2] - a[2])
            return a[0] + t * (b[0] - a[0])
    return min(_CENTRE, key=lambda p: abs(p[2] - z))[0]


FORMATIONS = [(n, round(river_x(z) + side * setback, 2), z,
               yaw + (180.0 if FAMILY_OUTWARD[fam] != side else 0.0), sc, fam, side, tout, tfwd)
              for n, z, side, setback, yaw, sc, fam, tout, tfwd in _FORMATION_SPEC]


# ---- rotations (AV Gen: +Y up) ------------------------------------------------------------------------------------
def _qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def _qaxis(axis, deg):
    h = math.radians(deg) / 2
    return tuple(c * math.sin(h) for c in axis) + (math.cos(h),)


def _qrot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    tx, ty, tz = 2 * (y * vz - z * vy), 2 * (z * vx - x * vz), 2 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))


def formation_quat(f):
    """Yaw about +Y, then tilt: outward (away from the river: about +Z, toward the formation's side) and forward
    (about +X), in world axes."""
    _n, _x, _z, yaw, _s, _fam, side, tout, tfwd = f
    q = _qaxis((0, 1, 0), yaw)
    q = _qmul(_qaxis((1, 0, 0), tfwd), q)
    return _qmul(_qaxis((0, 0, 1), -side * tout), q)


# ---- the engine's own ground height (tools/world_preview.cpp --heights) -------------------------------------------
# height() above is the features only; the noise layers (phase 2e) move the ground by metres, so placement and the
# river check ask the engine itself.
WORLD_TOOL = os.environ.get("AVGEN_WORLD_PREVIEW", os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "av-gen-prism", "build", "render", "tools",
    "avgen_world_preview"))


def engine_heights(points):
    """Ground heights at [(x, z), ...] from the engine's WorldMap for this world."""
    with tempfile.TemporaryDirectory() as d:
        w, pts, out = (os.path.join(d, n) for n in ("world.json", "points.txt", "heights.txt"))
        json.dump(world_json(), open(w, "w"))
        with open(pts, "w") as f:
            f.writelines("%.3f %.3f\n" % p for p in points)
        subprocess.run([WORLD_TOOL, "--heights", pts, out, w, os.path.join(d, "map.png"), "64"], check=True,
                       stdout=subprocess.DEVNULL)
        return [float(v) for v in open(out).read().split()]


def water_level(x, z):
    """The water surface of the nearest river at (x, z), and whether (x, z) is inside any river's reach."""
    best = None
    for path, r in _RIVERS_S:
        d, level = _closest(path, x, z)
        if d < r["width"] and (best is None or d < best[0]):
            best = (d, level)
    return None if best is None else best[1]


# ---- the river clearance check (phase 2d, the owner: "ensure the crystals are not rendered in the river") -------
RIVER_MARGIN = 4.0   # metres between any crystal vertex's ground footprint and the water
_RINGS = (0.0, 2.0, 4.0, 6.0, 9.0, 13.0, 18.0, 25.0)


def place(footprints):
    """Each formation's world transform and its vertices on the ground. footprints: {family: [[x, y, z], ...]} in the
    family's own frame (AV Gen metres). The base sits 1.5 m x scale below the lowest engine ground under its
    base ring, so it never floats on a slope. Returns [(formation, (x, y, z), quat, [(wx, wz) ...])]."""
    out = []
    for f in FORMATIONS:
        name, x, z, _yaw, s, fam = f[:6]
        q = formation_quat(f)
        verts = [_qrot(q, (s * v[0], s * v[1], s * v[2])) for v in footprints[fam]]
        base = [(x + v[0], z + v[2]) for v, src in zip(verts, footprints[fam]) if src[1] < 0.5]
        out.append((f, x, z, q, verts, base))
    hs = engine_heights([(x, z) for _f, x, z, *_r in out] + [p for o in out for p in o[5]])
    k = len(out)
    placed = []
    for f, x, z, q, verts, base in out:
        lo = min([hs[out.index((f, x, z, q, verts, base))]] + hs[k:k + len(base)])
        k += len(base)
        placed.append((f, (x, lo - 1.5 * f[4], z), q, [(x + v[0], z + v[2]) for v in verts]))
    return placed


def check_crystals(footprints, margin=RIVER_MARGIN):
    """Every formation's every vertex, projected to the ground (a tip leaning over the water counts), must stand at
    least `margin` from wet ground: ground the engine puts below the river's surface. Clearance is measured on rings
    of 8 directions around each vertex. Returns the placement, [(formation, clearance m)] and the failures."""
    placed = place(footprints)
    probes = []
    for _f, _p, _q, ground in placed:
        for gx, gz in ground:
            for r in _RINGS:
                for i in range(8 if r > 0 else 1):
                    a = math.tau * i / 8
                    probes.append((gx + r * math.cos(a), gz + r * math.sin(a), r))
    hs = engine_heights([(px, pz) for px, pz, _r in probes])
    rows, bad, k = [], [], 0
    for f, _p, _q, ground in placed:
        clear = _RINGS[-1]
        for _ in ground:
            for r in _RINGS:
                for i in range(8 if r > 0 else 1):
                    px, pz, _ = probes[k]
                    lvl = water_level(px, pz)
                    if lvl is not None and hs[k] <= lvl:
                        clear = min(clear, r)
                    k += 1
        rows.append((f[0], clear))
        if clear < margin:
            bad.append((f[0], clear))
    return placed, rows, bad


# ---- the layering check (phase 2f): what each formation shows of itself past the ones in front of it ------------
def _project(p):
    cx, cy, cz = CAMERA_POS
    f = [CAMERA_TARGET[i] - CAMERA_POS[i] for i in range(3)]
    n = math.sqrt(sum(c * c for c in f))
    f = [c / n for c in f]
    r = [-f[2], 0.0, f[0]]
    n = math.hypot(r[0], r[2])
    r = [r[0] / n, 0.0, r[2] / n]
    u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]]
    d = [p[0] - cx, p[1] - cy, p[2] - cz]
    z = sum(a * b for a, b in zip(d, f))
    t = math.tan(math.radians(CAMERA_FOV) / 2)
    x = sum(a * b for a, b in zip(d, r)) / (z * t * 16 / 9)
    y = sum(a * b for a, b in zip(d, u)) / (z * t)
    return (0.5 + 0.5 * x) * 1920, (0.5 - 0.5 * y) * 1080, z


def _hull(pts):
    pts = sorted(set(pts))
    if len(pts) < 3:
        return pts
    def half(seq):
        h = []
        for p in seq:
            while len(h) >= 2 and ((h[-1][0] - h[-2][0]) * (p[1] - h[-2][1]) - (h[-1][1] - h[-2][1]) * (p[0] - h[-2][0])) <= 0:
                h.pop()
            h.append(p)
        return h
    lo, hi = half(pts), half(reversed(pts))
    return lo[:-1] + hi[:-1]


def _inside(poly, x, y):
    c = False
    for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def layering(placed, footprints, cell=8):
    """Per formation: its on-screen area (the convex hull of its projected vertices, % of the frame) and the share of
    that area no nearer formation covers. Hulls overstate a cluster's own area, so the visible share is a floor."""
    shapes = []
    for f, pos, q, _g in placed:
        s = f[4]
        pts = [_project([pos[i] + v[i] for i in range(3)])
               for v in (_qrot(q, (s * a[0], s * a[1], s * a[2])) for a in footprints[f[5]])]
        shapes.append((f[0], sum(p[2] for p in pts) / len(pts), _hull([(round(p[0]), round(p[1])) for p in pts])))
    owner, area = {}, {n: 0 for n, _d, _h in shapes}
    for gx in range(0, 1920, cell):
        for gy in range(0, 1080, cell):
            hits = [(d, n) for n, d, h in shapes if len(h) > 2 and _inside(h, gx, gy)]
            for _d, n in hits:
                area[n] += 1
            if hits:
                owner[min(hits)[1]] = owner.get(min(hits)[1], 0) + 1
    total = (1920 // cell) * (1080 // cell)
    return [(n, 100.0 * area[n] / total, 100.0 * owner.get(n, 0) / max(area[n], 1)) for n, _d, _h in shapes], \
        100.0 * sum(owner.values()) / total


def quat_y(deg):
    h = math.radians(deg) / 2
    return (0.0, math.sin(h), 0.0, math.cos(h))

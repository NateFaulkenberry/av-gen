"""The Bioluminescent Valley's geography: one world that holds all four of the reference's shot types.

    north (+z), far
         the valley recedes between rows of karst towers into violet haze; far ranges behind
     |                                      |
     |   west wall          river           |   east wall (towers, cliffs)
     |   (towers)       ~~~~~~~~~~~         |
     |      RIVERBANK: a tributary            |
     |      from the west cliff  ~~           |
     |                       ~~~~   MEADOW: a terrace east of the river, a small stream
     |                        ~~~~  VISTA: a 300 m promontory on the
     |                                     |   east side of the floor, looking north
    south (-z), behind the vista camera

Coordinates are metres, +y up. The valley floor is at about y = 0; the walls and the plateau behind them stand
at about 150-260 m. Towers (towers.py) stand on the walls, the plateau and, rarely, the floor.

Heights for placing anything are asked of the engine (`avgen_world_preview`), never re-derived: a lattice of
probes is cached (build/biovalley/) and interpolated bilinearly (the Rift's `land.Ground`).
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np

import importlib.util  # noqa: E402

_spec = importlib.util.spec_from_file_location(
    "rift_land", Path(__file__).resolve().parents[1] / "bioluminescent" / "land.py")
rift_land = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rift_land)

REPO = Path(__file__).resolve().parents[2]
CACHE = REPO / "build" / "biovalley"
rift_land.CACHE = CACHE

SIZE = 9000.0
Z_RANGE = (-2200.0, 4400.0)
FLOOR_HALF = 430.0       # the flat floor's half width


def cx(z):
    """The valley's centre line."""
    return 90.0 * math.sin(z / 700.0 + 0.3) + 30.0 * math.sin(z / 260.0 + 1.1)


def rx(z):
    """The main river: it meanders inside the floor."""
    return cx(z) + 85.0 * math.sin(z / 330.0 + 0.8) + 20.0 * math.sin(z / 120.0)


def river_level(z):
    return 1.0 + z * 0.0012


def floor_level(z):
    return river_level(z) + 2.2


def _path(fn, level_fn, step=40.0, z0=Z_RANGE[0], z1=Z_RANGE[1]):
    zs = np.arange(z0, z1 + 1, step)
    return [[round(fn(z), 2), round(level_fn(z), 2), round(float(z), 2)] for z in zs]


# ---- the four shot sites ---------------------------------------------------------------------------------------------

VISTA_Z = -1150.0
VISTA_X = cx(VISTA_Z) + 250.0
VISTA_HEIGHT = 560.0      # the promontory's rise above the floor

MEADOW_Z = 140.0
MEADOW_X = cx(MEADOW_Z) - 170.0

BANK_Z = 860.0
BANK_X0 = cx(BANK_Z) - 265.0   # the stream head: cut into the foot of the west wall, under the falls
BANK_LEVEL = 1.9               # the calm reach's bed (its surface is 1 m higher; the floor is ~4-6 m here)


def probe(w, xz):
    """Engine heights at arbitrary (x, z) points for world `w` (cached)."""
    import hashlib, json, re, subprocess
    xz = [(round(float(x), 2), round(float(z), 2)) for x, z in xz]
    key = hashlib.sha256((json.dumps(w, sort_keys=True) + json.dumps(xz)).encode()).hexdigest()[:16]
    CACHE.mkdir(parents=True, exist_ok=True)
    f = CACHE / f"probe-{key}.json"
    if f.exists():
        return np.array(json.loads(f.read_text()))
    wf = CACHE / f"probe-world-{key}.json"
    wf.write_text(json.dumps({"world": w}))
    hs = []
    for i in range(0, len(xz), 4000):
        chunk = xz[i:i + 4000]
        args = [a for (x, z) in chunk for a in (f"{x:.2f}", f"{z:.2f}")]
        out = subprocess.run([str(rift_land.TOOL), str(wf), str(CACHE / "probe.png"), "16"] + args,
                             capture_output=True, text=True, check=True).stdout
        found = re.findall(r"probe \([^)]*\): height (-?[\d.]+)", out)
        assert len(found) == len(chunk)
        hs += [float(h) for h in found]
    f.write_text(json.dumps(hs))
    return np.array(hs)


def _carve(base, pts, depth, end_level=None, cap=None):
    """A stream's levels from the ground it crosses: densify the path, read the base world's heights, and make
    the level fall monotonically (a running minimum) `depth` below the ground, so it never climbs or floats."""
    P = np.array(pts, float)
    seg = [P[0]]
    for a, b in zip(P[:-1], P[1:]):
        n = max(2, int(np.hypot(*(b - a)) / 6.0))
        for t in np.linspace(0, 1, n + 1)[1:]:
            seg.append(a + (b - a) * t)
    seg = np.array(seg)
    h = probe(base, seg)
    lv = np.minimum.accumulate(h - depth)
    if cap is not None:          # a level the water never rises above: a calm reach cut into rising ground
        lv = np.minimum(lv, cap)
    if end_level is not None:
        lv = np.minimum(lv, np.maximum(end_level, lv))
    keep = list(range(0, len(seg), 3)) + [len(seg) - 1]
    return [[round(float(seg[k, 0]), 2), round(float(lv[k]), 2), round(float(seg[k, 1]), 2)] for k in sorted(set(keep))]


def meadow_stream_pts():
    """A small stream: it rises at the meadow's north edge, runs toward the camera, and turns east to the river."""
    x, z = MEADOW_X, MEADOW_Z
    return [(x + 25, z + 260), (x + 8, z + 170), (x - 4, z + 95), (x + 6, z + 40), (x - 2, z - 10),
            (x + 40, z - 50), (x + 120, z - 70), (rx(z - 80) - 10, z - 80)]


def bank_stream_pts():
    """The riverbank's tributary: from the foot of the west cliff, east across the floor to the main river."""
    z = BANK_Z
    return [(BANK_X0 + 45, z + 8), (BANK_X0 + 70, z + 2), (BANK_X0 + 105, z - 6), (BANK_X0 + 150, z + 4),
            (BANK_X0 + 210, z - 20), (BANK_X0 + 260, z - 50), (rx(z - 80), z - 80)]


def world(stage="final"):
    feats = [
        # the valley: a broad trough between walls
        {"name": "valley", "kind": "valley", "path": _path(cx, lambda z: 0.0, 60.0), "width": 1150.0,
         "amplitude": 185.0, "falloff": 1.7, "roughness": 1.0, "smoothing": 3},
        # its floor: level, gently descending south, but not glassy (hummocks stay)
        {"name": "floor", "kind": "flat", "path": _path(cx, floor_level, 60.0), "width": FLOOR_HALF + 60.0,
         "falloff": 2.2, "flatten": 0.95, "roughness": 0.32, "smoothing": 3},
        # the main river
        {"name": "river", "kind": "river", "path": _path(rx, river_level, 30.0), "width": 34.0, "amplitude": 4.0,
         "falloff": 1.1, "flatten": 0.9, "roughness": 0.12, "water": True, "waterDepth": 1.8, "smoothing": 3},
        # a back channel: leaves the river, loops through the floor, rejoins (the reference's braided floor)
        {"name": "backwater", "kind": "river",
         "path": [[round(rx(z) + off, 2), round(river_level(z) + 0.15, 2), z] for z, off in
                  ((1500, 0), (1380, -60), (1250, -120), (1120, -140), (1000, -95), (900, -30), (840, 0))],
         "width": 16.0, "amplitude": 3.0, "falloff": 1.0, "flatten": 0.9, "roughness": 0.1, "water": True,
         "waterDepth": 1.2, "smoothing": 3},
        # far north: a lake where the river leaves the towers
        {"name": "farlake", "kind": "river",
         "path": [[round(rx(z) + off, 2), round(river_level(z), 2), z] for z, off in
                  ((2350, -30), (2450, 30), (2560, 60), (2650, 10))],
         "width": 90.0, "amplitude": 4.0, "falloff": 1.2, "flatten": 0.92, "roughness": 0.1, "water": True,
         "waterDepth": 1.8, "smoothing": 3},
        # the vista promontory: a rock knob standing out of the west side of the floor, its north face a drop
        {"name": "promontory", "kind": "ridge",
         "path": [[round(VISTA_X + 160, 1), 0.0, VISTA_Z - 120], [round(VISTA_X, 1), 0.0, VISTA_Z]],
         "width": 230.0, "amplitude": VISTA_HEIGHT, "falloff": 0.75, "roughness": 1.0, "smoothing": 2},
        # the meadow: a broad, slightly raised terrace east of the river, hummocky
        {"name": "meadow", "kind": "flat", "path": [[round(MEADOW_X, 1), floor_level(MEADOW_Z) + 1.4, MEADOW_Z + 60]],
         "width": 260.0, "falloff": 1.3, "flatten": 0.8, "roughness": 0.55},
    ]
    if stage == "final":
        base = world("base")
        feats += [
        {"name": "meadowStream", "kind": "river", "path": _carve(base, meadow_stream_pts(), 0.8), "width": 5.0, "amplitude": 1.2,
         "falloff": 1.0, "flatten": 0.85, "roughness": 0.25, "water": True, "waterDepth": 0.4, "smoothing": 3},
        # the riverbank's tributary, from the west cliff
        {"name": "bankStream", "kind": "river", "path": _carve(base, bank_stream_pts(), 1.2, cap=BANK_LEVEL), "width": 9.0, "amplitude": 2.2,
         "falloff": 1.0, "flatten": 0.88, "roughness": 0.3, "water": True, "waterDepth": 1.0, "smoothing": 3},
        # a ravine down the east wall (a waterfall's course) and knolls on the floor (forested islands)
        {"name": "eastRavine", "kind": "valley",
         "path": [[round(cx(1900) + 700, 1), 0.0, 2050], [round(cx(1800) + 420, 1), 0.0, 1850],
                  [round(cx(1750) + 260, 1), 0.0, 1760]],
         "width": 70.0, "amplitude": 40.0, "falloff": 1.2, "roughness": 1.0, "smoothing": 3},
    ]
    for k, (z, off, amp, w) in enumerate(((420, -170, 14, 60), (700, 160, 10, 45), (1150, -40, 9, 50),
                                          (1620, 150, 18, 70), (1900, -150, 12, 60), (330, -60, 6, 35),
                                          (980, 210, 11, 55), (2150, 60, 10, 55))):
        feats.append({"name": f"knoll{k}", "kind": "ridge", "path": [[round(cx(z) + off, 1), 0.0, z]],
                      "width": w, "amplitude": amp, "falloff": 0.8, "roughness": 1.0})
    return {
        "name": "bioluminescent-valley",
        "seed": 20261008,
        "size": [SIZE, SIZE],
        "baseHeight": 175.0,
        "erosion": 0.6,
        "seaLevel": -1000.0,
        "layers": [
            {"frequency": 0.0009, "amplitude": 140.0, "ridged": 0.35, "warp": 380.0},   # plateau swell
            {"frequency": 0.0035, "amplitude": 55.0, "ridged": 0.7, "warp": 140.0},     # buttresses, gullies
            {"frequency": 0.013, "amplitude": 14.0, "ridged": 0.75, "warp": 35.0},      # rock ribs
            {"frequency": 0.05, "amplitude": 2.6, "ridged": 0.5, "warp": 8.0},          # ledges, hummocks
            {"frequency": 0.21, "amplitude": 0.35, "ridged": 0.0, "warp": 0.0},         # rubble
        ],
        "features": feats,
        "biomes": [],
    }


def ground(region, step):
    """Engine heights over `region` = (x0, x1, z0, z1) at `step` metres."""
    return rift_land.Ground(world(), *region, step=step)

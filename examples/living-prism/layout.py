"""The Living Prism: the grotto's layout, in AV Gen coordinates (metres, +Y up, the camera's home looks toward -Z).

One source of truth for three consumers:
  - blender/prism_assets.py builds the meshes and, for previews, places them where the engine will;
  - build_scene.py writes the scene and the live project from it;
  - the camera route (CAMERA_*) is baked from it into looping timelines.

Nothing here touches Blender or the engine. The terrain height is evaluated with the engine's own formulas for the
features used (src/world/world_map.cpp: featureWeight, the ridge raise and the water cut), so a formation placed
"on the ground" here stands on the ground the engine draws.
"""
import math

# ---------------------------------------------------------------------------------------------------------------
# The chamber: an enclosed grotto about 29 x 27 m, ceiling about 12.5 m, with a back passage that recedes into haze.
# Blender builds the shell as the surface of this "air" volume, seen from inside.
CHAMBER = {"center": (0.0, 2.0, -2.5), "radii": (14.5, 8.5, 13.5)}
ALCOVES = [  # (centre, radius): side pockets that break the ellipsoid's regularity
    ((-11.0, 2.0, -9.0), 4.2),
    ((11.5, 3.0, 1.5), 4.0),
    ((-10.5, 4.5, 5.5), 3.6),
]
# the back passage: a bent tube behind the arch, closed at its end, lit from inside (the depth cue)
PASSAGE = [((1.0, 2.0, -12.5), 4.0), ((-1.5, 2.6, -19.0), 3.4), ((-6.5, 3.2, -24.0), 2.8), ((-11.0, 3.4, -26.0), 2.4)]

# ---------------------------------------------------------------------------------------------------------------
# The floor and the pool: a terrain node. Flat base, mounds for relief, one river feature (two points: an elongated
# pool) whose water surface is y = 0.
FLOOR_BASE = 0.5
POOL = {"name": "pool", "kind": "river", "path": [(-3.8, 0.0, -1.2), (3.6, 0.0, -2.6)],
        "width": 5.5, "amplitude": 1.4, "falloff": 0.6}
MOUNDS = [  # (name, (x, z), amplitude, width, falloff)
    ("shelf", (0.5, -8.6), 0.7, 4.0, 1.0),      # the primary formation's shelf behind the pool
    ("leftbank", (-9.0, -1.0), 0.6, 4.5, 1.0),
    ("rightbank", (9.0, -1.5), 0.5, 4.5, 1.0),
    ("backleft", (-8.5, -8.0), 1.4, 4.5, 1.0),
    ("backright", (8.5, -8.5), 1.2, 4.5, 1.0),
    ("frontleft", (-8.0, 6.5), 0.9, 4.0, 1.0),
    ("frontright", (8.0, 6.0), 0.8, 4.0, 1.0),
]
WORLD_SIZE = (60.0, 60.0)


def _smooth_weight(d, width, falloff):
    if width <= 0:
        return 0.0
    u = min(max(1.0 - d / width, 0.0), 1.0)
    s = u * u * (3.0 - 2.0 * u)
    return s if falloff == 1.0 else s ** max(falloff, 0.01)


def _seg_dist(p, a, b):
    ax, az, bx, bz = a[0], a[2], b[0], b[2]
    dx, dz = bx - ax, bz - az
    L2 = dx * dx + dz * dz
    t = 0.0 if L2 <= 0 else min(max(((p[0] - ax) * dx + (p[1] - az) * dz) / L2, 0.0), 1.0)
    return math.hypot(p[0] - (ax + t * dx), p[1] - (az + t * dz))


def pool_weight(x, z):
    return _smooth_weight(_seg_dist((x, z), POOL["path"][0], POOL["path"][1]), POOL["width"], POOL["falloff"])


def height(x, z):
    """The engine's terrain height for this world (base + ridge raises, then the pool's cut)."""
    h = FLOOR_BASE
    for _, (mx, mz), amp, width, falloff in MOUNDS:
        h += amp * _smooth_weight(math.hypot(x - mx, z - mz), width, falloff)
    w = pool_weight(x, z)
    if w > 0:
        target = 0.0 - POOL["amplitude"]
        h = min(h, h + (min(h, target) - h) * w)
    return h


def world_json():
    feats = [{"name": POOL["name"], "kind": "river", "path": [list(p) for p in POOL["path"]], "width": POOL["width"],
              "amplitude": POOL["amplitude"], "falloff": POOL["falloff"], "roughness": 0.0, "smoothing": 0,
              "water": True, "waterDepth": 0.0}]
    for name, (mx, mz), amp, width, falloff in MOUNDS:
        feats.append({"name": name, "kind": "ridge", "path": [[mx, 0.0, mz]], "width": width, "amplitude": amp,
                      "falloff": falloff, "roughness": 0.0, "smoothing": 0})
    return {"name": "living-prism", "seed": 20261022, "size": list(WORLD_SIZE), "baseHeight": FLOOR_BASE,
            "erosion": 0.0, "seaLevel": -1000.0, "layers": [{"frequency": 0.1, "amplitude": 0.0}],
            "features": feats}


# ---------------------------------------------------------------------------------------------------------------
# The formations. Each asset is built in Blender at its own origin, base at y = 0, its "front" facing +Z.
# yaw turns it about +Y (degrees, counter-clockwise seen from above); tilt is (about X, about Z) in degrees, applied
# before the yaw; sink lowers it into the ground so no base floats.
HEROES = [
    # the primary focal element: a tall fan of prisms on the shelf behind the pool, framed by the arch
    {"name": "spire", "role": "primary", "pos": (0.5, -8.4), "yaw": 8.0, "scale": 1.0, "sink": 0.35},
    # leaning tabular shards from the left bank, reaching over the water (a diagonal, not a spike)
    {"name": "fan", "role": "secondary", "pos": (-8.2, -1.6), "yaw": -10.0, "scale": 1.0, "sink": 0.45},
    # a split geode on the right bank, its lined cavity turned to the pool (a round silhouette, a hollow)
    {"name": "geode", "role": "secondary", "pos": (8.4, -0.6), "yaw": -72.0, "scale": 1.0, "sink": 0.55},
    # a cluster hanging from the ceiling over the near pool (points down; it doubles in the water)
    {"name": "chandelier", "role": "secondary", "pos": (3.6, -0.8), "yaw": 20.0, "scale": 0.85, "ceiling": True},
    # the warm accent: a stout, terraced block low in the right foreground (parallax for the camera)
    {"name": "ember", "role": "accent", "pos": (4.6, 5.6), "yaw": -35.0, "scale": 1.0, "sink": 0.3,
     "tilt": (0.0, -14.0)},
]
# the focal structure: a rock arch over the far shore, its underside crusted with small crystal
ARCH = {"name": "arch", "pos": (0.0, -5.0), "yaw": 6.0, "scale": 1.0, "sink": 0.9}

# supporting druse clusters on the banks and at wall bases: (variant, x, z, yaw, scale)
CLUSTERS = [
    ("druseA", -11.3, -5.2, 30.0, 1.1), ("druseB", -5.8, -9.6, -20.0, 0.9), ("druseC", 6.6, -9.8, 75.0, 1.0),
    ("druseA", 11.2, -6.0, 140.0, 0.85), ("druseB", -11.8, 2.8, 95.0, 1.0), ("druseC", 10.6, 4.4, -60.0, 0.9),
    ("druseA", -6.2, 6.4, 10.0, 0.8), ("druseB", 3.0, -10.6, 200.0, 0.75), ("druseC", -3.4, -9.4, 250.0, 0.7),
    ("druseA", 7.4, 2.9, 300.0, 0.6),
]
# loose single shards for glints and scale cues: (variant, x, z, yaw, scale, lean degrees)
SHARDS = [
    ("shardA", -6.6, 0.6, 20, 0.9, 18), ("shardB", -7.2, -3.8, 140, 1.1, 10), ("shardC", -5.6, 3.4, 260, 0.8, 25),
    ("shardA", 6.4, -3.2, 80, 0.9, 15), ("shardB", 7.0, 1.6, 200, 0.7, 30), ("shardC", 5.8, -5.4, 320, 1.0, 12),
    ("shardA", -2.0, -7.6, 45, 0.7, 20), ("shardB", 2.4, -7.9, 170, 0.8, 8), ("shardC", -1.2, 6.2, 90, 0.6, 35),
    ("shardA", 2.6, 6.8, 300, 0.7, 22), ("shardB", -9.8, -7.2, 15, 1.2, 6), ("shardC", 9.6, -7.6, 110, 1.1, 12),
    ("shardA", -4.2, -8.6, 230, 0.8, 18), ("shardB", 4.8, -8.0, 60, 0.9, 28),
]
# boulders: dark faceted rock that frames the pool and the crystals: (variant, x, z, yaw, scale, sink)
BOULDERS = [
    ("boulderA", -5.2, 2.6, 10, 1.3, 0.5), ("boulderB", -6.2, -4.6, 80, 1.6, 0.6), ("boulderC", 5.8, 3.4, 140, 1.4, 0.5),
    ("boulderD", 6.0, -5.0, 200, 1.8, 0.7), ("boulderA", -2.6, 5.4, 260, 0.9, 0.35), ("boulderB", 3.0, 7.4, 320, 0.8, 0.3),
    ("boulderC", -10.0, -2.0, 40, 2.4, 0.9), ("boulderD", 10.4, -2.6, 120, 2.2, 0.9), ("boulderA", -3.4, -6.9, 300, 1.1, 0.4),
    ("boulderB", 3.6, -6.8, 30, 1.0, 0.4), ("boulderC", -9.0, 8.2, 75, 2.0, 0.8), ("boulderD", 9.4, 8.0, 160, 2.2, 0.8),
    ("boulderA", 0.8, -11.2, 0, 1.8, 0.6), ("boulderB", -12.2, 6.0, 210, 2.6, 1.0),
]


def ceiling_y(x, z, inset=0.6):
    """The chamber ellipsoid's roof above (x, z), less an inset so a hung object embeds in the displaced rock."""
    cx, cy, cz = CHAMBER["center"]
    rx, ry, rz = CHAMBER["radii"]
    q = ((x - cx) / rx) ** 2 + ((z - cz) / rz) ** 2
    return cy + ry * math.sqrt(max(1.0 - q, 0.0)) + inset


def quat_from_euler_deg(rx=0.0, ry=0.0, rz=0.0):
    """Rotation about X, then Z, then Y (the yaw last, in the world), as (x, y, z, w)."""
    def q_axis(ax, deg):
        h = math.radians(deg) * 0.5
        s = math.sin(h)
        return (ax[0] * s, ax[1] * s, ax[2] * s, math.cos(h))

    def mul(a, b):
        ax, ay, az, aw = a
        bx, by, bz, bw = b
        return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)
    return mul(q_axis((0, 1, 0), ry), mul(q_axis((0, 0, 1), rz), q_axis((1, 0, 0), rx)))


def placement(x, z, yaw=0.0, scale=1.0, sink=0.0, tilt=(0.0, 0.0), y=None, flip=False):
    """One record for a `points` distribution: [x, y, z, qx, qy, qz, qw, sx, sy, sz]."""
    yy = (height(x, z) - sink * scale) if y is None else y
    q = quat_from_euler_deg(tilt[0] + (180.0 if flip else 0.0), yaw, tilt[1])
    return [round(x, 4), round(yy, 4), round(z, 4)] + [round(c, 6) for c in q] + [round(scale, 4)] * 3


def hero_placement(h):
    x, z = h["pos"]
    if h.get("ceiling"):
        # built upright with its knob at y = 0; flipped, the knob is at the top
        return placement(x, z, h["yaw"], h["scale"], y=ceiling_y(x, z), flip=True)
    return placement(x, z, h["yaw"], h["scale"], h.get("sink", 0.0), h.get("tilt", (0.0, 0.0)))


# ---------------------------------------------------------------------------------------------------------------
# The camera: a closed route through eight stations, each a position and a look-at target. Both are closed
# Catmull-Rom curves over the same parameter; the parameter is re-timed to constant speed along the position curve,
# so the camera moves at one even pace and the loop's seam is invisible (C1 at every station, the seam included).
CAMERA_FOV = 52.0          # vertical degrees: no wide-angle distortion
CAMERA_LOOP = 48 * 4 * 60.0 / 121.0   # 48 bars of the test track (95.21 s); live, it simply loops
CAMERA_STATIONS = [
    # (position, target, note)
    ((0.4, 1.5, 6.8), (0.3, 3.2, -7.5), "master: pool, arch and the spire through it"),
    ((-4.6, 2.6, 6.0), (3.0, 2.0, -3.0), "front left, rising: across the pool to the geode and the arch"),
    ((-5.6, 1.9, 3.6), (5.5, 1.6, -1.0), "left bank: the fan's blades pass in the foreground, the geode across"),
    ((-4.4, 1.2, -2.2), (1.6, 3.4, -8.0), "low by the arch's left leg: the spire three-quarter"),
    ((1.4, 1.1, 4.4), (3.4, 4.4, -2.0), "low at the near shore: up at the chandelier, the geode below it, both in the water"),
    ((5.8, 2.2, 4.2), (-4.2, 2.4, -5.0), "right bank: the fan and the chandelier across the water"),
    ((4.6, 4.0, 6.6), (-0.8, 0.3, -3.6), "high right: down onto the pool's reflections"),
    ((2.0, 2.2, 7.8), (0.0, 3.0, -6.2), "returning to the master"),
]


def _catmull_closed(pts, t):
    n = len(pts)
    s = t * n
    i = int(math.floor(s)) % n
    u = s - math.floor(s)
    p0, p1, p2, p3 = pts[(i - 1) % n], pts[i], pts[(i + 1) % n], pts[(i + 2) % n]
    u2, u3 = u * u, u * u * u
    return tuple(0.5 * ((2 * p1[k]) + (-p0[k] + p2[k]) * u + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * u2 +
                        (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * u3) for k in range(3))


def camera_samples(step=0.25):
    """[(time, position, target)] over one loop, at constant speed along the position curve."""
    P = [s[0] for s in CAMERA_STATIONS]
    T = [s[1] for s in CAMERA_STATIONS]
    N = 4000
    us = [i / N for i in range(N + 1)]
    pos = [_catmull_closed(P, u) for u in us]
    arc = [0.0]
    for a, b in zip(pos, pos[1:]):
        arc.append(arc[-1] + math.dist(a, b))
    total = arc[-1]
    out = []
    n = int(round(CAMERA_LOOP / step))
    j = 0
    for k in range(n + 1):
        t = k * CAMERA_LOOP / n
        d = total * k / n
        while j < N - 1 and arc[j + 1] < d:
            j += 1
        f = 0.0 if arc[j + 1] == arc[j] else (d - arc[j]) / (arc[j + 1] - arc[j])
        u = us[j] + f * (us[j + 1] - us[j])
        out.append((round(t, 4), _catmull_closed(P, u % 1.0), _catmull_closed(T, u % 1.0)))
    return out, total


def station_times():
    """The time at which the camera passes each station (for stills)."""
    P = [s[0] for s in CAMERA_STATIONS]
    N = 4000
    pos = [_catmull_closed(P, i / N) for i in range(N + 1)]
    arc = [0.0]
    for a, b in zip(pos, pos[1:]):
        arc.append(arc[-1] + math.dist(a, b))
    per = N // len(P)
    return [round(CAMERA_LOOP * arc[i * per] / arc[-1], 3) for i in range(len(P))]

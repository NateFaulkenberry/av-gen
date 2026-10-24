"""Aurora Borealis: the land. A closed-loop river with S-bends, faceted valley walls, a handful of art-directed hero
peaks on alternate banks, and three receding outer ranges, written as flat-faceted GLBs (tools/sonic_vfx/meshes.py).

Everything is a pure function of the constants below, so a rebuild writes the same bytes.

Coordinates: metres, +Y up. The river's centreline is the closed curve

    rc(theta) = R0 * (1 + A3 sin(3 theta + P3) + A5 sin(5 theta + P5)),   (x, z) = rc * (sin theta, cos theta)

which swings through alternating S-bends. The valley band is a grid whose rows follow the river (each row a fixed
distance from the water's edge, measured radially), so its facets are small at the water and grow with distance from
it -- the camera is always on the river, so that is the same as growing with distance from the camera. Rows of
different column counts are zipped together, so the band is watertight. The outer ranges are rings round the origin
whose inner edges tuck under the band.

The water is not in these meshes: the engine's water surface lies at y = 0 everywhere (a terrain node with its sea at
0, see build.py), and every mesh here starts below it, so the river's shape is simply where the land is not.
"""
from __future__ import annotations

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
from sonic_vfx import meshes as mx  # noqa: E402

TAU = 2.0 * math.pi

# ---- the river --------------------------------------------------------------------------------------------------
R0 = 1300.0
A3, P3 = 0.075, 0.6
A5, P5 = 0.035, 2.1


def rc(th):
    return R0 * (1.0 + A3 * math.sin(3.0 * th + P3) + A5 * math.sin(5.0 * th + P5))


def half_width(th):
    """Half the river's width (m): 56-80, wider on the long reaches."""
    return 68.0 + 12.0 * math.sin(2.0 * th + 1.0)


def centre(th):
    r = rc(th)
    return (r * math.sin(th), 0.0, r * math.cos(th))


def periodic_noise(th, cycles, seed, octaves=3):
    """0..1 smooth noise along the loop, periodic in theta (`cycles` lattice cells round the loop)."""
    return mx.fbm_p(0.37 * seed, (th % TAU) / TAU * cycles, seed, octaves, py=cycles)


# ---- the valley's profile across its band -------------------------------------------------------------------------
def valley_height(th, o, side):
    """Height (m) at `o` metres from the water's edge (negative: under the water), on `side` (+1 outer, -1 inner).
    A low dark rock ledge at the water, a strip of flat ground, then the valley wall rising to its ridgeline. Every
    dimension varies along the loop (slow periodic noise), and differently on the two sides, so neither repeats."""
    s = 0 if side > 0 else 1
    if o < 0.0:
        return max(-6.0, o * 0.45)
    bank_h = 2.0 + 5.0 * periodic_noise(th, 13, 3 + s)
    bank_w = 12.0 + 20.0 * periodic_noise(th, 9, 5 + s)
    foot = bank_w + 15.0 + 100.0 * periodic_noise(th, 6, 7 + s)          # where the wall starts
    wall_h = 90.0 + 140.0 * periodic_noise(th, 5, 9 + s)
    run = 220.0 + 300.0 * periodic_noise(th, 4, 13 + s)
    h = bank_h * mx.smooth(0.0, bank_w, o) ** 0.6
    h += wall_h * mx.smooth(foot, foot + run, o) ** 1.25
    return h


def wall_weight(th, o, side):
    s = 0 if side > 0 else 1
    bank_w = 12.0 + 20.0 * periodic_noise(th, 9, 5 + s)
    foot = bank_w + 15.0 + 100.0 * periodic_noise(th, 6, 7 + s)
    return mx.smooth(foot - 40.0, foot + 220.0, o)


# ---- the hero peaks: (theta deg, side, metres from the water's edge, height, radius, elongation, ridge azimuth deg
# relative to the river's tangent, seed). Alternating banks, each a deliberate silhouette: a long ridge with a summit
# off-centre, a steep pyramid, a twin summit. Heights are held so that from the river they rise 9-14 degrees.
PEAKS = [
    (8.0, +1, 560.0, 247.5, 330.0, 1.9, 25.0, 1),
    (38.0, -1, 470.0, 210.0, 280.0, 1.5, -40.0, 2),
    (71.0, +1, 760.0, 307.5, 420.0, 2.4, 10.0, 3),
    (104.0, -1, 560.0, 225.0, 300.0, 1.3, 60.0, 4),
    (139.0, +1, 640.0, 270.0, 380.0, 1.7, -20.0, 5),
    (176.0, -1, 430.0, 187.5, 260.0, 2.1, 15.0, 6),
    (205.0, +1, 880.0, 345.0, 470.0, 2.2, 35.0, 7),
    (238.0, -1, 600.0, 240.0, 320.0, 1.6, -55.0, 8),
    (272.0, +1, 520.0, 225.0, 300.0, 1.4, -10.0, 9),
    (306.0, -1, 500.0, 217.5, 290.0, 1.8, 30.0, 10),
    (334.0, +1, 700.0, 292.5, 400.0, 2.0, -35.0, 11),
]


def _peak_frames():
    out = []
    for deg, side, off, h, rad, elong, az, seed in PEAKS:
        th = math.radians(deg)
        r = rc(th) + side * (half_width(th) + off)
        cx, cz = r * math.sin(th), r * math.cos(th)
        # the river's tangent at theta (direction of increasing theta), turned by the ridge azimuth
        t = (math.cos(th), -math.sin(th))
        a = math.radians(az)
        u = (t[0] * math.cos(a) - t[1] * math.sin(a), t[0] * math.sin(a) + t[1] * math.cos(a))
        out.append((cx, cz, u, h, rad, elong, seed))
    return out


PEAK_FRAMES = _peak_frames()


def peak_height(x, z):
    """The hero peaks at (x, z): elongated, part-pyramidal cones (an L-infinity norm mixed into the round one gives
    them a few large planar faces), the summit pushed along the ridge, the crest broken by a little ridged noise."""
    best = -1e9
    for cx, cz, u, h, rad, elong, seed in PEAK_FRAMES:
        dx, dz = x - cx, z - cz
        if abs(dx) > rad * elong * 1.2 + 20 or abs(dz) > rad * elong * 1.2 + 20:
            continue
        a = dx * u[0] + dz * u[1]
        b = -dx * u[1] + dz * u[0]
        a -= 0.18 * rad * elong                      # summit off-centre along the ridge
        ua, vb = a / (rad * elong), b / rad
        d = 0.55 * math.sqrt(ua * ua + vb * vb) + 0.45 * max(abs(ua), abs(vb))
        if d >= 1.0:
            continue
        n = mx.fbm(x / 140.0, z / 140.0, 40 + seed, 3, ridged=True)
        best = max(best, h * (1.0 - d) ** 1.35 * (0.86 + 0.28 * n))
    return best


def detail(x, z, th, o, side):
    """Relief, so the facets are large, planar and irregular: a little on the flats, strong ridged spurs on the walls
    (their crests and gullies are what give the facets their different orientations). Nothing at the water's edge,
    so the bank meets it cleanly."""
    ww = wall_weight(th, o, side)
    flat = 2.5 * mx.smooth(3.0, 30.0, o) * (mx.fbm(x / 70.0, z / 70.0, 21, 2) - 0.5)
    spur = (30.0 + 0.12 * min(o, 900.0)) * ww * (mx.fbm(x / 230.0, z / 230.0, 23, 4, ridged=True) - 0.35)
    return flat + spur


def land_height(x, z, th, o, side):
    h = valley_height(th, o, side) + detail(x, z, th, o, side)
    if o > 10.0:
        h = max(h, peak_height(x, z))
    return h


# ---- the valley band ------------------------------------------------------------------------------------------------
# (offset from the water's edge in m, columns round the loop). Near rows are dense (about 17 m facets along the bank),
# the far ones coarse; the last outer row sinks so the band's edge tucks under the first range.
OUTER_ROWS = [(-16, 480), (0, 480), (9, 480), (22, 480), (40, 480), (64, 400), (96, 400), (138, 320), (190, 320),
              (256, 260), (340, 260), (450, 200), (590, 200), (760, 160), (960, 140), (1180, 120)]
INNER_NEAR = [(-16, 480), (0, 480), (9, 480), (22, 480), (40, 480), (64, 400), (96, 400), (138, 320), (190, 320),
              (256, 260), (340, 260), (450, 200)]
INNER_FAR = [(0.50, 160), (0.66, 120), (0.80, 90), (0.91, 60)]   # fractions of the way to the centre


def _row(offset_fn, n, seed, jitter=0.3, sink=0.0):
    pts = []
    for j in range(n):
        jt = (mx._hash(j, seed, 77) - 0.5) * 2.0 * jitter
        th = TAU * (j + jt) / n
        r, o, side = offset_fn(th, j)
        x, z = r * math.sin(th), r * math.cos(th)
        y = land_height(x, z, th, o, side) - sink
        pts.append((x, y, z))
    return pts


def _zip(inner, outer, up_is_outward):
    """Triangles between two closed rows (counts may differ), walking both by their angular parameter."""
    n, m = len(inner), len(outer)
    tris = []
    i = k = 0
    while i < n or k < m:
        ti = (i + 1) / n
        tk = (k + 1) / m
        a, b = inner[i % n], outer[k % m]
        if k >= m or (i < n and ti <= tk):
            c = inner[(i + 1) % n]
            tri = (a, b, c)
            i += 1
        else:
            c = outer[(k + 1) % m]
            tri = (a, b, c)
            k += 1
        # orient each triangle to face up
        nrm = mx.cross(mx.sub(tri[1], tri[0]), mx.sub(tri[2], tri[0]))
        if nrm[1] < 0.0:
            tri = (tri[0], tri[2], tri[1])
        tris.append(tri)
    return tris


def valley_band():
    rows_out, rows_in = [], []
    for idx, (o, n) in enumerate(OUTER_ROWS):
        last = idx == len(OUTER_ROWS) - 1
        f = (lambda o_: (lambda th, j: (rc(th) + half_width(th) + o_, o_, +1)))(o)
        rows_out.append(_row(f, n, 100 + idx, jitter=0.0 if o < 0 else 0.3, sink=40.0 if last else 0.0))
    for idx, (o, n) in enumerate(INNER_NEAR):
        f = (lambda o_: (lambda th, j: (rc(th) - half_width(th) - o_, o_, -1)))(o)
        rows_in.append(_row(f, n, 200 + idx, jitter=0.0 if o < 0 else 0.3))
    for idx, (fr, n) in enumerate(INNER_FAR):
        def f(th, j, fr=fr):
            edge = rc(th) - half_width(th)
            return (edge * (1.0 - fr), edge * fr, -1)
        rows_in.append(_row(f, n, 300 + idx))
    tris = []
    # outer band: row k to row k+1 (outward); inner band: row k to row k+1 (inward); the two -16 m rows are the river
    # bed's edges under the water, never joined across the river
    for a, b in zip(rows_out, rows_out[1:]):
        tris += _zip(a, b, True)
    for a, b in zip(rows_in, rows_in[1:]):
        tris += _zip(a, b, False)
    # the centre: a fan from the innermost row to the middle of the loop
    last = rows_in[-1]
    mid = (0.0, land_height(0.0, 0.0, 0.0, rc(0.0), -1), 0.0)
    for i in range(len(last)):
        a, b = last[i], last[(i + 1) % len(last)]
        tri = (a, b, mid)
        if mx.cross(mx.sub(tri[1], tri[0]), mx.sub(tri[2], tri[0]))[1] < 0.0:
            tri = (a, mid, b)
        tris.append(tri)
    return tris


# ---- the outer ranges -------------------------------------------------------------------------------------------
def _crest(t, peak, keep_outer):
    if t <= 0.0:
        return 0.0
    if t < peak:
        return mx.smooth(0.0, peak, t) ** 0.5
    if keep_outer:
        return 1.0 - 0.35 * mx.smooth(peak, 1.0, t)
    if t >= 1.0:
        return 0.0
    return mx.smooth(1.0, peak, t) ** 0.7


# (name, r0, r1, rings, columns, base height, relief, wavelength, seed, crest position, keep the outer side up)
RANGES = [
    ("near", 1950.0, 3500.0, 7, 150, 120.0, 320.0, 650.0, 3, 0.45, False),
    ("far", 3200.0, 5600.0, 7, 120, 230.0, 480.0, 950.0, 7, 0.5, False),
    ("distant", 5200.0, 9800.0, 6, 96, 380.0, 760.0, 1500.0, 13, 0.55, True),
]


def range_tris(name, r0, r1, nr, nth, base, amp, wl, seed, peak, keep):
    def h(x, z, r, th):
        t = (r - r0) / (r1 - r0)
        c = _crest(t, peak, keep)
        n = mx.fbm(x / wl, z / wl, seed, 4, ridged=True)
        return c * (base + amp * n) - 30.0 * (1.0 - c) ** 2 - 2.0
    return mx.polar_range(r0, r1, nr, nth, h, seed=seed)


def generate(mesh_dir):
    """Write every GLB; return {name: triangle count}."""
    os.makedirs(mesh_dir, exist_ok=True)
    out = {}
    tris = valley_band()
    mx.write_faceted(os.path.join(mesh_dir, "ab-valley.glb"), tris, "valley")
    out["valley"] = len(tris)
    for spec in RANGES:
        t = range_tris(*spec)
        mx.write_faceted(os.path.join(mesh_dir, "ab-%s.glb" % spec[0]), t, spec[0])
        out[spec[0]] = len(t)
    return out


# ---- the camera's path -------------------------------------------------------------------------------------------
def river_length(n=4000):
    total = 0.0
    p = centre(0.0)
    for i in range(1, n + 1):
        q = centre(TAU * i / n)
        total += math.dist(p, q)
        p = q
    return total


def theta_at_arc(n=8000):
    """A table mapping arc length to theta, so the camera moves at a constant speed."""
    arcs, ths = [0.0], [0.0]
    p = centre(0.0)
    for i in range(1, n + 1):
        th = TAU * i / n
        q = centre(th)
        arcs.append(arcs[-1] + math.dist(p, q))
        ths.append(th)
        p = q
    return arcs, ths


def lerp_table(arcs, ths, s):
    lo, hi = 0, len(arcs) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if arcs[mid] <= s:
            lo = mid
        else:
            hi = mid
    t = (s - arcs[lo]) / max(arcs[hi] - arcs[lo], 1e-9)
    return ths[lo] + (ths[hi] - ths[lo]) * t

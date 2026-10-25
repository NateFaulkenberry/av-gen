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
R0 = 3000.0
# a rounded triangle: with A3 = 0.10 each side's midpoint has zero curvature, so the loop has three long, nearly
# straight reaches the camera can look down (11-research-pass2-render.md s2.3); two small harmonics add the meanders
A3 = 0.10


# the river's course on the valley axis: straight reaches of about 1-2 km between mild bends (owner, Gate A take 3:
# "some straight aways and mild bends"). Each bend is one broad, low-curvature swing to one side -- a smooth bump in
# the river's offset from the axis -- with its own position, length and amplitude, alternating sides. Between them
# the offset is ~0 and the river runs straight down the valley toward the far ranges.
BENDS = []   # (theta centre, half-length in radians, amplitude m, side)


def _bends(count=7):
    out = []
    for i in range(count):
        c = TAU * (i + 0.5 + 0.35 * (_h(i, 1) - 0.5)) / count
        half = 0.11 + 0.06 * _h(i, 2)              # 0.11-0.17 rad: 330-510 m either side of the apex
        amp = (95.0 + 85.0 * _h(i, 3)) * (1 if i % 2 == 0 else -1)
        out.append((c, half, amp))
    return out


def _h(i, k):
    return mx._hash(i, k, 333)


BENDS = _bends()


def meander(th):
    d = 0.0
    for c, half, amp in BENDS:
        x = (th - c + math.pi) % TAU - math.pi
        if abs(x) < 2.5 * half:
            d += amp * math.exp(-(x / half) ** 2)
    return d


def rc_axis(th):
    """The valley's axis: the rounded triangle (A3 = 0.10 makes each side's midpoint straight). The camera follows it."""
    return R0 * (1.0 - A3 * math.cos(3.0 * th))


def rc(th):
    """The river: the valley axis, with the bends on it."""
    return rc_axis(th) + meander(th)


def half_width(th):
    """Half the river's width (m), measured radially: 155-215 m across the flow, widened where the river runs at an
    angle to the radius so the bends do not pinch -- a substantial river whose near reach carries a broad reflection."""
    perp = 185.0 + 30.0 * math.sin(2.0 * th + 1.0)
    e = 1e-4
    slope = (rc(th + e) - rc(th - e)) / (2.0 * e) / rc(th)
    # only half the radial correction: a full one widened the bends into lakes (owner, Gate A take 2)
    return perp * (1.0 + 0.5 * (math.sqrt(1.0 + slope * slope) - 1.0))


def centre(th):
    r = rc(th)
    return (r * math.sin(th), 0.0, r * math.cos(th))


def axis_centre(th):
    r = rc_axis(th)
    return (r * math.sin(th), 0.0, r * math.cos(th))


def periodic_noise(th, cycles, seed, octaves=3):
    """0..1 smooth noise along the loop, periodic in theta (`cycles` lattice cells round the loop)."""
    return mx.fbm_p(0.37 * seed, (th % TAU) / TAU * cycles, seed, octaves, py=cycles)


# ---- the valley's profile across its band -------------------------------------------------------------------------
def valley_height(th, o, side):
    """Height (m) at `o` metres from the water's edge (negative: under the water), on `side` (+1 outer, -1 inner).
    No lip: one continuous shore slope of 7-15 degrees through the waterline (the bed continues it below), easing to
    about 8 degrees out to the valley floor's edge `fl`, then the wall. The outer bank stands higher and steeper than
    the inner, so the two sides have distinct silhouettes."""
    s = 0 if side > 0 else 1
    s0 = math.tan(math.radians(7.0 + 8.0 * periodic_noise(th, 13, 3 + s)))
    if o < 0.0:
        return max(-8.0, o * s0)
    fl = floor_edge(th, side)
    if side > 0:
        wall_h = 110.0 + 110.0 * periodic_noise(th, 5, 9 + s)
    else:
        wall_h = 80.0 + 90.0 * periodic_noise(th, 5, 9 + s)
    run = 350.0 + 250.0 * periodic_noise(th, 4, 13 + s)
    low = s0 * min(o, 60.0) + 0.14 * max(o - 60.0, 0.0)
    return low + wall_h * mx.smooth(fl, fl + run, o) ** 1.3


def floor_edge(th, side):
    """Where the valley floor's lower slopes end and the wall begins (m from the water): 200-420."""
    s = 0 if side > 0 else 1
    return 200.0 + 220.0 * periodic_noise(th, 6, 31 + s)


def wall_weight(th, o, side):
    fl = floor_edge(th, side)
    return 0.15 * mx.smooth(40.0, 200.0, o) + 0.85 * mx.smooth(fl - 80.0, fl + 200.0, o)


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
        out.append((cx, cz, u, h, rad, elong, seed, side))
    return out


# ---- the spurs: ridges running down from the valley walls toward the river, every few degrees round the loop,
# alternating banks with jittered size and set-back. Looking down the river they overlap as they recede -- near, middle
# and far ridges stacked in depth -- which is the layered "river in a valley" read (the Living Prism aurora valley's
# composition, in this scene's faceted language). Each is a long low pyramid whose ridge points at the water.
def _spurs(count=140):
    out = []
    for i in range(count):
        side = 1 if i % 2 == 0 else -1
        jit = mx._hash(i, 3, 91)
        deg = (i + 0.35 * (jit - 0.5)) * 360.0 / count
        off = 450.0 + 650.0 * mx._hash(i, 5, 93)            # set back from the water's edge (m): near to far
        # summit height grows with the set-back, so from the river each rises about 8-12 degrees and the farther
        # ones show over the nearer: layers in depth, never a wall over the camera
        h = (30.0 + off * (0.13 + 0.07 * mx._hash(i, 7, 95))) * (1.15 if side > 0 else 0.85)
        rad = 120.0 + 0.25 * off
        elong = 1.8 + 1.0 * mx._hash(i, 11, 99)
        az = 90.0 + 30.0 * (mx._hash(i, 13, 101) - 0.5)     # across the river: the ridge points at the water
        out.append((deg, side, off, h, rad, elong, az, 100 + i))
    return out


# ---- the layered ranges: distinct peaks stepping back from the river in three layers per bank, at random bearings
# (so left and right never repeat). Nearer layers stand taller in the frame; further ones are smaller in angle and
# the program's haze pales them. The outer layers stand in the ring meshes (range_tris reads peak_height too).
LAYERS = [  # (side, set-back min, max (m from the water), height min, max, count)
    (+1, 650.0, 1100.0, 260.0, 420.0, 30),
    (+1, 1500.0, 2600.0, 480.0, 720.0, 34),
    (+1, 3400.0, 6000.0, 850.0, 1300.0, 36),
    (-1, 550.0, 1000.0, 220.0, 360.0, 26),
    (-1, 1250.0, 1900.0, 320.0, 480.0, 18),
]


def _layer_peaks():
    out = []
    k = 0
    for li, (side, o0, o1, h0, h1, n) in enumerate(LAYERS):
        for i in range(n):
            u = mx._hash(i, li, 211)
            deg = (i + 0.8 * (mx._hash(i, li, 213) - 0.5)) * 360.0 / n + 37.0 * li
            off = o0 + (o1 - o0) * u
            h = h0 + (h1 - h0) * mx._hash(i, li, 215)
            rad = h * (1.5 + 0.8 * mx._hash(i, li, 217))
            elong = 1.1 + 0.9 * mx._hash(i, li, 219)
            az = 360.0 * mx._hash(i, li, 221)
            out.append((deg % 360.0, side, off, h, rad, elong, az, 500 + k))
            k += 1
    return out


PEAKS = PEAKS + _spurs() + _layer_peaks()
PEAK_FRAMES = _peak_frames()


def peak_height(x, z, side=0):
    """The hero peaks at (x, z): elongated, part-pyramidal cones (an L-infinity norm mixed into the round one gives
    them a few large planar faces), the summit pushed along the ridge, the crest broken by a little ridged noise."""
    best = -1e9
    for cx, cz, u, h, rad, elong, seed, pside in PEAK_FRAMES:
        if side and pside != side:
            continue
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
    flat = 2.5 * mx.smooth(15.0, 70.0, o) * (mx.fbm(x / 70.0, z / 70.0, 21, 2) - 0.5)
    # rocky shoulders: large ridged facets from 30 m up the shore (the waterline itself stays a gentle slope)
    flat += (22.0 + 0.07 * min(o, 600.0)) * mx.smooth(30.0, 110.0, o) * (
        mx.fbm(x / 95.0, z / 95.0, 27 + (0 if side > 0 else 4), 3, ridged=True) - 0.25)
    # and a coarser set of rock buttresses, so every stretch of the near slopes has large readable facets
    flat += (12.0 + 0.04 * min(o, 600.0)) * mx.smooth(40.0, 160.0, o) * (
        mx.fbm(x / 260.0 + 3.7, z / 260.0, 29, 2, ridged=True) - 0.3)
    spur = (30.0 + 0.12 * min(o, 900.0)) * ww * (mx.fbm(x / 230.0, z / 230.0, 23, 4, ridged=True) - 0.35)
    return flat + spur


def land_height(x, z, th, o, side):
    h = valley_height(th, o, side) + detail(x, z, th, o, side)
    if o > 4.0:
        # a peak or spur rises only on its own bank, and its foot meets the water as the wall does
        fl = floor_edge(th, side)   # spurs and peaks stand behind the valley floor, never in the water
        h = max(h, peak_height(x, z, side) * mx.smooth(fl - 150.0, fl + 120.0, o) ** 0.7)
    return h


# ---- the valley band ------------------------------------------------------------------------------------------------
# (offset from the water's edge in m, columns round the loop). Near rows are dense (about 17 m facets along the bank),
# the far ones coarse; the last outer row sinks so the band's edge tucks under the first range.
OUTER_ROWS = [(-30, 900), (-8, 900), (14, 900), (40, 800), (72, 800), (110, 680), (160, 560), (220, 560),
              (300, 450), (400, 450), (520, 360), (680, 300), (880, 250), (1100, 210), (1350, 180)]
INNER_NEAR = [(-30, 900), (-8, 900), (14, 900), (40, 800), (72, 800), (110, 680), (160, 560), (220, 560),
              (300, 450), (400, 450), (520, 360)]
INNER_FAR = [(0.50, 160), (0.66, 120), (0.80, 90), (0.91, 60)]   # fractions of the way to the centre


def _row(offset_fn, n, seed, jitter=0.3, sink=0.0, ojit=0.0):
    pts = []
    for j in range(n):
        jt = (mx._hash(j, seed, 77) - 0.5) * 2.0 * jitter
        th = TAU * (j + jt) / n
        r, o, side = offset_fn(th, j)
        if ojit:   # break the rows' parallel strips: each vertex moves along its row's normal
            d = (mx._hash(j, seed, 79) - 0.5) * 2.0 * ojit
            r += d * side
            o += d
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
        rows_out.append(_row(f, n, 100 + idx, jitter=0.0 if o < 0 else 0.35, sink=40.0 if last else 0.0,
                             ojit=0.0 if (o < 0 or last) else 0.4 * (OUTER_ROWS[idx + 1][0] - o)))
    for idx, (o, n) in enumerate(INNER_NEAR):
        f = (lambda o_: (lambda th, j: (rc(th) - half_width(th) - o_, o_, -1)))(o)
        nxt = INNER_NEAR[idx + 1][0] if idx + 1 < len(INNER_NEAR) else o + 150
        rows_in.append(_row(f, n, 200 + idx, jitter=0.0 if o < 0 else 0.35, ojit=0.0 if o < 0 else 0.4 * (nxt - o)))
    for idx, (fr, n) in enumerate(INNER_FAR):
        def f(th, j, fr=fr):
            edge = rc(th) - half_width(th)
            return (edge * (1.0 - fr), edge * fr, -1)
        rows_in.append(_row(f, n, 300 + idx))
    outer, inner = [], []
    # outer band: row k to row k+1 (outward); inner band: row k to row k+1 (inward); the two -16 m rows are the river
    # bed's edges under the water, never joined across the river
    for a, b in zip(rows_out, rows_out[1:]):
        outer += _zip(a, b, True)
    for a, b in zip(rows_in, rows_in[1:]):
        inner += _zip(a, b, False)
    # the centre: a fan from the innermost row to the middle of the loop
    last = rows_in[-1]
    mid = (0.0, land_height(0.0, 0.0, 0.0, rc(0.0), -1), 0.0)
    for i in range(len(last)):
        a, b = last[i], last[(i + 1) % len(last)]
        tri = (a, b, mid)
        if mx.cross(mx.sub(tri[1], tri[0]), mx.sub(tri[2], tri[0]))[1] < 0.0:
            tri = (a, mid, b)
        inner.append(tri)
    # pass 3 (22-research-pass3-scene.md s5.1): the camera flies the loop in +theta with its right vector pointing out
    # of the loop, so the inner bank is always on the LEFT (the Moog's) and the outer bank on the RIGHT (the toms' and
    # the tone block's), with the three outer ranges
    return outer, inner


# ---- the riverbed (pass 3): faceted planes under the water for the river light to reveal ------------------------
# The owner (2026-10-24): the beam should reveal "an underwater faceted riverbed: the same low-poly, flat-shaded
# triangle planes as the mountains, seen through the water". Rows across the river, each vertex jittered along and
# across the flow and in depth, zipped into large irregular facets; deepest mid-river. The bed's program is nearly
# black (look.bed_program), so it is invisible until the light (ADR-1662's land term) catches its planes.
# (columns round the loop, interior rows across the river): BED["coarse"] about 50 m facets, BED["fine"] about 30 m.
BED = {"coarse": (380, 3), "fine": (640, 5)}


def bed_rows(variant="coarse"):
    cols, inner = BED[variant]
    fracs = [-1.0] + [-1.0 + 2.0 * (k + 1) / (inner + 1) for k in range(inner)] + [1.0]
    rows = []
    for idx, f in enumerate(fracs):
        edge = abs(f) > 0.999
        pts = []
        for j in range(cols):
            jt = 0.0 if edge else (mx._hash(j, 500 + idx, 77) - 0.5) * 0.7
            th = TAU * (j + jt) / cols
            fj = f if edge else f + (mx._hash(j, 500 + idx, 78) - 0.5) * 1.2 / (inner + 1)
            reach = half_width(th) - 30.0        # out to the banks' last under-water row (30 m inside the edge)
            r = rc(th) + fj * reach
            depth = -9.0 - 12.0 * (1.0 - min(1.0, fj * fj))
            if not edge:
                depth += (mx._hash(j, 500 + idx, 79) - 0.5) * 14.0   # tilted planes, so a grazing light picks some out
            pts.append((r * math.sin(th), depth, r * math.cos(th)))
        rows.append(pts)
    return rows


def bed_tris(variant="coarse"):
    rows = bed_rows(variant)
    tris = []
    for a, b in zip(rows, rows[1:]):
        tris += _zip(a, b, True)
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
    ("near", 4300.0, 7000.0, 9, 360, 120.0, 380.0, 900.0, 3, 0.45, False),
    ("far", 6500.0, 11000.0, 9, 300, 200.0, 500.0, 1300.0, 7, 0.5, False),
    ("distant", 10500.0, 19000.0, 7, 220, 600.0, 1300.0, 2100.0, 13, 0.55, True),
]


def range_tris(name, r0, r1, nr, nth, base, amp, wl, seed, peak, keep):
    def h(x, z, r, th):
        t = (r - r0) / (r1 - r0)
        c = _crest(t, peak, keep)
        n = mx.fbm(x / wl, z / wl, seed, 4, ridged=True)
        return max(c * (base + amp * n) - 30.0 * (1.0 - c) ** 2 - 2.0, peak_height(x, z) * min(1.0, 4.0 * t))
    return mx.polar_range(r0, r1, nr, nth, h, seed=seed)


# ---- Gate B: baked edge masks (research A probe F) (TEXCOORD_0 = (metres to this facet's chosen edge, that edge's importance)) ----------
import json as _json
import struct as _struct


def _key(p):
    return (round(p[0], 2), round(p[1], 2), round(p[2], 2))


def edge_uvs(tris):
    """Per triangle: the one edge most worth lighting -- a convex crease (ridge) with a large dihedral angle, raised
    for height -- and every vertex's distance to it in metres (affine across the facet, so the rasteriser's
    interpolation is exact). A facet with no such edge gets u = 1e4."""
    nrm = [mx.unit(mx.cross(mx.sub(b, a), mx.sub(c, a))) for a, b, c in tris]
    edges = {}
    for t, tri in enumerate(tris):
        for e in range(3):
            k = tuple(sorted((_key(tri[e]), _key(tri[(e + 1) % 3]))))
            edges.setdefault(k, []).append((t, e))
    imp = [[0.0, 0.0, 0.0] for _ in tris]
    for k, users in edges.items():
        if len(users) != 2:
            continue
        (t0, e0), (t1, e1) = users
        n0, n1 = nrm[t0], nrm[t1]
        cosang = max(-1.0, min(1.0, n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2]))
        ang = math.degrees(math.acos(cosang))
        opp = tris[t1][(e1 + 2) % 3]
        a = tris[t0][e0]
        convex = (opp[0] - a[0]) * n0[0] + (opp[1] - a[1]) * n0[1] + (opp[2] - a[2]) * n0[2] < 0.0
        if not convex:
            continue
        y = 0.5 * (k[0][1] + k[1][1])
        w = mx.smooth(14.0, 40.0, ang) * (0.35 + 0.65 * mx.smooth(20.0, 260.0, y))
        imp[t0][e0] = w
        imp[t1][e1] = w
    uvs = []
    for t, tri in enumerate(tris):
        e = max(range(3), key=lambda i: imp[t][i])
        w = imp[t][e]
        if w < 0.08:
            # no crease: no line, and the facet's own random value (a facet left at 0 lit with every other one)
            cx = sum(v[0] for v in tri) / 3.0
            cz = sum(v[2] for v in tri) / 3.0
            uvs += [(1.0e4, _crease_hash((cx, 0.0, cz), (cz, 0.0, cx)))] * 3
            continue
        p, q = tri[e], tri[(e + 1) % 3]
        d = mx.sub(q, p)
        L = math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2) or 1.0
        h = _crease_hash(p, q)   # the crease's own value: the two facets sharing it agree, so a picked ridge lights whole
        out = []
        for v in tri:
            r = mx.sub(v, p)
            c = mx.cross(r, d)
            # pass 3: u = metres to the crease over its importance (a weak crease draws thinner, under a pixel), v = the
            # crease's random value, which the glints' combs pick subsets by (22 s5.1)
            out.append((math.sqrt(c[0] ** 2 + c[1] ** 2 + c[2] ** 2) / L / w, h))
        uvs += out
    return uvs


def _crease_hash(p, q):
    """A deterministic value in [0, 1) for an edge, the same whichever way round its endpoints come."""
    a, b = sorted((_key(p), _key(q)))
    x = int(round((a[0] + b[0]) * 2.0)) * 73856093 ^ int(round((a[2] + b[2]) * 2.0)) * 19349663 ^ \
        int(round((a[1] + b[1]) * 2.0)) * 83492791
    x &= 0xFFFFFFFF
    x ^= x >> 16
    x = (x * 0x45D9F3B) & 0xFFFFFFFF
    x ^= x >> 16
    return (x & 0xFFFFFF) / float(1 << 24)


def write_faceted_uv(path, tris, name):
    pos, nrm = mx.faceted(tris)
    uvs = edge_uvs(tris)
    n = len(pos)
    pb = b"".join(_struct.pack("<3f", *p) for p in pos)
    nb = b"".join(_struct.pack("<3f", *v) for v in nrm)
    ub = b"".join(_struct.pack("<2f", *u) for u in uvs)
    ib = _struct.pack("<%dI" % n, *range(n))
    blob = pb + nb + ub + ib
    lo = [min(p[k] for p in pos) for k in range(3)]
    hi = [max(p[k] for p in pos) for k in range(3)]
    o1, o2, o3 = len(pb), len(pb) + len(nb), len(pb) + len(nb) + len(ub)
    g = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
         "materials": [{"name": name}],
         "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                   "indices": 3, "material": 0, "mode": 4}]}],
         "buffers": [{"byteLength": len(blob)}],
         "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(pb), "target": 34962},
                         {"buffer": 0, "byteOffset": o1, "byteLength": len(nb), "target": 34962},
                         {"buffer": 0, "byteOffset": o2, "byteLength": len(ub), "target": 34962},
                         {"buffer": 0, "byteOffset": o3, "byteLength": len(ib), "target": 34963}],
         "accessors": [{"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3", "min": lo, "max": hi},
                       {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC3"},
                       {"bufferView": 2, "componentType": 5126, "count": n, "type": "VEC2"},
                       {"bufferView": 3, "componentType": 5125, "count": n, "type": "SCALAR"}]}
    js = _json.dumps(g, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    with open(path, "wb") as f:
        f.write(_struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob)))
        f.write(_struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(_struct.pack("<II", len(blob), 0x004E4942) + blob)


def generate(mesh_dir):
    """Write every GLB, each with its edge mask in TEXCOORD_0; return {name: triangle count}."""
    os.makedirs(mesh_dir, exist_ok=True)
    out = {}
    outer, inner = valley_band()
    write_faceted_uv(os.path.join(mesh_dir, "ab-valleyL.glb"), inner, "valleyL")
    write_faceted_uv(os.path.join(mesh_dir, "ab-valleyR.glb"), outer, "valleyR")
    out["valleyL"] = len(inner)
    out["valleyR"] = len(outer)
    for spec in RANGES:
        t = range_tris(*spec)
        write_faceted_uv(os.path.join(mesh_dir, "ab-%s.glb" % spec[0]), t, spec[0])
        out[spec[0]] = len(t)
    for variant in BED:
        t = bed_tris(variant)
        write_faceted_uv(os.path.join(mesh_dir, "ab-bed-%s.glb" % variant), t, "bed")
        out["bed-" + variant] = len(t)
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


def theta_at_arc(n=8000, curve=None):
    """A table mapping arc length along `curve` (the valley axis by default) to theta, so the camera moves at a
    constant speed."""
    curve = curve or axis_centre
    arcs, ths = [0.0], [0.0]
    p = curve(0.0)
    for i in range(1, n + 1):
        th = TAU * i / n
        q = curve(th)
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

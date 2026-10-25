"""Aurora Borealis: the land. Pass 5: a lake in front of a static camera with faceted mountains all round it -- a
horseshoe open toward the camera -- the river valley's own shore profile, hero peaks, spurs and three receding layers,
written as flat-faceted GLBs (tools/sonic_vfx/meshes.py), plus the lakebed and the fish.

Everything is a pure function of the constants below, so a rebuild writes the same bytes.

Coordinates: metres, +Y up; the camera at the origin, looking down -z. The water is not in these meshes: the
engine's water surface lies at y = 0 (a terrain node with its sea at 0, see look.water_node), and the land starts
below it at the shore, so the lake's outline is simply where the land is not.
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

# ---- the lake (pass 5) ------------------------------------------------------------------------------------------
# The owner (pass 5): "turn the river into a lake and make the mountains horse shoe around the lake - pretty much close
# to what we have now. static camera". The camera stands still at the origin, looking down -z; the lake is an ellipse
# in front of it whose near end passes just behind the camera, and the land -- the same profile, peaks, spurs and
# layered ranges as the river's valley -- rises all round it. Seen from the camera that is a horseshoe open toward it:
# the arms come forward on the left and the right, the back closes the view.
#
# Polar coordinates about the lake's centre C: phi = 0 straight away from the camera (-z), +pi/2 to the right (+x),
# -pi/2 to the left; `o` metres outward from the shore. Every recipe below that took (theta, offset, side) round the
# river's loop takes (phi, o, side) here, periodic the same way.
LAKE_Z = -950.0                  # the lake's centre, metres in front of the camera
LAKE_A, LAKE_B = 520.0, 1150.0   # its semi-axes across and along the view: 290 m either side of the camera, 2.1 km deep
C = (0.0, LAKE_Z)


def lake_dir(ph):
    return (math.sin(ph), -math.cos(ph))


def periodic_noise(th, cycles, seed, octaves=3):
    """0..1 smooth noise round the lake, periodic in phi (`cycles` lattice cells round it)."""
    return mx.fbm_p(0.37 * seed, (th % TAU) / TAU * cycles, seed, octaves, py=cycles)


def shore_r(ph):
    """The shore's distance from C: the ellipse, its outline broken by a gentle wobble (bays and points)."""
    e = 1.0 / math.sqrt((math.sin(ph) / LAKE_A) ** 2 + (math.cos(ph) / LAKE_B) ** 2)
    return e * (1.0 + 0.14 * (periodic_noise(ph, 9, 41) - 0.5))


def lake_at(ph, r):
    d = lake_dir(ph)
    return (C[0] + r * d[0], C[1] + r * d[1])


def lake_polar(x, z):
    return math.atan2(x - C[0], -(z - C[1])), math.hypot(x - C[0], z - C[1])


def side_of(ph):
    """+1 right (x > 0, the bass's mountains), -1 left (the Moog's)."""
    return 1 if math.sin(ph) >= 0.0 else -1


def right_weight(ph):
    """The blend between the left and right banks' recipes: 0 left, 1 right, eased across the back and behind the camera
    so the land is continuous where the two meshes meet."""
    return mx.smooth(-0.22, 0.22, math.sin(ph))


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
    run = 260.0 + 200.0 * periodic_noise(th, 4, 13 + s)
    low = s0 * min(o, 60.0) + 0.14 * max(o - 60.0, 0.0)
    return low + wall_h * mx.smooth(fl, fl + run, o) ** 1.3


def floor_edge(th, side):
    """Where the shore's lower slopes end and the wall begins (m from the water): 90-210 round the lake (the river's
    valley floor was 200-420: a mountain lake's walls come down closer to the water)."""
    s = 0 if side > 0 else 1
    return 90.0 + 120.0 * periodic_noise(th, 6, 31 + s)


def wall_weight(th, o, side):
    fl = floor_edge(th, side)
    return 0.15 * mx.smooth(40.0, 200.0, o) + 0.85 * mx.smooth(fl - 80.0, fl + 200.0, o)


# ---- the hero peaks round the lake: (bearing from the camera deg, metres back from the shore along that bearing,
# elevation from the camera deg, radius over height, elongation, ridge azimuth deg relative to the shore's tangent,
# seed). Each a deliberate silhouette -- a long ridge with its summit off-centre, a steep pyramid, a twin summit --
# placed by where it stands in the frame (bearing 0 dead ahead, +-41 the frame's edges) and its height set so it rises
# the stated angle above the water: the arms tall at the frame's edges, the back lower, so the horseshoe reads.
HEROES = [
    (-50.0, 260.0, 17.0, 1.2, 1.5, 30.0, 1), (-37.0, 300.0, 15.0, 1.25, 1.9, -40.0, 2),
    (-26.0, 380.0, 12.0, 1.3, 1.4, 60.0, 3), (-15.0, 420.0, 9.5, 1.2, 2.1, 15.0, 4),
    (-5.0, 520.0, 8.0, 1.35, 1.7, -20.0, 5), (6.0, 460.0, 9.0, 1.3, 2.3, 10.0, 6),
    (16.0, 420.0, 10.5, 1.25, 1.8, 35.0, 7), (27.0, 340.0, 13.0, 1.3, 1.5, -55.0, 8),
    (38.0, 300.0, 15.5, 1.2, 2.2, -10.0, 9), (51.0, 260.0, 17.5, 1.35, 1.6, 25.0, 10),
]


def _place(ph, off):
    return lake_at(ph, shore_r(ph) + off)


def _shore_on_bearing(bearing):
    """The point where the ray from the camera on `bearing` (deg, + right) leaves the lake."""
    b = math.radians(bearing)
    d = (math.sin(b), -math.cos(b))
    s = 0.0
    while True:
        x, z = d[0] * s, d[1] * s
        ph, r = lake_polar(x, z)
        if s > 50.0 and r > shore_r(ph):
            return x, z, d
        s += 2.0


def _heroes():
    out = []
    for bearing, back, elev, rr, elong, az, seed in HEROES:
        x, z, d = _shore_on_bearing(bearing)
        x, z = x + d[0] * back, z + d[1] * back
        ph, r = lake_polar(x, z)
        h = math.hypot(x, z) * math.tan(math.radians(elev))
        out.append((math.degrees(ph), side_of(ph), r - shore_r(ph), h, h * rr, elong, az, seed))
    return out


# ---- the spurs: ridges running down from the slopes toward the water every few degrees round the lake, with
# jittered size and set-back, overlapping as they recede: near, middle and far ridges stacked in depth (the river
# valley's layered read, kept). Each is a long low pyramid whose ridge points at the water.
def _spurs(count=56):
    out = []
    for i in range(count):
        jit = mx._hash(i, 3, 91)
        deg = ((i + 0.35 * (jit - 0.5)) * 360.0 / count + 180.0) % 360.0 - 180.0
        side = side_of(math.radians(deg))
        off = 420.0 + 600.0 * mx._hash(i, 5, 93)            # set back from the shore (m): near to far
        h = (30.0 + off * (0.13 + 0.07 * mx._hash(i, 7, 95))) * (1.15 if side > 0 else 0.85)
        rad = 120.0 + 0.25 * off
        elong = 1.8 + 1.0 * mx._hash(i, 11, 99)
        az = 90.0 + 30.0 * (mx._hash(i, 13, 101) - 0.5)     # across the shore: the ridge points at the water
        out.append((deg, side, off, h, rad, elong, az, 100 + i))
    return out


# ---- the layered ranges: distinct peaks stepping back from the shore in three layers, at random bearings round the
# lake; nearer layers stand taller in the frame, further ones smaller in angle, paled by the program's haze. The right
# bank stands a little higher than the left, as the river's outer bank did. The outer two layers stand in the ring meshes.
LAYERS = [  # (set-back min, max (m from the shore), height min, max, count)
    (600.0, 1100.0, 260.0, 420.0, 24),
    (1400.0, 2400.0, 480.0, 720.0, 28),
    (3000.0, 5200.0, 850.0, 1300.0, 32),
]


def _layer_peaks():
    out = []
    k = 0
    for li, (o0, o1, h0, h1, n) in enumerate(LAYERS):
        for i in range(n):
            u = mx._hash(i, li, 211)
            deg = ((i + 0.8 * (mx._hash(i, li, 213) - 0.5)) * 360.0 / n + 37.0 * li + 180.0) % 360.0 - 180.0
            side = side_of(math.radians(deg))
            off = o0 + (o1 - o0) * u
            h = (h0 + (h1 - h0) * mx._hash(i, li, 215)) * (1.08 if side > 0 else 0.92)
            rad = h * (1.5 + 0.8 * mx._hash(i, li, 217))
            elong = 1.1 + 0.9 * mx._hash(i, li, 219)
            az = 360.0 * mx._hash(i, li, 221)
            out.append((deg, side, off, h, rad, elong, az, 500 + k))
            k += 1
    return out


PEAKS = _heroes() + _spurs() + _layer_peaks()


def _peak_frames():
    out = []
    for deg, side, off, h, rad, elong, az, seed in PEAKS:
        ph = math.radians(deg)
        cx, cz = _place(ph, off)
        d = lake_dir(ph)
        t = (-d[1], d[0])                          # the shore's tangent (increasing phi), turned by the ridge azimuth
        a = math.radians(az)
        u = (t[0] * math.cos(a) - t[1] * math.sin(a), t[0] * math.sin(a) + t[1] * math.cos(a))
        out.append((cx, cz, u, h, rad, elong, seed, side))
    return out


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
        fl = floor_edge(th, side)   # spurs and peaks stand behind the lower slopes, never in the water
        h = max(h, peak_height(x, z, side) * mx.smooth(fl - 150.0, fl + 120.0, o) ** 0.7)
    return h


def lake_land(x, z, ph, o):
    """The land round the lake: the left bank's recipe and the right's, blended across the back and behind the camera
    (right_weight), so the two halves meet without a step."""
    w = right_weight(ph)
    if w <= 0.0:
        return land_height(x, z, ph, o, -1)
    if w >= 1.0:
        return land_height(x, z, ph, o, +1)
    return (1.0 - w) * land_height(x, z, ph, o, -1) + w * land_height(x, z, ph, o, +1)


# ---- the shore band ---------------------------------------------------------------------------------------------
# (offset from the shore in m, columns round the lake). Near rows are dense (about 17 m facets along the shore), the
# far ones coarse; the last row sinks so the band's edge tucks under the first range. The first row is 8 m inside the
# water's edge: the lakebed's rim is the same row (bed_rows), so bed and bank share an edge.
BAND_ROWS = [(-8, 440), (14, 440), (40, 400), (72, 400), (110, 360), (160, 320), (220, 320), (300, 280), (400, 280),
             (520, 240), (680, 220), (880, 200), (1100, 190), (1350, 180)]


def _row(o, n, seed, jitter=0.3, sink=0.0, ojit=0.0):
    pts = []
    for j in range(n):
        jt = (mx._hash(j, seed, 77) - 0.5) * 2.0 * jitter
        th = TAU * (j + jt) / n
        oo = o
        if ojit:   # break the rows' parallel strips: each vertex moves along its row's normal
            oo += (mx._hash(j, seed, 79) - 0.5) * 2.0 * ojit
        x, z = lake_at(th, shore_r(th) + oo)
        pts.append((x, lake_land(x, z, th, oo) - sink, z))
    return pts


def _zip(inner, outer, up_is_outward=True):
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
            i += 1
        else:
            c = outer[(k + 1) % m]
            k += 1
        tri = (a, b, c)
        nrm = mx.cross(mx.sub(tri[1], tri[0]), mx.sub(tri[2], tri[0]))
        if nrm[1] < 0.0:   # orient each triangle to face up
            tri = (tri[0], tri[2], tri[1])
        tris.append(tri)
    return tris


def shore_band():
    rows = []
    for idx, (o, n) in enumerate(BAND_ROWS):
        last = idx == len(BAND_ROWS) - 1
        nxt = BAND_ROWS[idx + 1][0] if not last else o
        rows.append(_row(o, n, 100 + idx, jitter=0.0 if o < 0 else 0.35, sink=40.0 if last else 0.0,
                         ojit=0.0 if (o < 0 or last) else 0.4 * (nxt - o)))
    tris = []
    for a, b in zip(rows, rows[1:]):
        tris += _zip(a, b)
    return tris


def split_lr(tris):
    """The left (x < 0: the Moog's) and right (the bass's) halves, by each triangle's centre."""
    left, right = [], []
    for t in tris:
        (left if t[0][0] + t[1][0] + t[2][0] < 0.0 else right).append(t)
    return left, right


# ---- the lakebed (pass 5; the riverbed of pass 3): faceted planes under the water for the light to reveal. The owner:
# "make the pond appear deeper". Rings inward from the shore, each vertex jittered round, inward and in depth, zipped
# into large irregular facets; a shelf falling from the shore to about 45 m 500 m out, then a floor at 55-65 m, so a
# beam crossing the lake lights the slopes near the shores and loses the bed as it goes deeper (the land term's light
# reaches 40 m down, and the water's absorption darkens what it shows), and the middle is a dark volume. The program
# is nearly black (look.bed_program): only the light shows it. Rings: (fraction of the way to the centre, columns).
BED = {"coarse": [(0.0, 440), (0.035, 300), (0.09, 240), (0.16, 200), (0.26, 160), (0.38, 130), (0.52, 100),
                  (0.68, 70), (0.84, 40)],
       "fine": [(0.0, 440), (0.02, 400), (0.05, 340), (0.09, 300), (0.14, 260), (0.2, 220), (0.27, 190), (0.35, 160),
                (0.45, 130), (0.56, 100), (0.68, 80), (0.82, 50)]}
BED_FLOOR = -62.0


def bed_depth(rim, d):
    """The bed's depth `d` metres in from the shore, below the rim row's height `rim`."""
    return rim - 44.0 * mx.smooth(0.0, 520.0, d) ** 0.8 - 14.0 * mx.smooth(350.0, 900.0, d)


def bed_rows(variant="coarse"):
    rows = []
    for idx, (f, cols) in enumerate(BED[variant]):
        edge = idx == 0
        pts = []
        for j in range(cols):
            jt = 0.0 if edge else (mx._hash(j, 500 + idx, 77) - 0.5) * 0.7
            th = TAU * (j + jt) / cols
            sr = shore_r(th)
            if edge:   # the band's first row, vertex for vertex
                x, z = lake_at(th, sr - 8.0)
                pts.append((x, lake_land(x, z, th, -8.0), z))
                continue
            rings = BED[variant]
            gap = (rings[idx + 1][0] if idx + 1 < len(rings) else 1.0) - f
            fj = f + (mx._hash(j, 500 + idx, 78) - 0.5) * 0.5 * min(gap, f - rings[idx - 1][0])
            r = (sr - 8.0) * (1.0 - fj)
            x, z = lake_at(th, r)
            rim = valley_height(th, -8.0, side_of(th))
            y = bed_depth(rim, sr - r) + (mx._hash(j, 500 + idx, 79) - 0.5) * 12.0 * min(1.0, (sr - r) / 60.0)
            pts.append((x, y, z))
        rows.append(pts)
    return rows


def bed_tris(variant="coarse"):
    rows = bed_rows(variant)
    tris = []
    for a, b in zip(rows, rows[1:]):
        tris += _zip(b, a)
    centre = (C[0], BED_FLOOR - 2.0, C[1])
    last = rows[-1]
    for i in range(len(last)):
        tri = (last[i], last[(i + 1) % len(last)], centre)
        nrm = mx.cross(mx.sub(tri[1], tri[0]), mx.sub(tri[2], tri[0]))
        tris.append(tri if nrm[1] >= 0.0 else (tri[0], tri[2], tri[1]))
    return tris


# ---- the fish (pass 4; pass 5: "less fish, add some variety to their shapes and sizes - some of them stick out of the
# water make sure that doesnt happen"). Eighteen single faceted fish of four body types, each its own size and
# heading, spread over the lake where the beams run (the kick's, snare's and toms' lanes) so the light finds them.
# Their program is the bed's (near black): a fish shows only when a beam's under-water light passes over it. Static
# (no swim motion), so a fish's highest point is fixed: every one is clamped so its whole body -- back, fins, tail --
# stays FISH_CLEARANCE below the surface, and check_fish() (run by build.py and by the integration test) proves it.
FISH_COUNT = 18
FISH_CLEARANCE = 1.5   # metres between the surface and a fish's highest vertex, at least
# body types, in the fish's frame (forward f, up u, right r; times its size): nose, tail root, back, belly, the flanks'
# half-width, the tail fin's reach, top and bottom, a dorsal fin's height (0: none), and the size range (m)
FISH_TYPES = {
    "trout": dict(nose=0.55, root=-0.35, back=0.22, belly=-0.16, flank=0.14, tail=-0.62, tail_top=0.2,
                  tail_bot=-0.16, dorsal=0.0, size=(8.0, 13.0)),
    "bream": dict(nose=0.42, root=-0.28, back=0.36, belly=-0.30, flank=0.11, tail=-0.52, tail_top=0.26,
                  tail_bot=-0.24, dorsal=0.46, size=(7.0, 11.0)),
    "pike": dict(nose=0.78, root=-0.5, back=0.12, belly=-0.1, flank=0.09, tail=-0.8, tail_top=0.15,
                 tail_bot=-0.13, dorsal=0.2, size=(12.0, 18.0)),
    "ray": dict(nose=0.42, root=-0.3, back=0.07, belly=-0.05, flank=0.55, tail=-1.0, tail_top=0.02,
                tail_bot=-0.02, dorsal=0.0, size=(10.0, 16.0)),
}
FISH_ORDER = ["trout", "bream", "pike", "ray", "trout", "bream"]
FISH_LANES = [-150.0, 0.0, 150.0]   # look.py's kick, toms and snare lanes (m right of the lake's axis)


def _fish(kind, cx, cy, cz, heading, size):
    """One low-poly fish of `kind`: a six-point body (nose, tail root, back, belly, two flanks), a two-sided tail fin,
    and for some a dorsal fin."""
    b = FISH_TYPES[kind]
    c, s_ = math.cos(heading), math.sin(heading)

    def P(f, u, r):   # forward, up, right in the fish's frame -> world
        return (cx + (f * s_ + r * c) * size, cy + u * size, cz + (f * c - r * s_) * size)
    nose, root = P(b["nose"], 0.0, 0.0), P(b["root"], 0.0, 0.0)
    back, belly = P(0.05, b["back"], 0.0), P(0.0, b["belly"], 0.0)
    left, right = P(0.05, 0.02, -b["flank"]), P(0.05, 0.02, b["flank"])
    tail_top, tail_bot = P(b["tail"], b["tail_top"], 0.0), P(b["tail"], b["tail_bot"], 0.0)
    body = [(nose, back, left), (nose, right, back), (nose, left, belly), (nose, belly, right),
            (root, left, back), (root, back, right), (root, belly, left), (root, right, belly)]
    tail = [(root, tail_top, tail_bot), (root, tail_bot, tail_top)]
    if b["dorsal"] > 0.0:
        d0, d1, top = P(0.12, b["back"] * 0.8, 0.0), P(-0.2, b["back"] * 0.5, 0.0), P(-0.12, b["dorsal"], 0.0)
        tail += [(d0, top, d1), (d0, d1, top)]
    return body + tail


def fish_list():
    """(kind, x, y, z, heading, size) per fish; y is the body's centre line, already clamped below the surface."""
    out = []
    for k in range(FISH_COUNT):
        kind = FISH_ORDER[k % len(FISH_ORDER)]
        b = FISH_TYPES[kind]
        lo, hi = b["size"]
        size = lo + (hi - lo) * mx._hash(k, 700, 7)
        x = FISH_LANES[k % 3] + (mx._hash(k, 700, 2) - 0.5) * 140.0
        z = -380.0 - 1100.0 * (k + mx._hash(k, 700, 1)) / FISH_COUNT
        heading = TAU * mx._hash(k, 700, 6)          # every way round: along, across, toward, away
        y = -3.0 - 6.0 * mx._hash(k, 700, 5)
        top = max(b["back"], b["tail_top"], b["dorsal"], 0.02)
        y = min(y, -FISH_CLEARANCE - top * size)     # the whole body below the surface
        out.append((kind, x, y, z, heading, size))
    return out


def fish_tris():
    tris = []
    for kind, x, y, z, heading, size in fish_list():
        tris += _fish(kind, x, y, z, heading, size)
    return tris


def check_fish():
    """Every fish vertex at least FISH_CLEARANCE below the water (y = 0); returns the highest vertex's y."""
    top = max(v[1] for t in fish_tris() for v in t)
    assert top <= -FISH_CLEARANCE + 1e-6, "a fish breaks the surface: its highest vertex is at y = %.2f m" % top
    return top


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


# (name, r0, r1, rings, columns, base height, relief, wavelength, seed, crest position, keep the outer side up): rings
# round the lake's centre, their inner edges under the shore band's last row
RANGES = [
    ("near", 1650.0, 4800.0, 10, 260, 120.0, 380.0, 900.0, 3, 0.45, False),
    ("far", 4300.0, 8500.0, 9, 240, 200.0, 500.0, 1300.0, 7, 0.5, False),
    ("distant", 8000.0, 16000.0, 7, 200, 600.0, 1300.0, 2100.0, 13, 0.55, True),
]


def range_tris(name, r0, r1, nr, nth, base, amp, wl, seed, peak, keep):
    def h(x, z, r, th):
        x, z = x + C[0], z + C[1]
        t = (r - r0) / (r1 - r0)
        c = _crest(t, peak, keep)
        n = mx.fbm(x / wl, z / wl, seed, 4, ridged=True)
        return max(c * (base + amp * n) - 30.0 * (1.0 - c) ** 2 - 2.0, peak_height(x, z) * min(1.0, 4.0 * t))
    return [tuple((v[0] + C[0], v[1], v[2] + C[1]) for v in tri)
            for tri in mx.polar_range(r0, r1, nr, nth, h, seed=seed)]


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
    _write_faceted_uv(path, tris, name)


def _write_faceted_uv(path, tris, name):
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
    """Write every GLB, each with its edge mask in TEXCOORD_0; return {name: triangle count}. Each land mesh comes in a
    left and a right half (the Moog's and the bass's programs)."""
    os.makedirs(mesh_dir, exist_ok=True)
    out = {}
    parts = [("shore", shore_band())] + [(spec[0], range_tris(*spec)) for spec in RANGES]
    for name, tris in parts:
        for side, half in zip("LR", split_lr(tris)):
            write_faceted_uv(os.path.join(mesh_dir, "ab-%s%s.glb" % (name, side)), half, name + side)
            out[name + side] = len(half)
    check_fish()
    t = fish_tris()
    write_faceted_uv(os.path.join(mesh_dir, "ab-fish.glb"), t, "fish")
    out["fish"] = len(t)
    for variant in BED:
        t = bed_tris(variant)
        write_faceted_uv(os.path.join(mesh_dir, "ab-bed-%s.glb" % variant), t, "bed")
        out["bed-" + variant] = len(t)
    return out


LAND = ["shore", "near", "far", "distant"]   # the land meshes, each as <name>L and <name>R


if __name__ == "__main__":
    if "--check-fish" in sys.argv:
        print("fish: %d, the highest vertex %.2f m below the surface (at least %.1f m required)" % (
            FISH_COUNT, -check_fish(), FISH_CLEARANCE))

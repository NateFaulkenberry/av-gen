"""Neon Dreams: the world's fixed places, shared by the layout and the cloud design. Pure Python.

PLAN coordinates: x east, y north, z up, metres (Blender's frame). The Mooring is at the origin and the journey runs
toward +x. ENGINE coordinates (AV Gen, Y-up) are E(p) = (x, z, -y); a yaw about plan +z is the same yaw about engine +y.
"""
import math

FLOOR, BAY = 3.2, 2.4


def E(p):
    return [round(p[0], 3), round(p[2], 3), round(-p[1], 3)]


# ---- the Quay: an S-curved deck street stepping down twice toward the east
QUAY_X0, QUAY_X1 = -81.4, 112.0
STAIRS = (-25.0, 50.0)                      # centres of the two flights (4.8 m run each)


def qy(x):
    return 8.0 * math.sin((x + 80.0) / 190.0 * 2 * math.pi)


def qz(x):
    return 131.2 if x < STAIRS[0] else (128.0 if x < STAIRS[1] else 124.8)


def qyaw(x):
    return math.atan2(qy(x + 3) - qy(x - 3), 6.0)


# ---- places (plan)
GREAT_LANTERN = (-128.0, 48.0, 134.4)
PLAZA = (-102.0, -6.0, 131.2)
PIER_ROOT, PIER_DIR = (-118.0, -26.0, 131.2), (-0.8211, -0.5708)          # the pier runs south-west for 250 m, then breaks
GARDEN = (140.0, -34.0, 121.6)
HEAD, VEIL, FORE = (0.0, 262.0), (345.0, -45.0), (-120.0, -330.0)           # the cumulus towers around the Mooring (the Veil sits on the line from the approach to the Condensery)
CONDENSERY, CONDENSERY_YAW = (2050.0, 450.0, 215.0), 0.3 - math.radians(60.0)      # turned 60 degrees (engine, iteration 4): seen from the reveal the stacks stand apart instead of in one line
LONE_LAMP = (746.0, -563.0, 205.0)        # iteration 6: was (900, -300, 96), 2 degrees from the Outer Light as the approach sees it and level with the sea's crests. Now 11 degrees right of the Outer Light and 110 m higher, on a cloud of its own that touches nothing ((882, -478) was tried: from the approach it stood in front of Highwater's lights)
# iteration 6: a third settlement, never reached: glimpsed on the right of the reveal, 2.2 km out, at the horizon, half behind its own cloud
HIGHWATER, HIGHWATER_YAW = (2850.0, -480.0, 196.0), math.radians(-14.0)
LONG_ARC = (22500.0, 2351.0, -38.0)       # where the Long Arc leaves the sea (meshes/far/long_arc.glb: its +x is turned to the south)
# Iteration 6: the break was at (1640, 60), 1 km ahead of the reveal and 16 to 30 degrees under its horizon: below the frame
# for the whole reveal. It is now 450 m further east, with its north rim under the Condensery's pedestal: 9 to 20 degrees
# under the horizon from the reveal's path, so the whole break lies in the lower middle of the frame with the works
# standing on its far shore. OCEAN_Y is where the cosmic ocean's surface has to be for that view to see it: at -900 a
# line of sight 10 to 20 degrees down meets the far wall of the break long before it meets the ocean (measured in AV Gen).
BREAK = dict(centre=(2090.0, 20.0), radius=270.0, elongation=2.0, yaw=0.05, ocean_y=-110.0, ocean_semi_axes=(600.0, 330.0))
# lighthouses on cloud-rock outcrops: (name, asset, x, y, deck z, yaw deg, beam rpm, phase, beam length)
LIGHTHOUSES = [("harbour_light", "lighthouse_a", -374.0, -339.0, 128.0, 20.0, 5.0, 0.0, 900.0),
               ("outer_light", "lighthouse_b", 700.0, -250.0, 118.0, -35.0, 3.75, 0.37, 1100.0),
               ("break_light", "lighthouse_c", 1500.0, -70.0, 66.0, 60.0, 4.3, 0.71, 1000.0)]        # iteration 6: moved with the break (was (1150, -40)): on its west rim, right of centre in the reveal
# bearings are plan headings: 0 = +x (east), counter-clockwise, 90 = north. The cloud key was 34 / 26 in the Blender frames;
# in the engine that sat ahead of the approach and forward-scattered every cloud to milk, so the engine moved it to -18 / 12.
# Iteration 6: the disc stands at 21.5 (was 19). From the approach it is hidden behind the Veil either way; at the reveal the
# camera now heads about 4 degrees north of east with the Condensery on the left third, and at 21.5 the works stands on the
# moon's right limb and slides into the disc as the camera drifts on (the moon is at infinity, the works 1.4 km out).
MOON = dict(light_from_bearing_deg=-18.0, light_elevation_deg=12.0, disc_bearing_deg=21.5, disc_centre_elevation_deg=2.2, disc_angular_radius_deg=8.2,
            blender_frames_light_from=[34.0, 26.0], bearing_convention="plan heading: 0 = +x (east), counter-clockwise, 90 = north = -z")

# ---- the 70.2 s journey (Sabertooth): time, x, y, z. The camera looks along its path.
_APPROACH = [(0.0, -640, -400, 118), (2.8, -615, -386, 140), (10.0, -500, -322, 150), (17.0, -340, -236, 141), (25.25, -170, -100, 143), (29.0, -118, -34, 141.5)]
_QUAY_KEYS = [(31.0, -92, 140.0), (35.75, -30, 137.0), (41.75, 40, 133.5), (43.6, 70, 132.6)]      # time, x, z: y follows the street's centreline
# after 47 s. Iteration 4: the engine's tail (in the Veil to 57 s, then straight at the Condensery: stacks dead centre on
# the moon for 11 s). Iteration 6: the path bends a little to the right INSIDE the Veil (unseen), leaves the plug on the
# crash at 56.88 s already heading about 5 degrees north of east, crests there and then sinks about 3 degrees toward the
# ocean break, easing a few degrees to the left over the last seconds so the works holds its place: the Condensery stands
# on the left third against the moon's limb, the break opens below right, and the horizon sits above the middle of the
# frame. The camera still only looks where it travels.
# Iteration 9 (owner msg 103): the dive stays low. The Veil is now a low flat bank (crown 205 m) the camera descends
# into, not a tower it climbs through: 50.5 s at 158 (was 175), 54.4 s at 186 (214), out at 56.88 s at 206 (229), the
# reveal a little lower (226, was 232.5).
_LEAVE = [(45.0, 95, -17.0, 133.5), (46.2, 125, -26, 137.5), (47.0, 150, -30, 139.5), (50.5, 290, -5, 158), (54.4, 470, 30, 186),
          (56.88, 578, 44, 206), (58.95, 653, 51, 226), (66.5, 819, 66, 220), (70.2, 849, 69.5, 219)]


def _quay_path():
    out = []
    for x in (-92, -76, -60, -45, -30, -15, 0, 15, 28, 40, 55, 70):
        i = max(k for k in range(len(_QUAY_KEYS) - 1) if _QUAY_KEYS[k][1] <= x)
        (t0, x0, z0), (t1, x1, z1) = _QUAY_KEYS[i], _QUAY_KEYS[i + 1]; u = (x - x0) / (x1 - x0)
        out.append((round(t0 + (t1 - t0) * u, 2), x, round(qy(x) + 0.3, 2) if x > -90 else -2.0, round(z0 + (z1 - z0) * u, 2)))
    return out


CAMERA = _APPROACH + _quay_path() + _LEAVE


def cam_at(t):
    """Position on the journey at time t: Catmull-Rom through the waypoints (uniform in the waypoint index)."""
    P = CAMERA
    if t <= P[0][0]:
        return P[0][1:]
    if t >= P[-1][0]:
        return P[-1][1:]
    i = max(k for k in range(len(P) - 1) if P[k][0] <= t)
    u = (t - P[i][0]) / (P[i + 1][0] - P[i][0])
    p0, p1, p2, p3 = P[max(i - 1, 0)], P[i], P[i + 1], P[min(i + 2, len(P) - 1)]
    out = []
    for c in (1, 2, 3):
        a, b, c_, d = p0[c], p1[c], p2[c], p3[c]
        out.append(0.5 * ((2 * b) + (-a + c_) * u + (2 * a - 5 * b + 4 * c_ - d) * u * u + (-a + 3 * b - 3 * c_ + d) * u ** 3))
    return tuple(out)


# ---- the journey as the engine flies it (build.py journey()): a centripetal Catmull-Rom through the waypoints, time mapped
# to arc length by a monotone cubic. cam_at() above is the art's older approximation (uniform Catmull-Rom in time): the
# two agree at the waypoints and differ by up to 10 m along the path between them, which is 0.2 s at the Veil's speed.
def _cr(p0, p1, p2, p3, t, alpha=0.5):
    def tj(ti, a, b):
        return ti + max(math.dist(a, b), 1e-6) ** alpha
    t0 = 0.0; t1 = tj(t0, p0, p1); t2 = tj(t1, p1, p2); t3 = tj(t2, p2, p3); tt = t1 + (t2 - t1) * t

    def lerp(a, b, ta, tb):
        w = (tt - ta) / (tb - ta)
        return [a[i] + (b[i] - a[i]) * w for i in range(3)]
    a1 = lerp(p0, p1, t0, t1); a2 = lerp(p1, p2, t1, t2); a3 = lerp(p2, p3, t2, t3)
    return lerp(lerp(a1, a2, t0, t2), lerp(a2, a3, t1, t3), t1, t2)


class Journey:
    """Plan coordinates. at(t): position; s_at(t): metres flown; at_s(s): position at a distance; fwd(t): unit tangent."""

    def __init__(self, waypoints=None):
        wp = waypoints or CAMERA
        pts = [list(p[1:]) for p in wp]
        ext = [[2 * pts[0][i] - pts[1][i] for i in range(3)]] + pts + [[2 * pts[-1][i] - pts[-2][i] for i in range(3)]]
        dense, idx = [], []
        for k in range(len(pts) - 1):
            n = max(4, int(math.dist(pts[k], pts[k + 1]) / 4.0)); idx.append(len(dense))
            dense += [_cr(ext[k], ext[k + 1], ext[k + 2], ext[k + 3], j / n) for j in range(n)]
        idx.append(len(dense)); dense.append(pts[-1])
        cum = [0.0]
        for k in range(1, len(dense)):
            cum.append(cum[-1] + math.dist(dense[k - 1], dense[k]))
        ts = [p[0] for p in wp]; ss = [cum[i] for i in idx]; n = len(ts)
        d = [(ss[i + 1] - ss[i]) / (ts[i + 1] - ts[i]) for i in range(n - 1)]
        m = [d[0]] + [(d[i - 1] + d[i]) / 2 if d[i - 1] * d[i] > 0 else 0.0 for i in range(1, n - 1)] + [d[-1]]
        for i in range(n - 1):
            if d[i] == 0:
                m[i] = m[i + 1] = 0; continue
            a, b = m[i] / d[i], m[i + 1] / d[i]; h = a * a + b * b
            if h > 9:
                tau = 3 / math.sqrt(h); m[i], m[i + 1] = tau * a * d[i], tau * b * d[i]
        self.dense, self.cum, self.total, self.ts, self.ss, self.m = dense, cum, cum[-1], ts, ss, m

    def s_at(self, t):
        ts, ss, m = self.ts, self.ss, self.m; t = min(max(t, ts[0]), ts[-1])
        i = max(k for k in range(len(ts) - 1) if ts[k] <= t)
        hh = ts[i + 1] - ts[i]; u = (t - ts[i]) / hh
        return (2 * u ** 3 - 3 * u ** 2 + 1) * ss[i] + (u ** 3 - 2 * u ** 2 + u) * hh * m[i] + (-2 * u ** 3 + 3 * u ** 2) * ss[i + 1] + (u ** 3 - u ** 2) * hh * m[i + 1]

    def t_at(self, s):
        lo, hi = self.ts[0], self.ts[-1]
        for _ in range(40):
            mid = 0.5 * (lo + hi)
            lo, hi = (mid, hi) if self.s_at(mid) < s else (lo, mid)
        return 0.5 * (lo + hi)

    def at_s(self, s):
        cum, dense = self.cum, self.dense; s = min(max(s, 0.0), self.total); lo, hi = 0, len(cum) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            lo, hi = (mid, hi) if cum[mid] <= s else (lo, mid)
        u = (s - cum[lo]) / max(cum[hi] - cum[lo], 1e-9)
        return tuple(dense[lo][j] + (dense[hi][j] - dense[lo][j]) * u for j in range(3))

    def at(self, t):
        return self.at_s(self.s_at(t))

    def fwd_s(self, s, eps=1.0):
        a, b = self.at_s(s - eps), self.at_s(s + eps); d = [b[i] - a[i] for i in range(3)]; n = math.sqrt(sum(v * v for v in d)) or 1.0
        return tuple(v / n for v in d)

    def fwd(self, t):
        return self.fwd_s(self.s_at(t))


def town_top(x, y):
    """The height the cloud's lobes reach under the Mooring: about 5 m under every deck (the eroded surface sits a few
    metres lower), so the 14 m stilts vanish into it and the town emerges from the cloud instead of standing over it."""
    dy = y - qy(x)
    t = 123.6 - 0.0474 * max(-110.0, min(125.0, x))
    t += min(11.0, 0.115 * dy) if dy > 0 else max(-13.0, 0.36 * dy)
    return t


# ---- iteration 9 (owner msg 73: "it doesn't feel colossal yet ... build out from here, add more floating cloud
# structures, make them bob up and down (floating in the ocean)"). The Mooring grows three districts and the sea around it
# carries formations that float. Plan coordinates, as above.
# The Lampmarket: a market court west of the Great Lantern, one floor above the plaza, open to the south-west (the
# approach looks into it from 2.8 to 12 s, left of the Lantern).
LAMPMARKET = dict(centre=(-262.0, 82.0), deck=134.4, court=(46.0, 34.0))
# Lanternhill: the town climbing the Head's south flank behind the Stacks, four terraces two floors apart.
LANTERNHILL = dict(rows=[(147.2, 116.0), (153.6, 142.0), (160.0, 168.0), (166.4, 194.0)], x=(-100.0, 112.0), yaw_deg=-18.0)
MOORING_MAST = (12.0, 224.0, 166.4)        # the observation mast at the hill's crest
# Eastreach: garden roofs on two terraces east of the Stacks, north of the Veil (left of the journey at 41 to 47 s).
EASTREACH = dict(rows=[(137.6, 120.0, (150.0, 262.0)), (144.0, 146.0, (160.0, 276.0)), (150.4, 172.0, (176.0, 270.0))], yaw_deg=-8.0)
# owner msg 83: small floating islands in the gaps round the town. Msg 92: west2, inner2, south and east moved in toward the
# journey so they pass through the foreground (55-70 m from the lens) instead of sitting at 140-200 m or behind the town. (bob, rock variant, x, y, top z, width, yaw, content)
SMALL_ISLES = [("isle_west", "a", -215.0, 5.0, 127.0, 50.0, 20.0, "grove"), ("isle_west2", "c", -262.0, -262.0, 120.0, 26.0, -30.0, "house:mooring_shophouse_j"),
               ("isle_inner", "b", -72.0, -72.0, 117.0, 36.0, 60.0, "tea"), ("isle_inner2", "c", -108.0, -126.0, 118.0, 22.0, 10.0, "tree"),
               ("isle_south", "b", 15.0, -72.0, 112.0, 58.0, -15.0, "houses"), ("isle_south2", "a", 95.0, -108.0, 124.0, 36.0, 35.0, "tower:tower_hanging"),
               ("isle_east", "a", 158.0, 40.0, 156.0, 32.0, 0.0, "tower:tower_spire"), ("isle_exit", "c", 178.0, -95.0, 134.0, 30.0, -20.0, "tower:tower_mast")]
# Floating formations (they bob: cloud_design.json "bobs"). (name, plan x, y, deck z or top, foot radius)
ISLE_LAMPWICK = (235.0, -165.0, 140.0, 62.0)     # (owner msg 83: moved in toward the town, was (300, -255)) a hamlet on its own cloud, reached by gondola from the garden; right of the approach, right of the Quay's exit at 41-46 s
ISLE_WATCH = (640.0, -350.0, 172.0, 44.0)        # one tall house on a tall cloud stack
KEEP = (560.0, 2080.0, 1060.0, 430.0)            # the colossus on the left of the approach: a massif 1.1 km high with a town on a shelf at KEEP_SHELF
KEEP_SHELF = (430.0, 1890.0, 392.0)
DROWNED_MAST = (7330.0, 5690.0, -60.0)           # the enormous silhouette: 9.3 km out, behind the town from the approach (2.8 to 35 s), never in the reveal
# What floats, and how (owner msg 73: "make them bob up and down (floating in the ocean)"; msg 78: what floats stands on a
# rock island). The engine's ADR-2112 shape: amplitude [x, y, z] m (y bobs at `period`, x and z drift at 1.618 x it),
# period s, phase 0..1. Calm buoyancy, not a pulse: bigger and farther = slower and larger. The Mooring, its districts, the
# Head, the Veil and the sill do not bob. liftMax 0: no audio on the bob (yet). Cloud masses of the same name carry it.
BOBS = {
    "isle_lampwick": dict(amplitude=[1.0, 3.5, 0.8], period=13.0, phase=0.10, note="the hamlet south-east of the garden; seen 2.8-12 s and 41-46 s"),
    "isle_watch":    dict(amplitude=[0.8, 3.0, 0.8], period=15.5, phase=0.55, note="one tall house on a deep rock"),
    "lone_lamp":     dict(amplitude=[0.8, 2.8, 0.6], period=12.0, phase=0.30, note="one house on its own small rock"),
    "harbour_light": dict(amplitude=[0.4, 1.6, 0.4], period=17.0, phase=0.80, note="its beams pass over the journey at 5-14 s: keep it small"),
    "outer_light":   dict(amplitude=[0.4, 1.8, 0.4], period=16.0, phase=0.20, note=""),
    "break_light":   dict(amplitude=[0.4, 1.8, 0.4], period=18.5, phase=0.65, note="on the rim of the ocean break, in the reveal"),
    "condensery":    dict(amplitude=[0.5, 2.2, 0.5], period=37.0, phase=0.40, note="the reveal's subject: slow and small; its plume emitters and glows ride it"),
    "highwater":     dict(amplitude=[2.0, 6.0, 2.0], period=27.0, phase=0.15, note="2.2 km out in the reveal"),
    "keep":          dict(amplitude=[3.0, 12.0, 3.0], period=34.0, phase=0.70, note="the colossus 2.7 km out on the left of the approach, its town on a rock in its shelf"),
    "reef":          dict(amplitude=[2.0, 8.0, 2.0], period=29.0, phase=0.35, note="floating cumulus north-east and south-east of the Mooring"),
    # owner msg 83: the gaps round the town filled with small floating islands (gardens, houses, towers), at varied heights
    "isle_west":     dict(amplitude=[0.6, 2.4, 0.6], period=11.5, phase=0.05, note="the garden isle west of the Great Lantern"),
    "isle_west2":    dict(amplitude=[0.5, 1.9, 0.5], period=9.5, phase=0.62, note="a one-house rock beside it"),
    "isle_inner":    dict(amplitude=[0.5, 2.0, 0.5], period=12.5, phase=0.33, note="the tea garden inside the journey's curve, under the lens at 26-29 s"),
    "isle_inner2":   dict(amplitude=[0.4, 1.6, 0.4], period=10.0, phase=0.88, note="one umbel tree on a small rock"),
    "isle_south":    dict(amplitude=[0.7, 2.6, 0.7], period=14.0, phase=0.47, note="two houses and a garden south of the Low Walk"),
    "isle_south2":   dict(amplitude=[0.6, 2.2, 0.6], period=13.0, phase=0.15, note="the hanging-garden tower south of the Steps"),
    "isle_east":     dict(amplitude=[0.6, 2.4, 0.6], period=15.0, phase=0.72, note="the lantern spire between the Stacks and Eastreach"),
    "isle_exit":     dict(amplitude=[0.6, 2.4, 0.6], period=12.0, phase=0.25, note="the mast tower right of the journey as it leaves the Quay (45 s)"),
}

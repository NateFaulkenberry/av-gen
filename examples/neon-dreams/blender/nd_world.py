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
CONDENSERY, CONDENSERY_YAW = (2050.0, 450.0, 250.0), 0.3
LONE_LAMP = (900.0, -300.0, 96.0)
BREAK = dict(centre=(1640.0, 60.0), radius=270.0, elongation=2.0, yaw=0.05)
# lighthouses on cloud-rock outcrops: (name, asset, x, y, deck z, yaw deg, beam rpm, phase, beam length)
LIGHTHOUSES = [("harbour_light", "lighthouse_a", -374.0, -339.0, 128.0, 20.0, 5.0, 0.0, 900.0),
               ("outer_light", "lighthouse_b", 700.0, -250.0, 118.0, -35.0, 3.75, 0.37, 1100.0),
               ("break_light", "lighthouse_c", 1150.0, -40.0, 66.0, 60.0, 4.3, 0.71, 1000.0)]
MOON = dict(light_from_bearing_deg=34.0, light_elevation_deg=26.0, disc_bearing_deg=19.0, disc_centre_elevation_deg=2.2, disc_angular_radius_deg=8.2)

# ---- the 70.2 s journey (Sabertooth): time, x, y, z. The camera looks along its path.
_APPROACH = [(0.0, -640, -400, 118), (2.8, -615, -386, 140), (10.0, -500, -322, 150), (17.0, -340, -236, 141), (25.25, -170, -100, 143), (29.0, -118, -34, 141.5)]
_QUAY_KEYS = [(31.0, -92, 140.0), (35.75, -30, 137.0), (41.75, 40, 133.5), (43.6, 70, 132.6)]      # time, x, z: y follows the street's centreline
_LEAVE = [(45.0, 95, -17.0, 133.5), (46.2, 125, -26, 137.5), (47.0, 150, -30, 139.5), (50.5, 250, -18, 170), (54.4, 360, 10, 235), (58.95, 470, 48, 285), (70.2, 640, 84, 315)]


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


def town_top(x, y):
    """The height the cloud's lobes reach under the Mooring: about 5 m under every deck (the eroded surface sits a few
    metres lower), so the 14 m stilts vanish into it and the town emerges from the cloud instead of standing over it."""
    dy = y - qy(x)
    t = 123.6 - 0.0474 * max(-110.0, min(125.0, x))
    t += min(11.0, 0.115 * dy) if dy > 0 else max(-13.0, 0.36 * dy)
    return t

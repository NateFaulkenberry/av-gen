"""DIGITAL MOSH: the flight -- one closed path over the land that the camera soars along (camera mode 2).

Imported by build.py. Three petals, each leaving and returning over the salt pan, so the stone and its contagion
come back into view every half minute or so whatever the music's tempo:

  west   -- down onto the riverbed and upstream along it, banking with its bends, then back over the knoll, gliding
            past the tree;
  east   -- low over the dune crests past the east mesa, round through the southern dunes, over a crest and home;
  north  -- climbing over the escarpment's wall to reveal the land and mesas beyond, then descending past the mesa.

Points are (x, metres above the land, z); heights come from the engine (land.py). `check()` samples the curve the
engine will draw (a closed Catmull-Rom) every 2 m and reports where it comes within `CLEAR` metres of the land.
"""
from __future__ import annotations

import math

import land

CLEAR = 3.0

PETALS = {
    "west": [(-14, 8, -14), (-32, 6, -12), (-50, 5, 8), (-64, 5, 36), (-88, 6, 66), (-125, 9, 100), (-136, 18, 62),
             (-96, 20, 32), (-56, 15, 20), (-22, 12, 6), (0, 9, -4)],
    "east": [(22, 8, -10), (62, 9, -18), (110, 11, 4), (156, 14, 40), (150, 12, 92), (100, 10, 104), (52, 9, 72),
             (22, 8, 32), (6, 9, 6)],
    "north": [(0, 11, -30), (-10, 24, -90), (-22, 44, -150), (-20, 56, -212), (40, 60, -258), (130, 54, -232),
              (192, 40, -146), (152, 26, -78), (82, 17, -50), (30, 12, -26)],
}
ORDER = ["west", "east", "north"]


def points():
    """The closed path's control points in world space."""
    out = []
    for petal in ORDER:
        for x, alt, z in PETALS[petal]:
            out.append([float(x), round(land.height(x, z) + alt, 2), float(z)])
    return out


def _cr(p0, p1, p2, p3, t):
    t2, t3 = t * t, t * t * t
    return [0.5 * ((2 * p1[i]) + (-p0[i] + p2[i]) * t + (2 * p0[i] - 5 * p1[i] + 4 * p2[i] - p3[i]) * t2
                   + (-p0[i] + 3 * p1[i] - 3 * p2[i] + p3[i]) * t3) for i in range(3)]


def samples(step=2.0):
    pts = points()
    n = len(pts)
    out = []
    for i in range(n):
        p0, p1, p2, p3 = pts[(i - 1) % n], pts[i], pts[(i + 1) % n], pts[(i + 2) % n]
        seg = math.dist(p1, p2)
        k = max(2, int(seg / step))
        out += [_cr(p0, p1, p2, p3, j / k) for j in range(k)]
    return out


def length():
    s = samples()
    return sum(math.dist(s[i], s[(i + 1) % len(s)]) for i in range(len(s)))


def check():
    s = samples()
    hs = land.heights([(p[0], p[2]) for p in s])
    problems = []
    for p, h in zip(s, hs):
        if p[1] < h + CLEAR:
            problems.append(f"flight at {[round(v, 1) for v in p]} is {p[1] - h:.1f} m above the land")
    return problems


def spline_node():
    return {"name": "flight", "kind": "spline", "spline": {
        "name": "flight", "kind": "catmullRom", "closed": True, "tension": 0.5, "generator": "points",
        "samplesPerSegment": 24, "up": [0, 1, 0],
        "points": [{"position": p, "roll": 0.0, "scale": 1.0} for p in points()]}}


if __name__ == "__main__":
    print("length", round(length()), "m;", len(points()), "points")
    for line in check()[:40]:
        print(line)

#!/usr/bin/env python3
"""Generates examples/liminal/all-you-got{,.scene,.rig}.json -- the directed music video for the owner's
song *All You Got* (docs/prototypes/liminal-space/DIRECTOR-PLAN.md), on the engineer's Liminal
infrastructure (ADR-1040 to 1044).

The film is one continuous walk through a strange, ordinary building. It is built here as data:

  * worlds: seven SDF objects drawn with the vocabulary of tools/liminal_sdf.py -- a corridor whose storeys
    repeat forever (its stair arrives back at its own beginning), a great hall, an enfilade of rooms, a
    stairhead over a void, a room that grows, a Penrose stairwell, and the open;
  * the journey (ADR-1042): nine chapters, swapped inside thresholds of light;
  * the walk: a speed profile per section (holds, hesitations, the step up in tempo), integrated here into
    dense distance keys, so the music never moves the camera and silence holds it still;
  * the colour script K0-K12 (tools/liminal/all-you-got.sections.json, sRGB -> linear) as the project's
    palette (ADR-1043), keyed on the section boundaries; the sky withheld until the doorway at bar 70;
  * sparse, congruent reactivity: the bass makes the world breathe (a spring into a slow warp), the voice
    warms the beacon, phrase-end noise makes near walls tremble; a timeline 'coupling' source scales every
    route by the section's intent, so the silent bars are silent.

The song is referenced as ~/Desktop/All You Got.wav and never copied. Run from the repository root:

    python3 tools/liminal/make_all_you_got.py [--end SECONDS] [--check]
"""

from __future__ import annotations

import argparse
import bisect
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from liminal_sdf import box, breathing, landing, screw, sdf_node, slab, stairway, surface, translate, tremble  # noqa: E402
from liminal_sdf import difference as _difference  # noqa: E402
from liminal_sdf import union as _union  # noqa: E402

OUT = os.path.join(ROOT, "examples", "liminal")
SECTIONS = os.path.join(HERE, "all-you-got.sections.json")


def union(*children, name=None):
    """A union of any number of children, nested in groups of at most 8 (the node's limit)."""
    kids = [c for c in children if c is not None]
    while len(kids) > 8:
        kids = [_union(*kids[i:i + 8]) for i in range(0, len(kids), 8)]
    return _union(*kids, name=name)


def difference(solid, *cuts, name=None):
    """A solid minus any number of cuts (the cuts are unioned first when there are more than seven)."""
    cuts = [c for c in cuts if c is not None]
    if len(cuts) > 7:
        cuts = [union(*cuts)]
    return _difference(solid, *cuts, name=name)


def shellbox(ext, wall, k=None):
    """A hollow box, walls `wall` thick inside the given OUTER extents: six walls in three nodes."""
    (x0, x1), (y0, y1), (z0, z1) = ext
    c = [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2]
    h = [(x1 - x0) / 2 - wall / 2, (y1 - y0) / 2 - wall / 2, (z1 - z0) / 2 - wall / 2]
    node = translate(c, {"kind": "shell", "offset": wall, "children": [box(h)]})
    return surface(node, k) if k is not None else node


def moved(node, name):
    """A named translate (0 at rest) around a node, so the timeline can move it: `node/<name>/translation`."""
    return translate([0.0, 0.0, 0.0], node, name=name)


def count_nodes(tree):
    """(nodes, deepest nesting of point ops): the SDF tree limits are 96 nodes and 8 nested point ops."""
    unary = {"translate", "rotate", "repeat", "screw", "warp", "shell", "scale", "mirror", "fold", "twist",
             "bend", "polarRepeat", "recurse"}

    def walk(n, depth):
        d = depth + (1 if n["kind"] in unary else 0)
        total, worst = 1, d
        for c in n.get("children", []):
            t, w = walk(c, d)
            total += t
            worst = max(worst, w)
        return total, worst
    return walk(tree, 0)


# ---- the song's grid (SONG-ANALYSIS.md) ------------------------------------------------------------
BAR1 = 4 * 60.0 / 109.0   # bars 1-75
BAR2 = 4 * 60.0 / 111.0   # bars 76-116
SONG_END = 253.79


def bt(bar: float, beat: float = 1.0) -> float:
    """Seconds at bar.beat (1-based), on the two-tempo grid."""
    b = (bar - 1) + (beat - 1) / 4.0
    if b <= 75.0:
        return b * BAR1
    return 75.0 * BAR1 + (b - 75.0) * BAR2


# ---- surfaces: one convention for every world (ADR-1044) ------------------------------------------
PLASTER, FLOOR, ACCENT, LAMP, BEACON, SUN, SKY, DARK = range(8)


def surfaces_default():
    return [{"color": [0.6, 0.6, 0.6]}, {"color": [0.4, 0.4, 0.4]}, {"color": [0.6, 0.3, 0.35]},
            {"color": [0.0, 0.0, 0.0], "emission": [1.0, 1.0, 1.0]},
            {"color": [0.0, 0.0, 0.0], "emission": [1.0, 1.0, 1.0]},
            {"color": [0.5, 0.45, 0.35], "emission": [0.0, 0.0, 0.0]},
            {"color": [0.0, 0.0, 0.0], "emission": [1.0, 1.0, 1.0]},
            {"color": [0.25, 0.25, 0.26]}]


# ---- colour -----------------------------------------------------------------------------------------

def srgb_to_linear(c: float) -> float:
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hexlin(h: str, gain: float = 1.0) -> list:
    h = h.lstrip("#")
    return [round(srgb_to_linear(int(h[i:i + 2], 16) / 255.0) * gain, 5) for i in (0, 2, 4)]


def mul(c, k):
    return [round(v * k, 5) for v in c]


def mix(a, b, t):
    return [round(x + (y - x) * t, 5) for x, y in zip(a, b)]


# ---- the walk: a speed profile, integrated ----------------------------------------------------------

class Walk:
    """Camera speed as a function of time: (time, speed) keys joined by cosine eases (every change of pace
    eases in and out), integrated into distance. Negative speed walks back (a hesitation). Keys may carry a
    group; `fit` scales a group's speeds so the walk arrives at a distance on time."""

    def __init__(self):
        self.keys: list[list] = [[0.0, 0.0, None]]
        self._times = [0.0]

    def at(self, t: float, v: float, group=None):
        self.keys.append([t, v, group])
        return self

    def speed(self, t: float) -> float:
        ks = self.keys
        if t <= ks[0][0]:
            return ks[0][1]
        if t >= ks[-1][0]:
            return ks[-1][1]
        i = bisect.bisect_right(self._times, t) - 1
        t0, v0 = ks[i][0], ks[i][1]
        t1, v1 = ks[i + 1][0], ks[i + 1][1]
        if t1 - t0 <= 1e-9:
            return v1
        u = (t - t0) / (t1 - t0)
        w = 0.5 - 0.5 * math.cos(math.pi * u)
        return v0 + (v1 - v0) * w

    def integrate(self, end: float, dt: float = 1.0 / 120.0):
        self.keys.sort(key=lambda k: k[0])
        self._times = [k[0] for k in self.keys]
        ts, ds = [0.0], [0.0]
        t, d = 0.0, 0.0
        while t < end - 1e-9:
            h = min(dt, end - t)
            d += self.speed(t + h / 2) * h
            t += h
            ts.append(t)
            ds.append(d)
        self.ts, self.ds = ts, ds
        return self

    def distance(self, t: float) -> float:
        ts, ds = self.ts, self.ds
        if t <= 0:
            return 0.0
        if t >= ts[-1]:
            return ds[-1]
        i = bisect.bisect_right(ts, t) - 1
        i = max(0, min(i, len(ts) - 2))
        u = (t - ts[i]) / (ts[i + 1] - ts[i])
        return ds[i] + (ds[i + 1] - ds[i]) * u

    def fit(self, group, t0: float, t1: float, delta: float):
        """Scale `group`'s speeds so the walk covers `delta` metres between t0 and t1 (bisection)."""
        idx = [i for i, k in enumerate(self.keys) if k[2] == group]
        base = {i: self.keys[i][1] for i in idx}
        lo, hi = 0.0, 6.0
        for _ in range(48):
            m = (lo + hi) / 2
            for i in idx:
                self.keys[i][1] = base[i] * m
            self.integrate(t1 + 0.01)
            if self.distance(t1) - self.distance(t0) < delta:
                lo = m
            else:
                hi = m
        m = (lo + hi) / 2
        for i in idx:
            self.keys[i][1] = base[i] * m
        return m

    def keys_dense(self, end: float, step: float = 0.2):
        out = []
        n = int(math.ceil(end / step))
        for i in range(n + 1):
            t = min(i * step, end)
            out.append((round(t, 4), round(self.distance(t), 4)))
        return out


# ---- centripetal Catmull-Rom (the journey's own spline, ADR-1042), for path lengths and points -------

def _knot(a, b):
    return max(math.dist(a, b) ** 0.5, 1e-4)


class Path:
    def __init__(self, points, samples=48):
        self.points = points
        n = len(points)
        far = [points[-1][0] + 900.0, points[-1][1], points[-1][2]]

        def Q(i):
            if i < 0:
                return [2 * points[0][j] - points[1][j] for j in range(3)]
            if i >= n:
                return far if i == n else [far[0] + 900.0, far[1], far[2]]
            return points[i]

        def seg_point(p0, p1, p2, p3, u):
            t0 = 0.0
            t1 = t0 + _knot(p0, p1)
            t2 = t1 + _knot(p1, p2)
            t3 = t2 + _knot(p2, p3)
            t = t1 + (t2 - t1) * u

            def lerp(a, b, ta, tb):
                return [(tb - t) / (tb - ta) * x + (t - ta) / (tb - ta) * y for x, y in zip(a, b)]
            a1 = lerp(p0, p1, t0, t1)
            a2 = lerp(p1, p2, t1, t2)
            a3 = lerp(p2, p3, t2, t3)
            b1 = lerp(a1, a2, t0, t2)
            b2 = lerp(a2, a3, t1, t3)
            return lerp(b1, b2, t1, t2)

        self.cum = [0.0]
        self.pts = [list(points[0])]
        self.at_point = [0.0]
        prev = points[0]
        for i in range(n - 1):
            for k in range(1, samples + 1):
                p = seg_point(Q(i - 1), Q(i), Q(i + 1), Q(i + 2), k / samples)
                self.cum.append(self.cum[-1] + math.dist(prev, p))
                self.pts.append(p)
                prev = p
            self.at_point.append(self.cum[-1])
        self.length = self.cum[-1]

    def point(self, s):
        s = max(0.0, min(s, self.length))
        i = bisect.bisect_left(self.cum, s)
        i = max(1, min(i, len(self.cum) - 1))
        u = (s - self.cum[i - 1]) / max(self.cum[i] - self.cum[i - 1], 1e-9)
        return [a + (b - a) * u for a, b in zip(self.pts[i - 1], self.pts[i])]


# ======================================================================================================
# WORLD A: the first corridor. A plain, too-tall corridor (4.45 m) with tall windows full of fog on the
# left, doorways on the right, and far away at its end a doorway with warm light in it (the beacon). A side
# doorway opens onto a stair that climbs back over the corridor and arrives, one storey up, at the
# corridor's beginning: the storeys repeat forever (a vertical screw), so the frame the camera arrives in
# IS the film's opening frame. The seams run through the middle of each floor slab, where nothing is open.
# The loop (bars 76-83) walks the same corridor with its far wall receding and its ceiling lights on.
# ======================================================================================================
A_H = 4.75                 # storey height (floor to floor)
A_CEIL = 4.45              # interior height
A_HALF = 1.6               # half width (interior 3.2 m)
A_WALL = 0.3
A_X0, A_XF = -12.0, 150.0  # the start wall; how far the long walls run
A_X1 = 66.0                # the beacon wall, at rest
A_WIN_P = 3.0              # window spacing
A_STAIR_DOOR = 4.6         # the side doorway to the stair (x)
A_ARRIVE_DOOR = -4.2       # the doorway the stair arrives at, one storey up (x)
A_SLOT = (2.0, 3.9)        # the stair slot's interior z range
A_STEPS, A_RUN = 25, 0.25
A_RISE = A_H / A_STEPS
A_FOOT = 3.6               # x of the first riser (the flight climbs -X)
A_TOP = A_FOOT - A_STEPS * A_RUN   # x where the flight arrives (-2.65)
A_SLOT_X = (-6.6, 6.2)     # the slot's interior x range
A_SIDE_Z = 9.4             # the side hall behind the right wall beyond the slot reaches this z
A_SUN_X = 1.4              # the sun patch on every storey's floor
A_OPEN_X = A_ARRIVE_DOOR + 1.9


def world_corridor():
    Y0, Y1 = -1.0, A_H + 1.0   # walls run past the cell; the screw clips them
    z0, z1 = -A_HALF, A_HALF
    zs0, zs1 = A_SLOT
    xs0, xs1 = A_SLOT_X
    zr = A_SIDE_Z
    xa, xb = A_X0 - A_WALL, A_XF
    wins = translate([A_WIN_P / 2, 0.0, 0.0], {"kind": "repeat", "size": [A_WIN_P, 0.0, 0.0], "count": 0,
                                                "children": [slab(((-0.55, 0.55), (0.85, 3.95), (z0 - 1.2, z0 + 0.9)))]})
    doors = translate([A_STAIR_DOOR, 0.0, 0.0], {"kind": "repeat", "size": [A_STAIR_DOOR - A_ARRIVE_DOOR, 0.0, 0.0],
                                                  "count": 0,
                                                  "children": [slab(((-0.7, 0.7), (0.0, 3.05), (z1 - 0.9, z1 + 1.2)))]})
    walls = difference(union(moved(slab(((xa, xb), (Y0, Y1), (z0 - A_WALL, z0))), "wallL"),
                             moved(slab(((xa, xb), (Y0, Y1), (z1, z1 + A_WALL))), "wallR")), wins, doors)
    # The floor/ceiling stack over corridor, slot and side hall, with the stair well cut out: what remains
    # in the slot are the two landings. The ceiling is named so the loop can lower it.
    well = slab(((A_TOP, A_FOOT), (Y0, Y1), (zs0, zs1)))
    stacks = difference(union(moved(slab(((xa, xb), (A_CEIL, A_H), (z0 - A_WALL, zr + A_WALL))), "ceiling"),
                              slab(((xa, xb), (-0.3, 0.0), (z0 - A_WALL, zr + A_WALL))),
                              surface(slab(((A_X0, xb), (-0.02, 0.0), (z0, zr))), FLOOR)), well)
    # The start wall, and the beacon wall with its small glowing room (named: it recedes in the loop).
    bdoor = slab(((A_X1 - 1.0, A_X1 + 1.3), (0.0, 3.0), (-0.7, 0.7)))
    broom = surface(translate([A_X1 + 2.0, 1.5, 0.0], {"kind": "shell", "offset": 0.3,
                                                        "children": [box([1.85, 1.85, 1.85])]}), BEACON)
    end = moved(difference(union(slab(((A_X1, A_X1 + A_WALL), (Y0, Y1), (z0 - A_WALL, zr + A_WALL))), broom), bdoor),
                "beaconWall")
    start = slab(((xa, A_X0), (Y0, Y1), (z0 - A_WALL, zr + A_WALL)))
    # The stair slot behind the right wall and the side hall beyond it: outer walls and their ends.
    backs = union(slab(((xs0 - A_WALL, xs1 + A_WALL), (Y0, Y1), (zs1, zs1 + A_WALL))),
                  slab(((xs0 - A_WALL, xs0), (Y0, Y1), (z1, zs1 + A_WALL))),
                  slab(((xs1, xs1 + A_WALL), (Y0, Y1), (z1, zr + A_WALL))),
                  slab(((xs1, xb), (Y0, Y1), (zr, zr + A_WALL))))
    zc = (zs0 + zs1) / 2
    width = zs1 - zs0
    flights = union(*[stairway([A_FOOT, k * A_H, zc], A_STEPS, run=A_RUN, rise=A_RISE, width=width,
                               direction="-x", thickness=0.28) for k in (0, -1)])
    sun = surface(slab(((A_SUN_X - 0.8, A_SUN_X + 0.8), (-0.019, 0.001), (-1.45, 0.35))), SUN)
    lamps = surface(translate([1.08, A_CEIL - 0.004, 0.0],
                              {"kind": "repeat", "size": [2.16, 0.0, 0.0], "count": 0,
                               "children": [box([0.42, 0.012, 0.32])]}), LAMP)
    lamps = moved(lamps, "lampsAt")
    cell = union(walls, stacks, end, start, backs, flights, sun, lamps)
    # horizontal only, so floors stay floors; any disagreement at the seams falls inside the floor slabs
    cell = breathing(cell, amount=0.0, frequency=0.06, axes=(1.0, 0.0, 1.0), name="breath")
    mid = (A_H - 0.3) / 2.0
    tree = translate([0.0, mid, 0.0], screw(translate([0.0, -mid, 0.0], cell), [0.0, A_H, 0.0], seam=0.2,
                                            name="storeys"))
    return tremble(tree, amount=0.0, frequency=3.0, radius=3.5, fade=2.0)


def _a_loop(y):
    zc = (A_SLOT[0] + A_SLOT[1]) / 2
    return [
        [A_STAIR_DOOR - 1.6, y, 0.15],
        [A_STAIR_DOOR - 0.25, y, 0.95],   # turning right, into the doorway
        [A_STAIR_DOOR, y, 2.3],
        [A_FOOT + 0.45, y, zc],           # the foot of the flight, turning to climb
        [A_FOOT - 0.4, y + 0.12, zc],
        [(A_FOOT + A_TOP) / 2, y + A_H / 2, zc],
        [A_TOP + 0.35, y + A_H - 0.08, zc],
        [A_TOP - 1.0, y + A_H, zc - 0.1],   # the top landing
        [A_ARRIVE_DOOR + 0.35, y + A_H, 2.15],
        [A_ARRIVE_DOOR, y + A_H, 0.9],      # through the arrival doorway
        [A_OPEN_X, y + A_H, 0.25],          # the same corridor, one storey up: the opening frame
    ]


def path_intro():
    return ([[A_OPEN_X, 0.0, 0.25], [A_OPEN_X + 2.6, 0.0, 0.05]] + _a_loop(0.0) +
            [[A_OPEN_X + 2.6, A_H, 0.05]] + _a_loop(A_H)[:8])


def path_loop():
    return [[-10.0, 0.0, 0.0], [-4.0, 0.0, 0.0], [10.0, 0.0, 0.0], [28.0, 0.0, 0.0], [36.0, 0.0, 0.0]]


# ======================================================================================================
# WORLD B: the great hall (the dance). 22 m long, 22 m wide, 17 m tall, empty. The camera steps out high on
# the -X wall and goes down one long free-standing stair on the axis. High windows on both long walls; the
# -Z wall is a single plane of deep rose. The beacon is a lit doorway high in the far wall, unreachable; a
# plain doorway under it is the way on. Across the hall the figure stands on a platform, looking up at it.
# A stair from nowhere climbs to a doorway in mid-wall.
# ======================================================================================================
B_L, B_W, B_HT = 22.0, 11.0, 17.0
B_STEPS, B_RUN, B_RISE = 32, 0.3, 0.18
B_ENTRY_Y = B_STEPS * B_RISE                  # 5.76
B_TOP_X = 2.5
B_FOOT_X = B_TOP_X + B_STEPS * B_RUN          # 12.1
B_FIG = (16.0, 3.6, 6.5)


def world_hall():
    W = 0.4
    hall = shellbox(((-W, B_L + W), (-W, B_HT + W), (-B_W - W, B_W + W)), W)
    rose = surface(slab(((0.0, B_L), (0.0, B_HT), (-B_W - 0.05, -B_W + 0.01))), ACCENT)
    floor_skin = surface(slab(((0.0, B_L), (-0.05, 0.01), (-B_W, B_W))), FLOOR)
    vest_in = shellbox(((-4.8, 0.0), (B_ENTRY_Y - 0.3, B_ENTRY_Y + 3.6), (-1.1, 1.1)), 0.3, LAMP)
    vest_out = shellbox(((B_L, B_L + 4.8), (-0.3, 3.5), (-1.1, 1.1)), 0.3, LAMP)
    beacon = shellbox(((B_L, B_L + 2.6), (8.8, 12.8), (-1.1, 1.1)), 0.3, BEACON)
    solid = union(hall, rose, floor_skin, vest_in, vest_out, beacon)
    wins = translate([B_L / 2, 0.0, 0.0], {"kind": "repeat", "size": [4.4, 0.0, 0.0], "count": 2, "children": [
        union(slab(((-0.85, 0.85), (9.6, 15.6), (-B_W - 1.5, -B_W + 0.5))),
              slab(((-0.85, 0.85), (9.6, 15.6), (B_W - 0.5, B_W + 1.5))))]})
    cuts = [slab(((-1.6, 1.0), (B_ENTRY_Y, B_ENTRY_Y + 3.3), (-0.8, 0.8))),           # the way in
            slab(((B_L - 1.0, B_L + 1.6), (0.0, 3.2), (-0.75, 0.75))),               # the way on
            slab(((B_L - 1.0, B_L + 1.6), (9.2, 12.4), (-0.7, 0.7))),                # the beacon
            slab(((7.3, 8.7), (B_ENTRY_Y + 4.58, B_ENTRY_Y + 7.8), (B_W - 1.0, B_W + 1.5))),   # a door in mid-wall
            wins]
    building = difference(solid, *cuts)
    entry = union(slab(((0.0, B_TOP_X), (B_ENTRY_Y - 0.4, B_ENTRY_Y), (-1.2, 1.2))),
                  surface(slab(((0.0, B_TOP_X), (B_ENTRY_Y - 0.02, B_ENTRY_Y + 0.004), (-1.2, 1.2))), FLOOR))
    stair = stairway([B_FOOT_X, 0.0, 0.0], B_STEPS, run=B_RUN, rise=B_RISE, width=2.4, direction="-x",
                     thickness=0.38)
    fx, fy, fz = B_FIG
    fig_steps, fig_run = 20, 0.25
    fig = union(landing([fx, fy, fz], 3.0, 3.0, thickness=0.3),
                slab(((fx - 0.45, fx + 0.45), (0.0, fy - 0.3), (fz - 0.45, fz + 0.45))),
                stairway([fx - 1.5 - fig_steps * fig_run, 0.0, fz], fig_steps, run=fig_run, rise=fy / fig_steps,
                         width=1.4, direction="+x", thickness=0.3))
    nw_y = B_ENTRY_Y + 1.52
    nowhere = union(landing([8.0, nw_y, 5.1], 1.6, 1.8, thickness=0.3),
                    stairway([8.0, nw_y, 6.0], 17, run=0.3, rise=0.18, width=1.4, direction="+z", thickness=0.3))
    # Patches of sun on the floor where the high windows' light lands (the sun from +Z, high): the windows at
    # y 9.6-15.6 on the +Z wall project to z -7.6..-0.5 on the floor, 5 m further along x.
    patches = translate([B_L / 2 + 5.1 - 4.4 / 2, 0.0, -4.05], {"kind": "repeat", "size": [4.4, 0.0, 0.0], "count": 1,
                                                                "children": [slab(((-0.85, 0.85), (-0.01, 0.016), (-3.55, 3.55)))]})
    patches = surface(patches, SUN)
    tree = union(building, entry, stair, fig, nowhere)   # (the floor patches read as rugs, not light: cut)
    tree = breathing(tree, amount=0.0, frequency=0.05, axes=(1.0, 0.0, 1.0), name="breath")
    return tremble(tree, amount=0.0, frequency=3.0, radius=3.5, fade=2.0)


def path_hall():
    y = B_ENTRY_Y
    return [[-4.0, y, 0.0], [-1.6, y, 0.0], [0.8, y, 0.0], [B_TOP_X + 0.25, y - 0.05, 0.0],
            [(B_TOP_X + B_FOOT_X) / 2, y / 2, 0.0], [B_FOOT_X - 0.2, 0.06, 0.0], [14.5, 0.0, 0.0],
            [18.5, 0.0, 0.0], [B_L - 1.2, 0.0, 0.0], [B_L + 0.2, 0.0, 0.0], [B_L + 2.6, 0.0, 0.0]]


# ======================================================================================================
# WORLD C: the enfilade. Rooms of human size (9.4 x 6 x 4 m) joined by aligned doorways, an endless row of
# them (a translation screw), and a row on either side reached by side doorways: every view looks through
# two or three thresholds. One lamp per room. Windows between rows look into the next row's identical room.
# ======================================================================================================
C_L = 9.7
C_HW = 3.0
C_HT = 4.0
C_ROW = 6.3


def world_enfilade():
    ext = C_L / 2 + 0.6
    cross = difference(slab(((-0.15, 0.15), (-0.5, C_HT + 0.5), (-C_ROW / 2 - 0.2, C_ROW / 2 + 0.2))),
                       slab(((-1.0, 1.0), (0.0, 3.0), (-0.6, 0.6))))
    side_cuts = [slab(((2.0, 3.2), (0.0, 3.0), (-C_ROW / 2 - 1.2, C_ROW / 2 + 1.2))),   # the side doorways
                 slab(((-2.9, -1.9), (0.9, 3.3), (-C_ROW / 2 - 1.2, C_ROW / 2 + 1.2)))]  # the windows
    sides = difference(union(slab(((-ext, ext), (-0.5, C_HT + 0.5), (C_HW, C_HW + 0.3))),
                             slab(((-ext, ext), (-0.5, C_HT + 0.5), (-C_HW - 0.3, -C_HW)))), *side_cuts)
    stack = union(slab(((-ext, ext), (-0.3, 0.0), (-C_ROW / 2 - 0.2, C_ROW / 2 + 0.2))),
                  slab(((-ext, ext), (C_HT, C_HT + 0.3), (-C_ROW / 2 - 0.2, C_ROW / 2 + 0.2))),
                  surface(slab(((-ext, ext), (-0.02, 0.003), (-C_HW, C_HW))), FLOOR))
    lamp = translate([2.4, 0.0, 1.5], union(surface(slab(((-0.22, 0.22), (2.55, 2.75), (-0.22, 0.22))), LAMP),
                                            surface(slab(((-0.012, 0.012), (2.75, C_HT), (-0.012, 0.012))), DARK)),
                     name="lampAt")
    cell = union(cross, sides, stack, lamp)
    rows = {"kind": "repeat", "size": [0.0, 0.0, C_ROW], "count": 1, "children": [cell]}
    tree = screw(rows, [C_L, 0.0, 0.0], seam=0.2, name="rooms")
    tree = breathing(tree, amount=0.0, frequency=0.06, axes=(1.0, 0.0, 1.0), cell=[C_L, 0.0, 0.0], fade=3.0,
                     name="breath")
    return tremble(tree, amount=0.0, frequency=3.0, radius=3.5, fade=2.0)


def path_enfilade(turn=True):
    r1 = C_ROW
    row0 = [[-1.2, 0.0, 0.0], [2.5, 0.0, 0.15], [7.5, 0.0, -0.2], [12.0, 0.0, 0.1], [16.8, 0.0, -0.25],
            [19.8, 0.0, 0.2]]
    if not turn:
        return row0 + [[24.0, 0.0, 0.0], [29.1, 0.0, 0.0], [33.0, 0.0, 0.0]]
    return row0 + [[21.6, 0.0, 1.3], [22.0, 0.0, C_HW + 0.15], [22.5, 0.0, r1 - 1.3], [24.4, 0.0, r1 - 0.1],
                   [29.0, 0.0, r1 + 0.2], [34.0, 0.0, r1 - 0.15], [38.8, 0.0, r1], [41.5, 0.0, r1]]


# ======================================================================================================
# WORLD D: the stairhead over the void. A landing on the wall of an enormous shaft whose bottom is fog; two
# flights go down from it. The figure stands at the top of one; the camera takes the other. Below: a lower
# landing where a patch of sunlight slides, a narrow passage through a free-standing wall (the rebuild), and
# the crossing stairwell beyond (verse 2): a bridge, a flight up that the camera turns back from, a flight
# down, a doorway in a wall plane full of light.
# ======================================================================================================
D_FIG = (6.3, 0.0, 4.2)   # on a ledge past the head of the flights, facing the void
D_L_STEPS = 19
D_LOW_Y = -D_L_STEPS * 0.18          # -3.42
D_FOOT_L = 4.0 + D_L_STEPS * 0.3      # 9.7
D_PX0 = D_FOOT_L + 3.2               # the passage wall
D_BX0 = D_PX0 + 4.0                  # the bridge
D_DOWN = 16                          # the far flight's risers
D_Y3 = D_LOW_Y - D_DOWN * 0.18
D_Z3 = -4.0 - D_DOWN * 0.3           # the far flight's foot (z)


def world_void():
    rep_x = {"kind": "repeat", "size": [0.0, 9.0, 12.0], "count": 0,
             "children": [slab(((-1.0, 1.0), (-1.7, 1.7), (-1.0, 1.0)))]}
    rep_z = {"kind": "repeat", "size": [12.0, 9.0, 0.0], "count": 0,
             "children": [slab(((-1.0, 1.0), (-1.7, 1.7), (-1.0, 1.0)))]}
    far = union(difference(slab(((48.0, 48.6), (-90.0, 40.0), (-40.0, 40.0))), translate([48.3, 0.0, 0.0], rep_x)),
                difference(slab(((-30.0, 48.0), (-90.0, 40.0), (30.0, 30.6))), translate([0.0, 0.0, 30.3], rep_z)))
    door = slab(((-4.0, -2.0), (0.0, 3.2), (-0.75, 0.75)))
    near = difference(union(slab(((-3.4, -3.0), (-90.0, 40.0), (-30.0, 30.0))),
                            shellbox(((-7.3, -3.2), (-0.3, 3.5), (-1.1, 1.1)), 0.3, LAMP)), door)
    ledge = union(slab(((D_FIG[0] - 0.8, D_FIG[0] + 0.8), (-0.4, 0.0), (D_FIG[2] - 0.8, D_FIG[2] + 0.8))),
                  slab(((D_FIG[0] - 0.3, D_FIG[0] + 0.3), (-30.0, -0.4), (D_FIG[2] - 0.3, D_FIG[2] + 0.3))))
    head = union(slab(((-3.0, 4.0), (-0.5, 0.0), (-4.6, 4.6))), ledge,
                 surface(slab(((-3.0, 4.0), (-0.02, 0.004), (-4.6, 4.6))), FLOOR))
    flights = union(stairway([D_FOOT_L, D_LOW_Y, -2.0], D_L_STEPS, run=0.3, rise=0.18, width=1.6, direction="-x",
                             thickness=0.3),
                    stairway([4.0 + 44 * 0.3, -44 * 0.18, 2.0], 44, run=0.3, rise=0.18, width=1.6, direction="-x",
                             thickness=0.3))
    low = union(slab(((D_FOOT_L, D_FOOT_L + 3.2), (D_LOW_Y - 0.4, D_LOW_Y), (-3.6, -0.4))),
                slab(((D_FOOT_L + 1.1, D_FOOT_L + 2.1), (D_LOW_Y - 40.0, D_LOW_Y - 0.4), (-2.5, -1.5))),
                surface(translate([D_FOOT_L + 0.9, D_LOW_Y - 0.009, -2.6], box([0.65, 0.01, 0.75]), name="sunSlide"),
                        SUN))
    passage = difference(slab(((D_PX0, D_PX0 + 4.0), (D_LOW_Y - 1.0, D_LOW_Y + 3.2), (-5.0, 1.0))),
                         slab(((D_PX0 - 1.0, D_PX0 + 5.0), (D_LOW_Y, D_LOW_Y + 2.6), (-2.7, -1.3))))
    bx0 = D_BX0
    walk = union(slab(((bx0 - 0.5, bx0 + 6.0), (D_LOW_Y - 0.35, D_LOW_Y), (-2.8, -1.2))),
                 slab(((bx0 + 6.0, bx0 + 9.0), (D_LOW_Y - 0.4, D_LOW_Y), (-4.0, 0.0))),
                 slab(((bx0 + 7.0, bx0 + 8.0), (D_LOW_Y - 50.0, D_LOW_Y - 0.4), (-2.5, -1.5))),
                 stairway([bx0 + 7.5, D_LOW_Y, 0.0], 16, run=0.3, rise=0.18, width=1.5, direction="+z", thickness=0.3),
                 stairway([bx0 + 7.5, D_Y3, D_Z3], D_DOWN, run=0.3, rise=0.18, width=1.5, direction="+z",
                          thickness=0.3),
                 slab(((bx0 + 6.0, bx0 + 9.0), (D_Y3 - 0.4, D_Y3), (D_Z3 - 2.6, D_Z3))))
    # the wall plane across the foot of the far flight, its doorway full of light
    wz = D_Z3 - 2.6
    wallplane = difference(union(slab(((bx0 + 3.0, bx0 + 12.0), (D_Y3 - 6.0, D_Y3 + 7.0), (wz - 0.4, wz))),
                                 shellbox(((bx0 + 6.4, bx0 + 8.6), (D_Y3 - 0.3, D_Y3 + 3.3), (wz - 4.3, wz - 0.2)),
                                          0.3, LAMP)),
                           slab(((bx0 + 6.85, bx0 + 8.15), (D_Y3, D_Y3 + 3.0), (wz - 1.4, wz + 1.0))))
    cross = union(stairway([bx0 + 12.0, 1.0, 9.0], 30, run=0.3, rise=0.18, width=1.4, direction="+x", thickness=0.3),
                  stairway([bx0 + 2.0, -14.0, -12.0], 30, run=0.3, rise=0.18, width=1.4, direction="+z", thickness=0.3),
                  landing([bx0 + 23.5, 6.4, 9.0], 3.0, 3.0))
    tree = union(far, near, head, flights, low, passage, walk, wallplane, cross)
    tree = breathing(tree, amount=0.0, frequency=0.05, axes=(1.0, 0.0, 1.0), name="breath")
    return tremble(tree, amount=0.0, frequency=3.0, radius=3.5, fade=2.0)


def path_void():
    bx0 = D_BX0
    y1 = D_LOW_Y
    return [[-5.0, 0.0, 0.0], [-2.5, 0.0, 0.0], [0.5, 0.0, -0.9], [2.6, 0.0, -1.9],   # the stairhead (stop)
            [3.9, -0.02, -2.0], [D_FOOT_L - 0.2, D_LOW_Y + 0.05, -2.0],                     # down flight L
            [D_FOOT_L + 1.4, y1, -2.2], [D_FOOT_L + 2.8, y1, -2.0],                         # the low landing
            [D_PX0 + 1.0, y1, -2.0], [D_PX0 + 3.0, y1, -2.0],                               # the passage
            [bx0 + 1.5, y1, -2.0], [bx0 + 5.0, y1, -2.0], [bx0 + 7.3, y1, -1.0],            # the bridge
            [bx0 + 7.5, y1 + 0.35, 0.6],                                                  # up two steps...
            [bx0 + 7.6, y1, -1.6],                                                        # ...and back
            [bx0 + 7.5, y1, -3.7], [bx0 + 7.5, y1 - 0.2, -4.4],                           # down the far flight
            [bx0 + 7.5, D_Y3 + 0.2, D_Z3 + 0.3], [bx0 + 7.5, D_Y3, D_Z3 - 1.2],
            [bx0 + 7.5, D_Y3, D_Z3 - 3.0], [bx0 + 7.5, D_Y3, D_Z3 - 5.5]]


# ======================================================================================================
# WORLD G: the room that grows. A 3 m room with the beacon's light in it; the camera steps in on the impact
# of bar 67 and the room grows around it, walls receding and the ceiling rising, until it is a hall. A
# doorway opens in the far wall onto daylight: sky and sun, seen for the first time.
# ======================================================================================================
G_L0, G_L1 = 3.4, 18.0
G_W0, G_W1 = 3.4, 13.0
G_H0, G_H1 = 3.2, 10.0
G_WALL = 0.3


def g_room(L, W, Hh):
    size = [L / 2 + G_WALL / 2, Hh / 2 + G_WALL / 2, W / 2 + G_WALL / 2]
    at = [L / 2, Hh / 2, 0.0]
    return size, at


def world_grow():
    size, at = g_room(G_L0, G_W0, G_H0)
    body = translate(at, {"kind": "shell", "offset": G_WALL, "children": [box(size, name="grow")]}, name="growAt")
    entry = slab(((-1.0, 1.0), (0.0, 2.7), (-0.8, 0.8)))
    sky_door = translate([G_L0, 1.7, 0.0], box([0.0, 0.0, 0.0], name="skyDoor"), name="skyDoorAt")
    room = difference(body, entry, sky_door)
    floor_skin = surface(translate([at[0], 0.0, 0.0], box([size[0] - G_WALL, 0.012, size[2] - G_WALL], name="growFloor"),
                                   name="growFloorAt"), FLOOR)
    vest = shellbox(((-4.3, 0.0), (-0.3, 2.9), (-1.0, 1.0)), 0.3)
    out = union(slab(((G_L1 + 0.15, G_L1 + 4.0), (-0.4, 0.0), (-2.0, 2.0))),
                surface(slab(((G_L1 + 0.15, G_L1 + 4.0), (-0.02, 0.004), (-2.0, 2.0))), FLOOR))
    tree = difference(union(room, floor_skin, vest, out), slab(((-1.0, 0.3), (0.0, 2.7), (-0.8, 0.8))))
    tree = breathing(tree, amount=0.0, frequency=0.06, axes=(1.0, 0.0, 1.0), name="breath")
    return tremble(tree, amount=0.0, frequency=3.0, radius=3.5, fade=2.0)


def path_grow():
    return [[-3.6, 0.0, 0.0], [-1.0, 0.0, 0.0], [2.0, 0.0, 0.05], [6.0, 0.0, -0.1], [10.0, 0.0, 0.1],
            [14.0, 0.0, 0.0], [G_L1 - 0.2, 0.0, 0.0], [G_L1 + 1.0, 0.0, 0.0], [G_L1 + 3.0, 0.0, 0.0]]


# ======================================================================================================
# WORLD P: the Penrose stairwell. Four flights round a square well, each a stair up (a helix screw, n = 4),
# so four flights climbed arrive at the same landing, one storey higher. Every landing has a doorway in the
# outer wall full of the beacon's light. The outer walls are split at their middle: the seams open (keyed)
# onto slivers of sky.
# ======================================================================================================
P_R = 4.0
P_RISE = 2.4
P_STEPS = 12
P_RUN = 0.42
P_TURN = 4 * P_RISE
P_HALF = P_STEPS * P_RUN / 2              # 2.52
P_DOOR_Z = (P_R + P_HALF) / 2 + 0.25


def world_penrose():
    flight = stairway([P_R, 0.0, -P_HALF], P_STEPS, run=P_RUN, rise=P_RISE / P_STEPS, width=2.0,
                      direction="+z", thickness=0.3)
    land0 = landing([P_R, 0.0, -(P_R + P_HALF) / 2 - 0.25], 2.0, P_R - P_HALF + 1.5, thickness=0.3)
    land1 = landing([P_R, P_RISE, (P_R + P_HALF) / 2 + 0.25], 2.0, P_R - P_HALF + 1.5, thickness=0.3)
    ywall = (P_RISE / 2 - P_TURN / 2, P_RISE / 2 + P_TURN / 2)
    wall_a = moved(difference(slab(((P_R + 1.0, P_R + 1.3), ywall, (0.0, P_R + 1.3))),
                              slab(((P_R + 0.5, P_R + 1.8), (P_RISE, P_RISE + 2.7), (P_DOOR_Z - 0.8, P_DOOR_Z + 0.8)))),
                   "crackA")
    wall_b = moved(slab(((P_R + 1.0, P_R + 1.3), ywall, (-P_R - 1.3, 0.0))), "crackB")
    sky = surface(slab(((P_R + 2.2, P_R + 2.4), ywall, (-1.5, 1.5))), SKY)
    beacon = surface(shellbox(((P_R + 1.2, P_R + 3.6), (P_RISE - 0.3, P_RISE + 3.0), (P_DOOR_Z - 1.2, P_DOOR_Z + 1.2)),
                              0.3), BEACON)
    beacon = difference(beacon, slab(((P_R + 0.8, P_R + 1.8), (P_RISE, P_RISE + 2.7), (P_DOOR_Z - 0.8, P_DOOR_Z + 0.8))))
    cell = union(flight, land0, land1, wall_a, wall_b, sky, beacon)
    tree = screw(cell, [0.0, P_RISE, 0.0], count=4, seam=0.16, name="penrose")
    tree = breathing(tree, amount=0.0, frequency=0.07, axes=(1.0, 0.0, 1.0), name="breath")
    return tremble(tree, amount=0.0, frequency=3.0, radius=3.5, fade=2.0)


def _helix(p, k):
    a = 2.0 * math.pi / 4 * k
    c, s_ = math.cos(a), math.sin(a)
    x, y, z = p
    return [x * c - z * s_, y + k * P_RISE, x * s_ + z * c]


def path_penrose():
    cell = [[P_R, 0.0, -P_R], [P_R, 0.08, -P_HALF - 0.1], [P_R, P_RISE / 2, 0.0], [P_R, P_RISE - 0.08, P_HALF + 0.1]]
    pts = [[P_R - 3.0, 0.0, -P_R], [P_R - 1.2, 0.0, -P_R]]
    for k in range(4):
        pts += [_helix(p, k) for p in cell]
    # after four flights: the same landing, one turn up; turn to its doorway and go in
    pts += [_helix([P_R - 0.3, P_RISE, P_DOOR_Z - 0.1], 3), _helix([P_R + 1.1, P_RISE, P_DOOR_Z], 3),
            _helix([P_R + 2.0, P_RISE, P_DOOR_Z], 3), _helix([P_R + 2.4, P_RISE, P_DOOR_Z], 3)]
    return pts


# ======================================================================================================
# WORLD E: the open. A landing in the air, the four walls of the stairwell around it separating and the
# ceiling lifting away; a long stair up into the sky; the building's parts standing free across a bright
# haze (doorways, landings, flights, wall planes); the figure at the top; far below, small and roofless, the
# first corridor; ahead, three fragments that line up from the top into one doorway framing the sun.
# ======================================================================================================
E_STEPS = 44
E_RUN, E_RISE = 0.32, 0.18
E_TOP_Y = E_STEPS * E_RISE                    # 7.92
E_TOP_X = 2.0 + E_STEPS * E_RUN               # 16.08
E_BR_Y = E_TOP_Y
E_BR_L = 16.0
E_HIGH_Y = E_TOP_Y + 4.5
E_UP_STEPS = 25
E_HX0 = E_TOP_X + E_BR_L + E_UP_STEPS * 0.3 + 0.2   # the top landing's near edge


def doorframe(x, y, z, w=1.5, h=3.1, t=0.3, depth=0.4):
    return union(slab(((x - depth / 2, x + depth / 2), (y, y + h + t), (z - w / 2 - t, z - w / 2))),
                 slab(((x - depth / 2, x + depth / 2), (y, y + h + t), (z + w / 2, z + w / 2 + t))),
                 slab(((x - depth / 2, x + depth / 2), (y + h, y + h + t), (z - w / 2 - t, z + w / 2 + t))))


def world_open():
    base = union(slab(((-2.4, 2.4), (-0.4, 0.0), (-2.4, 2.4))),
                 surface(slab(((-2.4, 2.4), (-0.02, 0.004), (-2.4, 2.4))), FLOOR),
                 slab(((-0.5, 0.5), (-30.0, -0.4), (-0.5, 0.5))),
                 difference(shellbox(((-6.8, -2.6), (-0.3, 3.5), (-1.1, 1.1)), 0.3, LAMP),
                            slab(((-3.2, -2.0), (0.0, 3.2), (-0.75, 0.75)))))
    walls = [moved(difference(slab(((-2.8, -2.4), (-0.4, 6.0), (-2.8, 2.8))),
                              slab(((-3.6, -1.8), (0.0, 3.2), (-0.75, 0.75)))), "wallW"),
             moved(slab(((-2.8, 2.8), (-0.4, 6.0), (2.4, 2.8))), "wallN"),
             moved(slab(((-2.8, 2.8), (-0.4, 6.0), (-2.8, -2.4))), "wallS"),
             moved(slab(((2.4, 2.8), (-0.4, 6.0), (-2.8, 2.8))), "wallE"),
             moved(slab(((-2.8, 2.8), (6.0, 6.4), (-2.8, 2.8))), "lid")]
    stair = stairway([2.0, 0.0, 0.0], E_STEPS, run=E_RUN, rise=E_RISE, width=2.4, direction="+x", thickness=0.45)
    bx0, bx1 = E_TOP_X, E_TOP_X + E_BR_L
    bridge = union(slab(((bx0, bx1), (E_BR_Y - 0.4, E_BR_Y), (-1.4, 1.4))),
                   surface(slab(((bx0, bx1), (E_BR_Y - 0.02, E_BR_Y + 0.004), (-1.4, 1.4))), FLOOR))
    frames = translate([bx0 + 7.0, 0.0, 0.0], {"kind": "repeat", "size": [4.6, 0.0, 0.0], "count": 1, "children": [
        doorframe(0.0, E_BR_Y, 0.0)]})
    hx0 = E_HX0
    upper = union(stairway([bx1, E_BR_Y, 0.0], E_UP_STEPS, run=0.3, rise=(E_HIGH_Y - E_BR_Y) / E_UP_STEPS, width=2.0,
                           direction="+x", thickness=0.4),
                  slab(((hx0 - 0.3, hx0 + 4.5), (E_HIGH_Y - 0.4, E_HIGH_Y), (-2.5, 2.5))),
                  surface(slab(((hx0 - 0.3, hx0 + 4.5), (E_HIGH_Y - 0.02, E_HIGH_Y + 0.004), (-2.5, 2.5))), FLOOR),
                  slab(((hx0 + 1.8, hx0 + 2.7), (E_HIGH_Y - 40.0, E_HIGH_Y - 0.4), (-0.45, 0.45))))
    free = union(
        surface(difference(slab(((14.0, 22.0), (2.0, 14.0), (12.0, 12.4))), slab(((17.2, 18.6), (5.0, 8.2), (11.0, 13.4)))),
                ACCENT),
        stairway([24.0, -4.0, -14.0], 24, run=0.3, rise=0.18, width=1.4, direction="+z", thickness=0.3),
        union(landing([30.0, 3.0, 13.0], 3.0, 3.0), slab(((29.6, 30.4), (-30.0, 2.7), (12.6, 13.4)))))
    cx, cy, cz = 70.0, -26.0, -40.0   # ahead-left of the top landing, 40 m down: seen at bar 108
    corr = difference(shellbox(((cx - 20.3, cx + 20.3), (cy - 0.3, cy + 4.75), (cz - 1.9, cz + 1.9)), 0.3, FLOOR),
                      slab(((cx - 19.9, cx + 19.9), (cy + 4.0, cy + 6.0), (cz - 1.6, cz + 1.6))),
                      translate([cx + 1.5, cy + 2.4, cz - 1.75], {"kind": "repeat", "size": [3.0, 0.0, 0.0], "count": 6,
                                                                  "children": [box([0.55, 1.55, 0.6])]}))
    corr = union(corr, surface(slab(((cx + 20.6, cx + 20.9), (cy, cy + 3.2), (cz - 0.7, cz + 0.7))), BEACON))
    ax = hx0 + 2.2
    align = [moved(slab(((ax + 40.0, ax + 40.6), (E_HIGH_Y - 8.0, E_HIGH_Y + 6.0), (-4.6, -1.6))), "alignL"),
             moved(slab(((ax + 55.0, ax + 55.6), (E_HIGH_Y - 8.0, E_HIGH_Y + 7.0), (2.2, 5.6))), "alignR"),
             moved(slab(((ax + 70.0, ax + 70.6), (E_HIGH_Y + 9.0, E_HIGH_Y + 12.0), (-6.0, 6.0))), "alignT")]
    tree = union(base, *walls, stair, bridge, frames, upper, free, corr, *align)
    tree = breathing(tree, amount=0.0, frequency=0.03, axes=(1.0, 0.0, 1.0), name="breath")
    return tremble(tree, amount=0.0, frequency=3.0, radius=3.5, fade=2.0)


def path_open():
    bx0, bx1 = E_TOP_X, E_TOP_X + E_BR_L
    hx0 = E_HX0
    up_run = E_UP_STEPS * 0.3
    return [[-6.0, 0.0, 0.0], [-3.6, 0.0, 0.0], [-1.0, 0.0, 0.0], [1.6, 0.0, 0.0], [2.3, 0.05, 0.0],
            [(2.0 + E_TOP_X) / 2, E_TOP_Y / 2, 0.0], [E_TOP_X - 0.3, E_TOP_Y - 0.05, 0.0], [E_TOP_X + 1.5, E_BR_Y, 0.0],
            [bx0 + 7.0, E_BR_Y, 0.05], [bx0 + 11.6, E_BR_Y, -0.05], [bx1 - 0.6, E_BR_Y, 0.0],
            [bx1 + 0.3, E_BR_Y + 0.05, 0.0], [bx1 + up_run / 2, (E_BR_Y + E_HIGH_Y) / 2, 0.0],
            [bx1 + up_run - 0.2, E_HIGH_Y - 0.05, 0.0], [hx0 + 0.8, E_HIGH_Y, -0.5], [hx0 + 1.8, E_HIGH_Y, -1.75],
            [hx0 + 2.3, E_HIGH_Y, -2.0], [hx0 + 3.5, E_HIGH_Y, -2.05]]


# ======================================================================================================
# scene, project
# ======================================================================================================

def track(target, keys, component=-1, interp="smooth"):
    return {"target": target, "component": component, "timeBase": "seconds", "mode": "replace",
            "loopLength": 0.0, "enabled": True,
            "keys": [{"time": round(float(t), 4), "value": v if isinstance(v, list) else [float(v)], "interp": interp}
                     for t, v in keys]}


def route(source, target, amount=1.0, component=-1, depth=None, **chain):
    r = {"source": source, "target": target, "amount": amount, "op": "add", "polarity": "unipolar",
         "component": component, "enabled": True, "chain": chain}
    if depth:
        r["depthSource"] = depth
        r["depthMin"] = 0.0
        r["depthMax"] = 1.0
    return r


def tl_source(name, keys, interp="smooth"):
    return {"kind": "timeline", "name": name,
            "settings": {"keys": [{"time": round(float(t), 4), "value": float(v), "interp": interp} for t, v in keys],
                         "loopLength": 0.0, "mode": "value"}}


WORLDS = ["corridor", "hall", "enfilade", "void", "grow", "penrose", "open"]
# Every world at the origin. Only the active chapter's nodes and lights are drawn, so the worlds may overlap;
# and they must stay near the origin: placed 1000-6000 m out, the image jittered horizontally by up to 20 px
# at 1080p with the camera still (float precision; measured with every track frozen, gone at the origin).
OFFSET = {w: [0.0, 0.0, 0.0] for w in WORLDS}


def wpt(world, p):
    o = OFFSET[world]
    return [p[0] + o[0], p[1] + o[1], p[2] + o[2]]


def build(check_only=False, end=SONG_END):
    with open(SECTIONS) as f:
        sec = json.load(f)
    keys = sec["palette_keys"]

    # ---- the palette: K0..K12 as linear RGB roles ---------------------------------------------------
    beacon_hex = {"K0": "#E2B06E", "K1": "#F2A54A", "K2": "#F2A54A", "K3": "#D9B58A", "K4": "#F2A54A",
                  "K5": "#F2B85A", "K6": "#F7C85C", "K7": "#F7C85C", "K8": "#FFB85A", "K9": "#FFB85A",
                  "K10": "#FFF4DC", "K11": "#FFF8E8", "K12": "#FFFAF0"}
    # The sky is also the interior's ambient light (the procedural sky lights every surface through its IBL),
    # so these are the light colours of each place as much as what a window shows: warm walls under a cool
    # ceiling in the hall, dusk with a warm horizon in the rooms, sodium in the loop, daylight at the end.
    sky_zen = {"K1": "#A9B9E6", "K2": "#7F8FBE", "K5": "#7F8FBE", "K6": "#7FB8E8", "K7": "#72A5D0", "K8": "#C9824A",
               "K9": "#CF8C54", "K10": "#8EC5EE", "K11": "#A5D2F2", "K12": "#F8F1E4"}
    sky_hor = {"K1": "#F3D8B0", "K2": "#C9A98E", "K5": "#C9A98E", "K6": "#F6E7C8", "K7": "#E8D9B8", "K8": "#F0A060",
               "K9": "#F2A866", "K10": "#F6E7C8", "K11": "#FBF0DC", "K12": "#FFFAF0"}
    states = []
    for i in range(13):
        name = f"K{i}"
        k = keys[name]
        walls = hexlin(k["walls"])
        air = hexlin(k["air"])
        if name == "K4":   # the restless teal, quieter as an air colour (the whole frame is air)
            air = mix(air, hexlin(keys["K3"]["air"]), 0.55)
        if name == "K1":   # luminous cobalt: the depth of the hall, not a lavender wash over everything
            air = mix(air, hexlin("#8C9FD8"), 0.45)
        light = hexlin(k["light"])
        accent = hexlin(k["accent"])
        zen = hexlin(sky_zen[name]) if name in sky_zen else mul(air, 1.5)
        hor = hexlin(sky_hor[name]) if name in sky_hor else mul(air, 1.5)
        sky_glow = hexlin(k["accent"]) if name in ("K6", "K7", "K9") else mix(air, [1.0, 1.0, 1.0], 0.2)
        states.append({"name": name, "colors": {
            "walls": walls, "floor": mul(walls, 0.66), "dark": mul(walls, 0.3), "air": air, "light": light,
            "accent": accent, "beacon": hexlin(beacon_hex[name]), "lamp": light, "skyglow": sky_glow,
            "zenith": zen, "horizon": hor, "ground": mul(walls, 0.85) if name not in ("K10", "K11", "K12") else hor,
            "ambient": mix(light, air, 0.2 if name in ("K1", "K2", "K5", "K6", "K7", "K10", "K11") else 0.5),
            "figure": mul(walls, 0.035),
        }})

    # ---- worlds -----------------------------------------------------------------------------------
    makers = {"corridor": world_corridor, "hall": world_hall, "enfilade": world_enfilade, "void": world_void,
              "grow": world_grow, "penrose": world_penrose, "open": world_open}
    looks = {"corridor": 0.55, "hall": 0.5, "enfilade": 0.55, "void": 0.45, "grow": 0.5, "penrose": 0.55, "open": 0.35}
    maxd = {"corridor": 150.0, "hall": 90.0, "enfilade": 120.0, "void": 160.0, "grow": 90.0, "penrose": 90.0,
            "open": 220.0}
    nodes = []
    for w in WORLDS:
        tree = makers[w]()
        n, depth = count_nodes(tree)
        assert n <= 96 and depth <= 8, (w, n, depth)
        seam = 0.0012 * maxd[w]
        node = sdf_node(w, tree, surfaces=surfaces_default(), step_scale=0.8, max_distance=maxd[w],
                        look={"aoStrength": looks[w], "aoDistance": 1.4}, position=OFFSET[w])
        pass
        nodes.append(node)
        print(f"world {w}: {n} nodes, depth {depth}, seam floor {seam:.3f}")

    # ---- the figure: one silhouette, four places (Quaternius UAL1, CC0, local-only) -------------------
    def figure(name, pos, yaw, state="Idle_Loop", journey=None):
        n = {"name": name, "kind": "gltf", "asset": "../../assets/quaternius/animations/UAL1_Standard.glb",
             "position": pos, "rotation": [0.0, yaw, 0.0], "scale": [1.0, 1.0, 1.0], "tint": [0.1, 0.1, 0.1],
             "animation": {"state": state, "blend": 0.3, "updateHz": 0.0, "cullDistance": 0.0}}
        if journey is not None:
            n["journey"] = {"distance": journey}
        n["roughnessScale"] = 2.0
        return n
    nodes.append(figure("figHall", wpt("hall", [B_FIG[0] + 0.3, B_FIG[1], B_FIG[2]]), 90.0))
    nodes.append(figure("figWalk", wpt("enfilade", [3 * C_L + 0.35, 0.0, 0.1]), 90.0))   # in a doorway, two rooms on
    nodes.append(figure("figVoid", wpt("void", list(D_FIG)), 90.0))
    nodes.append(figure("figTop", wpt("open", [E_HX0 + 3.9, E_HIGH_Y, 1.3]), 90.0))

    # ---- motes: dust hanging in the light (the hall's shafts, the growing room's gold) -------------------
    def motes(name, world, centre, extent, rate, color):
        return {"name": name, "kind": "particles", "particles": {
            "capacity": 20000, "spawnRate": rate, "shape": "box", "position": wpt(world, centre), "extent": extent,
            "lifetimeMin": 9.0, "lifetimeMax": 16.0, "speedMin": 0.01, "speedMax": 0.06, "spread": 1.0,
            "gravity": [0.0, 0.004, 0.0], "sizeStart": 0.011, "sizeEnd": 0.0, "blend": "additive",
            "colorStart": color + [0.32], "colorEnd": color + [0.0]}}
    nodes.append(motes("motesGrow", "grow", [9.0, 4.0, 0.0], [8.0, 4.0, 5.5], 350.0, [1.0, 0.8, 0.45]))

    # ---- lights ---------------------------------------------------------------------------------------
    lights = []

    def point(name, world, p, color, intensity, rng, vol=0.0):
        lights.append({"name": name, "type": "point", "position": wpt(world, p), "color": color,
                       "intensity": intensity, "range": rng, "castsShadow": False, "volumetric": vol})
        return name

    def sun_light(name, direction, color, intensity, vol=0.0, shadow=False):
        lights.append({"name": name, "type": "directional", "direction": direction, "color": color,
                       "intensity": intensity, "castsShadow": shadow, "volumetric": vol})
        return name

    L = {w: [] for w in WORLDS}
    L["corridor"].append(point("riserLight", "corridor", [A_TOP - 2.0, 2 * A_H + 2.6, 2.95], [1.0, 0.85, 0.65], 0.0, 14.0, 0.6))
    L["hall"].append(sun_light("hallSun", [0.25, -0.62, -0.74], [1.0, 0.84, 0.62], 3.0, vol=0.0, shadow=False))
    for r in range(0, 5):
        row = 0.0 if r < 3 else C_ROW
        L["enfilade"].append(point(f"lamp{r}", "enfilade", [2.4 + C_L * r, 2.4, row + 1.5], [1.0, 0.72, 0.42], 9.0, 7.5))
    L["enfilade2"] = []
    for r in range(0, 4):
        L["enfilade2"].append(point(f"lampB{r}", "enfilade", [2.4 + C_L * r, 2.4, -1.5], [1.0, 0.72, 0.42], 9.0, 7.5))
    L["void"].append(point("voidBeacon", "void", [40.0, -6.0, 18.0], [1.0, 0.7, 0.4], 30.0, 40.0, 0.4))
    L["grow"].append(point("goldLight", "grow", [G_L0 - 0.6, 2.2, 0.0], [1.0, 0.78, 0.36], 6.0, 6.0, 0.8))
    L["grow"].append(sun_light("daySunG", [0.55, -0.6, 0.58], [1.0, 0.94, 0.84], 0.0))
    for k in range(5):
        L["penrose"].append(point(f"pBeacon{k}", "penrose", _helix([P_R + 1.6, P_RISE + 1.4, P_DOOR_Z], k),
                                  [1.0, 0.65, 0.3], 6.0, 6.0))
    L["open"].append(sun_light("daySun", [0.55, -0.6, 0.58], [1.0, 0.93, 0.82], 2.6))

    # ======================================================================================================
    # THE WALK. Speed keys per section; fitted so the camera arrives where the plan says, on the beat.
    # ======================================================================================================
    walk = Walk()
    w = walk
    P = {"corridor": Path(path_intro()), "hall": Path(path_hall()), "enfilade": Path(path_enfilade()),
         "void": Path(path_void()), "enfilade2": Path(path_enfilade(turn=False)), "grow": Path(path_grow()),
         "loop": Path(path_loop()), "penrose": Path(path_penrose()), "open": Path(path_open())}
    a_loop = P["corridor"].at_point[12]       # the opening frame, one storey up

    # Intro (bars 1-17): silence; the walk begins; a window; the stair; the opening frame again; the riser.
    w.at(bt(2), 0.0).at(bt(2) + 2.2, 0.55).at(bt(4), 0.55).at(bt(4) + 0.9, 0.2)
    w.at(bt(5) - 0.3, 0.2).at(bt(6), 0.5).at(bt(6) + 1.6, 1.2, "a").at(bt(8), 1.25, "a").at(bt(11, 3), 1.2, "a")
    w.at(bt(12) - 0.1, 0.0).at(bt(12, 3), 0.0)
    w.at(bt(13) + 0.4, 1.0, "b").at(bt(16), 1.1, "b").at(bt(17, 2), 0.9, "b").at(bt(17, 4), 0.0)
    # Hall (bars 18-25): out of the light on the downbeat, down the long stair, across, into the way on.
    w.at(bt(18), 0.0).at(bt(18) + 1.2, 1.4, "h").at(bt(20), 1.5, "h").at(bt(22), 1.6, "h").at(bt(25), 1.4, "h")
    w.at(bt(25, 4), 0.9, "h").at(bt(26), 0.8)
    # Verse 1 (bars 26-41): the enfilade; a hesitation at bar 34; the side door; slowing into the last door.
    w.at(bt(26) + 1.0, 1.1, "c1").at(bt(30), 1.15, "c1").at(bt(34), 1.1, "c1").at(bt(34) + 0.5, -0.35)
    w.at(bt(34) + 1.3, 0.0).at(bt(34) + 2.2, 1.0, "c2").at(bt(38), 1.15, "c2").at(bt(40, 3), 1.0, "c2")
    w.at(bt(41, 2), 0.7, "c2").at(bt(41, 4), 0.35)
    # Let it go (bar 42): stop. Bars 43-48: down the flight, carried by the pulse. Bars 49-50: the passage.
    w.at(bt(42), 0.0).at(bt(43), 0.0).at(bt(43) + 1.5, 0.5, "d1").at(bt(46), 0.5, "d1").at(bt(46) + 0.8, 0.12)
    w.at(bt(47) - 0.2, 0.12).at(bt(47) + 1.0, 0.55, "d1").at(bt(48, 3), 0.55, "d1")
    w.at(bt(49), 0.3, "d2").at(bt(50, 3), 0.25, "d2").at(bt(51), 1.0)
    # Verse 2 (bars 51-58): the bridge; up two steps; a change of mind (back); the far flight down; the door.
    w.at(bt(51) + 1.2, 1.2, "v2a").at(bt(53) - 0.3, 1.0, "v2a").at(bt(53) + 0.3, 0.0).at(bt(53) + 0.9, -0.4)
    w.at(bt(53) + 1.6, 0.0).at(bt(54), 0.0).at(bt(54) + 1.0, 1.1, "v2b").at(bt(56), 1.1, "v2b")
    w.at(bt(56) + 0.5, 0.3).at(bt(56, 3), 0.3).at(bt(57), 1.1, "v2c").at(bt(58, 2), 1.0, "v2c").at(bt(58, 4), 0.6)
    # Bridge (bars 59-66): the verse-1 walk again, through the same rooms, the light flipped; a small doorway.
    w.at(bt(59) + 1.0, 1.1, "br").at(bt(63), 1.15, "br").at(bt(66), 1.0, "br").at(bt(66, 3), 0.35).at(bt(66, 4), 0.3)
    # Feel it grow (bars 67-73): into the tiny room on the impact; it grows; to the far doorway; stop (74).
    w.at(bt(67), 0.4).at(bt(67) + 1.5, 0.9, "g").at(bt(70), 1.15, "g").at(bt(72), 1.35, "g").at(bt(73, 3), 0.8, "g")
    w.at(bt(74), 0.0).at(bt(75, 3), 0.0).at(bt(75, 4) + 0.1, 0.9)
    # Is that all you (bars 76-83): the corridor again, faster, the far door never closer; the pulse stops.
    w.at(bt(76) + 0.8, 1.9, "l").at(bt(80), 2.05, "l").at(bt(83), 2.2, "l").at(bt(83, 3), 0.9).at(bt(84), 0.6)
    # Is that all (bars 84-91): four flights round the well, back where we were; the doorway; the fill.
    w.at(bt(84) + 0.8, 2.0, "p").at(bt(88), 2.15, "p").at(bt(89, 4), 2.0, "p").at(bt(90) + 0.6, 0.45)
    w.at(bt(91, 2), 0.4).at(bt(91, 4), 0.55)
    # Release (bars 92-99): out into the open, up the long stair. For your life (100-107): the doorways.
    w.at(bt(92) + 1.0, 1.4, "e1").at(bt(93), 1.5, "e1").at(bt(99), 1.5, "e1")
    w.at(bt(100), 1.3, "e2").at(bt(106), 1.25, "e2").at(bt(107, 3), 0.8, "e2").at(bt(108), 0.0)
    # Bar 108: beside the figure. Turn down, turn to the sun; the last steps; still.
    w.at(bt(113), 0.0).at(bt(114), 0.3).at(bt(115), 0.2).at(bt(116), 0.0).at(end + 1.0, 0.0)
    walk.integrate(end)

    chapters = []
    swaps = {}

    def local(ch, t):
        return ch["from"] + walk.distance(t) - ch["start"]

    def chapter(name, world, path, t_swap, frm, extra=None):
        ch = {"name": name, "world": world, "path": path, "t": t_swap, "from": frm,
              "start": walk.distance(t_swap) if t_swap > 0 else 0.0}
        if extra:
            ch.update(extra)
        chapters.append(ch)
        return ch

    def fit(group, ch, t0, t1, l1):
        """Fit `group` so chapter `ch`'s camera is at local distance l1 at t1 (from wherever it is at t0)."""
        walk.integrate(end)
        l0 = local(ch, t0)
        walk.fit(group, t0, t1, l1 - l0)
        walk.integrate(end)
        ch["start"] = walk.distance(ch["t"]) if ch["t"] > 0 else 0.0

    # The fits, in time order. Each chapter starts at a swap time inside a threshold of light.
    ch_a = chapter("corridor", "corridor", "corridor", 0.0, 0.0)
    fit("a", ch_a, 0.0, bt(12), a_loop)
    fit("b", ch_a, bt(12, 3), bt(17, 3) + 0.05, a_loop + 13.0)
    ch_b = chapter("hall", "hall", "hall", bt(17, 3) + 0.1, 0.4)
    fit("h", ch_b, bt(18), bt(25, 3) + 0.5, P["hall"].at_point[9] - 0.2)
    ch_c = chapter("enfilade", "enfilade", "enfilade", bt(25, 4) + 0.15, 0.6)
    fit("c1", ch_c, bt(26), bt(34), 21.0)
    fit("c2", ch_c, bt(34) + 1.3, bt(41, 2), P["enfilade"].at_point[12] - 1.6)
    ch_d = chapter("void", "void", "void", bt(41, 3), P["void"].at_point[3] - 0.9)
    fit("d1", ch_d, bt(43), bt(48, 3), P["void"].at_point[5] + 0.3)
    fit("d2", ch_d, bt(48, 3), bt(51), P["void"].at_point[9] - 0.4)
    fit("v2a", ch_d, bt(51), bt(53) + 0.3, P["void"].at_point[13] - 0.1)
    fit("v2b", ch_d, bt(54), bt(56) + 0.5, P["void"].at_point[17] + 0.4)
    fit("v2c", ch_d, bt(56, 3), bt(58, 3), P["void"].at_point[19] - 0.4)
    ch_c2 = chapter("enfilade2", "enfilade", "enfilade2", bt(58, 4) + 0.1, 0.6)
    fit("br", ch_c2, bt(59), bt(66, 3), P["enfilade2"].at_point[6] + 3.3)
    ch_g = chapter("grow", "grow", "grow", bt(66, 4) + 0.1, 0.8)
    fit("g", ch_g, bt(67), bt(74), P["grow"].at_point[6])
    ch_l = chapter("loop", "corridor", "loop", bt(75, 4) + 0.3, 1.0)
    fit("l", ch_l, bt(76), bt(83, 3), 34.0)
    ch_p = chapter("penrose", "penrose", "penrose", bt(83, 4), 1.2)
    fit("p", ch_p, bt(84), bt(90), P["penrose"].at_point[18])
    ch_e = chapter("open", "open", "open", bt(91, 4) + 0.15, 1.6)
    fit("e1", ch_e, bt(92), bt(100), P["open"].at_point[7])
    fit("e2", ch_e, bt(100), bt(108), P["open"].at_point[15] - 0.1)
    walk.integrate(end)
    for ch in chapters:
        ch["start"] = walk.distance(ch["t"]) if ch["t"] > 0 else 0.0

    # Report: where the camera is in each chapter, against the path's length.
    def where(t):
        for ch in reversed(chapters):
            if walk.distance(t) >= ch["start"] and t >= ch["t"] - 1e-6:
                return ch, local(ch, t)
        return chapters[0], local(chapters[0], t)

    print("chapter          swap(s)  start(m)  from   local range        path length")
    for i, ch in enumerate(chapters):
        t1 = chapters[i + 1]["t"] if i + 1 < len(chapters) else end
        l0, l1 = local(ch, max(ch["t"], 0.0)), local(ch, t1)
        print(f"  {ch['name']:<12} {ch['t']:8.2f} {ch['start']:9.2f} {ch['from']:6.2f} {l0:7.2f}..{l1:7.2f}   "
              f"{P[ch['path']].length:7.2f}")
    checks = [("opening frame again (bar 12)", bt(12)), ("riser stop (17.4)", bt(17, 4)), ("hall downbeat", bt(18)),
              ("verse 1", bt(26)), ("side door (35.3)", bt(35, 3)), ("let it go stop (42)", bt(42)),
              ("bridge (59)", bt(59)), ("grow (67)", bt(67)), ("break (74)", bt(74)), ("loop (76)", bt(76)),
              ("is that all (84)", bt(84)), ("same landing (90)", bt(90)), ("release (92)", bt(92)),
              ("for your life (100)", bt(100)), ("beside the figure (108)", bt(108))]
    for label, t in checks:
        ch, l = where(t)
        p = P[ch["path"]].point(l)
        print(f"  {label:<26} t={t:7.2f}  {ch['name']:<10} local {l:6.2f}  at {[round(v, 2) for v in p]}  "
              f"speed {walk.speed(t):.2f}")

    # ---- the journey ------------------------------------------------------------------------------------
    def extended(pts, lengths=(12.0, 24.0)):
        """The path carried straight on past its last point, so the gaze (taken a few metres ahead) never runs
        onto the closing segment towards the screw's far image, where the look direction jittered."""
        (x0, y0, z0), (x1, y1, z1) = pts[-2], pts[-1]
        d = math.dist((x0, z0), (x1, z1)) or 1.0
        ux, uz = (x1 - x0) / d, (z1 - z0) / d
        return pts + [[x1 + ux * L, y1, z1 + uz * L] for L in lengths]
    paths = {"corridor": path_intro(), "hall": path_hall(), "enfilade": path_enfilade(), "void": path_void(),
             "enfilade2": path_enfilade(turn=False), "grow": path_grow(), "loop": path_loop(),
             "penrose": path_penrose(), "open": path_open()}
    paths = {k: extended(v) for k, v in paths.items()}
    # (The dust nodes belong to no chapter: they simulate from the first frame, so a beam is full when the
    # camera arrives; 1000 m from every other world, they are never in another chapter's view.)
    chapter_nodes = {"corridor": ["corridor"], "hall": ["hall", "figHall"],
                     "enfilade": ["enfilade", "figWalk"],
                     "void": ["void", "figVoid"], "enfilade2": ["enfilade"], "grow": ["grow", "motesGrow"],
                     "loop": ["corridor"],
                     "penrose": ["penrose"], "open": ["open", "figTop"]}
    chapter_lights = {"corridor": L["corridor"], "hall": L["hall"], "enfilade": L["enfilade"], "void": L["void"],
                      "enfilade2": L["enfilade2"], "grow": L["grow"], "loop": [], "penrose": L["penrose"],
                      "open": L["open"]}
    journey = []
    for ch in chapters:
        journey.append({"name": ch["name"], "start": round(ch["start"], 4), "from": round(ch["from"], 4),
                        "path": [[round(v, 4) for v in p] for p in paths[ch["path"]]],
                        "screw": {"translation": [5000.0, 0.0, 0.0], "count": 0}, "collide": ch["world"],
                        "radius": 0.14, "offset": OFFSET[ch["world"]], "yaw": 0.0,
                        "nodes": chapter_nodes[ch["name"]], "lights": chapter_lights[ch["name"]]})

    # ======================================================================================================
    # THE TIMELINE
    # ======================================================================================================
    T = []
    T.append(track("camera/journey/distance", walk.keys_dense(end, 0.1), interp="smooth"))

    # The gaze. yaw + left, pitch + up; eased between poses.
    yaw = [(0, 0.0), (bt(4), 0.0), (bt(4) + 1.4, 36.0), (bt(5) - 0.2, 38.0), (bt(5) + 1.3, 0.0),
           (bt(13), 0.0), (bt(14), -6.0), (bt(16), 0.0),
           # hall: a long look up and across at the figure and the shafts
           (bt(18), 0.0), (bt(19), -14.0), (bt(20, 3), -18.0), (bt(21, 3), 0.0),
           # verse 1: the gaze wanders to side rooms when the voice rests
           (bt(26), 0.0), (bt(28, 3), 0.0), (bt(29, 2), 24.0), (bt(30), 20.0), (bt(30, 3), 0.0),
           (bt(37), 0.0), (bt(37, 3), -28.0), (bt(38, 2), -22.0), (bt(39), 0.0),
           # let it go: still, looking out past the figure at the void; bar 46 look back up at it; bar 48 forward
           (bt(41, 3), 0.0), (bt(42) - 0.3, -30.0), (bt(43), -30.0), (bt(44), -8.0), (bt(46) - 0.6, 0.0), (bt(46) + 2.8, -112.0), (bt(47, 3), -112.0), (bt(48) + 1.6, 0.0),
           # verse 2: searching
           (bt(51), 0.0), (bt(52), 18.0), (bt(53), 30.0), (bt(54), -10.0), (bt(55), -40.0), (bt(56), -20.0),
           (bt(57), 0.0),
           # bridge: the same walk; the gaze passes over the empty doorway (bar 63)
           (bt(59), 0.0), (bt(62, 3), 0.0), (bt(63, 2), 24.0), (bt(64), 20.0), (bt(64, 3), 0.0),
           (bt(67), 0.0), (bt(116), 0.0)]
    # Bar 108: turn to the figure's gaze (down to the corridor); bar 110: back to the sun.
    yaw = [k for k in yaw if k[0] < bt(92)] + [(bt(92), 0.0), (bt(92) + 0.8, 0.0), (bt(93, 3), 28.0), (bt(94, 3), 4.0),
                                                (bt(96), 0.0)] + [(bt(108), 0.0), (bt(108) + 2.6, 52.0), (bt(110), 54.0),
                                                 (bt(111, 3), 0.0), (end, 0.0)]
    T.append(track("camera/journey/yaw", yaw, interp="easeInOut"))
    pitch = [(0, 0.0), (bt(18), 0.0), (bt(18) + 1.5, 12.0), (bt(20), 18.0), (bt(21), 22.0), (bt(22), 6.0),
             (bt(24), 10.0), (bt(25), 4.0), (bt(26), 0.0), (bt(42), 0.0), (bt(46) - 0.6, 0.0), (bt(46) + 2.8, 21.0),
             (bt(47, 3), 21.0), (bt(48) + 1.0, -4.0), (bt(49), 0.0), (bt(56), 0.0), (bt(57), -24.0),
             (bt(58), -6.0), (bt(58, 4), 0.0), (bt(70), 0.0), (bt(84), 0.0), (bt(84) + 0.8, 14.0), (bt(89), 16.0),
             (bt(90), 2.0), (bt(92), 0.0), (bt(92) + 1.5, 8.0), (bt(99), 8.0), (bt(100), 4.0), (bt(107), 4.0),
             (bt(108), 0.0), (bt(108) + 2.6, -38.0), (bt(110), -39.0), (bt(111, 3), 4.0), (bt(113), 6.0),
             (end, 6.0)]
    T.append(track("camera/journey/pitch", pitch, interp="easeInOut"))
    T.append(track("camera/journey/lookAhead", [(0, 3.0), (bt(84), 3.0), (bt(84) + 0.5, 4.0), (bt(90), 4.0),
                                                (bt(90) + 1.0, 3.0), (end, 3.0)]))
    sway = [(0, 0.0), (bt(2), 0.0), (bt(3), 1.2), (bt(17), 1.2), (bt(18), 0.5), (bt(26), 1.4), (bt(41), 1.4),
            (bt(42), 0.0), (bt(43), 0.0), (bt(44), 0.8), (bt(51), 2.0), (bt(58), 2.0), (bt(59), 1.4),
            (bt(67), 0.8), (bt(74), 0.0), (bt(76), 0.0), (bt(76) + 1.0, 0.4), (bt(84), 0.6), (bt(92), 0.8),
            (bt(108), 0.6), (bt(114), 0.0), (end, 0.0)]
    T.append(track("camera/journey/sway", sway))
    T.append(track("camera/journey/swayRate", [(0, 0.045), (end, 0.045)]))
    bob = [(0, 0.0), (bt(2), 0.0), (bt(2) + 2.0, 0.004), (bt(17), 0.004), (bt(18), 0.003), (bt(26), 0.005),
           (bt(42), 0.005), (bt(43), 0.004), (bt(76), 0.004), (bt(76) + 0.8, 0.007), (bt(84), 0.005),
           (bt(92), 0.003), (end, 0.003)]
    T.append(track("camera/journey/bob", bob))
    T.append(track("camera/journey/stride", [(0, 1.3), (bt(76), 1.3), (bt(76) + 0.8, 2.16), (bt(84), 2.16),
                                             (bt(84) + 0.8, 1.4), (end, 1.4)]))
    T.append(track("camera/journey/height", [(0, 1.6), (bt(110), 1.6), (bt(113), 1.75), (end, 1.75)]))
    T.append(track("camera/journey/radius", [(0, 0.14), (end, 0.14)]))
    # The beacon gaze: the voice steers attention. Hall: the beacon high on the far wall.
    beacon_hall = wpt("hall", [B_L + 1.6, 10.8, 0.0])
    T.append(track("camera/journey/lookAt", [(0, beacon_hall), (bt(75), beacon_hall),
                                             (bt(75) + 0.01, wpt("corridor", [140.0, 1.6, 0.0])), (end, wpt("corridor", [140.0, 1.6, 0.0]))],
                   interp="step"))
    T.append(track("camera/journey/lookAtWeight", [(0, 0.0), (bt(22), 0.0), (bt(23), 0.35), (bt(24, 3), 0.35),
                                                   (bt(25, 2), 0.0), (bt(76), 0.0), (bt(76) + 1.0, 0.7),
                                                   (bt(83, 3), 0.7), (bt(83, 4), 0.0), (end, 0.0)]))
    fov = [(0, 58.0), (bt(76), 58.0), (bt(76) + 0.5, 52.0), (bt(80), 72.0), (bt(83), 64.0), (bt(84), 60.0),
           (end, 60.0)]
    T.append(track("camera/fov", fov))

    # The palette's slow voice (K0..K12) on the section boundaries, plus the thresholds of light.
    pos = [(0.0, 0.0)]
    for t0, t1, name in sec["palette_timeline"][1:]:
        idx = int(name[1:])
        pos.append((t0, idx - 1 if pos[-1][1] < idx - 1 else pos[-1][1]))
        pos.append((t1, float(idx)))
    pos.append((end, 12.0))
    clean = []
    for t, v in sorted(pos):
        if clean and t <= clean[-1][0] + 1e-3:
            clean[-1] = (clean[-1][0], v)
        else:
            clean.append((t, v))
    T.append(track("palette/position", clean, interp="easeInOut"))

    # Thresholds of light: lightness and exposure up and down around each chapter swap.
    # (and the slow brightening of the air with the bass pulse at bar 6 -- the music's first growth, answered
    # by the light, not by motion; and "let it go" dimmer than everything around it)
    value = [(0, 1.0), (bt(6), 1.0), (bt(8), 1.07), (bt(13), 1.07)]
    expo = [(0, 0.0)]

    fog_spikes = []

    def threshold(t_in, t_peak0, t_peak1, t_out, v=1.45, ev=1.1, v_in=1.0, v_out=1.0):
        """A threshold of light: the air thickens and brightens and the exposure opens, so the frame becomes a
        luminous haze (bright, never clipped flat) in which the world is swapped; then it clears."""
        value.extend([(t_in, v_in), (t_peak0, v), (t_peak1, v), (t_out, v_out)])
        expo.extend([(t_in, 0.0), (t_peak0, ev), (t_peak1, ev), (t_out, 0.0)])
        fog_spikes.append((t_in, t_peak0, t_peak1, t_out))
    threshold(bt(14, 3), bt(17, 3), bt(18) + 0.15, bt(19), 1.5, 1.2, 1.07, 1.12)  # the riser into the hall
    threshold(bt(25, 2), bt(25, 4), bt(26) + 0.2, bt(26) + 1.4, v_in=1.12)  # the way on, into the rooms
    threshold(bt(41, 1), bt(41, 3) - 0.3, bt(41, 3) + 0.25, bt(42) - 0.1, 1.4, 1.0)   # into the stairhead
    threshold(bt(58, 3), bt(58, 4), bt(59) + 0.2, bt(59) + 1.4)            # back into the rooms
    threshold(bt(66, 3), bt(66, 4) + 0.05, bt(67) + 0.15, bt(67) + 1.6, 1.5, 1.2)    # the tiny room's light
    threshold(bt(75, 3), bt(75, 4) + 0.2, bt(76) + 0.1, bt(76) + 0.9, 1.5, 1.2)      # into the light; the loop
    threshold(bt(83, 3), bt(83, 4) - 0.1, bt(84) + 0.1, bt(84) + 0.9, 1.4, 1.0)      # the pulse stops
    threshold(bt(91, 3), bt(91, 4), bt(92) + 0.3, bt(93), 1.7, 1.5)        # the fill: the brightest door
    value.extend([(bt(42) + 0.6, 0.74), (bt(48), 0.74), (bt(50), 1.0)])

    value.extend([(bt(114), 1.0), (bt(116), 1.35), (end, 1.45)])
    value.sort()
    expo.sort()
    expo.extend([(bt(114), 0.0), (bt(116), 0.9), (end, 1.2)])
    T.append(track("palette/value", value))
    T.append(track("camera/exposure/compensation", expo))
    T.append(track("palette/saturation", [(0, 1.0), (bt(18), 1.0), (bt(19) + 0.5, 1.3), (bt(25, 2), 1.3),
                                          (bt(26) + 1.0, 1.0), (bt(42) - 0.4, 1.0), (bt(42) + 0.6, 0.55), (bt(43), 0.55),
                                          (bt(49), 0.85), (bt(51), 1.0), (bt(74), 1.0), (bt(74) + 0.6, 0.85),
                                          (bt(75, 4), 0.85), (bt(76), 1.0), (bt(114), 1.0), (end, 0.6)]))

    # World air: how thick the luminous fog is, per place (pure functions of time; chapters swap in light).
    dens = [(0, 0.026), (bt(17, 3), 0.026), (bt(18), 0.011), (bt(25, 4), 0.011), (bt(26), 0.03), (bt(41, 3), 0.03),
            (bt(42) - 0.2, 0.05), (bt(48), 0.05), (bt(50), 0.04), (bt(58, 4), 0.04), (bt(59), 0.03), (bt(66, 4), 0.03), (bt(67), 0.024),
            (bt(70), 0.014), (bt(73), 0.004), (bt(75, 4), 0.004), (bt(76), 0.034), (bt(83, 4), 0.034), (bt(84), 0.028),
            (bt(91, 4), 0.028), (bt(92), 0.007), (bt(100), 0.005), (bt(113), 0.005), (bt(116), 0.03), (end, 0.04)]
    dens0 = list(dens)

    def dens_at(t):
        for (t0, v0), (t1, v1) in zip(dens0, dens0[1:]):
            if t0 <= t <= t1:
                u = (t - t0) / max(t1 - t0, 1e-6)
                return v0 + (v1 - v0) * u
        return dens0[-1][1]
    for (a, b, c, d) in fog_spikes:
        dens = [k for k in dens if not (a < k[0] < d)]
        dens += [(a, dens_at(a)), (b, max(dens_at(b), 0.03) * 7.0), (c, max(dens_at(c), 0.03) * 7.0), (d, dens_at(d))]
        dens.sort()
    T.append(track("scene/volumeDensity", dens))
    T.append(track("scene/volumeEmission", [(0, 1.0), (bt(17, 4), 1.0), (bt(18), 0.3), (bt(25, 4), 0.3),
                                            (bt(26), 1.0), (end, 1.0)]))
    T.append(track("lightrig/AllYouGot/ambientIntensity", [
        (0, 1.0), (bt(18), 1.3), (bt(25, 4), 1.3), (bt(26), 0.7), (bt(42), 0.85), (bt(51), 0.7), (bt(67), 0.8), (bt(76), 0.8),
        (bt(84), 0.8), (bt(92), 0.7), (end, 0.9)]))

    # The sun patch: on the corridor floor with the bass pulse (bar 6); in the loop it has moved.
    T.append(track(f"sdf/corridor/surface/{SUN}/emission", [
        (0, [0.0, 0.0, 0.0]), (bt(6), [0.0, 0.0, 0.0]), (bt(7), [1.7, 1.3, 0.75]), (bt(17), [1.7, 1.3, 0.75]),
        (bt(18), [0.0, 0.0, 0.0]), (bt(75), [0.0, 0.0, 0.0]), (bt(76), [1.4, 0.8, 0.35]), (end, [1.4, 0.8, 0.35])]))
    T.append(track(f"sdf/void/surface/{SUN}/emission", [(0, [1.5, 1.15, 0.7]), (end, [1.5, 1.15, 0.7])]))
    T.append(track(f"sdf/hall/surface/{SUN}/emission", [(0, [0.95, 0.62, 0.3]), (end, [0.95, 0.62, 0.3])]))
    T.append(track("sdf/void/node/sunSlide/translation", [(bt(42), [D_FOOT_L + 0.9, D_LOW_Y - 0.009, -2.6]),
                                                           (bt(51), [D_FOOT_L + 2.3, D_LOW_Y - 0.009, -1.6])], interp="linear"))
    # Emission levels (the palette multiplies them by the colour): lamps, beacons, the sky glow, thresholds.
    for wname in WORLDS:
        T.append(track(f"sdf/{wname}/surface/{BEACON}/emission", [(0, [4.0, 4.0, 4.0]), (end, [4.0, 4.0, 4.0])]))
        T.append(track(f"sdf/{wname}/surface/{SKY}/emission", [(0, [2.5, 2.5, 2.5]), (end, [2.5, 2.5, 2.5])]))
    T.append(track(f"sdf/corridor/surface/{LAMP}/emission", [(0, [0.0, 0.0, 0.0]), (bt(75, 4), [0.0, 0.0, 0.0]),
                                                             (bt(76), [6.0, 6.0, 6.0]), (end, [6.0, 6.0, 6.0])]))
    for wname, lvl in (("hall", 3.2), ("open", 3.6)):
        T.append(track(f"sdf/{wname}/surface/{LAMP}/emission", [(0, [lvl] * 3), (end, [lvl] * 3)]))
    T.append(track(f"sdf/void/surface/{LAMP}/emission", [(0, [3.2] * 3), (bt(41, 4), [3.2] * 3), (bt(42), [0.6] * 3),
                                                         (bt(48), [0.6] * 3), (bt(50), [3.2] * 3), (end, [3.2] * 3)]))
    T.append(track(f"sdf/enfilade/surface/{LAMP}/emission", [(0, [5.0, 5.0, 5.0]), (end, [5.0, 5.0, 5.0])]))
    T.append(track("lights/riserLight/intensity", [(0, 0.0), (bt(14), 0.0), (bt(17), 40.0), (end, 40.0)]))
    T.append(track("lights/hallSun/intensity", [(0, 4.5), (end, 4.5)]))

    # The loop: the beacon wall recedes as fast as the camera walks; the corridor narrows and lowers.
    loop_keys = []
    lp = P["loop"]
    for i in range(0, 200):
        t = bt(75, 4) + i * 0.1
        if t > bt(84):
            break
        l = local(ch_l, t)
        x = lp.point(l)[0]
        loop_keys.append((t, [max(0.0, x + 46.0 - A_X1), 0.0, 0.0]))
    T.append(track("sdf/corridor/node/beaconWall/translation",
                   [(0, [0.0, 0.0, 0.0]), (bt(75, 4), [0.0, 0.0, 0.0])] + loop_keys[1:] +
                   [(end, loop_keys[-1][1])], interp="linear"))
    T.append(track("sdf/corridor/node/wallL/translation", [(0, [0.0, 0.0, 0.0]), (bt(76), [0.0, 0.0, 0.0]),
                                                            (bt(83, 3), [0.0, 0.0, 0.32]), (end, [0.0, 0.0, 0.32])]))
    T.append(track("sdf/corridor/node/wallR/translation", [(0, [0.0, 0.0, 0.0]), (bt(76), [0.0, 0.0, 0.0]),
                                                            (bt(83, 3), [0.0, 0.0, -0.32]), (end, [0.0, 0.0, -0.32])]))
    T.append(track("sdf/corridor/node/ceiling/translation", [(0, [0.0, 0.0, 0.0]), (bt(76), [0.0, 0.0, 0.0]),
                                                              (bt(83, 3), [0.0, -0.75, 0.0]), (end, [0.0, -0.75, 0.0])]))
    T.append(track("sdf/corridor/node/lampsAt/translation", [(0, [0.0, 0.0, 0.0]), (bt(76), [0.0, 0.0, 0.0]),
                                                              (bt(83, 3), [0.0, -0.75, 0.0]), (end, [0.0, -0.75, 0.0])]))

    # The bridge's rooms: the lamp on the other side (the light flipped).
    T.append(track("sdf/enfilade/node/lampAt/translation", [(0, [2.4, 0.0, 1.5]), (bt(58, 4), [2.4, 0.0, 1.5]),
                                                             (bt(58, 4) + 0.05, [2.4, 0.0, -1.5]), (end, [2.4, 0.0, -1.5])],
                   interp="step"))

    # The room that grows (bars 67-73), the far doorway opening (bar 70), the gold spreading.
    g_keys, g_at, g_floor, g_floor_at, g_door, g_door_at, g_light = [], [], [], [], [], [], []
    for i in range(0, 61):
        u = i / 60.0
        t = bt(67) + u * (bt(73) - bt(67))
        e = u * u * (3 - 2 * u)
        Lx, Wz, Hh = G_L0 + (G_L1 - G_L0) * e, G_W0 + (G_W1 - G_W0) * e, G_H0 + (G_H1 - G_H0) * e
        size, at = g_room(Lx, Wz, Hh)
        g_keys.append((t, size))
        g_at.append((t, at))
        g_floor.append((t, [size[0] - G_WALL, 0.012, size[2] - G_WALL]))
        g_floor_at.append((t, [at[0], 0.0, 0.0]))
        ud = max(0.0, min(1.0, (t - bt(70)) / (bt(72) - bt(70))))
        ed = ud * ud * (3 - 2 * ud)
        g_door.append((t, [0.7 * ed + 0.001, 1.7 * ed + 0.001, 1.0 * ed + 0.001]))
        g_door_at.append((t, [Lx, 1.7, 0.0]))
        g_light.append((t, [Lx - 0.8, 1.6 + 0.4 * Hh, 0.0]))
    for target, ks in (("sdf/grow/node/grow/size", g_keys), ("sdf/grow/node/growAt/translation", g_at),
                       ("sdf/grow/node/growFloor/size", g_floor), ("sdf/grow/node/growFloorAt/translation", g_floor_at),
                       ("sdf/grow/node/skyDoor/size", g_door), ("sdf/grow/node/skyDoorAt/translation", g_door_at)):
        T.append(track(target, ks, interp="linear"))
    T.append(track("lights/goldLight/position", [(t, wpt("grow", p)) for t, p in g_light], interp="linear"))
    T.append(track("lights/goldLight/intensity", [(0, 8.0), (bt(67), 8.0), (bt(70), 30.0), (bt(73), 70.0),
                                                  (bt(74), 70.0), (bt(75), 55.0), (end, 55.0)]))
    T.append(track("lights/goldLight/range", [(0, 6.0), (bt(67), 6.0), (bt(73), 30.0), (end, 30.0)]))
    T.append(track("lights/daySunG/intensity", [(0, 0.0), (bt(70), 0.0), (bt(72), 2.0), (end, 2.0)]))
    T.append(track("env/sky/sunIntensity", [(0, 0.0), (bt(70), 0.0), (bt(72), 30.0), (bt(75, 4), 30.0),
                                            (bt(76), 0.0), (bt(91, 4), 0.0), (bt(92), 40.0), (end, 40.0)]))

    # The Penrose stairwell's seams: they open onto sky a little wider each time round, then close.
    crack = [(0, 0.0), (bt(84), 0.0), (bt(85, 3), 0.18), (bt(86, 3), 0.04), (bt(87), 0.3), (bt(88), 0.08),
             (bt(88, 3), 0.42), (bt(89, 3), 0.12), (bt(90), 0.5), (bt(91, 3), 0.0), (end, 0.0)]
    T.append(track("sdf/penrose/node/crackA/translation", [(t, [0.0, 0.0, v]) for t, v in crack]))
    T.append(track("sdf/penrose/node/crackB/translation", [(t, [0.0, 0.0, -v]) for t, v in crack]))

    # The open: the stairwell's walls drift apart in order and the lid lifts away (bars 92-96).
    t0, t1 = bt(92) + 0.4, bt(96)
    for name, vec in (("wallW", [-1.0, 0.0, 0.0]), ("wallN", [0.0, 0.0, 1.0]), ("wallS", [0.0, 0.0, -1.0]),
                      ("wallE", [0.0, 0.0, 0.0]), ("lid", [0.0, 1.0, 0.0])):
        dist = 14.0 if name != "lid" else 24.0
        if name == "wallE":
            T.append(track(f"sdf/open/node/{name}/translation", [(0, [0.0, 0.0, 0.0]), (bt(92) + 0.2, [0.0, 0.0, 0.0]),
                                                                (bt(93) + 0.3, [0.0, -40.0, 0.0]), (end, [0.0, -40.0, 0.0])],
                           interp="easeIn"))
            continue
        T.append(track(f"sdf/open/node/{name}/translation", [(0, [0.0, 0.0, 0.0]), (t0, [0.0, 0.0, 0.0]),
                                                            (t1, mul(vec, dist)), (end, mul(vec, dist))]))
    # The alignment (bars 110-113): three fragments slide into one doorway on the sun, seen from the top.
    for name, start_off in (("alignL", [0.0, -3.0, -7.0]), ("alignR", [0.0, 2.5, 8.0]), ("alignT", [0.0, 6.0, 5.0])):
        T.append(track(f"sdf/open/node/{name}/translation", [(0, start_off), (bt(110), start_off),
                                                            (bt(113), [0.0, 0.0, 0.0]), (end, [0.0, 0.0, 0.0])]))

    # The walking figure in verse 1: ahead in the rooms from bar 30, through the side door, then gone.
    T.append(track("nodes/figWalk/visible", [(0, 0.0), (bt(29, 3), 1.0), (bt(35, 3), 0.0), (end, 0.0)], interp="step"))

    # ---- sources and routes: sparse, congruent; nothing reacts in silence -------------------------------
    coupling = []
    for s_ in sec["sections"]:
        t0, t1 = bt(s_["bar0"]), bt(s_["bar1"] + 1)
        c = float(s_["intent"]["coupling"])
        coupling += [(t0 + 0.3, c), (t1 - 0.3, c)]
    coupling = [(0.0, 0.0)] + [k for k in coupling if k[0] < bt(42) - 0.4] + [(bt(42) - 0.3, 0.5), (bt(42), 0.0),
                                                                               (bt(43) - 0.3, 0.0), (bt(43) + 0.4, 0.25)] + \
        [k for k in coupling if k[0] > bt(43) + 0.5]
    coupling = sorted(coupling)
    breath_on = [(0.0, 0.0), (bt(6), 0.0), (bt(8), 0.4), (bt(17, 3), 0.4), (bt(18) - 0.5, 0.0), (bt(18) + 1.0, 1.0), (bt(41, 4), 1.0), (bt(42), 0.0),
                 (bt(51) - 0.5, 0.0), (bt(51) + 1.0, 0.8), (bt(58, 4), 0.8), (bt(59), 1.0), (bt(73, 4), 0.6),
                 (bt(74), 0.0), (bt(84), 0.0), (bt(84) + 0.8, 1.0), (bt(91, 3), 1.0), (bt(91, 4), 0.0),
                 (bt(92), 0.0), (bt(93), 1.6), (bt(113), 1.6), (bt(114), 0.0), (end, 0.0)]
    voice_on = [(0.0, 0.0), (bt(14) - 0.5, 0.0), (bt(14), 0.6), (bt(25, 4), 0.6), (bt(26), 1.0), (bt(74), 1.0),
                (bt(74) + 0.2, 0.0), (bt(76) - 0.3, 0.0), (bt(76), 1.0), (bt(113), 1.0), (bt(114), 0.0), (end, 0.0)]
    rough = [(0.0, 0.0), (bt(76) - 0.3, 0.0), (bt(76) + 0.5, 0.8), (bt(91, 3), 0.9), (bt(92), 0.0), (end, 0.0)]
    sources = [tl_source("coupling", coupling), tl_source("breath", breath_on), tl_source("voice", voice_on),
               tl_source("rough", rough)]
    routes = []
    for wname in ("corridor", "hall", "enfilade", "void", "grow", "penrose", "open"):
        # The world breathes under the low end: a slow, heavy spring into a low-frequency warp.
        routes.append(route("audio.bass", f"sdf/{wname}/node/breath/amount", 0.22, depth="timeline.breath",
                            attackMs=250, decayMs=1800, springHz=0.4, springDamping=0.6))
        # ...and the warp's flow runs at the music's energy and stops in silence (an integrated phase).
        routes.append(route("audio.energy", f"sdf/{wname}/node/breath/translation", 0.05, 0, attackMs=400,
                            decayMs=2000, integrate=True))
        routes.append(route("audio.energy", f"sdf/{wname}/node/breath/translation", 0.035, 2, attackMs=400,
                            decayMs=2000, integrate=True))
    for wname in WORLDS:
        # The voice warms the beacon (the palette multiplies the level by the beacon's colour).
        routes.append(route("audio.mid", f"sdf/{wname}/surface/{BEACON}/emission", 3.5, depth="timeline.voice",
                            attackMs=200, decayMs=1500, springHz=1.2, springDamping=0.8))
        # The bright, noisy top as a nearby shiver, only where the coupling allows it.
        routes.append(route("audio.treble", f"sdf/{wname}/node/tremble/amount", 0.012, depth="timeline.rough",
                            attackMs=40, decayMs=400, springHz=2.0, springDamping=0.6))
        routes.append(route("audio.treble", f"sdf/{wname}/node/tremble/translation", 2.0, 1, attackMs=40,
                            decayMs=400, integrate=True))
    # Phrase-end noise sweeps: the near walls tremble for a beat (authored at the analysed bars).
    trem = [(0.0, 0.0)]
    for bar in (29, 33, 41, 54, 58, 62, 66, 99):
        trem += [(bt(bar, 3) - 0.1, 0.0), (bt(bar, 4), 0.03), (bt(bar + 1) + 0.2, 0.0)]
    trem.append((end, 0.0))
    for wname in WORLDS:
        T.append(track(f"sdf/{wname}/node/tremble/amount", trem))

    # ---- palette bindings ---------------------------------------------------------------------------------
    bindings = []
    for n_ in WORLDS:
        bindings += [{"role": "walls", "target": f"sdf/{n_}/surface/{PLASTER}/color"},
                     {"role": "walls", "target": f"sdf/{n_}/surface/{LAMP}/color"},
                     {"role": "floor", "target": f"sdf/{n_}/surface/{FLOOR}/color"},
                     {"role": "accent", "target": f"sdf/{n_}/surface/{ACCENT}/color"},
                     {"role": "dark", "target": f"sdf/{n_}/surface/{DARK}/color"},
                     {"role": "lamp", "target": f"sdf/{n_}/surface/{LAMP}/emission", "mode": "multiply"},
                     {"role": "beacon", "target": f"sdf/{n_}/surface/{BEACON}/emission", "mode": "multiply"},
                     {"role": "skyglow", "target": f"sdf/{n_}/surface/{SKY}/emission", "mode": "multiply"}]
    bindings += [{"role": "air", "target": "scene/fogColor"},
                 {"role": "zenith", "target": "env/sky/zenithColor"},
                 {"role": "horizon", "target": "env/sky/horizonColor"},
                 {"role": "ground", "target": "env/sky/groundColor"},
                 {"role": "ambient", "target": "lightrig/AllYouGot/ambientColor"},
                 {"role": "figure", "target": "nodes/figHall/tint"}, {"role": "figure", "target": "nodes/figWalk/tint"},
                 {"role": "figure", "target": "nodes/figVoid/tint"}, {"role": "figure", "target": "nodes/figTop/tint"}]
    for wl in L.values():
        for name in wl:
            if name.startswith("lamp") or name.startswith("pBeacon") or name == "voidBeacon":
                bindings.append({"role": "lamp" if name.startswith("lamp") else "beacon",
                                 "target": f"lights/{name}/color"})
    palette = {"states": states, "bindings": bindings, "position": 0.0, "saturation": 1.0, "value": 1.0}

    k0air = hexlin(keys["K0"]["air"])
    scene = {
        "format": "avgen-scene", "version": 1, "name": "All You Got (Liminal)",
        "camera": {"mode": 3, "fov": 58.0, "journey": {"chapters": journey}},
        "environment": {"intensity": 0.0, "background": k0air, "fogColor": k0air,
                        "lightRig": "all-you-got.rig.json", "volumeDensity": 0.03, "volumeAbsorption": 1.0,
                        "volumeEmission": 1.0, "volumeScattering": 0.6, "volumeMaxDistance": 70.0,
                        "volumeJitter": 0.05, "volumeLocalLights": 1.0,
                        "sky": {"enabled": True, "background": True, "useKeyLight": False,
                                "zenithColor": k0air, "horizonColor": k0air, "groundColor": k0air,
                                "sunColor": [1.0, 0.95, 0.85], "sunDirection": [0.9962, 0.0785, 0.0400],
                                "sunIntensity": 0.0, "sunSize": 0.012, "sunGlow": 0.035, "intensity": 0.9}},
        "nodes": nodes,
        "lights": lights,
    }
    rig = {
        "format": "avgen-lightrig", "version": 1, "name": "AllYouGot",
        "description": "Pale, even interior light: a soft ambient (the luminous air); the rooms add their lamps, "
                       "the hall its shafts, the open its sun.",
        "keyIntensity": 1.0, "ambientIntensity": 1.0, "ambientColor": [0.9, 0.93, 1.0], "ambientTemperature": 6500,
        "lights": [{"name": "key", "type": "directional", "role": "key", "azimuth": 90.0, "elevation": 10.0,
                    "distance": 2.0, "intensity": 0.0, "color": [1.0, 1.0, 1.0], "temperature": 6500,
                    "castsShadow": False, "volumetric": 0.0, "followCamera": False}],
    }
    project = {
        "format": "avgen-project", "version": 4, "app": {"name": "avgen", "version": "0.1.0"},
        "assets": {"audio": {"path": "~/Desktop/All You Got.wav"},
                   "scene": {"kind": "composition", "path": "all-you-got.scene.json"}},
        "parameters": {
            "camera/exposure/mode": 0, "camera/exposure/compensation": 0.0, "post/tonemap/operator": 1,
            "post/bloom/enabled": True, "post/bloom/intensity": 0.22, "post/bloom/threshold": 1.0,
            "post/grade/contrast": 1.2, "post/grade/saturation": 1.0, "post/output/vignette": 0.3,
            "post/output/grain": 0.0, "post/motionBlur/amount": 0.5,
        },
        "sources": sources,
        "routes": routes,
        "timeline": {"enabled": True, "cues": [], "tracks": T},
        "palette": palette,
        "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "startSeconds": 0.0,
                   "endSeconds": end},
    }
    if check_only:
        return
    os.makedirs(OUT, exist_ok=True)
    for name, doc in (("all-you-got.scene.json", scene), ("all-you-got.rig.json", rig),
                      ("all-you-got.json", project)):
        with open(os.path.join(OUT, name), "w") as f:
            json.dump(doc, f, indent=1)
            f.write("\n")
    print("wrote", OUT)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--end", type=float, default=SONG_END)
    a = ap.parse_args()
    build(a.check, a.end)

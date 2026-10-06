"""DIGITAL MOSH: the forms -- the olive tree and the Tanguy object -- as SDF trees and voxel shells.

Imported by build.py. Each form is built from one seeded skeleton, which yields both its raymarched SDF objects and
the cubes of its voxel shell, so the blocks sit exactly on the surface they replace.
"""
from __future__ import annotations

import math
import random

# ---------------------------------------------------------------------------------------------------------- vectors


def norm(v):
    m = math.sqrt(sum(x * x for x in v))
    return [x / m for x in v]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def add(a, b):
    return [x + y for x, y in zip(a, b)]


def mul(a, s):
    return [x * s for x in a]


def lerp(a, b, t):
    return [x + (y - x) * t for x, y in zip(a, b)]


def euler_from_matrix(R):
    """Degrees (x, y, z) such that glm::quat(radians(e)) == R, with R = Rz * Ry * Rx (glm's convention)."""
    y = math.asin(max(-1.0, min(1.0, -R[2][0])))
    x = math.atan2(R[2][1], R[2][2])
    z = math.atan2(R[1][0], R[0][0])
    return [math.degrees(x), math.degrees(y), math.degrees(z)]


def rot_y_to(d):
    """Rotation matrix taking +Y onto the unit vector d (Rodrigues)."""
    dx, dy, dz = d
    ax, az = dz, -dx
    s = math.sqrt(ax * ax + az * az)
    c = dy
    if s < 1e-9:
        return [[1, 0, 0], [0, 1 if c > 0 else -1, 0], [0, 0, 1 if c > 0 else -1]]
    ax, az = ax / s, az / s
    t = 1 - c
    return [[t * ax * ax + c, -s * az, t * ax * az], [s * az, c, -s * ax], [t * ax * az, s * ax, t * az * az + c]]


def frame_x_to(d):
    """Local->world matrix whose x is d, z horizontal, y = z x x (upward-ish)."""
    x = norm(d)
    z = cross(x, [0.0, 1.0, 0.0])
    if math.sqrt(dot(z, z)) < 1e-6:
        z = [0.0, 0.0, 1.0]
    z = norm(z)
    y = cross(z, x)
    return [[x[0], y[0], z[0]], [x[1], y[1], z[1]], [x[2], y[2], z[2]]]


def mat_t_vec(R, v):
    return [R[0][i] * v[0] + R[1][i] * v[1] + R[2][i] * v[2] for i in range(3)]


def mat_vec(R, v):
    return [R[i][0] * v[0] + R[i][1] * v[1] + R[i][2] * v[2] for i in range(3)]


# ---------------------------------------------------------------------------------------------------------- SDF


def sdf(kind, children=None, name=None, **kw):
    n = {"kind": kind}
    n.update(kw)
    if children is not None:
        n["children"] = children
    if name:
        n["name"] = name
    return n


def capsule_between(a, b, r):
    d = sub(b, a)
    L = math.sqrt(dot(d, d))
    mid = mul(add(a, b), 0.5)
    e = euler_from_matrix(rot_y_to(norm(d)))
    return sdf("translate", [sdf("rotate", [sdf("capsule", radius=r, height=L)], rotation=e)], translation=mid)


def chunked(items, kind, k):
    """Combinations take at most 8 children: fold in chunks of 8."""
    if len(items) == 1:
        return items[0]
    out = [sdf(kind, items[i:i + 8], smooth=k) if len(items[i:i + 8]) > 1 else items[i]
           for i in range(0, len(items), 8)]
    return chunked(out, kind, k) if len(out) > 1 else out[0]


def count_nodes(n):
    return 1 + sum(count_nodes(c) for c in n.get("children", []))


def capsule_distance(p, a, b, r):
    pa, ba = sub(p, a), sub(b, a)
    h = max(0.0, min(1.0, dot(pa, ba) / max(dot(ba, ba), 1e-9)))
    q = sub(pa, mul(ba, h))
    return math.sqrt(dot(q, q)) - r


def smin(a, b, k):
    h = max(k - abs(a - b), 0.0) / k
    return min(a, b) - h * h * k * 0.25


def voxel_shell(dist, lo, hi, cell, inside=1.0, outside=0.4):
    """Cell centres whose distance is in (-inside*cell, outside*cell]: one layer of blocks straddling the surface."""
    out = []
    n = [int(math.ceil((hi[i] - lo[i]) / cell)) for i in range(3)]
    for i in range(n[0]):
        x = lo[0] + (i + 0.5) * cell
        for j in range(n[1]):
            y = lo[1] + (j + 0.5) * cell
            for k in range(n[2]):
                z = lo[2] + (k + 0.5) * cell
                d = dist([x, y, z])
                if -inside * cell < d <= outside * cell:
                    out.append([x, y, z])
    return out


# ---------------------------------------------------------------------------------------------------------- the olive
# Dali's dead olive, made our own: a trunk of three strands braided round each other (an old olive's trunk splits
# and twists as it ages), a flare of roots gripping the ground, and three limbs -- one long and nearly level, the limb
# the dream softens first. Five SDF objects (the 96-node limit is per object), all in tree-local space; the limbs are
# authored in their own frame (x along the limb from its base) so `bend` curls them like wax and their node
# position lifts them away whole.

class Olive:
    def __init__(self, seed=7):
        self.rnd = random.Random(seed)
        self.trunk = []   # (a, b, r)
        self.roots = []
        self.limbs = []   # [(base, dir, [(a, b, r), ...])]
        self._build()

    def _build(self):
        rnd = self.rnd
        H = 3.3
        lean = norm([0.24, 1.0, 0.06])
        # three strands, each twisting 190 degrees round the trunk's axis, thinning as they rise
        for s in range(3):
            pts = []
            for k in range(7):
                u = k / 6
                axis = mul(lean, H * u)
                ang = s * 2.094 + u * 3.3 + rnd.uniform(-0.15, 0.15)
                rad = 0.20 * (1.0 - 0.45 * u) + 0.03 * math.sin(u * 9 + s)
                off = [math.cos(ang) * rad, 0.0, math.sin(ang) * rad]
                pts.append(add(add(axis, off), [0, -0.25 if k == 0 else 0, 0]))
            for k in range(6):
                u = k / 6
                self.trunk.append((pts[k], pts[k + 1], 0.20 * (1 - 0.5 * u) + 0.02))
        self.top = mul(lean, H)
        # the root flare: five roots leave the trunk's foot, arch and dive into the ground
        for k in range(5):
            a = k * 1.2566 + rnd.uniform(-0.25, 0.25)
            d = [math.cos(a), 0.0, math.sin(a)]
            p0 = [d[0] * 0.15, 0.45, d[2] * 0.15]
            p1 = [d[0] * 0.65, 0.12, d[2] * 0.65]
            p2 = [d[0] * 1.25 + rnd.uniform(-0.1, 0.1), -0.25, d[2] * 1.25 + rnd.uniform(-0.1, 0.1)]
            self.roots.append((p0, p1, 0.17))
            self.roots.append((p1, p2, 0.09))
        specs = [(norm([1.0, 0.16, 0.18]), 4.8, 0.22), (norm([-0.6, 0.82, 0.22]), 3.1, 0.17),
                 (norm([0.08, 0.9, -0.62]), 2.7, 0.15)]
        for li, (d0, L, r0) in enumerate(specs):
            base = list(self.top)
            segs = []
            self._grow(segs, base, d0, L, r0, level=0)
            self.limbs.append((base, d0, segs))

    def _grow(self, segs, a, d, L, r, level):
        rnd = self.rnd
        n = 3 if level == 0 else 2
        p = a
        rr = r
        for k in range(n):
            j = [rnd.uniform(-0.2, 0.2), rnd.uniform(-0.06, 0.14), rnd.uniform(-0.2, 0.2)]
            d = norm(add(d, j))
            q = add(p, mul(d, L / n))
            r1 = rr * 0.72
            segs.append((p, q, (rr + r1) / 2))
            # side branches from along this segment
            if level < 2 and k >= (1 if level == 0 else 0):
                for _b in range(2 if level == 0 else 1):
                    side = norm([rnd.uniform(-1, 1), rnd.uniform(0.25, 0.9), rnd.uniform(-1, 1)])
                    sd = norm(add(mul(d, 0.55), side))
                    self._grow(segs, lerp(p, q, rnd.uniform(0.4, 0.9)), sd, L * (0.42 if level == 0 else 0.5),
                               r1 * 0.75, level + 1)
            p, rr = q, r1
        if level == 2:
            return

    # -- SDF objects -----------------------------------------------------------------------------------------------

    def _wrap(self, body, name):
        eaten = sdf("displaceField", [body], amount=0.0, reference="rot", name="eaten")
        return {"root": sdf("displaceNoise", [eaten], amount=0.016, frequency=7.0, speed=0.0, seed=3, name="bark")}

    def trunk_tree(self):
        strands = chunked([capsule_between(a, b, r) for (a, b, r) in self.trunk], "smoothUnion", 0.16)
        roots = chunked([capsule_between(a, b, r) for (a, b, r) in self.roots], "smoothUnion", 0.12)
        body = sdf("smoothUnion", [strands, roots], smooth=0.25)
        return self._wrap(body, "trunk")

    def limb_tree(self, li):
        base, d0, segs = self.limbs[li]
        R = frame_x_to(d0)
        local = [capsule_between(mat_t_vec(R, sub(a, base)), mat_t_vec(R, sub(b, base)), r) for (a, b, r) in segs]
        bent = sdf("bend", [chunked(local, "smoothUnion", 0.1)], amount=0.0, name="melt")
        oriented = sdf("rotate", [bent], rotation=euler_from_matrix(R))
        return self._wrap(oriented, f"limb{li}")

    def limb_bounds(self, li):
        base, _d, segs = self.limbs[li]
        pts = [mul(sub(a, base), 1) for (a, b, r) in segs] + [sub(b, base) for (a, b, r) in segs]
        lo = [min(p[i] for p in pts) - 0.6 for i in range(3)]
        hi = [max(p[i] for p in pts) + 0.6 for i in range(3)]
        return lo, hi

    # -- the voxel shell ---------------------------------------------------------------------------------------------

    def caps(self):
        out = list(self.trunk) + list(self.roots)
        for _b, _d, segs in self.limbs:
            out += segs
        return out

    def blocks(self, cell):
        caps = self.caps()

        def dist(p):
            if p[1] < -0.05:
                return 1e9  # nothing below the ground
            d = 1e9
            for (a, b, r) in caps:
                d = min(d, capsule_distance(p, a, b, r))
            return d

        pts = voxel_shell(dist, [-3, -0.2, -3.5], [9, 9, 4], cell)
        s = cell * 0.94
        return [[round(x, 4), round(y, 4), round(z, 4), 0, 0, 0, 1, s, s, s] for (x, y, z) in pts]


# ---------------------------------------------------------------------------------------------------------- Tanguy
# Not a pebble: a Tanguy presence. A smooth bone-pale mass pierced by a hole, a slender filament rising from it to a
# small balanced sphere, and a needle pointing down that stops a hand's width above its own shadow (S8, S11).
TANGUY_BODY = [([0.0, 0.0, 0.0], 0.95), ([0.72, -0.12, 0.18], 0.66), ([-0.68, -0.22, -0.2], 0.5),
               ([0.1, 0.55, -0.05], 0.48)]
TANGUY_HOLE = ([0.15, 0.05, 0.0], [0.0, 0.0, 1.0], 0.34)      # centre, axis, radius
TANGUY_FILAMENT = [([0.1, 0.95, -0.05], [0.35, 1.9, 0.1], 0.07), ([0.35, 1.9, 0.1], [0.2, 2.65, 0.0], 0.045)]
TANGUY_BEAD = ([0.2, 2.8, 0.0], 0.16)
TANGUY_NEEDLE = ([0.0, -0.75, 0.0], [0.05, -1.55, 0.02], 0.05)


def tanguy_tree():
    body = sdf("smoothUnion", [sdf("translate", [sdf("sphere", radius=r)], translation=c) for c, r in TANGUY_BODY],
               smooth=0.55)
    c, axis, r = TANGUY_HOLE
    hole = sdf("translate", [sdf("rotate", [sdf("cylinder", radius=r, height=4.0)], rotation=[90, 0, 0])],
               translation=c)
    pierced = sdf("smoothDifference", [body, hole], smooth=0.18)
    filament = [capsule_between(a, b, rr) for a, b, rr in TANGUY_FILAMENT]
    bead = sdf("translate", [sdf("sphere", radius=TANGUY_BEAD[1])], translation=TANGUY_BEAD[0])
    a, b, rr = TANGUY_NEEDLE
    needle = capsule_between(a, b, rr)
    whole = sdf("smoothUnion", [pierced, filament[0], filament[1], needle], smooth=0.12)
    form = sdf("union", [whole, bead])
    return {"root": sdf("displaceField", [form], amount=0.0, reference="rot", name="eaten")}


def tanguy_distance(p):
    d = None
    for c, r in TANGUY_BODY:
        e = math.sqrt(dot(sub(p, c), sub(p, c))) - r
        d = e if d is None else smin(d, e, 0.55)
    c, _axis, r = TANGUY_HOLE
    q = sub(p, c)
    hole = math.sqrt(q[0] ** 2 + q[1] ** 2) - r
    d = max(d, -hole)
    for a, b, rr in TANGUY_FILAMENT:
        d = smin(d, capsule_distance(p, a, b, rr), 0.12)
    a, b, rr = TANGUY_NEEDLE
    d = smin(d, capsule_distance(p, a, b, rr), 0.12)
    c, r = TANGUY_BEAD
    return min(d, math.sqrt(dot(sub(p, c), sub(p, c))) - r)


def tanguy_blocks(cell):
    pts = voxel_shell(tanguy_distance, [-1.6, -1.7, -1.4], [1.6, 3.1, 1.4], cell)
    s = cell * 0.94
    return [[round(x, 4), round(y, 4), round(z, 4), 0, 0, 0, 1, s, s, s] for (x, y, z) in pts]

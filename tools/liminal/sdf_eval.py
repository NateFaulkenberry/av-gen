"""A small CPU evaluator for the SDF trees the pass 2 generator writes (the node kinds kit.py uses), for checks
that need no GPU: does a shot's eye pass through furniture? It follows docs/sdf.md's formulas; displacement
nodes and screws are evaluated as their child (a bound is enough for a clearance check)."""

from __future__ import annotations

import math


def _rot_matrix(deg):
    x, y, z = (math.radians(v) for v in deg)
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    # R = Rz Ry Rx
    rx = [[1, 0, 0], [0, cx, -sx], [0, sx, cx]]
    ry = [[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]
    rz = [[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]

    def mm(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return mm(mm(rz, ry), rx)


def _inv_rot(m, p):
    # conj(R) p = R^T p
    return [m[0][0] * p[0] + m[1][0] * p[1] + m[2][0] * p[2],
            m[0][1] * p[0] + m[1][1] * p[1] + m[2][1] * p[2],
            m[0][2] * p[0] + m[1][2] * p[1] + m[2][2] * p[2]]


def _box(p, h):
    q = [abs(p[i]) - h[i] for i in range(3)]
    out = math.sqrt(sum(max(v, 0.0) ** 2 for v in q))
    return out + min(max(q), 0.0)


def evaluate(n, p):
    k = n["kind"]
    if k == "box":
        return _box(p, n["size"])
    if k == "roundedBox":
        r = n.get("rounding", 0.0)
        h = [s - r for s in n["size"]]
        return _box(p, h) - r
    if k == "sphere":
        return math.sqrt(sum(v * v for v in p)) - n["radius"]
    if k == "cylinder":
        h = n["height"] / 2
        d0 = math.hypot(p[0], p[2]) - n["radius"]
        d1 = abs(p[1]) - h
        return min(max(d0, d1), 0.0) + math.hypot(max(d0, 0.0), max(d1, 0.0))
    if k == "capsule":
        h = n["height"] / 2
        y = p[1] - max(-h, min(h, p[1]))
        return math.sqrt(p[0] ** 2 + y ** 2 + p[2] ** 2) - n["radius"]
    if k == "torus":
        q = math.hypot(p[0], p[2]) - n["radius"]
        return math.hypot(q, p[1]) - n["rounding"]
    if k == "cone":
        # a conservative bound: the cone's bounding cylinder
        h = n["height"] / 2
        d0 = math.hypot(p[0], p[2]) - n["radius"]
        d1 = abs(p[1]) - h
        return min(max(d0, d1), 0.0) + math.hypot(max(d0, 0.0), max(d1, 0.0))
    if k == "plane":
        a = n["axis"]
        L = math.sqrt(sum(v * v for v in a)) or 1.0
        return sum(p[i] * a[i] / L for i in range(3)) - n.get("offset", 0.0)
    if k == "stairs":
        run, rise, hw = n["size"]
        cnt = n.get("count", 1)
        # a bound: the flight's bounding wedge (box under the slope)
        x, y, z = p
        top = rise * min(max(math.floor(x / run) + 1, 1), cnt)
        dz = abs(z) - hw
        inside_x = -x if x < 0 else (x - cnt * run if x > cnt * run else max(-x, x - cnt * run))
        dy = y - top
        dist = max(dy, inside_x, dz)
        return dist if dist > 0 else dist
    kids = n.get("children", [])
    if k == "union":
        return min(evaluate(c, p) for c in kids)
    if k == "intersection":
        return max(evaluate(c, p) for c in kids)
    if k == "difference":
        d = evaluate(kids[0], p)
        for c in kids[1:]:
            d = max(d, -evaluate(c, p))
        return d
    if k == "translate":
        t = n["translation"]
        return evaluate(kids[0], [p[0] - t[0], p[1] - t[1], p[2] - t[2]])
    if k == "rotate":
        return evaluate(kids[0], _inv_rot(_rot_matrix(n["rotation"]), p))
    if k == "scale":
        s = n["scale"]
        return evaluate(kids[0], [v / s for v in p]) * s
    if k == "mirror":
        m = n["size"]
        return evaluate(kids[0], [abs(p[i]) if m[i] > 0 else p[i] for i in range(3)])
    if k == "repeat":
        size, cnt = n["size"], n.get("count", 0)
        q = list(p)
        for i in range(3):
            if size[i] > 0:
                r = math.floor(p[i] / size[i] + 0.5)
                if cnt > 0:
                    r = max(-cnt, min(cnt, r))
                q[i] = p[i] - size[i] * r
        return evaluate(kids[0], q)
    if k == "polarRepeat":
        c = n["count"]
        if c <= 0:
            return evaluate(kids[0], p)
        sector = 2 * math.pi / c
        a = math.atan2(p[2], p[0])
        a2 = a - sector * math.floor(a / sector + 0.5)
        r = math.hypot(p[0], p[2])
        return evaluate(kids[0], [math.cos(a2) * r, p[1], math.sin(a2) * r])
    if k == "shell":
        return abs(evaluate(kids[0], p)) - n["offset"] / 2
    if k in ("displaceWave", "displaceNoise", "warp", "screw"):
        return evaluate(kids[0], p)
    raise ValueError(k)

"""Rainbow World: the knot's centreline, built EXACTLY as AV Gen builds a spline.

The band's frame and the camera's up must agree (ADR-1541: the camera's up is the spline's normal). So the band is
swept along the very frames AV Gen computes for the same spline: a closed Catmull-Rom (tension 0.5) through the
control points, sampled `samplesPerSegment` per segment, rotation-minimising normals by double reflection (Wang et al.
2008), the seam mismatch spread linearly by distance, then each sample's interpolated `roll` (src/spatial/spline.cpp,
`ensureCache`). This module is that code in Python (numpy), so the frames here and in the engine are the same frames.

Pure Python + numpy (no bpy), so the tests and the scene builder can import it anywhere.
"""
import json
import math

import numpy as np

TENSION = 0.5
SAMPLES_PER_SEGMENT = 24


def trefoil_controls(n=72, scale=260.0, z_amp=0.8, wobble=None):
    """Control points of a (2,3) torus knot, z squashed, in Blender's frame (Z up). `wobble` (optional) is a
    function u -> (dx, dy, dz) in metres for hand-shaping."""
    pts = []
    for i in range(n):
        u = i / n
        a = 2 * math.pi * u
        r = 2.0 + math.cos(3 * a)
        p = np.array([r * math.cos(2 * a), r * math.sin(2 * a), -z_amp * math.sin(3 * a)]) * scale
        if wobble is not None:
            p = p + np.array(wobble(u))
        pts.append(p)
    return np.array(pts)


def _hermite(p0, m0, p1, m1, u):
    u2, u3 = u * u, u * u * u
    h00, h10, h01, h11 = 2 * u3 - 3 * u2 + 1, u3 - 2 * u2 + u, -2 * u3 + 3 * u2, u3 - u2
    d00, d10, d01, d11 = 6 * u2 - 6 * u, 3 * u2 - 4 * u + 1, -6 * u2 + 6 * u, 3 * u2 - 2 * u
    return h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1, d00 * p0 + d10 * m0 + d01 * p1 + d11 * m1


def _perp(t, ref):
    p = ref - t * np.dot(ref, t)
    n = np.linalg.norm(p)
    if n > 1e-5:
        return p / n
    a = np.array([1.0, 0, 0]) if abs(t[0]) < 0.9 else np.array([0, 1.0, 0])
    p = a - t * np.dot(a, t)
    return p / np.linalg.norm(p)


def _rotate_about(v, axis, ang):
    c, s = math.cos(ang), math.sin(ang)
    return v * c + np.cross(axis, v) * s + axis * (np.dot(axis, v) * (1 - c))


class Path:
    """A closed AV Gen spline's sample table: positions, unit tangents, normals (with roll), binormals, distance.
    Coordinates are in the frame the controls were given in (Blender, Z up); `up` likewise."""

    def __init__(self, controls, rolls=None, up=(0.0, 0.0, 1.0), per_segment=SAMPLES_PER_SEGMENT, roll_turns=0):
        self.controls = np.asarray(controls, float)
        n = len(self.controls)
        self.rolls = np.zeros(n) if rolls is None else np.asarray(rolls, float)
        self.up = np.asarray(up, float)
        self.per = per_segment
        self.roll_turns = int(roll_turns)
        segs = n
        total = segs * per_segment + 1
        P = np.zeros((total, 3))
        T = np.zeros((total, 3))
        R = np.zeros(total)
        for k in range(total):
            seg, j = divmod(k, per_segment)
            if seg >= segs:
                seg, j = segs - 1, per_segment
            u = j / per_segment
            a, b = self.controls[seg % n], self.controls[(seg + 1) % n]
            prev, nxt = self.controls[(seg - 1) % n], self.controls[(seg + 2) % n]
            m0 = (b - prev) * TENSION
            m1 = (nxt - a) * TENSION
            P[k], T[k] = _hermite(a, m0, b, m1, u)
            rb = self.rolls[(seg + 1) % n] + (2 * math.pi * self.roll_turns if seg == n - 1 else 0.0)  # ADR-1542
            R[k] = self.rolls[seg % n] + (rb - self.rolls[seg % n]) * u
        last = np.array([0, 0, 1.0])
        for k in range(total):
            t = T[k]
            if np.linalg.norm(t) <= 1e-6:
                t = P[min(k + 1, total - 1)] - P[k]
            nt = np.linalg.norm(t)
            T[k] = t / nt if nt > 1e-6 else last
            last = T[k]
        D = np.zeros(total)
        D[1:] = np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))
        length = D[-1]
        N = np.zeros((total, 3))
        N[0] = _perp(T[0], self.up)
        for k in range(total - 1):
            v1 = P[k + 1] - P[k]
            c1 = np.dot(v1, v1)
            rL, tL = N[k], T[k]
            if c1 > 1e-12:
                rL = N[k] - v1 * (2.0 / c1 * np.dot(v1, N[k]))
                tL = T[k] - v1 * (2.0 / c1 * np.dot(v1, T[k]))
            v2 = T[k + 1] - tL
            c2 = np.dot(v2, v2)
            r = rL
            if c2 > 1e-12:
                r = rL - v2 * (2.0 / c2 * np.dot(v2, rL))
            N[k + 1] = _perp(T[k + 1], r if np.linalg.norm(r) > 1e-5 else N[k])
        # seam correction (closed)
        target = _perp(T[-1], N[0])
        ang = math.atan2(np.dot(np.cross(N[-1], target), T[-1]), np.dot(N[-1], target))
        self.seam_angle = ang
        if abs(ang) > 1e-7:
            for k in range(1, total):
                N[k] = _perp(T[k], _rotate_about(N[k], T[k], ang * (D[k] / length)))
        self.rmf = N.copy()  # before roll
        for k in range(total):
            if R[k] != 0.0:
                N[k] = _perp(T[k], _rotate_about(N[k], T[k], R[k]))
        B = np.cross(N, T)
        self.P, self.T, self.N, self.B, self.D, self.length = P, T, N, B, D, length

    def at(self, s):
        """Interpolated (position, tangent, normal, binormal) at arc length s (wrapped)."""
        s = s % self.length
        k = int(np.searchsorted(self.D, s, side="right") - 1)
        k = max(0, min(k, len(self.D) - 2))
        f = (s - self.D[k]) / max(1e-9, self.D[k + 1] - self.D[k])
        p = self.P[k] * (1 - f) + self.P[k + 1] * f
        t = self.T[k] * (1 - f) + self.T[k + 1] * f
        t /= np.linalg.norm(t)
        n = _perp(t, self.N[k] * (1 - f) + self.N[k + 1] * f)
        return p, t, n, np.cross(n, t)

    def to_avgen_json(self, name, to_avgen):
        """The spline node for an AV Gen scene file; positions converted to Y-up by `to_avgen`."""
        return {"name": name, "kind": "spline", "spline": {
            "name": name, "kind": "catmullRom", "closed": True, "tension": TENSION, "generator": "points",
            "samplesPerSegment": self.per, "up": [round(v, 6) for v in to_avgen(self.up)],
            **({"rollTurns": self.roll_turns} if self.roll_turns else {}),
            "points": [{"position": [round(v, 4) for v in to_avgen(p)], "roll": round(float(r), 6), "scale": 1.0}
                       for p, r in zip(self.controls, self.rolls)]}}


def rolls_to_follow(controls, desired_up_at_controls, up=(0.0, 0.0, 1.0)):
    """Per-control-point rolls (radians, unwrapped) that turn the spline's corrected RMF normal onto the desired
    up vectors at the control points, and the whole turns they wind round the loop (ADR-1542). Returns
    (rolls, path)."""
    base = Path(controls, None, up)
    n = len(controls)
    rolls = []
    prev = None
    for i in range(n):
        k = i * base.per
        t, nrm = base.T[k], base.rmf[k]
        want = _perp(t, np.asarray(desired_up_at_controls[i], float))
        a = math.atan2(np.dot(np.cross(nrm, want), t), np.dot(nrm, want))
        if prev is not None:
            while a - prev > math.pi:
                a -= 2 * math.pi
            while a - prev < -math.pi:
                a += 2 * math.pi
        rolls.append(a)
        prev = a
    # the seam: how far past the first roll the last one has wound, in whole turns
    nxt = rolls[0]
    while nxt - prev > math.pi:
        nxt -= 2 * math.pi
    while nxt - prev < -math.pi:
        nxt += 2 * math.pi
    turns = int(round((nxt - rolls[0]) / (2 * math.pi)))
    return np.array(rolls), Path(controls, rolls, up, roll_turns=turns)


if __name__ == "__main__":
    c = trefoil_controls()
    p = Path(c)
    print("length %.1f m, seam angle %.3f rad" % (p.length, p.seam_angle))
    print(json.dumps(p.to_avgen_json("knot", lambda v: (v[0], v[2], -v[1])))[:200])

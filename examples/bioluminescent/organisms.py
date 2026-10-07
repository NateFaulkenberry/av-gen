"""The Rift's organisms as generated geometry (numpy), one GLB per part so a scene can give each part its own material:
a dark body part and an emitting part (photophores, polyps, chains) share the same placements.

UV convention for every part: v = position along the organism from its root (0) to its farthest tip (1), so a
material program can send light out along an arm or up a stalk; u = a per-element hash in [0, 1) (bead / leaf /
branch identity), so a program can make individual photophores differ.

The species are abyssal fauna re-imagined as a land ecosystem in a drained trench (02-art-direction.md):
  crinoid   giant sea lily, 20-45 m: the canopy. Ribbed stalk, calyx, ten arms forking once, feathered with pinnules,
            photophores along the arms, siphonophore chains hanging from the arm tips
  seapen    1-2 m feather on a stalk: the meadow. Waves of light run up it (real sea pens do this)
  fan       gorgonian sea fan, 1-4 m: a cupped planar lattice of branches; polyps glow along it
  whips     a tuft of 7-15 long filaments with luminous tips
  mat       a 1.2 m cushion of polyps: one instance is ~90 micro-organisms (perceived density at low instance cost)
  lanterns  3-7 translucent pods on curved stalks: the rare, warm, brightest organisms
  comb      a floating comb jelly: 8 iridescent comb rows on a clear body
"""
from __future__ import annotations

import math
from pathlib import Path

import numpy as np

from glb import write_glb

OUT = Path(__file__).resolve().parent / "meshes"


class Mesh:
    def __init__(self):
        self.p, self.n, self.uv, self.i = [], [], [], []
        self.count = 0

    def add(self, p, n, uv, i):
        self.p.append(np.asarray(p, np.float32))
        self.n.append(np.asarray(n, np.float32))
        self.uv.append(np.asarray(uv, np.float32))
        self.i.append(np.asarray(i, np.uint32) + self.count)
        self.count += len(p)

    def write(self, name):
        OUT.mkdir(parents=True, exist_ok=True)
        if not self.p:
            raise ValueError(f"{name}: empty")
        tris = write_glb(OUT / f"{name}.glb", np.concatenate(self.p), np.concatenate(self.n),
                         np.concatenate(self.uv), np.concatenate(self.i), name=name)
        print(f"  {name}: {self.count} verts, {tris} tris")
        return tris


# ---- primitives -----------------------------------------------------------------------------------------------------

def _frames(pts):
    """Parallel-transport frames along a polyline: tangents, normals, binormals."""
    t = np.gradient(pts, axis=0)
    t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-9)
    ref = np.array([1.0, 0.0, 0.0]) if abs(t[0][1]) > 0.9 else np.array([0.0, 1.0, 0.0])
    n0 = np.cross(t[0], ref)
    n0 /= np.linalg.norm(n0)
    ns = [n0]
    for k in range(1, len(pts)):
        n = ns[-1] - np.dot(ns[-1], t[k]) * t[k]
        ln = np.linalg.norm(n)
        if ln < 1e-6:
            n = ns[-1]
        else:
            n /= ln
        ns.append(n)
    ns = np.array(ns)
    bs = np.cross(t, ns)
    return t, ns, bs


def tube(mesh, pts, radii, sides, v0=0.0, v1=1.0, u=0.0, cap=False, flatten=1.0):
    """Sweep a circle (or an ellipse squashed by `flatten` along the binormal) along `pts`."""
    pts = np.asarray(pts, float)
    radii = np.broadcast_to(np.asarray(radii, float), (len(pts),))
    t, ns, bs = _frames(pts)
    ang = np.linspace(0, 2 * math.pi, sides, endpoint=False)
    ca, sa = np.cos(ang), np.sin(ang)
    P = pts[:, None, :] + radii[:, None, None] * (ca[None, :, None] * ns[:, None, :] +
                                                  flatten * sa[None, :, None] * bs[:, None, :])
    N = ca[None, :, None] * ns[:, None, :] + (sa[None, :, None] / max(flatten, 1e-3)) * bs[:, None, :]
    vs = np.linspace(v0, v1, len(pts))
    UV = np.stack([np.full((len(pts), sides), u), np.repeat(vs[:, None], sides, 1)], -1)
    idx = []
    for a in range(len(pts) - 1):
        for b in range(sides):
            c = (b + 1) % sides
            p00, p01, p10, p11 = a * sides + b, a * sides + c, (a + 1) * sides + b, (a + 1) * sides + c
            idx += [p00, p10, p01, p01, p10, p11]
    P, N, UV = P.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2)
    if cap:  # a pointed tip vertex closing the end
        tip = len(P)
        P = np.vstack([P, pts[-1] + t[-1] * radii[-1]])
        N = np.vstack([N, t[-1]])
        UV = np.vstack([UV, [u, v1]])
        base = (len(pts) - 1) * sides
        for b in range(sides):
            idx += [base + b, tip, base + (b + 1) % sides]
    mesh.add(P, N, UV, idx)


_ICO = None


def _icosphere():
    global _ICO
    if _ICO is None:
        g = (1 + 5 ** 0.5) / 2
        v = np.array([[-1, g, 0], [1, g, 0], [-1, -g, 0], [1, -g, 0], [0, -1, g], [0, 1, g], [0, -1, -g], [0, 1, -g],
                      [g, 0, -1], [g, 0, 1], [-g, 0, -1], [-g, 0, 1]], float)
        v /= np.linalg.norm(v, axis=1, keepdims=True)
        f = np.array([[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2],
                      [10, 7, 6], [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5],
                      [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]])
        _ICO = (v, f)
    return _ICO


def blob(mesh, centre, radii, v=0.0, u=0.0, axis=None, rings=0):
    """A bead or a pod: an icosahedron (rings=0) or a UV ellipsoid (rings>0) with radii along (x, y, z); `axis`
    rotates its y onto that direction."""
    if rings:
        segs = rings * 2
        th = np.linspace(0, math.pi, rings + 1)
        ph = np.linspace(0, 2 * math.pi, segs, endpoint=False)
        T, PH = np.meshgrid(th, ph, indexing="ij")
        unit = np.stack([np.sin(T) * np.cos(PH), np.cos(T), np.sin(T) * np.sin(PH)], -1).reshape(-1, 3)
        idx = []
        for a in range(rings):
            for b in range(segs):
                c = (b + 1) % segs
                idx += [a * segs + b, a * segs + c, (a + 1) * segs + b, a * segs + c, (a + 1) * segs + c,
                        (a + 1) * segs + b]
    else:
        unit, f = _icosphere()
        idx = f.reshape(-1).tolist()
    r = np.asarray(radii, float)
    P = unit * r
    N = unit / np.maximum(r, 1e-6)
    if axis is not None:
        y = np.asarray(axis, float)
        y /= np.linalg.norm(y)
        ref = np.array([1.0, 0, 0]) if abs(y[0]) < 0.9 else np.array([0, 0, 1.0])
        x = np.cross(y, ref)
        x /= np.linalg.norm(x)
        z = np.cross(x, y)
        R = np.stack([x, y, z], 1)
        P, N = P @ R.T, N @ R.T
    UV = np.tile([u, v], (len(P), 1))
    mesh.add(P + np.asarray(centre, float), N, UV, idx)


def ribbon(mesh, pts, widths, side, v0, v1, u=0.0):
    """A two-sided strip along `pts`, widening along `side` (a leaf, a polyp fin)."""
    pts = np.asarray(pts, float)
    w = np.broadcast_to(np.asarray(widths, float), (len(pts),))
    side = np.asarray(side, float)
    side /= np.linalg.norm(side)
    t = np.gradient(pts, axis=0)
    nrm = np.cross(t, side)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
    vs = np.linspace(v0, v1, len(pts))
    for s in (1, -1):
        P = np.concatenate([pts - side * w[:, None] * 0.5, pts + side * w[:, None] * 0.5])
        N = np.concatenate([nrm, nrm]) * s
        UV = np.concatenate([np.stack([np.full(len(pts), u), vs], 1)] * 2)
        k = len(pts)
        idx = []
        for a in range(k - 1):
            q = [a, a + 1, k + a, k + a + 1]
            idx += [q[0], q[2], q[1], q[1], q[2], q[3]] if s > 0 else [q[0], q[1], q[2], q[1], q[3], q[2]]
        mesh.add(P, N, UV, idx)


def curve(p0, d0, length, n, bend=np.zeros(3), wobble=0.0, rng=None, freq=1.0):
    """A curve from p0 starting along d0; `bend` is a constant curvature pull (a vector added to the direction per
    metre), `wobble` a smooth random lateral drift."""
    p = np.array(p0, float)
    d = np.array(d0, float)
    d /= np.linalg.norm(d)
    step = length / (n - 1)
    pts = [p.copy()]
    ph = rng.uniform(0, 6.28, 3) if rng is not None else np.zeros(3)
    for k in range(1, n):
        s = k * step
        d = d + np.asarray(bend) * step
        if wobble:
            d = d + wobble * step * np.sin(freq * s + ph)
        d /= np.linalg.norm(d)
        p = p + d * step
        pts.append(p.copy())
    return np.array(pts)


# ---- species --------------------------------------------------------------------------------------------------------

def crinoid(seed, height=26.0):
    rng = np.random.default_rng(seed)
    stalk, arms, beads, chains = Mesh(), Mesh(), Mesh(), Mesh()
    # stalk: a gentle S, ribbed by its columnals
    n = 120
    s = np.linspace(0, 1, n)
    lean = rng.uniform(1.5, 3.5) * np.array([math.cos(seed), 0, math.sin(seed)])
    pts = np.stack([lean[0] * (s ** 2) + 0.6 * np.sin(s * 3.1 + seed),
                    s * height,
                    lean[2] * (s ** 2) + 0.6 * np.cos(s * 2.7 + seed)], 1)
    base_r = 0.5 * height / 26.0
    radii = base_r * (1.0 - 0.45 * s) * (1.0 + 0.10 * np.abs(np.sin(s * height / 0.55 * math.pi)))
    radii[:4] *= np.array([2.2, 1.7, 1.35, 1.12])  # holdfast flare
    tube(stalk, pts, radii, 12, 0.0, 0.25)
    top = pts[-1]
    # calyx: a cup of plates
    blob(stalk, top + [0, 0.5, 0], [1.1, 0.9, 1.1], v=0.27, rings=6)
    # cirri: whorls of small hooked side-arms down the stalk (real crinoids carry them)
    for k in range(10, n - 8, 9):
        for a in range(5):
            ang = a * 2 * math.pi / 5 + k * 0.7
            d = np.array([math.cos(ang), -0.15, math.sin(ang)])
            c = curve(pts[k], d, 0.9 * base_r * 3, 6, bend=np.array([0, -1.6, 0]))
            tube(stalk, c, np.linspace(0.05, 0.015, 6) * base_r / 0.5, 4, 0.02 + s[k] * 0.2, 0.03 + s[k] * 0.2)
    # ten arms, each forking once, arching out and drooping; pinnules feather both sides
    arm_tips = []
    na = 10
    for a in range(na):
        ang = a * 2 * math.pi / na + rng.uniform(-0.12, 0.12)
        out = np.array([math.cos(ang), 0, math.sin(ang)])
        L1 = rng.uniform(2.0, 3.0)
        c1 = curve(top + [0, 0.8, 0], out * 0.8 + [0, 1.0, 0], L1, 8, bend=np.array([0, -0.25, 0]))
        tube(arms, c1, np.linspace(0.16, 0.12, 8), 6, 0.28, 0.4, u=a / na)
        for fork in (-1, 1):
            side = np.cross(out, [0, 1, 0]) * fork
            d = out * 0.9 + [0, 0.55, 0] + side * 0.35
            L = rng.uniform(7.0, 10.0)
            c2 = curve(c1[-1], d, L, 26, bend=np.array([0, -0.33, 0]) + out * 0.02, wobble=0.04, rng=rng, freq=0.8)
            tube(arms, c2, np.linspace(0.12, 0.035, 26), 5, 0.4, 1.0, u=(a + 0.5 * (fork + 1) * 0.5) / na)
            arm_tips.append(c2[-1])
            t, ns_, bs_ = _frames(c2)
            for k in range(1, 25):
                frac = k / 25
                plen = 0.75 * (1 - 0.6 * frac)
                for sgn in (1, -1):
                    pd = bs_[k] * sgn * 0.8 + t[k] * 0.45 + np.array([0, -0.25, 0])
                    pc = curve(c2[k], pd, plen, 4, bend=np.array([0, -0.6, 0]))
                    v = 0.4 + 0.6 * frac
                    tube(arms, pc, np.linspace(0.025, 0.008, 4), 3, v, v, u=rng.random())
                    if k % 2 == 0:
                        blob(beads, pc[-1], [0.05, 0.05, 0.05], v=v, u=rng.random())
                if k % 3 == 0:
                    blob(beads, c2[k] + ns_[k] * 0.1, [0.075, 0.075, 0.075], v=0.4 + 0.6 * frac, u=rng.random())
    # siphonophore chains hanging from half the arm tips: beads and small bells, swaying
    for tip in arm_tips[::2]:
        L = rng.uniform(4.0, 12.0)
        m = int(L / 0.22)
        drift = rng.normal(0, 0.15, 3) * [1, 0, 1]
        for k in range(m):
            f = k / max(m - 1, 1)
            p = tip + np.array([0.35 * math.sin(f * 4 + seed) * f, -k * 0.22, 0.35 * math.cos(f * 3 + seed) * f]) + \
                drift * f * L
            r = 0.04 + 0.05 * (k % 4 == 0)
            if k % 7 == 3:
                blob(chains, p, [0.11, 0.08, 0.11], v=f, u=rng.random(), rings=4)
            else:
                blob(chains, p, [r, r * 1.3, r], v=f, u=rng.random())
    return {"crinoid_stalk": stalk, "crinoid_arms": arms, "crinoid_beads": beads, "crinoid_chains": chains}


def seapen(seed, height=1.7):
    rng = np.random.default_rng(seed)
    body = Mesh()
    lean = rng.normal(0, 0.06, 3) * [1, 0, 1]
    n = 24
    s = np.linspace(0, 1, n)
    pts = np.stack([lean[0] * s * s * height, s * height, lean[2] * s * s * height], 1)
    tube(body, pts, 0.028 * (1 - 0.6 * s) + 0.004, 6, 0.0, 1.0)
    # bulb at the foot (the peduncle)
    blob(body, pts[0] + [0, 0.05, 0], [0.06, 0.09, 0.06], v=0.0, rings=4)
    pairs = 20
    for k in range(pairs):
        f = 0.25 + 0.73 * k / (pairs - 1)
        p = pts[0] + (pts[-1] - pts[0]) * f
        L = 0.24 * math.sin(math.pi * min(1.0, (f - 0.2) / 0.85)) + 0.04
        for sgn in (1, -1):
            ang = k * 0.21 + (0 if sgn > 0 else math.pi)
            out = np.array([math.cos(ang), 0, math.sin(ang)])
            c = curve(p, out + [0, 0.9, 0], L, 5, bend=np.array([0, 0.6, 0]))
            side = np.cross(out, [0, 1, 0])
            ribbon(body, c, np.linspace(0.02, 0.07, 5), side + [0, 0.3, 0], f, f + 0.02, u=rng.random())
    return {"seapen": body}


def fan(seed, height=2.6):
    rng = np.random.default_rng(seed)
    body, polyps = Mesh(), Mesh()
    cup = rng.uniform(0.08, 0.18)

    def bend_z(p):
        return p + np.array([0, 0, cup * p[0] ** 2 + 0.05 * math.sin(p[1] * 2.3 + seed)])

    def branch(p, ang, length, radius, depth, v):
        d = np.array([math.sin(ang), math.cos(ang), 0.0])
        nseg = 6
        pts = np.array([bend_z(p + d * length * k / (nseg - 1) + np.array(
            [0.04 * math.sin(k + depth * 3 + seed), 0, 0])) for k in range(nseg)])
        v1 = min(1.0, v + length / height)
        tube(body, pts, np.linspace(radius, radius * 0.75, nseg), 4 if depth > 2 else 6, v, v1, u=rng.random())
        if depth >= 2:
            for k in range(1, nseg):
                if rng.random() < 0.8:
                    blob(polyps, pts[k] + rng.normal(0, 0.012, 3), [0.016, 0.016, 0.016],
                         v=v + (v1 - v) * k / nseg, u=rng.random())
        end = p + d * length
        if depth < 5 and radius > 0.006:
            spread = rng.uniform(0.25, 0.5)
            for sgn in (-1, 1):
                branch(end, ang + sgn * spread + rng.normal(0, 0.08), length * rng.uniform(0.62, 0.8),
                       radius * 0.72, depth + 1, v1)
        else:
            blob(polyps, bend_z(end), [0.022, 0.022, 0.022], v=v1, u=rng.random())

    trunk_len = height * 0.18
    tube(body, [[0, 0, 0], [0, trunk_len * 0.5, 0], [0, trunk_len, 0]], [0.07, 0.055, 0.05], 6, 0, 0.1)
    for a in (-0.5, -0.15, 0.2, 0.55):
        branch(np.array([0, trunk_len, 0]), a + rng.normal(0, 0.05), height * 0.28, 0.035, 0, 0.1)
    return {"fan_body": body, "fan_polyps": polyps}


def whips(seed):
    rng = np.random.default_rng(seed)
    body, tips = Mesh(), Mesh()
    k = rng.integers(7, 15)
    for j in range(k):
        ang = rng.uniform(0, 2 * math.pi)
        out = np.array([math.cos(ang), 0, math.sin(ang)])
        L = rng.uniform(1.2, 4.0)
        base = out * rng.uniform(0, 0.25)
        c = curve(base, out * rng.uniform(0.1, 0.45) + [0, 1, 0], L, 14, bend=out * rng.uniform(0.05, 0.25) +
                  np.array([0, -0.06, 0]), wobble=0.08, rng=rng)
        tube(body, c, np.linspace(0.022, 0.006, 14), 4, 0, 1, u=j / k)
        blob(tips, c[-1], [0.035, 0.05, 0.035], v=1.0, u=rng.random())
        for q in (10, 11, 12):
            blob(tips, c[q], [0.018, 0.018, 0.018], v=q / 13, u=rng.random())
    return {"whips_body": body, "whips_tips": tips}


def mat(seed):
    """A cushion of polyps, ~1.2 m: one instance reads as ~90 small organisms."""
    rng = np.random.default_rng(seed)
    cushion, polyps = Mesh(), Mesh()
    # the cushion: a squashed, lumpy dome
    rings, segs = 6, 14
    for lump in range(3):
        c = rng.normal(0, 0.18, 3) * [1, 0, 1]
        blob(cushion, c, [rng.uniform(0.35, 0.55), rng.uniform(0.07, 0.14), rng.uniform(0.35, 0.55)], v=0.0,
             rings=5)
    for q in range(90):
        r = 0.55 * math.sqrt(rng.random())
        a = rng.uniform(0, 2 * math.pi)
        x, z = r * math.cos(a), r * math.sin(a)
        h = 0.12 * (1 - (r / 0.6) ** 2) + rng.uniform(0.0, 0.07)
        stalk_top = np.array([x, h, z])
        tube(cushion, [[x, max(h - 0.08, 0), z], [x * 1.02, h - 0.03, z * 1.02], stalk_top], [0.007, 0.006, 0.005], 3,
             0.0, 0.5)
        rr = rng.uniform(0.008, 0.022)
        blob(polyps, stalk_top, [rr, rr * 1.2, rr], v=r / 0.55, u=rng.random())
    return {"mat_cushion": cushion, "mat_polyps": polyps}


def lanterns(seed):
    rng = np.random.default_rng(seed)
    stalks, pods = Mesh(), Mesh()
    for j in range(rng.integers(3, 8)):
        ang = rng.uniform(0, 2 * math.pi)
        out = np.array([math.cos(ang), 0, math.sin(ang)])
        L = rng.uniform(0.25, 0.9)
        c = curve([0, 0, 0], out * 0.5 + [0, 1, 0], L, 8, bend=out * 0.9 + np.array([0, -0.5, 0]))
        tube(stalks, c, np.linspace(0.02, 0.012, 8), 5, 0, 0.6)
        t = c[-1] - c[-2]
        r = rng.uniform(0.06, 0.13)
        blob(pods, c[-1] + t / np.linalg.norm(t) * r, [r * 0.8, r * 1.35, r * 0.8], v=1.0, u=rng.random(), axis=t,
             rings=6)
    return {"lantern_stalks": stalks, "lantern_pods": pods}


def comb(seed):
    rng = np.random.default_rng(seed)
    body, rows = Mesh(), Mesh()
    blob(body, [0, 0, 0], [0.28, 0.42, 0.28], v=0.5, rings=10)
    for r in range(8):
        a = r * 2 * math.pi / 8
        pts = [[0.29 * math.sin(t) * math.cos(a), 0.43 * math.cos(t), 0.29 * math.sin(t) * math.sin(a)]
               for t in np.linspace(0.35, math.pi - 0.35, 18)]
        tube(rows, pts, 0.018, 4, 0.0, 1.0, u=r / 8)
    return {"comb_body": body, "comb_rows": rows}


def build_all():
    print("organisms:")
    total = {}
    for fn, seed in ((crinoid, 3), (seapen, 5), (fan, 7), (whips, 11), (mat, 13), (lanterns, 17), (comb, 19)):
        for name, mesh in fn(seed).items():
            total[name] = mesh.write(name)
    return total


if __name__ == "__main__":
    build_all()

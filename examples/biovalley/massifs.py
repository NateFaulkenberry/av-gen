"""Mountain masses: the reference's craggy karst peaks and cliff ranges, as generated meshes.

The owner's CP1 note: not obelisks. Massive jagged masses with sheer faces, ledges, cliffs and broken ridgelines, of
very different widths, some broad and buttressed, with vegetation on the ledges, grouped into ranges in depth layers.

Each massif is a heightfield over its own irregular footprint (a mesh, not terrain, because it needs near-vertical
cliffs and stands kilometres out where the terrain is coarse):

  footprint   a superellipse warped by noise (buttresses, re-entrants); outside it the surface drops below the ground
  cliffs      the height rises over a thin band at the footprint's edge, so the sides are sheer faces
  peaks       several summits of different heights and sharpness, joined by a ridged-noise ridgeline
  ledges      terracing: the height is pulled toward steps, so flat ledges alternate with steep risers (bedding)
  crags       ridged noise on top, and the cliff faces are pushed sideways by noise so a riser is not a straight wall

UVs: u = flatness (1 on a ledge, 0 on a cliff), v = height fraction. A material program can put moss on ledges.
`ledges` returns the ledge points (local metres), where vegetation is placed.
"""
from __future__ import annotations

import math

import numpy as np

from geom import Mesh, grid_faces, value_noise2


def _fbm(x, z, seed, octaves, lac=2.0, gain=0.5):
    s, a, f, n = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        s = s + a * value_noise2(x * f + o * 7.3, z * f - o * 3.1, seed + o * 13)
        n += a
        a *= gain
        f *= lac
    return s / n


def _ridged(x, z, seed, octaves):
    s, a, f, n = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        v = 1.0 - np.abs(2.0 * value_noise2(x * f + o * 5.1, z * f + o * 9.7, seed + o * 29) - 1.0)
        s = s + a * v * v
        n += a
        a *= 0.5
        f *= 2.1
    return s / n


KINDS = ("range", "peak", "cliff", "spire")


def massif(seed, kind, L, W, H, res=150):
    """A massif `L` long (x), `W` wide (z), `H` tall, centred on the origin, foot at y = 0 (the skirt drops to
    -0.12 H). Returns (mesh, ledges) with ledges an (n, 3) array of local points on flat ground high on the mass."""
    r = np.random.default_rng(seed)
    nx = res
    nz = max(24, int(res * W / L))
    xs = np.linspace(-0.62 * L, 0.62 * L, nx)
    zs = np.linspace(-0.62 * W, 0.62 * W, nz)
    X, Z = np.meshgrid(xs, zs, indexing="ij")
    u, v = X / (0.5 * L), Z / (0.5 * W)
    cell = max(xs[1] - xs[0], zs[1] - zs[0])

    # footprint: a warped superellipse; buttresses are lobes of the warp
    p = {"range": 2.2, "peak": 2.0, "cliff": 3.0, "spire": 2.0}[kind]
    ang = np.arctan2(v, u)
    lobes = sum(r.uniform(0.04, 0.12) * np.sin(k * ang + r.uniform(0, 6.28)) for k in range(3, 9))
    warp = 0.18 * (_fbm(X / (0.35 * L) + 3.0, Z / (0.35 * L), seed + 1, 3) - 0.5) + lobes
    rad = (np.abs(u) ** p + np.abs(v) ** p) ** (1.0 / p) * (1.0 - warp)
    edge = 1.0 - rad                                     # > 0 inside, 0 at the footprint's edge

    # the outer edge: the mass meets the ground in a cliff band (thin for cliffs, wider for buttressed peaks)
    band = {"range": 0.22, "peak": 0.3, "cliff": 0.1, "spire": 0.25}[kind]
    slope_var = 0.5 + 1.0 * _fbm(X / (0.2 * L), Z / (0.2 * L), seed + 2, 2)
    wall = np.clip(edge / (band * slope_var), 0.0, 1.0) ** 0.6

    # summits: broad convex masses (exponent < 1) for ranges and peaks, sharp ones for spires
    n_peaks = {"range": r.integers(4, 8), "peak": r.integers(2, 4), "cliff": r.integers(2, 4),
               "spire": r.integers(1, 3)}[kind]
    summit = np.zeros_like(X)
    for k in range(int(n_peaks)):
        if kind == "range":
            pu, pv = r.uniform(-0.7, 0.7), r.uniform(-0.3, 0.3)
        else:
            pu, pv = r.normal(0, 0.22), r.normal(0, 0.22)
        hk = H * (1.0 if k == 0 else r.uniform(0.4, 0.85))
        rk = {"range": r.uniform(0.35, 0.6), "peak": r.uniform(0.7, 1.0), "cliff": r.uniform(0.8, 1.1),
              "spire": r.uniform(0.6, 0.9)}[kind]
        ex = {"range": r.uniform(0.7, 1.1), "peak": r.uniform(0.8, 1.2), "cliff": 0.35,
              "spire": r.uniform(1.2, 1.6)}[kind]
        d = np.sqrt((u - pu) ** 2 + (v - pv) ** 2) / rk
        summit = np.maximum(summit, hk * np.clip(1.0 - d, 0, 1) ** ex)
    ridge = _ridged(X / (0.16 * L), Z / (0.16 * L), seed + 3, 5)
    h = summit * (0.75 + 0.5 * ridge) + 0.12 * H * (ridge - 0.5)
    h = np.maximum(h, 0.08 * H)

    # ledges: pull toward steps (bedding); steps uneven in height
    step = H * r.uniform(0.045, 0.075)
    q = h / step + 0.4 * (_fbm(X / 90.0, Z / 90.0, seed + 4, 2) - 0.5)
    fq = q - np.floor(q)
    terr = (np.floor(q) + np.clip((fq - 0.55) / 0.45, 0, 1) ** 2.2) * step
    h = 0.35 * h + 0.65 * terr

    # crags: fine ridged detail, strongest high up
    h += (0.035 * H) * (_ridged(X / 40.0, Z / 40.0, seed + 5, 3) - 0.5) * (0.4 + h / H)

    Y = h * wall - 0.5 * H * (1.0 - wall)        # outside the footprint the skirt drops deep into the ground

    # break straight risers: push the steep parts sideways along the surface gradient
    gx, gy = np.gradient(Y, xs, zs)          # d/dx, d/dz
    steep = np.clip(np.hypot(gx, gy) / 3.0, 0, 1)
    jit = (_fbm(X / 25.0, Y / 25.0, seed + 6, 3) - 0.5) * 0.9 * cell
    gl = np.maximum(np.hypot(gx, gy), 1e-6)
    PX = X + steep * jit * gx / gl
    PZ = Z + steep * jit * gy / gl

    P = np.stack([PX, Y, PZ], -1).reshape(-1, 3)
    flat = 1.0 - np.clip(np.hypot(gx, gy) / 1.2, 0, 1)
    UV = np.stack([flat, np.clip(Y / H, 0, 1)], -1).reshape(-1, 2)
    mesh = Mesh()
    mesh.add(P, grid_faces(nx, nz, False), UV)

    # ledges: flat cells above a sixth of the height, inside the footprint
    sel = (flat > 0.6) & (Y > 0.16 * H) & (wall > 0.98)
    pts = np.stack([PX[sel], Y[sel], PZ[sel]], -1)
    return mesh, pts, (xs, zs, Y)


def up(P):
    return np.tile([0.0, 1.0, 0.0], (len(P), 1))


# ---- the catalogue -----------------------------------------------------------------------------------------------
# name: (kind, L, W, H, seed). Instances scale these uniformly (0.5-1.7) and turn them about y.
CATALOGUE = {
    "range0": ("range", 1000, 340, 420, 101), "range1": ("range", 1300, 420, 480, 102),
    "range2": ("range", 800, 300, 380, 103), "range3": ("range", 1100, 380, 520, 104),
    "peak0": ("peak", 420, 380, 560, 201), "peak1": ("peak", 360, 320, 500, 202), "peak2": ("peak", 520, 420, 600, 203),
    "cliff0": ("cliff", 620, 240, 360, 301), "cliff1": ("cliff", 480, 200, 300, 302),
    "bankcliff": ("cliff", 320, 150, 110, 305),
    "spire0": ("spire", 220, 200, 420, 401), "spire1": ("spire", 260, 220, 480, 402), "spire2": ("spire", 180, 170, 360, 403),
}


def build(out_dir):
    """Write every catalogue massif's GLB and a cache of its heightfield and ledges (for placement)."""
    import json
    info = {}
    tris = 0
    for name, (kind, L, W, H, seed) in CATALOGUE.items():
        res = 150 if kind in ("range", "cliff") else 120
        mesh, ledges, (xs, zs, Y) = massif(seed, kind, L, W, H, res=res)
        tris += mesh.write(out_dir / f"massif_{name}.glb", f"massif_{name}", outward_from=up)
        np.savez(out_dir / f"massif_{name}.npz", xs=xs, zs=zs, Y=Y, ledges=ledges)
        info[name] = {"kind": kind, "L": L, "W": W, "H": H, "ledges": int(len(ledges))}
    (out_dir / "massifs.json").write_text(json.dumps(info, indent=1))
    return tris


class Field:
    """A catalogue massif's heightfield, for sampling in its local frame."""

    def __init__(self, out_dir, name):
        d = np.load(out_dir / f"massif_{name}.npz")
        self.xs, self.zs, self.Y, self.ledges = d["xs"], d["zs"], d["Y"], d["ledges"]

    def height(self, x, z):
        i = np.clip(np.searchsorted(self.xs, x), 1, len(self.xs) - 1)
        k = np.clip(np.searchsorted(self.zs, z), 1, len(self.zs) - 1)
        return self.Y[i, k]

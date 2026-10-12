"""Geometry helpers for the Bioluminescent Valley: grid meshes with computed normals, consistent outward winding,
and a GLB writer (the Rift's, reused).

Every mesh here is generated (numpy), owned by this repository and reproducible from the scripts. Generated GLBs are
gitignored; `valley.py` writes them.
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np

sys.path.append(str(Path(__file__).resolve().parents[1] / "bioluminescent"))
from glb import write_glb  # noqa: E402


def vertex_normals(P, F):
    """Area-weighted vertex normals for positions P (n,3) and faces F (m,3)."""
    a, b, c = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
    fn = np.cross(b - a, c - a)
    N = np.zeros_like(P)
    for k in range(3):
        np.add.at(N, F[:, k], fn)
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
    return N


def grid_faces(rows, cols, wrap):
    """Faces of a (rows x cols) vertex grid; `wrap` joins the last column to the first."""
    F = []
    cc = cols if wrap else cols - 1
    r = np.arange(rows - 1)[:, None]
    c = np.arange(cc)[None, :]
    c1 = (c + 1) % cols
    p00 = r * cols + c
    p01 = r * cols + c1
    p10 = (r + 1) * cols + c
    p11 = (r + 1) * cols + c1
    F = np.stack([np.stack([p00, p10, p01], -1), np.stack([p01, p10, p11], -1)], -2).reshape(-1, 3)
    return F


class Mesh:
    def __init__(self):
        self.P, self.F, self.UV = [], [], []
        self.n = 0

    def add(self, P, F, UV=None):
        P = np.asarray(P, np.float64).reshape(-1, 3)
        if UV is None:
            UV = np.zeros((len(P), 2))
        self.P.append(P)
        self.F.append(np.asarray(F, np.int64).reshape(-1, 3) + self.n)
        self.UV.append(np.asarray(UV, np.float64).reshape(-1, 2))
        self.n += len(P)

    def arrays(self):
        P = np.concatenate(self.P)
        F = np.concatenate(self.F)
        UV = np.concatenate(self.UV)
        return P, F, UV

    def write(self, path, name, outward_from=None):
        """Write a GLB. `outward_from(P) -> directions` (or None) checks the winding: if most face normals point
        against it, every face is flipped, so nothing renders inside out."""
        P, F, UV = self.arrays()
        if outward_from is not None:
            a, b, c = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
            fn = np.cross(b - a, c - a)
            ctr = (a + b + c) / 3.0
            d = np.einsum("ij,ij->i", fn, outward_from(ctr))
            if (d < 0).mean() > 0.5:
                F = F[:, [0, 2, 1]]
        N = vertex_normals(P, F)
        tris = write_glb(path, P.astype(np.float32), N.astype(np.float32), UV.astype(np.float32),
                         F.astype(np.uint32), name=name)
        return tris


def value_noise1(x, seed):
    """Smooth 1D value noise in [-1, 1]."""
    x = np.asarray(x, float)
    i = np.floor(x).astype(np.int64)
    t = x - i
    t = t * t * (3 - 2 * t)

    def h(k):
        v = (k * 374761393 + seed * 668265263) & 0xFFFFFFFF
        v = ((v ^ (v >> 13)) * 1274126177) & 0xFFFFFFFF
        return (v & 0xFFFF) / 32767.5 - 1.0

    return h(i) * (1 - t) + h(i + 1) * t


def value_noise2(x, z, seed):
    """Smooth 2D value noise in [0, 1]."""
    x, z = np.asarray(x, float), np.asarray(z, float)
    ix, iz = np.floor(x).astype(np.int64), np.floor(z).astype(np.int64)
    tx, tz = x - ix, z - iz
    tx, tz = tx * tx * (3 - 2 * tx), tz * tz * (3 - 2 * tz)

    def h(a, b):
        v = (a * 374761393 + b * 668265263 + seed * 2246822519) & 0xFFFFFFFF
        v = ((v ^ (v >> 13)) * 1274126177) & 0xFFFFFFFF
        return (v & 0xFFFFFF) / float(0xFFFFFF)

    return (h(ix, iz) * (1 - tx) * (1 - tz) + h(ix + 1, iz) * tx * (1 - tz) + h(ix, iz + 1) * (1 - tx) * tz +
            h(ix + 1, iz + 1) * tx * tz)


def fbm2(x, z, seed, octaves=5, lac=2.03, gain=0.5):
    s, a, f, norm = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        s = s + a * value_noise2(x * f, z * f, seed + o * 17)
        norm += a
        a *= gain
        f *= lac
    return s / norm


def quat_y(yaw):
    """Quaternion (x, y, z, w) for a rotation of `yaw` radians about +y."""
    return [0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)]


def sphere(mesh, centre, radius, rings=48, segs=96):
    """A UV sphere, wound outward."""
    th = np.linspace(0, math.pi, rings + 1)
    ph = np.linspace(0, 2 * math.pi, segs, endpoint=False)
    T, Ph = np.meshgrid(th, ph, indexing="ij")
    d = np.stack([np.sin(T) * np.cos(Ph), np.cos(T), np.sin(T) * np.sin(Ph)], -1).reshape(-1, 3)
    P = np.asarray(centre) + radius * d
    UV = np.stack([(Ph / (2 * math.pi)).ravel(), (T / math.pi).ravel()], -1)
    mesh.add(P, grid_faces(rings + 1, segs, True), UV)

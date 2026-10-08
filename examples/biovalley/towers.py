"""Karst towers and the far ranges, as generated meshes.

The reference's valley is walled by Zhangjiajie-like quartz-sandstone pillars: near-vertical faces cut by joints
(deep vertical grooves), stepped by bedding (horizontal strata that make ledges), slender (height several times the
width), often fused in twos and threes, crowned by rounded or broken tops. A heightfield cannot make a vertical
face or a ledge, so they are meshes; the terrain carries the ground they stand on.

A tower's UVs: u = angle around it / 2pi, v = height fraction (0 at the foot, 1 at the crown), so a material
program can put moss on the upper ledges or streaks down the faces.
"""
from __future__ import annotations

import math

import numpy as np

from geom import Mesh, grid_faces, value_noise1, fbm2


def _profile(kind, t, seed, crown_jag):
    """Radius multiplier along a pillar's height fraction t."""
    if kind == "peak":
        # a Guilin cone: broad foot, steepening flanks, a sharp broken summit
        p = (1.0 - t) ** 0.8 * (1.0 + 0.5 * math.exp(-t / 0.06))
        p *= 1.0 + 0.16 * value_noise1(t * 6, seed + 3)
        return max(p, 0.015)
    # a needle: sheer, slowly tapering, necked here and there, a crown that breaks off
    p = (1.0 + 0.45 * math.exp(-t / 0.04)) * (1.0 - 0.5 * t) * (1.0 + 0.14 * value_noise1(t * 5, seed + 3))
    if crown_jag < 0.33:
        # a broken, flat-ish top (Zhangjiajie): the faces run up nearly to the crown, which slumps at the end
        if t > 0.94:
            u = (t - 0.94) / 0.06
            p *= (1.0 - u) ** 0.25
    else:
        c0 = 0.9 - 0.3 * crown_jag            # where the crown starts: low = a long point
        if t > c0:
            u = (t - c0) / (1.0 - c0)
            p *= (1.0 - u) ** (0.45 + 0.9 * crown_jag)
    return max(p, 0.02)


def _pillar(mesh, rng, base, H, R, lean, kind="needle", sides=48):
    """One pillar: beds of near-vertical faces separated by ledges, joints cut down it, a broken crown."""
    bounds = [0.0]
    while bounds[-1] < 1.0:
        bounds.append(bounds[-1] + rng.uniform(0.014, 0.045))
    bounds[-1] = 1.0
    bounds = np.array(bounds)
    mult = 1.0 + rng.uniform(-0.09, 0.07, len(bounds))           # each bed's own setback
    rings_t = []
    for k in range(len(bounds) - 1):
        a, b = bounds[k], bounds[k + 1]
        g = min(0.003, (b - a) * 0.15)
        rings_t += [(a + g, k), (a + (b - a) * 0.5, k), (b - g, k)]
    ts = np.array([t for t, _ in rings_t])
    beds = np.array([k for _, k in rings_t])

    th = np.linspace(0, 2 * math.pi, sides, endpoint=False)
    harm = [(k, rng.uniform(0.03, 0.10) / (1 + 0.15 * k), rng.uniform(0, 6.28), rng.uniform(-3.0, 3.0))
            for k in range(2, 9)]
    joints = [(rng.uniform(0, 6.28), rng.uniform(0.05, 0.13), rng.uniform(0.15, 0.42), rng.uniform(-0.5, 0.5))
              for _ in range(rng.integers(3, 8))]
    crown_jag = rng.uniform(0.0, 0.85)
    seed = int(rng.integers(1, 1 << 30))
    P = np.zeros((len(ts), sides, 3))
    for i, (t, k) in enumerate(zip(ts, beds)):
        rough = 1.0 + 3.0 * t ** 3                                    # the upper faces are more broken
        c = np.ones(sides)
        for (h, a, ph, drift) in harm:
            c += rough * a * np.sin(h * th + ph + drift * t)
        for (j0, w, d, drift) in joints:
            dj = np.angle(np.exp(1j * (th - j0 - drift * t)))
            c -= d * np.exp(-(dj / w) ** 2) * (0.55 + 0.45 * (value_noise1(t * 11 + j0, seed) * 0.5 + 0.5))
        c *= 1.0 + 0.05 * value_noise1(th * 4.0 + t * 55.0, seed + 11)
        prof = _profile(kind, t, seed, crown_jag) * mult[k]
        r = R * prof * np.maximum(c, 0.2)
        off = np.array(lean) * H * t * t
        P[i, :, 0] = base[0] + off[0] + r * np.cos(th)
        P[i, :, 2] = base[2] + off[1] + r * np.sin(th)
        P[i, :, 1] = base[1] + t * H
    crown = np.array([base[0] + lean[0] * H, base[1] + H * (1.0 + (0.012 if kind == "peak" else 0.004)),
                      base[2] + lean[1] * H])
    UV = np.stack([np.tile(th / (2 * math.pi), len(ts)), np.repeat(ts, sides)], -1)
    n0 = mesh.n
    mesh.add(P.reshape(-1, 3), grid_faces(len(ts), sides, True), UV)
    top = (len(ts) - 1) * sides
    cap = [[n0 + top + b, n0 + top + (b + 1) % sides, mesh.n] for b in range(sides)]
    mesh.P.append(crown[None, :])
    mesh.UV.append(np.array([[0.0, 1.0]]))
    mesh.F.append(np.array(cap))
    mesh.n += 1


def tower(seed, H=300.0):
    """A tower variant: a needle, a cone peak, or a massif of fused needles. H (metres) is the tallest pillar; the
    mesh's foot is at y = -0.08 H (sunk into the ground it stands on)."""
    rng = np.random.default_rng(seed)
    mesh = Mesh()
    kind = ["needle", "needle", "peak", "massif", "needle", "peak", "massif", "needle"][seed % 8]
    if kind == "peak":
        R = H * rng.uniform(0.2, 0.27)
        _pillar(mesh, rng, (0, -0.08 * H, 0), H * 1.08, R, rng.normal(0, 0.03, 2), "peak")
        for _ in range(rng.integers(1, 3)):        # shoulder peaks
            a = rng.uniform(0, 6.28)
            d = R * rng.uniform(0.5, 0.9)
            _pillar(mesh, rng, (d * math.cos(a), -0.08 * H, d * math.sin(a)), H * rng.uniform(0.45, 0.75),
                    R * rng.uniform(0.5, 0.8), rng.normal(0, 0.04, 2), "peak")
        return mesh
    n = 1 if kind == "needle" else int(rng.integers(3, 6))
    R = H * (rng.uniform(0.1, 0.15) if kind == "needle" else rng.uniform(0.08, 0.12))
    for k in range(n):
        h = H if k == 0 else H * rng.uniform(0.5, 0.95)
        r = R * (1.0 if k == 0 else rng.uniform(0.6, 1.0))
        a = rng.uniform(0, 6.28)
        d = 0.0 if k == 0 else R * rng.uniform(0.9, 1.6)
        _pillar(mesh, rng, (d * math.cos(a), -0.08 * H, d * math.sin(a)), h * 1.08, r, rng.normal(0, 0.03, 2))
    return mesh


def tower_outward(P):
    """Outward for the winding check: away from the vertical axis (the majority of faces are walls)."""
    d = P.copy()
    d[:, 1] = 0.0
    return d


def ranges(seed=7, r0=6500.0, r1=11500.0, az0=-1.9, az1=1.9, n_az=520, n_r=40):
    """The far ranges: a band of spiky karst peaks around the north half, centred on the origin, heights
    250-1300 m, sharp crests (ridged noise sharpened), foot at y = -40 so it meets the haze, not the sky."""
    az = np.linspace(az0, az1, n_az)
    rr = np.linspace(r0, r1, n_r)
    A, Rr = np.meshgrid(az, rr, indexing="ij")
    X = Rr * np.sin(A)
    Z = Rr * np.cos(A)
    # peaks: ridged noise, sharpened, plus a comb of spires on the nearest ridge line
    n1 = fbm2(X / 900.0, Z / 900.0, seed, 5)
    ridge = 1.0 - np.abs(2 * n1 - 1)
    ridge = ridge ** 3.0
    n2 = fbm2(X / 260.0, Z / 260.0, seed + 5, 4)
    spikes = (1.0 - np.abs(2 * n2 - 1)) ** 6
    u = (Rr - r0) / (r1 - r0)
    env = np.sin(np.clip(u, 0, 1) * math.pi) ** 0.6           # rises from the inner edge, falls at the outer
    side = np.clip((np.abs(A) - 0.0) / 1.9, 0, 1)
    Y = -60.0 + env * (220.0 + 1100.0 * ridge + 120.0 * spikes ** 0.5 * (0.5 + 0.5 * side))
    P = np.stack([X, Y, Z], -1).reshape(-1, 3)
    UV = np.stack([(A - az0) / (az1 - az0), np.clip(Y / 1300.0, 0, 1)], -1).reshape(-1, 2)
    mesh = Mesh()
    mesh.add(P, grid_faces(n_az, n_r, False)[:, :], UV)
    return mesh

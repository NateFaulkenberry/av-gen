"""Waterfalls: veils of falling water and the mist where they land.

A fall is a bundle of strands, each a thin ribbon that leaves the lip with a little forward speed and drops under
gravity, its width and brightness its own: together they read as a streaked veil rather than a sheet. The strands
are faintly self-lit (moonlit spray; the bioluminescence pass makes them carry the valley's light), double-sided, and
face the shot they serve. Where a fall lands, a particle pool of slow, soft mist rises and drifts.

A fall needs a lip and a foot: `find_lip` walks a massif's heightfield from its centre toward a viewer and returns
the cliff's top edge and the ground below it.
"""
from __future__ import annotations

import math

import numpy as np

from geom import Mesh


def find_lip(field, rec, toward, frac=0.55):
    """The top of the cliff on the side of `toward` (world x, z) for a massif instance `rec`: (lip xyz, foot xyz)."""
    x, y, z, qx, qy, qz, qw, s = rec[:8]
    yaw = 2.0 * math.atan2(qy, qw)
    c, sn = math.cos(yaw), math.sin(yaw)
    dx, dz = (toward[0] - x) / s, (toward[1] - z) / s
    lx, lz = c * dx - sn * dz, sn * dx + c * dz              # the viewer in the massif's frame
    d = np.array([lx, lz]) / math.hypot(lx, lz)
    ts = np.linspace(0, max(field.xs[-1], field.zs[-1]), 400)
    h = np.array([field.height(d[0] * t, d[1] * t) for t in ts])
    top = h.max()
    above = np.where(h > frac * top)[0]
    i = above[-1]                                             # the outermost high point: the lip
    j = i + int(np.argmax(h[i:] < 0.08 * top)) if np.any(h[i:] < 0.08 * top) else len(ts) - 1

    def world(t, hh):
        px, pz = d[0] * t * s, d[1] * t * s
        return np.array([x + c * px + sn * pz, y + hh * s, z - sn * px + c * pz])

    return world(ts[i], h[i]), world(ts[j], max(h[j], 0.0))


def fall(lip, foot_y, facing, width, seed, strands=40, speed=1.6, segs=40):
    """A veil of `strands` ribbons from `lip` (xyz) down to `foot_y`, falling away from the cliff along `facing`."""
    r = np.random.default_rng(seed)
    facing = np.array([facing[0], 0.0, facing[1]], float)
    facing /= np.linalg.norm(facing)
    across = np.cross([0.0, 1.0, 0.0], facing)
    m = Mesh()
    drop = lip[1] - foot_y
    for k in range(strands):
        o = across * r.normal(0, width * 0.32) + facing * r.uniform(-0.3, 0.3)
        v0 = speed * r.uniform(0.6, 1.4)
        tmax = math.sqrt(2 * drop / 9.81)
        ts = np.linspace(0, tmax, segs)
        # the veil spreads as it falls, and wanders
        spread = (r.normal(0, 0.12) * drop * (ts / tmax) ** 1.5)[:, None] * across[None, :]
        P = (lip + o)[None, :] + facing[None, :] * (v0 * ts)[:, None] - np.array([0, 1.0, 0])[None, :] * \
            (0.5 * 9.81 * ts * ts)[:, None] + spread
        w = width / strands * r.uniform(0.6, 2.2) * (1.0 + 1.5 * ts / tmax)
        L = P - across[None, :] * w[:, None] * 0.5
        R = P + across[None, :] * w[:, None] * 0.5
        V = np.stack([L, R], 1).reshape(-1, 3)
        u = r.uniform(0, 1)
        UV = np.stack([np.full(len(V), u), np.repeat(ts / tmax, 2)], -1)
        F = []
        for a in range(segs - 1):
            p0, p1, p2, p3 = 2 * a, 2 * a + 1, 2 * a + 2, 2 * a + 3
            F += [[p0, p2, p1], [p1, p2, p3]]
        m.add(V, np.array(F), UV)
    return m


def mist(name, pos, extent, rate, seed, color=(0.55, 0.68, 1.0), size=(1.5, 4.0), emissive=0.25):
    """A pool of rising, drifting spray at a fall's foot."""
    return {"name": name, "kind": "particles", "particles": {
        "capacity": int(rate * 10), "seed": seed, "shape": "disc", "position": [round(float(v), 2) for v in pos],
        "extent": [float(extent), 0.0, float(extent)], "direction": [0, 1, 0], "spread": 0.9, "spawnRate": float(rate),
        "speedMin": 0.3, "speedMax": 1.4, "gravity": [0, 0.05, 0], "drag": 0.4, "turbulence": 0.6,
        "turbulenceScale": 0.15, "turbulenceSpeed": 0.25, "sizeStart": size[0], "sizeEnd": size[1],
        "sizeVariance": 0.5, "lifetimeMin": 5.0, "lifetimeMax": 9.0,
        "colorStart": [*color, 0.5], "colorEnd": [*color, 0.0], "emissive": emissive, "blend": "additive",
        "softness": 1.0}}

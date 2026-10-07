"""The Rift: a drained abyssal trench, as AV Gen terrain.

A canyon about 150 m deep and 120-200 m across meanders along +z. Its walls are ribbed rock (ridged noise), its floor
is a broad shelf, and a shallow dark river runs down the middle. The camera lives inside, so the walls close every
view: there is no edge of the world to see, and the far reaches of the canyon fade into haze in both directions.

Heights for placing organisms are asked of the engine (avgen_world_preview), never re-derived: a lattice of probes is
cached and interpolated bilinearly.
"""
from __future__ import annotations

import json
import math
import subprocess
import hashlib
import re
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
TOOL = REPO / "build" / "release" / "tools" / "avgen_world_preview"
CACHE = REPO / "build" / "biolum"

SIZE = 2400.0
FLOOR = -150.0       # the canyon floor's level
Z_RANGE = (-1100.0, 1100.0)


def centre_x(z):
    """The canyon's centre line."""
    return 70.0 * math.sin(z / 260.0) + 22.0 * math.sin(z / 97.0 + 1.0)


def river_x(z):
    return centre_x(z) + 9.0 * math.sin(z / 61.0 + 0.4)


def _path(fn, level_fn, step=40.0):
    zs = np.arange(Z_RANGE[0], Z_RANGE[1] + 1, step)
    return [[round(fn(z), 2), round(level_fn(z), 2), round(float(z), 2)] for z in zs]


def world():
    return {
        "name": "rift",
        "seed": 20261006,
        "size": [SIZE, SIZE],
        "baseHeight": 0.0,
        "erosion": 0.35,
        "seaLevel": -1000.0,
        "layers": [
            {"frequency": 0.0018, "amplitude": 70.0, "ridged": 0.2, "warp": 260.0},   # the plateau's swell
            {"frequency": 0.007, "amplitude": 26.0, "ridged": 0.75, "warp": 90.0},    # buttresses and gullies
            {"frequency": 0.022, "amplitude": 9.0, "ridged": 0.8, "warp": 25.0},      # rock ribs
            {"frequency": 0.08, "amplitude": 1.6, "ridged": 0.5, "warp": 6.0},        # ledges
            {"frequency": 0.3, "amplitude": 0.25, "ridged": 0.0, "warp": 0.0},        # rubble
        ],
        "features": [
            {"name": "rift", "kind": "valley", "path": _path(centre_x, lambda z: FLOOR), "width": 210.0,
             "amplitude": 175.0, "falloff": 4.0, "roughness": 1.0, "smoothing": 3},
            {"name": "shelf", "kind": "flat", "path": _path(centre_x, lambda z: FLOOR + 2.0 - z * 0.004),
             "width": 62.0, "falloff": 3.0, "flatten": 0.92, "roughness": 0.3, "smoothing": 3},
            {"name": "river", "kind": "river", "path": _path(river_x, lambda z: FLOOR - 1.5 - z * 0.004, 30.0),
             "width": 15.0, "amplitude": 3.0, "falloff": 1.2, "flatten": 0.9, "roughness": 0.15,
             "water": True, "waterDepth": 1.3, "smoothing": 3},
        ],
        "biomes": [],
    }


class Ground:
    """Engine heights on a lattice, interpolated."""

    def __init__(self, w, x0, x1, z0, z1, step=2.0):
        self.w = w
        key = hashlib.sha256((json.dumps(w, sort_keys=True) + f"{x0},{x1},{z0},{z1},{step}").encode()).hexdigest()[:16]
        CACHE.mkdir(parents=True, exist_ok=True)
        f = CACHE / f"heights-{key}.npz"
        self.x0, self.z0, self.step = x0, z0, step
        xs = np.arange(x0, x1 + step, step)
        zs = np.arange(z0, z1 + step, step)
        if f.exists():
            d = np.load(f)
            self.h, self.water = d["h"], d["water"]
        else:
            wf = CACHE / f"world-{key}.json"
            wf.write_text(json.dumps({"world": w}))
            pts = [(x, z) for z in zs for x in xs]
            hs, ws = [], []
            for i in range(0, len(pts), 4000):
                chunk = pts[i:i + 4000]
                args = [a for (x, z) in chunk for a in (f"{x:.3f}", f"{z:.3f}")]
                out = subprocess.run([str(TOOL), str(wf), str(CACHE / "probe.png"), "16"] + args,
                                     capture_output=True, text=True, check=True).stdout
                found = re.findall(r"probe \([^)]*\): height (-?[\d.]+) .*? water (\S+)", out)
                assert len(found) == len(chunk), (len(found), len(chunk))
                for h, wv in found:
                    hs.append(float(h))
                    ws.append(-1e9 if wv == "none" else float(wv))
            self.h = np.array(hs).reshape(len(zs), len(xs))
            self.water = np.array(ws).reshape(len(zs), len(xs))
            np.savez(f, h=self.h, water=self.water)

    def _bil(self, a, x, z):
        fx = (np.asarray(x) - self.x0) / self.step
        fz = (np.asarray(z) - self.z0) / self.step
        ix = np.clip(np.floor(fx).astype(int), 0, a.shape[1] - 2)
        iz = np.clip(np.floor(fz).astype(int), 0, a.shape[0] - 2)
        tx, tz = np.clip(fx - ix, 0, 1), np.clip(fz - iz, 0, 1)
        return (a[iz, ix] * (1 - tx) * (1 - tz) + a[iz, ix + 1] * tx * (1 - tz) + a[iz + 1, ix] * (1 - tx) * tz +
                a[iz + 1, ix + 1] * tx * tz)

    def height(self, x, z):
        return self._bil(self.h, x, z)

    def wet(self, x, z):
        """True where the point is under water."""
        fx = np.clip(np.round((np.asarray(x) - self.x0) / self.step).astype(int), 0, self.water.shape[1] - 1)
        fz = np.clip(np.round((np.asarray(z) - self.z0) / self.step).astype(int), 0, self.water.shape[0] - 1)
        return self.water[fz, fx] > self.h[fz, fx]

    def water_level(self, x, z):
        fx = np.clip(np.round((np.asarray(x) - self.x0) / self.step).astype(int), 0, self.water.shape[1] - 1)
        fz = np.clip(np.round((np.asarray(z) - self.z0) / self.step).astype(int), 0, self.water.shape[0] - 1)
        return self.water[fz, fx]

    def slope(self, x, z, d=1.5):
        hx = (self.height(np.asarray(x) + d, z) - self.height(np.asarray(x) - d, z)) / (2 * d)
        hz = (self.height(x, np.asarray(z) + d) - self.height(x, np.asarray(z) - d)) / (2 * d)
        return np.sqrt(hx * hx + hz * hz)

    def normal(self, x, z, d=1.5):
        hx = (self.height(np.asarray(x) + d, z) - self.height(np.asarray(x) - d, z)) / (2 * d)
        hz = (self.height(x, np.asarray(z) + d) - self.height(x, np.asarray(z) - d)) / (2 * d)
        n = np.stack([-hx, np.ones_like(hx), -hz], -1)
        return n / np.linalg.norm(n, axis=-1, keepdims=True)

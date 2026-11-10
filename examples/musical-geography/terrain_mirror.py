"""A numpy mirror of scene/tile_terrain.cpp's tileSample (ADR-1950), for art planning only: where the sea, the shore,
the hills and the crowns are, so still cameras and the flight route can be placed. It is not the parity source (the
C++ and the WGSL are); expect float-rounding differences.

    python3 terrain_mirror.py [--map out.png] [--size 1600] [--step 4]   # a hypsometric map of world.TILES
"""
from __future__ import annotations

import numpy as np

M32 = 0xFFFFFFFF
K_REGION, K_CONT, K_WX, K_WZ, K_HILL = 0xA5A5A5A5, 0x00C0FFEE, 0x01234567, 0x07654321, 0x0000BEEF


U = np.uint64


def _mix32(x):
    x = U(x) & U(M32) if np.isscalar(x) else x.astype(U) & U(M32)
    x ^= x >> U(16)
    x = (x * U(0x7feb352d)) & U(M32)
    x ^= x >> U(15)
    x = (x * U(0x846ca68b)) & U(M32)
    x ^= x >> U(16)
    return x


def _hash(ix, iz, seed, ch):
    s = _mix32(U((seed + ch * 0x9e3779b9) & M32))
    izu = (iz.astype(np.int64) & M32).astype(U)
    ixu = (ix.astype(np.int64) & M32).astype(U)
    z = _mix32(((izu * U(0xd8163841)) & U(M32)) ^ s)
    return _mix32(((ixu * U(0x8da6b343)) & U(M32)) ^ z)


def _unit(h):
    return (h >> U(8)).astype(np.float64) / 16777216.0


def value_noise(seed, x, z):
    xf, zf = np.floor(x), np.floor(z)
    ix, iz = xf.astype(np.int64), zf.astype(np.int64)
    fx, fz = x - xf, z - zf
    sx, sz = fx * fx * (3 - 2 * fx), fz * fz * (3 - 2 * fz)
    c = lambda a, b: _unit(_hash(ix + a, iz + b, seed, 101))   # noqa: E731
    a = c(0, 0) + (c(1, 0) - c(0, 0)) * sx
    b = c(0, 1) + (c(1, 1) - c(0, 1)) * sx
    return a + (b - a) * sz


def fbm(seed, x, z, octaves):
    s = n = 0.0
    amp = freq = 1.0
    for o in range(max(1, min(6, octaves))):
        s = s + amp * value_noise((seed + o * 1013) & M32, x * freq + o * 17.31, z * freq - o * 9.17)
        n += amp
        amp *= 0.5
        freq *= 2.0
    return s / n


def _sm(e0, e1, x):
    t = np.clip((x - e0) / max(e1 - e0, 1e-6), 0, 1)
    return t * t * (3 - 2 * t)


def path_distance(path, x, z):
    best = np.full(np.shape(x), 1e9)
    for (ax, az), (bx, bz) in zip(path[:-1], path[1:]):
        abx, abz = bx - ax, bz - az
        apx, apz = x - ax, z - az
        t = np.clip((apx * abx + apz * abz) / max(abx * abx + abz * abz, 1e-6), 0, 1)
        best = np.minimum(best, np.hypot(apx - abx * t, apz - abz * t))
    return best


def tile_sample(s, x, z):
    seed = s["seed"]
    region = value_noise(seed ^ K_REGION, x / s["regionWavelength"], z / s["regionWavelength"])
    k = 1 + s["regionVariation"] * (2 * region - 1)
    c = fbm(seed ^ K_CONT, x / s["continentWavelength"], z / s["continentWavelength"], 3)
    lt, sb = s["landThreshold"], s["shoreBand"]
    land = _sm(lt - sb, lt + sb, c)
    inland = _sm(lt, lt + s["inlandBand"], c)
    ww = s["warpWavelength"]
    wx = (fbm(seed ^ K_WX, x / ww, z / ww, 2) * 2 - 1) * s["hillWarp"]
    wz = (fbm(seed ^ K_WZ, x / ww + 31.7, z / ww - 11.3, 2) * 2 - 1) * s["hillWarp"]
    hwl = s["hillWavelength"] * k
    hn = fbm(seed ^ K_HILL, (x + wx) / hwl, (z + wz) / hwl, s["hillOctaves"])
    shaped = _sm(s["hillLow"], s["hillHigh"], hn)
    hill = shaped - np.maximum(shaped - s["crownThreshold"], 0) * s["crownFlatten"]
    land_h = s["shoreRise"] + s["hillHeight"] * k * hill * inland * s.get("heightScale", 1.0)
    channel = np.zeros_like(land_h)
    # ADR-1965 (approximate, for planning maps): rivers carve to the sea floor (the CP-A rule; a bedDepth >= 0 bed is
    # drawn at the floor too, so a stream's valley reads but its level is not mirrored), dry valleys lower the land by
    # depthFraction of its relief, ponds are water within their radius in a basin. Legacy `river` = rivers[0].
    rivers = list(s.get("rivers") or ([{"points": s["river"], "width": s["riverWidth"], "valleyWidth": s["valleyWidth"]}]
                                      if s.get("river") else []))
    for r in rivers:
        d = path_distance(r["points"], x, z)
        v = 1 - _sm(0.5 * r["width"], 0.5 * r["valleyWidth"], d)
        land_h = land_h + (-s["seaDepth"] - land_h) * v
        channel = np.maximum(channel, 1 - _sm(0.5 * r["width"] - 0.5, 0.5 * r["width"] + 0.5, d))
    for vl in s.get("valleys") or []:
        d = path_distance(vl["points"], x, z)
        v = 1 - _sm(0.0, 0.5 * vl["valleyWidth"], d)
        land_h = land_h - (land_h - s["shoreRise"]) * vl.get("depthFraction", 0.5) * v
    for p in s.get("ponds") or []:
        d = np.hypot(x - p["centre"][0], z - p["centre"][1])
        v = 1 - _sm(p["radius"], p["radius"] + p["basinWidth"], d)
        land_h = land_h * (1 - 0.6 * v)
        channel = np.maximum(channel, 1 - _sm(p["radius"] - 0.5, p["radius"] + 0.5, d))
    lo = s["seaLevel"] - s["seaDepth"]
    h = lo + ((s["seaLevel"] + land_h) - lo) * land
    out = {"water": 1 - land + channel * land, "shore": 1 - _sm(0, 3 * sb, np.abs(c - lt)),
           "flat": _sm(s["crownThreshold"], s["crownThreshold"] + 0.06, shaped) * s["crownFlatten"] * inland * land}
    p = np.zeros_like(h)
    if s.get("path"):
        d = path_distance(s["path"], x, z)
        p = (1 - _sm(0.5 * s["pathWidth"], 0.5 * s["pathWidth"] + 0.75, d)) * land
        h = h + s["pathLift"] * p
    out["height"], out["path"] = h, p
    return out


if __name__ == "__main__":
    import argparse
    import os
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import world
    from PIL import Image
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", default="terrain-map.png")
    ap.add_argument("--size", type=float, default=1600)
    ap.add_argument("--step", type=float, default=4)
    a = ap.parse_args()
    r = np.arange(-a.size, a.size, a.step) + 0.5
    X, Z = np.meshgrid(r, r)
    t = tile_sample(world.TILES, X, Z)
    h = t["height"]
    g = np.clip(h / max(h.max(), 1), 0, 1)
    img = np.stack([60 + 195 * g, 60 + 170 * g, 90 + 40 * g], -1)
    img[t["water"] > 0.5] = [30, 60, 190]
    img[t["path"] > 0.5] = [255, 255, 255]
    img[(t["flat"] > 0.3)] = [255, 120, 180]
    Image.fromarray(img.astype(np.uint8)).save(a.map)
    print("height %.1f..%.1f  water %.2f  flat %.3f  map %s (x right, z down, %d m)" %
          (h.min(), h.max(), (t["water"] > 0.5).mean(), (t["flat"] > 0.3).mean(), a.map, 2 * a.size))

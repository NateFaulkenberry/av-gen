"""Compose still cameras without the GPU: a small ray-marched preview of world.TILES through build.CAMERAS, coloured
by water / height / path, with the sky above. Art planning only (the engine is the truth).

    python3 frame_preview.py <camera> [<camera> ...] --out DIR
"""
from __future__ import annotations

import argparse
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build            # noqa: E402
import terrain_mirror   # noqa: E402
import world            # noqa: E402
from PIL import Image   # noqa: E402

W, H = 320, 180
STATE = "day"


def preview(name, out_dir):
    c = build.camera(name)
    eye = np.array(c["position"], float)
    tgt = np.array(c["target"], float)
    f = tgt - eye
    f /= np.linalg.norm(f)
    r = np.cross(f, [0, 1, 0])
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    th = math.tan(math.radians(c["fov"]) / 2)
    ys, xs = np.mgrid[0:H, 0:W]
    px = (2 * (xs + 0.5) / W - 1) * th * W / H
    py = (1 - 2 * (ys + 0.5) / H) * th
    d = f[None, None] + px[..., None] * r + py[..., None] * u
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    hit = np.full((H, W), np.inf)
    t = np.zeros((H, W))
    alive = np.ones((H, W), bool)
    for _ in range(260):
        p = eye + d * t[..., None]
        s = terrain_mirror.tile_sample(world.TILES, p[..., 0], p[..., 2])
        below = (p[..., 1] <= s["height"]) & alive
        hit[below] = t[below]
        alive &= ~below
        t = t + np.maximum(0.6, 0.012 * t + 0.4 * np.maximum(p[..., 1] - s["height"], 0))
        alive &= t < 700
        if not alive.any():
            break
    p = eye + d * np.where(np.isfinite(hit), hit, 0)[..., None]
    s = terrain_mirror.tile_sample(world.TILES, p[..., 0], p[..., 2])
    import look
    st = look.STATES[STATE]
    lin = np.array([look.hexlin(c) for c in st["land"]])
    t = np.clip(np.interp(s["height"], look.LAND_STOPS, np.linspace(0, 1, 5)), 0, 1) * 4
    i = np.minimum(np.floor(t).astype(int), 3)
    fr = (t - i)[..., None]
    col = lin[i] * (1 - fr) + lin[i + 1] * fr
    img = 255 * np.clip(col, 0, 1) ** (1 / 2.2)
    img[s["water"] > 0.5] = 255 * np.array(look.hexlin(st["water"][0])) ** (1 / 2.2)
    land = np.isfinite(hit) & (s["water"] <= 0.5)
    warm = land & (s["height"] >= look.LAND_STOPS[2])
    print("   warm (>= stop 2) %.0f%% of land" % (100 * warm.sum() / max(land.sum(), 1)))
    img[s["path"] > 0.5] = [255, 255, 255]
    fog = np.clip(np.where(np.isfinite(hit), hit, 700) / 700.0, 0, 1)[..., None] ** 2
    img = img * (1 - fog) + np.array([190, 205, 240]) * fog
    img[~np.isfinite(hit)] = [150, 180, 240]
    sky = (~np.isfinite(hit)).mean()
    path = (np.isfinite(hit) & (s["path"] > 0.5)).mean()
    water = (np.isfinite(hit) & (s["water"] > 0.5)).mean()
    out = os.path.join(out_dir, "preview-%s.png" % name)
    Image.fromarray(img.astype(np.uint8)).resize((W * 3, H * 3), Image.NEAREST).save(out)
    print("%-10s sky %.0f%%  water %.0f%%  path %.2f%%  -> %s" % (name, sky * 100, water * 100, path * 100, out))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("cameras", nargs="+")
    ap.add_argument("--out", default=".")
    ap.add_argument("--state", default="day")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    STATE = a.state
    for n in a.cameras:
        preview(n, a.out)

#!/usr/bin/env python3
"""Remap a rectilinear frame to a fisheye projection -- the CPU twin of a candidate post pass.

The rectilinear input covers horizontal FOV --in-fov. Each output pixel at radius r (normalised so
the output's half-WIDTH is the input's half-width angle) is given the angle theta it stands for
under the chosen model, and sampled from the rectilinear frame at tan(theta). Output pixels whose
angle the input does not contain are left black, so the result shows honestly what is missing.

  stereographic  r = 2 f tan(theta/2)   (keeps shapes round; mildest edge squeeze)
  equisolid      r = 2 f sin(theta/2)   (the usual photographic fisheye)
  equidistant    r = f theta

--fit width|diag: f is chosen so the input's horizontal half-angle lands at the output's
half-width (no black, keeps the input's horizontal FOV) or at the half-diagonal (the full input
lands inside the diagonal; the corners go black only if the model needs more angle than exists).

Usage: remap_fisheye.py in.png out.png --in-fov 140 --model stereographic --out-w 640 --out-h 360
"""
import argparse
import math

import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--in-fov", type=float, required=True)
    ap.add_argument("--model", default="stereographic", choices=["stereographic", "equisolid", "equidistant"])
    ap.add_argument("--out-w", type=int, default=640)
    ap.add_argument("--out-h", type=int, default=360)
    ap.add_argument("--fit", default="width", choices=["width", "diag"])
    a = ap.parse_args()

    src = np.asarray(Image.open(a.inp).convert("RGB"), dtype=np.float32)
    sh, sw = src.shape[:2]
    half_in = math.radians(a.in_fov) / 2
    fr = (sw / 2) / math.tan(half_in)  # rectilinear focal in source pixels

    W, H = a.out_w, a.out_h
    rmax = W / 2 if a.fit == "width" else math.hypot(W, H) / 2
    if a.model == "stereographic":
        f = rmax / (2 * math.tan(half_in / 2))
    elif a.model == "equisolid":
        f = rmax / (2 * math.sin(half_in / 2))
    else:
        f = rmax / half_in

    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    dx, dy = xs + 0.5 - W / 2, ys + 0.5 - H / 2
    r = np.hypot(dx, dy)
    if a.model == "stereographic":
        th = 2 * np.arctan(r / (2 * f))
    elif a.model == "equisolid":
        th = 2 * np.arcsin(np.clip(r / (2 * f), 0, 1))
    else:
        th = r / f
    valid = th < math.radians(89.5)
    rs = fr * np.tan(np.where(valid, th, 0))
    scale = np.where(r > 1e-6, rs / np.maximum(r, 1e-6), fr / f)
    sx = sw / 2 + dx * scale
    sy = sh / 2 + dy * scale
    inside = valid & (sx >= 0) & (sx < sw - 1) & (sy >= 0) & (sy < sh - 1)
    sx, sy = np.clip(sx, 0, sw - 1.001), np.clip(sy, 0, sh - 1.001)
    x0, y0 = sx.astype(int), sy.astype(int)
    fx, fy = (sx - x0)[..., None], (sy - y0)[..., None]
    c = (src[y0, x0] * (1 - fx) * (1 - fy) + src[y0, x0 + 1] * fx * (1 - fy)
         + src[y0 + 1, x0] * (1 - fx) * fy + src[y0 + 1, x0 + 1] * fx * fy)
    c[~inside] = 0
    Image.fromarray(np.clip(c, 0, 255).astype(np.uint8)).save(a.out)
    # centre magnification: output px per source px at the centre (>1 means the centre is upsampled)
    print(a.out, "centre magnification %.2f, black %.1f%%" % (f / fr * (1 if a.model != "equidistant" else 1),
                                                              100 * (1 - inside.mean())))


if __name__ == "__main__":
    main()

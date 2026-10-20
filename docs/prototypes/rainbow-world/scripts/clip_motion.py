#!/usr/bin/env python3
"""Rough motion measurements of the reference clip.

For frames sampled every --step seconds it reports:
  * roll_deg: in-plane rotation between consecutive samples, estimated with a
    Fourier-Mellin log-polar phase correlation on the magnitude spectrum of the
    central disc. Fisheye distortion plus translation make this an estimate
    only; the confidence column is the phase-correlation peak height.
  * change: mean absolute luminance difference (0-255) between the samples, a
    proxy for apparent speed.

Usage: clip_motion.py <frames> <pts.txt> [--step 0.1] [--skip-bottom 40]
"""
import argparse
import os

import numpy as np
from PIL import Image


def load(frames, i, skip_bottom):
    im = Image.open(os.path.join(frames, "f_%04d.png" % (i + 1))).convert("L")
    a = np.asarray(im, dtype=np.float32)
    if skip_bottom:
        a = a[: a.shape[0] - skip_bottom]
    h, w = a.shape
    s = min(h, w)
    y0, x0 = (h - s) // 2, (w - s) // 2
    return a[y0 : y0 + s, x0 : x0 + s]


def logpolar_mag(a, n_ang=360, n_rad=128):
    win = np.outer(np.hanning(a.shape[0]), np.hanning(a.shape[1]))
    f = np.abs(np.fft.fftshift(np.fft.fft2((a - a.mean()) * win)))
    f = np.log1p(f)
    c = a.shape[0] / 2.0
    ang = np.linspace(0, np.pi, n_ang, endpoint=False)  # magnitude spectrum is symmetric
    rad = np.exp(np.linspace(np.log(4), np.log(c - 1), n_rad))
    yy = (c + rad[None, :] * np.sin(ang[:, None])).astype(int)
    xx = (c + rad[None, :] * np.cos(ang[:, None])).astype(int)
    return f[yy, xx]


def rotation(a, b):
    la, lb = logpolar_mag(a), logpolar_mag(b)
    A, B = np.fft.fft2(la), np.fft.fft2(lb)
    r = A * np.conj(B)
    r /= np.abs(r) + 1e-9
    corr = np.real(np.fft.ifft2(r))
    k = np.unravel_index(np.argmax(corr), corr.shape)
    dang = k[0] if k[0] < corr.shape[0] // 2 else k[0] - corr.shape[0]
    return dang * 180.0 / corr.shape[0], float(corr.max())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames")
    ap.add_argument("pts")
    ap.add_argument("--step", type=float, default=0.1)
    ap.add_argument("--skip-bottom", type=int, default=40)
    a = ap.parse_args()
    pts = [float(l.strip().strip(",")) for l in open(a.pts) if l.strip()]
    t, idx = 0.0, []
    while t <= pts[-1]:
        i = min(range(len(pts)), key=lambda k: abs(pts[k] - t))
        if not idx or idx[-1] != i:
            idx.append(i)
        t += a.step
    print("t0\tt1\troll_deg\tconf\tchange")
    cum = 0.0
    for i, j in zip(idx, idx[1:]):
        fa, fb = load(a.frames, i, a.skip_bottom), load(a.frames, j, a.skip_bottom)
        rot, conf = rotation(fa, fb)
        cum += rot
        change = float(np.mean(np.abs(fa - fb)))
        print("%.3f\t%.3f\t%+.1f\t%.3f\t%.1f\tcum %+.0f" % (pts[i], pts[j], rot, conf, change, cum))


if __name__ == "__main__":
    main()

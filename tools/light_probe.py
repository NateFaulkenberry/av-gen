#!/usr/bin/env python3
"""Measure a frame's key-to-shadow ratio against the geometry it was lit with.

    python3 tools/light_probe.py <render-dir> --key-azimuth <deg> --key-elevation <deg>

A render directory here is one written with `--aov normal,shadow`: the tone-mapped PNG, the
world-space normal EXR and the directional-visibility EXR of the same frame.

Why the normals rather than two rectangles drawn on the image. A hand-placed "key side" box and
"shadow side" box measure wherever they were put, and they keep measuring it after the light has
moved -- which makes them a probe that cannot fail in the sense ADR-182 means. The normal AOV lets
the partition be made out of the thing under test: a pixel is on the key side when its surface
faces the key (n . L above +0.35) and on the shadow side when it faces away (below -0.05), and the
ratio is between the luminances the renderer actually produced in those two sets. Move the key and
the partition moves with it; switch the key off and the two sets are still there, still populated,
and the ratio collapses -- which is the control this needs and the reason the no-key arm is not
optional.

Subject pixels are those the normal AOV wrote a non-zero normal into, so the background never
enters any statistic.

Reports, and what each is for:

  shadowed_frac    fraction of SUBJECT pixels the key's own shadow map puts in shadow (visibility
                   below 0.5). The whole of the brief's section 14 in one number. A scene whose
                   subject sits past the last cascade reads 0.0000 exactly.
  key_to_shadow    mean luminance of the key-facing set over the away-facing set. Section 4.
  shadow_floor     fraction of away-facing pixels darker than 2/255 -- how much of the shadow side
                   has gone to black. Section 6 fails when this climbs.
  shadow_detail    RMS contrast within the away-facing set. Structure surviving in shadow, which
                   is the other half of section 6: a shadow side that is uniformly dark grey is
                   not "trunk detail and branch structure survive".
"""
from __future__ import annotations

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from read_exr import read_exr  # noqa: E402
from image_stats import read_png  # noqa: E402


def key_vector(azimuth_deg: float, elevation_deg: float):
    """Unit vector from the surface TOWARDS the source, matching Composition::lightAngles."""
    a, e = math.radians(azimuth_deg), math.radians(elevation_deg)
    c = math.cos(e)
    return (c * math.sin(a), math.sin(e), c * math.cos(a))


def srgb_to_linear(u: float) -> float:
    return u / 12.92 if u <= 0.04045 else ((u + 0.055) / 1.055) ** 2.4


_LIN = [srgb_to_linear(i / 255.0) for i in range(256)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("directory")
    ap.add_argument("--key-azimuth", type=float, required=True)
    ap.add_argument("--key-elevation", type=float, required=True)
    ap.add_argument("--frame", default="frame_000000")
    args = ap.parse_args()

    d = args.directory
    png = os.path.join(d, args.frame + ".png")
    nrm = os.path.join(d, args.frame + ".normal.exr")
    shd = os.path.join(d, args.frame + ".shadow.exr")

    w, h, channels, pix = read_png(png)
    wn, hn, nc = read_exr(nrm)
    if (wn, hn) != (w, h):
        raise SystemExit(f"{d}: normal AOV is {wn}x{hn}, frame is {w}x{h}")
    nx, ny, nz = nc["R"], nc["G"], nc["B"]

    vis = None
    if os.path.exists(shd):
        ws, hs, sc = read_exr(shd)
        if (ws, hs) == (w, h):
            vis = sc["R"]

    lx, ly, lz = key_vector(args.key_azimuth, args.key_elevation)

    subject = 0
    shadowed = 0
    lit_sum = 0.0
    lit_n = 0
    dark_sum = 0.0
    dark_sq = 0.0
    dark_n = 0
    dark_floor = 0
    for i in range(w * h):
        ax, ay, az = nx[i], ny[i], nz[i]
        m2 = ax * ax + ay * ay + az * az
        if m2 < 1e-6:
            continue
        subject += 1
        if vis is not None and vis[i] < 0.5:
            shadowed += 1
        m = math.sqrt(m2)
        ndl = (ax * lx + ay * ly + az * lz) / m
        j = i * channels
        lum = 0.2126 * _LIN[pix[j]] + 0.7152 * _LIN[pix[j + 1]] + 0.0722 * _LIN[pix[j + 2]]
        if ndl > 0.35:
            lit_sum += lum
            lit_n += 1
        elif ndl < -0.05:
            dark_sum += lum
            dark_sq += lum * lum
            dark_n += 1
            if lum < _LIN[2]:
                dark_floor += 1

    if subject == 0:
        raise SystemExit(f"{d}: the normal AOV is empty -- nothing was rendered")
    if lit_n == 0 or dark_n == 0:
        raise SystemExit(f"{d}: partition is empty (lit {lit_n}, away {dark_n}); the key angles "
                         f"are probably not the ones this frame was lit with")

    lit_mean = lit_sum / lit_n
    dark_mean = dark_sum / dark_n
    var = max(dark_sq / dark_n - dark_mean * dark_mean, 0.0)
    print(f"{d}")
    print(f"  subject_px       {subject}  ({100.0 * subject / (w * h):.1f}% of frame)")
    print(f"  key_facing_px    {lit_n}")
    print(f"  away_facing_px   {dark_n}")
    if vis is not None:
        print(f"  shadowed_frac    {shadowed / subject:.4f}")
    else:
        print(f"  shadowed_frac    n/a (no shadow AOV)")
    print(f"  key_mean_lum     {lit_mean:.5f}")
    print(f"  shadow_mean_lum  {dark_mean:.5f}")
    print(f"  key_to_shadow    {lit_mean / dark_mean if dark_mean > 0 else float('inf'):.3f}")
    print(f"  shadow_floor     {dark_floor / dark_n:.4f}")
    print(f"  shadow_detail    {math.sqrt(var) / dark_mean if dark_mean > 0 else 0.0:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

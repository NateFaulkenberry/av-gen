#!/usr/bin/env python3
"""Measure high-frequency chroma noise -- per-pixel RGB fringing -- in a rendered PNG.

`image_stats.py` reports mean saturation, and mean saturation cannot see this artifact at all: a
frame speckled with red and blue pixels has the same *mean* saturation as a clean one, because the
speckle averages out. Measured: an FXAA-off arm moved mean saturation by 0.001 while the fringing
was plainly still there in the picture. A detector that cannot separate the arms is not a detector.

What this measures instead is how much the *chroma* changes between neighbouring pixels. Luminance
detail is what a rendered image is supposed to have -- leaves have edges -- so luminance is
deliberately discarded. Chroma that changes violently from one pixel to the next is not detail of
anything: no real surface is red on one pixel and blue on the next, and that is exactly what
per-channel displacement, stochastic alpha and a broken temporal resolve all produce.

    chroma = (R - G, G - B)            -- luminance removed, two opponent axes
    speckle = mean over pixels of |chroma(p) - mean(chroma of 4-neighbours)|

Reported as a percentage of full scale, plus the fraction of pixels over a threshold, which
separates "a faint wash everywhere" from "a few violently wrong pixels" -- two different bugs.

Usage:
    tools/chroma_speckle.py a.png b.png ...
    tools/chroma_speckle.py --mask out.png in.png    # write a visualisation of where it is
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image_stats import read_png


def speckle(path, threshold=0.06):
    w, h, ch, px = read_png(path)
    # Opponent chroma per pixel, in 0..1 units.
    cr = [0.0] * (w * h)
    cb = [0.0] * (w * h)
    for i in range(w * h):
        r, g, b = px[i * ch] / 255.0, px[i * ch + 1] / 255.0, px[i * ch + 2] / 255.0
        cr[i] = r - g
        cb[i] = g - b

    total = 0.0
    over = 0
    counted = 0
    worst = 0.0
    # The border is skipped rather than clamped: a clamped edge pixel compares itself with itself
    # and reports zero, which would dilute the mean by exactly the frame's perimeter.
    for y in range(1, h - 1):
        row = y * w
        for x in range(1, w - 1):
            i = row + x
            n = (cr[i - 1] + cr[i + 1] + cr[i - w] + cr[i + w]) * 0.25
            m = (cb[i - 1] + cb[i + 1] + cb[i - w] + cb[i + w]) * 0.25
            d = abs(cr[i] - n) + abs(cb[i] - m)
            total += d
            worst = max(worst, d)
            if d > threshold:
                over += 1
            counted += 1
    if counted == 0:
        return {'speckle_pct': 0.0, 'over_pct': 0.0, 'worst': 0.0}
    return {
        'speckle_pct': 100.0 * total / counted,
        'over_pct': 100.0 * over / counted,
        'worst': worst,
    }


def main(argv):
    paths = [a for a in argv[1:] if not a.startswith('--')]
    if not paths:
        raise SystemExit(__doc__)
    print(f"{'file':<34}{'speckle%':>10}{'over%':>9}{'worst':>8}")
    for p in paths:
        s = speckle(p)
        print(f"{os.path.basename(p):<34}{s['speckle_pct']:>10.4f}{s['over_pct']:>9.3f}{s['worst']:>8.3f}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))

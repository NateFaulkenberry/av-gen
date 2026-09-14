#!/usr/bin/env python3
"""Mean gradient magnitude of a frame: a proxy for how much detail survived the filters.

An antialiasing change is judged on two numbers that pull in opposite directions -- how much it
stops the image flickering, and how much of the image it takes away doing it. `temporal_stats.py`
measures the first. This measures the second, so a change cannot be accepted on the flicker number
alone: a filter that blurs everything scores perfectly on flicker.

Not an absolute measure of sharpness. It is only meaningful as a *ratio* between two renders of the
same frame, which is how it is used.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image_stats import read_png


def sharpness(path):
    w, h, c, px = read_png(path)
    luma = [0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2] for i in range(0, len(px), c)]
    total = 0.0
    n = 0
    for y in range(1, h - 1):
        row = y * w
        for x in range(1, w - 1):
            i = row + x
            gx = luma[i + 1] - luma[i - 1]
            gy = luma[i + w] - luma[i - w]
            total += abs(gx) + abs(gy)
            n += 1
    return total / max(n, 1)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    base = None
    for path in argv[1:]:
        s = sharpness(path)
        if base is None:
            base = s
            print(f"{s:8.4f}  (reference)  {path}")
        else:
            print(f"{s:8.4f}  {100.0 * (s - base) / base:+6.2f}%      {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

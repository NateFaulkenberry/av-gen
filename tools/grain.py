#!/usr/bin/env python3
"""Mean absolute high-frequency residual of a frame: the grain metric ADR-389/460/461 quote.

    tools/grain.py <frame.png> [more.png ...] [--window lower|full|<x0>,<y0>,<x1>,<y1>]

`grain = mean(|luma - box3(luma)|)` over a window, in luminance code values (0-255). It exists
because three ADRs have now quoted a number by this name and each of them re-derived it in a
throwaway script; the definition lived only in prose, which is how two arms get compared with two
different measures.

**It is only meaningful between arms of the SAME view.** More genuine detail scores higher, and
that is not a defect -- it is why `spatial_stats.py` says the same thing about its own measure. A
number from one scene means nothing against a number from another.

**The window is not a detail.** ADR-389: "a metric that measures the whole frame would have missed
it -- the tree's foliage dominates the high-frequency energy of this shot, and the whole-frame
number moves by 3% across arms that move the medium's own grain by 54%." So `lower` (the default,
and what ADR-389/460/461 report) is the bottom half of the frame, where the placed medium is in the
Tree of Life hero shot. Choosing where to look is ADR-182's point met honestly, rather than by
choosing a threshold.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image_stats import read_png


def grain(path, window="lower"):
    w, h, c, px = read_png(path)
    lum = [0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2] for i in range(0, len(px), c)]
    if window == "lower":
        x0, y0, x1, y1 = 0, h // 2, w, h
    elif window == "full":
        x0, y0, x1, y1 = 0, 0, w, h
    else:
        x0, y0, x1, y1 = (int(v) for v in window.split(","))
    x0 = max(x0, 1); y0 = max(y0, 1); x1 = min(x1, w - 1); y1 = min(y1, h - 1)
    total = 0.0
    n = 0
    for y in range(y0, y1):
        row = y * w
        for x in range(x0, x1):
            i = row + x
            box = (lum[i - w - 1] + lum[i - w] + lum[i - w + 1] +
                   lum[i - 1] + lum[i] + lum[i + 1] +
                   lum[i + w - 1] + lum[i + w] + lum[i + w + 1]) / 9.0
            total += abs(lum[i] - box)
            n += 1
    return total / max(n, 1), (x1 - x0) * (y1 - y0)


def mean_luma(path, window="lower"):
    w, h, c, px = read_png(path)
    lum = [0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2] for i in range(0, len(px), c)]
    if window == "lower":
        x0, y0, x1, y1 = 0, h // 2, w, h
    elif window == "full":
        x0, y0, x1, y1 = 0, 0, w, h
    else:
        x0, y0, x1, y1 = (int(v) for v in window.split(","))
    vals = [lum[y * w + x] for y in range(y0, y1) for x in range(x0, x1)]
    return sum(vals) / max(len(vals), 1)


def main(argv):
    window = "lower"
    paths = []
    i = 1
    while i < len(argv):
        if argv[i] == "--window":
            window = argv[i + 1]
            i += 2
            continue
        paths.append(argv[i])
        i += 1
    if not paths:
        print(__doc__)
        return 2
    print(f"{'arm':<28} {'grain':>9} {'mean luma':>10}   window={window}")
    for p in paths:
        g, _ = grain(p, window)
        print(f"{os.path.basename(os.path.dirname(p)) or os.path.basename(p):<28} {g:9.4f} {mean_luma(p, window):10.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

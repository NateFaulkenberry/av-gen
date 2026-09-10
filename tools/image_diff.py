#!/usr/bin/env python3
"""Pixel diff of two PNGs: how many differ and by how much.

The evidence a render change did nothing visible, or exactly what it was meant to. Prints the
differing pixel count, the largest channel delta and the bounding box of the difference, because
"95 pixels differ" means something quite different when they are scattered than when they are a
block.
"""
import sys
from image_stats import read_png


def diff(path_a, path_b):
    wa, ha, ca, pa = read_png(path_a)
    wb, hb, cb, pb = read_png(path_b)
    if (wa, ha) != (wb, hb):
        return f"{path_a} is {wa}x{ha}, {path_b} is {wb}x{hb}"
    n, worst, count = wa * ha, 0, 0
    lo = [wa, ha]
    hi = [-1, -1]
    for i in range(n):
        d = max(abs(pa[i * ca + k] - pb[i * cb + k]) for k in range(3))
        if d:
            count += 1
            worst = max(worst, d)
            x, y = i % wa, i // wa
            lo[0], lo[1] = min(lo[0], x), min(lo[1], y)
            hi[0], hi[1] = max(hi[0], x), max(hi[1], y)
    box = f", bbox ({lo[0]},{lo[1]})-({hi[0]},{hi[1]})" if count else ""
    return f"{count} of {n} pixels differ ({100.0 * count / n:.4f}%), max channel delta {worst}{box}"


if __name__ == '__main__':
    print(diff(sys.argv[1], sys.argv[2]))

#!/usr/bin/env python3
"""Measure temporal instability in a rendered frame sequence, from the outside.

The question Priority 1 of the renderer gap-closure work has to answer is *which* temporal artifacts
actually exist, before anyone reaches for a temporal technique to fix them. A frame sequence taken
from a **static camera** answers it without needing motion vectors or reprojection: with the view
held still, every frame-to-frame difference is the scene, and the only thing left to separate is
animation from instability.

That separation is the whole of this tool, and it is the second difference in time:

    flicker(t) = | x(t+1) - 2*x(t) + x(t-1) |

A pixel that is animating -- water travelling, a character walking, a light ramping -- moves smoothly,
so its second difference is near zero however *fast* it moves. A pixel that shimmers alternates, and
alternation is exactly what a second difference is large for. First differences cannot tell the two
apart: a bright ripple crossing a pixel and a pixel flickering on and off look identical in |x(t+1) -
x(t)|, which is why "the frame changed" has never been evidence of instability.

Reported per frame and as a map, so an artifact can be *located* rather than only counted -- the
renderer has enough separately-animated subsystems that a single number would be attributed by
guesswork.
"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image_stats import read_png


def luma(px, i):
    return 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2]


def sequence(directory):
    names = sorted(f for f in os.listdir(directory) if f.endswith(".png"))
    return [os.path.join(directory, n) for n in names]


def analyze(directory, threshold=6.0, tiles=8):
    paths = sequence(directory)
    if len(paths) < 3:
        return None, f"{directory}: need at least three frames, found {len(paths)}"
    frames = []
    w = h = c = None
    for p in paths:
        fw, fh, fc, px = read_png(p)
        if w is None:
            w, h, c = fw, fh, fc
        elif (fw, fh) != (w, h):
            return None, f"{p} is {fw}x{fh}, expected {w}x{h}"
        frames.append([luma(px, (y * fw + x) * fc) for y in range(fh) for x in range(fw)])

    n = w * h
    # Peak flicker per pixel across the whole sequence, so a single-frame pop is not averaged away
    # by the frames either side of it -- which is exactly what a pop is.
    peak = [0.0] * n
    per_frame = []
    for t in range(1, len(frames) - 1):
        a, b, cf = frames[t - 1], frames[t], frames[t + 1]
        worst = 0.0
        count = 0
        for i in range(n):
            f = abs(cf[i] - 2.0 * b[i] + a[i])
            if f > peak[i]:
                peak[i] = f
            if f > threshold:
                count += 1
            if f > worst:
                worst = f
        per_frame.append((t, count, worst))

    flickering = sum(1 for v in peak if v > threshold)
    # Where it is, coarsely. A tile map is enough to say "the river" or "the canopy" and cheap enough
    # to print beside the number.
    tw, th = max(1, w // tiles), max(1, h // tiles)
    grid = []
    for ty in range(tiles):
        row = []
        for tx in range(tiles):
            worst = 0.0
            for y in range(ty * th, min((ty + 1) * th, h)):
                for x in range(tx * tw, min((tx + 1) * tw, w)):
                    v = peak[y * w + x]
                    if v > worst:
                        worst = v
            row.append(worst)
        grid.append(row)
    return {
        "frames": len(frames),
        "size": (w, h),
        "flickering_pixels": flickering,
        "flickering_percent": 100.0 * flickering / n,
        "peak": max(peak) if peak else 0.0,
        "per_frame": per_frame,
        "grid": grid,
        "threshold": threshold,
    }, None


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        print("usage: temporal_stats.py <frame-directory> [threshold]")
        return 2
    threshold = float(argv[2]) if len(argv) > 2 else 6.0
    stats, err = analyze(argv[1], threshold)
    if err:
        print(err)
        return 1
    w, h = stats["size"]
    print(f"{argv[1]}: {stats['frames']} frames at {w}x{h}, flicker threshold {threshold:.1f}/255")
    print(f"  pixels that ever flicker: {stats['flickering_pixels']} ({stats['flickering_percent']:.3f}%)")
    print(f"  peak second difference:   {stats['peak']:.1f}")
    worst = sorted(stats["per_frame"], key=lambda r: -r[1])[:3]
    for t, count, peak in worst:
        print(f"  worst frames: t={t} {count} pixels over threshold, peak {peak:.1f}")
    print("  where (peak second difference per tile, rows top to bottom):")
    for row in stats["grid"]:
        print("   " + " ".join(f"{v:5.1f}" for v in row))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

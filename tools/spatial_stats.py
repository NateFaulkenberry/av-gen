#!/usr/bin/env python3
"""Measure spatial aliasing -- jaggedness -- in a rendered frame sequence, from the outside.

This exists because `temporal_stats.py` was measuring the wrong artifact, and nothing here says
that its numbers were wrong. They are correct measurements of temporal alternation. The mistake was
using them to decide what to *fix* (ADR-243).

When §4's reviewer finally looked at the shipping picture, what they objected to was the grass:
"jagged in wind animations", at the outline "where a grass blade meets what's behind it". On that
exact region the temporal detector is **anti-correlated** with their preference:

    grass beds          temporal (2nd difference)   spatial (this file)   the reviewer
    t1 baseline                     --                      --            "wouldn't ship"
    t2 FXAA off                   -42%  "better"          +31%  worse     "more distracting"
    t4 supersample 2x              +9%  "worse"            -9%  better    "much calmer"

Both arms, the opposite of what the viewer said. This measure agrees with them on both, and in the
same order. So the two are not rivals: they measure different artifacts, and a scene can have one
without the other.

**What this measures.** The mean absolute spatial Laplacian, `|4c - left - right - up - down|`, over
the luma of each frame, averaged across frames and reported per tile. A staircased edge is
high-frequency in space and scores high; the same edge resolved or filtered scores low.

**What it cannot tell you, stated rather than assumed away.** It cannot separate *aliasing* from
*detail*. A frame with more genuine fine structure in it scores higher, and that is not a defect --
so this number is only meaningful **between arms of the same view**, never between scenes, and never
as an absolute quality bar. Two arms that differ in content differ in this number for a reason that
has nothing to do with aliasing. That is the same rule `--ab` has always had, for the same reason.

The artifact it was built to find is geometry thinner than the sampling rate: grass blades and the
gill filaments under Glowmere's hero mushroom, which at 1080p drop out of the raster entirely on some
pixels and come back on others. Supersampling fixes it at the source; FXAA hides it; and the
temporal detector rewards removing FXAA, which makes it worse.

Usage:
    tools/spatial_stats.py <frame-directory> [tiles]
    tools/spatial_stats.py <dir-a> <dir-b> --compare      # the arm comparison this is for
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image_stats import read_png


def luma(px, i):
    return 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2]


def sequence(directory):
    names = sorted(f for f in os.listdir(directory) if f.endswith(".png"))
    return [os.path.join(directory, n) for n in names]


def analyze(directory, tiles=8, limit=0):
    """Mean |spatial Laplacian| over the sequence, whole-frame and per tile.

    `limit` caps how many frames are read; the measure is spatial, so frames are independent samples
    of the same quantity and a handful is usually enough. Zero reads all of them.
    """
    paths = sequence(directory)
    if not paths:
        return None, f"{directory}: no PNG frames"
    if limit > 0:
        paths = paths[:limit]

    w = h = None
    total = 0.0
    count = 0
    grid = None
    for p in paths:
        fw, fh, fc, px = read_png(p)
        if w is None:
            w, h = fw, fh
            grid = [[0.0] * tiles for _ in range(tiles)]
            gcount = [[0] * tiles for _ in range(tiles)]
        elif (fw, fh) != (w, h):
            return None, f"{p} is {fw}x{fh}, expected {w}x{h}"
        lum = [luma(px, (y * fw + x) * fc) for y in range(fh) for x in range(fw)]
        # The border is skipped rather than clamped: a clamped edge pixel reports a Laplacian that
        # is an artifact of the clamp, and there are 2(w+h) of them.
        for y in range(1, fh - 1):
            base = y * fw
            ty = min(tiles - 1, y * tiles // fh)
            for x in range(1, fw - 1):
                q = base + x
                v = abs(4.0 * lum[q] - lum[q - 1] - lum[q + 1] - lum[q - fw] - lum[q + fw])
                total += v
                count += 1
                tx = min(tiles - 1, x * tiles // fw)
                grid[ty][tx] += v
                gcount[ty][tx] += 1
    for r in range(tiles):
        for c in range(tiles):
            if gcount[r][c]:
                grid[r][c] /= gcount[r][c]
    return {"size": (w, h), "frames": len(paths), "mean": total / max(count, 1), "grid": grid}, None


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    compare = "--compare" in argv
    if not args:
        print(__doc__)
        print("usage: spatial_stats.py <frame-directory> [tiles]")
        return 2

    if compare and len(args) >= 2:
        a, err = analyze(args[0])
        if err:
            print(err)
            return 1
        b, err = analyze(args[1])
        if err:
            print(err)
            return 1
        if a["size"] != b["size"]:
            print(f"refusing: {a['size']} vs {b['size']}. Two sizes are two experiments.")
            return 1
        delta = 100.0 * (b["mean"] - a["mean"]) / a["mean"]
        print(f"{args[0]}  mean |laplacian| {a['mean']:.3f}")
        print(f"{args[1]}  mean |laplacian| {b['mean']:.3f}  ({delta:+.0f}%)")
        print("  positive = the second arm is more jagged. Only meaningful between arms of the")
        print("  same view: this measure cannot separate aliasing from genuine detail.")
        return 0

    tiles = int(args[1]) if len(args) > 1 else 8
    stats, err = analyze(args[0], tiles)
    if err:
        print(err)
        return 1
    w, h = stats["size"]
    print(f"{args[0]}: {stats['frames']} frames at {w}x{h}")
    print(f"  mean |spatial laplacian|: {stats['mean']:.3f}")
    print("  where (per tile, rows top to bottom):")
    for row in stats["grid"]:
        print("   " + " ".join(f"{v:6.2f}" for v in row))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

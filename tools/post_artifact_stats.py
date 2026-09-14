#!/usr/bin/env python3
"""Measure the anamorphic comb in a rendered frame, from the outside.

The GPU forensics suite (tests/rendering/test_post_artifact_forensics_gpu.cpp)
measures the post chain's intermediate targets. This measures the same thing on
a finished PNG, so a claim about a *shipped* render can be checked without a
device and without trusting the renderer's own account of itself.

The quantity is the effect's own contribution -- the same frame rendered with
`post/anamorphic/enabled` on and off, subtracted -- because a streak is a small
addition to a whole image and measuring the image measures the image.

Two statistics, both from docs/post-artifact-forensics.md:

  prominence   how far the horizontal autocorrelation at a given period stands
               above the line through its anti-phase neighbours. A wide smooth
               blur correlates strongly at every small lag, so the correlation
               itself cannot tell a comb from a blur; only the tooth can. Near
               zero for a blur, strongly positive for repeated copies.

  peaks        pixels far brighter than the ring around them. A blur destroys
               isolation; a resample preserves it.

The period to look at is not a guess. `fs_wide` used to step its taps by
`stretch` texels of the quarter-resolution wide target, so the comb's period in
full-resolution pixels is `4 * stretch` -- 41.5 px at Glowmere's authored 10.386,
at any frame size.

Usage:
    tools/post_artifact_stats.py ON.png OFF.png [--period 42] [--no-peaks]
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from image_stats import read_png  # noqa: E402  (the repo's dependency-free PNG reader)


def luma_difference(on_path, off_path):
    """|on - off| as one luminance plane, which is the effect's own contribution."""
    w, h, ch, a = read_png(on_path)
    w2, h2, ch2, b = read_png(off_path)
    if (w, h) != (w2, h2):
        raise SystemExit(f"size mismatch: {w}x{h} vs {w2}x{h2}")
    out = [0.0] * (w * h)
    for i in range(w * h):
        pa, pb = i * ch, i * ch2
        la = 0.2126 * a[pa] + 0.7152 * a[pa + 1] + 0.0722 * a[pa + 2]
        lb = 0.2126 * b[pb] + 0.7152 * b[pb + 1] + 0.0722 * b[pb + 2]
        out[i] = abs(la - lb)
    return w, h, out


def column_profile(w, h, field):
    """Summed down y, which keeps horizontal repetition and discards the rest."""
    profile = [0.0] * w
    for y in range(h):
        row = y * w
        for x in range(w):
            profile[x] += field[row + x]
    mean = sum(profile) / w
    return [p - mean for p in profile]


def correlation(profile, lag):
    zero = sum(p * p for p in profile)
    if zero <= 1e-12 or lag >= len(profile):
        return 0.0
    return sum(profile[x] * profile[x + lag] for x in range(len(profile) - lag)) / zero


def prominence(profile, period):
    """The tooth: the correlation at `period` above the line through its anti-phase neighbours."""
    if period < 2 or period * 3 // 2 >= len(profile) // 2:
        return 0.0
    return correlation(profile, period) - 0.5 * (
        correlation(profile, period // 2) + correlation(profile, period * 3 // 2))


def isolated_peaks(w, h, field, factor=4.0, floor=1.0):
    """Pixels more than `factor` times the mean of the ring two texels out."""
    count = 0
    for y in range(2, h - 2):
        for x in range(2, w - 2):
            centre = field[y * w + x]
            if centre <= floor:
                continue
            ring, n = 0.0, 0
            for dy in (-2, -1, 0, 1, 2):
                for dx in (-2, -1, 0, 1, 2):
                    if abs(dx) < 2 and abs(dy) < 2:
                        continue
                    ring += field[(y + dy) * w + x + dx]
                    n += 1
            if centre > factor * (ring / n):
                count += 1
    return count


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("on", type=pathlib.Path, help="frame with the effect enabled")
    ap.add_argument("off", type=pathlib.Path, help="the same frame with it disabled")
    ap.add_argument("--period", type=int, default=42,
                    help="comb period in pixels to test for (default 42 = 4 * stretch 10.386)")
    ap.add_argument("--no-peaks", action="store_true", help="skip the isolated-peak scan (slow)")
    args = ap.parse_args()

    w, h, field = luma_difference(args.on, args.off)
    profile = column_profile(w, h, field)
    total = sum(field)
    print(f"{args.on.name} vs {args.off.name}: {w}x{h}")
    print(f"  contribution   max {max(field):.1f}  mean {total / (w * h):.4f}  "
          f"pixels changed {sum(1 for v in field if v > 0.5)}")
    print(f"  correlation at lag {args.period}: {correlation(profile, args.period):+.4f}")
    print(f"  comb prominence  at lag {args.period}: {prominence(profile, args.period):+.4f}")
    for lag in (args.period // 2, args.period, args.period * 2, args.period * 3):
        print(f"    lag {lag:>4}: correlation {correlation(profile, lag):+.4f}  "
              f"prominence {prominence(profile, lag):+.4f}")
    if not args.no_peaks:
        print(f"  isolated peaks: {isolated_peaks(w, h, field)}")


if __name__ == "__main__":
    main()

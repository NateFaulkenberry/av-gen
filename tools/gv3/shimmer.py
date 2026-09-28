#!/usr/bin/env python3
"""How much a region of a locked-off shot changes from one frame to the next -- shimmer, crawl and
aliasing on water, where nothing else in the region moves.

    python3 tools/gv3/shimmer.py build/gv3/v2.mov 170.5 171.9 --rows 0.78 1.0

Decodes the span at the video's own frame rate and reports the mean absolute luminance change per
frame inside the band of rows given (fractions of the height; the default is the bottom fifth,
where a foreground river sits), with its 99th percentile. Measured, not judged: it says whether the
surface moved more or less than another render of the same shot did, and where the change is.
"""
import subprocess
import sys

import numpy as np


def main():
    video, t0, t1 = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
    rows = (0.8, 1.0)
    if "--rows" in sys.argv:
        k = sys.argv.index("--rows")
        rows = (float(sys.argv[k + 1]), float(sys.argv[k + 2]))
    w, h = 480, 270
    raw = subprocess.run(["ffmpeg", "-v", "error", "-ss", f"{t0:.4f}", "-t", f"{t1 - t0:.4f}", "-i", video,
                          "-vf", f"scale={w}:{h}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                         capture_output=True, check=True).stdout
    f = np.frombuffer(raw, np.uint8).reshape(-1, h, w, 3).astype(np.float32) / 255.0
    lum = 0.2126 * f[..., 0] + 0.7152 * f[..., 1] + 0.0722 * f[..., 2]
    band = lum[:, int(rows[0] * h):int(rows[1] * h)]
    diff = np.abs(np.diff(band, axis=0))
    per_frame = diff.mean(axis=(1, 2))
    print(f"{len(f)} frames, rows {rows[0]:.2f}-{rows[1]:.2f}: mean change {per_frame.mean():.4f}/frame, "
          f"p99 pixel change {np.percentile(diff, 99):.4f}, band mean {band.mean():.4f}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Dead space in frames: how much of each picture is empty, not merely dark.

A frame is first smoothed (a 9-pixel box at 1080p, scaled with the frame), so film grain, dither and sparse
noise do not count as content. It is then split into a grid of tiles. A tile is EMPTY when nothing is there: its
99th-percentile luma is below `--black` (default 6/255) and its spread (p99 - p1) is below `--flat` (default
3/255). A dark tile with a faint
vein, a star or haze in it is not empty -- dark is allowed, a void is not. For each frame it reports the empty
fraction and the largest 4-connected empty region (as fractions of the frame), and fails a frame whose empty
fraction or largest region exceeds the limits.

    tools/frame_coverage.py <png|jpg ...> | --video clip.mp4 [--every 2.0] [--grid 32x18]
                            [--max-empty 0.03] [--max-region 0.015] [--json out.json]

Exit status: 0 when every frame passes, 1 otherwise (2 on bad input).
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image


def measure(img: Image.Image, grid=(32, 18), black=6 / 255, flat=3 / 255) -> dict:
    from PIL import ImageFilter
    img = img.convert("RGB")
    radius = max(2, round(4 * img.height / 1080))
    img = img.filter(ImageFilter.BoxBlur(radius))
    a = np.asarray(img, dtype=np.float32) / 255.0
    lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    h, w = lum.shape
    gx, gy = grid
    empty = np.zeros((gy, gx), bool)
    for j in range(gy):
        for i in range(gx):
            t = lum[j * h // gy:(j + 1) * h // gy, i * w // gx:(i + 1) * w // gx]
            p1, p99 = np.percentile(t, [1, 99])
            empty[j, i] = p99 < black and (p99 - p1) < flat
    # largest 4-connected empty region
    seen = np.zeros_like(empty)
    largest = 0
    for j in range(gy):
        for i in range(gx):
            if empty[j, i] and not seen[j, i]:
                stack, n = [(j, i)], 0
                seen[j, i] = True
                while stack:
                    y, x = stack.pop()
                    n += 1
                    for yy, xx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                        if 0 <= yy < gy and 0 <= xx < gx and empty[yy, xx] and not seen[yy, xx]:
                            seen[yy, xx] = True
                            stack.append((yy, xx))
                largest = max(largest, n)
    total = gx * gy
    return {"empty_frac": round(float(empty.sum()) / total, 4), "largest_region_frac": round(largest / total, 4),
            "mean_luma": round(float(lum.mean()), 4), "empty_rows": [int(r) for r in empty.sum(axis=1)]}


def frames_from_video(path: Path, every: float):
    tmp = Path(tempfile.mkdtemp(prefix="coverage-"))
    subprocess.run(["ffmpeg", "-loglevel", "error", "-i", str(path), "-vf", f"fps=1/{every}",
                    str(tmp / "f_%05d.png")], check=True)
    return [(f"{path.name}@{(k) * every:.1f}s", p) for k, p in enumerate(sorted(tmp.glob("*.png")))]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("images", nargs="*")
    ap.add_argument("--video")
    ap.add_argument("--every", type=float, default=2.0)
    ap.add_argument("--grid", default="32x18")
    ap.add_argument("--black", type=float, default=6 / 255)
    ap.add_argument("--flat", type=float, default=3 / 255)
    ap.add_argument("--max-empty", type=float, default=0.03)
    ap.add_argument("--max-region", type=float, default=0.015)
    ap.add_argument("--json")
    a = ap.parse_args()
    gx, gy = (int(v) for v in a.grid.split("x"))
    items = [(Path(p).name, Path(p)) for p in a.images]
    if a.video:
        items += frames_from_video(Path(a.video), a.every)
    if not items:
        print("no frames", file=sys.stderr)
        return 2
    rows, failed = [], 0
    for name, p in items:
        m = measure(Image.open(p), (gx, gy), a.black, a.flat)
        ok = m["empty_frac"] <= a.max_empty and m["largest_region_frac"] <= a.max_region
        failed += 0 if ok else 1
        rows.append({"frame": name, "pass": ok, **m})
        print(f"{'ok  ' if ok else 'FAIL'} {name:40s} empty {100 * m['empty_frac']:5.1f}%  "
              f"largest {100 * m['largest_region_frac']:5.1f}%  mean luma {m['mean_luma']:.3f}")
    worst = max(rows, key=lambda r: r["empty_frac"])
    summary = {"frames": len(rows), "failed": failed, "max_empty_frac": worst["empty_frac"],
               "max_region_frac": max(r["largest_region_frac"] for r in rows),
               "limits": {"max_empty": a.max_empty, "max_region": a.max_region, "black": a.black, "flat": a.flat,
                          "grid": [gx, gy]}}
    print(f"{len(rows)} frames, {failed} failed; worst empty {100 * summary['max_empty_frac']:.1f}%, "
          f"worst region {100 * summary['max_region_frac']:.1f}%")
    if a.json:
        Path(a.json).write_text(json.dumps({"summary": summary, "frames": rows}, indent=1))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Review a rendered cut shot by shot: contact sheets, image measurements, and flags.

    python3 tools/gv3/review.py build/gv3/v0.mov build/gv3/shots.json build/gv3/review-v0

Writes, into the output directory:
  * sheet-NN.png  -- one row per shot, frames at its start, middle and end (tools/contact_sheet.py)
  * report.md     -- one line per shot: exposure, contrast, colour, motion between its frames, and
                     the flags a reviewer should look at first

The measurements are the ones a value structure is judged on (tools/image_stats.py's reasoning:
statistics only say whether a change moved the image the way it was meant to, never whether the
frame is worth looking at). The flags are deliberately blunt -- a black frame, a frame with no
contrast, a shot whose subject cannot have moved because nothing in it did -- so the eye can go
straight to the shots that need it. Needs Pillow and numpy.
"""
import io
import json
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

SAMPLES = 3            # frames per shot: start, middle, end (inset from the cuts)
INSET = 0.12
ROWS_PER_SHEET = 10


def frame(video, t):
    out = subprocess.run(["ffmpeg", "-v", "error", "-ss", f"{t:.4f}", "-i", str(video), "-frames:v", "1",
                          "-f", "image2pipe", "-vcodec", "png", "-"], capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(out)).convert("RGB")


def measure(img):
    a = np.asarray(img).astype(np.float32) / 255.0
    lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    mx, mn = a.max(axis=2), a.min(axis=2)
    sat = np.where(mx > 1e-4, (mx - mn) / np.maximum(mx, 1e-4), 0.0)
    return {
        "mean": float(lum.mean()), "p01": float(np.percentile(lum, 1)), "p50": float(np.percentile(lum, 50)),
        "p99": float(np.percentile(lum, 99)), "rms": float(lum.std()),
        "clip": float((a.max(axis=2) > 0.995).mean()), "sat": float(sat[lum > 0.02].mean()) if (lum > 0.02).any() else 0.0,
        "lum": lum,
    }


def flags(m, moved, duration):
    out = []
    if m["mean"] < 0.02:
        out.append("NEAR-BLACK")
    elif m["mean"] < 0.05:
        out.append("dark")
    if m["rms"] < 0.035:
        out.append("flat (no contrast)")
    if m["clip"] > 0.02:
        out.append(f"clipping {m['clip'] * 100:.1f}%")
    if moved < 0.004 and duration > 1.5:
        out.append("static image")
    return out


def main():
    video, shots_json, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    out.mkdir(parents=True, exist_ok=True)
    shots = json.loads(shots_json.read_text())["shots"]
    from contact_sheet import compose  # noqa: E402

    rows, report = [], ["| Shot | Time | Segment | Label | mean | p99 | contrast | sat | motion | flags |",
                        "|---|---|---|---|---|---|---|---|---|---|"]
    for s in shots:
        a, b = float(s["start"]), float(s["end"])
        lo, hi = a + INSET, max(a + INSET, b - INSET)
        times = [lo, 0.5 * (lo + hi), hi] if SAMPLES == 3 else [0.5 * (a + b)]
        imgs = [frame(video, t) for t in times]
        ms = [measure(i) for i in imgs]
        mid = ms[len(ms) // 2]
        moved = float(np.mean([np.abs(ms[i]["lum"] - ms[i + 1]["lum"]).mean() for i in range(len(ms) - 1)]))
        f = flags(mid, moved, b - a)
        report.append(f"| {s['id']} | {a:.2f}-{b:.2f} | {s.get('segment', '')} | {s.get('label', '')[:40]} | "
                      f"{mid['mean']:.3f} | {mid['p99']:.2f} | {mid['rms']:.3f} | {mid['sat']:.2f} | {moved:.3f} | "
                      f"{', '.join(f)} |")
        rows.append((s, imgs, times))
    for k in range(0, len(rows), ROWS_PER_SHEET):
        chunk = rows[k:k + ROWS_PER_SHEET]
        cells, titles = [], []
        for s, imgs, times in chunk:
            titles.append(f"{s['id']}  {s['start']:.2f}-{s['end']:.2f}s  {s.get('segment', '')}  {s.get('label', '')[:60]}")
            cells += [(im, f"{t:.2f}s") for im, t in zip(imgs, times)]
        sheet = compose(cells, SAMPLES, 400, titles)
        sheet.save(out / f"sheet-{k // ROWS_PER_SHEET + 1:02d}.png")
    (out / "report.md").write_text("\n".join(report) + "\n")
    print(f"{len(shots)} shots reviewed -> {out}")


if __name__ == "__main__":
    main()

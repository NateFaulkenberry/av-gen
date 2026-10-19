#!/usr/bin/env python3
"""Timestamped contact sheets from a frame dump of the reference clip.

Usage:
  ffmpeg -i clip.mov -fps_mode passthrough <frames>/f_%04d.png
  ffprobe -v error -select_streams v -show_entries frame=pts_time -of csv=p=0 clip.mov > pts.txt
  clip_sheet.py <frames> <pts.txt> <out.png> --every 0.25 [--start 0 --end 8.4] [--cols 6] [--scale 0.5]
  clip_sheet.py <frames> <pts.txt> <out.png> --times 1.0,1.1,1.2

Picks, for each requested time, the frame whose pts is nearest, and labels it
with the real pts and frame index (the clip is variable frame rate).
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFont


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames")
    ap.add_argument("pts")
    ap.add_argument("out")
    ap.add_argument("--every", type=float, default=0.0)
    ap.add_argument("--times", default="")
    ap.add_argument("--start", type=float, default=0.0)
    ap.add_argument("--end", type=float, default=1e9)
    ap.add_argument("--cols", type=int, default=6)
    ap.add_argument("--scale", type=float, default=0.5)
    ap.add_argument("--title", default="")
    a = ap.parse_args()

    pts = [float(l.strip().strip(",")) for l in open(a.pts) if l.strip()]
    end = min(a.end, pts[-1])
    if a.times:
        want = [float(t) for t in a.times.split(",")]
    else:
        want, t = [], a.start
        while t <= end + 1e-9:
            want.append(round(t, 4))
            t += a.every
    picks = []
    for t in want:
        i = min(range(len(pts)), key=lambda k: abs(pts[k] - t))
        if not picks or picks[-1] != i:
            picks.append(i)

    first = Image.open(os.path.join(a.frames, "f_0001.png"))
    w, h = int(first.width * a.scale), int(first.height * a.scale)
    cols = a.cols
    rows = (len(picks) + cols - 1) // cols
    top = 28 if a.title else 0
    sheet = Image.new("RGB", (cols * w, rows * (h + 18) + top), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 13)
        tfont = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 16)
    except OSError:
        font = tfont = ImageFont.load_default()
    if a.title:
        d.text((8, 5), a.title, fill=(240, 240, 240), font=tfont)
    for n, i in enumerate(picks):
        im = Image.open(os.path.join(a.frames, "f_%04d.png" % (i + 1))).convert("RGB")
        im = im.resize((w, h), Image.LANCZOS)
        x, y = (n % cols) * w, top + (n // cols) * (h + 18)
        sheet.paste(im, (x, y + 18))
        d.text((x + 4, y + 2), "t=%.3fs  #%d" % (pts[i], i + 1), fill=(255, 220, 120), font=font)
    sheet.save(a.out)
    print(a.out, len(picks), "frames")


if __name__ == "__main__":
    main()

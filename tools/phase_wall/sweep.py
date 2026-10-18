#!/usr/bin/env python3
"""The "no dead space" gate (docs/prototypes/phase-wall/02-plan.md, Stage 2).

    python3 tools/phase_wall/sweep.py write <dir> [--palette i] [--time t]   # review projects + a render list
    python3 tools/phase_wall/sweep.py score <dir>                            # contact sheet + the metric

Five camera positions spread through the hall, each looking round at six yaws and two pitches (60 frames). A frame
fails when 5% or more of it is DEAD: blocks of 16 px that are both near-black (mean linear luminance under 0.8%, about sRGB 23/255; the R&D A3 void scores 37-41% of its frame) and featureless
(standard deviation under 1%). Designed darks pass: they carry hue and structure; a void does not.
"""
import glob
import json
import math
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
from tools.phase_wall import build  # noqa: E402

POSITIONS = [(-14.0, 4.0, 48.0), (12.0, 9.0, 20.0), (0.0, 3.0, -10.0), (-16.0, 14.0, -40.0), (14.0, 6.0, -56.0)]
YAWS = [0, 60, 120, 180, 240, 300]
PITCHES = [-10, 20]
BLOCK, DARK, FLAT, LIMIT = 16, 0.008, 0.01, 0.05


def write(out, palette=0, time=28.0):
    os.makedirs(out, exist_ok=True)
    pp = os.path.join(build.OUT, build.SID + ".json")
    lines = []
    for i, p in enumerate(POSITIONS):
        for y in YAWS:
            for pt in PITCHES:
                d = (math.sin(math.radians(y)) * math.cos(math.radians(pt)), math.sin(math.radians(pt)),
                     -math.cos(math.radians(y)) * math.cos(math.radians(pt)))
                t = [p[k] + 20.0 * d[k] for k in range(3)]
                cam = "%g,%g,%g:%g,%g,%g" % (p + tuple(t))
                name = "p%d-y%03d-%s%02d" % (i, y, "u" if pt > 0 else "d", abs(pt))
                v = build.write_variant(pp, os.path.join(out, name + ".json"), cam=cam, palette=palette)
                lines.append("%s %s %g:%g 480x270" % (v, os.path.join(out, "f", name), time - 0.05, time))
    with open(os.path.join(out, "list.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(os.path.join(out, "list.txt"), len(lines))


def score(out):
    from PIL import Image
    frames = sorted(glob.glob(os.path.join(out, "f", "*", "frame_000001.png")))
    worst, fails = 0.0, []
    for fr in frames:
        a = np.asarray(Image.open(fr).convert("RGB"), dtype=np.float64) / 255.0
        lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
        lum = lin @ np.array([0.2126, 0.7152, 0.0722])
        h, w = (lum.shape[0] // BLOCK) * BLOCK, (lum.shape[1] // BLOCK) * BLOCK
        b = lum[:h, :w].reshape(h // BLOCK, BLOCK, w // BLOCK, BLOCK).transpose(0, 2, 1, 3).reshape(-1, BLOCK * BLOCK)
        dead = float(np.mean((b.mean(1) < DARK) & (b.std(1) < FLAT)))
        worst = max(worst, dead)
        if dead >= LIMIT:
            fails.append((os.path.basename(os.path.dirname(fr)), round(dead, 3)))
    sheet = os.path.join(out, "sweep.png")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-pattern_type", "glob", "-i",
                    os.path.join(out, "f", "*", "frame_000001.png"), "-vf", "scale=240:-1,tile=12x5", "-frames:v", "1",
                    sheet], check=True)
    print("frames=%d worst dead fraction=%.3f fails=%d %s" % (len(frames), worst, len(fails), fails[:8]))
    print(sheet)
    return 0 if not fails and frames else 1


if __name__ == "__main__":
    if sys.argv[1] == "write":
        args = sys.argv[2:]
        pal = int(args[args.index("--palette") + 1]) if "--palette" in args else 0
        tm = float(args[args.index("--time") + 1]) if "--time" in args else 28.0
        write(args[0], pal, tm)
    else:
        sys.exit(score(sys.argv[2]))

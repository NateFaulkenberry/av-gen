#!/usr/bin/env python3
"""Cymatic World's palette comparison mode (12-brief-multi-palette.md section 5).

Renders the ten palettes on the SAME frame -- the same geometry, light, camera and exposure, only the palette differs
(each one selected and locked, ADR-1420) -- and lays them out as one contact sheet:

    python3 tools/cymatic_world/palette_sheet.py --time 60 --out sheet.png [--track feline-footwear] [--size 960x540]
    python3 tools/cymatic_world/palette_sheet.py --time 60 --only 8 --out silver.png      # one palette

Each frame goes through tools/gpu-lock.sh. The scene carries no audio; the track is fed with --audio (ADR-1408).
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)


def render(index: int, t: float, track: str, size: str, work: str) -> str:
    env = dict(os.environ, CW_PALETTE="M", CW_PAL=str(index), CW_PAL_LOCK="1")
    project = os.path.join(work, "palette-%d.json" % index)
    subprocess.run([sys.executable, os.path.join(HERE, "build.py"), "--variant", "--out", project], env=env,
                   check=True, stdout=subprocess.DEVNULL)
    out = os.path.join(work, "frames-%d" % index)
    os.makedirs(out, exist_ok=True)
    subprocess.run([os.path.join(REPO, "tools", "gpu-lock.sh"), os.path.join(REPO, "build", "release", "src", "avgen"),
                    "--headless", "--project", project, "--audio", os.path.join(REPO, "assets", "audio", track + ".wav"),
                    "--render", out, "--format", "png", "--range", "%g:%g" % (t, t + 0.034), "--size", size],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return os.path.join(out, sorted(f for f in os.listdir(out) if f.endswith(".png"))[0])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--time", type=float, required=True, help="the track time, seconds")
    ap.add_argument("--out", required=True, help="the contact sheet (PNG)")
    ap.add_argument("--track", default="feline-footwear", help="assets/audio/<track>.wav")
    ap.add_argument("--size", default="960x540")
    ap.add_argument("--only", type=int, help="one palette index (0-9) instead of all ten")
    a = ap.parse_args()
    from PIL import Image, ImageDraw
    import build
    indices = [a.only] if a.only is not None else list(range(len(build.MULTI_PALETTES)))
    with tempfile.TemporaryDirectory() as work:
        frames = [render(i, a.time, a.track, a.size, work) for i in indices]
        subprocess.run([sys.executable, os.path.join(HERE, "build.py")], check=True, stdout=subprocess.DEVNULL)
        ims = [Image.open(f).convert("RGB") for f in frames]
        w, h = ims[0].size
        cols = min(5, len(ims))
        rows = (len(ims) + cols - 1) // cols
        sheet = Image.new("RGB", (w * cols, h * rows))
        for k, (i, im) in enumerate(zip(indices, ims)):
            x, y = (k % cols) * w, (k // cols) * h
            sheet.paste(im, (x, y))
            ImageDraw.Draw(sheet).text((x + 8, y + 8), "%s %s  (%s %gs)" % ("ABCDEFGHIJ"[i], build.MULTI_PALETTES[i][0],
                                                                         a.track, a.time), fill=(255, 255, 255))
        sheet.save(a.out)
    print(a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

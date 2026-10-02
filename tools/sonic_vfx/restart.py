#!/usr/bin/env python3
"""Review media for the art restart (03-brief-art-restart.md): the SILENT still first, the reactive clip after.

    python3 tools/sonic_vfx/restart.py silent <scene-id> [--at 6] [--size 1920x1080] [--projects DIR] [--out FILE]
    python3 tools/sonic_vfx/restart.py blockout <scene-id> [...]     # a silent still at 960x540 into work/blockout
    python3 tools/sonic_vfx/restart.py clip <scene-id> [--class full] [--size 1920x1080] [--seconds 20]
    python3 tools/sonic_vfx/restart.py ba <scene-id> --old NN-old-id   # the before/after pair against 25-sonic-vfx
    python3 tools/sonic_vfx/restart.py sheet                           # every silent still on one contact sheet

Files go to ~/Desktop/av-gen-review/27-sonic-art-restart/, named by set-list position (`01-cenote-silent.png`,
`01-cenote-reactive.mp4`, `01-cenote-before-after.jpg`). Every render goes through tools/gpu-lock.sh with the pinned
engine (review.py's PIN), one job per hold.

The silent still is `variant.make_silent`: every route, interpret source, publish source and triggered effect removed,
no live input, no audio, no notes. It is rendered at its time directly (a silent world has no history to play
through) after a particle warm-up.
"""
import argparse
import glob
import importlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.sonic_vfx import review  # noqa: E402
from tools.sonic_vfx.variant import make_silent  # noqa: E402

OUT = os.path.expanduser("~/Desktop/av-gen-review/27-sonic-art-restart")
OLD = os.path.expanduser("~/Desktop/av-gen-review/25-sonic-vfx")


def scene_ids():
    from tools.sonic_vfx.scenes import SCENES
    return [importlib.import_module("tools.sonic_vfx.scenes." + n).ID for n in SCENES]


def numbered(sid):
    ids = scene_ids()
    n = ids.index(sid) + 1 if sid in ids else 99
    return "%02d-%s" % (n, sid)


def project_path(sid, projects=None):
    return os.path.join(projects or os.path.join(REPO, "examples", "sonic-vfx"), sid + ".json")


def silent_still(sid, at, size, dst, projects=None, tier="realtime", warmup=240):
    work = os.path.join(OUT, "work", "silent")
    os.makedirs(work, exist_ok=True)
    proj = make_silent(project_path(sid, projects), os.path.join(work, sid + "--silent.json"))
    d = os.path.join(work, "%s--%s" % (sid, size))
    shutil.rmtree(d, ignore_errors=True)
    review.run(["--headless", "--project", proj, "--render", d, "--format", "png", "--range",
                "%.3f:%.3f" % (at, at + 0.04), "--size", size, "--fps", "25", "--particle-warmup", str(warmup),
                "--tier", tier])
    got = sorted(glob.glob(os.path.join(d, "*.png")))
    if not got:
        print("no frame rendered for", sid)
        return None
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    shutil.copy(got[0], dst)
    print(dst)
    return dst


def reactive_clip(sid, cls, size, seconds, dst, projects=None, tier="realtime"):
    work = os.path.join(OUT, "work", "reactive")
    os.makedirs(work, exist_ok=True)
    proj, dur = review.variant(sid, cls, work, projects)
    end = min(dur, seconds) if seconds else dur
    review.run(["--headless", "--project", proj, "--render", dst, "--range", "0:%.2f" % end, "--size", size,
                "--fps", "30", "--codec", "h264", "--quality", "90", "--particle-warmup", "120", "--tier", tier])
    print(dst)
    return dst


def before_after(sid, old, dst):
    """The old pass's still (from its capture clip, 12 s in: the frame the analysis used) beside the new silent
    still, captioned."""
    from PIL import Image, ImageDraw
    new = os.path.join(OUT, numbered(sid) + "-silent.png")
    if not os.path.exists(new):
        sys.exit("no silent still for %s: run `restart.py silent %s` first" % (sid, sid))
    old_png = os.path.join(OUT, "work", "old-" + old + ".png")
    if not os.path.exists(old_png):
        os.makedirs(os.path.dirname(old_png), exist_ok=True)
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-ss", "12", "-i", os.path.join(OLD, old + ".mp4"),
                        "-frames:v", "1", old_png], check=True)
    a, b = Image.open(old_png).convert("RGB"), Image.open(new).convert("RGB")
    w, h = 960, 540
    a, b = a.resize((w, h)), b.resize((w, h))
    canvas = Image.new("RGB", (w * 2 + 12, h + 44), (18, 18, 18))
    canvas.paste(a, (0, 44))
    canvas.paste(b, (w + 12, 44))
    d = ImageDraw.Draw(canvas)
    d.text((10, 14), "BEFORE  (rejected pass, %s, with audio)" % old, fill=(220, 220, 220))
    d.text((w + 22, 14), "AFTER  (%s, SILENT: no modulation)" % sid, fill=(220, 220, 220))
    canvas.save(dst, quality=90)
    print(dst)


def sheet(dst, cols=4):
    from PIL import Image, ImageDraw
    paths = [os.path.join(OUT, numbered(s) + "-silent.png") for s in scene_ids()]
    paths = [p for p in paths if os.path.exists(p)]
    if not paths:
        sys.exit("no silent stills yet")
    w, h = 480, 270
    rows = (len(paths) + cols - 1) // cols
    canvas = Image.new("RGB", (w * cols, h * rows), (0, 0, 0))
    d = ImageDraw.Draw(canvas)
    for i, p in enumerate(paths):
        im = Image.open(p).convert("RGB").resize((w, h))
        x, y = (i % cols) * w, (i // cols) * h
        canvas.paste(im, (x, y))
        d.text((x + 8, y + 6), os.path.basename(p).replace("-silent.png", ""), fill=(255, 255, 255))
    canvas.save(dst, quality=90)
    print(dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["silent", "blockout", "clip", "ba", "sheet"])
    ap.add_argument("scene", nargs="?")
    ap.add_argument("--at", type=float, default=6.0)
    ap.add_argument("--size", default="")
    ap.add_argument("--class", dest="cls", default="full")
    ap.add_argument("--seconds", type=float, default=0.0)
    ap.add_argument("--projects", default="")
    ap.add_argument("--out", default="")
    ap.add_argument("--old", default="")
    ap.add_argument("--tier", default="realtime")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    proj = a.projects or None
    if a.mode == "silent":
        silent_still(a.scene, a.at, a.size or "1920x1080",
                     a.out or os.path.join(OUT, numbered(a.scene) + "-silent.png"), proj, a.tier)
    elif a.mode == "blockout":
        silent_still(a.scene, a.at, a.size or "960x540",
                     a.out or os.path.join(OUT, "work", "blockout", a.scene + ".png"), proj, a.tier)
    elif a.mode == "clip":
        reactive_clip(a.scene, a.cls, a.size or "1920x1080", a.seconds,
                      a.out or os.path.join(OUT, numbered(a.scene) + "-reactive.mp4"), proj, a.tier)
    elif a.mode == "ba":
        before_after(a.scene, a.old, a.out or os.path.join(OUT, numbered(a.scene) + "-before-after.jpg"))
    else:
        sheet(a.out or os.path.join(OUT, "00-silent-contact-sheet.jpg"))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""SONIC ABSTRACT: the abstract direction's eight prototypes (04-brief-abstract-direction.md), their own project
(examples/sonic-abstract/, the index category "Sonic Abstract"). Builds them, and makes the review media: each a still, a
reactive clip with real music, a MIDI clip where it is MIDI-driven, and its modulation map.

    python3 tools/sonic_vfx/abstract.py build [module ...]             # write examples/sonic-abstract/ (no module: all
                                                                       #   eight, and the index's Sonic Abstract entries)
    python3 tools/sonic_vfx/abstract.py build <module> --projects DIR  # look development elsewhere
    python3 tools/sonic_vfx/abstract.py music                          # cut the real-music excerpts (gitignored)
    python3 tools/sonic_vfx/abstract.py blockout <id> [--at 6] [--projects DIR] [--tag v2]  # silent, 960x540
    python3 tools/sonic_vfx/abstract.py silent <id> [--at 6] [--size 1920x1080]             # the silent still
    python3 tools/sonic_vfx/abstract.py clip <id> [--class allyougot] [--seconds 30] [--size 1920x1080]
    python3 tools/sonic_vfx/abstract.py frame <id> --class allyougot --at 12   # one frame of a reactive clip
    python3 tools/sonic_vfx/abstract.py sheet [--blockouts]                     # contact sheet of the eight

Files go to ~/Desktop/av-gen-review/28-sonic-abstract/, named by set-list position (`01-sacred-geometry-still.png`,
`01-sacred-geometry-allyougot.mp4`). Every render goes through tools/gpu-lock.sh with the pinned engine (review.py's
PIN), one job per hold.

Classes: `allyougot` and `rebuild` are 30 s excerpts of the owner's tracks (audio only); `full`, `chords`, `lead`, ...
are tools/sonic_vfx/make_test_material.py's synthesized MIDI material (audio plus the MIDI file).
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
from tools.sonic_vfx import review, kit  # noqa: E402
from tools.sonic_vfx.variant import make_silent  # noqa: E402

OUT = os.path.expanduser("~/Desktop/av-gen-review/28-sonic-abstract")
# The excerpts: (source on the owner's Desktop, start second, length). All You Got from 34 s builds into the bass
# entry at about 42 s; Rebuild from 156 s holds a breakdown (162-178 s) and the return at 180 s.
MUSIC = {
    "allyougot": (os.path.expanduser("~/Desktop/All You Got.wav"), 34.0, 30.0),
    "rebuild": (os.path.expanduser("~/Desktop/Rebuild.mp3"), 156.0, 30.0),
}


def scene_ids():
    from tools.sonic_vfx.scenes import ABSTRACT
    return [importlib.import_module("tools.sonic_vfx.scenes." + n).ID for n in ABSTRACT]


def build(modules, out_dir=None):
    from tools.sonic_vfx.scenes import ABSTRACT
    names = modules or ABSTRACT
    built = []
    for name in names:
        sc = importlib.import_module("tools.sonic_vfx.scenes." + name).build()
        pp, _ = sc.write(out_dir or kit.ABSTRACT_DIR)
        built.append(sc)
        print("%-24s %3d nodes %2d effects %4d routes %3d mappings -> %s" % (
            sc.id, len(sc.nodes), len(sc.effects), len(sc.routes),
            len(sc.mappings) + len(sc.mappings2) + len(sc.mappings3), os.path.relpath(pp, REPO)))
    if not modules and not out_dir:
        kit.write_index(built, category=kit.ABSTRACT_CATEGORY, out_dir=kit.ABSTRACT_DIR, doc="ABSTRACT-PLAN.md")
        print("examples/index.json: the %s category lists %d scenes" % (kit.ABSTRACT_CATEGORY, len(built)))


def numbered(sid):
    ids = scene_ids()
    n = ids.index(sid) + 1 if sid in ids else 99
    return "%02d-%s" % (n, sid)


def project_path(sid, projects=None):
    return os.path.join(projects or kit.ABSTRACT_DIR, sid + ".json")


def cut_music():
    for cls, (src, start, length) in MUSIC.items():
        dst = os.path.join(REPO, "assets", "audio", review.REAL_MUSIC[cls])
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(start), "-t", str(length), "-i", src, "-ar",
                        "48000", "-ac", "2", "-c:a", "pcm_s16le", dst], check=True)
        print(dst)


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
    proj, dur = review.variant(sid, cls, work, projects or kit.ABSTRACT_DIR)
    end = min(dur, seconds) if seconds else dur
    review.run(["--headless", "--project", proj, "--render", dst, "--range", "0:%.2f" % end, "--size", size,
                "--fps", "30", "--codec", "h264", "--quality", "90", "--particle-warmup", "120", "--tier", tier])
    print(dst)
    return dst


def clip_frame(clip, at, dst):
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-ss", "%.3f" % at, "-i", clip, "-frames:v", "1", "-update", "1",
                    dst], check=False)
    print(dst)


def sheet(dst, blockouts=False, cols=4):
    from PIL import Image, ImageDraw
    if blockouts:
        paths = [os.path.join(OUT, "work", "blockout", s + ".png") for s in scene_ids()]
    else:
        paths = [os.path.join(OUT, numbered(s) + "-still.png") for s in scene_ids()]
    paths = [p for p in paths if os.path.exists(p)]
    if not paths:
        sys.exit("no stills yet")
    w, h = 480, 270
    rows = (len(paths) + cols - 1) // cols
    canvas = Image.new("RGB", (w * cols, h * rows), (0, 0, 0))
    d = ImageDraw.Draw(canvas)
    for i, p in enumerate(paths):
        im = Image.open(p).convert("RGB").resize((w, h), Image.LANCZOS)
        x, y = (i % cols) * w, (i // cols) * h
        canvas.paste(im, (x, y))
        d.text((x + 8, y + 6), os.path.basename(p).replace("-still.png", "").replace(".png", ""),
               fill=(255, 255, 255))
    canvas.save(dst, quality=92)
    print(dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["build", "music", "silent", "blockout", "clip", "frame", "sheet"])
    ap.add_argument("scene", nargs="*")
    ap.add_argument("--at", type=float, default=6.0)
    ap.add_argument("--size", default="")
    ap.add_argument("--class", dest="cls", default="allyougot")
    ap.add_argument("--seconds", type=float, default=0.0)
    ap.add_argument("--projects", default="")
    ap.add_argument("--out", default="")
    ap.add_argument("--tag", default="", help="a suffix for a blockout iteration (work/blockout/<id>-<tag>.png)")
    ap.add_argument("--tier", default="realtime")
    ap.add_argument("--blockouts", action="store_true")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    proj = a.projects or None
    if a.mode == "build":
        build(a.scene, proj)
        return
    a.scene = a.scene[0] if a.scene else None
    if a.mode == "music":
        cut_music()
    elif a.mode == "silent":
        silent_still(a.scene, a.at, a.size or "1920x1080",
                     a.out or os.path.join(OUT, numbered(a.scene) + "-still.png"), proj, a.tier)
    elif a.mode == "blockout":
        name = a.scene + ("-" + a.tag if a.tag else "") + ".png"
        silent_still(a.scene, a.at, a.size or "960x540", a.out or os.path.join(OUT, "work", "blockout", name), proj,
                     a.tier)
    elif a.mode == "clip":
        reactive_clip(a.scene, a.cls, a.size or "1920x1080", a.seconds,
                      a.out or os.path.join(OUT, "%s-%s.mp4" % (numbered(a.scene), a.cls)), proj, a.tier)
    elif a.mode == "frame":
        clip = os.path.join(OUT, "%s-%s.mp4" % (numbered(a.scene), a.cls))
        clip_frame(clip, a.at, a.out or os.path.join(OUT, "%s-%s-%04.1f.png" % (numbered(a.scene), a.cls, a.at)))
    else:
        sheet(a.out or os.path.join(OUT, "00-contact-sheet%s.jpg" % ("-blockouts" if a.blockouts else "")),
              a.blockouts)


if __name__ == "__main__":
    main()

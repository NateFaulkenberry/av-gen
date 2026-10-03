#!/usr/bin/env python3
"""Review captures for the Sonic VFX scenes (deliverable: a still and a short clip with audio per scene, and a tour).

    python3 tools/sonic_vfx/capture.py still <scene-id> [--class full] [--at 9.0] [--size 1920x1080]
    python3 tools/sonic_vfx/capture.py clip  <scene-id> [--class full] [--size 1920x1080]
    python3 tools/sonic_vfx/capture.py tour  [--seconds 10] [--size 1280x720]
    python3 tools/sonic_vfx/capture.py tourfull [--seconds 8] [--size 1280x720]   # one full mix through every scene

Files go to ~/Desktop/av-gen-review/25-sonic-vfx/, named by set-list position (`01-salt-flat-mirage.png`, ...). The
tour plays the set list in order, a few seconds of each scene with its test audio, cross-faded. Every render goes
through tools/gpu-lock.sh with the pinned engine (review.py's).
"""
import argparse
import importlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.sonic_vfx import review  # noqa: E402
from tools.sonic_vfx.scenes import SCENES  # noqa: E402

OUT = os.path.expanduser("~/Desktop/av-gen-review/25-sonic-vfx")
# The class each scene is shown with: the full mix (pads, bass, lead and drums) for every scene, so its clip has the
# whole instrument and music worth listening to, and the tour cut from the clips plays one piece of music through
# sixteen worlds (since ADR-1067/1068 the live detector hears the drums under the mix). The single-input classes are
# the test matrix's.
SHOWCASE = {}


def scene_ids():
    ids = []
    for name in SCENES:
        ids.append(importlib.import_module("tools.sonic_vfx.scenes." + name).ID)
    return ids


def numbered(sid):
    ids = scene_ids()
    n = ids.index(sid) + 1 if sid in ids else 99
    return "%02d-%s" % (n, sid)


def still(a):
    cls = a.cls or SHOWCASE.get(a.scene, "full")
    work = os.path.join(OUT, "work")
    os.makedirs(work, exist_ok=True)
    proj, dur = review.variant(a.scene, cls, work, a.projects or None)
    t = a.at if a.at is not None else min(dur * 0.6, dur - 0.5)
    # Played from the start, not seeked: a render that starts at t has none of the history the music left before it
    # (no lantern a note released, no silk a phrase laid), so the still is the last frame of a near-lossless clip.
    # The scene's capture clip already plays it from the start at this size: take the frame from it when it exists.
    dst = os.path.join(OUT, numbered(a.scene) + ".png")
    clip_path = os.path.join(OUT, numbered(a.scene) + ".mp4")
    if os.path.exists(clip_path) and not a.cls:
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-ss", "%.3f" % t, "-i", clip_path, "-frames:v", "1",
                        "-update", "1", dst], check=False)
        print(dst)
        return
    tmp = os.path.join(work, a.scene + "-still.mp4")
    review.run(["--headless", "--project", proj, "--render", tmp, "--range", "0:%.3f" % (t + 0.04), "--size", a.size,
                "--fps", "25", "--codec", "h264", "--quality", "97", "--particle-warmup", "120", "--tier", a.tier])
    if os.path.exists(tmp):
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-sseof", "-0.05", "-i", tmp, "-frames:v", "1", "-update", "1",
                        dst], check=False)
        print(dst)


def clip(a, seconds=None, size=None, dst=None, cls=None):
    cls = cls or a.cls or SHOWCASE.get(a.scene, "full")
    work = os.path.join(OUT, "work")
    os.makedirs(work, exist_ok=True)
    proj, dur = review.variant(a.scene, cls, work, a.projects or None)
    end = min(dur, seconds) if seconds else dur
    dst = dst or os.path.join(OUT, numbered(a.scene) + ".mp4")
    review.run(["--headless", "--project", proj, "--render", dst, "--range", "0:%.2f" % end, "--size",
                size or a.size, "--fps", "30", "--codec", "h264", "--quality", "92", "--particle-warmup", "120",
                "--tier", a.tier])
    print(dst)
    return dst


def tour(a, same_music=False):
    """The set list in order, `--seconds` of each scene's capture clip (from 3 s in, past the growing-in), cross-faded.
    Cut from the clips `capture.py clip` wrote (no re-render); a missing clip is rendered first.

    `same_music` (mode `tourfull`): every scene plays the same full mix (pads, bass, lead and drums), rendered afresh at
    the tour's size, so the cut compares sixteen worlds answering one piece of music."""
    parts = []
    work = os.path.join(OUT, "work", "tourfull" if same_music else "tour")
    os.makedirs(work, exist_ok=True)
    for sid in scene_ids():
        if same_music:
            src = os.path.join(work, sid + "--full.mp4")
            if not os.path.exists(src):
                a.scene = sid
                clip(a, seconds=3.0 + a.seconds + 0.5, size=a.size, dst=src, cls="full")
        else:
            src = os.path.join(OUT, numbered(sid) + ".mp4")
            if not os.path.exists(src):
                a.scene = sid
                clip(a, cls=SHOWCASE.get(sid, "full"))
        part = os.path.join(work, sid + ".mp4")
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-ss", "3.0", "-t", "%.2f" % a.seconds, "-i", src,
                        "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k",
                        part], check=True)
        parts.append(part)
    # cross-fade the parts (video xfade, audio acrossfade), 0.6 s each
    fade, n = 0.6, len(parts)
    inputs = sum((["-i", p] for p in parts), [])
    vf, af, off = [], [], 0.0
    prev_v, prev_a = "[0:v]", "[0:a]"
    for i in range(1, n):
        off += a.seconds - fade
        vf.append("%s[%d:v]xfade=transition=fade:duration=%g:offset=%g[v%d]" % (prev_v, i, fade, off, i))
        af.append("%s[%d:a]acrossfade=d=%g[a%d]" % (prev_a, i, fade, i))
        prev_v, prev_a = "[v%d]" % i, "[a%d]" % i
    dst = os.path.join(OUT, "00-tour-same-music.mp4" if same_music else "00-tour.mp4")
    cmd = ["ffmpeg", "-y", "-v", "error"] + inputs + ["-filter_complex", ";".join(vf + af), "-map", prev_v, "-map",
                                                       prev_a, "-c:v", "libx264", "-crf", "20", "-pix_fmt", "yuv420p",
                                                       "-c:a", "aac", "-b:a", "192k", dst]
    subprocess.run(cmd, check=True)
    print(dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["still", "clip", "tour", "tourfull"])
    ap.add_argument("scene", nargs="?")
    ap.add_argument("--class", dest="cls", default="")
    ap.add_argument("--at", type=float, default=None)
    ap.add_argument("--size", default="1920x1080")
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--projects", default="")
    ap.add_argument("--tier", default="realtime", help="the live tier, so a capture shows what the live demo shows")
    a = ap.parse_args()
    if a.mode == "tourfull":
        tour(a, same_music=True)
    else:
        {"still": still, "clip": clip, "tour": tour}[a.mode](a)


if __name__ == "__main__":
    main()

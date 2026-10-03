#!/usr/bin/env python3
"""Review renders of the Sonic VFX scenes through their test material (the file path computes the same character
and context as live, ADR-1025). Every render goes through tools/gpu-lock.sh with the art agent's pinned engine.

    python3 tools/sonic_vfx/review.py stills <scene-id> <class> [--at 4,8,12] [--size 960x540] [--out DIR]
    python3 tools/sonic_vfx/review.py clip   <scene-id> <class> [--range a:b] [--size 960x540] [--out FILE]
    python3 tools/sonic_vfx/review.py matrix <scene-id> [class ...] [--size 640x360] [--out DIR]
    python3 tools/sonic_vfx/review.py eval   <scene-id> <class> [--size 960x540] [--out DIR]
             a clip, the engine's signal trace and tools/sonic_vfx_critic.py's measure report (r.json, r.md)

`AVGEN` names the engine (default: the pinned wrapper in the session scratchpad, else build/release/src/avgen).
"""
import argparse
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))
from tools.sonic_vfx.variant import make_variant  # noqa: E402
from tools.sonic_vfx import kit  # noqa: E402

PIN = "/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad/vfx/avgen.sh"
# The pinned engine only: never the engineer's build/release (its binary and the shared source shaders move under a
# render). The scratchpad is wiped when a session restarts; a missing pin is an error, not a silent fallback (it fell
# back once, 2026-10-02 20:04). Rebuild it: docs/prototypes/sonic-garden/PROGRESS-abstract.md, "Resume here".
AVGEN = os.environ.get("AVGEN") or PIN
LOCK = os.path.join(REPO, "tools", "gpu-lock.sh")
CLASSES = ["pads", "chords", "bass", "lead", "arp", "edrums", "drumloop", "dense", "sparse", "velocity", "rapid",
           "sustained", "full"]
DRUM_ONLY = {"edrums", "drumloop"}
# Real music (the owner's own tracks, 30 s excerpts cut into the gitignored assets/audio by tools/sonic_vfx/abstract.py
# `music`): audio only, no MIDI.
REAL_MUSIC = {"allyougot": "sonic-abstract-allyougot.wav", "rebuild": "sonic-abstract-rebuild.wav"}


def material(cls):
    if cls in REAL_MUSIC:
        wav = os.path.join(REPO, "assets", "audio", REAL_MUSIC[cls])
        if not os.path.exists(wav):
            sys.exit("missing %s: run python3 tools/sonic_vfx/abstract.py music" % wav)
        return wav, None
    wav = os.path.join(REPO, "assets", "audio", "sonic-vfx-%s.wav" % cls)
    mid = None if cls in DRUM_ONLY else os.path.join(REPO, "examples", "sonic-vfx", "test", "%s.mid" % cls)
    if not os.path.exists(wav):
        sys.exit("missing %s: run python3 tools/sonic_vfx/make_test_material.py" % wav)
    return wav, mid


def duration(wav):
    import wave
    with wave.open(wav) as w:
        return w.getnframes() / float(w.getframerate())


def variant(scene, cls, workdir, project_dir=None):
    project = os.path.join(project_dir or kit.OUT_DIR, scene + ".json")  # kit.OUT_DIR: examples/sonic-vfx
    wav, mid = material(cls)
    out = os.path.join(workdir, "%s--%s.json" % (scene, cls))
    make_variant(project, wav, mid, out)
    import json
    with open(out) as f:
        doc = json.load(f)
    doc.setdefault("render", {})["muxAudio"] = True
    with open(out, "w") as f:
        json.dump(doc, f, indent=1)
    return out, duration(wav)


def run(args):
    if not os.path.exists(AVGEN):
        sys.exit("the pinned engine is missing (%s): rebuild it, see PROGRESS-abstract.md 'Resume here'" % AVGEN)
    cmd = [LOCK, AVGEN] + args
    r = subprocess.run(cmd, capture_output=True, text=True)
    tail = [l for l in (r.stdout + r.stderr).splitlines() if "render complete" in l or "error" in l.lower()]
    print("\n".join(tail[-3:]))
    return r.returncode


def sheet(paths, out, cols=2):
    from PIL import Image
    ims = [Image.open(p) for p in paths]
    if not ims:
        return
    w, h = ims[0].size
    rows = (len(ims) + cols - 1) // cols
    canvas = Image.new("RGB", (w * cols, h * rows))
    for i, im in enumerate(ims):
        canvas.paste(im, ((i % cols) * w, (i // cols) * h))
    canvas.save(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["stills", "clip", "matrix", "eval"])
    ap.add_argument("scene")
    ap.add_argument("classes", nargs="*")
    ap.add_argument("--at", default="")
    ap.add_argument("--range", default="")
    ap.add_argument("--size", default="")
    ap.add_argument("--out", default="")
    ap.add_argument("--fps", default="30")
    ap.add_argument("--projects", default="", help="directory holding the scene projects (default examples)")
    ap.add_argument("--tier", default="realtime", help="quality tier (the live look is realtime; offline for finals)")
    a = ap.parse_args()
    work = a.out or os.path.join("/tmp", "sonic-vfx-review")
    if a.mode == "stills":
        cls = a.classes[0] if a.classes else "full"
        os.makedirs(work, exist_ok=True)
        proj, dur = variant(a.scene, cls, work, a.projects or None)
        times = [float(x) for x in a.at.split(",")] if a.at else [dur * f for f in (0.2, 0.4, 0.6, 0.8)]
        frames = []
        for i, t in enumerate(times):
            d = os.path.join(work, "%s--%s--%02d" % (a.scene, cls, i))
            subprocess.run(["rm", "-rf", d])
            run(["--headless", "--project", proj, "--render", d, "--format", "png", "--range",
                 "%.3f:%.3f" % (t, t + 0.05), "--size", a.size or "960x540", "--fps", "20",
                 "--particle-warmup", "240", "--tier", a.tier])
            got = sorted(glob.glob(os.path.join(d, "*.png")))
            if got:
                frames.append(got[0])
        out = os.path.join(work, "%s--%s-sheet.png" % (a.scene, cls))
        sheet(frames, out)
        print(out)
    elif a.mode == "clip":
        cls = a.classes[0] if a.classes else "full"
        os.makedirs(os.path.dirname(os.path.abspath(a.out or work + "/x")), exist_ok=True)
        proj, dur = variant(a.scene, cls, os.path.dirname(os.path.abspath(a.out)) if a.out else work,
                            a.projects or None)
        out = a.out or os.path.join(work, "%s--%s.mp4" % (a.scene, cls))
        rng = a.range or "0:%.2f" % dur
        run(["--headless", "--project", proj, "--render", out, "--range", rng, "--size", a.size or "960x540",
             "--fps", a.fps, "--codec", "h264", "--quality", "80", "--particle-warmup", "120", "--tier", a.tier])
        print(out)
    elif a.mode == "eval":
        cls = a.classes[0] if a.classes else "full"
        os.makedirs(work, exist_ok=True)
        proj, dur = variant(a.scene, cls, work, a.projects or None)
        base = os.path.join(work, "%s--%s" % (a.scene, cls))
        run(["--headless", "--project", proj, "--render", base + ".mp4", "--range", "0:%.2f" % dur, "--size",
             a.size or "960x540", "--fps", a.fps, "--codec", "h264", "--quality", "80", "--particle-warmup", "120",
             "--tier", a.tier])
        subprocess.run([AVGEN, "--project", proj, "--sonic-trace", base + ".trace.csv"], capture_output=True)
        subprocess.run([sys.executable, os.path.join(REPO, "tools", "sonic_vfx_critic.py"), "measure", "--video",
                        base + ".mp4", "--trace", base + ".trace.csv", "--project", proj, "--out",
                        base + ".critic.json", "--md", base + ".critic.md"])
        print(base + ".critic.md")
    else:
        classes = a.classes or CLASSES
        os.makedirs(work, exist_ok=True)
        for cls in classes:
            proj, dur = variant(a.scene, cls, work, a.projects or None)
            out = os.path.join(work, "%s--%s.mp4" % (a.scene, cls))
            run(["--headless", "--project", proj, "--render", out, "--range", "0:%.2f" % dur, "--size",
                 a.size or "640x360", "--fps", a.fps, "--codec", "h264", "--quality", "70",
                 "--particle-warmup", "120", "--tier", a.tier])
            print(out)


if __name__ == "__main__":
    main()

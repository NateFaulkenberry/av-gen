#!/usr/bin/env python3
"""A contact sheet of the post instruments (ADR-1065/1066) on a real project: one still per effect, labelled.

    python3 tools/sonic_post_fx_sheet.py --project examples/sonic-garden/variants/perc.json --at 12 \
        --out ~/Desktop/av-gen-review/25-sonic-vfx/eng/post-fx-sheet.png [--size 640x360] [--only shock,sort]

Each effect is the project with a few parameters set (the `parameters` block). The variants are written beside the
project (so its relative paths resolve), rendered in one tools/gpu-lock.sh batch, and deleted. System python: PIL.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AVGEN = os.path.join(ROOT, "build", "release", "src", "avgen")

# name -> parameters; every value here is a suggestion for the art agent, not a look.
EFFECTS = [
    ("none", {}),
    ("shock ring", {"post/shock/amount": 70, "post/shock/radius": 0.45, "post/shock/width": 0.12,
                    "post/shock/chroma": 0.6}),
    ("block glitch", {"post/glitch/amount": 0.25, "post/glitch/block": 40, "post/glitch/drift": 60,
                      "post/glitch/swap": 0.4}),
    ("line tear", {"post/glitch/tear": 0.25, "post/glitch/tearShift": 90}),
    ("rgb split", {"post/split/amount": 14}),
    ("spectral split", {"post/split/amount": 18, "post/split/spectral": 1}),
    ("pixel sort", {"post/sort/amount": 1, "post/sort/threshold": 0.6, "post/sort/length": 220}),
    ("radial blur", {"post/radial/amount": 0.18}),
    ("scanlines", {"post/display/scanlines": 0.6, "post/display/lines": 240}),
    ("mosaic", {"post/display/pixelate": 14}),
    ("posterize + dither", {"post/display/posterize": 4, "post/display/dither": 0.7}),
    ("feedback", {"temporal/feedback/enabled": True, "temporal/feedback/amount": 0.7, "temporal/feedback/zoom": 1.04,
                  "temporal/feedback/rotate": 3, "temporal/feedback/hue": 0.04}),
    ("slit-scan", {"temporal/slit/enabled": True, "temporal/slit/amount": 1, "temporal/slit/frames": 16}),
]


def write_bench(args, runs):
    """The new passes' GPU medians and the frame's, per effect, against the effect-free run."""
    rows = []
    for name, out in runs:
        b = json.load(open(out + ".json"))["records"][0]
        passes = {p["label"]: p["medianMs"] for p in b.get("gpuPassMedianMs", [])}
        mine = {k: v for k, v in passes.items() if k.startswith(("post/glitch", "post/display", "temporal/"))}
        g = b.get("gpuMs")
        gpu = g.get("p50", g.get("median")) if isinstance(g, dict) else None
        rows.append((name, gpu, mine, b))
    base = rows[0][1]
    lines = ["| effect | its passes (GPU median ms) | frame GPU p50 ms | vs none |", "|---|---|---|---|"]
    for name, gpu, mine, _ in rows:
        p = ", ".join("%s %.3f" % (k, v) for k, v in sorted(mine.items())) or "-"
        delta = ("%+.2f" % (gpu - base)) if (gpu is not None and base is not None) else "?"
        lines.append("| %s | %s | %s | %s |" % (name, p, "%.2f" % gpu if gpu is not None else "?", delta))
    open(os.path.expanduser(args.bench), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--at", type=float, default=10.0)
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", default="640x360")
    ap.add_argument("--only", default="")
    ap.add_argument("--no-lock", action="store_true")
    ap.add_argument("--bench", default="", help="instead of a sheet: write a cost table (markdown) here")
    ap.add_argument("--frames", type=int, default=240)
    ap.add_argument("--tier", default="")
    args = ap.parse_args()
    project = os.path.abspath(args.project)
    doc = json.load(open(project))
    want = [e for e in EFFECTS if not args.only or e[0] == "none" or e[0].replace(" ", "") in
            [x.replace(" ", "") for x in args.only.split(",")]]
    tmp = tempfile.mkdtemp(prefix="fxsheet-")
    written, lines, stills = [], [], []
    try:
        for i, (name, params) in enumerate(want):
            v = json.loads(json.dumps(doc))
            v.setdefault("parameters", {}).update(params)
            path = os.path.join(os.path.dirname(project), ".fxsheet-%02d.json" % i)
            json.dump(v, open(path, "w"))
            written.append(path)
            out = os.path.join(tmp, "s%02d" % i)
            # A second of lead-in, so the temporal ring is warm (it refills in at most 16 frames).
            start = max(args.at - 1.0, 0.0)
            if args.bench:
                tier = (" --tier " + args.tier) if args.tier else ""
                lines.append("%s --headless --project %s --range %.3f: --frames %d --bench-json %s.json --size %s%s" % (
                    AVGEN, path, start, args.frames, out, args.size, tier))
            else:
                lines.append("%s --headless --project %s --render %s --format png --range %.3f:%.3f --size %s" % (
                    AVGEN, path, out, start, args.at, args.size))
            stills.append((name, out))
        script = os.path.join(tmp, "batch.sh")
        open(script, "w").write("set -e\n" + "\n".join(lines) + "\n")
        cmd = ["bash", script] if args.no_lock else [os.path.join(ROOT, "tools", "gpu-lock.sh"), "bash", script]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if args.bench:
            return write_bench(args, stills)
        w, h = (int(x) for x in args.size.split("x"))
        cols = 4
        rows = (len(stills) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * w, rows * (h + 22)), (12, 12, 14))
        draw = ImageDraw.Draw(sheet)
        for k, (name, out) in enumerate(stills):
            frames = sorted(f for f in os.listdir(out) if f.endswith(".png"))
            im = Image.open(os.path.join(out, frames[-1])).convert("RGB").resize((w, h))
            x, y = (k % cols) * w, (k // cols) * (h + 22)
            sheet.paste(im, (x, y + 22))
            draw.text((x + 6, y + 5), name, fill=(230, 230, 230))
        os.makedirs(os.path.dirname(os.path.abspath(os.path.expanduser(args.out))), exist_ok=True)
        sheet.save(os.path.expanduser(args.out))
        print("wrote", args.out)
    finally:
        for p in written:
            if os.path.exists(p):
                os.remove(p)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Frame cost of every Sonic VFX scene at the live size (02-brief-vfx-expansion.md §19, deliverable 19).

    python3 tools/sonic_vfx/perf.py [scene-id ...] [--size 1920x1080] [--tier realtime] [--frames 180]
                                    [--class full] [--md OUT.md]

Each scene runs headless through its test material (the file path, so the response and the effects fire as live)
for `--frames` frames from a second into the clip, with `--bench-json`, under tools/gpu-lock.sh with the pinned
engine. The table gives the frame's GPU p50 and p95 and the scene's five most expensive passes.
"""
import argparse
import importlib
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.sonic_vfx import review  # noqa: E402
from tools.sonic_vfx.scenes import SCENES  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenes", nargs="*")
    ap.add_argument("--size", default="1920x1080")
    ap.add_argument("--tier", default="")
    ap.add_argument("--frames", type=int, default=180)
    ap.add_argument("--class", dest="cls", default="full")
    ap.add_argument("--projects", default="")
    ap.add_argument("--work", default=os.path.expanduser("~/Desktop/av-gen-review/25-sonic-vfx/work/perf"))
    ap.add_argument("--md", default=os.path.expanduser("~/Desktop/av-gen-review/25-sonic-vfx/perf.md"))
    a = ap.parse_args()
    ids = a.scenes or [importlib.import_module("tools.sonic_vfx.scenes." + n).ID for n in SCENES]
    os.makedirs(a.work, exist_ok=True)
    rows = []
    for sid in ids:
        proj, dur = review.variant(sid, a.cls, a.work, a.projects or None)
        out = os.path.join(a.work, sid + ".bench.json")
        args = ["--headless", "--project", proj, "--range", "1.0:", "--frames", str(a.frames), "--bench-json", out,
                "--size", a.size]
        if a.tier:
            args += ["--tier", a.tier]
        review.run(args)
        try:
            rec = json.load(open(out))["records"][0]
        except Exception as e:  # noqa: BLE001
            rows.append((sid, None, None, "no bench (%s)" % e))
            continue
        g = rec.get("gpuMs") or {}
        p50 = g.get("p50", g.get("median")) if isinstance(g, dict) else None
        p95 = g.get("p95") if isinstance(g, dict) else None
        passes = sorted(((p["medianMs"], p["label"]) for p in rec.get("gpuPassMedianMs", [])), reverse=True)[:5]
        rows.append((sid, p50, p95, ", ".join("%s %.2f" % (lbl, ms) for ms, lbl in passes)))
        print(sid, p50, p95)
    lines = ["| scene | GPU p50 ms | GPU p95 ms | costliest passes (median ms) |", "|---|---|---|---|"]
    for sid, p50, p95, passes in rows:
        lines.append("| %s | %s | %s | %s |" % (sid, "%.1f" % p50 if p50 is not None else "?",
                                              "%.1f" % p95 if p95 is not None else "?", passes))
    open(a.md, "w").write("Size %s, tier %s, %d frames from 1 s, class %s.\n\n" % (
        a.size, a.tier or "(default)", a.frames, a.cls) + "\n".join(lines) + "\n")
    print(a.md)


if __name__ == "__main__":
    main()

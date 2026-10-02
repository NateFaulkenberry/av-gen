#!/usr/bin/env python3
"""Live runs of the Sonic VFX scenes (02-brief-vfx-expansion.md §20, deliverable 20): the scene opened in the live
editor, the probe (tools/sonic_live_probe.cpp: a CoreMIDI virtual source plus a synth played into BlackHole) playing a
scenario into it, every second frame captured, and the evaluator run on the capture against the live log.

    python3 tools/sonic_vfx/live.py <scene-id> [--scenario demo] [--out DIR]

It opens an editor window for the length of the scenario (about 30 s for `demo`). Under tools/gpu-lock.sh.
"""
import argparse
import csv
import glob
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.sonic_vfx import review  # noqa: E402

PIN_DIR = os.path.dirname(os.path.realpath(review.PIN))
PROBE_LEN = {"demo": 27.5, "patches": 40.0, "play": 82.0, "latency": 21.0, "chords": 20.0, "arp": 16.0,
             "low": 16.0, "high": 16.0, "distorted": 20.0}


def pin_bin():
    """The pinned engine's directory (the wrapper names it)."""
    for line in open(review.PIN):
        if line.startswith("B=${AVGEN_PIN:-"):
            return line.split(":-", 1)[1].rstrip("}\n")
    return PIN_DIR


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scene")
    ap.add_argument("--scenario", default="demo")
    ap.add_argument("--out", default=os.path.expanduser("~/Desktop/av-gen-review/25-sonic-vfx/live"))
    ap.add_argument("--size", default="960x540")
    a = ap.parse_args()
    out = os.path.join(a.out, "%s--%s" % (a.scene, a.scenario))
    subprocess.run(["rm", "-rf", out])
    os.makedirs(os.path.join(out, "frames"), exist_ok=True)
    project = os.path.join(REPO, "examples", "sonic-vfx", a.scene + ".json")
    length = PROBE_LEN.get(a.scenario, 30.0) + 4.0
    frames = int(length * 60)
    b = pin_bin()
    probe = os.path.join(b, "avgen_sonic_probe")
    script = os.path.join(out, "run.sh")
    with open(script, "w") as f:
        f.write("set -e\n")
        f.write("export AVGEN_SHADER_DIR=%s/shaders\n" % b)
        # the editor first (it must be listening when the probe's virtual source appears), then the probe
        f.write("%s/avgen --project %s --live --input BlackHole --midi 'AV Gen Probe' --live-capture %s "
                "--live-capture-every 2 --live-capture-size %s --sonic-live-log %s --frames %d &\n"
                % (b, project, os.path.join(out, "frames"), a.size, os.path.join(out, "live.csv"), frames))
        f.write("APP=$!\nsleep 3\n")
        f.write("%s %s --out %s --device BlackHole --lead-in 1\n" % (probe, a.scenario,
                                                                     os.path.join(out, "probe.csv")))
        f.write("wait $APP\n")
    subprocess.run([review.LOCK, "bash", script])
    # the capture as a clip, and the live log as a trace (time = seconds since the first captured frame)
    idx = list(csv.DictReader(open(os.path.join(out, "frames", "frames.csv"))))
    if not idx:
        sys.exit("no frames captured")
    t0 = float(idx[0]["liveSeconds"])
    dur = float(idx[-1]["liveSeconds"]) - t0
    fps = max(1.0, (len(idx) - 1) / max(dur, 1e-3))
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-framerate", "%.3f" % fps, "-pattern_type", "glob", "-i",
                    os.path.join(out, "frames", "*.ppm"), "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20",
                    os.path.join(out, "live.mp4")], check=True)
    rows = list(csv.DictReader(open(os.path.join(out, "live.csv"))))
    keys = [k for k in rows[0].keys() if k.startswith(("response.", "notes.", "visual.", "sonic."))]
    with open(os.path.join(out, "trace.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["time"] + keys)
        for r in rows:
            try:
                w.writerow([float(r["liveSeconds"]) - t0] + [float(r[k] or 0.0) for k in keys])
            except ValueError:
                continue
    subprocess.run([sys.executable, os.path.join(REPO, "tools", "sonic_vfx_critic.py"), "measure", "--video",
                    os.path.join(out, "live.mp4"), "--trace", os.path.join(out, "trace.csv"), "--project", project,
                    "--out", os.path.join(out, "critic.json"), "--md", os.path.join(out, "critic.md")])
    print(out)


if __name__ == "__main__":
    main()

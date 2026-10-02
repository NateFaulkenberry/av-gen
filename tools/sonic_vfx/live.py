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
             "low": 16.0, "high": 16.0, "distorted": 20.0,
             "tour": 1.0 + 20.0 * 17}     # program 0 (Sonic Live), then the 16 scenes, 20 s each (ADR-1063)


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(len(xs) * q))] if xs else float("nan")


def tour_table(out, rows):
    """Split a tour's live log at the probe's program changes (both stamp the host clock in ns): per scene, the
    frame interval and GPU time, the notes that arrived, and how high the response's envelopes went."""
    import importlib
    from tools.sonic_vfx.scenes import SCENES
    names = ["Sonic Live"] + [importlib.import_module("tools.sonic_vfx.scenes." + n).TITLE for n in SCENES]
    events = list(csv.DictReader(open(os.path.join(out, "probe.csv"))))
    cuts = [(int(e["hostNs"]), int(e["key"])) for e in events if e["kind"] == "program"]
    if not cuts:
        return None
    end_ns = int(events[-1]["hostNs"])
    lines = ["| # | scene | frames | interval p50 / p99 ms | GPU p50 / p95 ms | notes | noteEnv max | kickEnv max |"
             " sustain max |", "|---|---|---|---|---|---|---|---|---|"]
    for i, (t0, k) in enumerate(cuts):
        t1 = cuts[i + 1][0] if i + 1 < len(cuts) else end_ns
        # skip the first 3 s: the switch loads the scene (that hitch is the switch's, not the scene's)
        seg = [r for r in rows if t0 + 3_000_000_000 <= int(r["frameNs"]) < t1]
        if len(seg) < 3:
            continue
        ns = [int(r["frameNs"]) for r in seg]
        dt = [(ns[j] - ns[j - 1]) / 1e6 for j in range(1, len(ns))]
        gpu = [g for g in (float(r.get("gpuMs") or -1.0) for r in seg) if g >= 0.0]

        def mx(col):
            vals = [float(r[col]) for r in seg if r.get(col) not in (None, "")]
            return max(vals) if vals else float("nan")
        notes = int(seg[-1].get("notesReceived") or 0) - int(seg[0].get("notesReceived") or 0)
        lines.append("| %d | %s | %d | %.1f / %.1f | %.1f / %.1f | %d | %.2f | %.2f | %.2f |" % (
            k, names[k % len(names)], len(seg), pct(dt, 0.5), pct(dt, 0.99), pct(gpu, 0.5), pct(gpu, 0.95), notes,
            mx("response.noteEnv"), mx("response.kickEnv"), mx("response.sustain")))
    md = os.path.join(out, "tour.md")
    open(md, "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return md


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
    ap.add_argument("--no-capture", action="store_true",
                    help="measure the live frame rate only (the capture re-renders every 2nd frame and costs time)")
    a = ap.parse_args()
    out = os.path.join(a.out, "%s--%s%s" % (a.scene, a.scenario, "--nocap" if a.no_capture else ""))
    subprocess.run(["rm", "-rf", out])
    os.makedirs(os.path.join(out, "frames"), exist_ok=True)
    project = os.path.join(REPO, "examples", "sonic-vfx", a.scene + ".json")
    length = PROBE_LEN.get(a.scenario, 30.0) + 4.0
    frames = int(length * 120)
    b = pin_bin()
    probe = os.path.join(b, "avgen_sonic_probe")
    script = os.path.join(out, "run.sh")
    with open(script, "w") as f:
        f.write("set -e\n")
        f.write("export AVGEN_SHADER_DIR=%s/shaders\n" % b)
        # The editor first, listening to every MIDI source: a source that appears later connects on arrival only
        # without a name filter (with one, a source missing at start-up is never retried). Then the probe, whose
        # virtual source appears and connects, with a lead-in while the editor finishes starting.
        capture = "" if a.no_capture else ("--live-capture %s --live-capture-every 2 --live-capture-size %s "
                                           % (os.path.join(out, "frames"), a.size))
        f.write("%s/avgen --project %s --live --input BlackHole %s--sonic-live-log %s --frames %d > %s 2>&1 &\n"
                % (b, project, capture, os.path.join(out, "live.csv"), frames, os.path.join(out, "editor.log")))
        f.write("APP=$!\nsleep 3\n")
        f.write("%s %s --out %s --device BlackHole --lead-in 2\n" % (probe, a.scenario,
                                                                     os.path.join(out, "probe.csv")))
        # the scenario is over: let the last notes ring out, then close the editor (--frames is only a bound)
        f.write("sleep 2\nkill -TERM $APP 2>/dev/null || true\nwait $APP 2>/dev/null || true\n")
    subprocess.run([review.LOCK, "bash", script])
    rows = list(csv.DictReader(open(os.path.join(out, "live.csv"))))
    ns = [int(r["frameNs"]) for r in rows]
    dt = sorted((ns[i] - ns[i - 1]) / 1e6 for i in range(1, len(ns)))
    if dt:
        print("live frame interval p50 %.1f ms, p95 %.1f ms, p99 %.1f ms; notes received %s"
              % (dt[len(dt) // 2], dt[int(len(dt) * 0.95)], dt[int(len(dt) * 0.99)], rows[-1].get("notesReceived")))
    if a.scenario == "tour":
        tour_table(out, rows)
    if a.no_capture:
        return
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

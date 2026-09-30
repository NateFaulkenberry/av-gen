#!/usr/bin/env python3
"""Replays a live probe run through the file path, for tuning the live art without a GPU or a live session
(01-brief-live.md PARTS 15-16).

    python3 tools/sonic_live_replay.py <probe-events.csv> <probe.wav> <out-dir> [--project p.json] [--trace]
        [--fps 60]

`avgen_sonic_probe <scenario> --out e.csv --wav a.wav` plays a scenario into BlackHole and records what it played;
this turns that recording into a file project:

  - <out-dir>/<name>.mid: the probe's notes, timed from the recording's first sample (the probe logs both on the
    host clock);
  - <out-dir>/<name>.json: the live project (default examples/sonic-garden/sonic-live.json) with the probe's audio
    and notes as its files and `sonic.live` off, the scene path made absolute;
  - with --trace, <out-dir>/<name>.trace.csv from `avgen --sonic-trace` (no GPU), every sonic, notes, timbre and
    visual signal per frame.

The file path and the live path compute the same character (ADR-1025: the threaded live timbre path reaches the
file path's to 1e-6), so a trace of the replay is what the live world reads, less the live path's latency (about
13 ms for MIDI and 24 ms for audio at the median). The replay project also renders headless (`--render`) under
tools/gpu-lock.sh, for a full-quality picture of the same performance.
"""
import argparse
import copy
import csv
import json
import os
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIVE = os.path.join(REPO, "examples", "sonic-garden", "sonic-live.json")
AVGEN = os.path.join(REPO, "build", "release", "src", "avgen")
PPQ = 960
TEMPO_US = 500000  # 120 BPM: one tick is 0.52 ms


def vlq(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def write_midi(path, events):
    """events: (seconds, is_on, key, velocity 0..127), type 0 at 120 BPM."""
    ticks_per_second = PPQ * 1e6 / TEMPO_US
    rows = sorted(((max(0, round(t * ticks_per_second)), on, k, v) for t, on, k, v in events),
                  key=lambda e: (e[0], e[1], e[2]))  # offs before ons at the same tick
    track = bytearray(vlq(0) + b"\xff\x51\x03" + TEMPO_US.to_bytes(3, "big"))
    last = 0
    for tick, on, key, vel in rows:
        track += vlq(tick - last) + bytes([0x90 if on else 0x80, key, vel if on else 64])
        last = tick
    track += vlq(0) + b"\xff\x2f\x00"
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ))
        f.write(b"MTrk" + struct.pack(">I", len(track)) + bytes(track))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("events")
    ap.add_argument("wav")
    ap.add_argument("out")
    ap.add_argument("--project", default=LIVE)
    ap.add_argument("--trace", action="store_true")
    ap.add_argument("--fps", type=float, default=60.0)
    a = ap.parse_args()

    with open(a.events) as f:
        rows = list(csv.DictReader(f))
    rec0 = int(next(r for r in rows if r["kind"] == "recording")["hostNs"])
    events = []
    for r in rows:
        if r["kind"] in ("on", "off"):
            t = (int(r["hostNs"]) - rec0) / 1e9
            vel = max(1, min(127, round(float(r["velocity"]) * 127))) if r["kind"] == "on" else 0
            events.append((t, r["kind"] == "on", int(r["key"]), vel))
    os.makedirs(a.out, exist_ok=True)
    name = os.path.splitext(os.path.basename(a.wav))[0]
    mid = os.path.abspath(os.path.join(a.out, name + ".mid"))
    write_midi(mid, events)

    with open(a.project) as f:
        doc = json.load(f)
    proj = copy.deepcopy(doc)
    base = os.path.dirname(os.path.abspath(a.project))
    assets = proj.setdefault("assets", {})
    assets["audio"] = {"path": os.path.abspath(a.wav)}
    scene = assets.get("scene", {})
    if scene.get("path") and not os.path.isabs(scene["path"]):
        scene["path"] = os.path.normpath(os.path.join(base, scene["path"]))
    sonic = proj.setdefault("sonic", {})
    sonic["notes"] = mid
    sonic["live"] = False
    proj.setdefault("app", {})["name"] = "Sonic Live replay: " + name
    out_project = os.path.abspath(os.path.join(a.out, name + ".json"))
    with open(out_project, "w") as f:
        json.dump(proj, f, indent=1)
        f.write("\n")
    print(f"wrote {out_project} ({len(events)} note events)")
    if a.trace:
        trace = os.path.join(a.out, name + ".trace.csv")
        r = subprocess.run([AVGEN, "--project", out_project, "--sonic-trace", trace, "--fps", str(a.fps)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stdout + r.stderr)
            sys.exit(r.returncode)
        print(f"wrote {trace}")


if __name__ == "__main__":
    main()

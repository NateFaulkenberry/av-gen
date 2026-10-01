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
13 ms for MIDI and 24 ms for audio at the median).

    python3 tools/sonic_live_replay.py <probe-events.csv> <probe.wav> <out-dir> --render <clip.mp4> [--size 1280x720]
        [--title T]

renders the replay project headless at 30 fps (under tools/gpu-lock.sh: it takes the GPU lock itself, so do not
call it from inside the lock), then writes a review clip: the render with a readout strip under it (the synth patch and knobs, MIDI, the sound, the fast
channels and the world's families, from the trace) and the probe's own recording as its soundtrack. The live
editor's --live-capture shows the same performance through the live path at about 15 fps; this is the full-quality
picture of it.
"""
import argparse
import copy
import csv
import json
import os
import struct
import shutil
import subprocess
import sys
import tempfile

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
    ap.add_argument("--render", help="also render a review clip (mp4) of the replay")
    ap.add_argument("--size", default="1280x720")
    ap.add_argument("--title", default="")
    ap.add_argument("--no-lock", action="store_true",
                    help="render without taking tools/gpu-lock.sh (the caller already holds it)")
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
    if a.trace or a.render:
        trace = os.path.join(a.out, name + ".trace.csv")
        r = subprocess.run([AVGEN, "--project", out_project, "--sonic-trace", trace, "--fps", str(a.fps)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stdout + r.stderr)
            sys.exit(r.returncode)
        print(f"wrote {trace}")
    if a.render:
        render_clip(a, out_project, trace, rows, rec0)


def render_clip(a, project, trace, events, rec0):
    """Renders the replay (30 fps video), draws a readout strip at 10 fps, stacks the two and muxes the probe's
    recording under them."""
    from PIL import Image, ImageDraw
    import bisect
    start = (int(next(r for r in events if r["kind"] == "start")["hostNs"]) - rec0) / 1e9
    end = (int(next(r for r in events if r["kind"] == "end")["hostNs"]) - rec0) / 1e9
    t0, t1 = max(0.0, start), end + 0.3
    w, h = (int(x) for x in a.size.split("x"))
    tmp = tempfile.mkdtemp(prefix="sonic-replay-")
    video = os.path.join(tmp, "render.mp4")
    cmd = [AVGEN, "--headless", "--project", project, "--render", video,
           "--range", f"{t0:.3f}:{t1:.3f}", "--size", a.size, "--quality", "92", "--particle-warmup", "120"]
    if not a.no_lock:
        cmd.insert(0, os.path.join(REPO, "tools", "gpu-lock.sh"))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(video):
        sys.stderr.write(r.stdout[-4000:] + r.stderr[-4000:])
        sys.exit(r.returncode or 1)
    with open(trace) as f:
        tr = list(csv.DictReader(f))
    times = [float(x["time"]) for x in tr]
    synth = sorted(((int(e["hostNs"]) - rec0) / 1e9, e) for e in events if e["kind"] not in ("recording",))
    strips = os.path.join(tmp, "strip")
    os.makedirs(strips)
    n = int((t1 - t0) * 10) + 1
    for i in range(n):
        t = t0 + i / 10.0
        k = min(bisect.bisect_left(times, t), len(tr) - 1)
        row = tr[k]
        g = lambda c: float(row.get(c, 0.0) or 0.0)
        ev = [e for tt, e in synth if tt <= t]
        last = ev[-1] if ev else synth[0][1]
        patch = next((e["kind"][6:] for e in reversed(ev) if e["kind"].startswith("patch:")), "-")
        img = Image.new("RGB", (w, 66), (12, 12, 16))
        d = ImageDraw.Draw(img)
        c1, c2 = 10, w // 2 - 20
        d.text((c1, 6), f"{a.title}   t {t - start:5.1f} s", fill=(230, 230, 230))
        d.text((c1, 24), f"synth {patch}: cutoff {float(last['cutoff']):5.0f} Hz  drive {float(last['drive']):4.1f}",
               fill=(200, 200, 140))
        d.text((c1, 42), f"MIDI held {g('notes.active'):.0f}  pitch {g('notes.pitch'):.2f}  "
                         f"vel {g('notes.velocity'):.2f}", fill=(140, 200, 240))
        d.text((c2, 6), f"sound: bright {g('sonic.brightness'):.2f}  rough {g('sonic.roughness'):.2f}  "
                        f"warm {g('sonic.warmth'):.2f}  energy {g('sonic.energy'):.2f}", fill=(240, 170, 140))
        d.text((c2, 24), f"fast: glow {g('visual.glow'):.2f}  grit {g('visual.grit'):.2f}  "
                         f"figure {g('visual.figure'):.2f}  stack {g('visual.stack'):.2f}", fill=(240, 220, 150))
        d.text((c2, 42), f"world: organic {g('visual.organic'):.2f}  glass {g('visual.crystalline'):.2f}  "
                         f"heavy {g('visual.tectonic'):.2f}  strike {g('visual.impact'):.2f}", fill=(170, 240, 170))
        img.save(os.path.join(strips, f"{i:06d}.png"))
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", video, "-framerate", "10", "-i",
                    os.path.join(strips, "%06d.png"), "-ss", f"{t0:.4f}", "-i", a.wav,
                    "-filter_complex", "[1:v]fps=30[s];[0:v][s]vstack=inputs=2,format=yuv420p[v]",
                    "-map", "[v]", "-map", "2:a", "-shortest", "-c:v", "libx264", "-crf", "19", "-c:a", "aac",
                    "-b:a", "160k", a.render], check=True)
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"wrote {a.render} ({t1 - t0:.1f} s)")


if __name__ == "__main__":
    main()

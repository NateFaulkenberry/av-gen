#!/usr/bin/env python3
"""A review clip of a live Sonic session (ADR-1025): the frames --live-capture wrote, timed by their host clock,
with the probe's recorded audio under them and a readout strip of what MIDI and the sound were doing.

    python3 tools/sonic_live_clip.py <capture-dir> <app-log.csv> <probe-events.csv> <probe.wav> <out.mp4> [--title T]

Only the span the probe played is kept. Every frame is shown for as long as it was on screen in the session (the
capture costs frame time, so the clip's frame rate is what the session ran at, about 20 fps).
"""
import csv
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw


def load(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def main():
    cap, app_log, probe_log, wav, out = sys.argv[1:6]
    title = sys.argv[7] if len(sys.argv) > 7 and sys.argv[6] == "--title" else "live Sonic input"
    frames = load(os.path.join(cap, "frames.csv"))
    app = {int(r["frameNs"]): r for r in load(app_log)}
    probe = load(probe_log)
    start = int(next(r for r in probe if r["kind"] == "start")["hostNs"])
    end = int(next(r for r in probe if r["kind"] == "end")["hostNs"])
    rec0 = int(next(r for r in probe if r["kind"] == "recording")["hostNs"])
    events = sorted((int(r["hostNs"]), r) for r in probe if r["kind"] not in ("recording",))
    frames = [f for f in frames if start - 0.2e9 <= int(f["frameNs"]) <= end + 0.5e9]
    tmp = tempfile.mkdtemp(prefix="sonic-clip-")
    lines = []
    for i, f in enumerate(frames):
        ns = int(f["frameNs"])
        img = Image.open(os.path.join(cap, f["file"])).convert("RGB")
        w, h = img.size
        canvas = Image.new("RGB", (w, h + 64), (12, 12, 16))
        canvas.paste(img, (0, 0))
        d = ImageDraw.Draw(canvas)
        r = app.get(ns, {})
        synth = next((e for t, e in reversed(events) if t <= ns), None)
        patch = next((e["kind"][6:] for t, e in reversed(events) if t <= ns and e["kind"].startswith("patch:")), "")
        g = lambda k: float(r.get(k, 0.0) or 0.0)
        d.text((10, h + 6), f"{title}   t {(ns - start) / 1e9:5.2f} s", fill=(230, 230, 230))
        if synth:
            d.text((10, h + 24), f"synth {patch or '-'}: cutoff {float(synth['cutoff']):5.0f} Hz  drive "
                                 f"{float(synth['drive']):4.1f}", fill=(200, 200, 140))
        d.text((10, h + 42), f"MIDI held {g('notes.active'):.0f}  pitch {g('notes.pitch'):.2f}  "
                             f"vel {g('notes.velocity'):.2f}", fill=(140, 200, 240))
        d.text((w // 2 - 30, h + 6), f"sound: bright {g('sonic.brightness'):.2f}  rough {g('sonic.roughness'):.2f}"
                                     f"  warm {g('sonic.warmth'):.2f}  energy {g('sonic.energy'):.2f}",
               fill=(240, 170, 140))
        d.text((w // 2 - 30, h + 24), f"fast: glow {g('visual.glow'):.2f}  grit {g('visual.grit'):.2f}  "
                                      f"figure {g('visual.figure'):.2f}  stack {g('visual.stack'):.2f}",
               fill=(240, 220, 150))
        d.text((w // 2 - 30, h + 42), f"world: organic {g('visual.organic'):.2f}  glass {g('visual.crystalline'):.2f}"
                                      f"  heavy {g('visual.tectonic'):.2f}  strike {g('visual.impact'):.2f}",
               fill=(170, 240, 170))
        name = os.path.join(tmp, f"{i:06d}.png")
        canvas.save(name)
        nxt = int(frames[i + 1]["frameNs"]) if i + 1 < len(frames) else ns + 50_000_000
        lines.append(f"file '{name}'\nduration {(nxt - ns) / 1e9:.6f}\n")
    lines.append(f"file '{os.path.join(tmp, f'{len(frames) - 1:06d}.png')}'\n")
    listing = os.path.join(tmp, "list.txt")
    with open(listing, "w") as f:
        f.writelines(lines)
    # The audio: the probe's recording, offset so its sample at the first frame's host time lines up.
    offset = (int(frames[0]["frameNs"]) - rec0) / 1e9
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "concat", "-safe", "0", "-i", listing,
                    "-ss", f"{max(offset, 0):.4f}", "-i", wav, "-map", "0:v", "-map", "1:a", "-shortest",
                    "-vf", "fps=30,format=yuv420p", "-c:v", "libx264", "-crf", "20", "-c:a", "aac", "-b:a", "160k",
                    out], check=True)
    print(f"wrote {out}: {len(frames)} frames over {(int(frames[-1]['frameNs']) - int(frames[0]['frameNs'])) / 1e9:.1f} s")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""THE RIFT: Creative Critic inputs for a render of rift.json.

    python3 examples/bioluminescent/critic_inputs.py <video.mp4> <out_dir> [--start S]

Traces the arc through the engine (the probe copy build.py writes, `--sonic-trace`, no GPU) and writes `intent.json`
(the brief's mood, visual language and avoid-list, one segment per stage with its intent and energy) and
`shots.json` (one shot per committed state), then prints the `critic submit` command.
"""
from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
STAGE_INTENT = {
    "Dark": ("an alien bioluminescent canyon at night, mostly dark: organisms glow faintly and breathe, plankton "
             "in the river, a slow drift low over the water", 0.1),
    "Stirring": ("the music begins to disturb the ecosystem: kicks launch small fronts of light through the "
                 "meadows; the camera flies low along the river", 0.3),
    "Breath": ("a held breath: the bass stops, the world waits, the camera hangs", 0.2),
    "Awake": ("a reach of the canyon awakens: waves of light travel through meadows and walls, the crinoid canopy "
              "answers late, the camera passes through the crowns and along the walls", 0.5),
    "Build": ("the build: the world dims and holds its breath, spores rise with the highs, the camera climbs into "
              "the canopy", 0.6),
    "Drop": ("THE DROP: thousands of organisms ignite at once, a wave of light runs down the canyon, the walls "
             "fluoresce magenta, the canopy flares, swarms stream past, the camera surges through the crowns", 1.0),
    "Body": ("the awakened ecosystem: rhythmic waves on every kick, fast flights through dense glowing vegetation, "
             "river, canopy, wall and reveal shots", 0.8),
    "Aftermath": ("the aftermath: the light drains slowly, the canopy's chains and a few lanterns remain", 0.15),
}


def trace() -> list[dict]:
    probe = REPO / "build" / "biolum" / "rift-trace.json"
    out = REPO / "build" / "biolum" / "trace-critic.csv"
    subprocess.run([str(REPO / "build/release/src/avgen"), "--headless", "--project", str(probe), "--sonic-trace",
                    str(out)], check=True, capture_output=True)
    return list(csv.DictReader(out.open()))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("out")
    ap.add_argument("--start", type=float, default=0.0)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    project = json.loads((HERE / "rift.json").read_text())
    names = [st["name"] for st in project["states"]["states"]]
    spans = []
    for row in trace():
        t = float(row["time"])
        k = int(round(float(row["visual.pState"]) * 20))
        name = names[k] if 0 <= k < len(names) else str(k)
        if not spans or spans[-1][0] != name:
            spans.append([name, t, t])
        spans[-1][2] = t
    dur = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0",
                                a.video], check=True, capture_output=True, text=True).stdout.strip())
    lo, hi = a.start, a.start + dur
    shots, segments = [], []
    for i, (name, t0, t1) in enumerate(spans):
        t1 = spans[i + 1][1] if i + 1 < len(spans) else t1
        if t1 <= lo or t0 >= hi or t1 - t0 < 0.5:
            continue
        s0, s1 = max(t0, lo), min(t1, hi)
        stage = name.split(" ")[0]
        shots.append({"id": f"s{len(shots) + 1:02d}", "start": round(s0, 3), "end": round(s1, 3), "segment": stage,
                      "label": name})
        if segments and segments[-1]["id"] == stage:
            segments[-1]["end"] = round(s1, 3)
        else:
            text, energy = STAGE_INTENT.get(stage, ("", 0.5))
            seg = {"id": stage, "name": stage, "start": round(s0, 3), "end": round(s1, 3), "intent": text,
                   "energy": energy}
            if stage == "Drop":
                seg["role"] = "climax"
            segments.append(seg)
    intent = {
        "project": "THE RIFT: a bioluminescent audiovisual ecosystem (a drained abyssal trench) on Trench",
        "mood": "a vast, mysterious, living bioluminescent ecosystem that happens to respond to music; dark and alive "
                "at rest, awakening, then a spectacular drop",
        "visual_language": "deep-sea fauna as a land ecosystem in a canyon at night: giant crinoids with hanging "
                           "siphonophore chains, sea-pen meadows, sea fans, living wall crust, plankton in a dark "
                           "river; light travels through the world as waves; a soaring flight through it",
        "avoid": ["generic green neon forest", "random particles over a landscape", "a handful of glowing mushrooms",
                  "empty cinematic void", "visible edge of the world", "colour washing out to grey",
                  "static camera", "primitive geometry", "beat-synced cheese"],
        "segments": segments,
    }
    (out / "intent.json").write_text(json.dumps(intent, indent=1))
    (out / "shots.json").write_text(json.dumps(shots, indent=1))
    audio = (HERE / project["assets"]["audio"]["path"]).resolve()
    cmd = ["critic", "submit", "--mode", "preview", "--video", str(Path(a.video).resolve()), "--intent",
           str((out / "intent.json").resolve()), "--shots", str((out / "shots.json").resolve()), "--audio",
           str(audio)]
    if a.start:
        cmd += ["--video-start", str(a.start)]
    print(" ".join(cmd))
    return 0


if __name__ == "__main__":
    sys.exit(main())

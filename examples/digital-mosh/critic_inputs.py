#!/usr/bin/env python3
"""DIGITAL MOSH: Creative Critic inputs for a render of a project.

    python3 examples/digital-mosh/critic_inputs.py <project.json> <video.mp4> <out_dir> [--start S]

Traces the project's arc through the engine (as arc.py does, no GPU) and writes `intent.json` (the brief's mood,
visual language and avoid-list, plus one segment per stage with that stage's intent) and `shots.json` (one shot per
committed state: every vantage the camera travelled to), then prints the `critic submit` command.
"""
from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
STAGE_INTENT = {
    "Dream": ("beautiful, quiet, hypnotic: a Dali/Tanguy desert, an olive tree, a Tanguy object, long shadows; "
              "restrained movement, the camera floating", 0.15),
    "Uncanny": ("subtle relation errors: the shadows turn against the sun, the object appears twice, de Chirico's "
                "light; nothing broken, everything slightly wrong", 0.3),
    "Infection": ("the first block goes bad: an ink stain with a glowing front spreads from the Tanguy object, "
                  "the land drains to Tanguy's grey-blue", 0.5),
    "Corruption": ("Ernst's rot: the tree is eaten from the root up into its own blocks, its limbs melt like wax; "
                   "image corruption only on transients", 0.7),
    "Nightmare": ("systemic: a blood-red sky, limbs floating free, the horizon tilting, abrupt camera jumps", 0.9),
    "Collapse": ("the world decomposes into the renderer's units: geometry, fragments, particles, temporal "
                 "fragments, pixels, colour, light", 1.0),
    "Decay": ("collapse: fragments become particles and temporal fragments", 1.0),
    "Pixels": ("collapse: pixels, then colour", 1.0),
    "Light": ("collapse: everything burns out to white", 1.0),
    "Respite": ("a breakdown's temporary recovery: Magritte's Empire of Light, a day sky over a land in night", 0.2),
    "Recovery": ("the keyframe: suddenly calm, the dream exactly as it was, except one macroblock that did not "
                 "refresh", 0.1),
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("project")
    ap.add_argument("video")
    ap.add_argument("out")
    ap.add_argument("--start", type=float, default=0.0)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    csv_path = out / "arc.csv"
    subprocess.run([sys.executable, str(HERE / "arc.py"), a.project, "--out", str(csv_path)], check=True,
                   capture_output=True)
    p = json.loads(Path(a.project).read_text())
    names = [st["name"] for st in p["states"]["states"]]
    scale = 0.02 if len(names) > 10 else 0.1
    rows = list(csv.DictReader(csv_path.open()))
    spans = []
    for row in rows:
        t = float(row["time"])
        k = int(round(float(row["visual.state_index"]) / scale))
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
        shots.append({"id": f"s{len(shots) + 1:02d}", "start": round(s0, 3), "end": round(s1, 3),
                      "segment": stage, "label": f"{name}"})
        if segments and segments[-1]["id"] == stage:
            segments[-1]["end"] = round(s1, 3)
        else:
            text, energy = STAGE_INTENT.get(stage, ("", 0.5))
            seg = {"id": stage, "name": stage, "start": round(s0, 3), "end": round(s1, 3), "intent": text,
                   "energy": energy}
            if stage in ("Collapse", "Decay", "Pixels", "Light"):
                seg["role"] = "climax"
            segments.append(seg)
    intent = {
        "project": "DIGITAL MOSH: a surrealist dreamscape that becomes corrupted data",
        "mood": "a beautiful dream slowly discovered to be made of corrupted information: beautiful, strange, uncanny, "
                "fascinating, unstable, overwhelming, nightmarish, empty -- and, when it returns to beauty, untrusted",
        "visual_language": "a sparse Dali/Tanguy desert painted with palettes sampled from the paintings; the "
                           "corruption is a substance in the world (an ink stain on a macroblock grid, objects rotting "
                           "into their own blocks), not a filter; a travelling camera",
        "avoid": ["generic glitch shaders", "constant RGB splitting", "endless particle explosions",
                  "random noise everywhere", "constant camera shake", "generic psychedelic visuals", "everything neon",
                  "chaos from frame one", "visible edge of the world or unrendered black", "vortex centrepiece"],
        "segments": segments,
    }
    (out / "intent.json").write_text(json.dumps(intent, indent=1))
    (out / "shots.json").write_text(json.dumps(shots, indent=1))
    audio = p.get("assets", {}).get("audio", {}).get("path", "")
    audio = str((Path(a.project).parent / audio).resolve()) if audio else ""
    cmd = ["critic", "submit", "--mode", "preview", "--video", str(Path(a.video).resolve()),
           "--intent", str((out / "intent.json").resolve()), "--shots", str((out / "shots.json").resolve())]
    if a.start:
        cmd += ["--video-start", str(a.start)]
    if audio:
        cmd += ["--audio", audio]
    print(" ".join(cmd))
    return 0


if __name__ == "__main__":
    sys.exit(main())

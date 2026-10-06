#!/usr/bin/env python3
"""DIGITAL MOSH: trace the arc of a track through the real engine, without a GPU.

    python3 examples/digital-mosh/arc.py <project.json> [--out arc.csv] [--plot arc.png]

Writes a copy of the project with a probe source and a `sonic` block, then runs
`avgen --headless --project <copy> --sonic-trace` (no GPU, about two seconds per song). It prints the stage
timeline the state machine produced: the trace runs the routes, the interpret sources, the macros and the
scene-state machine. With `--plot` (needs matplotlib), it also plots energy, baseline, lift, pace, dose, depth
and stage.

This is how the arc was tuned so that it works for any song (03-implementation.md, "The arc"). Use it on a new track
before rendering: if the track never leaves the Dream, raise `macros/sensitivity` in its project, as a performer
would trim the SENSITIVITY knob.
"""
from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROBE = ["macro.energy", "macro.baseline", "macro.dose", "macro.keyframe", "state.index", "audio.energy",
         "audio.trebleLevel"]
STAGES = ["Dream", "Uncanny", "Infection", "Corruption", "Nightmare", "Collapse", "Decay", "Pixels", "Light",
          "Respite", "Recovery"]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("project")
    ap.add_argument("--out", default=None)
    ap.add_argument("--plot", default=None)
    ap.add_argument("--avgen", default=str(REPO / "build/release/src/avgen"))
    a = ap.parse_args()
    src = Path(a.project).resolve()
    p = json.loads(src.read_text())
    # absolute asset paths, so the copy can live anywhere
    for key in ("scene", "audio"):
        if key in p.get("assets", {}):
            p["assets"][key]["path"] = str((src.parent / p["assets"][key]["path"]).resolve())
    if "audio" not in p.get("assets", {}):
        print("this project has no audio file (a LIVE project?): trace the file projects instead")
        return 2
    p.setdefault("sonic", {})
    gains = {"state.index": 0.1}
    p["sources"].append({"kind": "interpret", "name": "probe", "settings": {"mappings": [
        {"name": k.replace(".", "_"), "combine": "mean", "inputs": [{"signal": k, "weight": 1.0}], "bias": 0.0,
         "gain": gains.get(k, 1.0), "curve": 1.0} for k in PROBE]}})
    with tempfile.TemporaryDirectory() as tmp:
        proj = Path(tmp) / "arc.json"
        proj.write_text(json.dumps(p))
        out = Path(a.out) if a.out else Path(tmp) / "arc.csv"
        r = subprocess.run([a.avgen, "--headless", "--project", str(proj), "--sonic-trace", str(out)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stdout[-2000:], r.stderr[-2000:])
            return r.returncode
        rows = list(csv.DictReader(out.open()))
        t = [float(row["time"]) for row in rows]
        idx = [int(round(float(row["visual.state_index"]) * 10)) for row in rows]
        spans = []
        for ti, k in zip(t, idx):
            if not spans or spans[-1][0] != k:
                spans.append([k, ti, ti])
            spans[-1][2] = ti
        print(src.name + ":")
        for k, a0, a1 in spans:
            print(f"  {STAGES[k] if 0 <= k < len(STAGES) else k:<11} {a0:6.1f} - {a1:6.1f} s")
        if a.plot:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
            fig, ax = plt.subplots(figsize=(15, 4))
            for k in ["visual.macro_energy", "visual.macro_baseline", "visual.lift", "visual.pace",
                      "visual.macro_dose", "visual.depth", "visual.state_index"]:
                ax.plot(t, [float(row[k]) for row in rows], label=k.replace("visual.", ""), lw=0.9)
            ax.legend(fontsize=7, loc="upper left")
            ax.grid(alpha=0.3)
            ax.set_title(src.name)
            fig.tight_layout()
            fig.savefig(a.plot, dpi=80)
    return 0


if __name__ == "__main__":
    sys.exit(main())

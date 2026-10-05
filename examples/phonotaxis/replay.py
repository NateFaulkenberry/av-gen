#!/usr/bin/env python3
"""Turn a recorded PHONOTAXIS performance into a project that renders it offline, deterministically.

A live performance is live audio plus MIDI. A headless render has no MIDI (control IO is off), so the replay
moves the performer onto the timeline:
  * each pad hit becomes an event key on a `timeline` source named like the pad (timeline.padSurge, ...),
    and the states' and routes' `control.<pad>` triggers are renamed to it;
  * each knob move becomes keys on a timeline track on its macro (macros/<knob>), the same values the CC set;
  * the strike field's trigger listens to timeline.strike (a scene variant, phonotaxis-replay.scene.json).
The music is the same file the performer played into the live input, from the same second.

    replay.py <perform-events.csv> --start 176 [--score default] [--out phonotaxis-replay.json]

then render it (the range is the performance: start .. start + its length):
    avgen --headless --project examples/phonotaxis/phonotaxis-replay.json --render out.mp4 --range 176:250 ...
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("events")
    ap.add_argument("--start", type=float, required=True, help="the song second the performance began at")
    ap.add_argument("--score", default="default")
    ap.add_argument("--out", default=str(HERE / "phonotaxis-replay.json"))
    a = ap.parse_args()

    import perform  # the score: sweep durations and starting values
    rows = list(csv.DictReader(open(a.events)))
    t0 = next(int(r["hostNs"]) for r in rows if r["kind"] == "audio-start")
    rel = lambda r: (int(r["hostNs"]) - t0) / 1e9  # noqa: E731
    sweeps = [e for e in perform.SCORES[a.score] if e[1] == "sweep"]

    pads: dict[str, list[float]] = {}
    knobs: dict[str, list[tuple[float, float]]] = {}
    current = {"energy": 0.0, "memory": 0.0, **{k: 0.5 for k in perform.CC if k not in ("energy", "memory")}}
    si = 0
    for r in rows:
        kind, what = r["kind"], r["what"]
        if kind == "pad":
            pads.setdefault(what, []).append(a.start + rel(r))
        elif kind == "cc":
            t = a.start + rel(r)
            knobs.setdefault(what, []).append((t, float(r["value"])))
            current[what] = float(r["value"])
        elif kind == "sweep":
            to, seconds = sweeps[si][3]
            si += 1
            end = a.start + rel(r)
            knobs.setdefault(what, []).append((end - seconds, current[what]))
            knobs[what].append((end, float(to)))
            current[what] = float(to)

    base = json.loads((HERE / "phonotaxis.json").read_text())
    scene = json.loads((HERE / "phonotaxis.scene.json").read_text())
    for n in scene["nodes"]:
        if n["name"] == "strike":
            n["field"]["trigger"]["name"] = "timeline.strike"
    (HERE / "phonotaxis-replay.scene.json").write_text(json.dumps(scene, indent=1) + "\n")

    p = json.loads(json.dumps(base))
    p["app"]["name"] = "PHONOTAXIS (performance replay)"
    p["assets"]["scene"]["path"] = "phonotaxis-replay.scene.json"
    p.pop("control", None)
    names = {"strike": "strike", "scatter": "scatter"}
    for st in perform.PAD:
        if st not in names:
            names[st] = "pad" + st
    for pad, times in pads.items():
        p["sources"].append({"kind": "timeline", "name": names[pad], "settings": {
            "mode": "event", "keys": [{"time": round(t, 4), "value": 1.0, "interp": "step"} for t in times]}})
    for s in p["states"]["states"]:
        for t in s.get("triggers", []):
            if str(t.get("signal", "")).startswith("control."):
                t["signal"] = "timeline." + t["signal"][len("control."):]
    for r in p["routes"]:
        if r["source"].startswith("control."):
            r["source"] = "timeline." + r["source"][len("control."):]
            if r.get("depthSource", "").startswith("control."):
                r["depthSource"] = "timeline." + r["depthSource"][len("control."):]
    tracks = []
    for knob, keys in knobs.items():
        keys = sorted(keys)
        tracks.append({"target": f"macros/{knob}", "component": -1, "timeBase": "seconds", "mode": "replace",
                       "loopLength": 0.0, "enabled": True,
                       "keys": [{"time": round(a.start - 1.0, 4), "value": [keys[0][1] if knob in ("energy", "memory") else 0.5],
                                 "interp": "smooth"}] +
                               [{"time": round(t, 4), "value": [v], "interp": "smooth"} for t, v in keys]})
    p["timeline"] = {"enabled": True, "cues": [], "tracks": tracks}
    p["_note"] = (f"A performance recorded live ({Path(a.events).name}), moved onto the timeline so a headless "
                  "render can play it: pads are timeline events, knobs are tracks on their macros.")
    Path(a.out).write_text(json.dumps(p, indent=1) + "\n")
    print("wrote", a.out, {k: len(v) for k, v in pads.items()}, {k: len(v) for k, v in knobs.items()})


if __name__ == "__main__":
    main()

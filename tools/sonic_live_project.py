#!/usr/bin/env python3
"""The Sonic Live demo project (ADR-1025, 01-brief-live.md PART 15's plumbing).

    python3 tools/sonic_live_project.py

Writes examples/sonic-garden/sonic-live.json from the Sonic Garden master (examples/sonic-garden/sonic-garden.json),
beside it so every relative path in the scene still resolves. It is the master with only what a live instrument
cannot have taken out:

  - no audio file: the sound comes from the live audio input;
  - no MIDI file: the notes come from the live MIDI input;
  - `sonic.live: true`, so the live editor turns live input on when it opens the project;
  - the camera held at the master's mid-move framing (its keyed push-in is 21.5 s long and a performance is not).

Everything the art reads -- the interpreter mappings, the routes, the character tuning, the scene -- is the master's,
unchanged: the worlds read only bus signals, so live input needs no art change. The art agent tunes it for live
play (PARTS 15-16) by editing the master's tool (tools/sonic_garden_look.py) or by adding live-only overrides
HERE, then re-running this.
"""
import copy
import json
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GARDEN = os.path.join(REPO, "examples", "sonic-garden")
MASTER = os.path.join(GARDEN, "sonic-garden.json")
OUT = os.path.join(GARDEN, "sonic-live.json")

# The camera's hold: the master's key at this second (the middle of its push-in).
HOLD_SECONDS = 10.75


def value_at(track, seconds):
    keys = sorted(track["keys"], key=lambda k: k["time"])
    for key in keys:
        if abs(key["time"] - seconds) < 1e-6:
            return key["value"]
    # No key exactly there: the nearest one (the tool is only ever asked for an authored key).
    return min(keys, key=lambda k: abs(k["time"] - seconds))["value"]


def main():
    with open(MASTER) as f:
        doc = json.load(f)
    live = copy.deepcopy(doc)
    live["app"] = {"name": "Sonic Live"}
    live.get("assets", {}).pop("audio", None)
    sonic = live.setdefault("sonic", {})
    sonic.pop("notes", None)
    sonic["live"] = True
    for track in live.get("timeline", {}).get("tracks", []):
        if track.get("target") in ("camera/position", "camera/target"):
            track["keys"] = [{"time": 0.0, "value": value_at(track, HOLD_SECONDS), "interp": "smooth"}]
    live.setdefault("render", {})["path"] = "renders/sonic-live"
    with open(OUT, "w") as f:
        json.dump(live, f, indent=1)
        f.write("\n")
    print(f"wrote {os.path.relpath(OUT, REPO)}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""DIGITAL MOSH: play a track into the live input, for testing the LIVE project with real music.

Plays an audio file into an output device that AV Gen captures as its live input (BlackHole on this machine), after a
lead-in, and records the host time (time.monotonic_ns: mach_absolute_time in ns, the clock AV Gen's --live-capture
and --sonic-live-log use) at which the music started, so a capture can be joined to the music afterwards.

Needs: sounddevice, soundfile, numpy.

    play_live.py --audio assets/audio/trench.wav --start 40 --seconds 60 --out live-start.json [--device BlackHole]

Run AV Gen first:
    avgen --project examples/digital-mosh/digital-mosh-live.json --input "BlackHole 2ch" \\
          --live-capture cap --live-capture-every 2 --sonic-live-log live.csv
"""
from __future__ import annotations

import argparse
import json
import time

import numpy as np
import sounddevice as sd
import soundfile as sf


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--audio", required=True)
    ap.add_argument("--start", type=float, default=0.0)
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--device", default="BlackHole")
    ap.add_argument("--lead-in", type=float, default=4.0)
    ap.add_argument("--out", default="live-start.json")
    a = ap.parse_args()
    data, rate = sf.read(a.audio, dtype="float32", always_2d=True)
    s0 = int(a.start * rate)
    clip = data[s0:s0 + int(a.seconds * rate)]
    if clip.shape[1] == 1:
        clip = np.repeat(clip, 2, axis=1)
    device = next(i for i, d in enumerate(sd.query_devices())
                  if a.device.lower() in d["name"].lower() and d["max_output_channels"] >= 2)
    time.sleep(a.lead_in)
    host_ns = time.monotonic_ns()
    sd.play(clip[:, :2], rate, device=device, blocking=True)
    json.dump({"audio": a.audio, "start": a.start, "seconds": a.seconds, "hostStartNs": host_ns, "rate": rate},
              open(a.out, "w"), indent=1)
    print("played", a.seconds, "s from", a.start, "s; host start", host_ns)


if __name__ == "__main__":
    main()

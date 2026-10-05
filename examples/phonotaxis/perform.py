#!/usr/bin/env python3
"""A scripted performer for PHONOTAXIS LIVE: stands in for a person with a MIDI controller and a sound source.

It plays an audio file into an audio OUTPUT device that AV Gen captures as its live input (BlackHole on this
machine), and sends a score of MIDI knob moves and pad hits from a CoreMIDI virtual source ("PHONOTAXIS
Performer"). Every event is logged on the host clock (time.monotonic_ns, which is mach_absolute_time in ns on
macOS: the clock AV Gen's --sonic-live-log and --live-capture use), so the logs join.

Needs: pip install mido python-rtmidi sounddevice soundfile numpy

    perform.py --audio song.wav --start 140 --seconds 70 --out events.csv [--device BlackHole] [--score default]

Run AV Gen first, e.g.
    avgen --project examples/phonotaxis/phonotaxis-live.json --input BlackHole --midi "PHONOTAXIS Performer" \
          --sonic-live-log app.csv --live-capture cap --live-capture-every 2
The script waits --lead-in seconds so AV Gen can connect to the virtual source before the score starts.
"""
from __future__ import annotations

import argparse
import csv
import threading
import time

import mido
import numpy as np
import sounddevice as sd
import soundfile as sf

# The score: (seconds from the start, kind, what, value). Kinds: cc (number, 0..1), pad (note), sweep (cc, to,
# seconds) -- a knob turned smoothly. The vocabulary is build.py's KNOBS and PADS.
CC = {"energy": 1, "hunger": 21, "restless": 22, "current": 23, "memory": 24, "reach": 25, "orbit": 26,
      "glow": 27, "sensitivity": 28}
PAD = {"strike": 36, "scatter": 37, "Dormant": 40, "Germination": 41, "Chorus": 43, "Surge": 45, "Eruption": 47,
       "Collapse": 48, "Rebirth": 50}

SCORES = {
    "default": [
        (0.0, "cc", "sensitivity", 0.3),
        (4.0, "pad", "Germination", 1.0),
        (10.0, "pad", "Chorus", 1.0),
        (14.0, "sweep", "reach", (0.25, 6.0)),
        (20.0, "pad", "strike", 1.0),
        (24.0, "pad", "Surge", 1.0),
        (26.0, "sweep", "restless", (0.95, 5.0)),
        (31.0, "pad", "strike", 1.0),
        (34.0, "sweep", "restless", (0.5, 3.0)),
        (36.0, "pad", "Eruption", 1.0),
        (37.0, "sweep", "memory", (0.7, 4.0)),
        (43.0, "pad", "scatter", 1.0),
        (46.0, "sweep", "memory", (0.0, 3.0)),
        (48.0, "pad", "Collapse", 1.0),
        (49.0, "sweep", "reach", (0.5, 4.0)),
        (56.0, "pad", "Rebirth", 1.0),
        (58.0, "sweep", "orbit", (0.85, 4.0)),
        (63.0, "pad", "strike", 1.0),
        (65.0, "pad", "strike", 0.8),
        (66.0, "sweep", "orbit", (0.5, 3.0)),
    ],
    # one pad hit every two seconds, for MIDI latency (the strike's spore burst is visible in the next frame)
    "latency": [(2.0 * i, "pad", "scatter", 1.0) for i in range(1, 30)],
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--audio", required=True)
    ap.add_argument("--start", type=float, default=0.0)
    ap.add_argument("--seconds", type=float, default=70.0)
    ap.add_argument("--device", default="BlackHole")
    ap.add_argument("--out", default="perform-events.csv")
    ap.add_argument("--score", default="default")
    ap.add_argument("--lead-in", type=float, default=4.0)
    a = ap.parse_args()

    data, sr = sf.read(a.audio, dtype="float32", always_2d=True)
    data = data[int(a.start * sr): int((a.start + a.seconds) * sr)]
    mido.set_backend("mido.backends.rtmidi")
    port = mido.open_output("PHONOTAXIS Performer", virtual=True)
    log = []
    lock = threading.Lock()

    def note(kind, what, value):
        with lock:
            log.append((time.monotonic_ns(), kind, what, value))

    time.sleep(a.lead_in)  # let AV Gen find the virtual source
    t0 = time.monotonic()
    note("audio-start", a.audio, a.start)
    stream_pos = {"i": 0}

    def callback(out, frames, _time, _status):
        i = stream_pos["i"]
        chunk = data[i:i + frames]
        out[:len(chunk)] = chunk
        out[len(chunk):] = 0
        stream_pos["i"] = i + frames

    with sd.OutputStream(device=a.device, samplerate=sr, channels=data.shape[1], callback=callback):
        for at, kind, what, value in SCORES[a.score]:
            if at > a.seconds:
                break
            while time.monotonic() - t0 < at:
                time.sleep(0.001)
            if kind == "cc":
                port.send(mido.Message("control_change", control=CC[what], value=int(round(value * 127))))
                note("cc", what, value)
            elif kind == "pad":
                port.send(mido.Message("note_on", note=PAD[what], velocity=int(round(value * 127))))
                note("pad", what, value)
                port.send(mido.Message("note_off", note=PAD[what], velocity=0))
            elif kind == "sweep":
                to, seconds = value
                # a knob turned by hand: 30 messages a second along a smooth curve from wherever it was
                start = getattr(main, "_knobs", {}).get(what, 0.5)
                steps = max(1, int(seconds * 30))
                for k in range(1, steps + 1):
                    x = k / steps
                    v = start + (to - start) * (0.5 - 0.5 * np.cos(np.pi * x))
                    port.send(mido.Message("control_change", control=CC[what], value=int(round(v * 127))))
                    time.sleep(seconds / steps)
                note("sweep", what, to)
                main._knobs = {**getattr(main, "_knobs", {}), what: to}
            if kind == "cc":
                main._knobs = {**getattr(main, "_knobs", {}), what: value}
        while time.monotonic() - t0 < a.seconds:
            time.sleep(0.01)
    note("audio-end", a.audio, a.start + a.seconds)
    with open(a.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["hostNs", "kind", "what", "value"])
        w.writerows(log)
    print("wrote", a.out, len(log), "events")


if __name__ == "__main__":
    main()

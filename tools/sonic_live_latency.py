#!/usr/bin/env python3
"""Latency of live Sonic input (ADR-1025): joins avgen_sonic_probe's event log with the app's --sonic-live-log.

    python3 tools/sonic_live_latency.py <probe-events.csv> <app-log.csv>

Both logs are on the host clock (mach_absolute_time, ns), so they join directly. For every note the probe played
(MIDI sent and the synth voice started at the same instant):
  midi->bus    the first frame whose note count includes it: frame start (the bus is written in that frame)
  midi->submit that frame's present call returning (its picture handed to the compositor)
  audio->bus   the first frame whose raw transient (timbre.transient) exceeds 0.3, or loudness rises 12 dB
  audio->submit
Scanout adds up to one display refresh after "submit" (8.3 ms at 120 Hz, 16.7 at 60), plus the GPU's frame.
"""
import csv
import statistics
import sys


def load(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def main():
    probe = load(sys.argv[1])
    app = load(sys.argv[2])
    ons = [int(r["hostNs"]) for r in probe if r["kind"] == "on"]
    frames = [(int(r["frameNs"]), int(r["presentNs"]), int(r["notesReceived"]), float(r["timbre.transient"]),
               float(r["timbre.loudness"]), float(r["gpuMs"])) for r in app]
    base_notes = None
    rows = []
    for i, t in enumerate(ons):
        before = [f for f in frames if f[0] < t]
        if not before:
            continue
        count_before = before[-1][2]
        loud_before = before[-1][4]
        midi = next((f for f in frames if f[0] >= t and f[2] > count_before), None)
        audio = next((f for f in frames if f[0] >= t and (f[3] > 0.3 or f[4] > loud_before + 12.0)), None)
        rows.append(((midi[0] - t) / 1e6 if midi else None, (midi[1] - t) / 1e6 if midi else None,
                     (audio[0] - t) / 1e6 if audio else None, (audio[1] - t) / 1e6 if audio else None))
    gpu = statistics.median(f[5] for f in frames if f[5] > 0)
    period = statistics.median((b[0] - a[0]) / 1e6 for a, b in zip(frames, frames[1:]))
    print(f"notes: {len(rows)}   frame period p50 {period:.1f} ms   GPU frame p50 {gpu:.1f} ms")
    for name, k in (("midi->bus", 0), ("midi->submit", 1), ("audio->bus", 2), ("audio->submit", 3)):
        v = sorted(r[k] for r in rows if r[k] is not None)
        if not v:
            print(f"{name:14s} none detected")
            continue
        p = lambda q: v[min(len(v) - 1, int(q * (len(v) - 1) + 0.5))]
        print(f"{name:14s} n={len(v):2d}  min {v[0]:6.1f}  p50 {p(0.5):6.1f}  p90 {p(0.9):6.1f}  max {v[-1]:6.1f} ms")
    d = sorted(r[2] - r[0] for r in rows if r[0] is not None and r[2] is not None)
    if d:
        print(f"audio behind midi (same note): p50 {d[len(d)//2]:.1f} ms, range {d[0]:.1f}..{d[-1]:.1f} ms")


if __name__ == "__main__":
    main()

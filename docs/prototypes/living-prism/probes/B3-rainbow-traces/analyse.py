#!/usr/bin/env python3
"""Living Prism probe B3: the traces' numbers from the two Rainbow World --sonic-trace CSVs.

usage: analyse.py <rw-mix.csv> <rw-stems.csv> <fps>
"""
import csv
import math
import statistics as st
import sys

fps = float(sys.argv[3])
dt = 1.0 / fps


def load(p):
    rows = list(csv.DictReader(open(p)))
    return {k: [float(r[k]) for r in rows] for k in rows[0]}


def rises(v, thr):
    return [i for i in range(1, len(v)) if v[i] > thr and v[i - 1] <= thr]


def pct(v, q):
    s = sorted(v)
    return s[min(len(s) - 1, int(q * len(s)))]


for name, path in (("mix", sys.argv[1]), ("stems", sys.argv[2])):
    d = load(path)
    T = d["time"]
    print(f"===== {name}: {len(T)} frames, {T[-1]:.1f} s")
    # ---- trace 1: a transient. response.snare (traced through env.snareEv, +1 frame) and the arch bump knob.
    sn = [i - 1 for i in rises(d["env.snareEv"], 0.05)]  # the frame the event was on the bus
    on = [i - 1 for i in rises(d["response.onsetEnv"], 0.3)]  # response.onsetEnv is published in-frame
    deltas = []
    for i in sn:
        prev = [j for j in on if i - int(0.1 * fps) <= j <= i]
        if prev:
            deltas.append((i - max(prev)) * dt * 1000)
    print(f"T1 response.snare events {len(sn)}; with a response.onset in the 100 ms before: {len(deltas)}; "
          f"snare minus onset, ms: median {st.median(deltas) if deltas else float('nan'):.1f} "
          f"p10 {pct(deltas, 0.1) if deltas else float('nan'):.1f} p90 {pct(deltas, 0.9) if deltas else float('nan'):.1f}")
    k = d.get("macro.p_snareArch")
    if k:
        kr = rises(k, 0.2)
        lag = [min((j - i for j in kr if j >= i), default=None) for i in sn]
        lag = [x for x in lag if x is not None and x <= 3]
        print(f"   arch-bump route final (knob, +1 frame) rises {len(kr)}; frames from the snare event to the knob: "
              f"{sorted(set(lag))} counts {[lag.count(x) for x in sorted(set(lag))]}; knob p50/p90/max "
              f"{pct(k, .5):.2f}/{pct(k, .9):.2f}/{max(k):.2f}")
    c = d.get("macro.p_clapArchStem")
    if c and max(c) > 0:
        print(f"   stem clap route (stem.drums.highMid band, gated by offset -1.55): rises>0.2 {len(rises(c, 0.2))}, "
              f"p50/p90 {pct(c, .5):.2f}/{pct(c, .9):.2f}")
    # ---- trace 2: continuous. response.bass -> macros/swell (gain 2.4, offset -1.45, clamp, attack 10 / decay 180)
    rb, sw = d["response.bass"], d["macro.swell"]
    y, s = [], None
    for x in rb:
        u = min(1.0, max(0.0, 2.4 * x - 1.45))
        if s is None:
            s = u
        else:
            tau = 10.0 if u > s else 180.0
            s = s + (u - s) * (1 - math.exp(-dt * 1000 / tau))
        y.append(s)
    for lag in (0, 1, 2):
        err = max(abs(sw[i] - y[i - lag]) for i in range(5, len(sw)))
        print(f"T2 macro.swell vs the chain of response.bass, {lag} frame(s) earlier: max |diff| {err:.4f}")
    print(f"   response.bass p10/p50/p90 {pct(rb, .1):.2f}/{pct(rb, .5):.2f}/{pct(rb, .9):.2f}; macro.swell "
          f"p10/p50/p90 {pct(sw, .1):.2f}/{pct(sw, .5):.2f}/{pct(sw, .9):.2f}; share > 0.3: "
          f"{sum(1 for v in sw if v > 0.3) / len(sw):.0%}")
    # ---- trace 4: long-timescale. macro.scan (trend > threshold -> latch 6.5 s hold, 12 s refractory -> spring)
    sc = d["macro.scan"]
    fires = rises(sc, 0.5)
    spans = []
    for i in fires:
        j = i
        while j < len(sc) and sc[j] > 0.5:
            j += 1
        spans.append((T[i], (j - i) * dt))
    print(f"T4 macro.scan fires (>0.5): {len(fires)}: " + ", ".join(f"{t:.1f}s for {w:.1f}s" for t, w in spans))
    # (event columns read 0 after Engine::update's clearEvents, so phrases are counted from beat.phraseCount)
    print(f"   phrases {int(max(d['beat.phraseCount'])) + 1}; bpm median {st.median(d['beat.bpm']):.1f}")

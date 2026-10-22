#!/usr/bin/env python3
"""Living Prism probe B4: precision, recall and latency of every hit detector against a reference.

usage: analyse.py <trace.csv> <fps> synthetic <truth.json>
       analyse.py <trace.csv> <fps> stem          (reference = the drum stem's classifier in the same trace)

Events are read through envelope sources (env.<name>), which publish one frame after their trigger (probe B1); every
column shares that frame, so it is subtracted once. A detector event matches the nearest unmatched reference event of
the class within [-30 ms, +120 ms] of it (a detector may be early by its window half; the classes are up to ~70 ms
late). Precision = matched / events; recall = matched / reference events; latency = event - reference, median and
p90. "Hits elsewhere" says what the unmatched events landed on (another class, or nothing).
"""
import csv
import json
import statistics as st
import sys

trace, fps, mode = sys.argv[1], float(sys.argv[2]), sys.argv[3]
dt = 1.0 / fps
rows = list(csv.DictReader(open(trace)))
T = [float(r["time"]) for r in rows]


def events(col, thr=0.2):
    if col not in rows[0]:
        return None
    v = [float(r[col]) for r in rows]
    return [T[i] - dt for i in range(1, len(v)) if v[i] > thr and v[i - 1] <= thr]  # -dt: the env source's frame


if mode == "synthetic":
    truth = json.load(open(sys.argv[4]))
    ref = {"kick": truth["kicks"], "snare": truth["snares"], "hat": truth.get("hats", []), "bass": truth["bassNotes"]}
    end = 18.5
else:
    ref = {"kick": events("env.dKick"), "snare": events("env.dSnare"), "hat": events("env.dHat"),
           "drum": events("env.dOnset")}
    end = T[-1]
ref = {k: [t for t in v if t < end] for k, v in ref.items() if v}


def match(ev, refs):
    used, lat = set(), []
    for t in ev:
        best = None
        for j, r in enumerate(refs):
            d = t - r
            if -0.03 <= d <= 0.12 and j not in used and (best is None or abs(d) < abs(best[1])):
                best = (j, d)
        if best:
            used.add(best[0])
            lat.append(best[1] * 1000)
    return len(used), lat


cols = [("env.rKick", "kick"), ("env.rSnare", "snare"), ("env.rHat", "hat"), ("env.rLow", "kick"),
        ("env.rOnset", None), ("env.aOnset", None), ("env.aLow", "kick"), ("env.aMid", "snare"),
        ("env.aHigh", "hat"), ("env.beatPulse", "kick"), ("env.dKick", "kick"), ("env.dSnare", "snare"),
        ("env.dHat", "hat"), ("env.dOnset", None)]
print(f"{trace}: reference counts " + ", ".join(f"{k} {len(v)}" for k, v in ref.items()))
print(f"{'detector':14} {'vs':6} {'events':>6} {'prec':>5} {'recall':>6}  lat ms med/p90   unmatched events landed on")
for col, cls in cols:
    ev = events(col)
    if ev is None:
        continue
    ev = [t for t in ev if t < end]
    if mode == "stem" and col in ("env.dKick", "env.dSnare", "env.dHat"):
        print(f"{col:14} (the reference: {len(ev)} events)")
        continue
    if cls is None:  # broadband: against every drum hit
        allref = sorted(sum((v for k, v in ref.items() if k in ("kick", "snare", "hat", "drum")), []))
        if mode == "stem":
            allref = ref.get("drum", allref)
        m, lat = match(ev, allref)
        n_ref = len(allref)
        label = "drums"
    else:
        m, lat = match(ev, ref.get(cls, []))
        n_ref = len(ref.get(cls, []))
        label = cls
    other = {}
    matched_set = set()
    for t in ev:
        for k, v in ref.items():
            if k == cls or (cls is None):
                continue
            if any(-0.03 <= t - r <= 0.12 for r in v):
                other[k] = other.get(k, 0) + 1
                break
    prec = m / len(ev) if ev else 0
    rec = m / n_ref if n_ref else 0
    l = f"{st.median(lat):5.1f}/{sorted(lat)[int(0.9 * (len(lat) - 1))]:5.1f}" if lat else "   -  "
    print(f"{col:14} {label:6} {len(ev):6} {prec:5.2f} {rec:6.2f}  {l}   {other if cls else ''}")

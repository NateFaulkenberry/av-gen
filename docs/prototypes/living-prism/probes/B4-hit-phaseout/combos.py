#!/usr/bin/env python3
"""Living Prism probe B4b: could a mix-only "kick" be built from honest parts? Coincidence rules over the B4 traces.

usage: combos.py <trace.csv> <fps> synthetic <truth.json> | combos.py <trace.csv> <fps> stem

Candidates (all causal, all from existing mix signals; a stand-in for P1-1's register attribute, which does not exist):
  onset&low   response.onset with response.low within +-1 frame          (a broadband attack with low-band energy)
  onset&beat  response.onset within 2 frames of beat.pulse               (an attack on the beat clock)
  onset&low&beat
Scored like analyse.py: precision and recall against the reference kicks within [-30, +120] ms.
"""
import csv, json, statistics as st, sys
trace, fps, mode = sys.argv[1], float(sys.argv[2]), sys.argv[3]
dt = 1 / fps
rows = list(csv.DictReader(open(trace))); T = [float(r["time"]) for r in rows]
def ev(c):
    v = [float(r[c]) for r in rows]
    return [i - 1 for i in range(1, len(v)) if v[i] > 0.2 and v[i - 1] <= 0.2]
if mode == "synthetic":
    kicks = [t for t in json.load(open(sys.argv[4]))["kicks"]]
else:
    kicks = [T[i] for i in ev("env.dKick")]
on, low, beat = ev("env.rOnset"), set(ev("env.rLow")), ev("env.beatPulse")
near = lambda i, S, w: any(abs(i - j) <= w for j in S)
cands = {"onset&low": [i for i in on if near(i, low, 1)],
         "onset&beat": [i for i in on if near(i, beat, 2)],
         "onset&low&beat": [i for i in on if near(i, low, 1) and near(i, beat, 2)],
         "beat.pulse": beat}
for name, idx in cands.items():
    times = [T[i] for i in idx]
    used, lat = set(), []
    for t in times:
        best = min(((abs(t - k), j, t - k) for j, k in enumerate(kicks) if -0.03 <= t - k <= 0.12 and j not in used), default=None)
        if best:
            used.add(best[1]); lat.append(best[2] * 1000)
    p = len(used) / len(times) if times else 0; r = len(used) / len(kicks)
    print(f"{name:16} events {len(times):5} precision {p:.2f} recall {r:.2f} latency med {st.median(lat) if lat else float('nan'):.1f} ms")

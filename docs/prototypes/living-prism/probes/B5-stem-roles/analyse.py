#!/usr/bin/env python3
"""Living Prism probe B5: what each event signal did over the song, against the drum stem's own classifier.

usage: analyse.py <reference trace.csv> <trace.csv>...

Each trace's project is beside it (`<name>.json`). Events are read through its envelope sources (env.<name>, which
publish one frame after their trigger -- probe B1 -- so one frame is subtracted), as probe B4 does: --sonic-trace
records an event column after the frame's events are cleared (audit R5). The reference is the drum stem's classifier
(the env on `stem.drums.kick/snare/hat`) in the first trace (probe B4's reference). A
detector event matches the nearest unmatched reference event within [-30 ms, +120 ms]. Per trace: every event column's
count, and precision / recall of `signal.kick`, `role.drums.*` and `stem.drums.*` against the reference. Also the
correlation of `signal.bassLevel` / `signal.synthLevel` with the bass / synth stem's level and with the mix's stand-in.
"""
import csv
import math
import sys


def load(path):
    import json
    rows = list(csv.DictReader(open(path)))
    T = [float(r["time"]) for r in rows]
    doc = json.load(open(path[:-4] + ".json"))
    envs = {s["settings"]["trigger"]: "env." + s["name"] for s in doc["sources"] if s["kind"] == "envelope"}
    return rows, T, envs


def events(trace, signal, thr=0.2):
    rows, T, envs = trace
    col = envs.get(signal)
    if col is None or col not in rows[0]:
        return None
    dt = T[1] - T[0]
    v = [float(r[col]) for r in rows]
    return [T[i] - dt for i in range(1, len(v)) if v[i] > thr and v[i - 1] <= thr]


def match(ev, refs):
    used, n = set(), 0
    for t in ev:
        best = None
        for j, r in enumerate(refs):
            d = t - r
            if -0.03 <= d <= 0.12 and j not in used and (best is None or abs(d) < abs(best[1])):
                best = (j, d)
        if best:
            used.add(best[0])
            n += 1
    return n


def corr(a, b):
    n = len(a)
    ma, mb = sum(a) / n, sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    if va <= 0 or vb <= 0:
        return float("nan")
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(va * vb)


ref_trace = load(sys.argv[1])
ref = {c: events(ref_trace, f"stem.drums.{c}") for c in ("kick", "snare", "hat")}
print(f"reference {sys.argv[1]}: " + ", ".join(f"{k} {len(v) if v else 'absent'}" for k, v in ref.items()))
for path in sys.argv[2:]:
    trace = load(path)
    rows, T, envs = trace
    print(f"\n== {path}")
    for signal in envs:
        ev = events(trace, signal)
        on_bus = not signal.startswith(("stem.", "role.")) or signal in rows[0]  # the trace lists every stem/role name
        if ev is None or not on_bus:
            print(f"  {signal:22}   not on the bus")
            continue
        cls = signal.split(".")[-1]
        line = f"  {signal:22} {len(ev):6}"
        if cls in ref and ref[cls] and not signal.startswith("response."):
            m = match(ev, ref[cls])
            line += f"   vs the drum stem's {cls}s: prec {m / len(ev) if ev else 0:4.2f} recall {m / len(ref[cls]):4.2f}"
        print(line)
    for sig, stem, mix in (("signal.bassLevel", "stem.bass.level", "response.bass"),
                           ("signal.synthLevel", "stem.synth.level", "response.sustain")):
        if sig in rows[0]:
            s = [float(r[sig]) for r in rows]
            out = f"  {sig:22} mean {sum(s) / len(s):.3f}"
            for other in (stem, mix):
                if other in rows[0]:
                    out += f"   r({other}) {corr(s, [float(r[other]) for r in rows]):.3f}"
            print(out)

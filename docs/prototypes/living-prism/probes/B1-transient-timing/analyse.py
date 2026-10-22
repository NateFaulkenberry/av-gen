#!/usr/bin/env python3
"""Living Prism probe B1: read the --sonic-trace CSV against truth.json.

usage: analyse.py <fixture dir> <trace.csv> <fps>

For every envelope column env.<detector> (attack 0, sustain 1, release 60 ms) a trigger is the first frame whose value
rises past 0.2 from below (an envelope source never shows its peak: see the report). Each trigger is matched to the nearest truth hit of the expected class within +-150 ms;
the report gives hits found, misses, false triggers and the frame-time minus true-onset offset (ms). Continuous
columns give the one-frame macro lag and the silence behaviour.
"""
import csv
import json
import statistics as st
import sys
from pathlib import Path

fix = Path(sys.argv[1])
trace = Path(sys.argv[2])
fps = float(sys.argv[3])
truth = json.loads((fix / "truth.json").read_text())
rows = list(csv.DictReader(open(trace)))
T = [float(r["time"]) for r in rows]
col = lambda n: [float(r[n]) for r in rows]

expect = {  # detector column -> truth class it should follow
    "env.aOnset": "any", "env.aLow": "kicks", "env.aMid": "snares", "env.rKick": "kicks", "env.rSnare": "snares",
    "env.rLow": "low", "env.rOnset": "any", "env.dOnset": "drums", "env.dKick": "kicks", "env.dSnare": "snares",
    "env.bOnset": "bassNotes", "env.aBeat": "kicks", "env.beatPulse": "kicks",
}
classes = {"kicks": truth["kicks"], "snares": truth["snares"], "bassNotes": truth["bassNotes"]}
classes["drums"] = sorted(classes["kicks"] + classes["snares"])
classes["low"] = sorted(classes["kicks"] + classes["bassNotes"])
classes["any"] = sorted(classes["drums"] + classes["bassNotes"])

print(f"fps {fps}; frames {len(rows)}")
print(f"{'detector':16} {'class':9} {'trig':>4} {'hit':>4} {'miss':>4} {'false':>5}  offset ms: median  p10   p90   (frame - true onset)")
for c, cls in expect.items():
    if c not in rows[0]:
        print(c, "absent")
        continue
    v = col(c)
    trig = [T[i] for i in range(1, len(v)) if v[i] > 0.2 and v[i - 1] <= 0.2]
    ref = classes[cls]
    offs, used = [], set()
    false = 0
    for t in trig:
        cand = [(abs(t - r), j) for j, r in enumerate(ref) if abs(t - r) <= 0.15 and j not in used]
        if not cand:
            false += 1
            continue
        _, j = min(cand)
        used.add(j)
        offs.append((t - ref[j]) * 1000)
    miss = len([r for r in ref if r < 18.5]) - len(used)
    if offs:
        q = sorted(offs)
        p10, p90 = q[len(q) // 10], q[(len(q) * 9) // 10]
        print(f"{c:16} {cls:9} {len(trig):4} {len(used):4} {miss:4} {false:5}  {st.median(offs):7.1f} {p10:6.1f} {p90:6.1f}")
    else:
        print(f"{c:16} {cls:9} {len(trig):4} {0:4} {miss:4} {false:5}")

# The one-frame macro lag (ADR-1573): macro.rms(frame k) against stem.drums.rms(frame k-1) and (frame k).
a, m = col("stem.drums.rms"), col("macro.rms")
d0 = max(abs(m[i] - a[i]) for i in range(1, len(a)))
d1 = max(abs(m[i] - a[i - 1]) for i in range(1, len(a)))
print(f"macro.rms vs stem.drums.rms: max |diff| same frame {d0:.4f}, against the previous frame {d1:.4f}")
# env.dKick vs macro.kick (a route-chain envelope into a knob): first rise frames
ek, mk = col("env.dKick"), col("macro.kick")
re = [i for i in range(1, len(ek)) if ek[i] > 0.2 and ek[i - 1] <= 0.2]
rm = [i for i in range(1, len(mk)) if mk[i] > 0.2 and mk[i - 1] <= 0.2]
pairs = [min((abs(j - i), j - i) for j in rm)[1] for i in re if rm]
if pairs:
    print(f"macro.kick rise frame minus env.dKick rise frame: {sorted(set(pairs))} (counts {[pairs.count(x) for x in sorted(set(pairs))]})")

# Silence behaviour: 19.2-20.8 s digital silence, 21.5-24 s a -70 dBFS noise floor in the mix only.
def window(name, lo, hi):
    v = [x for t, x in zip(T, col(name)) if lo <= t <= hi]
    return (min(v), st.median(v), max(v)) if v else None
print("silence (19.2-20.8 s digital zero) and noise floor (21.5-24 s, -70 dBFS, mix only): min / median / max")
for name in ["macro.aBass", "macro.aTreble", "macro.aOnsetStr", "macro.tempoConf", "response.level", "response.bass",
             "response.sustain", "response.kickEnv", "stem.drums.level", "stem.drums.active", "stem.bass.active",
             "beat.phase", "beat.bpm", "sonic.energy", "timbre.loudness"]:
    if name in rows[0]:
        a1, a2 = window(name, 19.2, 20.8), window(name, 21.5, 24.0)
        print(f"  {name:20} silence {a1[0]:.3f}/{a1[1]:.3f}/{a1[2]:.3f}   noise {a2[0]:.3f}/{a2[1]:.3f}/{a2[2]:.3f}")
# events during silence / noise
for c in expect:
    if c in rows[0]:
        v = col(c)
        n = sum(1 for i in range(1, len(v)) if v[i] > 0.2 and v[i - 1] <= 0.2 and T[i] > 18.9)
        if n:
            print(f"  triggers after 18.9 s (silence/noise): {c} {n}")

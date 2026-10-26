#!/usr/bin/env python3
"""Talking Heads: busts arrive and leave with their sources, cleanly (fx1, 14-brief-secondary-fx.md section 6).

Runs the engine's sonic trace (no GPU) with sources that arrive and leave mid-song (review.py `spans`: each stem cut to
a span and placed at its start), then checks every window between changes, after a second to settle:
  - the bust count (signal.thCount) equals the number of sources assigned there (1..4; never 5);
  - each shown bust is at full size and each hidden one has shrunk to nothing (final node scale);
  - a hidden bust is silent: its gate never opens, its rays stay dark (no stale state, no signal from another source);
  - no two shown busts share a layout place (no duplicates).
Exits non-zero on a failure.

    python3 tools/talking_heads/lifecycle_check.py --project <talking-heads.json>
"""
import argparse
import csv
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, REPO)
import review as rv  # noqa: E402
from tools.sonic_vfx.scenes import talking_heads as th  # noqa: E402

AVGEN = os.environ.get("AVGEN") or os.path.join(REPO, "build", "release", "src", "avgen")
# drums throughout; bass from 70 s; synth 80-100 s; the mix (as `other`) 90-105 s
SPANS = "bass:70:,synth:80:100,other:90:105"
# each window starts 1.5 s after a change: a bust glides 0.7 s to its new place and shrinks away in 0.5 s
WINDOWS = [(61.5, 69, {"th1"}), (71.5, 79, {"th1", "th2"}), (81.5, 89, {"th1", "th2", "th3"}),
           (91.5, 99, {"th1", "th2", "th3", "th4"}), (101.5, 104, {"th1", "th2", "th4"}), (106.5, 110, {"th1", "th2"})]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    a = ap.parse_args()
    work = tempfile.mkdtemp(prefix="th-life-")
    proj = rv.variant(a.project, "feline", True, work, sources="drums,bass,synth,other", spans=SPANS)
    out = os.path.join(work, "t.csv")
    r = subprocess.run([AVGEN, "--project", proj, "--sonic-trace", out, "--trace-finals"], capture_output=True,
                       text=True)
    if r.returncode != 0:
        sys.exit("avgen exited %d\n%s" % (r.returncode, r.stderr[-2000:]))
    rows = list(csv.DictReader(open(out)))
    fails = []
    for t0, t1, shown in WINDOWS:
        win = [x for x in rows if t0 <= float(x["time"]) <= t1]
        counts = {round(float(x["signal.thCount"])) for x in win}
        line = "%5.1f-%5.1f s: expect %d %s | count %s" % (t0, t1, len(shown), sorted(shown), sorted(counts))
        ok = counts == {len(shown)}
        for h in th.HEADS:
            P = h["prefix"]
            sc = [float(x["final:nodes/%s-bust/scale[0]" % P]) for x in win]
            live = max(float(x["signal.%sLive" % P]) for x in win)
            rays = max(float(x.get("final:fx/%s-mouth0/intensity[0]" % P, 0.0)) for x in win)
            if P in shown:
                good = min(sc) > 0.8   # the four-bust layout's smallest is 0.92
                line += " | %s shown (scale %.2f-%.2f)" % (P, min(sc), max(sc))
            else:
                good = max(sc) < 0.01 and live < 0.5 and rays < 1e-6   # 0.001 when settled
                line += " | %s hidden (scale %.3f, gate %s, rays %.1g)" % (P, max(sc), "shut" if live < 0.5 else
                                                                             "OPEN", rays)
            ok = ok and good
        places = [(round(float(win[-1]["final:nodes/%s-bust/position[0]" % h["prefix"]]), 2),
                   round(float(win[-1]["final:nodes/%s-bust/position[2]" % h["prefix"]]), 2))
                  for h in th.HEADS if h["prefix"] in shown]
        ok = ok and len(set(places)) == len(places)
        print(("ok    " if ok else "FAIL  ") + line)
        if not ok:
            fails.append(line)
    if fails:
        sys.exit("%d window(s) wrong" % len(fails))
    print("busts arrive and leave with their sources: no fifth, no phantom, no stale state")


if __name__ == "__main__":
    main()

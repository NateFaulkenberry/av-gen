#!/usr/bin/env python3
"""Talking Heads: the rays are a direct function of the mouth (the owner: "mouth fully closed = rays off / fully open =
rays fully bright"), and the scan is readable. Runs the engine's sonic trace with every route's final value over a clip
(no GPU), then checks, for every head and every frame -- settle frames included:

    ray intensity == max(0, hinge angle / MOUTH_OPEN) x RAY_BRIGHTNESS x the ray's weight x the rays knob's depth

(the hinge angle is the head's mouth Hinge deformer's amount, ADR-1672)

and reports each head's gate (openings, openings per second, share open, the shortest interval between openings) and
its looks (the bust's scan and the neck's yaw and pitch: ranges, change per second). Exits non-zero on a ray that
leaves its mouth.

    python3 tools/talking_heads/ray_check.py --project <talking-heads.json> [--wav feline] [--stems] [--range 0:90]
"""
import argparse
import csv
import importlib.util
import os
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
AVGEN = os.environ.get("AVGEN") or os.path.join(REPO, "build", "release", "src", "avgen")
sys.path.insert(0, REPO)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--wav", default="feline")
    ap.add_argument("--stems", action="store_true")
    ap.add_argument("--range", default="")
    ap.add_argument("--tol", type=float, default=1e-3)
    a = ap.parse_args()
    rv = load("th_review", os.path.join(HERE, "review.py"))
    import importlib
    th = importlib.import_module("tools.sonic_vfx.scenes.talking_heads")
    work = tempfile.mkdtemp(prefix="th-raycheck-")
    proj = rv.variant(a.project, a.wav, a.stems, work)
    trace = os.path.join(work, "trace.csv")
    r = subprocess.run([AVGEN, "--project", proj, "--sonic-trace", trace, "--trace-finals"], capture_output=True,
                       text=True)
    if r.returncode != 0:
        print(r.stderr[-2000:])
        sys.exit("avgen exited %d" % r.returncode)
    rows = list(csv.DictReader(open(trace)))
    if a.range:
        lo, hi = (float(v) for v in a.range.split(":"))
        rows = [x for x in rows if lo <= float(x["time"]) <= hi]
    t = [float(x["time"]) for x in rows]
    heads = [c[len("signal."):-len("Live")] for c in rows[0] if c.startswith("signal.") and c.endswith("Live")
             and c != "signal.padShutHold"]
    bad = 0
    for P in heads:
        # the mouth's angle: the trace carries no procedural deformer target, so it is the hinge route's own arithmetic
        # on its source -- the pose, cut to exactly 0 below 0.002 (talking_heads.routes: the same source and chain)
        pose = [float(x["signal.%sPose" % P]) for x in rows]
        jaw = [th.MOUTH_OPEN * (v if v > 0.002 else 0.0) for v in pose]
        depth = [min(2.0, 2.0 * float(x["visual.beamsGate"])) for x in rows]    # depthMax 2 over the knob's gate
        worst = 0.0
        for i, w in enumerate(th.RAY_WEIGHT):
            col = "final:fx/%s-mouth%d/intensity[0]" % (P, i)
            if col not in rows[0]:
                continue
            for k, x in enumerate(rows):
                want = max(0.0, jaw[k] / th.MOUTH_OPEN) * th.RAY_BRIGHTNESS * w * depth[k]   # >= 0: cut at rest
                got = float(x[col])
                if abs(pose[k] - 0.002) < 1e-5:   # on the cut itself (the trace rounds): either side is the route's
                    want = got if min(abs(got), abs(got - th.RAY_BRIGHTNESS * w * depth[k] * pose[k])) < a.tol else want
                worst = max(worst, abs(got - want))
                if abs(got - want) > a.tol:
                    bad += 1
        g = [float(x["signal.%sLive" % P]) for x in rows]
        opens = [t[i] for i in range(1, len(g)) if g[i] > 0.5 and g[i - 1] <= 0.5]
        gaps = [q - p for p, q in zip(opens, opens[1:])]
        yaw = [float(x["final:nodes/%s-bust/rotation[1]" % P]) for x in rows]
        per = [abs(yaw[i + 30] - yaw[i]) for i in range(0, len(yaw) - 30, 30)]
        span = t[-1] - t[0] if len(t) > 1 else 1.0
        ny = [float(x["final:nodes/%s-head/rotation[1]" % P]) for x in rows]
        npitch = [-float(x["final:nodes/%s-pivot/rotation[0]" % P]) for x in rows]   # back (up) positive
        nper = [abs(ny[i + 30] - ny[i]) for i in range(0, len(ny) - 30, 30)]
        print("%s: neck yaw %.1f..%.1f deg (change per second median %.1f), pitch %.1f..%.1f deg (back +, the "
              "gate's snap included)" % (P, min(ny), max(ny), statistics.median(nper) if nper else 0.0, min(npitch),
                                          max(npitch)))
        print("%s: rays vs mouth worst %.2g | gate %d openings, %.2f/s, open %.0f%%, shortest interval %.2f s | scan "
              "yaw %.1f..%.1f deg, change per second median %.1f deg, %.0f%% of seconds turning > 5 deg" % (
                  P, worst, len(opens), len(opens) / span, 100.0 * sum(1 for v in g if v > 0.5) / max(1, len(g)),
                  min(gaps) if gaps else 0.0, min(yaw), max(yaw), statistics.median(per) if per else 0.0,
                  100.0 * sum(1 for v in per if v > 5.0) / max(1, len(per))))
    if bad:
        sys.exit("%d frame(s) where a ray left its mouth" % bad)
    print("every ray tracks its mouth on every frame")


if __name__ == "__main__":
    main()

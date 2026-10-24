#!/usr/bin/env python3
"""Sound to light (ADR-1605, The Living Prism P1-3): how long a transient takes to reach the screen, live.

    python3 tools/sound_to_light.py prepare <project.json>            # writes the measured twin beside it
    python3 tools/sound_to_light.py analyse <run-dir>... [--json out.json]   # several runs are pooled

The rig is tools/sound_to_light.sh. `avgen_sonic_probe transients` plays 80 one-shot transients (kick, snare, hat,
pluck in turn, 0.6031 s apart) into BlackHole 2ch on both channels and logs each one's start on the host clock (the
stream's first callback plus the shot's sample index; see "the reference" below). AV Gen runs the project live
(`--live-profile --mode live`: the projection window at 1920x1080, the 30 fps target, the Auto ladder) capturing
BlackHole, channel 2 also analysed as the drum stem (`--input-sources drums@2:drums`), and writes `--sonic-live-log`: one
row per frame with
  frameNs    when the frame's bus was built (the engine update's live clock reading)
  presentNs  when its present call returned (the picture handed to the compositor)
  gpuMs      the newest landed frame's GPU span (gpuEndRaw/gpuCompleted: its raw last timestamp, for the record)
and every response.*, stem.*, signal.*, env.*, beat.*, audio.onset* value, plus with --trace-finals every route target's
final. All of it is mach_absolute_time ns, so it joins the probe directly.

`prepare` writes `<project>.s2l.json` beside the project (so its relative paths resolve) with the impact paths a
scene can author on and Rainbow World does not have: signals on `response.onset` and on `stem.drums.onset` (attack 0,
the recommended impact shape), an envelope source on `response.onset`, and route envelopes (a peak-hold chain on the
route itself) from both onsets to two untouched post parameters. The rig deletes it afterwards.

For every transient and every path, the first frame in [shot, shot + 200 ms] where the path rose by 10% of its range
over the run (an event is 0 on every other frame, so its own frame is the rise):
  bus      that frame's bus time: the value exists
  present  that frame's present call returning
  gpu      that frame's GPU finishing its last pass: present + the GPU span (see estimate_gpu_end)
  light    gpu + the wait for the next display refresh: the estimate adds half a refresh, the upper bound a whole one
A path is also tested 300 ms before each shot, where nothing was played: how often it "rises" there is its false
alarm count (a beat-grid signal rises on the grid, not on the sound).

The reference: the probe's first callback is stamped when it runs, and every later sample is counted from it at
48 kHz. A playback callback runs ahead of its samples' output time by at least its own period (BlackHole adds no
device latency), so the reference is early by about one probe period: the latencies here are high by ~2.7 ms at a
128-frame period. BlackHole also has no input latency; an interface adds its own (3.1 ms on the Volt 876, 02 §6.1).
"""
import argparse
import bisect
import csv
import json
import os
import re
import sys

FAMILIES = ["kick", "snare", "hat", "pluck"]
WINDOW_MS = 200.0
QUIET_OFFSET_MS = -300.0  # a window with no shot in it: the false-alarm test

# The paths reported, in order: (name, column, families it is meant for; None = all). Missing columns are skipped.
PATHS = [
    ("response.onset", "response.onset", None),
    ("response.onsetEnv", "response.onsetEnv", None),
    ("audio.onset", "audio.onset", None),
    ("stem.drums.onset", "stem.drums.onset", None),
    ("signal on response.onset", "signal.s2lOnset", None),
    ("signal on stem.drums.onset", "signal.s2lDrums", None),
    ("env on response.onset", "env.s2lHit", None),
    ("route env (response.onset)", "final:post/grade/contrast[0]", None),
    ("route env (stem.drums.onset)", "final:post/tonemap/chroma-retention[0]", None),
    # (ADR-1620: response.kick fires only from a drum source the feeds name; the mix has none)
    ("response.snare", "response.snare", ["snare"]),
    ("response.hat", "response.hat", ["hat"]),
    ("stem.drums.kick", "stem.drums.kick", ["kick"]),
    ("stem.drums.snare", "stem.drums.snare", ["snare"]),
    ("stem.drums.hat", "stem.drums.hat", ["hat"]),
    # Rainbow World's own impact signals (ADR-1602) and its clap route's final
    ("RW signal.clap", "signal.clap", ["snare"]),
    ("RW signal.glint", "signal.glint", ["hat"]),
    ("RW clap route final", "final:material/rw_rainbow/op/9/constant/constant[0]", ["snare"]),
    ("RW signal.kick (beat grid)", "signal.kick", ["kick"]),
    # Aurora Borealis's accents (02-research-signals.md s3.3) and the kick pulse's route final (ADR-1652)
    ("AB signal.kickWave", "signal.kickWave", ["kick"]),
    ("AB signal.snareFold", "signal.snareFold", ["snare"]),
    ("AB kick pulse route final", "final:fx/aurora/pulse[0]", ["kick"]),
]


def prepare(project):
    with open(project) as f:
        p = json.load(f)
    p.setdefault("sources", [])
    p.setdefault("routes", [])
    impact = {"attackMs": 0.0, "decayMs": 250.0}
    p["sources"] += [
        {"kind": "signal", "name": "s2lOnset",
         "settings": {"combine": "firstActive", "inputs": [{"source": "response.onset", "chain": impact}]}},
        {"kind": "signal", "name": "s2lDrums",
         "settings": {"combine": "firstActive", "inputs": [{"source": "stem.drums.onset", "chain": impact}]}},
        {"kind": "envelope", "name": "s2lHit", "settings": {"trigger": "response.onset"}},
    ]
    route_env = {"attackMs": 0.0, "decayMs": 0.0, "envelope": "peakhold", "envelopeHoldMs": 40.0,
                 "envelopeFallPerSecond": 5.0}
    p["routes"] += [
        {"source": "response.onset", "target": "post/grade/contrast", "amount": 0.02, "op": "add",
         "polarity": "unipolar", "chain": route_env},
        {"source": "stem.drums.onset", "target": "post/tonemap/chroma-retention", "amount": 0.01, "op": "add",
         "polarity": "unipolar", "chain": route_env},
    ]
    out = os.path.splitext(project)[0] + ".s2l.json"
    with open(out, "w") as f:
        json.dump(p, f, indent=1)
    print(out)


def pct(xs, q):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return None
    k = (len(xs) - 1) * q
    lo = int(k)
    hi = min(lo + 1, len(xs) - 1)
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def load_frames(path):
    with open(path) as f:
        rows = list(csv.DictReader(f))
    frames = []
    for r in rows:
        try:
            frames.append({"frameNs": int(r["frameNs"]), "presentNs": int(r["presentNs"]),
                           "vsyncNs": int(r.get("vsyncNs") or 0), "vsyncPeriodNs": int(r.get("vsyncPeriodNs") or 0),
                           "gpuEndRaw": int(r.get("gpuEndRaw") or 0), "gpuMs": num(r.get("gpuMs")),
                           "snapMs": num(r.get("snapshotToFrameMs")), "row": r})
        except (KeyError, ValueError, TypeError):
            continue
    return frames


def estimate_gpu_end(frames):
    """Gives each frame an estimate of when its GPU work finished: its present call returning + the GPU span.

    The frame timeline's own timestamps (gpuEndRaw) tick at the host clock's rate (within 10 ppm) but from another
    epoch (about 5.5 s apart on the M2 Max, Dawn/Metal), so they cannot be joined to the host clock absolutely. The
    estimate holds when the GPU is idle at submit, which a paced 30 fps loop whose GPU span is under the period
    gives: the queue is empty when the frame is submitted, just before its present call. The span is the newest
    landed frame's (the readback lags a frame or two), so the estimate is statistical, not per frame."""
    last = None
    for f in frames:
        if f["gpuMs"] is not None and f["gpuMs"] > 0:
            last = f["gpuMs"]
        if last is not None:
            f["gpuEndNs"] = f["presentNs"] + int(last * 1e6)
            # ADR-1609: on the display's own vsync grid (the log carries its latest vsync and period), the light is the
            # first vsync at or after the GPU's end -- measured phase, not an assumed half refresh.
            if f["vsyncNs"] > 0 and f["vsyncPeriodNs"] > 0:
                k = -(-(f["gpuEndNs"] - f["vsyncNs"]) // f["vsyncPeriodNs"])  # ceil
                f["lightNs"] = f["vsyncNs"] + k * f["vsyncPeriodNs"]


def epoch_offset_ms(frames):
    """How far the frame timeline's epoch is from the host clock's (median of frameNs - gpuEndRaw), for the record."""
    d = [(f["frameNs"] - f["gpuEndRaw"]) / 1e6 for f in frames if f["gpuEndRaw"] > 0]
    return pct(d, 0.5)


def summarise(bus, present, gpu, refresh_ms, light=None):
    s = {"n": len(present), "busP50": pct(bus, 0.5), "busP95": pct(bus, 0.95),
         "presentP50": pct(present, 0.5), "presentP95": pct(present, 0.95)}
    if gpu:
        s.update({"gpuN": len(gpu), "gpuP50": pct(gpu, 0.5), "gpuP95": pct(gpu, 0.95),
                  # the next refresh after the GPU finishes: uniform in [0, refresh) -- half on average, at most one
                  "lightEstP50": pct([x + refresh_ms / 2 for x in gpu], 0.5),
                  "lightEstP95": pct([x + refresh_ms / 2 for x in gpu], 0.95),
                  "lightUpperP50": pct([x + refresh_ms for x in gpu], 0.5),
                  "lightUpperP95": pct([x + refresh_ms for x in gpu], 0.95)})
    if light:
        s.update({"lightN": len(light), "lightP50": pct(light, 0.5), "lightP95": pct(light, 0.95)})
    return s


def probe_period(run_dir):
    """The probe's output period in frames, from its log ("(512-frame period)"); 0 when it cannot be read."""
    try:
        text = open(os.path.join(run_dir, "probe.log")).read()
    except OSError:
        return 0
    m = re.search(r"\((\d+)-frame period\)", text)
    return int(m.group(1)) if m else 0


def collect(run_dir, refresh_ms, lists):
    """Adds one run's latencies to `lists` (path -> family -> metric -> values). Returns the run's description."""
    shots = []
    with open(os.path.join(run_dir, "probe.csv")) as f:
        for r in csv.DictReader(f):
            if r["kind"] == "shot":
                shots.append((int(r["hostNs"]), FAMILIES[int(r["key"])]))
    frames = load_frames(os.path.join(run_dir, "live.csv"))
    if not frames or not shots:
        sys.exit("%s: no frames or no shots" % run_dir)
    estimate_gpu_end(frames)
    # The reference correction (module docstring): the probe's callback runs one output period ahead of its samples.
    period = probe_period(run_dir)
    correction_ns = int(period / 48000.0 * 1e9)
    intervals = [(frames[i]["frameNs"] - frames[i - 1]["frameNs"]) / 1e6 for i in range(1, len(frames))]
    t0, t1 = frames[0]["frameNs"], frames[-1]["frameNs"]
    played = len(shots)
    shots = [s for s in shots if t0 + 1e9 < s[0] < t1 - 0.5e9]  # the shots the log covers, with a quiet window
    values = {c: [num(f["row"].get(c)) for f in frames] for c in frames[0]["row"].keys()}
    frame_ns = [f["frameNs"] for f in frames]

    def first_rise(v, t_ns, thr):
        i = bisect.bisect_left(frame_ns, t_ns)
        while i < len(frames) and (frame_ns[i] - t_ns) / 1e6 <= WINDOW_MS:
            if i > 0 and v[i] is not None and v[i - 1] is not None and v[i] - v[i - 1] > thr:
                return i
            i += 1
        return None

    for name, col, fams in PATHS:
        if col not in values:
            continue
        v = values[col]
        rng = (pct(v, 0.995) or 0.0) - (pct(v, 0.005) or 0.0)
        entry = lists.setdefault(name, {"column": col, "tested": 0, "missed": 0, "falseAlarms": 0, "fam": {}})
        if rng <= 0:
            entry["missed"] += sum(1 for _, f_ in shots if fams is None or f_ in fams)
            entry["tested"] += sum(1 for _, f_ in shots if fams is None or f_ in fams)
            continue
        thr = 0.1 * rng
        for ns, fam in shots:
            if fams is not None and fam not in fams:
                continue
            entry["tested"] += 1
            if first_rise(v, ns + int(QUIET_OFFSET_MS * 1e6), thr) is not None:
                entry["falseAlarms"] += 1
            i = first_rise(v, ns, thr)
            if i is None:
                entry["missed"] += 1
                continue
            fr = frames[i]
            sound = ns + correction_ns
            m = entry["fam"].setdefault(fam, {k: [] for k in ("bus", "present", "gpu", "updateToPresent", "gpuSpan", "light", "vsyncWait")})
            m["bus"].append((fr["frameNs"] - sound) / 1e6)
            m["present"].append((fr["presentNs"] - sound) / 1e6)
            m["updateToPresent"].append((fr["presentNs"] - fr["frameNs"]) / 1e6)
            if "gpuEndNs" in fr:
                m["gpu"].append((fr["gpuEndNs"] - sound) / 1e6)
                m["gpuSpan"].append((fr["gpuEndNs"] - fr["presentNs"]) / 1e6)
            if "lightNs" in fr:
                m["light"].append((fr["lightNs"] - sound) / 1e6)
                m["vsyncWait"].append((fr["lightNs"] - fr["gpuEndNs"]) / 1e6)
    return {"dir": run_dir, "probePeriodFrames": period, "referenceCorrectionMs": correction_ns / 1e6,
            "shotsPlayed": played, "shots": len(shots), "frames": len(frames),
            "frameIntervalP50": pct(intervals, 0.5), "frameIntervalP99": pct(intervals, 0.99),
            "gpuMsP50": pct([f["gpuMs"] for f in frames if f["gpuMs"] and f["gpuMs"] > 0], 0.5),
            "timelineEpochOffsetMs": epoch_offset_ms(frames)}


def analyse(run_dirs, json_out, refresh_hz):
    refresh_ms = 1000.0 / refresh_hz
    lists = {}
    runs = [collect(d, refresh_ms, lists) for d in run_dirs]
    results = {}
    for name, e in lists.items():
        per_family = {}
        pooled = {k: [] for k in ("bus", "present", "gpu", "updateToPresent", "gpuSpan", "light", "vsyncWait")}
        for fam in FAMILIES:
            if fam not in e["fam"]:
                continue
            m = e["fam"][fam]
            per_family[fam] = summarise(m["bus"], m["present"], m["gpu"], refresh_ms, m["light"])
            for k in pooled:
                pooled[k] += m[k]
        results[name] = {"column": e["column"], "shots": e["tested"], "missed": e["missed"],
                         "falseAlarms": e["falseAlarms"], **summarise(pooled["bus"], pooled["present"], pooled["gpu"],
                                                                      refresh_ms, pooled["light"]),
                         "families": per_family,
                         "components": {k: {"p50": pct(pooled[k], 0.5), "p95": pct(pooled[k], 0.95)}
                                        for k in ("updateToPresent", "gpuSpan", "vsyncWait") if pooled[k]}}
    out = {"runs": runs, "refreshMs": refresh_ms, "paths": results}
    print_table(out)
    if json_out:
        with open(json_out, "w") as f:
            json.dump(out, f, indent=1)
    return out


def f1(x):
    return "    -" if x is None else "%5.1f" % x


def print_table(out):
    for r in out["runs"]:
        print("%s: %d of %d transients in the log, %d frames, interval p50 %.1f p99 %.1f ms, GPU p50 %s ms; probe "
              "period %d (reference corrected by %.1f ms)" % (
                  os.path.basename(r["dir"].rstrip("/")), r["shots"], r["shotsPlayed"], r["frames"],
                  r["frameIntervalP50"], r["frameIntervalP99"], f1(r["gpuMsP50"]).strip(), r["probePeriodFrames"],
                  r["referenceCorrectionMs"]))
    print("%-30s %3s %4s %3s | %5s %5s | %5s %5s | %5s %5s | %5s %5s" % (
        "path", "n", "miss", "fa", "bus50", "bus95", "prs50", "prs95", "gpu50", "gpu95", "lt50", "lt95"))
    for name, r in out["paths"].items():
        if not r.get("n"):
            print("%-30s %3d %4s     never rose" % (name, 0, r.get("missed", "-")))
            continue
        print("%-30s %3d %4d %3d | %s %s | %s %s | %s %s | %s %s" % (
            name, r["n"], r["missed"], r["falseAlarms"], f1(r["busP50"]), f1(r["busP95"]), f1(r["presentP50"]),
            f1(r["presentP95"]), f1(r.get("gpuP50")), f1(r.get("gpuP95")),
            f1(r.get("lightP50", r.get("lightEstP50"))), f1(r.get("lightP95", r.get("lightEstP95")))))
    print("ms from the sound. fa = rises 300 ms before a shot (nothing played). gpu = present + the GPU span; "
          "lt = the first vsync after the GPU end, on the display's vsync grid when the log has it (ADR-1609), else "
          "gpu + half a refresh.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("prepare")
    p.add_argument("project")
    a_ = sub.add_parser("analyse")
    a_.add_argument("run_dirs", nargs="+", help="one or more run directories, pooled")
    a_.add_argument("--json")
    a_.add_argument("--refresh-hz", type=float, default=60.0)
    a = ap.parse_args()
    if a.cmd == "prepare":
        prepare(a.project)
    else:
        analyse(a.run_dirs, a.json, a.refresh_hz)


if __name__ == "__main__":
    main()

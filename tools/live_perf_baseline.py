#!/usr/bin/env python3
"""The live performance baseline and its regression check (ADR-1608, The Living Prism P1-7).

    python3 tools/live_perf_baseline.py run   --out <dir> [--reps 3] [--bin build/release] [--scenes a,b]
                                              [--extra "<avgen args>"]
    python3 tools/live_perf_baseline.py check --out <dir> [--baseline docs/perf/live-baseline.json] [--scenes a,b]
    python3 tools/live_perf_baseline.py write --out <dir> [--baseline docs/perf/live-baseline.json]

`run` measures the reference live scenes the way the Living Prism audit measured them (01 §4.1, §9): 1920x1080 at the
live target of 30 fps, Feline Footwear driving the analysis muted, from 60 s (the audit's representative window); per
scene `--reps` interleaved repetitions of a live Auto run (`--warmup 5 --measure 30`: misses, the frame interval, the
levels held) and one headless Ultra run (`--warmup-frames 150 --measure 15`: where the GPU time goes). Every run holds
the GPU lock (tools/gpu-lock.sh) and logs the power source before and after: a profile is evidence only on AC power.
A scene whose project is not in this tree (or whose generated scene is missing) is skipped and said so. `--extra`
appends arguments to every avgen run, for an injected regression (e.g. "--quality ultra": the ladder pinned).

`check` judges a run directory (exit 0 pass, 1 fail, 2 nothing to judge):
  absolute thresholds (03 §11, as the owner amended them in §14):
    T1  live deadline misses per 900 frames, median of the reps         <= 5
    T2  live frame interval p99, median of the reps                      <= 40 ms
    T3  share of measured live frames at Medium or better, median        >= 90%
    T4  headless Ultra GPU p50 <= 29.3 ms and p95 <= 1.5 x p50
  regression against the committed baseline (T9: "worse misses or p99 beyond the reps' spread fails review"):
    misses (median) above the baseline's worst repetition + 2, or p99 (median) above its worst repetition + 2 ms.
    The margins exist because three repetitions' worst is not the tail: the audit's reps spread 1-6 misses.
A threshold the baseline already failed (THE RIFT's T4: it relies on the ladder) is reported, not failed on; one the
baseline met and this run does not ("LOST") fails. A baseline scene missing from the run fails (a crash must not pass),
unless `--scenes` narrows the check. A run that exited non-zero fails.
`write` makes a run the baseline (numbers only: no asset, no image, no audio).
"""
import argparse
import csv
import json
import os
import shlex
import statistics
import subprocess
import sys
import time
from datetime import datetime

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
# The audit's reference scenes minus Digital Mosh Live (shelved by the owner; its misses are a separate ticket, 03
# §14 #7). Each: (project, a file its generated scene needs).
SCENES = {
    "rainbow-world": ("examples/rainbow-world/rainbow-world.json", "examples/rainbow-world/scene"),
    "rift-live": ("examples/bioluminescent/rift-live.json", "examples/bioluminescent/rift.scene.json"),
    "event-horizon": ("examples/sonic-vfx/event-horizon.json", "examples/sonic-vfx/event-horizon.scene.json"),
    "aurora-borealis": ("examples/aurora-borealis/aurora-borealis.json",
                        "examples/aurora-borealis/scene/aurora-borealis.scene.json"),
}
AUDIO = "assets/audio/feline-footwear.wav"
COMMON = ["--audio", AUDIO, "--no-audio", "--target-fps", "30", "--size", "1920x1080", "--start", "60"]
LEVELS = ["Ultra", "High", "Medium", "Low", "Emergency"]
DEFAULT_BASELINE = os.path.join(ROOT, "docs", "perf", "live-baseline.json")
MISS_MARGIN = 2.0
P99_MARGIN_MS = 2.0


def power():
    try:
        return subprocess.run(["pmset", "-g", "batt"], capture_output=True, text=True).stdout.splitlines()[0]
    except (OSError, IndexError):
        return "unknown"


def available(scene):
    project, needs = SCENES[scene]
    if not os.path.exists(os.path.join(ROOT, project)):
        return "its project is not in this tree"
    if needs and not os.path.exists(os.path.join(ROOT, needs)):
        return "its generated scene is missing (%s)" % needs
    return None


def chosen(a):
    names = list(SCENES) if not a.scenes else a.scenes.split(",")
    unknown = [n for n in names if n not in SCENES]
    if unknown:
        sys.exit("unknown scene(s): %s (known: %s)" % (", ".join(unknown), ", ".join(SCENES)))
    return names


def run(a):
    os.makedirs(a.out, exist_ok=True)
    log = open(os.path.join(a.out, "conditions.log"), "a")

    def say(line):
        print(line, flush=True)
        log.write(line + "\n")
        log.flush()

    if not os.path.exists(os.path.join(ROOT, AUDIO)):
        sys.exit("%s is missing (the analysis track; never committed)" % AUDIO)
    scenes = []
    for scene in chosen(a):
        why = available(scene)
        if why:
            say("-- %s skipped: %s" % (scene, why))
        else:
            scenes.append(scene)
    avgen = os.path.join(a.bin, "src", "avgen")
    plan = [(scene, "live", rep) for rep in range(1, a.reps + 1) for scene in scenes]  # interleaved
    if not a.live_only:
        plan += [(scene, "headless", 1) for scene in scenes]
    extra = shlex.split(a.extra) if a.extra else []
    if extra:
        say("-- extra arguments on every run: %s" % a.extra)
    exits = {}
    for index, (scene, mode, rep) in enumerate(plan):
        if index > 0 and a.gap > 0:
            time.sleep(a.gap)  # leave the GPU lock to the other agents between runs
        rep += a.rep_offset
        name = "%s-%s-r%d" % (scene, mode, rep)
        args = ["--live-profile", "--mode", mode, "--project", SCENES[scene][0]] + COMMON
        args += ["--quality", "auto", "--warmup", "5", "--measure", "30"] if mode == "live" else \
                ["--quality", "ultra", "--warmup-frames", "150", "--measure", "15"]
        args += ["--json", os.path.join(a.out, name + ".json"), "--trace", os.path.join(a.out, name + ".csv")]
        args += extra
        say("== %s %s %s" % (name, datetime.now().strftime("%H:%M:%S"), power()))
        env = dict(os.environ, AVGEN_GPU_LOCK_TIMEOUT=os.environ.get("AVGEN_GPU_LOCK_TIMEOUT", "14400"))
        with open(os.path.join(a.out, name + ".log"), "w") as out:
            rc = subprocess.run([os.path.join(ROOT, "tools", "gpu-lock.sh"), avgen] + args, cwd=ROOT, env=env,
                                stdout=out, stderr=subprocess.STDOUT).returncode
        exits[name] = rc
        say("   exit %d %s %s" % (rc, datetime.now().strftime("%H:%M:%S"), power()))
    with open(os.path.join(a.out, "exits.json"), "w") as f:
        json.dump(exits, f, indent=1)
    return 0 if all(rc == 0 for rc in exits.values()) else 1


def levels_share(trace):
    """Share of measured live frames at each level, from the live trace's `level` column (ADR-1362)."""
    counts = {}
    total = 0
    with open(trace) as f:
        for r in csv.DictReader(f):
            if r.get("phase") != "m":
                continue
            counts[r["level"]] = counts.get(r["level"], 0) + 1
            total += 1
    return {k: v / total for k, v in counts.items()} if total else {}


def summarise(out):
    s = {}
    exits = {}
    if os.path.exists(os.path.join(out, "exits.json")):
        exits = json.load(open(os.path.join(out, "exits.json")))
    for scene in SCENES:
        live = []
        rep = 1
        while os.path.exists(os.path.join(out, "%s-live-r%d.json" % (scene, rep))):
            r = json.load(open(os.path.join(out, "%s-live-r%d.json" % (scene, rep))))
            b = r["frame"]["budget"]
            frames = max(1, b["frames"])
            share = levels_share(os.path.join(out, "%s-live-r%d.csv" % (scene, rep)))
            live.append({"frames": b["frames"], "deadlineMisses": b.get("deadlineMisses"),
                         "missesPer900": (b.get("deadlineMisses") or 0) * 900.0 / frames,
                         "p50": r["frame"]["frameMs"]["p50"], "p99": r["frame"]["frameMs"]["p99"],
                         "max": r["frame"]["frameMs"]["max"],
                         "mediumOrBetter": sum(share.get(lv, 0.0) for lv in LEVELS[:3]),
                         "levels": {k: round(v, 4) for k, v in sorted(share.items())},
                         "revision": r.get("conditions", {}).get("gitRevision")})
            rep += 1
        head = None
        hp = os.path.join(out, "%s-headless-r1.json" % scene)
        if os.path.exists(hp):
            r = json.load(open(hp))
            head = {"gpuP50": r["frame"]["gpuMs"]["p50"], "gpuP95": r["frame"]["gpuMs"]["p95"],
                    "cpuWorkP50": r["frame"].get("cpuWorkMs", {}).get("p50") if isinstance(
                        r["frame"].get("cpuWorkMs"), dict) else None}
        failed_runs = sorted(k for k, rc in exits.items() if k.startswith(scene + "-") and rc != 0)
        if not live and not failed_runs:
            continue
        s[scene] = {
            "project": SCENES[scene][0],
            "reps": live,
            "failedRuns": failed_runs,
            "missesMedian": statistics.median(x["missesPer900"] for x in live) if live else None,
            "missesWorst": max(x["missesPer900"] for x in live) if live else None,
            "p99Median": statistics.median(x["p99"] for x in live) if live else None,
            "p99Worst": max(x["p99"] for x in live) if live else None,
            "mediumOrBetter": statistics.median(x["mediumOrBetter"] for x in live) if live else None,
            "headless": head,
        }
    return s


def thresholds(sc):
    if sc["missesMedian"] is None:
        return {"T1": False, "T2": False, "T3": False, "T4": False}
    t = {"T1": sc["missesMedian"] <= 5.0, "T2": sc["p99Median"] <= 40.0, "T3": sc["mediumOrBetter"] >= 0.90}
    h = sc.get("headless")
    t["T4"] = bool(h) and h["gpuP50"] <= 29.3 and h["gpuP95"] <= 1.5 * h["gpuP50"]
    return t


def check(a):
    now = summarise(a.out)
    if not now:
        print("nothing to judge in", a.out)
        return 2
    base = json.load(open(a.baseline)) if os.path.exists(a.baseline) else {"scenes": {}}
    if not base["scenes"]:
        print("no baseline at %s: thresholds only" % a.baseline)
    wanted = a.scenes.split(",") if a.scenes else None
    failed = False
    print("%-15s %7s %7s %6s %6s %6s  %-23s %s" % ("scene", "misses", "(base)", "p99", "(base)", "Med+",
                                                 "T1 T2 T3 T4", "T9 regression"))
    for scene in (wanted or sorted(set(now) | set(base["scenes"]))):
        sc = now.get(scene)
        b = base["scenes"].get(scene)
        if sc is None:
            print("%-15s not measured in this run%s" % (scene, "" if b is None else " (it is in the baseline)"))
            failed = failed or b is not None or wanted is not None
            continue
        t = thresholds(sc)
        bt = b.get("thresholds", {}) if b else {}
        reg = []
        if sc["failedRuns"]:
            reg.append("runs exited non-zero: " + ", ".join(sc["failedRuns"]))
        if b and sc["missesMedian"] is not None:
            if sc["missesMedian"] > b["missesWorst"] + MISS_MARGIN:
                reg.append("misses %.1f > %.1f + %.0f" % (sc["missesMedian"], b["missesWorst"], MISS_MARGIN))
            if sc["p99Median"] > b["p99Worst"] + P99_MARGIN_MS:
                reg.append("p99 %.1f > %.1f + %.0f ms" % (sc["p99Median"], b["p99Worst"], P99_MARGIN_MS))
        lost = [k for k, ok in t.items() if not ok and bt.get(k, False)]
        marks = " ".join(("ok" if ok else ("LOST" if k in lost else "no")) for k, ok in t.items())
        fmt = lambda x, f="%.1f": "-" if x is None else f % x
        print("%-15s %7s %7s %6s %6s %5s%%  %-23s %s" % (
            scene, fmt(sc["missesMedian"]), fmt(b["missesMedian"]) if b else "-", fmt(sc["p99Median"]),
            fmt(b["p99Median"]) if b else "-", fmt(None if sc["mediumOrBetter"] is None else
                                                  100 * sc["mediumOrBetter"], "%.0f"),
            marks, "; ".join(reg) or "none"))
        failed = failed or bool(reg) or bool(lost)
    print("RESULT:", "FAIL" if failed else "PASS")
    return 1 if failed else 0


def write(a):
    now = summarise(a.out)
    if not now:
        sys.exit("nothing to write from " + a.out)
    bad = [s for s, sc in now.items() if sc["failedRuns"] or not sc["reps"]]
    if bad:
        sys.exit("refusing to write a baseline with failed runs: " + ", ".join(bad))
    for sc in now.values():
        sc["thresholds"] = thresholds(sc)
    rev = subprocess.run(["git", "describe", "--always", "--dirty", "--abbrev=8"], cwd=ROOT, capture_output=True,
                         text=True).stdout.strip()
    os.makedirs(os.path.dirname(a.baseline), exist_ok=True)
    doc = {"format": "avgen-live-baseline", "version": 1,
           "machine": "Apple M2 Max, macOS, 60 Hz display (the owner's)",
           "revision": rev,
           "conditions": "Release; 1920x1080; 30 fps target; Feline Footwear muted (--no-audio) from 60 s; live Auto "
                         "--warmup 5 --measure 30 x %d interleaved reps; headless Ultra --warmup-frames 150 --measure "
                         "15; AC power; tools/gpu-lock.sh" % max(len(sc["reps"]) for sc in now.values()),
           "thresholds": {"T1": "misses/900 median <= 5", "T2": "p99 median <= 40 ms",
                          "T3": "Medium or better >= 90% (owner, 03 §14)", "T4": "headless Ultra GPU p50 <= 29.3, "
                          "p95 <= 1.5 x p50", "T9": "misses > worst rep + %.0f or p99 > worst rep + %.0f ms fails" % (
                              MISS_MARGIN, P99_MARGIN_MS)},
           "written": datetime.now().strftime("%Y-%m-%d"), "scenes": now}
    with open(a.baseline, "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    print("wrote", a.baseline)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["run", "check", "write"])
    ap.add_argument("--out", required=True)
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--bin", default=os.path.join(ROOT, "build", "release"))
    ap.add_argument("--baseline", default=DEFAULT_BASELINE)
    ap.add_argument("--scenes", help="comma-separated subset of: " + ", ".join(SCENES))
    ap.add_argument("--extra", help="arguments appended to every avgen run (an injected regression, an A/B arm)")
    ap.add_argument("--gap", type=float, default=0.0, help="seconds between runs, so other GPU users get the lock")
    ap.add_argument("--live-only", action="store_true", help="skip the headless Ultra runs (T4 is then unjudged)")
    ap.add_argument("--rep-offset", type=int, default=0, help="number this run's reps from offset+1 (to interleave "
                    "two arms rep by rep into one directory each)")
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)
    a.bin = os.path.abspath(a.bin)
    if a.command == "run":
        return run(a)
    if a.command == "write":
        return write(a)
    return check(a)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""The live performance baseline and its regression check (ADR-1608, The Living Prism P1-7).

    python3 tools/live_perf_baseline.py run   --out <dir> [--reps 3] [--bin build/release] [--scenes a,b]
                                              [--extra "<avgen args>"]
    python3 tools/live_perf_baseline.py check --out <dir> [--baseline docs/perf/live-baseline.json] [--scenes a,b]
    python3 tools/live_perf_baseline.py write --out <dir> [--baseline docs/perf/live-baseline.json] [--add]

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

ADR-1808, the gate knows its conditions. Live deadline misses depend on the display's refresh rate (a 120 Hz display
paces presents differently from the 60 Hz one the baseline was measured on) and on what else the machine is doing. So
`run` samples the 1-minute load average every 2 s while each avgen run holds the GPU, into `<run>.load.json`, and
every live rep carries the `displayRefreshHz` its profile recorded. `check` compares them with the baseline's:
  - a live rep at a refresh rate other than the baseline's, or
  - a median load during the live reps above `--max-load` (default 4.0; the baseline's own median + 2 if higher)
make the verdict "CONDITIONS DIFFER" (exit 3) instead of PASS or FAIL: the numbers are printed, but they are not a
verdict on the code. A run that exited non-zero still FAILs (a crash is not a condition). `--ignore-conditions` judges
anyway. A baseline with no recorded load is judged against `--max-load` alone.
`write` makes a run the baseline (numbers and conditions only: no asset, no image, no audio), and refuses a run whose
own conditions were not quiet (median load above `--max-load`) or mixed refresh rates.
"""
import argparse
import csv
import json
import os
import shlex
import statistics
import subprocess
import sys
import threading
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
    "talking-heads": ("examples/sonic-abstract/talking-heads.json", "examples/sonic-abstract/talking-heads.scene.json"),
}
# Scenes that play their own song (Aurora Borealis pass 3: Sabertooth, 70 s, with its stems) instead of Feline from
# 60 s: their analysis track and start. Sabertooth from 12 s puts the 5 s warm-up and the 30 s measured window inside
# the song, over its swells, crashes, tom fills and the busiest beam passages.
SCENE_AUDIO = {
    "aurora-borealis": ("assets/audio/stems/sabertooth/Sabertooth.wav", "12"),
}
# The command that writes a scene's generated (gitignored) files, for a skipped scene's message.
GENERATORS = {
    "aurora-borealis": "python3 examples/aurora-borealis/build.py",
    "rainbow-world": "python3 examples/rainbow-world/build.py (Blender 5, about 20 minutes)",
}
AUDIO = "assets/audio/feline-footwear.wav"
COMMON = ["--audio", AUDIO, "--no-audio", "--target-fps", "30", "--size", "1920x1080", "--start", "60"]


def common(scene):
    """COMMON, with the scene's own song and start where it has one (SCENE_AUDIO)."""
    if scene not in SCENE_AUDIO:
        return list(COMMON)
    audio, start = SCENE_AUDIO[scene]
    return ["--audio", audio, "--no-audio", "--target-fps", "30", "--size", "1920x1080", "--start", start]
LEVELS = ["Ultra", "High", "Medium", "Low", "Emergency"]
DEFAULT_BASELINE = os.path.join(ROOT, "docs", "perf", "live-baseline.json")
MISS_MARGIN = 2.0
P99_MARGIN_MS = 2.0
# ADR-1808: the median 1-minute load average above which a live run is not on a quiet machine. The M2 Max has 12 cores
# and avgen itself contributes about 1-2; the audit's shared-machine runs sat at 3-23.
MAX_LOAD = 4.0
LOAD_SAMPLE_S = 2.0


class LoadSampler:
    """Samples the 1-minute load average while a run holds the GPU (ADR-1808)."""

    def __init__(self):
        self.samples = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)

    def _loop(self):
        while True:
            try:
                self.samples.append(round(os.getloadavg()[0], 2))
            except OSError:
                pass
            if self._stop.wait(LOAD_SAMPLE_S):
                return

    def __enter__(self):
        self._thread.start()
        return self

    def __exit__(self, *exc):
        self._stop.set()
        self._thread.join()

    def summary(self):
        if not self.samples:
            return {"samples": [], "median": None, "max": None}
        return {"samples": self.samples, "median": statistics.median(self.samples), "max": max(self.samples)}


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
    if scene in SCENE_AUDIO and not os.path.exists(os.path.join(ROOT, SCENE_AUDIO[scene][0])):
        return "its song is missing (%s; never committed)" % SCENE_AUDIO[scene][0]
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
    skipped = []
    for scene in chosen(a):
        why = available(scene)
        if why:
            say("-- %s SKIPPED: %s" % (scene, why))
            skipped.append(scene)
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
        args = ["--live-profile", "--mode", mode, "--project", SCENES[scene][0]] + common(scene)
        args += ["--quality", "auto", "--warmup", "5", "--measure", "30"] if mode == "live" else \
                ["--quality", "ultra", "--warmup-frames", "150", "--measure", "15"]
        args += ["--json", os.path.join(a.out, name + ".json"), "--trace", os.path.join(a.out, name + ".csv")]
        args += extra
        say("== %s %s %s" % (name, datetime.now().strftime("%H:%M:%S"), power()))
        env = dict(os.environ, AVGEN_GPU_LOCK_TIMEOUT=os.environ.get("AVGEN_GPU_LOCK_TIMEOUT", "14400"))
        with open(os.path.join(a.out, name + ".log"), "w") as out:
            # ADR-1808: the load while the run holds the GPU. gpu-lock.sh waits for the lock first, so the samples
            # taken while waiting are included; the run itself dominates them because the wait is usually short and a
            # long wait is itself a sign the machine was busy.
            with LoadSampler() as load:
                rc = subprocess.run([os.path.join(ROOT, "tools", "gpu-lock.sh"), avgen] + args, cwd=ROOT, env=env,
                                    stdout=out, stderr=subprocess.STDOUT).returncode
        with open(os.path.join(a.out, name + ".load.json"), "w") as f:
            json.dump(load.summary(), f)
        exits[name] = rc
        ls = load.summary()
        say("   exit %d %s %s; load median %s max %s" % (rc, datetime.now().strftime("%H:%M:%S"), power(),
                                                         ls["median"], ls["max"]))
    with open(os.path.join(a.out, "exits.json"), "w") as f:
        json.dump(exits, f, indent=1)
    if skipped:
        # A run that measured less than it was asked to is not a pass (Aurora Borealis's generated scene is absent from
        # a fresh checkout, and the old summary line was easy to miss): say how to make each one, and fail.
        say("FAILED: %d scene(s) not measured: %s" % (len(skipped), ", ".join(skipped)))
        for scene in skipped:
            if scene in GENERATORS:
                say("   make %s with: %s" % (scene, GENERATORS[scene]))
        return 1
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
            lp = os.path.join(out, "%s-live-r%d.load.json" % (scene, rep))
            load = json.load(open(lp)) if os.path.exists(lp) else {}
            live.append({"frames": b["frames"], "deadlineMisses": b.get("deadlineMisses"),
                         "missesPer900": (b.get("deadlineMisses") or 0) * 900.0 / frames,
                         "p50": r["frame"]["frameMs"]["p50"], "p99": r["frame"]["frameMs"]["p99"],
                         "max": r["frame"]["frameMs"]["max"],
                         "mediumOrBetter": sum(share.get(lv, 0.0) for lv in LEVELS[:3]),
                         "levels": {k: round(v, 4) for k, v in sorted(share.items())},
                         "revision": r.get("conditions", {}).get("gitRevision"),
                         "environment": r.get("conditions", {}).get("environment", ""),
                         # ADR-1808: the conditions the misses depend on
                         "displayRefreshHz": r.get("conditions", {}).get("displayRefreshHz"),
                         "loadMedian": load.get("median"), "loadMax": load.get("max")})
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
    # None: not judged. A --live-only run has no headless Ultra run, and T4 is that run's threshold; reporting it as
    # lost made every live-only check FAIL whatever the live numbers said.
    t["T4"] = None if not h else (h["gpuP50"] <= 29.3 and h["gpuP95"] <= 1.5 * h["gpuP50"])
    return t


def run_conditions(scenes):
    """The refresh rates and the load of a summary's live reps (ADR-1808)."""
    reps = [r for sc in scenes.values() for r in sc.get("reps", [])]
    rates = sorted({r.get("displayRefreshHz") for r in reps if r.get("displayRefreshHz")})
    loads = [r["loadMedian"] for r in reps if r.get("loadMedian") is not None]
    peaks = [r["loadMax"] for r in reps if r.get("loadMax") is not None]
    return {"displayRefreshHz": rates, "loadMedian": statistics.median(loads) if loads else None,
            "loadMax": max(peaks) if peaks else None}


def baseline_conditions(base):
    """The baseline's conditions: from its reps, or from the document's own record for a baseline written before
    ADR-1808 recorded them per rep."""
    c = run_conditions(base.get("scenes", {}))
    recorded = base.get("gateConditions", {})
    if not c["displayRefreshHz"] and recorded.get("displayRefreshHz"):
        c["displayRefreshHz"] = [recorded["displayRefreshHz"]]
    if c["loadMedian"] is None and recorded.get("loadMedian") is not None:
        c["loadMedian"] = recorded["loadMedian"]
    return c


def conditions_differ(now, base, max_load):
    """Why a run's conditions do not match the baseline's, as a list of reasons (empty: they match)."""
    why = []
    if len(now["displayRefreshHz"]) > 1:
        why.append("the run's live reps saw different refresh rates (%s Hz)" %
                   ", ".join("%g" % x for x in now["displayRefreshHz"]))
    if not now["displayRefreshHz"]:
        why.append("the run recorded no display refresh rate")
    elif not base["displayRefreshHz"]:
        why.append("the baseline recorded no display refresh rate")
    elif set(now["displayRefreshHz"]) != set(base["displayRefreshHz"]):
        why.append("display %s Hz, baseline %s Hz" % ("/".join("%g" % x for x in now["displayRefreshHz"]),
                                                       "/".join("%g" % x for x in base["displayRefreshHz"])))
    limit = max(max_load, (base["loadMedian"] or 0.0) + 2.0)
    if now["loadMedian"] is None:
        why.append("the run recorded no load average (made before ADR-1808)")
    elif now["loadMedian"] > limit:
        why.append("load average median %.1f (max %.1f) > %.1f" % (now["loadMedian"], now["loadMax"] or 0.0, limit))
    return why


def check(a):
    now = summarise(a.out)
    if not now:
        print("nothing to judge in", a.out)
        return 2
    base = json.load(open(a.baseline)) if os.path.exists(a.baseline) else {"scenes": {}}
    if not base["scenes"]:
        print("no baseline at %s: thresholds only" % a.baseline)
    wanted = a.scenes.split(",") if a.scenes else None
    nc = run_conditions({k: v for k, v in now.items() if wanted is None or k in wanted})
    bc = baseline_conditions(base)
    fmt_load = lambda c: "not recorded" if c["loadMedian"] is None else "median %.1f%s" % (
        c["loadMedian"], "" if c["loadMax"] is None else " max %.1f" % c["loadMax"])
    print("conditions: run %s Hz, load %s; baseline %s Hz, load %s" % (
        "/".join("%g" % x for x in nc["displayRefreshHz"]) or "?", fmt_load(nc),
        "/".join("%g" % x for x in bc["displayRefreshHz"]) or "?", fmt_load(bc)))
    differ = conditions_differ(nc, bc, a.max_load) if base["scenes"] else []
    crashed = False
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
            crashed = True
        if b and sc["missesMedian"] is not None:
            if sc["missesMedian"] > b["missesWorst"] + MISS_MARGIN:
                reg.append("misses %.1f > %.1f + %.0f" % (sc["missesMedian"], b["missesWorst"], MISS_MARGIN))
            if sc["p99Median"] > b["p99Worst"] + P99_MARGIN_MS:
                reg.append("p99 %.1f > %.1f + %.0f ms" % (sc["p99Median"], b["p99Worst"], P99_MARGIN_MS))
        lost = [k for k, ok in t.items() if ok is False and bt.get(k, False)]
        marks = " ".join(("-" if ok is None else "ok" if ok else ("LOST" if k in lost else "no")) for k, ok in t.items())
        fmt = lambda x, f="%.1f": "-" if x is None else f % x
        print("%-15s %7s %7s %6s %6s %5s%%  %-23s %s" % (
            scene, fmt(sc["missesMedian"]), fmt(b["missesMedian"]) if b else "-", fmt(sc["p99Median"]),
            fmt(b["p99Median"]) if b else "-", fmt(None if sc["mediumOrBetter"] is None else
                                                  100 * sc["mediumOrBetter"], "%.0f"),
            marks, "; ".join(reg) or "none"))
        failed = failed or bool(reg) or bool(lost)
    if differ and not crashed and not a.ignore_conditions:
        # ADR-1808: misses measured under other conditions are not evidence about the code, either way
        print("conditions differ from the baseline's: " + "; ".join(differ))
        print("RESULT: CONDITIONS DIFFER (would have been %s; re-run on a quiet machine at the baseline's refresh "
              "rate, or pass --ignore-conditions)" % ("FAIL" if failed else "PASS"))
        return 3
    if differ:
        print("conditions differ from the baseline's: " + "; ".join(differ) +
              (" (judged anyway: --ignore-conditions)" if a.ignore_conditions else " (a run crashed: judged anyway)"))
    print("RESULT:", "FAIL" if failed else "PASS")
    return 1 if failed else 0


def write(a):
    now = summarise(a.out)
    if not now:
        sys.exit("nothing to write from " + a.out)
    bad = [s for s, sc in now.items() if sc["failedRuns"] or not sc["reps"]]
    if bad:
        sys.exit("refusing to write a baseline with failed runs: " + ", ".join(bad))
    # ADR-1803: a run under an A/B arm or any other AVGEN_* override measured a different program
    overridden = sorted({"%s (%s)" % (s, r["environment"]) for s, sc in now.items() for r in sc["reps"]
                         if r.get("environment")})
    if overridden:
        sys.exit("refusing to write a baseline from runs with AVGEN_* overrides: " + "; ".join(overridden))
    # the owner's rule: a profile is evidence only on AC power
    conditions = os.path.join(a.out, "conditions.log")
    if os.path.exists(conditions):
        off_ac = [line.strip() for line in open(conditions) if "drawing from" in line and "AC Power" not in line]
        if off_ac:
            sys.exit("refusing to write a baseline from a run not on AC power: " + off_ac[0])
    # ADR-1808: a baseline is only as good as the conditions it was measured in
    wc = run_conditions(now)
    if len(wc["displayRefreshHz"]) != 1:
        sys.exit("refusing to write a baseline whose live reps recorded %s refresh rates" %
                 (len(wc["displayRefreshHz"]) or "no"))
    if wc["loadMedian"] is None:
        sys.exit("refusing to write a baseline with no recorded load average (run it with this tool's `run`)")
    if wc["loadMedian"] > a.max_load:
        sys.exit("refusing to write a baseline from a loaded machine: load median %.1f > %.1f" %
                 (wc["loadMedian"], a.max_load))
    for sc in now.values():
        sc["thresholds"] = thresholds(sc)
    if getattr(a, "add", False) and os.path.exists(a.baseline):
        # keep the baseline's other scenes: add (or replace) only the scenes this run measured
        with open(a.baseline) as f:
            old = json.load(f)
        merged = dict(old.get("scenes", {}))
        merged.update(now)
        now = merged
    rev = subprocess.run(["git", "describe", "--always", "--dirty", "--abbrev=8"], cwd=ROOT, capture_output=True,
                         text=True).stdout.strip()
    os.makedirs(os.path.dirname(a.baseline), exist_ok=True)
    if getattr(a, "add", False) and os.path.exists(a.baseline):
        oc = baseline_conditions(old)
        if oc["displayRefreshHz"] and set(oc["displayRefreshHz"]) != set(wc["displayRefreshHz"]):
            sys.exit("refusing to add %s Hz scenes to a %s Hz baseline" % (wc["displayRefreshHz"][0],
                                                                          oc["displayRefreshHz"][0]))
    doc = {"format": "avgen-live-baseline", "version": 1,
           "machine": "Apple M2 Max, macOS, %g Hz display (the owner's)" % wc["displayRefreshHz"][0],
           "gateConditions": {"displayRefreshHz": wc["displayRefreshHz"][0], "loadMedian": wc["loadMedian"],
                              "loadMax": wc["loadMax"]},
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
    ap.add_argument("--add", action="store_true", help="write: keep the baseline's other scenes, add or replace this run's")
    ap.add_argument("--max-load", type=float, default=MAX_LOAD, help="ADR-1808: the median 1-minute load average "
                    "above which a run is not on a quiet machine (check: conditions differ; write: refused)")
    ap.add_argument("--ignore-conditions", action="store_true", help="check: judge PASS/FAIL even when the run's "
                    "refresh rate or load differ from the baseline's")
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

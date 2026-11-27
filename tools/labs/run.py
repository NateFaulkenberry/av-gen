#!/usr/bin/env python3
"""The lab runner (ADR-2232): lab cases and benchmarks through avgen's public flags only, one record per run.

    python3 tools/labs/run.py cases volumetric --cases 1-6 --out <dir>             # render each case, hash it
    python3 tools/labs/run.py bench cel=examples/sonic-abstract/cel-dream.json \\
        lab=volumetric:3 --reps 3 --out <dir>                                       # interleaved GPU benchmark
    python3 tools/labs/run.py noise <specs...> --reps 3 --out <dir>                 # the same, read as a noise floor
    python3 tools/labs/run.py stats <dir>/results.json                              # re-read a run

Every row is `avgen-lab-run/1`, appended to <out>/results.json with the run's header (pwd, HEAD, dirty count, load).

The method it encodes (plan section 2, rule 8, and docs/testing.md), so that nobody has to remember it:
  * the exit code is the BINARY's, captured per run, never a wrapper's or a pipeline's (testing.md 18);
  * benchmarks drop `--warmup-frames` (60) and measure `--frames` (200) at a fixed quality level -- never `auto`,
    because the ladder moves; arms are INTERLEAVED rep by rep (testing.md 19), and medians and p95 are of GPU TOTALS
    (`gpuMs`), not pass labels;
  * a pipeline compiled inside the measured window makes the rep `valid: false` (the frame paid a compile);
  * a pass whose timestamp slot came back unwritten is reported as unwritten (the record's `unwrittenSlots`), never as
    0 ms -- on this Dawn/Metal path every compute pass's slot is unwritten;
  * the noise floor of an arm is the spread of its rep medians, and a difference smaller than twice it is no change.

GPU work needs the machine lock: run this under tools/gpu-lock.sh (one hold for the whole batch). It refuses to run a
GPU command unless the lock's holder is this process or one of its ancestors, which is the rule src/api/gpu_lock.hpp
applies; `--no-lock-check` overrides it for a machine with no other GPU user. Standard library only.
"""
import argparse
import csv
import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
import time

FORMAT = "avgen-lab-run/1"
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


# ---- conditions ------------------------------------------------------------------------------------------------------

def git(*args):
    try:
        return subprocess.run(["git", "-C", ROOT, *args], capture_output=True, text=True, check=False).stdout.strip()
    except OSError:
        return ""


def load_average():
    try:
        return [round(x, 2) for x in os.getloadavg()]
    except OSError:
        return None


def header():
    dirty = git("status", "--porcelain")
    return {"pwd": ROOT, "head": git("rev-parse", "--short", "HEAD"),
            "dirty": len([l for l in dirty.splitlines() if l.strip()]), "load": load_average(),
            "started": time.strftime("%Y-%m-%dT%H:%M:%S")}


def lock_dir():
    return os.path.join(os.environ.get("TMPDIR", "/tmp"), "avgen-gpu.lock")


def ancestors():
    out, pid = [], os.getpid()
    for _ in range(64):
        out.append(pid)
        try:
            ppid = int(subprocess.run(["ps", "-o", "ppid=", "-p", str(pid)], capture_output=True, text=True).stdout)
        except ValueError:
            break
        if ppid <= 1 or ppid == pid:
            break
        pid = ppid
    return out


def require_lock(args):
    if args.no_lock_check:
        return
    try:
        with open(os.path.join(lock_dir(), "pid")) as f:
            holder = int(f.read().strip())
    except (OSError, ValueError):
        sys.exit("run.py: the GPU lock is not held. Run under tools/gpu-lock.sh (one hold for the batch), or pass "
                 "--no-lock-check on a machine nobody else is using")
    if holder not in ancestors():
        sys.exit(f"run.py: the GPU lock is held by pid {holder}, not by this process or its parents; waiting for it is "
                 "tools/gpu-lock.sh's job, so run under it")


# ---- statistics ------------------------------------------------------------------------------------------------------

def percentile(values, p):
    v = sorted(values)
    if not v:
        return None
    k = (len(v) - 1) * p
    f = int(k)
    c = min(f + 1, len(v) - 1)
    return v[f] + (v[c] - v[f]) * (k - f)


def trace_stats(path):
    """The measured ('m') rows of a --live-profile headless trace."""
    with open(path) as f:
        rows = [r for r in csv.DictReader(f) if r.get("phase") == "m"]
    if not rows:
        return {"available": False, "reason": "no measured rows in the trace"}
    gpu = [float(r["gpuMs"]) for r in rows if float(r["gpuMs"]) >= 0.0]
    out = {
        "frames": len(rows),
        "gpuP50": percentile(gpu, 0.5) if gpu else None,
        "gpuP95": percentile(gpu, 0.95) if gpu else None,
        "frameP50": percentile([float(r["frameMs"]) for r in rows], 0.5),
        "cpuWorkP50": percentile([float(r["cpuWorkMs"]) for r in rows], 0.5),
        "compilesInWindow": int(rows[-1]["compiles"]) - int(rows[0]["compiles"]),
        "gpuTimedFrames": len(gpu),
    }
    if "unwrittenSlots" in rows[0]:
        out["framesWithUnwrittenSlots"] = sum(1 for r in rows if int(r["unwrittenSlots"]) > 0)
    return out


# ---- running avgen ---------------------------------------------------------------------------------------------------

def avgen_binary(args):
    return os.path.join(ROOT, args.bin, "src", "avgen") if not os.path.isabs(args.bin) else os.path.join(args.bin, "src", "avgen")


def run_avgen(argv, log_path):
    start = time.time()
    with open(log_path, "w") as log:
        proc = subprocess.run(argv, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    # The binary's own exit code: subprocess.run returns it directly, no shell and no pipe in between.
    return proc.returncode, round(time.time() - start, 2)


def grep_log(path, pattern):
    rx = re.compile(pattern)
    with open(path, errors="replace") as f:
        for line in f:
            m = rx.search(line)
            if m:
                yield m


def file_digests(folder):
    out = []
    if not os.path.isdir(folder):
        return out
    for name in sorted(os.listdir(folder)):
        p = os.path.join(folder, name)
        if os.path.isfile(p) and name.lower().endswith((".png", ".exr")):
            with open(p, "rb") as f:
                out.append({"file": name, "md5": hashlib.md5(f.read()).hexdigest()})
    return out


def parse_range(text):
    if text in (None, "", "all"):
        return None
    out = set()
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-", 1)
            out.update(range(int(a), int(b) + 1))
        else:
            out.add(int(part))
    return out


def load_cases(lab):
    path = os.path.join(ROOT, "examples", "labs", lab, "cases.json")
    with open(path) as f:
        doc = json.load(f)
    if doc.get("format") != "avgen-lab-cases":
        sys.exit(f"run.py: {path} is not an avgen-lab-cases file")
    return doc["cases"]


def append(out_dir, row):
    path = os.path.join(out_dir, "results.json")
    doc = {"format": FORMAT, "rows": []}
    if os.path.exists(path):
        with open(path) as f:
            doc = json.load(f)
    doc["rows"].append(row)
    with open(path, "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")


# ---- commands --------------------------------------------------------------------------------------------------------

def cmd_cases(args):
    require_lock(args)
    os.makedirs(args.out, exist_ok=True)
    wanted = parse_range(args.cases)
    hdr = header()
    failures = 0
    for case in load_cases(args.lab):
        n = case["number"]
        if wanted is not None and n not in wanted:
            continue
        tag = f"{args.lab}-{n}"
        row = {"format": FORMAT, "kind": "case", "case": f"{args.lab}:{n}", "title": case.get("title"), **hdr}
        if case.get("blockedBy"):
            row.update({"available": False, "reason": f"blocked by {case['blockedBy']}"})
            append(args.out, row)
            continue
        frames = os.path.join(args.out, tag)
        argv = [avgen_binary(args), "--headless", "--lab-case", f"{args.lab}:{n}", "--render", frames]
        if args.camera:
            argv += ["--camera", args.camera]
        rc, seconds = run_avgen(argv, os.path.join(args.out, tag + ".log"))
        hashes = [m.group(1) for m in grep_log(os.path.join(args.out, tag + ".log"), r"sequence hash ([0-9a-f]{16})")]
        camera = [m.group(1) for m in grep_log(os.path.join(args.out, tag + ".log"), r"render: through (.*)$")]
        row.update({"argv": argv[1:], "exit": rc, "seconds": seconds, "sequenceHash": hashes[-1] if hashes else None,
                    "camera": camera[-1] if camera else "film", "frames": file_digests(frames),
                    "loadAfter": load_average()})
        failures += rc != 0
        append(args.out, row)
        print(f"{tag}: exit {rc}, hash {row['sequenceHash']}, through {row['camera']}, {seconds} s")
    return 1 if failures else 0


def parse_spec(text):
    """name=<project | composition | lab:case>[@start seconds]"""
    if "=" not in text:
        sys.exit(f"run.py: a bench spec is name=<project or lab:case>[@start], got '{text}'")
    name, target = text.split("=", 1)
    start = None
    if "@" in target:
        target, at = target.rsplit("@", 1)
        start = float(at)
    return name, target, start


def bench_rows(args, purpose):
    require_lock(args)
    os.makedirs(args.out, exist_ok=True)
    if args.quality == "auto":
        sys.exit("run.py: --quality auto moves during the run; name a level (ultra, high, medium, low, emergency)")
    specs = [parse_spec(s) for s in args.specs]
    hdr = header()
    measure = args.frames / args.fps
    rows = []
    reps = ["warm"] + [f"rep{r}" for r in range(1, args.reps + 1)]
    for rep in reps:  # interleaved: every arm once, then every arm again
        for name, target, spec_start in specs:
            argv = [avgen_binary(args), "--live-profile", "--mode", "headless"]
            if re.fullmatch(r"[a-z0-9-]+:\d+", target):
                lab, n = target.split(":")
                case = next((c for c in load_cases(lab) if c["number"] == int(n)), None)
                if case is None:
                    sys.exit(f"run.py: no case {target}")
                fixture = case["fixture"]
                argv += ["--composition" if fixture.endswith(".scene.json") else "--project", fixture]
                start = spec_start if spec_start is not None else args.start if args.start is not None else \
                    case.get("timeSeconds", 0.0)
                if isinstance(case.get("camera"), str):
                    argv += ["--camera", case["camera"]]
                elif isinstance(case.get("camera"), dict):
                    e, a = case["camera"]["eye"], case["camera"]["aim"]
                    argv += ["--camera-pose", ",".join(f"{x:g}" for x in [*e, *a])]
            else:
                argv += ["--composition" if target.endswith(".scene.json") else "--project", target]
                start = spec_start if spec_start is not None else args.start if args.start is not None else 0.0
            if args.camera:
                argv += ["--camera", args.camera]
            tag = f"{name}-{rep}"
            trace = os.path.join(args.out, tag + ".csv")
            record = os.path.join(args.out, tag + ".json")
            warm = rep == "warm"
            argv += ["--size", args.size, "--target-fps", f"{args.fps:g}", "--start", f"{start:g}",
                     "--warmup-frames", "10" if warm else str(args.warmup_frames),
                     "--measure", "1" if warm else f"{measure:.4f}", "--quality", args.quality, "--no-audio",
                     "--no-text", "--json", record, "--trace", trace]
            rc, seconds = run_avgen(argv, os.path.join(args.out, tag + ".log"))
            row = {"format": FORMAT, "kind": purpose, "arm": name, "rep": rep, "argv": argv[1:], "exit": rc,
                   "seconds": seconds, **hdr, "loadAfter": load_average()}
            if warm:
                row.update({"available": False, "reason": "the pipeline-compile run, discarded by design"})
            elif rc != 0:
                row.update({"available": False, "reason": f"avgen exited {rc}"})
            else:
                row.update(trace_stats(trace))
                try:
                    with open(record) as f:
                        rec = json.load(f)
                    row["unwrittenSlots"] = rec.get("unwrittenSlots",
                                                    {"available": False, "reason": "a record from before ADR-2232"})
                except (OSError, ValueError) as e:
                    row["unwrittenSlots"] = {"available": False, "reason": str(e)}
                row["valid"] = row.get("compilesInWindow", 1) == 0
                if not row["valid"]:
                    row["reason"] = f"{row.get('compilesInWindow')} pipeline(s) compiled inside the measured window"
            append(args.out, row)
            rows.append(row)
            print(f"{tag}: exit {rc}, gpu p50 {row.get('gpuP50')}, p95 {row.get('gpuP95')}, "
                  f"compiles {row.get('compilesInWindow')}, {seconds} s, load {row['loadAfter']}")
    return rows


def summarise(rows, out_dir):
    by_arm = {}
    for r in rows:
        if r.get("gpuP50") is not None:
            by_arm.setdefault(r["arm"], []).append(r)
    summary = []
    for arm, rs in by_arm.items():
        p50 = [r["gpuP50"] for r in rs]
        med = statistics.median(p50)
        spread = max(p50) - min(p50)
        summary.append({"format": FORMAT, "kind": "noise-floor", "arm": arm, "reps": len(rs),
                        "gpuP50OfReps": round(med, 3), "gpuP50Min": round(min(p50), 3), "gpuP50Max": round(max(p50), 3),
                        "repSpreadMs": round(spread, 3), "repSpreadPct": round(100.0 * spread / med, 1) if med else None,
                        "noChangeBandMs": round(2.0 * spread, 3),
                        "allValid": all(r.get("valid") for r in rs),
                        "gpuP95OfReps": round(statistics.median([r["gpuP95"] for r in rs]), 3)})
    for s in summary:
        append(out_dir, s)
        print(f"{s['arm']}: GPU p50 {s['gpuP50OfReps']} ms over {s['reps']} reps "
              f"({s['gpuP50Min']}-{s['gpuP50Max']}, spread {s['repSpreadPct']}%), "
              f"no-change band +-{s['noChangeBandMs']} ms, all valid {s['allValid']}")
    return summary


def cmd_bench(args):
    rows = bench_rows(args, "bench")
    summarise(rows, args.out)
    return 1 if any(r["exit"] != 0 for r in rows) else 0


def cmd_noise(args):
    if args.reps < 3:
        sys.exit("run.py noise: a spread needs at least 3 reps")
    rows = bench_rows(args, "noise")
    summarise(rows, args.out)
    return 1 if any(r["exit"] != 0 for r in rows) else 0


def cmd_stats(args):
    with open(args.results) as f:
        doc = json.load(f)
    rows = [r for r in doc["rows"] if r.get("kind") in ("bench", "noise")]
    for r in rows:
        print(f"{r['arm']:24} {r['rep']:6} exit {r['exit']} gpu p50 {r.get('gpuP50')} p95 {r.get('gpuP95')} "
              f"compiles {r.get('compilesInWindow')} unwritten {r.get('unwrittenSlots')}")
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bin", default="build/release", help="the build directory (default build/release)")
    p.add_argument("--no-lock-check", action="store_true", help="do not require the GPU lock to be ours")
    sub = p.add_subparsers(dest="command", required=True)
    c = sub.add_parser("cases", help="render lab cases and hash them")
    c.add_argument("lab")
    c.add_argument("--cases", default="all", help="e.g. 1-6 or 301,302 (default all)")
    c.add_argument("--camera", help="override every case's camera with this named camera")
    c.add_argument("--out", required=True)
    c.set_defaults(func=cmd_cases)
    for name, func in (("bench", cmd_bench), ("noise", cmd_noise)):
        b = sub.add_parser(name, help="interleaved headless GPU benchmark" if name == "bench" else "noise floors")
        b.add_argument("specs", nargs="+", help="name=<project or composition path, or lab:case>[@start seconds]")
        b.add_argument("--reps", type=int, default=3)
        b.add_argument("--warmup-frames", type=int, default=60)
        b.add_argument("--frames", type=int, default=200)
        b.add_argument("--fps", type=float, default=30.0)
        b.add_argument("--size", default="1920x1080")
        b.add_argument("--quality", default="ultra")
        b.add_argument("--start", type=float, help="piece time to measure from (default: the case's time, else 0)")
        b.add_argument("--camera", help="a named camera for every arm")
        b.add_argument("--out", required=True)
        b.set_defaults(func=func)
    s = sub.add_parser("stats", help="re-read a results.json")
    s.add_argument("results")
    s.set_defaults(func=cmd_stats)
    args = p.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())

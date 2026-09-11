#!/usr/bin/env python3
"""Measure a set of world presets headless and print what they cost.

The renderer laboratory's measuring instrument (Renderer 2.0 §60, §68). It runs
`avgen --headless` over the density ladder in `examples/recipes/glowmere-*.recipe.json` and
reports the frame times and the geometry counters the engine already logs, so a renderer change
can be shown to help or hurt rather than argued about.

    tools/render_bench.py                               # the four-rung ladder, 3 repeats
    tools/render_bench.py low medium --repeats 5
    tools/render_bench.py --frames 300 --size 2880x1800
    tools/render_bench.py --wait-idle 600               # let another agent's build finish first
    tools/render_bench.py examples/world/glowmere-stylized.json   # a project, for comparison
    tools/render_bench.py --json out.json               # everything, machine-readable

Three things it does that a shell loop over `avgen` does not, each because the naive version
produced a wrong answer first:

**It interleaves.** Runs go round-robin across the presets rather than finishing one preset before
starting the next. A laptop under sustained GPU load loses several per cent over a couple of
minutes, and whichever preset is measured last inherits all of that drift as if it were a property
of the preset. Interleaving spreads the drift across every preset instead of concentrating it in
the last one. It cannot remove the drift; it stops the drift from having a preferred victim.

**It reports every run, not just the median.** A median over three runs is a number with no error
bar, and the reader has no way to tell a 5% regression from a noisy machine. The per-run table is
printed by default for exactly that reason, and the summary carries the spread.

**It checks the things that silently invalidate a pass**: a non-zero GPU error count, another
`avgen` or a build competing for the machine before *or* after each run, and the benchmarked binary
changing mid-pass. All three have happened; all three produce numbers that look perfectly
plausible. Contended runs are excluded from the medians and kept in the per-run table.

Pure standard library.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BINARY = os.path.join(REPO, "build", "release", "src", "avgen")
RECIPE_DIR = os.path.join(REPO, "examples", "recipes")
LADDER = ["low", "medium", "dense", "extreme"]

# The engine already logs everything below. Parsing its own output rather than adding a reporting
# mode to the renderer is deliberate: this tool must not be a reason to touch `src/`, and a
# benchmark that needs an engine change before it can measure anything is a benchmark that arrives
# after the change it was meant to evaluate.
WALL = re.compile(
    r"frame wall clock over (\d+) steady frames: median ([\d.]+) ms\s+p10 ([\d.]+)\s+p90 ([\d.]+)\s+min ([\d.]+)")
GPU = re.compile(r"gpu frame median ([\d.]+) ms; pass medians \(sum ([\d.]+)\):(.*)$", re.M)
PASS = re.compile(r"([\w/]+)=([\d.]+)")
STATS = re.compile(
    r"draws=(\d+) \(indirect (\d+), empty (\d+), skipped (\d+)\)\s+shadowDraws=(\d+)"
    r".*?\btris=(\d+)\s+instances=(\d+)v/(\d+)c\s+lod=([\d/]+)")
ERRORS = re.compile(r"headless run complete: (\d+) frames at ([\d.]+) fps; GPU errors: (\d+)")
PLACED = re.compile(r"scatter '([\w.-]+)' placed (\d+) instances")
CHUNKS = re.compile(r"(\d+) chunks \(\d+ with water\), (\d+) triangles at LOD 0")


def resolve(name):
    """A ladder rung, a recipe path or a project path -> (label, path, flag).

    The flag matters: a recipe is composed at start-up with `--generate`, a project is loaded with
    `--project`. Feeding a recipe to `--project` fails with "is not an avgen project", which is a
    clear enough message but only if you are looking at the log rather than at an empty column.
    """
    if os.path.sep in name or name.endswith(".json"):
        path = os.path.abspath(name)
        label = os.path.basename(path).replace(".recipe.json", "").replace(".json", "")
    else:
        path = os.path.join(RECIPE_DIR, "glowmere-%s.recipe.json" % name)
        label = name
    if not os.path.exists(path):
        sys.exit("no such preset or file: %s (looked for %s)" % (name, path))
    return label, path, "--generate" if path.endswith(".recipe.json") else "--project"


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()[:12]


# Programs whose presence makes a frame time meaningless. `avgen` shares the GPU outright; a
# compiler or a linker saturates the cores the submission thread runs on. Matched on argv[0] only.
NOISY = ("avgen", "clang", "clang++", "cc1", "ld", "ninja", "cmake", "ctest")


def competing_processes(binary):
    """Other processes that will contend with the run about to start.

    This is not paranoia. A windowed `avgen --play` left running in another terminal moved a
    Glowmere frame from 26 ms to 104 ms -- a 4x error, entirely plausible-looking, and invisible
    unless somebody goes looking for the cause. A parallel build of the test suite did the same
    thing to a pass that was already half finished, so the first two runs of every preset were
    clean and the last three were not, which reads exactly like a preset getting slower.
    """
    if not shutil.which("ps"):
        return []
    try:
        out = subprocess.run(["ps", "-Ao", "pid,args"], capture_output=True, text=True, timeout=10).stdout
    except (subprocess.SubprocessError, OSError):
        return []
    mine = str(os.getpid())
    names = set(NOISY) | {os.path.basename(binary)}
    found = []
    for line in out.splitlines()[1:]:
        pid, _, args = line.strip().partition(" ")
        # argv[0] only. Matching the name anywhere in the command line reported every clang
        # invocation of a build as a competing renderer, which is the kind of warning a reader
        # learns to ignore and then misses the real one.
        argv0 = args.strip().split(" ", 1)[0]
        if pid == mine or os.path.basename(argv0) not in names:
            continue
        found.append((pid, args.strip()[:90]))
    return found


def wait_for_quiet(binary, timeout, poll=5.0):
    """Block until nothing noisy is running, or give up and say so.

    Worth having rather than simply refusing: in a repository several agents are working in at
    once, waiting two minutes for a build to finish is the difference between a benchmark pass and
    no benchmark pass.
    """
    deadline = time.time() + timeout
    announced = False
    while True:
        busy = competing_processes(binary)
        if not busy:
            return []
        if time.time() >= deadline:
            return busy
        if not announced:
            print("  waiting for %d competing process(es) to finish (%s)..."
                  % (len(busy), ", ".join(os.path.basename(c.split(" ")[0]) for _, c in busy[:4])),
                  file=sys.stderr, flush=True)
            announced = True
        time.sleep(poll)


def run_once(binary, path, flag, frames, fps, size, tier, extra):
    cmd = [binary, "--headless", flag, path, "--frames", str(frames),
           "--fps", str(fps), "--size", size, "--tier", tier] + list(extra)
    started = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    # avgen logs to stderr; keeping both means a build that changes that does not silently produce
    # a table of empty columns.
    log = (proc.stdout or "") + (proc.stderr or "")
    result = {"command": " ".join(cmd), "exit": proc.returncode,
              "process_seconds": round(time.time() - started, 2)}

    if m := WALL.search(log):
        result.update(steady_frames=int(m.group(1)), wall_median=float(m.group(2)),
                      wall_p10=float(m.group(3)), wall_p90=float(m.group(4)),
                      wall_min=float(m.group(5)))
    if m := GPU.search(log):
        result.update(gpu_median=float(m.group(1)), pass_sum=float(m.group(2)),
                      passes={k: float(v) for k, v in PASS.findall(m.group(3))})
    # The *last* statistics line, not the first: the first frame carries pipeline creation, mesh
    # upload and an unsettled LOD distribution, and its instance counts are all zero.
    if hits := STATS.findall(log):
        d, ind, empty, skipped, shadow, tris, vis, culled, lod = hits[-1]
        result.update(draws=int(d), indirect=int(ind), empty_draws=int(empty),
                      skipped_draws=int(skipped), shadow_draws=int(shadow),
                      tris=int(tris), visible=int(vis), culled=int(culled), lod=lod)
    if m := ERRORS.search(log):
        result["gpu_errors"] = int(m.group(3))
    # How much of the GPU frame the passes account for. The timeline's intervals partition the
    # frame, so on an idle machine this sits near 1. When it collapses -- 26 ms of passes inside a
    # 105 ms frame -- the frame is waiting on something outside every measured pass, which on a
    # shared GPU means somebody else's work. It is the one number in this tool that says "do not
    # believe the column to the left of me".
    if result.get("gpu_median") and result.get("pass_sum") is not None:
        result["accounted"] = result["pass_sum"] / result["gpu_median"]
    result["placed"] = {n: int(c) for n, c in PLACED.findall(log)}
    result["placed_total"] = sum(result["placed"].values())
    if m := CHUNKS.search(log):
        result.update(terrain_chunks=int(m.group(1)), terrain_tris=int(m.group(2)))
    if proc.returncode != 0 or result.get("gpu_errors", 1) != 0:
        result["log_tail"] = "\n".join(log.splitlines()[-12:])
    return result


def spread(values):
    """Peak-to-peak as a fraction of the median. The one number that says whether to believe the
    median at all: at 2% a 10% difference between presets is a result, at 40% it is weather."""
    if len(values) < 2:
        return None
    mid = statistics.median(values)
    return (max(values) - min(values)) / mid if mid else None


def table(rows, headers, aligns):
    widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(cell))
    def line(cells):
        return "  ".join(c.rjust(widths[i]) if aligns[i] == "r" else c.ljust(widths[i])
                         for i, c in enumerate(cells)).rstrip()
    out = [line(headers), "  ".join("-" * w for w in widths)]
    out.extend(line(r) for r in rows)
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("presets", nargs="*", default=None,
                    help="ladder rungs (low medium dense extreme), recipe paths or project paths")
    ap.add_argument("--repeats", type=int, default=3, help="runs per preset (default 3)")
    ap.add_argument("--frames", type=int, default=180,
                    help="frames per run; the engine drops the first 12 as warm-up (default 180)")
    ap.add_argument("--fps", type=float, default=30, help="simulation cadence, not achieved speed")
    ap.add_argument("--size", default="1440x900")
    ap.add_argument("--tier", default="realtime", choices=["preview", "realtime", "high", "offline"])
    ap.add_argument("--binary", default=DEFAULT_BINARY)
    ap.add_argument("--json", help="write every run's full record here")
    ap.add_argument("--extra", nargs=argparse.REMAINDER, default=[],
                    help="everything after this is passed to avgen (e.g. --extra --disable volume)")
    ap.add_argument("--wait-idle", type=float, default=0.0, metavar="SECONDS",
                    help="before each run, wait up to this long for competing builds and other "
                         "avgen processes to finish (0 = do not wait, but still report them)")
    ap.add_argument("--no-interleave", action="store_true",
                    help="run each preset to completion in turn; thermal drift then lands on "
                         "whichever preset is last, so this is for debugging the harness only")
    ap.add_argument("--quiet", action="store_true", help="summary only, no per-run table")
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        sys.exit("no binary at %s -- build first: cmake --build build/release -j8" % args.binary)
    targets = [resolve(n) for n in (args.presets or LADDER)]

    if competing := competing_processes(args.binary):
        print("WARNING: %d competing process(es) are running (renderer or build):" % len(competing))
        for pid, cmdline in competing:
            print("  pid %s  %s" % (pid, cmdline))
        print("  Every number below is contended. Stop them, or pass --wait-idle.\n")

    before = digest(args.binary)
    order = []
    for r in range(args.repeats):
        for t in targets:
            order.append((r, t))
    if args.no_interleave:
        order.sort(key=lambda x: (targets.index(x[1]), x[0]))

    runs = {label: [] for label, _, _ in targets}
    total = len(order)
    for n, (repeat, (label, path, flag)) in enumerate(order, 1):
        print("[%d/%d] %s (run %d)" % (n, total, label, repeat + 1), file=sys.stderr, flush=True)
        # Checked per run, not once for the pass. A build that starts in the middle contaminates
        # the second half and leaves the first half looking like the fast presets.
        busy = wait_for_quiet(args.binary, args.wait_idle) if args.wait_idle else competing_processes(args.binary)
        record = run_once(args.binary, path, flag, args.frames, args.fps, args.size, args.tier, args.extra)
        record["repeat"] = repeat + 1
        # Before *and* after: waiting for quiet only protects the start, and a build kicked off
        # thirty seconds into a run is exactly the case that produced a table where every preset's
        # later runs were four times its earlier ones.
        record["contended"] = sorted({c for _, c in busy} | {c for _, c in competing_processes(args.binary)})
        runs[label].append(record)
        if record.get("exit") or record.get("gpu_errors", 1):
            print("  FAILED (exit %s, GPU errors %s)" % (record.get("exit"), record.get("gpu_errors")),
                  file=sys.stderr)
            print(record.get("log_tail", ""), file=sys.stderr)

    after = digest(args.binary)
    print()

    if not args.quiet:
        rows = []
        for label, _, _ in targets:
            for rec in runs[label]:
                rows.append([label, str(rec["repeat"]),
                             "%.2f" % rec.get("wall_median", float("nan")),
                             "%.2f" % rec.get("wall_p10", float("nan")),
                             "%.2f" % rec.get("wall_p90", float("nan")),
                             "%.2f" % rec.get("gpu_median", float("nan")),
                             "%.2f" % rec.get("passes", {}).get("scene", float("nan")),
                             "%.2f" % rec["accounted"] if "accounted" in rec else "-",
                             str(rec.get("visible", "-")), str(rec.get("draws", "-")),
                             str(rec.get("gpu_errors", "-")),
                             "busy" if rec.get("contended") else ""])
        print("Every run, in the order they were taken")
        print(table(rows, ["preset", "run", "wall", "p10", "p90", "gpu", "scene", "acct",
                           "visible", "draws", "err", ""], "lrrrrrrrrrrl"))
        print("`acct` is the pass medians' sum over the GPU frame median. Near 1 means the frame is")
        print("the passes; well under 1 means the frame spent its time outside every measured pass,")
        print("which on a shared GPU means somebody else's work. `busy` marks a run that started")
        print("while another avgen or a build was running.")
        print()

    rows = []
    dropped = 0
    all_contended = []
    for label, _, _ in targets:
        recs = [r for r in runs[label] if "wall_median" in r]
        # Contended runs are excluded from the medians rather than averaged in. Including them
        # does not make the answer more robust -- it makes it a median of two different machines.
        # They stay in the per-run table and in the JSON, so nothing is hidden.
        clean = [r for r in recs if not r.get("contended")]
        if clean:
            dropped += len(recs) - len(clean)
            recs = clean
        elif recs:
            # Nothing clean to fall back to. Reporting an empty row would hide the run entirely,
            # so the numbers stand and the preset is named as contended throughout.
            all_contended.append(label)
        if not recs:
            rows.append([label] + ["-"] * 11)
            continue
        walls = [r["wall_median"] for r in recs]
        gpus = [r["gpu_median"] for r in recs if "gpu_median" in r]
        scenes = [r["passes"]["scene"] for r in recs if "scene" in r.get("passes", {})]
        last = recs[-1]
        sp = spread(walls)
        rows.append([
            label,
            str(len(recs)),
            "%.2f" % statistics.median(walls),
            "%.2f" % statistics.median([r["wall_p10"] for r in recs]),
            "%.2f" % statistics.median([r["wall_p90"] for r in recs]),
            "%.0f%%" % (sp * 100) if sp is not None else "-",
            "%.2f" % statistics.median(gpus) if gpus else "-",
            "%.2f" % statistics.median(scenes) if scenes else "-",
            str(last.get("draws", "-")),
            str(last.get("shadow_draws", "-")),
            "%d" % last.get("visible", 0),
            "%.2fM" % (last.get("tris", 0) / 1e6),
        ])
    print("Summary: median over %d interleaved run(s), %d frames at %s, tier %s%s"
          % (args.repeats, args.frames, args.size, args.tier,
             "; %d contended run(s) excluded" % dropped if dropped else ""))
    if all_contended:
        print("EVERY run of %s was contended. Those rows are not a measurement of anything."
              % ", ".join(all_contended))
    print(table(rows, ["preset", "runs", "wall", "p10", "p90", "spread", "gpu", "scene",
                       "draws", "shadowDr", "visible", "tris"], "lrrrrrrrrrrr"))
    print()
    print("wall/p10/p90/gpu/scene are milliseconds; spread is the peak-to-peak of the per-run")
    print("medians over their median. `visible` is instances surviving the GPU cull. `tris` is the")
    print("engine's `tris=` counter, which moves with resolution and with the LOD the cull pass")
    print("chose -- so it is a submitted count, not the pre-cull figure of the same name, and it is")
    print("only comparable between runs taken at the same --size.")
    if before != after:
        print("\nWARNING: %s changed during the pass (%s -> %s). The runs are not comparable."
              % (os.path.basename(args.binary), before, after))
    if competing := competing_processes(args.binary):
        print("\nWARNING: %d competing process(es) were still running at the end of the pass."
              % len(competing))

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"binary": args.binary, "binary_sha256_before": before,
                       "binary_sha256_after": after, "frames": args.frames, "fps": args.fps,
                       "size": args.size, "tier": args.tier, "repeats": args.repeats,
                       "interleaved": not args.no_interleave, "runs": runs}, f, indent=1)
        print("\nwrote %s" % args.json)

    failed = [r for recs in runs.values() for r in recs if r.get("exit") or r.get("gpu_errors", 1)]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

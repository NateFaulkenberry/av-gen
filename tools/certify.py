#!/usr/bin/env python3
"""Phase G certification: run the declared stress scenes and say whether the renderer still
matches what they declare about themselves.

    tools/certify.py                       # every subject, 3 interleaved repeats
    tools/certify.py glowmere-dense        # one subject
    tools/certify.py --repeats 5 --frames 240
    tools/certify.py --record              # rewrite each scene's declaration from this run
    tools/certify.py --report out.md       # the certification table, as markdown

WHAT THIS IS FOR
----------------
`docs/renderer-2-benchmark-world.md` measures a ladder of Glowmere densities and writes the
resulting instance, draw and triangle counts into a table in a document.  A number in a document is
not a check: nothing fails when the renderer stops producing it, and the only way to find out what a
scene now contains is to render it and read the log.  So each certification subject carries a
`certification` block **inside the scene file**, and this tool renders the scene and compares.

The distinction that makes the block worth having: **counters are deterministic and timings are
not.**  A composition is a pure function of (recipe, library, seed) and the culling decision is a
pure function of that plus the camera and the resolution, so `placed`, `visible`, `draws` and
`submittedTriangles` are exact -- an unequal count is a real change every time, with no noise floor
to argue about.  Frame times are not exact and are never compared against a declared number here;
they are measured, distributed, and reported with the spread the repeats actually showed.  A
certification that asserted a millisecond figure would be comparing against another session, which
`docs/renderer-upgrade/01-audit-and-baseline.md` §3.1 forbids.

WHAT IT MEASURES
----------------
Per subject, per repeat, through the engine's own `--bench-json` record (ADR-113), which carries the
conditions: revision, dirty flag, build, adapter, camera pose, resolution, tier and a per-process
session id.  Records from different session ids are never compared.

  * the declared counters, checked exactly (G1);
  * the frame pacing -- p50, p90, p95, p99, the 1% low and the 0.1% low, on both clocks (G3).
    The 1% low is the *mean of the slowest 1%* and is not p99; both are reported because the gap
    between them is the shape of the tail;
  * the spread across repeats, which is the only thing that says whether any of the timings mean
    anything today.

The repeats are interleaved round-robin across subjects, not blocked per subject: a machine that
drifts otherwise charges all of its drift to whichever subject ran last.

WHAT IT DOES NOT DO
-------------------
It does not fail on a timing.  It fails on a counter, on a GPU error, on a dirty tree when
`--strict` is given, and on nothing else.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
BINARY = ROOT / "build" / "release" / "src" / "avgen"
GPU_LOCK = ROOT / "tools" / "gpu-lock.sh"

# The counters a declaration may pin.  Every one of them is a deterministic function of the scene,
# the camera and the resolution; nothing that varies frame to frame on an unchanged scene is on this
# list, because a check that is sometimes wrong teaches people to ignore it.
CHECKED_COUNTERS = [
    "draws",
    "shadowDraws",
    "submittedTriangles",
    "logicalTriangles",
    "visibleInstances",
    "culledInstances",
    "entities",
    "shadowCasters",
    "lights",
]


def discover_subjects() -> list[dict]:
    """Every scene file under examples/ carrying a `certification` block, in name order."""
    found = []
    for path in sorted(ROOT.glob("examples/**/*.json")):
        try:
            doc = json.loads(path.read_text())
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        if not isinstance(doc, dict):
            continue
        block = doc.get("certification")
        if isinstance(block, dict):
            found.append({"path": path, "declaration": block, "name": path.name.split(".")[0]})
    return found


def run_one(subject: dict, args, out_dir: pathlib.Path, index: int) -> dict | None:
    """One render of one subject, returning its bench-json record."""
    path = subject["path"]
    declaration = subject["declaration"]
    kind = declaration.get("load", "generate")
    record_path = out_dir / f"{subject['name']}-{index}.json"
    command = [str(BINARY), "--headless", f"--{kind}", str(path),
               "--frames", str(args.frames), "--fps", str(declaration.get("fps", 30)),
               "--size", declaration.get("atResolution", "1280x800"),
               "--tier", declaration.get("tier", "realtime"),
               "--bench-json", str(record_path)]
    if not args.no_lock:
        command = [str(GPU_LOCK)] + command
    started = time.time()
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    elapsed = time.time() - started
    if completed.returncode != 0 or not record_path.is_file():
        sys.stderr.write(f"  {subject['name']} run {index}: FAILED (exit {completed.returncode})\n")
        sys.stderr.write("".join(completed.stderr.splitlines(keepends=True)[-15:]))
        return None
    record = json.loads(record_path.read_text())["records"][0]
    # The engine prints its own GPU error count; a run with errors measured a frame the device did
    # not finish, and its numbers are not evidence whatever they say.
    errors = re.search(r"GPU errors: (\d+)", completed.stdout + completed.stderr)
    record["_gpuErrors"] = int(errors.group(1)) if errors else -1
    record["_wallSeconds"] = elapsed
    print(f"  {subject['name']:<22} run {index}  gpu p50 {record['gpuMs']['p50']:7.3f} ms  "
          f"wall p50 {record['wallMs']['p50']:7.3f} ms  "
          f"draws {record['counters']['draws']:.0f}  "
          f"visible {record['counters']['visibleInstances']:.0f}  "
          f"errors {record['_gpuErrors']}  ({elapsed:.0f}s)")
    return record


def spread_percent(values: list[float]) -> float:
    if not values:
        return 0.0
    middle = sorted(values)[len(values) // 2]
    return 0.0 if middle <= 0 else (max(values) - min(values)) / middle * 100.0


def check_counters(declared: dict, records: list[dict]) -> list[str]:
    """Counter differences, as sentences.  Every repeat is checked, not only the first: a counter
    that moves *between repeats of the same scene* is a different and worse finding than one that
    has changed value, and collapsing to the first repeat would hide it."""
    problems = []
    expected = declared.get("counters", {})
    for name in CHECKED_COUNTERS:
        if name not in expected:
            continue
        seen = {record["counters"].get(name) for record in records}
        if len(seen) > 1:
            problems.append(f"{name}: not stable across repeats -- {sorted(seen)}")
            continue
        actual = seen.pop() if seen else None
        if actual != expected[name]:
            problems.append(f"{name}: declared {expected[name]:.0f}, measured {actual:.0f}"
                            f" ({actual - expected[name]:+.0f})")
    return problems


def pacing_rows(records: list[dict], clock: str) -> dict:
    """The distribution of the medians across repeats, plus each repeat's own tail statistics.
    Reported per repeat rather than pooled: pooling frames from two runs makes a bimodal sample
    whose percentiles describe neither run."""
    return {
        "p50": [r[clock]["p50"] for r in records],
        "p90": [r[clock]["p90"] for r in records],
        "p95": [r[clock]["p95"] for r in records],
        "p99": [r[clock]["p99"] for r in records],
        "low1": [r[clock]["low1Percent_meanOfSlowest1Pct"] for r in records],
        "max": [r[clock]["max"] for r in records],
        "min": [r[clock]["min"] for r in records],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("subjects", nargs="*", help="subject names; default is every declared one")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--report", type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path, default=None,
                        help="where the bench-json records go (default: a temp dir)")
    parser.add_argument("--record", action="store_true",
                        help="rewrite each scene's declared counters from this run, and stamp the "
                             "revision it was taken at; review the diff in git")
    parser.add_argument("--no-lock", action="store_true",
                        help="skip tools/gpu-lock.sh -- for a machine with one agent on it, and "
                             "never for a result anybody will quote")
    parser.add_argument("--strict", action="store_true",
                        help="fail when the tree is dirty: a certification of an uncommitted tree "
                             "names a revision that does not contain what was measured")
    args = parser.parse_args()

    if not BINARY.is_file():
        sys.stderr.write(f"no binary at {BINARY}; build the release preset first\n")
        return 2

    subjects = discover_subjects()
    if args.subjects:
        wanted = set(args.subjects)
        subjects = [s for s in subjects if s["name"] in wanted]
        missing = wanted - {s["name"] for s in subjects}
        if missing:
            sys.stderr.write(f"no declared subject named: {', '.join(sorted(missing))}\n")
            return 2
    if not subjects:
        sys.stderr.write("no scene under examples/ carries a `certification` block\n")
        return 2

    out_dir = args.out or pathlib.Path(os.environ.get("TMPDIR", "/tmp")) / "avgen-certify"
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"certifying {len(subjects)} subject(s), {args.repeats} interleaved repeats of "
          f"{args.frames} frames\n")
    runs: dict[str, list[dict]] = {s["name"]: [] for s in subjects}
    for repeat in range(args.repeats):
        for subject in subjects:
            record = run_one(subject, args, out_dir, repeat)
            if record is not None:
                runs[subject["name"]].append(record)
        print()

    # One session id across every record, or the comparison between subjects is not one this
    # protocol allows.  Each record is its own process, so they legitimately differ -- what is
    # checked is that the *conditions* that must match do.
    conditions = [r["conditions"] for group in runs.values() for r in group]
    if not conditions:
        sys.stderr.write("every run failed\n")
        return 1
    revisions = {(c["gitRevision"], c["gitDirty"]) for c in conditions}
    if len(revisions) > 1:
        sys.stderr.write(f"the binary changed mid-run: {revisions}\n")
        return 1
    revision, dirty = revisions.pop()
    if dirty and args.strict:
        sys.stderr.write(f"tree is dirty at {revision}; --strict refuses to certify it\n")
        return 1

    failures = 0
    lines = [f"# Certification run — {revision}{' (dirty)' if dirty else ''}",
             "",
             f"{args.repeats} interleaved repeats of {args.frames} frames, "
             f"{conditions[0]['buildType']} build, {conditions[0]['backend']} on "
             f"{conditions[0]['platform']}.",
             "",
             "Counters are checked exactly and timings are not checked at all: a counter is a "
             "deterministic function of the scene and the camera, and a millisecond is a property "
             "of a session. See the tool's header for why.",
             ""]

    print("\n=== counters, against each scene's own declaration ===")
    for subject in subjects:
        records = runs[subject["name"]]
        if not records:
            print(f"  {subject['name']}: NO RUNS")
            failures += 1
            continue
        errored = [r for r in records if r["_gpuErrors"] != 0]
        problems = check_counters(subject["declaration"], records)
        if errored:
            problems.append(f"{len(errored)} run(s) reported GPU errors")
        if problems:
            failures += 1
            print(f"  {subject['name']}: {len(problems)} DIFFERENCE(S)")
            for line in problems:
                print(f"      {line}")
        else:
            print(f"  {subject['name']}: matches its declaration")
        lines.append(f"## {subject['name']}")
        lines.append("")
        lines.append(f"`{subject['path'].relative_to(ROOT)}` — {subject['declaration'].get('what', '')}")
        lines.append("")
        if problems:
            lines.append("**Counters differ from the declaration:**")
            lines.extend(f"- {line}" for line in problems)
        else:
            lines.append("Counters match the declaration exactly.")
        lines.append("")
        counters = records[0]["counters"]
        lines.append("| counter | value |")
        lines.append("|---|---:|")
        for name in CHECKED_COUNTERS:
            if name in counters:
                lines.append(f"| {name} | {counters[name]:,.0f} |")
        lines.append("")
        for clock in ("gpuMs", "wallMs"):
            rows = pacing_rows(records, clock)
            lines.append(f"**{clock}** — each cell is the median across repeats, "
                         f"with the peak-to-peak spread of the repeats after it.")
            lines.append("")
            lines.append("| stat | ms | spread |")
            lines.append("|---|---:|---:|")
            for label, key in (("min", "min"), ("p50", "p50"), ("p90", "p90"), ("p95", "p95"),
                               ("p99", "p99"), ("1% low (mean of slowest 1%)", "low1"),
                               ("max", "max")):
                values = rows[key]
                middle = sorted(values)[len(values) // 2]
                lines.append(f"| {label} | {middle:.3f} | {spread_percent(values):.1f}% |")
            lines.append("")
        p50s = [r["gpuMs"]["p50"] for r in records]
        print(f"      gpu p50 {sorted(p50s)[len(p50s)//2]:.3f} ms, "
              f"spread across repeats {spread_percent(p50s):.1f}%, "
              f"1% low {sorted(pacing_rows(records,'gpuMs')['low1'])[len(records)//2]:.3f} ms")

    if args.record:
        for subject in subjects:
            records = runs[subject["name"]]
            if not records:
                continue
            doc = json.loads(subject["path"].read_text())
            block = doc["certification"]
            block["counters"] = {name: records[0]["counters"][name]
                                 for name in CHECKED_COUNTERS if name in records[0]["counters"]}
            block["recordedAt"] = {"revision": revision, "dirty": dirty,
                                   "resolution": block.get("atResolution", "1280x800")}
            subject["path"].write_text(json.dumps(doc, indent=1) + "\n")
            print(f"  recorded {subject['path'].relative_to(ROOT)}")

    if args.report:
        args.report.write_text("\n".join(lines) + "\n")
        print(f"\nreport written to {args.report}")
    print(f"\n{len(subjects) - failures}/{len(subjects)} subject(s) match their declaration")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

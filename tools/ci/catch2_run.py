#!/usr/bin/env python3
"""Run a Catch2 test binary for CI and say honestly what happened.

    tools/ci/catch2_run.py run --binary build/release/tests/avgen_tests --name cpu \
        --out ci-results/cpu --shards 3 --timeout-min 90
    tools/ci/catch2_run.py report --results-dir ci-results --job-results jobs.json

`run` executes the binary (optionally split into Catch2 shards that run in parallel), then reads
Catch2's streaming XML and the binary's OWN exit code (docs/testing.md, entry 18) and writes:

    <out>/result.json    machine-readable verdict (counts, failures, crashes, sanitizer reports)
    <out>/summary.md     the same, as Markdown for $GITHUB_STEP_SUMMARY
    <out>/shard-N.log    full console output of each shard (stdout + stderr)
    <out>/shard-N.xml    Catch2 XML reporter output
    <out>/shard-N.junit.xml  Catch2 JUnit reporter output

It exits non-zero if any shard failed, crashed, timed out, raised a sanitizer report, or if the
cases that ran do not add up to the cases the binary lists. What it deliberately does NOT do:

  * count a `[!shouldfail]` case as a failure. Catch2 reports it as `expectedFailures` and exits 0;
    docs/testing.md entry 8. It is listed as an expected failure instead.
  * trust a `FAILED:` line. A killed process prints one with no `with expansion:` (entry 4); the
    verdict here comes from the exit code, the XML and the signal, never from grepping text.
  * treat a missing verdict as a pass (entry 11). No closing `OverallResults` means the run did
    not finish, and the case that was open when it stopped is named.

`report` merges every `result.json` under a directory into the one run summary (`# AV Gen CI`).
Standard library only; Python 3.9+.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path

SAN_PATTERNS = [
    # (kind, regex over one log line). The first group is the report type.
    ("AddressSanitizer", re.compile(r"ERROR: AddressSanitizer: ([\w-]+)")),
    ("LeakSanitizer", re.compile(r"ERROR: LeakSanitizer: ([\w -]+)")),
    ("ThreadSanitizer", re.compile(r"WARNING: ThreadSanitizer: ([\w -]+?)(?: \(pid=\d+\))?$")),
    ("UndefinedBehaviorSanitizer", re.compile(r"(?:^|\s)(\S+:\d+:\d+): runtime error: (.*)$")),
]
MAX_FAILURE_DETAILS = 25


# ------------------------------------------------------------------------------------------------
# Running
# ------------------------------------------------------------------------------------------------

def list_case_count(binary: str, filters: list[str]) -> int | None:
    """The number of cases the binary itself says the selection contains."""
    proc = subprocess.run([binary, "--list-tests", *filters], capture_output=True, text=True)
    m = re.search(r"^(\d+) (?:matching )?test cases?", proc.stdout, re.M)
    return int(m.group(1)) if m else None


def load_exceptions(path: str) -> dict:
    """tools/ci/hosted-runner-exceptions.txt: {'exclude': {name: reason}, 'needs-assets': {...}}."""
    out = {"exclude": {}, "needs-assets": {}}
    if not path:
        return out
    for raw in Path(path).read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        kind, name, reason = (part.strip() for part in line.split("|", 2))
        if kind not in out:
            raise SystemExit(f"{path}: unknown kind '{kind}' in: {raw}")
        # Only an exclusion becomes a Catch2 spec; a needs-assets name is matched against the XML.
        if kind == "exclude" and ("," in name or '"' in name):
            raise SystemExit(f"{path}: '{name}' has a comma or quote; Catch2 would split it (testing.md 14)")
        out[kind][name] = reason
    return out


def build_spec(user_filter: str, excluded: list[str]) -> list[str]:
    """One Catch2 spec: the user's OR-ed tag terms, each AND-ed with every exclusion."""
    neg = "".join(f'~"{n}"' for n in excluded)
    if not user_filter:
        return [neg] if neg else []
    return [",".join(term + neg for term in user_filter.split(","))]


def run_shards(args) -> list[dict]:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    procs = []
    total = args.shard_total or args.shards
    for i in range(args.shards):
        g = args.shard_first + i   # the global Catch2 shard index
        tmp = out / f"tmp-{i}"
        tmp.mkdir(exist_ok=True)
        cmd = [args.binary, *args.spec,
               "--rng-seed", str(args.seed),
               "--reporter", f"console::out={out}/shard-{i}.console.txt::colour-mode=none",
               "--reporter", f"xml::out={out}/shard-{i}.xml",
               "--reporter", f"junit::out={out}/shard-{i}.junit.xml",
               "--durations", "yes"]
        if total > 1:
            cmd += ["--shard-count", str(total), "--shard-index", str(g)]
        # Each shard gets its own TMPDIR: tests write fixtures under temp_directory_path(), and two
        # processes of the same binary would otherwise share (and delete) each other's files.
        env = dict(os.environ, TMPDIR=str(tmp) + "/")
        log = open(out / f"shard-{i}.log", "wb")
        print(f"[catch2_run] shard {i}: {' '.join(cmd)}", flush=True)
        p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env,
                             cwd=args.cwd or None, start_new_session=True)
        procs.append({"index": i, "proc": p, "log": log, "start": time.monotonic(),
                      "timed_out": False, "end": None})

    deadline = time.monotonic() + args.timeout_min * 60
    while any(s["proc"].poll() is None for s in procs):
        for s in procs:
            if s["end"] is None and s["proc"].poll() is not None:
                s["end"] = time.monotonic()
        if time.monotonic() > deadline:
            for s in procs:
                if s["proc"].poll() is None:
                    s["timed_out"] = True
                    os.killpg(s["proc"].pid, signal.SIGTERM)
            grace = time.monotonic() + 30
            while any(s["proc"].poll() is None for s in procs) and time.monotonic() < grace:
                time.sleep(1)
            for s in procs:
                if s["proc"].poll() is None:
                    os.killpg(s["proc"].pid, signal.SIGKILL)
            break
        time.sleep(2)

    results = []
    for s in procs:
        rc = s["proc"].wait()   # the BINARY's own status, not a wrapper's (docs/testing.md 18)
        s["log"].close()
        end = s["end"] or time.monotonic()
        results.append({"index": s["index"], "returncode": rc, "timed_out": s["timed_out"],
                        "seconds": round(end - s["start"], 1)})
        print(f"[catch2_run] shard {s['index']}: exit {rc}"
              f"{' (TIMEOUT)' if s['timed_out'] else ''} after {results[-1]['seconds']} s", flush=True)
    return results


# ------------------------------------------------------------------------------------------------
# Reading the results
# ------------------------------------------------------------------------------------------------

def _text(el) -> str:
    return (el.text or "").strip() if el is not None else ""


def parse_xml(path: Path) -> dict:
    """Stream Catch2's XML. Tolerates a truncated file: the case open when it ends is named."""
    res = {"cases": [], "totals": None, "open_case": None, "complete": False, "seed": None}
    if not path.exists() or path.stat().st_size == 0:
        return res
    section_stack: list[str] = []
    current = None
    try:
        for event, el in ET.iterparse(path, events=("start", "end")):
            tag = el.tag
            if event == "start":
                if tag == "Catch2TestRun":
                    res["seed"] = el.get("rng-seed")
                elif tag == "TestCase":
                    current = {"name": el.get("name"), "tags": el.get("tags", ""),
                               "file": el.get("filename"), "line": el.get("line"),
                               "problems": [], "expected_problems": 0}
                    res["open_case"] = current
                    section_stack = []
                elif tag == "Section":
                    section_stack.append(el.get("name", ""))
                continue
            # end events
            if tag == "Section":
                if section_stack:
                    section_stack.pop()
            elif tag in ("Expression", "Exception", "FatalErrorCondition", "Failure") and current:
                if tag == "Expression" and el.get("success") == "true":
                    continue
                prob = {"kind": tag, "file": el.get("filename"), "line": el.get("line"),
                        "sections": [s for s in section_stack[1:]]}
                if tag == "Expression":
                    prob["macro"] = el.get("type")
                    prob["expression"] = _text(el.find("Original"))
                    prob["expansion"] = _text(el.find("Expanded"))
                    inner = el.find("Exception")
                    if inner is not None:
                        prob["message"] = _text(inner)
                else:
                    prob["message"] = _text(el)
                prob["info"] = [_text(i) for i in el.findall("Info")][:5]
                current["problems"].append(prob)
            elif tag == "Skip" and current is not None:
                current.setdefault("skip_messages", []).append(_text(el))
            elif tag == "OverallResult" and current is not None:
                current["success"] = el.get("success") == "true"
                current["skips"] = int(el.get("skips", "0") or 0)
                current["seconds"] = float(el.get("durationInSeconds", "0") or 0)
            elif tag == "TestCase" and current is not None:
                res["cases"].append(current)
                res["open_case"] = None
                current = None
                el.clear()
            elif tag == "OverallResultsCases":
                res["totals"] = {k: int(el.get(k, "0") or 0)
                                 for k in ("successes", "failures", "expectedFailures", "skips")}
            elif tag == "Catch2TestRun":
                res["complete"] = True
    except ET.ParseError:
        pass   # truncated: the process died mid-run. open_case says where.
    return res


def scan_log(path: Path) -> dict:
    """Sanitizer reports and anything else a human needs out of a raw log."""
    reports = []
    lines = path.read_text(errors="replace").splitlines() if path.exists() else []
    for n, line in enumerate(lines):
        for kind, rx in SAN_PATTERNS:
            m = rx.search(line)
            if not m:
                continue
            if kind == "UndefinedBehaviorSanitizer":
                rtype, where = m.group(2), m.group(1)
            else:
                rtype, where = m.group(1).strip(), ""
                for follow in lines[n + 1:n + 40]:
                    fm = re.search(r"#\d+ 0x[0-9a-f]+ in (.+)$", follow)
                    if fm and "sanitizer_common" not in follow and "asan_" not in follow \
                            and "tsan_" not in follow and "interceptor" not in follow:
                        where = fm.group(1).strip()
                        break
            excerpt = "\n".join(lines[n:n + 30])
            reports.append({"sanitizer": kind, "type": rtype, "where": where,
                            "line_in_log": n + 1, "excerpt": excerpt})
            break
    tail = "\n".join(lines[-40:])
    return {"sanitizer_reports": reports, "tail": tail}


def signal_name(rc: int) -> str:
    try:
        return signal.Signals(-rc).name
    except ValueError:
        return f"signal {-rc}"


def summarise(name: str, binary: str, listed: int | None, shard_status: list[dict], out: Path,
              label: str, seed: int, expected_label: str, exceptions: dict | None = None,
              excluded: list[str] | None = None) -> dict:
    exceptions = exceptions or {"exclude": {}, "needs-assets": {}}
    known = exceptions["needs-assets"]
    asset_failures, stale = [], []
    counts = Counter()
    failures, crashes, timeouts = [], [], []
    expected_failures, skip_reasons, slowest = [], Counter(), []
    san_reports = []
    cases_seen = 0
    asset_failures_by_shard: dict[int, int] = {}
    for st in shard_status:
        i = st["index"]
        x = parse_xml(out / f"shard-{i}.xml")
        log = scan_log(out / f"shard-{i}.log")
        for case in x["cases"]:
            cases_seen += 1
            slowest.append((case.get("seconds", 0.0), case["name"]))
            if not case.get("success", False) and case["name"] in known:
                # Ran, failed, and is documented as needing gitignored assets: reported, never
                # counted as passed, and does not fail the job.
                counts["needs_assets"] += 1
                asset_failures.append({"name": case["name"], "reason": known[case["name"]],
                                       "file": case["file"], "line": case["line"]})
                asset_failures_by_shard[i] = asset_failures_by_shard.get(i, 0) + 1
            elif not case.get("success", False):
                counts["failed"] += 1
                failures.append({**case, "shard": i})
            elif case["problems"]:
                # Failed assertions inside a case Catch2 still calls a success: [!shouldfail] or
                # [!mayfail]. Catch2's own verdict stands; they are listed, not counted as failures.
                counts["expected_failures"] += 1
                expected_failures.append(case["name"])
            elif case.get("skips", 0) > 0:
                counts["skipped"] += 1
                for msg in case.get("skip_messages", []) or ["(no message)"]:
                    skip_reasons[normalise_skip(msg)] += 1
            else:
                counts["passed"] += 1
                if case["name"] in known:
                    stale.append(case["name"])
        san_reports += [{**r, "shard": i} for r in log["sanitizer_reports"]]
        rc = st["returncode"]
        where = x["open_case"]["name"] if x["open_case"] else None
        if st["timed_out"]:
            # Catch2 handles our SIGTERM and closes the XML, recording the running case as a
            # FatalErrorCondition "failure" with no expansion (docs/testing.md entry 4). That case
            # is the one that was killed: attribute it to the timeout, not to the test.
            killed = [f for f in failures if f["shard"] == i and any(
                p["kind"] == "FatalErrorCondition" and "SIGTERM" in (p.get("message") or "")
                for p in f["problems"])]
            for f in killed:
                failures.remove(f)
                counts["failed"] -= 1
            where = where or (killed[0]["name"] if killed else None)
            timeouts.append({"shard": i, "test": where, "seconds": st["seconds"], "tail": log["tail"]})
        elif rc < 0 or rc > 255 or (rc != 0 and not x["complete"]):
            fatal = next((p for c in x["cases"] for p in c["problems"]
                          if p["kind"] == "FatalErrorCondition"), None)
            crashes.append({"shard": i, "returncode": rc,
                            "signal": signal_name(rc) if rc < 0 else None,
                            "test": where or (fatal and next(
                                (c["name"] for c in x["cases"] if fatal in c["problems"]), None)),
                            "fatal": fatal["message"] if fatal else None,
                            "tail": log["tail"]})
        elif not x["complete"]:
            crashes.append({"shard": i, "returncode": rc, "signal": None, "test": where,
                            "fatal": "the XML report never closed: the run did not finish",
                            "tail": log["tail"]})
        st["catch2_verdict_complete"] = x["complete"]
        st["catch2_totals"] = x["totals"]

    # Exit codes and the XML must agree. A disagreement is reported, never resolved in favour of
    # "passed" (docs/testing.md entries 5 and 18).
    disagreements = []
    for st in shard_status:
        t = st.get("catch2_totals")
        if t and st["returncode"] == 0 and t["failures"] > 0:
            disagreements.append(f"shard {st['index']}: exit 0 but Catch2 counted {t['failures']} failed cases")
        if t and st["returncode"] > 0 and t["failures"] == 0 and not st["timed_out"]:
            disagreements.append(f"shard {st['index']}: exit {st['returncode']} but Catch2 counted 0 failed cases")

    unexercised = (listed - cases_seen) if listed is not None else None
    if not listed:
        disagreements.append(f"the binary listed {listed} cases for this selection: an empty "
                             "selection is a mistake, not a pass")
    ok = (counts["failed"] == 0 and not crashes and not timeouts and not san_reports
          and not disagreements and (unexercised is None or unexercised == 0)
          and all(st["returncode"] == 0 or shard_explained(st, asset_failures_by_shard.get(st["index"], 0))
                  for st in shard_status))
    slowest.sort(reverse=True)
    result = {
        "name": name, "label": label, "binary": binary, "ok": ok, "rng_seed": seed,
        "listed": listed, "ran": cases_seen, "unexercised": unexercised,
        "passed": counts["passed"], "failed": counts["failed"], "skipped": counts["skipped"],
        "expected_failures": counts["expected_failures"], "expected_failure_names": expected_failures,
        "expected_label": expected_label,
        "skip_reasons": skip_reasons.most_common(),
        "seconds": max((st["seconds"] for st in shard_status), default=0),
        "shards": shard_status, "failures": failures[:MAX_FAILURE_DETAILS],
        "failures_total": len(failures), "failure_names": [f["name"] for f in failures],
        "crashes": crashes, "timeouts": timeouts, "sanitizer_reports": san_reports[:20],
        "sanitizer_reports_total": len(san_reports), "disagreements": disagreements,
        "slowest": [{"seconds": round(s, 1), "name": n} for s, n in slowest[:10]],
        "needs_assets": counts["needs_assets"], "needs_assets_failures": asset_failures,
        "needs_assets_stale": stale,
        "excluded": [{"name": n, "reason": exceptions["exclude"].get(n, "")} for n in (excluded or [])],
    }
    (out / "result.json").write_text(json.dumps(result, indent=2))
    (out / "summary.md").write_text(render_binary_md(result, level=2))
    return result


def shard_explained(st: dict, known_failed: int) -> bool:
    """A shard's non-zero exit is fully explained when Catch2 exits with exactly the number of
    failed cases and every one of them is a documented needs-assets case. Anything else -- a
    signal, a timeout, a different count -- is not explained."""
    t = st.get("catch2_totals")
    # Catch2 v3 exits 42 (TestFailureExitCode) when any assertion failed, whatever the count.
    return (st["returncode"] == 42 and not st["timed_out"] and t is not None
            and t["failures"] == known_failed and known_failed > 0)


def normalise_skip(msg: str) -> str:
    msg = " ".join(msg.split())
    # "no GPU adapter available: <driver text>" -> keep the stable half.
    return msg.split(": ")[0][:140] if msg.startswith("no GPU adapter") else msg[:140]


# ------------------------------------------------------------------------------------------------
# Rendering
# ------------------------------------------------------------------------------------------------

def fmt_seconds(s: float) -> str:
    s = int(round(s))
    return f"{s // 60}m {s % 60:02d}s" if s >= 60 else f"{s}s"


def md_escape(s: str) -> str:
    return (s or "").replace("|", "\\|").replace("\n", " ")


def verdict(r: dict) -> str:
    if r.get("report_only"):
        return "INFORMATIONAL (" + _verdict(r) + ", does not gate)"
    return _verdict(r)


def _verdict(r: dict) -> str:
    if r["ok"] and r.get("gate") == "sanitizer":
        return ("PASS (no sanitizer reports" + (f"; {r['failed']} assertion failures under the "
                "sanitizer, listed below, do not gate" if r["failed"] else "") + ")")
    if r["ok"]:
        return "PASS"
    if r["sanitizer_reports_total"]:
        return "SANITIZER FAILURE"
    if r["crashes"]:
        return "CRASH"
    if r["timeouts"]:
        return "TIMEOUT"
    return "FAIL"


def render_binary_md(r: dict, level: int = 2) -> str:
    h = "#" * level
    L = [f"{h} {r['label']}: {verdict(r)}", ""]
    L.append(f"`{r['binary']}` · rng-seed `{r['rng_seed']}` · {len(r['shards'])} shard(s)")
    L.append("")
    L.append("| Passed | Failed | Skipped | Failed: needs local assets | Expected failures | Excluded | Ran / listed | Duration |")
    L.append("|---:|---:|---:|---:|---:|---:|---:|---:|")
    L.append(f"| {r['passed']} | {r['failed']} | {r['skipped']} | {r.get('needs_assets', 0)} | {r['expected_failures']} | "
             f"{len(r.get('excluded', []))} | {r['ran']} / {r['listed'] if r['listed'] is not None else '?'} | {fmt_seconds(r['seconds'])} |")
    L.append("")
    if r.get("expected_label"):
        L.append(f"**{r['expected_label']}**")
        L.append("")
    if r["unexercised"]:
        L.append(f"**{r['unexercised']} listed cases never ran** (a crash, timeout or shard error "
                 f"stopped them). They are neither passed nor skipped.")
        L.append("")
    for s in r["sanitizer_reports"]:
        L.append(f"{h}# SANITIZER FAILURE: {s['sanitizer']} {s['type']}")
        L.append(f"- location: `{s['where'] or 'see excerpt'}` (shard {s['shard']}, log line {s['line_in_log']})")
        L.append("```")
        L.append(s["excerpt"][:3000])
        L.append("```")
    if r["sanitizer_reports_total"] > len(r["sanitizer_reports"]):
        L.append(f"...and {r['sanitizer_reports_total'] - len(r['sanitizer_reports'])} more sanitizer reports in the logs.")
    for c in r["crashes"]:
        sig = c["signal"] or f"exit {c['returncode']}"
        L.append(f"{h}# CRASH: {sig} in shard {c['shard']}")
        L.append(f"- executable: `{r['binary']}`")
        L.append(f"- test running when it died: `{c['test'] or 'unknown (no case open)'}`")
        if c["fatal"]:
            L.append(f"- Catch2: {c['fatal']}")
        L.append("<details><summary>last 40 log lines</summary>\n\n```\n" + c["tail"][-4000:] + "\n```\n</details>")
    for t in r["timeouts"]:
        L.append(f"{h}# TIMEOUT: shard {t['shard']} killed after {fmt_seconds(t['seconds'])}")
        L.append(f"- test running when it was killed: `{t['test'] or 'unknown'}`")
        L.append("- A killed Catch2 prints a `FAILED:` with no `with expansion:` (docs/testing.md entry 4); "
                 "that line is the kill, not a test result.")
    for d in r["disagreements"]:
        L.append(f"- **exit code / report disagreement:** {d}")
    if r["failures"]:
        L.append(f"{h}# Failures ({r['failures_total']})")
        for f in r["failures"]:
            L.append(f"{h}## {f['name']}")
            L.append(f"- source: `{f['file']}:{f['line']}` · tags `{f['tags']}` · shard {f['shard']}")
            for p in f["problems"][:3]:
                where = f"`{p['file']}:{p['line']}`"
                sect = (" · section: " + " / ".join(p["sections"])) if p["sections"] else ""
                if p["kind"] == "Expression":
                    L.append(f"- {where}{sect}")
                    L.append(f"  - expected: `{p['macro']}( {md_escape(p['expression'])} )`")
                    L.append(f"  - actual: `{md_escape(p['expansion'])}`")
                    if p.get("message"):
                        L.append(f"  - exception: {md_escape(p['message'])}")
                else:
                    L.append(f"- {where}{sect} **{p['kind']}**: {md_escape(p.get('message', ''))}")
                for info in p["info"]:
                    L.append(f"  - info: {md_escape(info)}")
            if len(f["problems"]) > 3:
                L.append(f"- ...{len(f['problems']) - 3} more failed assertions in this case")
        if r["failures_total"] > len(r["failures"]):
            rest = r["failure_names"][len(r["failures"]):]
            L.append(f"...and {len(rest)} more: " + ", ".join(f"`{n}`" for n in rest[:50]))
        L.append("")
    if r.get("needs_assets_failures"):
        L.append(f"<details><summary><b>{len(r['needs_assets_failures'])} cases failed because gitignored "
                 f"assets are absent on the runner</b> (tools/ci/hosted-runner-exceptions.txt). Not a pass: "
                 f"CI proves nothing about them.</summary>\n")
        for a in r["needs_assets_failures"]:
            L.append(f"- `{md_escape(a['name'])}` ({md_escape(a['reason'])}) `{rel(a['file'])}:{a['line']}`")
        L.append("\n</details>\n")
    if r.get("needs_assets_stale"):
        L.append("Listed as needs-assets but PASSED here (remove from the exceptions file): "
                 + ", ".join(f"`{n}`" for n in r["needs_assets_stale"]))
        L.append("")
    if r.get("excluded"):
        L.append("Excluded from this run (tools/ci/hosted-runner-exceptions.txt): "
                 + "; ".join(f"`{e['name']}` ({md_escape(e['reason'])})" for e in r["excluded"]))
        L.append("")
    if r["expected_failures"]:
        L.append(f"Expected failures (`[!shouldfail]`/`[!mayfail]`, not failures): "
                 + ", ".join(f"`{n}`" for n in r["expected_failure_names"]))
        L.append("")
    if r["skip_reasons"]:
        L.append("<details><summary>Skipped cases by reason</summary>\n")
        L.append("| Cases | Reason |\n|---:|---|")
        for reason, n in r["skip_reasons"][:40]:
            L.append(f"| {n} | {md_escape(reason)} |")
        L.append("\n</details>\n")
    if r["slowest"]:
        L.append("<details><summary>Slowest cases</summary>\n")
        L.append("| Seconds | Case |\n|---:|---|")
        for s in r["slowest"]:
            L.append(f"| {s['seconds']} | {md_escape(s['name'])} |")
        L.append("\n</details>\n")
    return "\n".join(L) + "\n"


def annotate(r: dict) -> None:
    """GitHub workflow commands: failures show up on the Checks page and in the PR diff."""
    for f in r["failures"]:
        p = next(iter(f["problems"]), None)
        file_, line = (p["file"], p["line"]) if p else (f["file"], f["line"])
        detail = (f"{p['macro']}( {p['expression']} ) with expansion: {p['expansion']}"
                  if p and p["kind"] == "Expression" else (p or {}).get("message", ""))
        print(f"::error file={rel(file_)},line={line},title=FAILED {f['name'][:120]}::{oneline(detail)}")
    for c in r["crashes"]:
        print(f"::error title=CRASH {c['signal'] or c['returncode']} ({r['label']})::"
              f"{r['binary']} died in '{c['test']}'. {oneline(c['fatal'] or '')}")
    for t in r["timeouts"]:
        print(f"::error title=TIMEOUT ({r['label']})::shard {t['shard']} killed while running '{t['test']}'")
    for s in r["sanitizer_reports"][:10]:
        print(f"::error title=SANITIZER FAILURE {s['sanitizer']} {s['type']}::{oneline(s['where'])}")
    for d in r["disagreements"]:
        print(f"::error title=Exit code disagrees with report ({r['label']})::{d}")
    if r["unexercised"]:
        print(f"::error title=Cases never ran ({r['label']})::{r['unexercised']} listed cases never ran")


def rel(path: str | None) -> str:
    if not path:
        return ""
    ws = os.environ.get("GITHUB_WORKSPACE", "")
    if ws and path.startswith(ws + "/"):
        return path[len(ws) + 1:]
    while path.startswith("../"):   # a build-relative __FILE__ (e.g. from a ccache base_dir)
        path = path[3:]
    return path


def oneline(s: str) -> str:
    return " ".join((s or "").split())[:400].replace("%", "%25")


def print_console(r: dict, out: Path) -> None:
    """What lands in the Actions log (and so in `gh run view --log-failed`)."""
    print(f"\n===== {r['label']}: {verdict(r)} =====")
    print(f"passed {r['passed']}  failed {r['failed']}  skipped {r['skipped']}  "
          f"needs-assets-failed {r.get('needs_assets', 0)}  excluded {len(r.get('excluded', []))}  "
          f"expected-failures {r['expected_failures']}  ran {r['ran']}/{r['listed']}  "
          f"{fmt_seconds(r['seconds'])}")
    for f in r["failures"]:
        print(f"\nFAILED: {f['name']}\n  test source: {f['file']}:{f['line']}")
        for p in f["problems"][:3]:
            if p["kind"] == "Expression":
                print(f"  {p['file']}:{p['line']}: {p['macro']}( {p['expression']} )\n"
                      f"    with expansion: {p['expansion']}")
            else:
                print(f"  {p['file']}:{p['line']}: {p['kind']}: {p.get('message', '')}")
    for c in r["crashes"]:
        print(f"\nCRASH: {r['binary']} {c['signal'] or 'exit ' + str(c['returncode'])} "
              f"while running '{c['test']}'\n--- last log lines (shard {c['shard']}) ---\n{c['tail']}")
    for t in r["timeouts"]:
        print(f"\nTIMEOUT: shard {t['shard']} killed while running '{t['test']}'")
    for s in r["sanitizer_reports"][:5]:
        print(f"\nSANITIZER FAILURE: {s['sanitizer']} {s['type']} at {s['where']}\n{s['excerpt']}")
    for d in r["disagreements"]:
        print(f"\nEXIT CODE DISAGREEMENT: {d}")
    print(f"\nfull logs: {out}/shard-*.log (uploaded as the run's test-results artifact)")


def cmd_run(args) -> int:
    exceptions = load_exceptions(args.exceptions)
    # Only exclusions that name exactly one case of this binary AND fall inside this selection.
    # A stale one is reported, not silently kept.
    excluded = []
    for name in exceptions["exclude"]:
        n = list_case_count(args.binary, [f'"{name}"'])
        if n != 1:
            if n:
                print(f"::warning title=Ambiguous exclusion::'{name}' matches {n} cases in {args.binary}")
            continue
        terms = args.filter.split(",") if args.filter else [""]
        if list_case_count(args.binary, [",".join(f'{t}"{name}"' for t in terms)]):
            excluded.append(name)
    args.spec = build_spec(args.filter, excluded)
    base = list_case_count(args.binary, build_spec(args.filter, []))
    listed = list_case_count(args.binary, args.spec)
    print(f"[catch2_run] {args.binary}: {base} cases selected by {args.filter or '(default set)'}, "
          f"{listed} after {len(excluded)} documented exclusion(s)")
    if base is not None and listed is not None and base - listed != len(excluded):
        # The selector could not report a miss (testing.md 14): refuse rather than under-measure.
        print(f"::error title=Exclusion arithmetic::{base} - {len(excluded)} != {listed}; refusing to run")
        return 2
    if args.shard_total and args.shard_total != args.shards:
        # Part of a wider split across jobs: this job's own cases, as the binary itself lists them.
        listed = sum(list_case_count(args.binary, args.spec + ["--shard-count", str(args.shard_total),
                                                             "--shard-index", str(args.shard_first + k)]) or 0
                     for k in range(args.shards))
        print(f"[catch2_run] this job runs global shards {args.shard_first}..{args.shard_first + args.shards - 1}"
              f" of {args.shard_total}: {listed} cases")
    statuses = run_shards(args)
    r = summarise(args.name, args.binary, listed, statuses, Path(args.out), args.label, args.seed,
                  args.expected_label, exceptions, excluded)
    r["report_only"] = args.report_only
    r["gate"] = args.gate
    if args.gate == "sanitizer":
        # TSan: the job exists to find races. Assertion failures under a 5-15x slowdown are mostly
        # wall-clock waits and ceilings that do not hold; they are listed, loudly, but only a
        # sanitizer report, a crash, a timeout or an unexercised case decides the verdict.
        r["ok"] = (not r["sanitizer_reports_total"] and not r["crashes"] and not r["timeouts"]
                   and not r["unexercised"] and r["listed"])
    (Path(args.out) / "result.json").write_text(json.dumps(r, indent=2))
    (Path(args.out) / "summary.md").write_text(render_binary_md(r, level=2))
    print_console(r, Path(args.out))
    annotate(r)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as fh:
            fh.write((Path(args.out) / "summary.md").read_text())
    if r["ok"]:
        return 0
    # Report-only mode: the verdict is recorded (and shown), but the step does not fail. Used only
    # by jobs whose result is documented as not authoritative (see docs/development/ci.md).
    return 0 if args.report_only else 1


# ------------------------------------------------------------------------------------------------
# The run-wide summary
# ------------------------------------------------------------------------------------------------

def cmd_report(args) -> int:
    root = Path(args.results_dir)
    results = [json.loads(p.read_text()) for p in sorted(root.rglob("result.json"))]
    meta = {}
    for p in sorted(root.rglob("meta.json")):
        meta.update(json.loads(p.read_text()))
    jobs = json.loads(Path(args.job_results).read_text()) if args.job_results else {}
    L = ["# AV Gen CI", ""]
    build = meta.get("build", {})
    L.append(f"## Build: {build.get('status', 'unknown').upper()}")
    L.append("")
    if build:
        L.append(f"- runner: {build.get('runner', '?')} · macOS {build.get('macos', '?')} · {build.get('arch', '?')}")
        L.append(f"- compiler: {build.get('compiler', '?')} · Xcode {build.get('xcode', '?')} · SDK {build.get('sdk', '?')}")
        L.append(f"- CMake {build.get('cmake', '?')} · preset `{build.get('preset', '?')}` · "
                 f"configure {fmt_seconds(build.get('configure_seconds', 0))} · build {fmt_seconds(build.get('build_seconds', 0))}")
        L.append(f"- caches: CPM `{build.get('cpm_cache', '?')}` · ccache `{build.get('ccache', '?')}`")
        L.append("")
    L.append("## Tests")
    L.append("")
    if results:
        L.append("| Suite | Result | Passed | Failed | Skipped | Failed: needs assets | Expected fail | Excluded | Ran / listed | Duration |")
        L.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|")
        for r in results:
            label = r["label"] + (f" ({r['expected_label']})" if r.get("expected_label") else "")
            L.append(f"| {label} | **{verdict(r)}** | {r['passed']} | {r['failed']} | {r['skipped']} | "
                     f"{r.get('needs_assets', 0)} | {r['expected_failures']} | {len(r.get('excluded', []))} | "
                     f"{r['ran']} / {r['listed']} | {fmt_seconds(r['seconds'])} |")
        L.append("")
        for r in results:
            if r["skipped"]:
                top = "; ".join(f"{n}× {reason}" for reason, n in r["skip_reasons"][:4])
                L.append(f"- **{r['label']}: {r['skipped']} skipped** — {md_escape(top)}")
            if r.get("needs_assets"):
                L.append(f"- **{r['label']}: {r['needs_assets']} failed for lack of gitignored assets** "
                         f"(documented; not a pass). Plus ~55 cases that pass without asserting when "
                         f"assets/aliens is absent: see docs/development/ci.md, Coverage gaps.")
            if r.get("report_only"):
                L.append(f"- **{r['label']} is informational on hosted runners**: its result does not "
                         f"gate the run. See docs/development/ci.md, GPU.")
        L.append("")
    else:
        L.append("No test results were produced (the build or an earlier step failed).")
        L.append("")
    if jobs:
        L.append("## Jobs")
        L.append("")
        L.append("| Job | Result |")
        L.append("|---|---|")
        for name, res in jobs.items():
            L.append(f"| {name} | {res} |")
        L.append("")
    bad = [r for r in results if not r["ok"]]
    if bad:
        L.append("## Failures")
        L.append("")
        for r in bad:
            body = render_binary_md(r, level=3)
            L.append(body.split("\n", 1)[1] if body.startswith("###") else body)
    run_url = os.environ.get("GITHUB_SERVER_URL", "") + "/" + os.environ.get("GITHUB_REPOSITORY", "") \
        + "/actions/runs/" + os.environ.get("GITHUB_RUN_ID", "")
    L.append("## Artifacts")
    L.append("")
    L.append(f"Test results (Catch2 XML + JUnit + full logs per shard) and build logs are attached to "
             f"[this run]({run_url}#artifacts). `gh run download {os.environ.get('GITHUB_RUN_ID', '<run-id>')}` fetches them.")
    text = "\n".join(L) + "\n"
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as fh:
            fh.write(text)
    else:
        print(text)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--binary", required=True)
    r.add_argument("--name", required=True, help="short id, e.g. cpu, gpu")
    r.add_argument("--label", required=True, help="human name shown in the summary")
    r.add_argument("--out", required=True)
    r.add_argument("--shards", type=int, default=1, help="shards run in parallel by this invocation")
    r.add_argument("--shard-total", type=int, default=0,
                   help="total shards across all jobs (default: --shards); needs the same --seed everywhere")
    r.add_argument("--shard-first", type=int, default=0, help="global index of this invocation's first shard")
    r.add_argument("--timeout-min", type=float, default=120)
    r.add_argument("--seed", type=int, default=int(time.time()) % 2**31)
    r.add_argument("--filter", default="",
                   help="ONE Catch2 test spec, e.g. '~[golden]'. Tags only: a name with a comma splits.")
    r.add_argument("--expected-label", default="", help="caveat shown beside the suite name")
    r.add_argument("--report-only", action="store_true")
    r.add_argument("--gate", choices=["all", "sanitizer"], default="all",
                   help="sanitizer: only sanitizer reports, crashes, timeouts and unexercised cases fail")
    r.add_argument("--exceptions", default="", help="tools/ci/hosted-runner-exceptions.txt")
    r.add_argument("--cwd", default="")
    rp = sub.add_parser("report")
    rp.add_argument("--results-dir", required=True)
    rp.add_argument("--job-results", default="")
    args = ap.parse_args()
    return cmd_run(args) if args.cmd == "run" else cmd_report(args)


if __name__ == "__main__":
    sys.exit(main())

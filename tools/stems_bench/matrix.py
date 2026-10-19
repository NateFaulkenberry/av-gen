#!/usr/bin/env python3
"""Summarise the stems-multichannel live benchmark matrix (M8): one row per run, from the live-profile JSON and the
last `live input sources:` log line of the same run.

    python3 tools/stems_bench/matrix.py build/bench/m8
"""
import json
import re
import sys
from pathlib import Path

ARMS = {
    "A1": "BlackHole 2ch, no sources (the existing path)",
    "A1b": "BlackHole 2ch, 2 sources, hits on 1 (Core Audio path)",
    "A2": "synthetic, 4 sources",
    "A3": "synthetic, 8 sources",
    "A4": "synthetic, 8 sources, hits on all",
    "A5": "synthetic, 8 sources, hits on 2",
    "A6": "A4 + MIDI + projection",
}
LINE = re.compile(r"live input sources: (\d+) of (\d+) active; callback ([\d.]+) us mean, ([\d.]+) us max; drops (\d+) "
                  r"gaps (\d+) overloads (\d+); analysis ([\d.]+) us per source-hop, pass max (\d+) us, ([\d.]+)% of a "
                  r"core; onset latency p50 ([\d.]+) ms p99 ([\d.]+) ms max ([\d.]+) ms over (\d+) events")


def main():
    root = Path(sys.argv[1])
    print("| scene | arm | rep | level | frame p50/p99 | cpu work p50/p99 | gpu p50/p95/p99 | misses | max RSS MB | "
          "callback mean/max us | drops/gaps/ovl | analysis us/src-hop | core % | onset latency p50/p99 ms |")
    print("|" + "---|" * 14)
    for p in sorted(root.glob("*-A*-r*.json")):
        m = re.match(r"(.+)-(A\d+b?)-r(\d)", p.stem)
        scene, arm, rep = m.group(1), m.group(2), m.group(3)
        d = json.loads(p.read_text())
        f = d["frame"]
        txt = p.with_suffix(".txt").read_text(errors="replace")
        rss = re.search(r"(\d+)\s+maximum resident set size", txt)
        lines = LINE.findall(txt)
        live = "| - | - | - | - | - |"
        if lines:
            a, n, cm, cx, dr, ga, ov, us, pm, core, lp50, lp99, lmax, ev = lines[-1]
            live = f"| {cm}/{cx} | {dr}/{ga}/{ov} | {us} | {core} | {lp50}/{lp99} ({ev} ev) |"
        print(f"| {scene} | {arm} | {rep} | {d['conditions']['quality']['level']} | "
              f"{f['frameMs']['p50']:.1f}/{f['frameMs']['p99']:.1f} | {f['cpuWorkMs']['p50']:.2f}/{f['cpuWorkMs']['p99']:.2f} | "
              f"{f['gpuMs']['p50']:.1f}/{f['gpuMs']['p95']:.1f}/{f['gpuMs']['p99']:.1f} | "
              f"{f['budget']['deadlineMisses']}/{f['budget']['frames']} | "
              f"{int(rss.group(1)) / 1e6 if rss else 0:.0f} {live}")


if __name__ == "__main__":
    main()

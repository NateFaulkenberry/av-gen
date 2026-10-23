#!/usr/bin/env python3
"""Check that the response levels keep their range on real, mastered music (ADR-1510).

Runs `avgen --sonic-trace` over Feline Footwear, Trench and Hyperborea (assets/audio; a track that is absent is
skipped, and with none present the check is skipped with exit 0), at the default Response controls, and prints the
p10 / p50 / p90 of response.bass, level, sustain, levelSlow and intensity from 10 s in to 10 s before the end, with
the share of frames above 0.9 and the beat-rate ripple -- and the transient envelopes beside them for comparison. Exits 1 when a level misses
its target.

Why it exists: ADR-1062's fixed -60..-12 dBFS window read every frame of a commercial master as 1 for months while the
suite passed, because the tests used a moderate synthetic kit and every scene had been tuned against the pinned
values (docs/development/testing.md, "A moderate test signal hid a saturated one"). Run it before handing back any
work that touches the response chain, the analyser's levels, or routes on response.*:

    python3 tools/check_response_range.py                 # build/release/src/avgen, every track present
    python3 tools/check_response_range.py --gain -24      # the same records 24 dB down (a quiet interface channel)
    python3 tools/check_response_range.py --avgen build/debug/src/avgen --tracks trench
"""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import json
import os
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TRACKS = {"feline": "feline-footwear.wav", "trench": "trench.wav", "hyperborea": "hyperborea.wav"}
LEVELS = ["bass", "level", "sustain", "levelSlow", "intensity"]
ENVELOPES = ["onsetEnv", "lowEnv"]  # for comparison (ADR-1620: a mix has no kick or hat class)

# The targets (ADR-1510). Every level: not a constant near the top (a capped share of frames above 0.9, p50 at
# most 0.75-0.8), not dead (p50 at least 0.3-0.4), a real spread between p10 and p90 -- and not a second kick
# envelope: the RIPPLE, the median over 0.5 s windows of the signal's range inside the window, is bounded (a body that
# drops to zero between two kicks fails here as a pinned one fails above). onsetEnv's ripple is printed beside them for
# scale (about 0.5-0.6 on these tracks). The levels rest high on a track that holds one loudness and dip at its
# breakdowns, so p10 is bounded loosely.
TARGETS = {
    #            p10 max  p50 min  p50 max  p90 max  >0.9 max  p90-p10 min  ripple max
    "bass":      (0.60,   0.35,    0.75,    0.92,    0.10,     0.20,        0.25),
    "level":     (0.60,   0.35,    0.75,    0.92,    0.10,     0.15,        0.20),
    "sustain":   (0.50,   0.30,    0.70,    0.90,    0.10,     0.10,        0.15),
    "levelSlow": (None,   0.40,    0.80,    0.90,    0.10,     0.08,        0.10),
    "intensity": (None,   0.40,    0.80,    0.90,    0.10,     0.05,        0.05),
}


def ripple(times: list[float], values: list[float], window: float = 0.5) -> float:
    """The median over consecutive `window`-second windows of max - min inside each: beat-rate movement."""
    spans, start, lo, hi = [], None, 0.0, 0.0
    for t, v in zip(times, values):
        if start is None or t - start >= window:
            if start is not None:
                spans.append(hi - lo)
            start, lo, hi = t, v, v
        else:
            lo, hi = min(lo, v), max(hi, v)
    return percentile(spans, 0.5)


def percentile(values: list[float], q: float) -> float:
    s = sorted(values)
    if not s:
        return 0.0
    k = (len(s) - 1) * q
    lo, hi = int(k), min(int(k) + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def attenuate(src: Path, dst: Path, gain_db: float) -> None:
    """A copy of a 16- or 24-bit PCM wav, `gain_db` down (pure Python: no numpy needed)."""
    with wave.open(str(src)) as w:
        params = w.getparams()
        raw = w.readframes(params.nframes)
    width = params.sampwidth
    if width not in (2, 3):
        raise SystemExit(f"{src}: {8 * width}-bit wav is not supported for --gain")
    g = 10.0 ** (gain_db / 20.0)
    out = bytearray(len(raw))
    lo, hi = -(1 << (8 * width - 1)), (1 << (8 * width - 1)) - 1
    for i in range(0, len(raw), width):
        v = int.from_bytes(raw[i:i + width], "little", signed=True)
        out[i:i + width] = max(lo, min(hi, round(v * g))).to_bytes(width, "little", signed=True)
    with wave.open(str(dst), "wb") as o:
        o.setparams(params)
        o.writeframes(bytes(out))


def trace(avgen: Path, audio: Path, work: Path, name: str, response: dict) -> dict[str, list[float]]:
    base = json.loads((ROOT / "examples/sonic-garden/sonic-live.json").read_text())
    sonic = base["sonic"]
    sonic["response"] = response
    project = {
        "format": "avgen-project", "version": 4, "app": {"name": f"response range {name}"},
        "assets": {"scene": {"kind": "composition", "path": str(ROOT / "examples/sonic-garden/sonic-live.scene.json")},
                   "audio": {"path": str(audio)}},
        "sonic": sonic, "render": {"width": 640, "height": 360, "fps": 30},
    }
    pj = work / f"{name}.json"
    pj.write_text(json.dumps(project))
    out = work / f"{name}.csv"
    r = subprocess.run([str(avgen), "--project", str(pj), "--sonic-trace", str(out)], cwd=ROOT,
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"avgen --sonic-trace failed on {name} ({r.returncode}):\n{r.stdout[-1500:]}{r.stderr[-1500:]}")
    with out.open() as f:
        rows = list(csv.DictReader(f))
    t = [float(x["time"]) for x in rows]
    end = t[-1]
    keep = [i for i, x in enumerate(t) if 10.0 <= x <= end - 10.0]
    out = {s: [float(rows[i]["response." + s]) for i in keep] for s in LEVELS + ENVELOPES}
    out["time"] = [t[i] for i in keep]
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--avgen", type=Path, default=ROOT / "build/release/src/avgen")
    ap.add_argument("--tracks", nargs="*", default=list(TRACKS), choices=list(TRACKS))
    ap.add_argument("--gain", type=float, default=0.0, help="play the tracks this many dB down (e.g. -24)")
    ap.add_argument("--response", default="{}", help="a sonic.response JSON object (controls or settings)")
    a = ap.parse_args()
    if not a.avgen.exists():
        print(f"check_response_range: no avgen at {a.avgen}; build it first", file=sys.stderr)
        return 2
    present = {k: ROOT / "assets/audio" / TRACKS[k] for k in a.tracks if (ROOT / "assets/audio" / TRACKS[k]).exists()}
    for k in a.tracks:
        if k not in present:
            print(f"SKIP {k}: assets/audio/{TRACKS[k]} is absent (assets are not in the repository)")
    if not present:
        print("check_response_range: no test tracks present; skipped")
        return 0
    response = json.loads(a.response)
    failures: list[str] = []
    work_root = ROOT / "build"
    work_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="response-range-", dir=work_root) as tmp:
        work = Path(tmp)
        audio = {}
        for k, p in present.items():
            if a.gain != 0.0:
                audio[k] = work / f"{k}{a.gain:+.0f}dB.wav"
                attenuate(p, audio[k], a.gain)
            else:
                audio[k] = p
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as ex:
            futures = {k: ex.submit(trace, a.avgen, audio[k], work, k, response) for k in present}
            results = {k: f.result() for k, f in futures.items()}
    print(f"response levels, {a.gain:+.0f} dB input, response {json.dumps(response)}; 10 s in to 10 s before the end")
    for k, sig in results.items():
        print(k)
        for s in LEVELS + ENVELOPES:
            v = sig[s]
            p10, p50, p90 = percentile(v, 0.1), percentile(v, 0.5), percentile(v, 0.9)
            above = sum(1 for x in v if x > 0.9) / max(len(v), 1)
            rip = ripple(sig["time"], v)
            bad = []
            if s in TARGETS:
                t10, t50lo, t50hi, t90, tabove, tspread, tripple = TARGETS[s]
                if t10 is not None and p10 > t10:
                    bad.append(f"p10 {p10:.2f} > {t10}")
                if p50 < t50lo or p50 > t50hi:
                    bad.append(f"p50 {p50:.2f} outside {t50lo}-{t50hi}")
                if p90 > t90:
                    bad.append(f"p90 {p90:.2f} > {t90}")
                if above > tabove:
                    bad.append(f"{above:.0%} of frames > 0.9 (max {tabove:.0%})")
                if p90 - p10 < tspread:
                    bad.append(f"spread {p90 - p10:.2f} < {tspread}")
                if rip > tripple:
                    bad.append(f"ripple {rip:.2f} > {tripple} (a beat-rate envelope, not a level)")
            mark = "" if s in TARGETS else "   (for comparison)"
            verdict = ("  FAIL: " + "; ".join(bad)) if bad else ("  ok" if s in TARGETS else "")
            print(f"  response.{s:<10} p10 {p10:.2f}  p50 {p50:.2f}  p90 {p90:.2f}  >0.9 {above:4.0%}  ripple {rip:.2f}{verdict}{mark}")
            failures += [f"{k} response.{s}: {b}" for b in bad]
    if failures:
        print(f"\n{len(failures)} target(s) missed:")
        for f in failures:
            print("  " + f)
        return 1
    print("\nall response levels within their targets")
    return 0


if __name__ == "__main__":
    sys.exit(main())

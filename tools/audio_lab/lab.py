#!/usr/bin/env python3
"""The Audio Response Lab's end-to-end run (docs/prototypes/audio-response-lab, ADR-1720).

    python3 tools/audio_lab/lab.py run [--fixtures DIR] [--out DIR] [--only sparse,isolation] [--avgen PATH]
    python3 tools/audio_lab/lab.py evaluate <run-dir>/<fixture>     # re-score one fixture's traces

For each fixture (tools/audio_lab/make_fixtures.py) it:
  1. writes a project: the lab's own (examples/audio-response-lab/audio-response-lab.json) with the fixture's mix as
     its audio and, for the isolation fixture, its stems as analysis stems with their roles; `sonic.live` off.
     Ground truth never enters the project: the manifest is read only by step 3.
  2. runs the PRODUCTION pipeline offline through `avgen --sonic-trace --trace-finals` twice:
       hop.csv  at 93.75 fps = one row per 512-sample analysis hop (the analysis clock's resolution)
       frame.csv at 30 fps   = one row per render frame of the 1080p30 target, with every route target's final
     (src/app/sonic_trace_cli.cpp: Engine::update per row, the bus and the parameter finals after it).
  3. scores the traces against the manifest and writes results.json (every raw measurement) and summary.md.

Clock conventions (01-plan.md §4): a trace row's `time` is the render instant t = row / fps; the offline engine
publishes every analysis frame whose WINDOW CENTRE is at or before t (2048-sample Hann window, 512 hop at 48 kHz:
the centre is 1024 samples = 21.3 ms after the window's first sample). An event's detection time is the first row
it appears in. Errors are detection time minus the ground-truth onset time (sample / rate): negative means the
centre-stamped file path published before the onset sample (it can, by up to half a window); live input adds that
half window back (the frame exists only once its last sample is captured).
"""

import argparse
import bisect
import csv
import json
import math
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LAB_PROJECT = REPO / "examples" / "audio-response-lab" / "audio-response-lab.json"
DEFAULT_AVGEN = REPO / "build" / "release" / "src" / "avgen"
DEFAULT_FIXTURES = REPO / "build" / "release" / "test-fixtures" / "audio-lab"
DEFAULT_REPLAY = REPO / "build" / "release" / "tools" / "avgen_audio_lab_replay"

RATE = 48000
HOP = 512
WINDOW = 2048
HOP_FPS = RATE / HOP           # 93.75: one trace row per analysis hop
FRAME_FPS = 30.0               # the live target
HOP_S = HOP / RATE             # 10.667 ms
HALF_WINDOW_S = WINDOW / 2 / RATE  # 21.333 ms

# ---------------------------------------------------------------------------------------------------------------------
# Thresholds, derived (01-plan.md §6). The match window for an onset is the earliest the centre-stamped file path can
# publish an attack (half a window before it) to the latest a causal detector with a decision delay of D hops can
# (one hop to see it, D hops to decide). ADR-1060's onset decides on the hop after the rise (D = 1); it runs with a
# 60 ms refractory, so two attacks closer than that are one event by design.
MATCH_EARLY_S = HALF_WINDOW_S + 0.002     # -23.3 ms
MATCH_LATE_S = 3 * HOP_S + 0.002           # +34.0 ms (one hop to land in a window centre, one to decide, one spare)
CLUSTER_S = 0.030                          # GT attacks closer than this are one attack (below the 60 ms refractory)
REFRACTORY_S = 0.060
BEAT_TOL_S = 0.070                         # MIREX beat F-measure tolerance
# The live path (ADR-1725's replay): a frame exists once its last sample arrives (centre + half a window), so nothing
# is early; the latest is half a window, the decision (two hops, one spare) and the wait for the next render frame.
LIVE_EARLY_S = 0.002
LIVE_LATE_S = HALF_WINDOW_S + 3 * HOP_S + 1.0 / 30.0 + 0.002     # 87.7 ms
LOCK_PHASE = 0.10                          # |phase error| under this (of a beat) at a beat counts as locked
LOCK_BEATS = 4                             # ... for this many consecutive intended beats
SETTLE_S = 1.5                             # after this long in silence every level and envelope must be at rest
SUSTAIN_GRACE_S = 0.15                     # a sustained note's first 150 ms may hold its own (one) onset
LEVEL_REST = 0.02                          # "at rest": below this
QUIET_GUARD_S = 0.030                      # a silence ends this early: the next attack may publish 21 ms before it
# How long each signal is DOCUMENTED to take from full scale to LEVEL_REST once the sound stops (its own release
# constants), so "stale" is judged against what the code promises, not a guess. Plan §6.
LN_REST = math.log(1.0 / LEVEL_REST)       # 3.9 time constants
DOCUMENTED_REST_S = {
    # ADR-1510: a level's follower falls in dB (one-pole, release R) from the top of its window towards the -60 dB
    # gate and reads 0 once it crosses the window's bottom. The worst case is the reference at its floor (-40 dB):
    # t = R ln((top - gate) / (top - span - gate)) with top = -40 + topDb.
    "response.level": 1.0 * math.log((-40 + 6.5 + 60) / (-40 + 6.5 - 12.0 + 60)),   # 0.60 s
    "response.bass": 1.2 * math.log((-40 + 6.5 + 60) / (-40 + 6.5 - 15.0 + 60)),    # 1.00 s
    "response.sustain": 0.9 * LN_REST,     # sustainRelease 0.9 s
    "response.onsetEnv": 0.09 * LN_REST,   # hitRelease onset 0.09 s
    "response.lowEnv": 0.18 * LN_REST,     # hitRelease low 0.18 s
    # ADR-1723: the fast mid and high levels, the same dB-follower rule as the bass, at their own releases
    "response.mid": 0.15 * math.log((-40 + 6.5 + 60) / (-40 + 6.5 - 15.0 + 60)),
    "response.high": 0.12 * math.log((-40 + 6.5 + 60) / (-40 + 6.5 - 15.0 + 60)),
    "audio.midLevel": 60.0 / 4.343,        # power smoothed over 1 s on a 0 = -60 dB scale: 4.34 dB/s, 13.8 s from 0 dB
    "audio.trebleLevel": 60.0 / 4.343,
}


# ---------------------------------------------------------------------------------------------------------------------
# Running

def write_project(fixture_dir, out_dir):
    """The lab project with this fixture's audio. Never carries anything from the manifest but file names."""
    m = json.loads((fixture_dir / "manifest.json").read_text())
    p = json.loads(LAB_PROJECT.read_text())
    p["assets"]["scene"]["path"] = str(LAB_PROJECT.parent / p["assets"]["scene"]["path"])
    p["assets"]["audio"] = str(fixture_dir / "mix.wav")
    if m.get("stems"):
        p["assets"]["stems"] = [{"key": k, "label": k.title(), "file": str(fixture_dir / s["file"]), "role": s["role"]}
                                for k, s in m["sources"].items()]
    p["sonic"]["live"] = False
    p["render"]["path"] = str(out_dir / "render.mp4")
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "project.json"
    path.write_text(json.dumps(p, indent=1) + "\n")
    return path


def trace(avgen, project, out_csv, fps, finals):
    # Every avgen goes through the GPU lock, even a GPU-free trace: an unlocked process beside another agent's perf
    # run spoils its timing (tools/gpu-lock.sh).
    cmd = [str(REPO / "tools" / "gpu-lock.sh"), str(avgen), "--project", str(project), "--sonic-trace", str(out_csv),
           "--fps", repr(fps)]
    if finals:
        cmd.append("--trace-finals")
    r = subprocess.run(cmd, capture_output=True, text=True)
    (out_csv.parent / (out_csv.stem + ".log")).write_text(r.stdout + r.stderr)
    if r.returncode != 0:
        sys.exit(f"lab: {' '.join(cmd)} failed ({r.returncode}); see {out_csv.stem}.log")


def replay(tool, wav, prefix):
    """ADR-1725: the file through the live analysis path (tools/audio_lab_replay.cpp)."""
    if not Path(tool).exists():
        print(f"lab: no {tool}; the live path is not scored (build avgen_audio_lab_replay)")
        return
    cmd = [str(REPO / "tools" / "gpu-lock.sh"), str(tool), str(wav), str(prefix)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"lab: {' '.join(cmd)} failed ({r.returncode}): {r.stderr[-400:]}")


def read_trace(path):
    with open(path) as f:
        rows = csv.reader(f)
        header = next(rows)
        cols = {name: [] for name in header}
        for row in rows:
            for name, v in zip(header, row):
                cols[name].append(float(v))
    return cols


# ---------------------------------------------------------------------------------------------------------------------
# Measurement helpers

def pct(xs, q):
    if not xs:
        return None
    s = sorted(xs)
    k = (len(s) - 1) * q
    lo, hi = math.floor(k), math.ceil(k)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def r3(x):
    return None if x is None else round(x, 4)


def events_of(tr, col):
    """(time, strength) of every row the event column fired in."""
    t = tr["time"]
    return [(t[i], v) for i, v in enumerate(tr[col]) if v > 0.0] if col in tr else None


def in_any(t, intervals):
    return any(a <= t < b for a, b in intervals)


def cluster(times, gap=CLUSTER_S):
    """GT attack clusters: [first time, [member indices]]."""
    out = []
    for i, t in sorted(enumerate(times), key=lambda x: x[1]):
        if out and t - out[-1][1] < gap:
            out[-1][2].append(i)
            out[-1][1] = t
        else:
            out.append([t, t, [i]])
    return [(c[0], c[2]) for c in out]


def match(gt, det, early=MATCH_EARLY_S, late=MATCH_LATE_S):
    """Greedy in time order: each GT attack takes the earliest unused detection in [t - early, t + late].
    Returns (pairs [(gi, di, err)], unmatched gt indices, unmatched det indices)."""
    used = [False] * len(det)
    pairs, missed = [], []
    for gi, g in enumerate(gt):
        best = None
        lo = bisect.bisect_left(det, g - early)
        for di in range(lo, len(det)):
            if det[di] > g + late:
                break
            if not used[di]:
                best = di
                break
        if best is None:
            missed.append(gi)
        else:
            used[best] = True
            pairs.append((gi, best, det[best] - g))
    return pairs, missed, [i for i, u in enumerate(used) if not u]


def onset_metrics(m, gt_events, det_times, sustained, silent, duration, label, early=MATCH_EARLY_S,
                  late=MATCH_LATE_S):
    """Precision/recall/timing of one detector against a set of GT events (clustered into attacks)."""
    clusters = cluster([e["time"] for e in gt_events])
    gt_times = [c[0] for c in clusters]
    ambiguous = [all(gt_events[i]["ambiguous"] for i in c[1]) or not any(gt_events[i]["onset"] for i in c[1])
                 for c in clusters]
    pairs, missed, extra = match(gt_times, det_times, early, late)
    matched = {gi for gi, _, _ in pairs}
    errs = [e for gi, _, e in pairs if not ambiguous[gi]]
    # An unmatched detection is a DUPLICATE when it follows a matched one by less than 150 ms; otherwise FALSE.
    matched_det = sorted(det_times[di] for _, di, _ in pairs)
    dup, false = [], []
    for di in extra:
        t = det_times[di]
        k = bisect.bisect_right(matched_det, t) - 1
        (dup if k >= 0 and t - matched_det[k] < 0.150 else false).append(t)
    firm = [i for i, a in enumerate(ambiguous) if not a]
    tp = sum(1 for gi in matched if not ambiguous[gi])
    tp_all = len(pairs)
    # False events: in silence (after the settle time) and in sustained content (after its grace).
    quiet = [(a + 0.25, b - QUIET_GUARD_S) for a, b in silent if b - a > 0.25 + QUIET_GUARD_S]
    held = [(s["start"] + SUSTAIN_GRACE_S, s["end"]) for s in sustained]
    quiet_s = sum(b - a for a, b in quiet)
    held_s = sum(b - a for a, b in held)
    unmatched = [det_times[di] for di in extra]
    f_quiet = [t for t in unmatched if in_any(t, quiet)]
    f_held = [t for t in unmatched if in_any(t, held)]
    precision = tp_all / len(det_times) if det_times else None
    recall = tp / len(firm) if firm else None
    return {
        "detector": label,
        "groundTruthAttacks": len(gt_times), "firmAttacks": len(firm), "ambiguousAttacks": len(gt_times) - len(firm),
        "detections": len(det_times), "matched": tp_all, "matchedFirm": tp,
        "precision": r3(precision), "recall": r3(recall),
        "timingErrorMs": {"median": r3(1000 * pct(errs, 0.5)) if errs else None,
                          "p95": r3(1000 * pct(errs, 0.95)) if errs else None,
                          "min": r3(1000 * min(errs)) if errs else None, "max": r3(1000 * max(errs)) if errs else None,
                          "raw": [round(1000 * e, 2) for e in errs]},
        "missed": [{"time": round(gt_times[gi], 4), "ambiguous": ambiguous[gi],
                    "kinds": sorted({gt_events[i]["kind"] for i in clusters[gi][1]})} for gi in missed],
        "duplicates": [round(t, 4) for t in dup],
        "false": [round(t, 4) for t in false],
        "falsePerMinuteSilence": r3(60 * len(f_quiet) / quiet_s) if quiet_s > 0 else None,
        "falseInSilence": [round(t, 4) for t in f_quiet], "silenceSeconds": round(quiet_s, 3),
        "falsePerMinuteSustained": r3(60 * len(f_held) / held_s) if held_s > 0 else None,
        "falseInSustained": [round(t, 4) for t in f_held], "sustainedSeconds": round(held_s, 3),
    }


def rest_metrics(tr, silent, cols):
    """In each silent interval, once the signal's documented release has run out: the largest value it still holds
    (stale state), and how long it actually took to reach rest."""
    t = tr["time"]
    out = {}
    for c in cols:
        if c not in tr:
            out[c] = {"available": False}
            continue
        worst, at = 0.0, None
        settle = []
        allowed = DOCUMENTED_REST_S.get(c)
        judged = 0.0
        for a, b in silent:
            b -= QUIET_GUARD_S
            if b - a <= 0.25:
                continue
            if allowed is not None and b - a > allowed + 0.05:
                judged += b - a - allowed - 0.05
                for i, ti in enumerate(t):
                    if a + allowed + 0.05 <= ti < b and tr[c][i] > worst:
                        worst, at = tr[c][i], ti
            # How long from the start of silence until the column first reads at rest and stays there.
            rest_from = None
            for i, ti in enumerate(t):
                if a <= ti < b:
                    if tr[c][i] < LEVEL_REST:
                        rest_from = ti if rest_from is None else rest_from
                    else:
                        rest_from = None
            settle.append(None if rest_from is None else round(rest_from - a, 4))
        out[c] = {"documentedRestSeconds": None if allowed is None else round(allowed, 3),
                  "judgedSeconds": round(judged, 3), "maxAfterSettle": round(worst, 4) if judged > 0 else None,
                  "at": at, "secondsToRest": settle}
    return out


def mix_reference_db(fixture_dir, times, window_s=0.050):
    """The produced mix's RMS in dBFS over the `window_s` before each trace time (the reference envelope)."""
    import numpy as np
    raw = (fixture_dir / "mix.wav").read_bytes()
    data = raw.index(b"data") + 8
    x = np.frombuffer(raw[data:], dtype="<f4").reshape(-1, 2).astype(np.float64)
    mono = np.sqrt(0.5 * (x[:, 0] ** 2 + x[:, 1] ** 2))
    csum = np.concatenate([[0.0], np.cumsum(mono ** 2)])
    w = int(window_s * RATE)
    out = []
    for t in times:
        b = min(len(mono), int(round(t * RATE)))
        a = max(0, b - w)
        p = (csum[b] - csum[a]) / max(1, b - a)
        out.append(10 * math.log10(max(p, 1e-12)))
    return out


def corr(a, b):
    n = len(a)
    if n < 3:
        return None
    ma, mb = sum(a) / n, sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    if va <= 0 or vb <= 0:
        return None
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(va * vb)


def rank(xs):
    order = sorted(range(len(xs)), key=lambda i: xs[i])
    r = [0.0] * len(xs)
    for k, i in enumerate(order):
        r[i] = k
    return r


def envelope_metrics(fixture_dir, tr, col="response.level"):
    """Spearman correlation of a level with the mix's RMS dB (where audible), and the lag that maximises it."""
    t = tr["time"]
    ref = mix_reference_db(fixture_dir, t)
    keep = [i for i, d in enumerate(ref) if d > -60.0]
    if col not in tr or len(keep) < 10:
        return {"available": False}
    best = (None, None)
    for lag in range(0, 8):  # rows of the hop trace: 0..75 ms
        idx = [i for i in keep if i + lag < len(t)]
        c = corr(rank([ref[i] for i in idx]), rank([tr[col][i + lag] for i in idx]))
        if c is not None and (best[0] is None or c > best[0]):
            best = (c, lag)
    c0 = corr(rank([ref[i] for i in keep]), rank([tr[col][i] for i in keep]))
    return {"signal": col, "spearmanAtZeroLag": r3(c0), "bestSpearman": r3(best[0]),
            "bestLagMs": r3(1000 * best[1] / HOP_FPS) if best[1] is not None else None, "rowsAudible": len(keep)}


def release_metrics(m, tr, col):
    """For each isolated event followed by >= 1 s of silence: time from the event's end to `col` < LEVEL_REST."""
    t = tr["time"]
    out = []
    for a, b in m["silent"]:
        if b - a < 1.0 or a < 0.1:
            continue
        rest = None
        for i, ti in enumerate(t):
            if ti >= a and tr[col][i] < LEVEL_REST:
                rest = ti
                break
        peak = max((tr[col][i] for i, ti in enumerate(t) if a - 0.5 <= ti < a), default=0.0)
        out.append({"silenceStarts": a, "peakBefore": round(peak, 4),
                    "releaseMs": None if rest is None else round(1000 * (rest - a), 1)})
    return out


def band_matrix(m, tr):
    cols = ["response.bass", "response.mid", "response.high", "audio.bassLevel", "audio.lowMidLevel",
            "audio.midLevel", "audio.highMidLevel", "audio.trebleLevel", "response.level"]
    t = tr["time"]
    out = []
    for s in m["sections"]:
        if "register" not in s:
            continue
        a, b = s["start"] + 0.5, s["end"]
        idx = [i for i, ti in enumerate(t) if a <= ti < b]
        row = {"section": s["label"], "register": s["register"]}
        for c in cols:
            if c in tr and idx:
                row[c] = round(sum(tr[c][i] for i in idx) / len(idx), 4)
        out.append(row)
    return out


def beat_metrics(m, tr):
    beats = m.get("beats")
    if not beats:
        return {"applicable": False}
    gt = beats["times"]
    det = [x for x, _ in events_of(tr, "beat.pulse")]
    pairs, missed, extra = match(gt, det, BEAT_TOL_S, BEAT_TOL_S)
    errs = [e for _, _, e in pairs]
    p = len(pairs) / len(det) if det else 0.0
    r = len(pairs) / len(gt) if gt else 0.0
    f = 2 * p * r / (p + r) if p + r > 0 else 0.0
    # Phase: the GT phase at each row (fraction of the way between the GT beats around it), against beat.phase.
    t = tr["time"]
    phase_err = []
    for i, ti in enumerate(t):
        k = bisect.bisect_right(gt, ti) - 1
        if k < 0 or k + 1 >= len(gt):
            continue
        gp = (ti - gt[k]) / (gt[k + 1] - gt[k])
        d = (tr["beat.phase"][i] - gp + 0.5) % 1.0 - 0.5
        phase_err.append((ti, d))
    # Per tempo segment: the median |phase error|, the bpm the clock read, and when the phase locked (|err| < 0.1
    # for the next 4 GT beats' worth of rows).
    segs = []
    for seg in beats["tempo"]:
        a, b = seg["start"], seg["end"]
        errs_seg = [abs(d) for ti, d in phase_err if a <= ti < b]
        bpms = [tr["beat.bpm"][i] for i, ti in enumerate(t) if a + 0.5 <= ti < b]
        lock = None
        span = 4 * 60.0 / seg["bpm"]
        rows = [(ti, abs(d)) for ti, d in phase_err if a <= ti < b]
        for j, (ti, _) in enumerate(rows):
            window = [e for tj, e in rows[j:] if tj < ti + span]
            if window and ti + span <= b and max(window) < 0.1:
                lock = round(ti - a, 3)
                break
        segs.append({"start": a, "end": b, "bpm": seg["bpm"],
                     "clockBpmMedian": r3(pct(bpms, 0.5)) if bpms else None,
                     "octaveRatio": r3(pct(bpms, 0.5) / seg["bpm"]) if bpms else None,
                     "phaseErrorMedian": r3(pct(errs_seg, 0.5)) if errs_seg else None,
                     "lockAfterSeconds": lock})
    conf = tr.get("audio.tempoConfidence")
    return {"applicable": True, "gtBeats": len(gt), "pulses": len(det), "precision": r3(p), "recall": r3(r),
            "fMeasure": r3(f), "beatErrorMs": {"median": r3(1000 * pct(errs, 0.5)) if errs else None,
                                               "p95abs": r3(1000 * pct([abs(e) for e in errs], 0.95)) if errs else None},
            "segments": segs,
            "tempoConfidence": {"min": r3(min(conf)), "max": r3(max(conf)),
                                "distinctValues": len({round(c, 3) for c in conf})} if conf else None}


def source_metrics(m, tr):
    """Per stem: onsets against that stem's GT; activity at times only OTHER stems play (leakage); a silent stem's
    activity (false); drum classes against GT classes."""
    out = {}
    silent_all = m["silent"]
    for key, s in m["sources"].items():
        gt = [e for e in m["events"] if e["source"] == key]
        on = events_of(tr, f"stem.{key}.onset")
        if on is None:
            out[key] = {"available": False}
            continue
        det = [x for x, _ in on]
        sustained = [{"start": e["time"], "end": e["time"] + e.get("durationSeconds", 0)} for e in gt
                     if e.get("durationSeconds", 0) > 0.5]
        res = {"role": s["role"], "onsets": onset_metrics(m, gt, det, sustained, s["silent"], m["durationSeconds"],
                                                          f"stem.{key}.onset")}
        # Leakage: this stem's unmatched detections that fall in another stem's match window.
        others = sorted(e["time"] for e in m["events"] if e["source"] != key)
        leaks = []
        for t in res["onsets"]["onsets" if False else "false"]:
            k = bisect.bisect_left(others, t - MATCH_LATE_S)
            if k < len(others) and others[k] <= t + MATCH_EARLY_S:
                leaks.append(t)
        res["leakedOnsets"] = leaks
        # Level while this stem is silent (its own silence, measured from its file): leakage of level.
        t = tr["time"]
        lv = tr.get(f"stem.{key}.level")
        quiet = [(a + SETTLE_S, b) for a, b in s["silent"] if b - a > SETTLE_S]
        res["levelInOwnSilenceMax"] = round(max((lv[i] for i, ti in enumerate(t) if in_any(ti, quiet)), default=0.0), 4)
        act = tr.get(f"stem.{key}.active")
        res["activeFraction"] = round(sum(1 for v in act if v > 0.5) / len(act), 4) if act else None
        res["silentThroughout"] = s["peak"] == 0.0
        if res["silentThroughout"]:
            res["falseActivity"] = {"onsets": len(det),
                                    "levelMax": round(max(lv), 4) if lv else None}
        if s["role"] == "drums":
            classes = {}
            for cls in ("kick", "snare", "hat"):
                ev = events_of(tr, f"stem.{key}.{cls}")
                classes[cls] = [x for x, _ in ev] if ev is not None else None
            confusion = {}
            stamps = sorted(e["time"] for e in gt if "cls" in e)
            for e in gt:
                if "cls" not in e:
                    continue
                # A hit stacked on another class within one hop is one attack with two names: kept apart.
                stacked = sum(1 for x in stamps if abs(x - e["time"]) < HOP_S) > 1
                row = confusion.setdefault(e["cls"] + (f" [{e['kit']}]" if "kit" in e else "") +
                                           (" (stacked)" if stacked else ""),
                                           {"kick": 0, "snare": 0, "hat": 0, "none": 0})
                hit = None
                for cls, times in classes.items():
                    if times and any(-MATCH_EARLY_S <= x - e["time"] <= 0.120 for x in times):
                        row[cls] += 1
                        hit = cls
                if hit is None:
                    row["none"] += 1
            res["drumClassConfusion"] = confusion
            res["drumClassLatencyMs"] = {}
            for cls, times in classes.items():
                gts = sorted(e["time"] for e in gt if e.get("cls") == cls)
                pairs, _, _ = match(gts, times or [], MATCH_EARLY_S, 0.120)
                errs = [e for _, _, e in pairs]
                res["drumClassLatencyMs"][cls] = {"median": r3(1000 * pct(errs, 0.5)) if errs else None,
                                                  "p95": r3(1000 * pct(errs, 0.95)) if errs else None}
        out[key] = res
    return out


def mix_attribution(m, tr):
    """Mix-only: which GT sources each mix hit (response.low, response.onset) lands on."""
    out = {}
    gt = sorted(m["events"], key=lambda e: e["time"])
    times = [e["time"] for e in gt]
    for col in ("response.low", "response.onset"):
        ev = events_of(tr, col)
        counts = {}
        for x, _ in ev:
            lo = bisect.bisect_left(times, x - MATCH_LATE_S)
            hit = {f"{gt[k]['source']}:{gt[k]['kind']}" for k in range(lo, len(gt)) if gt[k]["time"] <= x + MATCH_EARLY_S}
            key = "+".join(sorted(hit)) if hit else "none"
            counts[key] = counts.get(key, 0) + 1
        out[col] = dict(sorted(counts.items(), key=lambda kv: -kv[1]))
    return out


def mapping_metrics(project, tr):
    """Stage 4 -> 6: each indicator's parameter final against the function of its signal the route declares."""
    layout = project["audioLab"]["layout"]
    out = {}
    for name, ind in layout.items():
        sig = ind["signal"]
        if sig not in tr:
            out[name] = {"signal": sig, "available": False}
            continue
        comp = ind.get("component", 0)
        col = f"final:{ind['param']}[{comp}]"
        if col not in tr:
            out[name] = {"signal": sig, "available": True, "final": "not traced"}
            continue
        v, f = tr[sig], tr[col]
        if ind["kind"] == "meter":
            rng = ind.get("range", 1.0)
            want = [0.001 + min(1.0, max(0.0, x / rng)) * (ind["scale"] - 0.001) for x in v]
        elif ind["kind"] == "lamp":
            want = [ind["base"] + ind["gain"] * x for x in v]
        else:
            out[name] = {"signal": sig, "available": True, "kind": ind["kind"],
                         "finalMin": round(min(f), 4), "finalMax": round(max(f), 4)}
            continue
        err = max(abs(a - b) for a, b in zip(want, f))
        out[name] = {"signal": sig, "available": True, "kind": ind["kind"], "maxAbsError": round(err, 6),
                     "finalMin": round(min(f), 4), "finalMax": round(max(f), 4)}
    return out


def probe_metrics(m, tr):
    """The probes with real chains: bounded, back at rest after silence, and the flash's duration."""
    t = tr["time"]
    out = {}
    flash = tr.get("final:procedural/pFlash/material/emissive[0]")
    onset = tr.get("response.onset")
    if flash and onset:
        durations = []
        for i, v in enumerate(onset):
            if v > 0:
                j = i + 1
                while j < len(t) and flash[j] > 0.02 + 1e-3 and onset[j] == 0:
                    j += 1
                if j < len(t) and onset[j] == 0:
                    durations.append(round(1000 * (t[j] - t[i]), 1))
        out["pFlash"] = {"declaredMs": 120.0, "measuredMs": durations[:200],
                         "median": r3(pct(durations, 0.5)) if durations else None, "max": max(durations) if durations else None,
                         "bounded": [round(min(flash), 4), round(max(flash), 4)]}
    for name, col, lo, hi, signal in (("pBass", "final:procedural/pBass/transform/scale[0]", 0.25, 1.4, "response.bass"),
                                      ("pStem", "final:procedural/pStem/transform/scale[0]", 0.2, 1.8, "response.level")):
        f = tr.get(col)
        if f:
            # The probe's chain falls (one-pole, 300 ms) from its top to within 0.01 of rest AFTER its signal has
            # reached rest (the signal's documented release): judged only once both have run out.
            settle = DOCUMENTED_REST_S[signal] + 0.3 * math.log((hi - lo) / 0.01) + 0.05
            quiet = [(a + settle, b - QUIET_GUARD_S) for a, b in m["silent"] if b - a > settle + QUIET_GUARD_S]
            rest = [f[i] for i, ti in enumerate(t) if in_any(ti, quiet)]
            out[name] = {"bounded": [round(min(f), 4), round(max(f), 4)], "declared": [lo, hi],
                         "withinDeclared": min(f) >= lo - 1e-4 and max(f) <= hi + 1e-4,
                         "maxInSettledSilence": round(max(rest), 4) if rest else None, "rest": lo}
    return out


def section_onsets(m, det_times, early=MATCH_EARLY_S, late=MATCH_LATE_S):
    """Per manifest section: firm attacks, detections, matched, false (the noisy SNR steps, the adversarial cases)."""
    out = []
    for sec in m["sections"]:
        a, b = sec["start"], sec["end"]
        ev = [e for e in m["events"] if a <= e["time"] < b]
        firm = cluster([e["time"] for e in ev if e["onset"] and not e["ambiguous"]])
        allc = cluster([e["time"] for e in ev])
        det = [t for t in det_times if a - early <= t < b]
        pairs, missed, extra = match([c[0] for c in allc], det, early, late)
        firm_times = {round(c[0], 6) for c in firm}
        hit_firm = sum(1 for gi, _, _ in pairs if round(allc[gi][0], 6) in firm_times)
        out.append({"section": sec["label"], "start": a, "end": b, "events": len(ev), "attacks": len(allc),
                    "firmAttacks": len(firm), "detections": len(det), "matched": len(pairs),
                    "recallFirm": r3(hit_firm / len(firm)) if firm else None, "unmatchedDetections": len(extra),
                    **({k: sec[k] for k in ("snrDb", "noiseRmsDbfs", "register") if k in sec})})
    return out


def kind_recall(m, onset_times, low_times):
    """Per section with an SNR, per event kind: how many response.onset and response.low caught."""
    out = []
    for sec in m["sections"]:
        if sec.get("snrDb") is None:
            continue
        for kind in sorted({e["kind"] for e in m["events"] if sec["start"] <= e["time"] < sec["end"]}):
            ts = [e["time"] for e in m["events"] if e["kind"] == kind and sec["start"] <= e["time"] < sec["end"]]
            row = {"section": sec["label"], "snrDb": sec["snrDb"], "kind": kind, "events": len(ts)}
            for name, det in (("response.onset", onset_times), ("response.low", low_times)):
                pairs, _, _ = match(ts, det)
                row[name] = len(pairs)
            out.append(row)
    return out


def pair_separation(m, det_times):
    """Events marked with `pair` (two attacks `pair` seconds apart): how many detections each pair produced."""
    out = []
    firsts = {}
    for e in m["events"]:
        if "pair" in e:
            firsts.setdefault(round(e["time"] - (e["pair"] if "second" in e.get("note", "") else 0.0), 4), e["pair"])
    for t0, gap in sorted(firsts.items()):
        n = sum(1 for t in det_times if t0 - MATCH_EARLY_S <= t <= t0 + gap + MATCH_LATE_S)
        out.append({"start": t0, "gapMs": round(1000 * gap), "detections": n, "separated": n >= 2})
    return out


def lock_timeline(m, tr):
    """Beat lock against the intended grid, beat by beat: locked when |phase error| < LOCK_PHASE and the clock's tempo
    is within 3% of the intended. Acquisition = the first of LOCK_BEATS locked beats in a row in a tempo segment; a loss
    = the first unlocked beat after a lock; reacquisition after each silence. Confidence is sampled at each beat."""
    beats = m.get("beats")
    if not beats or "beat.phase" not in tr:
        return None
    t = tr["time"]
    gt = beats["times"]
    tempo_at = lambda x: next((s["bpm"] for s in beats["tempo"] if s["start"] <= x < s["end"]), None)
    rows = []
    for k in range(len(gt) - 1):
        i = min(len(t) - 1, bisect.bisect_left(t, gt[k] + 0.25 * (gt[k + 1] - gt[k])))  # a quarter beat in
        gp = (t[i] - gt[k]) / (gt[k + 1] - gt[k])
        d = (tr["beat.phase"][i] - gp + 0.5) % 1.0 - 0.5
        bpm = tr["beat.bpm"][i]
        want = tempo_at(gt[k])
        ok = abs(d) < LOCK_PHASE and want is not None and bpm > 0 and abs(bpm / want - 1.0) < 0.03
        conf = tr["audio.tempoConfidence"][i] if "audio.tempoConfidence" in tr else None
        rows.append({"beat": round(gt[k], 4), "locked": ok, "phaseError": round(d, 3), "bpm": round(bpm, 2),
                     "confidence": None if conf is None else round(conf, 3)})
    def acquire(after, until):
        run = 0
        for r in rows:
            if r["beat"] < after or r["beat"] >= until:
                continue
            run = run + 1 if r["locked"] else 0
            if run >= LOCK_BEATS:
                return round(r["beat"] - (LOCK_BEATS - 1) * 60.0 / (tempo_at(r["beat"]) or 120) - after, 3)
        return None
    segs = [{"start": s["start"], "end": s["end"], "bpm": s["bpm"], "acquireSeconds": acquire(s["start"], s["end"])}
            for s in beats["tempo"]]
    losses = []
    was = False
    for r in rows:
        if was and not r["locked"]:
            losses.append(r["beat"])
        was = r["locked"]
    silences = []
    for a, b in m["silent"]:
        if b - a < 1.0 or a < 0.5:
            continue
        conf_in = [tr["audio.tempoConfidence"][i] for i, x in enumerate(t) if a + 0.5 <= x < b] \
            if "audio.tempoConfidence" in tr else []
        pulses_in = sum(1 for i, x in enumerate(t) if a + 0.5 <= x < b and tr["beat.pulse"][i] > 0)
        nxt = next((s["end"] for s in beats["tempo"] if s["start"] <= b < s["end"]), m["durationSeconds"])
        fall = None
        if "audio.tempoConfidence" in tr:
            for i, x in enumerate(t):
                if a <= x < b and tr["audio.tempoConfidence"][i] < 0.1:
                    fall = round(x - a, 3)
                    break
        silences.append({"start": a, "end": b, "pulsesInSilence": pulses_in, "confidenceFallsAfterSeconds": fall,
                         "confidenceInSilence": [r3(min(conf_in)), r3(max(conf_in))] if conf_in else None,
                         "reacquireSeconds": acquire(b, nxt)})
    return {"segments": segs, "losses": losses, "silences": silences,
            "lockedFraction": r3(sum(1 for r in rows if r["locked"]) / len(rows)) if rows else None, "beats": rows}


def live_metrics(m, run_dir):
    """The causal replay (ADR-1725): the live analysis path's onsets and its beat tracker, scored on their own."""
    path = run_dir / "live.frame.csv"
    if not path.exists():
        return None
    tr = read_trace(path)
    sustained = [s for s in m["sections"] if s.get("sustained")]
    out = {"matchWindowMs": [-1000 * LIVE_EARLY_S, round(1000 * LIVE_LATE_S, 1)], "onsets": {}}
    low_gt = [e for e in m["events"] if "low" in e.get("registers", [e["register"]])]
    for col, gt in (("response.onset", m["events"]), ("response.low", low_gt), ("audio.onset", m["events"])):
        det = [x for x, _ in events_of(tr, col)]
        # Matched on the live window: a live frame exists only after its window ends, so nothing is early.
        o = onset_metrics(m, gt, det, sustained, m["silent"], m["durationSeconds"], col + " (live)", LIVE_EARLY_S,
                          LIVE_LATE_S)
        o["liveWindow"] = {"matched": o["matched"], "missed": len(o["missed"]), "unmatched": len(o["false"]) +
                           len(o["duplicates"]), "precision": o["precision"], "recall": o["recall"],
                           "timingMs": o["timingErrorMs"]}
        out["onsets"][col] = o
    out["beat"] = beat_metrics(m, tr)
    out["lock"] = lock_timeline(m, tr)
    return out


# ---------------------------------------------------------------------------------------------------------------------

def evaluate(fixture_dir, run_dir):
    m = json.loads((fixture_dir / "manifest.json").read_text())
    project = json.loads((run_dir / "project.json").read_text())
    hop = read_trace(run_dir / "hop.csv")
    frame = read_trace(run_dir / "frame.csv")
    res = {"fixture": m["name"], "category": m["category"], "purpose": m["purpose"], "seed": m["seed"],
           "mixSha256": m["files"]["mix.wav"]["sha256"], "durationSeconds": m["durationSeconds"],
           "clock": {"hopFps": HOP_FPS, "frameFps": FRAME_FPS, "matchWindowMs": [-1000 * MATCH_EARLY_S,
                                                                                 1000 * MATCH_LATE_S]}}
    mix_gt = [e for e in m["events"]]
    sustained = [s for s in m["sections"] if s.get("sustained")]
    res["onsets"] = {}
    for col in ("response.onset", "audio.onset"):
        for rate, tr in (("hop", hop), ("frame", frame)):
            ev = events_of(tr, col)
            if ev is None:
                continue
            res["onsets"][f"{col}@{rate}"] = onset_metrics(m, mix_gt, [x for x, _ in ev], sustained, m["silent"],
                                                           m["durationSeconds"], col)
    low_gt = [e for e in m["events"] if "low" in e.get("registers", [e["register"]])]
    ev = events_of(hop, "response.low")
    res["onsets"]["response.low@hop (vs low-register GT)"] = onset_metrics(
        m, low_gt, [x for x, _ in ev], sustained, m["silent"], m["durationSeconds"], "response.low")
    res["rest"] = rest_metrics(hop, m["silent"], ["response.level", "response.bass", "response.mid", "response.high",
                                                  "response.sustain",
                                                  "response.onsetEnv", "response.lowEnv", "audio.midLevel",
                                                  "audio.trebleLevel", "audio.tempoConfidence", "sonic.energy",
                                                  "sonic.brightness"])
    res["envelope"] = envelope_metrics(fixture_dir, hop)
    res["release"] = {c: release_metrics(m, hop, c) for c in ("response.level", "response.bass", "response.onsetEnv")}
    res["bands"] = band_matrix(m, hop)
    res["beat"] = beat_metrics(m, frame)
    res["lock"] = lock_timeline(m, frame)
    det = [x for x, _ in events_of(hop, "response.onset")]
    res["sections"] = section_onsets(m, det)
    res["pairs"] = pair_separation(m, det)
    res["live"] = live_metrics(m, run_dir)
    if m.get("stems"):
        res["sources"] = source_metrics(m, hop)
        res["mixAttribution"] = mix_attribution(m, hop)
    res["mapping"] = mapping_metrics(project, frame)
    res["probes"] = probe_metrics(m, frame)
    res["_hop"] = hop
    res["checks"] = checks(m, res)
    del res["_hop"]
    return res


def checks(m, res):
    """Pass/fail against the derived thresholds (01-plan.md §6). Each says what it compares and why that bound."""
    out = []

    def check(name, ok, value, bound, why, gate=True):
        """`gate` False: a measured FINDING the owner has chosen to treat as separate work (reported, never
        silently passed: its pass/fail is still the measurement's)."""
        out.append({"check": name, "pass": bool(ok) if ok is not None else None, "value": value, "bound": bound,
                    "why": why, "gate": gate})

    o = res["onsets"]["response.onset@hop"]
    # Whole-fixture onset quality gates where the material is clean; on the noisy and adversarial fixtures it is a
    # finding (their per-section checks carry the gates), since this branch does not change a detector.
    clean = m["category"] not in ("noisy", "adversarial")
    if o["firmAttacks"]:
        check("response.onset recall (firm attacks)", o["recall"] is not None and o["recall"] >= 0.9, o["recall"],
              ">= 0.90", "every unambiguous attack is a separate event at least 60 ms from its neighbours", gate=clean)
        check("response.onset precision", o["precision"] is not None and o["precision"] >= 0.9, o["precision"],
              ">= 0.90", "a detection that matches no attack is a false trigger", gate=clean)
        p95 = o["timingErrorMs"]["p95"]
        check("response.onset timing p95 (hop clock)", p95 is not None and p95 <= 1000 * MATCH_LATE_S, p95,
              f"<= {1000 * MATCH_LATE_S:.1f} ms", "one hop to reach a window centre, one to decide, one spare")
    if o["falsePerMinuteSilence"] is not None:
        check("response.onset false events in silence", o["falsePerMinuteSilence"] == 0, o["falsePerMinuteSilence"],
              "0 / min", "digital silence has no attack")
    if o["falsePerMinuteSustained"] is not None:
        check("response.onset false events in sustained notes", o["falsePerMinuteSustained"] <= 6.0,
              o["falsePerMinuteSustained"], "<= 6 / min",
              "a held note may wobble; more than one retrigger every 10 s reads as a flicker")
    for col, r in res["rest"].items():
        if r.get("available", True) and r.get("maxAfterSettle") is not None:
            check(f"{col} at rest after its documented release ({r['documentedRestSeconds']} s) in silence",
                  r["maxAfterSettle"] < LEVEL_REST, r["maxAfterSettle"], f"< {LEVEL_REST}",
                  "no stale value once the sound has gone and the signal's own release has run out")
    for name, mp in res["mapping"].items():
        if "maxAbsError" in mp:
            check(f"mapping {name} = f({mp['signal']})", mp["maxAbsError"] < 1e-3, mp["maxAbsError"], "< 0.001",
                  "the indicator shows the published value (identity chain), within the CSV's 4 decimals")
    pf = res["probes"].get("pFlash")
    if pf and pf["max"] is not None:
        check("onset flash returns to rest", pf["max"] <= 120.0 + 1000 / FRAME_FPS + 1, pf["max"],
              f"<= {120 + 1000 / FRAME_FPS:.0f} ms", "the declared 120 ms fall plus one render frame")
    for name in ("pBass", "pStem"):
        p = res["probes"].get(name)
        if p:
            check(f"{name} bounded", p["withinDeclared"], p["bounded"], p["declared"], "the chain's clamp and remap")
            if p["maxInSettledSilence"] is not None:
                check(f"{name} back at rest in silence", p["maxInSettledSilence"] <= p["rest"] + 0.01,
                      p["maxInSettledSilence"], f"<= {p['rest'] + 0.01}", "no stale probe after silence")
    b = res["beat"]
    if b.get("applicable"):
        # Findings, not gates: the owner takes the beat clock as separate work (2026-10-26).
        check("file grid: beat F-measure (+-70 ms)", b["fMeasure"] >= 0.8, b["fMeasure"], ">= 0.80",
              "MIREX tolerance; a grid that follows the intended pulse", gate=False)
        for s in b["segments"]:
            check(f"file grid: tempo {s['bpm']:g} BPM ({s['start']:g}-{s['end']:g} s)",
                  s["octaveRatio"] is not None and abs(s["octaveRatio"] - 1.0) < 0.03, s["clockBpmMedian"],
                  f"{s['bpm']:g} +- 3%", "the intended tempo, not an octave of it", gate=False)
    lv = res.get("live")
    if lv:
        lb = lv["beat"]
        if lb.get("applicable"):
            check("live tracker: beat F-measure (+-70 ms)", lb["fMeasure"] >= 0.8, lb["fMeasure"], ">= 0.80",
                  "MIREX tolerance, on the causal BeatTracker", gate=False)
            for sg in lv["lock"]["segments"]:
                check(f"live tracker: locks within 4 s of {sg['start']:g} s ({sg['bpm']:g} BPM)",
                      sg["acquireSeconds"] is not None and sg["acquireSeconds"] <= 4.0, sg["acquireSeconds"], "<= 4 s",
                      "two bars at 120 BPM: a performer hears a beat-locked scene within two bars", gate=False)
            for si in lv["lock"]["silences"]:
                check(f"live tracker: confidence falls in the silence at {si['start']:g} s",
                      si["confidenceInSilence"] is not None and si["confidenceInSilence"][1] < 0.1,
                      si["confidenceInSilence"], "max < 0.1",
                      "a tracker with nothing to track should say so (ADR-1724's honesty rule)", gate=False)
        lo = lv["onsets"]["response.onset"]["liveWindow"]
        if lo["recall"] is not None:
            check("live path: response.onset recall within the live window", lo["recall"] >= 0.9, lo["recall"],
                  ">= 0.90", f"half a window + three hops + one frame ({1000 * LIVE_LATE_S:.0f} ms)", gate=clean)
            check("live path: response.onset timing p95", lo["timingMs"]["p95"] is not None and
                  lo["timingMs"]["p95"] <= 1000 * LIVE_LATE_S, lo["timingMs"]["p95"], f"<= {1000 * LIVE_LATE_S:.0f} ms",
                  "the live window's late edge")
    cat = m["category"]
    secs = {x["section"]: x for x in res.get("sections", [])}
    if cat == "dense":
        for row in res["bands"]:
            reg = row["register"]
            if reg in ("low", "mid", "high"):
                own = {"low": "response.bass", "mid": "response.mid", "high": "response.high"}[reg]
                others = [c for c in ("response.bass", "response.mid", "response.high") if c != own]
                check(f"band selectivity, {row['section']}: {own} leads", row[own] >= 0.3 and
                      all(row[c] <= 0.15 for c in others), {c: row[c] for c in (own, *others)},
                      f"{own} >= 0.30, the others <= 0.15",
                      "ADR-1723's levels are band powers: a section with one register lights only its own")
        for pr in res["pairs"]:
            check(f"pair {pr['gapMs']} ms apart at {pr['start']:g} s separated", pr["separated"] or pr["gapMs"] < 60,
                  pr["detections"], ">= 2 when >= 60 ms apart",
                  "two attacks at least one refractory (60 ms) apart are two events", gate=pr["gapMs"] >= 60)
    if cat == "noisy":
        nz = secs.get("noise only")
        if nz:
            det = [x for x, _ in events_of(res["_hop"], "response.onset")]
            steady = [x for x in det if 0.5 <= x < nz["end"] - 0.5]
            start = [x for x in det if x < 0.5]
            check("noisy: no onset in the steady -40 dBFS noise floor (0.5 s on)", not steady, steady, "none",
                  "a steady noise floor has no attack")
            check("noisy: no onset on the floor's first samples (fade-in from digital silence at the file start)",
                  not start, start, "none", "the first audible samples after a reset are not an attack; a level-free "
                  "detector with no history fires on them", gate=False)
        # Kicks and snares apart: the broadband onset loses kicks in broadband noise first.
        kicks = {}
        for x in res["sections"]:
            if x.get("snrDb") is not None:
                kicks[x["snrDb"]] = x
        res["noisyByKind"] = kind_recall(m, [x for x, _ in events_of(res["_hop"], "response.onset")],
                                         [x for x, _ in events_of(res["_hop"], "response.low")])
        for x in res["sections"]:
            if x.get("snrDb") is not None:
                gate = False  # a detector's quality in noise: reported, this branch changes no detector
                check(f"noisy: recall at SNR {x['snrDb']:g} dB", x["recallFirm"] is None or x["recallFirm"] >= 0.9,
                      x["recallFirm"], ">= 0.90",
                      "hits 30 dB over the floor are clear; lower SNRs are reported as findings", gate=gate)
                check(f"noisy: unmatched detections at SNR {x['snrDb']:g} dB", x["unmatchedDetections"] <= 1,
                      x["unmatchedDetections"], "<= 1", "the floor does not trigger between hits",
                      gate=x["snrDb"] >= 10)
    if cat == "adversarial":
        for label, most, why in (("sustained pure tone", 1, "one attack, then a held tone"),
                                 ("bass drone, no percussion", 1, "one attack, then a held note"),
                                 ("low-level noise (-60 dBFS)", 0, "noise at the gate is not an event")):
            x = secs.get(label)
            if x:
                check(f"adversarial: {label}: at most {most} onset", x["detections"] <= most, x["detections"],
                      f"<= {most}", why)
        x = secs.get("repeated transients (roll)")
        if x:
            check("adversarial: roll at 30 Hz is bounded by the refractory", x["detections"] <= 1.0 / REFRACTORY_S + 1,
                  x["detections"], f"<= {1 / REFRACTORY_S + 1:.0f}", "a 60 ms refractory caps a buzz roll's events")
        for pr in res["pairs"]:
            check(f"adversarial: click pair {pr['gapMs']} ms apart separated",
                  pr["separated"] or pr["gapMs"] < 60, pr["detections"], ">= 2 when >= 60 ms apart",
                  "one refractory apart is two events; closer is one by design", gate=pr["gapMs"] >= 60)
    if cat == "dynamic":
        x = secs.get("quiet intro")
        if x:
            check("dynamic: hits at -30 dB are detected (level-free detectors)", x["recallFirm"] is not None and
                  x["recallFirm"] >= 0.9, x["recallFirm"], ">= 0.90", "ADR-1060's ratios do not depend on the level")
    if "sources" in res:
        for key, s in res["sources"].items():
            if not s.get("available", True):
                continue
            if s["silentThroughout"]:
                check(f"stem.{key} silent source: no activity", s["falseActivity"]["onsets"] == 0 and
                      (s["falseActivity"]["levelMax"] or 0) < LEVEL_REST, s["falseActivity"], "0 onsets, level < 0.02",
                      "a configured source that never plays")
                continue
            so = s["onsets"]
            check(f"stem.{key}.onset recall", so["recall"] is not None and so["recall"] >= 0.9, so["recall"], ">= 0.90",
                  "the stem's own attacks")
            check(f"stem.{key}.onset no leakage", not s["leakedOnsets"], len(s["leakedOnsets"]), "0",
                  "an onset at another source's attack, with none of its own")
            check(f"stem.{key}.level at rest in its own silence", s["levelInOwnSilenceMax"] < LEVEL_REST,
                  s["levelInOwnSilenceMax"], f"< {LEVEL_REST}", "its own file is silent there")
    return out


def summary_md(results):
    lines = ["# Audio Response Lab: results", "",
             "Generated by `tools/audio_lab/lab.py`. Every number has its raw measurements in `results.json`.", ""]
    gates = [c for r in results for c in r["checks"] if c.get("gate", True)]
    finds = [c for r in results for c in r["checks"] if not c.get("gate", True)]
    lines += [f"**Gates: {sum(1 for c in gates if c['pass'])} of {len(gates)} pass. Findings (reported, not gated): "
              f"{sum(1 for c in finds if c['pass'])} of {len(finds)} pass** across {len(results)} fixtures.", ""]
    for r in results:
        lines += [f"## {r['fixture']} ({r['category']}): {r['purpose']}", ""]
        o = r["onsets"]["response.onset@hop"]
        lines += ["| detector | attacks (firm) | detections | precision | recall | timing median / p95 ms | missed | dup | false | false/min silence | false/min sustained |",
                  "|---|---|---|---|---|---|---|---|---|---|---|"]
        for k, o in r["onsets"].items():
            lines.append(f"| `{k}` | {o['groundTruthAttacks']} ({o['firmAttacks']}) | {o['detections']} | {o['precision']} | "
                         f"{o['recall']} | {o['timingErrorMs']['median']} / {o['timingErrorMs']['p95']} | {len(o['missed'])} | "
                         f"{len(o['duplicates'])} | {len(o['false'])} | {o['falsePerMinuteSilence']} | {o['falsePerMinuteSustained']} |")
        lines.append("")
        if r["beat"].get("applicable"):
            b = r["beat"]
            lines.append(f"Beat clock: F {b['fMeasure']} (P {b['precision']}, R {b['recall']}), beat error median "
                         f"{b['beatErrorMs']['median']} ms; tempo confidence {b['tempoConfidence']}.")
            for s in b["segments"]:
                lines.append(f"- {s['start']:g}-{s['end']:g} s at {s['bpm']:g} BPM: clock {s['clockBpmMedian']} BPM, "
                             f"median |phase error| {s['phaseErrorMedian']}, locked after {s['lockAfterSeconds']} s")
            lines.append("")
        lv = r.get("live")
        if lv:
            lo = lv["onsets"]["response.onset"]["liveWindow"]
            lines.append(f"Live path (causal replay): response.onset P {lo['precision']} R {lo['recall']}, timing median "
                         f"{lo['timingMs']['median']} / p95 {lo['timingMs']['p95']} ms after the onset sample.")
            if lv["beat"].get("applicable"):
                b = lv["beat"]
                lines.append(f"Live tracker: F {b['fMeasure']}, tempo confidence {b['tempoConfidence']}; lock "
                             f"{json.dumps(lv['lock']['segments'])}; losses at {lv['lock']['losses'][:12]}; silences "
                             f"{json.dumps(lv['lock']['silences'])}")
            lines.append("")
        if r.get("sections") and r["category"] in ("noisy", "adversarial", "dynamic", "dense", "sparse"):
            lines += ["| section | attacks (firm) | detections | matched | firm recall | unmatched |", "|---|---|---|---|---|---|"]
            for x in r["sections"]:
                lines.append(f"| {x['section']} | {x['attacks']} ({x['firmAttacks']}) | {x['detections']} | {x['matched']} | "
                             f"{x['recallFirm']} | {x['unmatchedDetections']} |")
            lines.append("")
        if r.get("noisyByKind"):
            lines += ["| section | kind | events | caught by response.onset | caught by response.low |", "|---|---|---|---|---|"]
            for x in r["noisyByKind"]:
                lines.append(f"| {x['section']} | {x['kind']} | {x['events']} | {x['response.onset']} | {x['response.low']} |")
            lines.append("")
        if r.get("bands"):
            lines.append(f"Band levels per section (means): `{json.dumps(r['bands'])}`")
            lines.append("")
        if "sources" in r:
            lines += ["| source | role | onset P / R | timing median ms | leaked onsets | level in own silence | notes |",
                      "|---|---|---|---|---|---|---|"]
            for k, s in r["sources"].items():
                if not s.get("available", True):
                    continue
                so = s["onsets"]
                note = ""
                if s["silentThroughout"]:
                    note = f"silent source: {s['falseActivity']}"
                if "drumClassConfusion" in s:
                    note += f"classes {s['drumClassConfusion']}; latency {s['drumClassLatencyMs']}"
                lines.append(f"| {k} | {s['role']} | {so['precision']} / {so['recall']} | {so['timingErrorMs']['median']} | "
                             f"{len(s['leakedOnsets'])} | {s['levelInOwnSilenceMax']} | {note} |")
            lines.append("")
            lines.append(f"Mix-only attribution (which sources the mix hits land on): `{json.dumps(r['mixAttribution'])}`")
            lines.append("")
        lines += ["| check | pass | value | bound | why |", "|---|---|---|---|---|"]
        for c in r["checks"]:
            verdict = ("PASS" if c["pass"] else "FAIL") + ("" if c.get("gate", True) else " (finding)")
            lines.append(f"| {c['check']} | {verdict} | {c['value']} | {c['bound']} | {c['why']} |")
        lines.append("")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    run = sub.add_parser("run")
    run.add_argument("--fixtures", type=Path, default=DEFAULT_FIXTURES)
    run.add_argument("--out", type=Path, default=REPO / "build" / "audio-lab-run")
    run.add_argument("--only", default="")
    run.add_argument("--avgen", type=Path, default=DEFAULT_AVGEN)
    run.add_argument("--no-generate", action="store_true")
    run.add_argument("--replay", type=Path, default=DEFAULT_REPLAY)
    ev = sub.add_parser("evaluate")
    ev.add_argument("run_dir", type=Path)
    ev.add_argument("--fixtures", type=Path, default=DEFAULT_FIXTURES)
    # For tests/integration/test_audio_lab_e2e.cpp, which runs the traces in-process:
    pr = sub.add_parser("prepare", help="write <run-dir>/project.json for a fixture")
    pr.add_argument("fixture_dir", type=Path)
    pr.add_argument("run_dir", type=Path)
    sc = sub.add_parser("score", help="score <run-dir>'s traces; exit 1 if a gate fails")
    sc.add_argument("fixture_dir", type=Path)
    sc.add_argument("run_dir", type=Path)
    args = ap.parse_args()

    if args.cmd == "prepare":
        write_project(args.fixture_dir, args.run_dir)
        return
    if args.cmd == "score":
        res = evaluate(args.fixture_dir, args.run_dir)
        (args.run_dir / "results.json").write_text(json.dumps(res, indent=1) + "\n")
        failed = [c for c in res["checks"] if c.get("gate", True) and not c["pass"]]
        for c in failed:
            print(f"GATE FAILED: {c['check']}: {c['value']} (bound {c['bound']})")
        print(f"score: {res['fixture']}: {sum(1 for c in res['checks'] if c.get('gate', True)) - len(failed)} gates pass, "
              f"{len(failed)} fail; {sum(1 for c in res['checks'] if not c.get('gate', True))} findings reported")
        sys.exit(1 if failed else 0)

    if args.cmd == "evaluate":
        res = evaluate(args.fixtures / args.run_dir.name, args.run_dir)
        print(json.dumps(res["checks"], indent=1))
        return
    only = [n for n in args.only.split(",") if n]
    if not args.no_generate:
        cmd = [sys.executable, str(REPO / "tools" / "audio_lab" / "make_fixtures.py"), "--out", str(args.fixtures)]
        if only:
            cmd += ["--only", ",".join(only)]
        subprocess.run(cmd, check=True)
        if not only or "drums-real" in only:  # ADR-1726: real one-shots, when this machine has them
            subprocess.run([sys.executable, str(REPO / "tools" / "audio_lab" / "make_drum_fixture.py"), "--out",
                            str(args.fixtures)], check=True)
    index = json.loads((args.fixtures / "index.json").read_text())
    results = []
    for f in index["fixtures"]:
        if only and f["name"] not in only:
            continue
        fdir = args.fixtures / f["dir"]
        if "unavailable" in json.loads((fdir / "manifest.json").read_text()):
            print(f"lab: {f['name']}: unavailable on this machine; skipped")
            continue
        rdir = args.out / f["name"]
        project = write_project(fdir, rdir)
        trace(args.avgen, project, rdir / "hop.csv", HOP_FPS, False)
        trace(args.avgen, project, rdir / "frame.csv", FRAME_FPS, True)
        replay(args.replay, fdir / "mix.wav", rdir / "live")
        res = evaluate(fdir, rdir)
        (rdir / "results.json").write_text(json.dumps(res, indent=1) + "\n")
        results.append(res)
        g = [c for c in res["checks"] if c.get("gate", True)]
        fnd = [c for c in res["checks"] if not c.get("gate", True)]
        print(f"lab: {f['name']}: gates {sum(1 for c in g if c['pass'])}/{len(g)}, findings "
              f"{sum(1 for c in fnd if c['pass'])}/{len(fnd)} pass")
    (args.out / "results.json").write_text(json.dumps({"format": "avgen-audio-lab-results", "version": 1,
                                                       "fixtures": results}, indent=1) + "\n")
    (args.out / "summary.md").write_text(summary_md(results))
    print(f"lab: {args.out / 'summary.md'}")


if __name__ == "__main__":
    main()

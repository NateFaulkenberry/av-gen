#!/usr/bin/env python3
"""Does the visual response to the music actually read? Measured from the pixels of a render, against the
owner's exact timeline (tools/liminal/pass2_grid.py).

The owner's question for art pass 2 (02-art-pass-2.md, sections 9-12, 19 phase 6) is not "is the scene
technically audio reactive" but "can you see it". This measures three things a viewer feels:

  * BEAT LOCK, per section: the frames' brightness and change are averaged over every quarter-note (and
    eighth-note) cycle of the section (an event-locked average). Anything the picture does on the beat adds
    up; everything else averages away. Reported as the swing of the averaged cycle over its noise floor
    (an SNR in dB), and as the share of the in-cycle variance that is beat-locked. Pure noise reads about
    +4 dB and a share under 0.1 (the max-min of a noisy average); a pulse a viewer feels reads above
    +12 dB with a share above 0.3. Pass 1 measured +0 to +8 dB and shares of 0.01-0.1 everywhere.
  * BIG CLAPS: the change in brightness, colour and structure in the 0-250 ms after each clap, as a z-score
    against the same measure on every other beat of its section. "Unmistakable" is z >= 3.
  * TRANSITIONS: the size of the change at each section boundary, against the median change in the section.

    python3 tools/liminal/pass2_av.py --video render.mp4 [--video-start 0] [--out dir]

Needs numpy and ffmpeg. No audio is read: the grid is the song's own (two cue markers, checked against the
onsets), so the events are where the owner says they are.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pass2_grid as G  # noqa: E402

W, H = 192, 108


def frames(path: str, fps: float) -> np.ndarray:
    cmd = ["ffmpeg", "-loglevel", "error", "-i", os.path.expanduser(path), "-vf",
           f"fps={fps},scale={W}:{H}:flags=area", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    return np.frombuffer(raw, np.uint8).reshape(-1, H, W, 3).astype(np.float32) / 255.0


def features(rgb: np.ndarray) -> dict:
    lin = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    luma = 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]
    mean = luma.mean(axis=(1, 2))
    p99 = np.percentile(luma.reshape(len(luma), -1), 99, axis=1)          # the brightest lines and lights
    chroma = (rgb.max(axis=3) - rgb.min(axis=3)).mean(axis=(1, 2))
    diff = np.zeros(len(rgb))
    diff[1:] = np.abs(rgb[1:] - rgb[:-1]).mean(axis=(1, 2, 3))             # how much the picture changes
    # structure change: gradient-magnitude images, frame to frame (geometry moving, not just brightness)
    g = np.abs(np.diff(luma, axis=1))[:, :, :-1] + np.abs(np.diff(luma, axis=2))[:, :-1, :]
    gdiff = np.zeros(len(rgb))
    gdiff[1:] = np.abs(g[1:] - g[:-1]).mean(axis=(1, 2))
    return {"mean": mean, "p99": p99, "chroma": chroma, "diff": diff, "gdiff": gdiff}


def beat_lock(sig: np.ndarray, fps: float, t0v: float, a: float, b: float, period: float, phase0: float,
              bins: int = 12) -> dict:
    """Event-locked average of `sig` over every `period` cycle in [a, b), starting at phase0. The signal is
    first high-passed by subtracting its centred one-period moving average: a ramp (a fade, a build) is
    removed exactly, and anything periodic at the beat survives."""
    win = max(3, int(round(period * fps)))
    kernel = np.ones(win) / win
    pad = np.pad(sig, (win, win), mode="edge")
    sig = sig - np.convolve(pad, kernel, mode="same")[win:-win]
    n = len(sig)
    k0 = math.ceil((a - phase0) / period - 1e-9)
    cycles = []
    k = k0
    while True:
        s = phase0 + k * period
        if s + period > b + 1e-6:
            break
        idx = [int(round((s + period * (j + 0.5) / bins - t0v) * fps)) for j in range(bins)]
        if idx[0] < 0 or idx[-1] >= n:
            k += 1
            continue
        cycles.append(sig[idx])
        k += 1
    if len(cycles) < 4:
        return {"cycles": len(cycles)}
    c = np.array(cycles)
    c = c - c.mean(axis=1, keepdims=True)            # each cycle about its own mean: slow drifts cancel
    avg = c.mean(axis=0)
    swing = float(avg.max() - avg.min())
    resid = c - avg
    noise = float(resid.std() / math.sqrt(len(cycles))) + 1e-12
    locked = float((avg ** 2).mean() / max((c ** 2).mean(), 1e-12))   # share of the in-cycle variance on the beat
    return {"cycles": len(cycles), "snr_db": round(20 * math.log10(max(swing, 1e-12) / (2 * noise)), 1),
            "locked_share": round(locked, 3), "swing": round(swing, 5),
            "peak_bin": int(np.argmax(avg)), "shape": [round(float(v), 5) for v in avg]}


def zscore_events(sig: np.ndarray, fps: float, t0v: float, times: list[float], baseline: list[float],
                  win: float = 0.25, pre: float = 0.4) -> list[float]:
    def resp(t):
        i0 = int(round((t - t0v) * fps))
        i1 = int(round((t + win - t0v) * fps))
        j0 = int(round((t - pre - t0v) * fps))
        if j0 < 0 or i1 >= len(sig):
            return None
        return float(sig[i0:i1 + 1].max() - np.median(sig[j0:i0]))
    base = [r for r in (resp(x) for x in baseline) if r is not None]
    if len(base) < 4:
        return [None for _ in times]
    mu, sd = float(np.mean(base)), float(np.std(base)) + 1e-9
    out = []
    for x in times:
        r = resp(x)
        out.append(None if r is None else round((r - mu) / sd, 1))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--video", required=True)
    ap.add_argument("--video-start", type=float, default=0.0)
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--out", default="")
    a = ap.parse_args()
    grid = G.build()
    rgb = frames(a.video, a.fps)
    f = features(rgb)
    dur = len(rgb) / a.fps
    t0v = a.video_start
    span = (t0v, t0v + dur)
    rows, claps_out, trans = [], [], []
    # ---- beat lock per section ----------------------------------------------------------------------
    for s in grid["sections"]:
        lo, hi = max(s["t0"], span[0]), min(s["t1"], span[1])
        if hi - lo < 3.0:
            continue
        bar0 = s["bars"][0]
        q = G.beat_len(bar0)
        phase0 = G.t(bar0) if bar0 < G.STEP_BAR or s["t0"] >= G.T_STEP else G.T_STEP
        row = {"section": s["id"], "name": s["name"], "t0": round(lo, 2), "t1": round(hi, 2)}
        for key in ("mean", "p99", "diff", "gdiff", "chroma"):
            row[f"q_{key}"] = beat_lock(f[key], a.fps, t0v, lo, hi, q, phase0)
            row[f"e_{key}"] = beat_lock(f[key], a.fps, t0v, lo, hi, q / 2, phase0, bins=8)
        rows.append(row)
    # ---- BIG CLAPs ------------------------------------------------------------------------------------
    by_sec = {}
    for c in grid["bigClaps"]:
        by_sec.setdefault(c["section"], []).append(c)
    for sid, cs in by_sec.items():
        s = next(x for x in grid["sections"] if x["id"] == sid)
        clap_t = [c["t"] for c in cs]
        beats = []
        bar = s["bars"][0]
        while G.t(bar) < s["t1"] - 1e-6:
            for b in (1.0, 2.0, 3.0, 4.0):
                tb = G.t(bar, b)
                if all(abs(tb - x) > 0.3 for x in clap_t):
                    beats.append(tb)
            bar += 1
        zs = {k: zscore_events(f[k], a.fps, t0v, clap_t, beats) for k in ("mean", "p99", "diff", "gdiff", "chroma")}
        for i, c in enumerate(cs):
            vals = {k: zs[k][i] for k in zs}
            best = max((v for v in vals.values() if v is not None), default=None)
            claps_out.append({"id": c["id"], "bar": f"{c['bar']}.{c['beat']:g}", "t": c["t"], "section": sid,
                              "z": vals, "best_z": best,
                              "verdict": None if best is None else ("unmistakable" if best >= 3 else
                                                                    "visible" if best >= 1.5 else "not read")})
    # ---- transitions ----------------------------------------------------------------------------------
    for s in grid["sections"][1:]:
        tb = s["t0"]
        if not (span[0] + 1 < tb < span[1] - 1):
            continue
        i = int(round((tb - t0v) * a.fps))
        local = f["diff"][max(0, i - int(4 * a.fps)):i + int(4 * a.fps)]
        peak = float(f["diff"][max(0, i - 3):i + 4].max())
        trans.append({"into": s["id"], "t": round(tb, 2), "change_vs_median": round(peak / (float(np.median(local)) + 1e-6), 1)})
    report = {"video": os.path.abspath(os.path.expanduser(a.video)), "videoStart": t0v, "seconds": round(dur, 2),
              "sections": rows, "bigClaps": claps_out, "transitions": trans}
    lines = ["# Does the visual response read? (pass2_av)", "",
             f"Video `{report['video']}`, {dur:.1f} s from film time {t0v:.2f}.", "",
             "## Beat lock per section (event-locked average; SNR in dB, share of in-cycle variance on the beat)",
             "", "| section | quarter: brightness | quarter: lines (p99) | quarter: change | eighth: change | eighth: lines |",
             "|---|---|---|---|---|---|"]

    def cell(d):
        if "snr_db" not in d:
            return "-"
        return f"{d['snr_db']:+.1f} dB ({d['locked_share']:.2f})"
    for r in rows:
        lines.append(f"| {r['name']} | {cell(r['q_mean'])} | {cell(r['q_p99'])} | {cell(r['q_diff'])} | "
                     f"{cell(r['e_diff'])} | {cell(r['e_p99'])} |")
    lines += ["", "## BIG CLAPs (z-score of the 0-250 ms response against every other beat of the section)", "",
              "| clap | bar.beat | t (s) | brightness | lines | change | structure | colour | verdict |",
              "|---|---|---|---|---|---|---|---|---|"]
    for c in claps_out:
        z = c["z"]

        def zz(k):
            return "-" if z[k] is None else f"{z[k]:+.1f}"
        lines.append(f"| {c['id']} | {c['bar']} | {c['t']:.2f} | {zz('mean')} | {zz('p99')} | {zz('diff')} | "
                     f"{zz('gdiff')} | {zz('chroma')} | {c['verdict']} |")
    lines += ["", "## Section boundaries (peak frame change at the boundary / median change around it)", ""]
    lines += [f"- into {x['into']} at {x['t']:.2f} s: {x['change_vs_median']}x" for x in trans]
    md = "\n".join(lines) + "\n"
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        with open(os.path.join(a.out, "pass2_av.json"), "w") as fh:
            json.dump(report, fh, indent=1)
        with open(os.path.join(a.out, "pass2_av.md"), "w") as fh:
            fh.write(md)
    print(md)


if __name__ == "__main__":
    main()

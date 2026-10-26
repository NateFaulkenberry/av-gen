#!/usr/bin/env python3
"""Talking Heads: does a head's gate open on the notes its stem plays? (the owner, stop 5: "i think it needs to be
more sensitive", of the synth head)

An independent reference, not the engine's: the stem is decoded (ffmpeg) and its note starts found by spectral flux
(log-magnitude STFT, half-wave rectified, adaptive median + MAD threshold, 70 ms minimum gap, only where the stem is
sounding: RMS within 40 dB of its loudest). The head's openings are the rising edges of `signal.<p>Live` in the
engine's sonic trace on the Feline stems.

    recall     the share of note starts with an opening within -60..+120 ms of them
    precision  the share of openings with a note start within that window
    open while sounding / open while silent: the gate's share of open frames when the stem sounds and when it is
               silent (a gate stuck open on a pad shows as the second climbing)

    python3 tools/talking_heads/gate_recall.py --project <talking-heads.json> --prefix th3 --stem Synth \
        [--open 0.55 --close 0.42] [--band mid] [--range 0:]
"""
import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, REPO)
import review as rv  # noqa: E402

AVGEN = os.environ.get("AVGEN") or os.path.join(REPO, "build", "release", "src", "avgen")
SR, HOP, WIN = 22050, 256, 1024


def onsets(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    x = np.frombuffer(raw, dtype=np.float32)
    n = 1 + (len(x) - WIN) // HOP
    idx = np.arange(WIN)[None, :] + HOP * np.arange(n)[:, None]
    frames = x[idx] * np.hanning(WIN)[None, :]
    mag = np.abs(np.fft.rfft(frames, axis=1))
    logm = np.log1p(100.0 * mag)
    flux = np.maximum(logm[1:] - logm[:-1], 0.0).sum(axis=1)
    flux = np.concatenate([[0.0], flux])
    rms = np.sqrt((frames ** 2).mean(axis=1))
    db = 20.0 * np.log10(rms + 1e-9)
    sounding = db > db.max() - 40.0
    t = (np.arange(n) * HOP + WIN / 2) / SR
    w = int(0.5 * SR / HOP)
    picks = []
    last = -1.0
    for i in range(1, n - 1):
        lo, hi = max(0, i - w), min(n, i + w)
        seg = flux[lo:hi]
        med = np.median(seg)
        mad = np.median(np.abs(seg - med)) + 1e-9
        if (flux[i] > med + 3.0 * mad and flux[i] >= flux[i - 1] and flux[i] >= flux[i + 1] and sounding[i]
                and t[i] - last > 0.07):
            picks.append(t[i])
            last = t[i]
    return np.array(picks), t, sounding


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", required=True)
    ap.add_argument("--prefix", default="th3")
    ap.add_argument("--stem", default="Synth")
    ap.add_argument("--open", type=float, default=None)
    ap.add_argument("--close", type=float, default=None)
    ap.add_argument("--band", default=None, help="the role band the gate reads (bass, mid, rms...)")
    ap.add_argument("--range", default="0:")
    a = ap.parse_args()
    P = a.prefix
    work = tempfile.mkdtemp(prefix="th-recall-")
    proj = rv.variant(a.project, "feline", True, work)
    doc = json.load(open(proj))
    for src in doc["sources"]:
        st = src.get("settings", {})
        if src.get("name") == P + "GateRaw" and a.open is not None:
            st["inputs"][0]["chain"]["thresholdLevel"] = a.open
        if src.get("name") == P + "GateKeep" and a.close is not None:
            st["inputs"][0]["chain"]["thresholdLevel"] = a.close
        if src.get("name") == P + "Level" and a.band:
            role = st["inputs"][0]["source"].split(".")[1]
            st["inputs"][0]["source"] = "role.%s.%s" % (role, a.band)
    json.dump(doc, open(proj, "w"), indent=1)
    trace = os.path.join(work, "t.csv")
    subprocess.run([AVGEN, "--project", proj, "--sonic-trace", trace], capture_output=True, check=True)
    rows = list(csv.DictReader(open(trace)))
    tt = np.array([float(r["time"]) for r in rows])
    live = np.array([float(r["signal.%sLive" % P]) for r in rows])
    lo, hi = (a.range.split(":") + [""])[:2]
    lo = float(lo or 0.0)
    hi = float(hi) if hi else tt[-1]
    opens = tt[1:][(live[1:] > 0.5) & (live[:-1] <= 0.5)]
    opens = opens[(opens >= lo) & (opens <= hi)]
    stem = os.path.join(REPO, "assets", "audio", "stems", "feline-footwear (%s).wav" % a.stem)
    on, ft, sounding = onsets(stem)
    on = on[(on >= lo) & (on <= hi)]
    hit = lambda x, ys: np.any((ys >= x - 0.06) & (ys <= x + 0.12))
    recall = np.mean([hit(o, opens) for o in on]) if len(on) else 0.0
    precision = np.mean([np.any((on >= x - 0.12) & (on <= x + 0.06)) for x in opens]) if len(opens) else 0.0
    snd = np.interp(tt, ft, sounding.astype(float)) > 0.5
    sel = (tt >= lo) & (tt <= hi)
    open_snd = (live[sel & snd] > 0.5).mean() if np.any(sel & snd) else 0.0
    open_sil = (live[sel & ~snd] > 0.5).mean() if np.any(sel & ~snd) else 0.0
    print("%s on %s (open %s, close %s, band %s): %d note starts, %d openings | recall %.0f%%, precision %.0f%% | "
          "open %.0f%% of sounding time, %.0f%% of silent time" % (
              P, a.stem, a.open, a.close, a.band, len(on), len(opens), 100 * recall, 100 * precision,
              100 * open_snd, 100 * open_sil))


if __name__ == "__main__":
    main()

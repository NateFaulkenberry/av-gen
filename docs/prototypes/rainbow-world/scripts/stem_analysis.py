#!/usr/bin/env python3
"""Rainbow World 2: a reproducible analysis of the Feline Footwear stems (brief 13, Phase 2).

numpy + matplotlib only (the environment has no scipy or librosa). Reads the three stems and the mix, and writes:
  <out>/analysis.json        every number the report quotes (metadata, levels, alignment, events, sections, tempo)
  <out>/events_<stem>.csv    the detected events (time, class, strength)
  <out>/*.png                overview, spectrograms, combined timeline, zoomed bars

  python3 docs/prototypes/rainbow-world/scripts/stem_analysis.py build/rw2/stems
      [--stems assets/audio/stems] [--mix assets/audio/feline-footwear.wav]

The detectors here are for understanding the music. The scene is driven by the engine's own `stem.<key>.*` features
(ADR-1520), which the report compares against these.
"""
import argparse
import csv
import json
import math
import os
import wave

import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--stems", default="assets/audio/stems")
ap.add_argument("--mix", default="assets/audio/feline-footwear.wav")
ap.add_argument("--no-plots", action="store_true")
ap.add_argument("--dpi", type=int, default=80)
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)

STEMS = {"drums": "feline-footwear (Drums).wav", "bass": "feline-footwear (Bass).wav",
         "synth": "feline-footwear (Synth).wav"}


def read_wav(path):
    with wave.open(path, "rb") as w:
        ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x).astype(np.float32) / float(1 << 23)
    elif sw == 2:
        x = np.frombuffer(raw, np.int16).astype(np.float32) / 32768.0
    else:
        raise SystemExit("unsupported sample width %d" % sw)
    return x.reshape(-1, ch), sr, sw * 8


def db(x, floor=-120.0):
    return np.maximum(20.0 * np.log10(np.maximum(x, 1e-12)), floor)


HOP, WIN = 512, 2048


def stft_mag(mono, sr):
    n = 1 + (len(mono) - WIN) // HOP
    idx = np.arange(WIN)[None, :] + HOP * np.arange(n)[:, None]
    win = np.hanning(WIN).astype(np.float32)
    out = np.empty((n, WIN // 2 + 1), np.float32)
    for k in range(0, n, 2048):  # chunked: memory
        out[k:k + 2048] = np.abs(np.fft.rfft(mono[idx[k:k + 2048]] * win, axis=1)) * (2.0 / win.sum())
    t = (np.arange(n) * HOP + WIN / 2) / sr
    f = np.fft.rfftfreq(WIN, 1.0 / sr)
    return out, t, f


def band(mag, f, lo, hi):
    m = (f >= lo) & (f < hi)
    return np.sqrt(np.mean(mag[:, m] ** 2, axis=1) + 1e-20)


def log_flux(mag, f, lo, hi, lam=100.0):
    m = (f >= lo) & (f < hi)
    L = np.log1p(lam * mag[:, m])
    d = np.diff(L, axis=0, prepend=L[:1])
    return np.mean(np.maximum(d, 0.0), axis=1)


def peaks(odf, t, rate, k_pre=0.06, k_post=0.06, avg=0.25, delta=0.0, wait=0.07, mult=1.5):
    """Local-max peak picking over an adaptive (moving-median-ish) threshold: Boeck et al. 2012's rules."""
    pre, post, w = int(k_pre * rate), int(k_post * rate), int(avg * rate)
    n = len(odf)
    # moving mean over a centred window as the adaptive baseline (median would need scipy)
    c = np.cumsum(np.concatenate([[0.0], odf]))
    lo_ = np.clip(np.arange(n) - w, 0, n)
    hi_ = np.clip(np.arange(n) + w + 1, 0, n)
    mean = (c[hi_] - c[lo_]) / np.maximum(hi_ - lo_, 1)
    out, last = [], -1e9
    for i in range(n):
        a0, a1 = max(0, i - pre), min(n, i + post + 1)
        if odf[i] < odf[a0:a1].max() or odf[i] < mult * mean[i] + delta:
            continue
        if t[i] - last < wait:
            continue
        out.append(i)
        last = t[i]
    return np.array(out, int)


report = {"files": {}, "stems": {}}
data = {}
for key, fn in list(STEMS.items()) + [("mix", None)]:
    path = a.mix if key == "mix" else os.path.join(a.stems, fn)
    x, sr, bits = read_wav(path)
    data[key] = (x, sr)
    peak = float(np.abs(x).max())
    rms = float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))
    lead = int(np.argmax(np.any(np.abs(x) > 10 ** (-60 / 20), axis=1)))
    tail = int(len(x) - np.argmax(np.any(np.abs(x[::-1]) > 10 ** (-60 / 20), axis=1)))
    L, R = x[:, 0].astype(np.float64), x[:, 1].astype(np.float64)
    corr = float(np.dot(L, R) / math.sqrt(np.dot(L, L) * np.dot(R, R) + 1e-30))
    side = float(np.sqrt(np.mean(((L - R) / 2) ** 2)) / max(1e-12, np.sqrt(np.mean(((L + R) / 2) ** 2))))
    report["files"][key] = {"path": path, "sampleRate": sr, "bits": bits, "channels": int(x.shape[1]),
                            "frames": int(len(x)), "seconds": round(len(x) / sr, 4),
                            "peakDbfs": round(float(db(peak)), 2), "rmsDbfs": round(float(db(rms)), 2),
                            "crestDb": round(float(db(peak) - db(rms)), 2),
                            "firstSoundS": round(lead / sr, 4), "lastSoundS": round(tail / sr, 4),
                            "lrCorrelation": round(corr, 4), "sideToMid": round(side, 4)}
    print(key, report["files"][key])

# ---- alignment: sum of stems against the mix ----------------------------------------------------------------------
sr = data["mix"][1]
n = min(len(data[k][0]) for k in data)
ssum = sum(data[k][0][:n].astype(np.float64) for k in STEMS)
mix = data["mix"][0][:n].astype(np.float64)
res = mix - ssum
report["alignment"] = {
    "sumVsMixResidualDb": round(float(db(np.sqrt(np.mean(res ** 2))) - db(np.sqrt(np.mean(mix ** 2)))), 2),
}
# best integer lag of the sum against the mix (+-50 ms) on 20 s of material
seg = slice(int(60 * sr), int(80 * sr))
mm, ss = mix[seg, 0] + mix[seg, 1], ssum[seg, 0] + ssum[seg, 1]
best = (None, -1)
for lag in range(-2400, 2401, 1 if False else 8):
    c = float(np.dot(mm[2400:-2400], np.roll(ss, lag)[2400:-2400]))
    if c > best[1]:
        best = (lag, c)
fine = best[0]
for lag in range(best[0] - 8, best[0] + 9):
    c = float(np.dot(mm[2400:-2400], np.roll(ss, lag)[2400:-2400]))
    if c > best[1]:
        best = (lag, c)
report["alignment"]["bestLagSamples"] = best[0]
# the gain that best fits the sum to the mix
g = float(np.dot(mix.ravel(), ssum.ravel()) / np.dot(ssum.ravel(), ssum.ravel()))
report["alignment"]["sumGainToMix"] = round(g, 4)
res2 = mix - g * ssum
report["alignment"]["residualAfterGainDb"] = round(float(db(np.sqrt(np.mean(res2 ** 2))) - db(np.sqrt(np.mean(mix ** 2)))), 2)
print("alignment", report["alignment"])

# ---- per-stem features ----------------------------------------------------------------------------------------------
feat = {}
for key in list(STEMS) + ["mix"]:
    x, sr = data[key]
    mono = x.mean(axis=1).astype(np.float32)
    mag, t, f = stft_mag(mono, sr)
    rate = sr / HOP
    rms = np.sqrt(np.convolve(mono.astype(np.float64) ** 2, np.ones(WIN) / WIN, "same")[::HOP][:len(t)] + 1e-20)
    fr = {
        "t": t, "rmsDb": db(rms),
        "sub": db(band(mag, f, 20, 60)), "low": db(band(mag, f, 60, 150)), "lowMid": db(band(mag, f, 150, 500)),
        "mid": db(band(mag, f, 500, 2000)), "highMid": db(band(mag, f, 2000, 6000)), "high": db(band(mag, f, 6000, 16000)),
    }
    p = mag ** 2
    fr["centroid"] = (p @ f) / np.maximum(p.sum(axis=1), 1e-20)
    fr["fluxAll"] = log_flux(mag, f, 30, 16000)
    fr["fluxLow"] = log_flux(mag, f, 30, 150)
    fr["fluxMid"] = log_flux(mag, f, 150, 4000)
    fr["fluxHigh"] = log_flux(mag, f, 5000, 16000)
    # dominant pitch in the bass register (harmonic product spectrum, 30-400 Hz)
    if key in ("bass", "synth"):
        lo_b, hi_b = (30, 400) if key == "bass" else (150, 2000)
        m = (f >= lo_b) & (f < hi_b)
        sub = mag[:, m]
        fi = f[m]
        fr["pitchHz"] = np.where(sub.max(axis=1) > 1e-3 * max(1e-9, sub.max()), fi[np.argmax(sub, axis=1)], np.nan)
    feat[key] = (fr, mag, f, rate)

# ---- events -----------------------------------------------------------------------------------------------------------
# Drums: every onset of the whole-band log flux, classified by what rises in the 40 ms after it against the 30 ms
# before: the sub/low band (a kick), the 2-6 kHz band (a clap or snare), the 6-16 kHz band alone (a hat or tick).
# An onset can be a kick AND a clap (the backbeat layers them).
events = {}
fr, mag, f, rate = feat["drums"]
t = fr["t"]
on_i = peaks(fr["fluxAll"], t, rate, mult=1.6, wait=0.045)
ev = []
for i in on_i:
    a0, a1 = max(0, i - 3), i
    b0, b1 = i, min(len(t), i + 4)
    rise = {b_: float(fr[b_][b0:b1].max() - fr[b_][a0:a1].mean()) if a1 > a0 else 0.0
            for b_ in ("sub", "low", "lowMid", "mid", "highMid", "high")}
    peak_ = {b_: float(fr[b_][b0:b1].max()) for b_ in ("sub", "low", "mid", "highMid", "high")}
    cls = []
    if max(rise["sub"], rise["low"]) > 15 and max(peak_["sub"], peak_["low"]) > -30:
        cls.append("kick")
    if rise["highMid"] > 10 and peak_["highMid"] > -42 and peak_["mid"] > -34:
        cls.append("clap")
    if not cls and rise["high"] > 6 and peak_["high"] > -52:
        cls.append("hat")
    if not cls:
        cls.append("perc")
    s_ = float(fr["fluxAll"][i])
    for c_ in cls:
        ev.append((float(t[i]), c_, s_, round(peak_["sub"], 1), round(peak_["highMid"], 1), round(peak_["high"], 1)))
ev.sort()
# strength: flux over its p95 per class
for c_ in {e[1] for e in ev}:
    v = np.array([e[2] for e in ev if e[1] == c_])
    p95 = max(1e-9, float(np.percentile(v, 95)))
    ev = [(e[0], e[1], min(1.5, e[2] / p95) if e[1] == c_ else e[2]) + tuple(e[3:]) for e in ev]
events["drums"] = ev
for key in ("bass", "synth"):
    frk, magk, fk, ratek = feat[key]
    idx = peaks(frk["fluxAll"], frk["t"], ratek, mult=1.8, wait=0.06)
    v = frk["fluxAll"][idx]
    p95 = max(1e-9, float(np.percentile(v, 95))) if len(v) else 1.0
    events[key] = [(float(frk["t"][i]), "onset", float(min(1.5, frk["fluxAll"][i] / p95))) for i in idx]
for key, ev_ in events.items():
    with open(os.path.join(a.out, "events_%s.csv" % key), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["time", "class", "strength", "subDb", "highMidDb", "highDb"][:len(ev_[0]) if ev_ else 3])
        for row in ev_:
            w.writerow([round(row[0], 4), row[1], round(row[2], 3)] + list(row[3:]))

# ---- tempo: the drum onset function's autocorrelation at the bar lag (four-on-the-floor gives a strong bar peak; the
# beat is a quarter of it), then the phase that best fits the kicks -------------------------------------------------------
odf = feat["drums"][0]["fluxAll"]
odf = odf - odf.mean()
ac = np.correlate(odf, odf, "full")[len(odf) - 1:]
lags = np.arange(len(ac)) / rate
m = (lags > 1.6) & (lags < 2.6)
bar_lag = lags[m][np.argmax(ac[m])]
kt = np.array([e[0] for e in events["drums"] if e[1] == "kick"])


def fit_grid(times, period):
    z = np.mean(np.exp(2j * np.pi * times / period))
    return (np.angle(z) / (2 * np.pi)) * period % period, float(np.abs(z))


best = None
for p_ in np.linspace(bar_lag / 4 * 0.99, bar_lag / 4 * 1.01, 2001):
    ph, R = fit_grid(kt, p_)
    if best is None or R > best[2]:
        best = (p_, ph, R)
period, phase, R = best
dev = ((kt - phase + period / 2) % period) - period / 2
report["tempo"] = {"barLagS": round(float(bar_lag), 4), "bpm": round(60.0 / period, 3), "beatPeriodS": round(float(period), 5),
                   "firstBeatS": round(float(phase), 4), "kickPhaseCoherence": round(R, 4),
                   "kickDeviationMsP50": round(float(np.median(np.abs(dev)) * 1000), 2),
                   "kickDeviationMsP95": round(float(np.percentile(np.abs(dev), 95) * 1000), 2),
                   "kicksOnGridShare": round(float(np.mean(np.abs(dev) < 0.03)), 3)}
print("tempo", report["tempo"])

# ---- per-bar activity: kicks, snares, hats, bass/synth onsets and levels per bar (4 beats) --------------------------
bar = 4 * period
nbars = int(math.ceil((data["mix"][0].shape[0] / sr - phase) / bar))
rows = []
for b in range(nbars):
    t0 = phase + b * bar
    t1 = t0 + bar
    row = {"bar": b + 1, "t0": round(t0, 3)}
    for cls in ("kick", "clap", "hat", "perc"):
        row[cls] = sum(1 for e in events["drums"] if e[1] == cls and t0 <= e[0] < t1)
    for key in ("bass", "synth"):
        row[key + "On"] = sum(1 for e in events[key] if t0 <= e[0] < t1)
    for key in ("drums", "bass", "synth", "mix"):
        frk = feat[key][0]
        mk = (frk["t"] >= t0) & (frk["t"] < t1)
        row[key + "Db"] = round(float(10 * np.log10(np.mean(10 ** (frk["rmsDb"][mk] / 10)) + 1e-20)), 1) if mk.any() else -120.0
        if key == "synth" and mk.any():
            row["synthCentroid"] = round(float(np.median(frk["centroid"][mk])), 0)
        if key == "bass" and mk.any():
            ph_ = frk["pitchHz"][mk]
            row["bassPitchHz"] = round(float(np.nanmedian(ph_)), 1) if np.isfinite(ph_).any() else None
    rows.append(row)
report["bars"] = rows
with open(os.path.join(a.out, "bars.csv"), "w", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)

# ---- grid transcription: the track is grid-locked (kicks within a few ms of a 121 BPM grid), so every 16th step of
# every bar is read for what rises on it: kick (sub/low), offbeat hat (high on the "and"), tick (mid on the "a"),
# clap (2-6 kHz louder on beats 2 and 4 than on 1 and 3), 16th hats (high on the "e") ------------------------------
step = period / 4
BANDS = ("sub", "low", "lowMid", "mid", "highMid", "high")


def step_read(key, s0):
    frk = feat[key][0]
    tt = frk["t"]
    m = (tt >= s0 - 0.012) & (tt < s0 + 0.05)
    pm = (tt >= s0 - 0.05) & (tt < s0 - 0.012)
    if not m.any() or not pm.any():
        return None
    return {b_: (float(frk[b_][m].max()), float(frk[b_][m].max() - max(frk[b_][pm].mean(), -90.0))) for b_ in BANDS}


layers = []
for b in range(nbars):
    t0 = phase + b * bar
    st = [step_read("drums", t0 + k * step) for k in range(16)]
    if any(x is None for x in st):
        break
    row = {"bar": b + 1, "t0": round(t0, 3)}
    row["kick"] = sum(1 for k in (0, 4, 8, 12) if max(st[k]["sub"][1], st[k]["low"][1]) > 20 and st[k]["sub"][0] > -40)
    row["offHat"] = sum(1 for k in (2, 6, 10, 14) if st[k]["high"][1] > 22)
    row["tick"] = sum(1 for k in (3, 7, 11, 15) if st[k]["mid"][1] > 18)
    row["hat16"] = sum(1 for k in (1, 5, 9, 13) if st[k]["high"][1] > 15)
    back = np.mean([st[k]["highMid"][0] for k in (4, 12)]) - np.mean([st[k]["highMid"][0] for k in (0, 8)])
    row["clapDb"] = round(float(back), 1)
    row["clap"] = int(back > 3.5)
    layers.append(row)
report["drumLayers"] = layers
with open(os.path.join(a.out, "drum_layers.csv"), "w", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=list(layers[0].keys()))
    w.writeheader()
    w.writerows(layers)

# bass: where in the bar its onsets fall (16th positions), and its pitch per beat
bass_grid = []
bt_ = np.array([e[0] for e in events["bass"]])
for b in range(nbars):
    t0 = phase + b * bar
    pos = [int(round((x - t0) / step)) for x in bt_ if t0 - step / 2 <= x < t0 + bar - step / 2]
    frk = feat["bass"][0]
    pitches = []
    for q in range(4):
        mk = (frk["t"] >= t0 + q * period) & (frk["t"] < t0 + (q + 1) * period)
        ph_ = frk["pitchHz"][mk] if mk.any() else np.array([np.nan])
        pitches.append(round(float(np.nanmedian(ph_)), 1) if np.isfinite(ph_).any() else None)
    bass_grid.append({"bar": b + 1, "steps": "".join("x" if k in pos else "." for k in range(16)), "pitchPerBeatHz": pitches})
report["bassGrid"] = bass_grid

# ---- per-stem summaries ----------------------------------------------------------------------------------------------
for key in list(STEMS) + ["mix"]:
    frk = feat[key][0]
    act = frk["rmsDb"] > -50
    s = {"activeShare": round(float(act.mean()), 3),
         "rmsDbP10": round(float(np.percentile(frk["rmsDb"][act], 10)), 1) if act.any() else None,
         "rmsDbP50": round(float(np.percentile(frk["rmsDb"][act], 50)), 1) if act.any() else None,
         "rmsDbP90": round(float(np.percentile(frk["rmsDb"][act], 90)), 1) if act.any() else None,
         "centroidHzP50": round(float(np.median(frk["centroid"][act])), 0) if act.any() else None}
    tot = sum(10 ** (np.mean(frk[b_][act]) / 10) for b_ in ("sub", "low", "lowMid", "mid", "highMid", "high")) if act.any() else 1
    s["bandShare"] = {b_: round(float(10 ** (np.mean(frk[b_][act]) / 10) / tot), 3)
                      for b_ in ("sub", "low", "lowMid", "mid", "highMid", "high")} if act.any() else {}
    if key in events:
        s["events"] = {}
        for cls in sorted({e[1] for e in events[key]}):
            ts = np.array([e[0] for e in events[key] if e[1] == cls])
            s["events"][cls] = {"count": int(len(ts)), "perSecond": round(len(ts) / (len(frk["t"]) / rate), 2),
                                "medianIoiMs": round(float(np.median(np.diff(ts)) * 1000), 1) if len(ts) > 2 else None}
    report["stems"][key] = s
with open(os.path.join(a.out, "analysis.json"), "w") as fh:
    json.dump(report, fh, indent=1)
np.savez_compressed(os.path.join(a.out, "features.npz"),
                    **{"%s_%s" % (k, n_): v for k in feat for n_, v in feat[k][0].items()})

if a.no_plots:
    raise SystemExit(0)

# ---- plots ---------------------------------------------------------------------------------------------------------------
import matplotlib  # noqa: E402

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

COL = {"drums": "#E8603C", "bass": "#3C78D8", "synth": "#A64CC8", "mix": "#444444"}
dur = data["mix"][0].shape[0] / sr

# 1. overview: rms per stem + bands
fig, axs = plt.subplots(4, 1, figsize=(16, 10), sharex=True)
for ax, key in zip(axs, ["drums", "bass", "synth", "mix"]):
    frk = feat[key][0]
    for b_, c_ in (("sub", "#203060"), ("low", "#3C78D8"), ("lowMid", "#48A060"), ("mid", "#C8A030"),
                   ("highMid", "#E07030"), ("high", "#D03070")):
        sm = np.convolve(frk[b_], np.ones(23) / 23, "same")
        ax.plot(frk["t"], sm, lw=0.6, color=c_, label=b_)
    ax.plot(frk["t"], np.convolve(frk["rmsDb"], np.ones(23) / 23, "same"), lw=1.2, color="k", label="rms")
    ax.set_ylim(-90, -10)
    ax.set_ylabel(key + " dB")
    for b in range(0, nbars, 8):
        ax.axvline(phase + b * bar, color="#999", lw=0.4)
axs[0].legend(ncol=7, fontsize=7, loc="lower right")
axs[-1].set_xlabel("seconds (grey lines: every 8 bars)")
fig.suptitle("Feline Footwear stems: levels and bands (smoothed 0.25 s)")
fig.tight_layout()
fig.savefig(os.path.join(a.out, "01-overview.png"), dpi=a.dpi)
plt.close(fig)

# 2. spectrograms
fig, axs = plt.subplots(3, 1, figsize=(16, 10), sharex=True)
for ax, key in zip(axs, STEMS):
    frk, magk, fk, ratek = feat[key]
    S = db(magk[::4].T + 1e-9)
    ax.imshow(S, origin="lower", aspect="auto", extent=[0, frk["t"][-1], 0, fk[-1]], vmin=S.max() - 80, vmax=S.max(),
              cmap="magma")
    ax.set_yscale("symlog", linthresh=200)
    ax.set_ylim(30, 16000)
    ax.set_ylabel(key + " Hz")
axs[-1].set_xlabel("seconds")
fig.suptitle("Spectrograms (log frequency)")
fig.tight_layout()
fig.savefig(os.path.join(a.out, "02-spectrograms.png"), dpi=a.dpi)
plt.close(fig)

# 3. combined timeline: events per bar
fig, axs = plt.subplots(5, 1, figsize=(16, 9), sharex=True)
bt = [r["t0"] for r in rows]
for ax, (k_, lab) in zip(axs, [("kick", "kicks/bar"), ("clap", "claps/bar"), ("hat", "hats/bar"),
                                ("bassOn", "bass onsets/bar"), ("synthOn", "synth onsets/bar")]):
    ax.bar(bt, [r[k_] for r in rows], width=bar * 0.9, align="edge",
           color=COL["drums"] if k_ in ("kick", "clap", "hat") else COL[k_[:-2]])
    ax.set_ylabel(lab, fontsize=8)
ax2 = axs[-1].twinx()
ax2.plot([r["t0"] + bar / 2 for r in rows], [r.get("synthCentroid", 0) for r in rows], color="k", lw=1)
ax2.set_ylabel("synth centroid Hz", fontsize=8)
axs[-1].set_xlabel("seconds")
fig.suptitle("Events per bar (%.2f BPM grid)" % (60 / period))
fig.tight_layout()
fig.savefig(os.path.join(a.out, "03-events-per-bar.png"), dpi=a.dpi)
plt.close(fig)

# 4. zoom: 4 bars at a few places, every stem's waveform envelope and events
for zi, z0 in enumerate([30.0, 75.0, 120.0, 165.0]):
    z0 = phase + round((z0 - phase) / bar) * bar
    z1 = z0 + 2 * bar
    fig, axs = plt.subplots(3, 1, figsize=(16, 7), sharex=True)
    for ax, key in zip(axs, STEMS):
        x, sr_ = data[key]
        seg_ = x[int(z0 * sr_):int(z1 * sr_)].mean(axis=1)
        tt = z0 + np.arange(len(seg_)) / sr_
        ax.plot(tt[::8], seg_[::8], lw=0.3, color=COL[key])
        for e in events[key]:
            if z0 <= e[0] < z1:
                c_ = {"kick": "k", "clap": "r", "hat": "#999", "perc": "#c90", "onset": "k"}[e[1]]
                ax.axvline(e[0], color=c_, lw=0.8 if e[1] != "hat" else 0.4, ymax=min(1, 0.3 + 0.5 * e[2]))
        for b in range(9):
            ax.axvline(z0 + b * period, color="#3a3", lw=0.5, ls=":")
        ax.set_ylabel(key)
    axs[-1].set_xlabel("seconds (green dotted: beats; black kick/onset, red clap, grey hat, ochre other)")
    fig.tight_layout()
    fig.savefig(os.path.join(a.out, "04-zoom-%d.png" % zi), dpi=a.dpi)
    plt.close(fig)
print("done", a.out)

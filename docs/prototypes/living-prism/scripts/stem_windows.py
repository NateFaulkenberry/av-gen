#!/usr/bin/env python3
"""The Living Prism, Stage 1: confirm the Feline Footwear stems and choose the two performance windows (spec 12.2).

numpy + matplotlib only. Reads the three stems and the mix and writes:
  <out>/windows.json   file checks, alignment, the per-bar table and the chosen windows
  <out>/bars.csv       the per-bar table
  <out>/windows.png    the per-bar activity with the windows marked

  python3 docs/prototypes/living-prism/scripts/stem_windows.py build/prism/stage1
      [--stems assets/audio/stems] [--mix assets/audio/feline-footwear.wav]

What it measures per bar (121 BPM, first downbeat 0.2425 s; Rainbow World 14 section 2):
  - drum onsets: peaks of the drum stem's broadband log spectral flux (the scene's impact source);
  - bass and synth level: RMS in dBFS, and the bass's note onsets;
  - synth brightness: the spectral centroid where the synth sounds.
The "load" of a bar is a proxy for what the scene will have to draw at once: overlapping drum impacts (fronts,
ripples, glints), bass energy (broad pulses) and synth energy and brightness (material response, palette drift).
It is a ranking device for choosing windows, not a cost model; the windows are confirmed by a profiling pass.
"""
import argparse
import csv
import json
import os
import wave

import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--stems", default="assets/audio/stems")
ap.add_argument("--mix", default="assets/audio/feline-footwear.wav")
ap.add_argument("--window-bars", type=int, default=10)
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)

STEMS = {"drums": "feline-footwear (Drums).wav", "bass": "feline-footwear (Bass).wav",
         "synth": "feline-footwear (Synth).wav"}
BPM, DOWNBEAT = 121.0, 0.2425
BAR = 4 * 60.0 / BPM
HOP, WIN = 512, 2048


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
    return x.reshape(-1, ch), sr, sw * 8, ch


def db(x, floor=-120.0):
    return np.maximum(20.0 * np.log10(np.maximum(x, 1e-12)), floor)


def stft_mag(mono, sr):
    n = 1 + (len(mono) - WIN) // HOP
    win = np.hanning(WIN).astype(np.float32)
    out = np.empty((n, WIN // 2 + 1), np.float32)
    for k in range(0, n, 2048):
        idx = np.arange(WIN)[None, :] + HOP * np.arange(k, min(n, k + 2048))[:, None]
        out[k:k + 2048] = np.abs(np.fft.rfft(mono[idx] * win, axis=1)) * (2.0 / win.sum())
    return out, (np.arange(n) * HOP + WIN / 2) / sr, np.fft.rfftfreq(WIN, 1.0 / sr)


def onsets(mag, t, lam=100.0, gap=0.06):
    """Peaks of the broadband log flux over an adaptive (median + k * MAD, 1 s) threshold."""
    L = np.log1p(lam * mag)
    d = np.mean(np.maximum(np.diff(L, axis=0, prepend=L[:1]), 0.0), axis=1)
    k = int(round(1.0 / (t[1] - t[0])))
    pad = np.pad(d, (k // 2, k // 2), mode="edge")
    med = np.array([np.median(pad[i:i + k]) for i in range(0, len(d))])
    thr = med + 0.5 * np.std(d) + 1e-4
    ev, last = [], -1.0
    for i in range(1, len(d) - 1):
        if d[i] > thr[i] and d[i] >= d[i - 1] and d[i] >= d[i + 1] and t[i] - last > gap:
            ev.append((float(t[i]), float(d[i])))
            last = t[i]
    return ev


data, meta = {}, {}
for key, name in STEMS.items():
    x, sr, bits, ch = read_wav(os.path.join(a.stems, name))
    data[key], meta[key] = x, {"file": name, "sr": sr, "bits": bits, "channels": ch, "frames": len(x),
                               "seconds": round(len(x) / sr, 4)}
mix, sr, bits, ch = read_wav(a.mix)
meta["mix"] = {"file": os.path.basename(a.mix), "sr": sr, "bits": bits, "channels": ch, "frames": len(mix),
               "seconds": round(len(mix) / sr, 4)}

# alignment: the stems' sum against the mix (best lag in +-2048 samples on a 30 s excerpt, then the residual)
n = min(len(v) for v in data.values())
s = data["drums"][:n] + data["bass"][:n] + data["synth"][:n]
m = mix[:n]
seg = slice(int(60 * sr), int(90 * sr))
xs, xm = s[seg, 0], m[seg, 0]
lags = range(-2048, 2049, 1)
best = max(lags, key=lambda L: float(np.dot(xs[2048 + L: len(xs) - 2048 + L], xm[2048: len(xm) - 2048])))
gain = float(np.dot(s.ravel(), m.ravel()) / max(np.dot(s.ravel(), s.ravel()), 1e-12))
resid = float(db(np.sqrt(np.mean((m - gain * s) ** 2))) - db(np.sqrt(np.mean(m ** 2))))
align = {"bestLagSamples": int(best), "gain": round(gain, 5), "residualDb": round(resid, 2)}

feat = {}
for key in list(STEMS) + ["mix"]:
    x = mix if key == "mix" else data[key]
    mono = x.mean(axis=1)
    mag, t, f = stft_mag(mono, sr)
    c2 = np.concatenate([[0.0], np.cumsum(mono.astype(np.float64) ** 2)])
    starts = np.arange(len(t)) * HOP
    rms = np.sqrt((c2[starts + WIN] - c2[starts]) / WIN + 1e-20)  # time-domain RMS per analysis frame
    cen = (mag * f[None, :]).sum(axis=1) / np.maximum(mag.sum(axis=1), 1e-12)
    peak = float(np.max(np.abs(x)))
    full_rms = float(np.sqrt(np.mean(x ** 2)))
    loud = np.nonzero(db(np.abs(mono)) > -60)[0]
    meta[key].update({"peakDb": round(float(db(peak)), 2), "rmsDb": round(float(db(full_rms)), 2),
                      "firstSound": round(loud[0] / sr, 3) if len(loud) else None,
                      "lastSound": round(loud[-1] / sr, 3) if len(loud) else None})
    feat[key] = {"t": t, "rms": rms, "cen": cen,
                 "on": onsets(mag, t) if key in ("drums", "bass", "synth") else []}

dur = meta["mix"]["seconds"]
nbars = int((dur - DOWNBEAT) // BAR) + 1
rows = []
for b in range(nbars):
    t0, t1 = DOWNBEAT + b * BAR, DOWNBEAT + (b + 1) * BAR
    r = {"bar": b + 1, "t0": round(t0, 3), "t1": round(t1, 3)}
    for key in ("drums", "bass", "synth", "mix"):
        F = feat[key]
        sel = (F["t"] >= t0) & (F["t"] < t1)
        lv = F["rms"][sel]
        r[key + "Db"] = round(float(db(np.sqrt(np.mean(lv ** 2)))) if lv.size else -120.0, 1)
        if key in ("drums", "bass", "synth"):
            r[key + "Onsets"] = sum(1 for (te, _) in F["on"] if t0 <= te < t1)
        if key == "synth":
            loud = sel & (db(F["rms"]) > -45)
            r["synthCentroid"] = round(float(np.median(F["cen"][loud])), 0) if loud.any() else 0.0
    rows.append(r)


def norm(v, lo, hi):
    return float(np.clip((v - lo) / (hi - lo), 0.0, 1.0))


for r in rows:
    # the load proxy: impacts dominate (each one is a front + ripple + glint), then the broad responses
    r["load"] = round(0.45 * norm(r["drumsOnsets"], 0, 32) + 0.2 * norm(r["bassDb"], -45, -12) +
                      0.2 * norm(r["synthDb"], -45, -14) + 0.15 * norm(r["synthCentroid"], 300, 1100), 3)

W = a.window_bars
loads = np.array([r["load"] for r in rows])
win = np.array([loads[i:i + W].mean() for i in range(len(rows) - W + 1)])
heavy = int(np.argmax(win))
# typical: inside the grooves (drums and synth bed both present), the window whose mean load is nearest the median
# of all groove bars and whose spread is small (a steady stretch, not a transition)
groove = [i for i, r in enumerate(rows) if r["drumsOnsets"] >= 8 and r["synthDb"] > -30]
med = float(np.median(loads[groove]))
cands = [i for i in range(len(rows) - W + 1) if all(j in groove for j in range(i, i + W))
         and abs(i - heavy) >= W]
typ = min(cands, key=lambda i: abs(win[i] - med) + 0.5 * float(np.std(loads[i:i + W])))


def window(i, label):
    rs = rows[i:i + W]
    return {"label": label, "bars": [rs[0]["bar"], rs[-1]["bar"]], "start": rs[0]["t0"], "end": rs[-1]["t1"],
            "meanLoad": round(float(win[i]), 3),
            "drumOnsetsPerBar": round(float(np.mean([r["drumsOnsets"] for r in rs])), 1),
            "bassDb": round(float(np.mean([r["bassDb"] for r in rs])), 1),
            "synthDb": round(float(np.mean([r["synthDb"] for r in rs])), 1),
            "synthCentroid": round(float(np.mean([r["synthCentroid"] for r in rs])), 0)}


out = {"files": meta, "alignment": align, "grid": {"bpm": BPM, "downbeat": DOWNBEAT, "bar": round(BAR, 5)},
       "windowBars": W, "grooveMedianLoad": round(med, 3),
       "typical": window(typ, "typical"), "heaviest": window(heavy, "heaviest"), "bars": rows}
with open(os.path.join(a.out, "windows.json"), "w") as f:
    json.dump(out, f, indent=1)
with open(os.path.join(a.out, "bars.csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)

import matplotlib  # noqa: E402
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

fig, ax = plt.subplots(4, 1, figsize=(14, 8), sharex=True)
tt = [r["t0"] for r in rows]
ax[0].bar(tt, [r["drumsOnsets"] for r in rows], width=BAR * 0.9, align="edge", color="#c04fd0")
ax[0].set_ylabel("drum onsets/bar")
ax[1].plot(tt, [r["bassDb"] for r in rows], label="bass", color="#3060ff")
ax[1].plot(tt, [r["synthDb"] for r in rows], label="synth", color="#ff7040")
ax[1].plot(tt, [r["drumsDb"] for r in rows], label="drums", color="#c04fd0")
ax[1].legend(loc="lower right")
ax[1].set_ylabel("dBFS")
ax[2].plot(tt, [r["synthCentroid"] for r in rows], color="#ff7040")
ax[2].set_ylabel("synth centroid Hz")
ax[3].bar(tt, loads, width=BAR * 0.9, align="edge", color="#666")
for w_, c in ((out["typical"], "#2a9d8f"), (out["heaviest"], "#e63946")):
    for x in ax:
        x.axvspan(w_["start"], w_["end"], color=c, alpha=0.15)
    ax[3].text(w_["start"], 0.95, w_["label"], color=c)
ax[3].set_ylabel("load")
ax[3].set_xlabel("seconds")
for x in ax:
    for ev in (8.19, 20.1, 47.85, 111.3, 182.7):
        x.axvline(ev, color="k", lw=0.5, ls=":")
fig.tight_layout()
fig.savefig(os.path.join(a.out, "windows.png"), dpi=80)
print(json.dumps({k: out[k] for k in ("alignment", "typical", "heaviest", "grooveMedianLoad")}, indent=1))
for k in ("drums", "bass", "synth", "mix"):
    print(k, meta[k])

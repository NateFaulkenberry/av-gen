#!/usr/bin/env python3
"""Independent structural analysis of a song, for a music-video director plan.

Written for the Liminal Euclidean World video of "All You Got" (docs/prototypes/liminal-space/SONG-ANALYSIS.md).
It reads the WAV where it lies (the song never enters the repo), parses the tempo markers the DAW wrote into the
file's cue chunk, lays the bar grid, and measures what a director needs bar by bar:

- loudness: EBU R128 momentary (400 ms) and short-term (3 s), integrated loudness and loudness range;
- energy in seven bands, brightness (spectral centroid), noisiness (spectral flatness), stereo width;
- which drum voices are playing: kick on the beat (four-on-the-floor), clap/snare on 2 and 4, hats on the offbeat
  and on the sixteenths, all from band-limited onset envelopes sampled on the grid;
- the harmonic/percussive balance (median-filter HPSS), a centre-panned harmonic presence in the vocal band
  (a proxy for the lead voice, cross-checked against lyric_times.py), the bass note, the chord and the key;
- a bar self-similarity matrix and Foote novelty curves (4- and 8-bar kernels) for section boundaries;
- a check of the tempo map against the kick onsets: a line fitted to the onsets of each tempo segment measures the
  tempo independently of the markers.

It prints a per-bar table and the boundary candidates, writes bars.json, and, when a sections file is given, draws
the review plots with the analyst's section labels.

    tools/liminal/song_analysis.py "~/Desktop/All You Got.wav" \\
        --sections tools/liminal/all-you-got.sections.json \\
        --lyrics /path/to/lyric_times.json \\
        --out ~/Desktop/av-gen-review/24-liminal-space/analysis

Needs numpy, scipy and matplotlib.
"""
import argparse
import json
import os
import re
import struct
import sys

import numpy as np
from scipy import ndimage, signal
from scipy.io import wavfile

# ----------------------------------------------------------------------------------------------------------------
# Reading


def read_markers(path):
    """[(sample, label)] from the WAV's `cue ` chunk and its `LIST/adtl` labels."""
    cues, labels = {}, {}
    with open(path, "rb") as f:
        riff, _, wave = struct.unpack("<4sI4s", f.read(12))
        if riff != b"RIFF" or wave != b"WAVE":
            return []
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                break
            cid, csize = struct.unpack("<4sI", hdr)
            pos = f.tell()
            if cid == b"cue ":
                n = struct.unpack("<I", f.read(4))[0]
                for _ in range(n):
                    ident, _pos, _chunk, _cs, _bs, offset = struct.unpack("<II4sIII", f.read(24))
                    cues[ident] = offset
            elif cid == b"LIST" and f.read(4) == b"adtl":
                end = pos + csize
                while f.tell() < end - 8:
                    sid, ssize = struct.unpack("<4sI", f.read(8))
                    body = f.read(ssize + (ssize & 1))
                    if sid in (b"labl", b"note"):
                        ident = struct.unpack("<I", body[:4])[0]
                        labels[ident] = body[4:ssize].split(b"\0")[0].decode("utf-8", "replace")
            f.seek(pos + csize + (csize & 1))
    return sorted((cues[i], labels.get(i, "")) for i in cues)


def load(path):
    sr, x = wavfile.read(path)
    scale = {np.dtype(np.int32): 2.0 ** 31, np.dtype(np.int16): 2.0 ** 15}.get(x.dtype, 1.0)
    x = x.astype(np.float32) / np.float32(scale)
    if x.ndim == 1:
        x = np.stack([x, x], 1)
    return sr, x[:, 0].copy(), x[:, 1].copy()


def tempo_map(markers, sr):
    tm = []
    for s, lab in markers:
        m = re.search(r"Tempo:\s*([0-9.]+)", lab)
        if m:
            tm.append((s / sr, float(m.group(1))))
    return tm


def beat_grid(tm, duration):
    """Beat times from a piecewise-constant tempo map; the first marker is taken as a downbeat."""
    beats = []
    for i, (t0, bpm) in enumerate(tm):
        t1 = tm[i + 1][0] if i + 1 < len(tm) else duration
        period = 60.0 / bpm
        k = 0
        while t0 + k * period < t1 - 1e-3:
            beats.append(t0 + k * period)
            k += 1
    return np.array(beats)


# ----------------------------------------------------------------------------------------------------------------
# Transforms


def stft(x, sr, n_fft, hop, complex_out=False, chunk=1024):
    """Centred Hann STFT, frames x bins; frame i is centred at i*hop/sr."""
    win = signal.windows.hann(n_fft, sym=False).astype(np.float32)
    xp = np.pad(x, (n_fft // 2, n_fft // 2))
    frames = np.lib.stride_tricks.sliding_window_view(xp, n_fft)[::hop]
    n = frames.shape[0]
    out = np.empty((n, n_fft // 2 + 1), np.complex64 if complex_out else np.float32)
    for i in range(0, n, chunk):
        F = np.fft.rfft(frames[i:i + chunk] * win, axis=1)
        out[i:i + chunk] = F if complex_out else np.abs(F)
    return out, np.arange(n) * hop / sr, np.fft.rfftfreq(n_fft, 1.0 / sr)


def k_weighting(sr):
    """ITU-R BS.1770 K-weighting as two biquads (the general-rate form of the published 48 kHz coefficients)."""
    G, Q1, fc1 = 3.999843853973347, 0.7071752369554196, 1681.974450955533
    K = np.tan(np.pi * fc1 / sr)
    Vh = 10 ** (G / 20)
    Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q1 + K * K
    b1 = np.array([(Vh + Vb * K / Q1 + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q1 + K * K) / a0])
    a1 = np.array([1.0, 2 * (K * K - 1) / a0, (1 - K / Q1 + K * K) / a0])
    Q2, fc2 = 0.5003270373238773, 38.13547087602444
    K = np.tan(np.pi * fc2 / sr)
    a0 = 1 + K / Q2 + K * K
    b2 = np.array([1.0, -2.0, 1.0])
    a2 = np.array([1.0, 2 * (K * K - 1) / a0, (1 - K / Q2 + K * K) / a0])
    return (b1, a1), (b2, a2)


def loudness(L, R, sr):
    (b1, a1), (b2, a2) = k_weighting(sr)
    p = np.zeros(len(L))
    for ch in (L, R):
        z = signal.lfilter(b2, a2, signal.lfilter(b1, a1, ch.astype(np.float64)))
        p += z * z
    c = np.concatenate([[0.0], np.cumsum(p)])

    def blocks(win, hop):
        w, h = int(win * sr), int(hop * sr)
        starts = np.arange(0, len(p) - w + 1, h)
        ms = (c[starts + w] - c[starts]) / w
        return (starts + w / 2) / sr, ms

    tm, msm = blocks(0.4, 0.1)
    ts, mss = blocks(3.0, 0.1)
    lufs = lambda ms: -0.691 + 10 * np.log10(ms + 1e-12)
    # integrated (BS.1770-4): 400 ms blocks with 75 % overlap, absolute gate -70, relative gate -10 LU
    _, mb = blocks(0.4, 0.1)
    g = mb[lufs(mb) > -70]
    g = g[lufs(g) > lufs(g.mean()) - 10]
    integrated = float(lufs(g.mean()))
    # loudness range (EBU Tech 3342): short-term blocks, absolute gate -70, relative gate -20, 10th-95th percentile
    st = lufs(mss)
    st = st[st > -70]
    st = st[st > lufs(10 ** ((st + 0.691) / 10).mean()) - 20]
    lra = float(np.percentile(st, 95) - np.percentile(st, 10))
    return tm, lufs(msm), ts, lufs(mss), integrated, lra


def onset_env(M, freqs, lo, hi, sr_frames):
    """Band-limited log-spectral flux, with its moving median removed and scaled to the 99th percentile."""
    sel = (freqs >= lo) & (freqs < hi)
    LM = np.log1p(1000.0 * M[:, sel])
    flux = np.maximum(0.0, np.diff(LM, axis=0, prepend=LM[:1])).sum(1)
    base = ndimage.median_filter(flux, size=int(0.5 * sr_frames) | 1)
    env = np.maximum(0.0, flux - base)
    return env / (np.percentile(env, 99) + 1e-9)


def hpss_masks(M, kt=17, kf=17):
    H = ndimage.median_filter(M, size=(kt, 1))
    P = ndimage.median_filter(M, size=(1, kf))
    h2, p2 = H * H, P * P
    mh = h2 / (h2 + p2 + 1e-12)
    return mh, 1.0 - mh


def chroma(M, freqs, lo, hi, tuning_cents=0.0):
    sel = (freqs >= lo) & (freqs <= hi)
    midi = 69 + 12 * np.log2(freqs[sel] / 440.0) - tuning_cents / 100.0
    near = np.round(midi)
    w = np.exp(-0.5 * ((midi - near) / 0.3) ** 2)  # favour bins near a semitone centre
    pc = near.astype(int) % 12
    P = (M[:, sel] ** 2) * w
    C = np.zeros((M.shape[0], 12))
    for k in range(12):
        C[:, k] = P[:, pc == k].sum(1)
    return C


def estimate_tuning(M, freqs, lo=200, hi=3000):
    """Energy-weighted deviation of spectral peaks from the equal-tempered A440 grid, in cents."""
    sel = np.where((freqs >= lo) & (freqs <= hi))[0]
    S = M[:, sel].mean(0)
    peaks, _ = signal.find_peaks(S, prominence=S.max() * 0.01)
    f = freqs[sel][peaks]
    dev = (69 + 12 * np.log2(f / 440.0))
    dev = (dev - np.round(dev)) * 100
    wts = S[peaks]
    ang = np.angle(np.sum(wts * np.exp(2j * np.pi * dev / 100)))  # circular mean on the semitone circle
    return float(ang / (2 * np.pi) * 100)


KK_MAJOR = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
KK_MINOR = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
NOTES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def key_estimate(c):
    scores = []
    for k in range(12):
        scores.append((np.corrcoef(c, np.roll(KK_MAJOR, k))[0, 1], f"{NOTES[k]} major"))
        scores.append((np.corrcoef(c, np.roll(KK_MINOR, k))[0, 1], f"{NOTES[k]} minor"))
    scores.sort(reverse=True)
    return scores[:4]


def chord_label(c, bass_pc):
    best = (-9, "N")
    for r in range(12):
        for q, iv in (("", (0, 4, 7)), ("m", (0, 3, 7))):
            t = np.zeros(12)
            t[[(r + i) % 12 for i in iv]] = 1
            s = np.dot(c / (np.linalg.norm(c) + 1e-12), t / np.linalg.norm(t)) + (0.08 if r == bass_pc else 0)
            if s > best[0]:
                best = (s, NOTES[r] + q)
    return best[1]


def foote_novelty(S, half):
    n = S.shape[0]
    g = signal.windows.gaussian(2 * half, std=half / 2)
    kern = np.outer(g, g) * np.kron(np.array([[1, -1], [-1, 1]]), np.ones((half, half)))
    Sp = np.pad(S, half, mode="edge")
    nov = np.array([(Sp[i:i + 2 * half, i:i + 2 * half] * kern).sum() for i in range(n)])
    return nov / (np.abs(nov).max() + 1e-12)


# ----------------------------------------------------------------------------------------------------------------
# Analysis


def analyse(path, lag_search_ms=80):
    sr, L, R = load(path)
    dur = len(L) / sr
    markers = read_markers(path)
    tm = tempo_map(markers, sr) or [(0.0, 120.0)]
    beats = beat_grid(tm, dur)
    mono = 0.5 * (L + R)
    side = 0.5 * (L - R)
    res = {"file": os.path.basename(path), "sr": sr, "duration": dur, "markers": markers, "tempo_map": tm}

    # Loudness
    t_m, lufs_m, t_s, lufs_s, integ, lra = loudness(L, R, sr)
    res.update(integrated_lufs=integ, lra=lra)

    # Spectral features (4096 / 1024)
    M, tA, fA = stft(mono, sr, 4096, 1024)
    S, _, _ = stft(side, sr, 4096, 1024)
    bands = [("sub", 20, 60), ("bass", 60, 150), ("lowmid", 150, 500), ("mid", 500, 2000),
             ("presence", 2000, 6000), ("brill", 6000, 12000), ("air", 12000, 20000)]
    P = M * M
    band_db = {n: 10 * np.log10(P[:, (fA >= lo) & (fA < hi)].sum(1) + 1e-10) for n, lo, hi in bands}
    sel = (fA >= 30) & (fA <= 16000)
    centroid = (M[:, sel] * fA[sel]).sum(1) / (M[:, sel].sum(1) + 1e-9)
    self_ = (fA >= 100) & (fA <= 10000)
    flat = np.exp(np.mean(np.log(P[:, self_] + 1e-12), 1)) / (P[:, self_].mean(1) + 1e-12)
    wsel = (fA >= 200) & (fA <= 8000)
    width = 10 * np.log10((S[:, wsel] ** 2).sum(1) + 1e-10) - 10 * np.log10(P[:, wsel].sum(1) + 1e-10)
    del S

    # HPSS, centre-harmonic vocal-band presence, chroma (4096 / 2048, up to 8.2 kHz)
    XL, tH, fH = stft(L, sr, 4096, 2048, complex_out=True)
    XR, _, _ = stft(R, sr, 4096, 2048, complex_out=True)
    top = int(np.searchsorted(fH, 8200))
    XL, XR, fH = XL[:, :top], XR[:, :top], fH[:top]
    MH = np.abs(XL + XR) * 0.5
    mh, mp = hpss_masks(MH)
    psi = 2 * np.real(XL * np.conj(XR)) / (np.abs(XL) ** 2 + np.abs(XR) ** 2 + 1e-12)
    psi = ndimage.uniform_filter1d(psi, 3, axis=0)
    centre = np.clip(psi, 0, 1) ** 6
    del XL, XR, psi
    vsel = (fH >= 250) & (fH <= 4000)
    E_v = (MH[:, vsel] ** 2).sum(1) + 1e-10
    voc_db = 10 * np.log10(((mh * centre)[:, vsel] * MH[:, vsel] ** 2).sum(1) + 1e-10)
    voc_ratio = ((mh * centre)[:, vsel] * MH[:, vsel] ** 2).sum(1) / E_v
    harm_ratio = ((mh * MH ** 2).sum(1) + 1e-10) / ((MH ** 2).sum(1) + 1e-10)
    tuning = estimate_tuning(MH, fH)
    res["tuning_cents"] = tuning
    Ch = chroma(MH * mh, fH, 130, 5000, tuning)
    # bass chroma needs a long window
    MB, tB, fB = stft(mono, sr, 16384, 2048)
    Cb = chroma(MB, fB, 30, 260, tuning)
    del MB

    # Onsets (2048 / 512)
    MO, tO, fO = stft(mono, sr, 2048, 512)
    fps = sr / 512
    env = {"kick": onset_env(MO, fO, 35, 130, fps), "clap": onset_env(MO, fO, 1000, 5000, fps),
           "hat": onset_env(MO, fO, 7000, 16000, fps), "full": onset_env(MO, fO, 30, 16000, fps)}
    del MO

    # Detector latency: the lag that best lines the kick envelope up with the grid
    lags = np.arange(-lag_search_ms, lag_search_ms + 1, 2) / 1000.0

    def at(e, times, lag=0.0, reach=1):
        idx = np.round((np.asarray(times) + lag) * fps).astype(int)
        out = np.zeros(len(idx))
        for j, i in enumerate(idx):
            a, b = max(0, i - reach), min(len(e), i + reach + 1)
            out[j] = e[a:b].max() if b > a else 0.0
        return out

    lag_score = [at(env["kick"], beats, l, 0).mean() for l in lags]
    lag = float(lags[int(np.argmax(lag_score))])
    res["detector_lag_ms"] = lag * 1000

    # Tempo check: kick onset peaks against each tempo segment's grid
    pk, _ = signal.find_peaks(env["kick"], height=0.25, distance=int(0.3 * fps))
    pk_t = pk / fps - lag
    tempo_fit = []
    for i, (t0, bpm) in enumerate(tm):
        t1 = tm[i + 1][0] if i + 1 < len(tm) else dur
        seg_beats = beats[(beats >= t0 - 1e-6) & (beats < t1 - 1e-6)]
        m = (pk_t >= t0 - 0.05) & (pk_t < t1 - 0.05)
        pts = pk_t[m]
        if len(pts) < 8:
            continue
        period = 60.0 / bpm
        k = np.round((pts - t0) / period)
        resid = pts - (t0 + k * period)
        ok = np.abs(resid) < 0.06
        A = np.stack([k[ok], np.ones(ok.sum())], 1)
        (slope, icpt), *_ = np.linalg.lstsq(A, pts[ok], rcond=None)
        tempo_fit.append({"segment": i, "marker_bpm": bpm, "t0": t0, "t1": t1, "n_onsets": int(ok.sum()),
                          "fitted_bpm": 60.0 / slope, "offset_ms": (icpt - t0) * 1000,
                          "resid_ms_rms": float(np.sqrt(np.mean((resid[ok]) ** 2)) * 1000)})
    res["tempo_fit"] = tempo_fit
    # the counterfactual: the second segment's onsets against the first tempo, extended
    if len(tm) > 1:
        period0 = 60.0 / tm[0][1]
        k = np.round(pk_t / period0)
        res["drift_vs_first_tempo"] = {"t": pk_t.tolist(), "resid": (pk_t - k * period0).tolist()}

    # Local tempo from the onset autocorrelation, 12 s windows (a coarse independent view)
    loc = []
    e = env["full"]
    W = int(12 * fps)
    for c0 in range(0, len(e) - W, int(2 * fps)):
        seg = e[c0:c0 + W] - e[c0:c0 + W].mean()
        ac = np.correlate(seg, seg, "full")[W - 1:]
        lo_, hi_ = int(fps * 60 / 140), int(fps * 60 / 90)
        j = lo_ + int(np.argmax(ac[lo_:hi_]))
        if 1 <= j < len(ac) - 1:  # parabolic refinement
            a_, b_, c_ = ac[j - 1], ac[j], ac[j + 1]
            j = j + 0.5 * (a_ - c_) / (a_ - 2 * b_ + c_ + 1e-12)
        loc.append(((c0 + W / 2) / fps, 60.0 * fps / j, float(ac[int(round(j))] / (ac[0] + 1e-9))))
    res["local_tempo"] = loc

    # Per-bar aggregation
    bar_starts = beats[::4]
    bar_ends = np.append(bar_starts[1:], dur)

    def mean_in(t, v, a, b):
        m = (t >= a) & (t < b)
        return float(np.mean(v[m])) if m.any() else float("nan")

    def pw_mean_db(t, v_db, a, b):
        m = (t >= a) & (t < b)
        return float(10 * np.log10(np.mean(10 ** (v_db[m] / 10)))) if m.any() else float("nan")

    bars = []
    for i, (a, b) in enumerate(zip(bar_starts, bar_ends)):
        bl = (b - a) / 4 if i < len(bar_starts) - 1 else 60.0 / (tm[-1][1])
        q = a + bl * np.arange(4)
        sixteenths = a + bl / 4 * np.arange(16)
        offs16 = np.delete(sixteenths, [0, 4, 8, 12])
        eighth_off = a + bl * (np.arange(4) + 0.5)
        k_on = at(env["kick"], q, lag).mean()
        k_off = at(env["kick"], offs16, lag).mean()
        cl = at(env["clap"], q, lag)
        h_off = at(env["hat"], eighth_off, lag).mean()
        h16 = at(env["hat"], sixteenths, lag).mean()
        mA = (tA >= a) & (tA < b)
        mH_ = (tH >= a) & (tH < b)
        mB_ = (tB >= a) & (tB < b)
        ch = Ch[mH_].sum(0)
        cb = Cb[mB_].sum(0)
        bass_pc = int(np.argmax(cb))
        row = {
            "bar": i + 1, "t0": round(float(a), 3), "t1": round(float(b), 3),
            "lufs_m": pw_mean_db(t_m, lufs_m, a, b), "lufs_s": float(np.interp((a + b) / 2, t_s, lufs_s)),
            **{n: pw_mean_db(tA, band_db[n], a, b) for n, _, _ in bands},
            "centroid": mean_in(tA, centroid, a, b), "flatness": mean_in(tA, flat, a, b),
            "width_db": pw_mean_db(tA, width, a, b),
            "kick_on": float(k_on), "kick_off": float(k_off), "four_floor": float(k_on - k_off),
            "backbeat": float(cl[[1, 3]].mean() - cl[[0, 2]].mean()), "clap": float(cl.mean()),
            "hat_off": float(h_off), "hat16": float(h16),
            "harm_ratio": float(np.mean(harm_ratio[mH_])), "voc_ratio": float(np.mean(voc_ratio[mH_])),
            "voc_db": pw_mean_db(tH, voc_db, a, b),
            "bass_note": NOTES[bass_pc], "chord": chord_label(ch, bass_pc),
            "chroma": (ch / (ch.sum() + 1e-12)).round(4).tolist(),
        }
        bars.append(row)
    res["bars"] = bars
    res["key"] = key_estimate(Ch.sum(0) + 0.5 * Cb.sum(0) / (Cb.sum() + 1e-12) * Ch.sum())

    # Bar self-similarity and novelty
    feats = ["lufs_m", "sub", "bass", "lowmid", "mid", "presence", "brill", "air", "centroid", "flatness",
             "width_db", "four_floor", "backbeat", "hat_off", "hat16", "harm_ratio", "voc_ratio"]
    F = np.array([[r[f] for f in feats] for r in bars], float)
    F = (F - np.nanmean(F, 0)) / (np.nanstd(F, 0) + 1e-9)
    Cm = np.array([r["chroma"] for r in bars])
    Cm = (Cm - Cm.mean(0)) / (Cm.std(0) + 1e-9)
    X = np.concatenate([F, 0.5 * Cm], 1)
    Xn = X / (np.linalg.norm(X, axis=1, keepdims=True) + 1e-9)
    SSM = Xn @ Xn.T
    res["ssm"] = SSM.round(4).tolist()
    res["novelty4"] = foote_novelty(SSM, 4).round(4).tolist()
    res["novelty8"] = foote_novelty(SSM, 8).round(4).tolist()
    nov = np.array(res["novelty4"])
    cand, _ = signal.find_peaks(nov, height=np.mean(nov) + 0.5 * np.std(nov), distance=3)
    res["boundary_candidates_bar"] = [int(c) + 1 for c in cand]  # the bar a new section starts on

    # frame series, decimated, for plotting
    res["_series"] = {"t_m": t_m, "lufs_m": lufs_m, "t_s": t_s, "lufs_s": lufs_s, "tA": tA, "band_db": band_db,
                      "centroid": centroid, "width": width, "tH": tH, "voc_ratio": voc_ratio,
                      "harm_ratio": harm_ratio, "env": env, "fps": fps, "M": M, "fA": fA, "beats": beats,
                      "Ch": Ch, "Cb": Cb}
    return res


# ----------------------------------------------------------------------------------------------------------------
# Plots (the dataviz reference palette on a light surface; one y-scale per panel; section marks shared)

SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]


def plots(res, sections, lyrics, out):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import LinearSegmentedColormap

    plt.rcParams.update({"figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "axes.edgecolor": INK2,
                         "axes.labelcolor": INK2, "xtick.color": INK2, "ytick.color": INK2, "text.color": INK,
                         "font.size": 9, "axes.titlesize": 10, "axes.titleweight": "bold", "axes.grid": True,
                         "grid.color": GRID, "grid.linewidth": 0.6, "axes.spines.top": False,
                         "axes.spines.right": False, "lines.linewidth": 1.4})
    s = res["_series"]
    dur = res["duration"]
    bars = res["bars"]
    secs = sections["sections"]

    def mark(ax, labels=False, shade=True):
        for j, sec in enumerate(secs):
            a, b = sec["t0"], sec["t1"]
            if shade and j % 2 == 1:
                ax.axvspan(a, b, color="#f0efeb", zorder=0, lw=0)
            ax.axvline(a, color=INK2, lw=0.6, alpha=0.6, zorder=1)
            if labels:
                ax.text((a + b) / 2, 1.02, sec["id"], transform=ax.get_xaxis_transform(), ha="center",
                        va="bottom", fontsize=8, color=INK, fontweight="bold")
        ax.set_xlim(0, dur)

    def bar_axis(ax):
        sec2 = ax.secondary_xaxis("top", functions=(lambda x: x, lambda x: x))
        ticks = [b["t0"] for b in bars if (b["bar"] - 1) % 8 == 0]
        sec2.set_xticks(ticks)
        sec2.set_xticklabels([str(b["bar"]) for b in bars if (b["bar"] - 1) % 8 == 0], fontsize=7)
        sec2.tick_params(colors=INK2, length=2, pad=1)
        return sec2

    legend_kw = dict(frameon=False, fontsize=8, loc="upper left", ncol=4)

    # 01 energy: loudness, bands, drums, vocal band, all on the shared time axis
    fig, ax = plt.subplots(5, 1, figsize=(16, 13), sharex=True,
                           gridspec_kw={"height_ratios": [1.3, 1.3, 1, 1, 0.9]})
    fig.suptitle("All You Got: energy and arrangement (bars on the top axis; sections are the analyst's)",
                 x=0.01, ha="left", fontsize=12, fontweight="bold")
    a0 = ax[0]
    a0.plot(s["t_m"], s["lufs_m"], color="#b9b8b2", lw=0.6, label="momentary (400 ms)")
    a0.plot(s["t_s"], s["lufs_s"], color=SERIES[0], lw=1.8, label="short-term (3 s)")
    a0.set_ylabel("loudness (LUFS)")
    a0.set_ylim(-40, max(-2, np.max(s["lufs_m"]) + 2))
    a0.legend(**legend_kw)
    mark(a0, labels=True)
    bar_axis(a0)
    a1 = ax[1]
    sm = lambda v: ndimage.uniform_filter1d(v, 45)  # ~1 s
    for j, (nm, lab) in enumerate([("sub", "sub 20-60 Hz"), ("bass", "bass 60-150"), ("mid", "mid 0.5-2 kHz"),
                                   ("presence", "presence 2-6 kHz"), ("brill", "brilliance 6-12 kHz")]):
        a1.plot(s["tA"], sm(s["band_db"][nm]), color=SERIES[j], lw=1.2, label=lab)
    a1.set_ylabel("band energy (dB, 1 s)")
    a1.legend(**legend_kw)
    mark(a1)
    a2 = ax[2]
    bt = np.array([b["t0"] for b in bars]) + np.array([b["t1"] - b["t0"] for b in bars]) / 2
    bw = np.array([b["t1"] - b["t0"] for b in bars]) * 0.42
    a2.bar(bt - bw / 2, [b["four_floor"] for b in bars], width=bw, color=SERIES[0], label="kick on the beat")
    a2.bar(bt + bw / 2, [b["hat16"] for b in bars], width=bw, color=SERIES[2], label="hats (sixteenths)")
    a2.set_ylabel("drum presence (onset)")
    a2.legend(**legend_kw)
    mark(a2)
    a3 = ax[3]
    a3.plot(s["tH"], ndimage.uniform_filter1d(s["voc_ratio"], 12), color=SERIES[4], lw=1.2,
            label="centre-panned harmonic share, 250 Hz-4 kHz")
    if lyrics:
        for seg in lyrics:
            a3.axvspan(seg["t0"], seg["t1"], ymin=0.9, ymax=1.0, color=SERIES[6], alpha=0.8, lw=0)
        a3.plot([], [], color=SERIES[6], lw=6, label="recognised sung phrases")
    a3.set_ylabel("vocal band")
    a3.legend(**legend_kw)
    mark(a3)
    a4 = ax[4]
    a4.plot(s["tA"], ndimage.uniform_filter1d(s["width"], 45), color=SERIES[6], lw=1.2,
            label="stereo width: side minus mid, 0.2-8 kHz")
    a4.set_ylabel("width (dB)")
    a4.set_xlabel("time (s)")
    a4.legend(**legend_kw)
    mark(a4)
    fig.tight_layout(rect=(0, 0, 1, 0.98))
    fig.savefig(os.path.join(out, "01-energy.png"), dpi=130)
    plt.close(fig)

    # 02 spectrogram, log frequency
    M, fA, tA = s["M"], s["fA"], s["tA"]
    edges = np.geomspace(30, 16000, 241)
    idx = np.searchsorted(fA, edges)
    D = np.stack([(M[:, idx[i]:max(idx[i + 1], idx[i] + 1)] ** 2).mean(1) for i in range(len(edges) - 1)], 1)
    D = 10 * np.log10(D + 1e-10)
    D = ndimage.uniform_filter1d(D, 4, axis=0)
    fig, axs = plt.subplots(1, 1, figsize=(16, 6.5))
    cmap = LinearSegmentedColormap.from_list("ink", ["#fcfcfb", "#9ec3ee", "#2a78d6", "#12305a", "#060d1a"])
    vmax = np.percentile(D, 99.7)
    axs.pcolormesh(tA[::2], edges, D[::2].T, cmap=cmap, vmin=vmax - 70, vmax=vmax, shading="auto",
                   rasterized=True)
    axs.set_yscale("log")
    axs.set_ylim(30, 16000)
    axs.set_yticks([50, 100, 200, 500, 1000, 2000, 5000, 10000])
    axs.set_yticklabels(["50", "100", "200", "500", "1k", "2k", "5k", "10k"])
    axs.set_ylabel("frequency (Hz)")
    axs.set_xlabel("time (s)")
    axs.grid(False)
    for sec in secs:
        axs.axvline(sec["t0"], color=SERIES[1], lw=1.0, alpha=0.9)
        axs.text(sec["t0"] + 0.6, 13500, sec["id"], color=INK, fontsize=8, fontweight="bold", va="top",
                 bbox=dict(facecolor=SURFACE, edgecolor="none", alpha=0.8, pad=1.5))
    bar_axis(axs)
    axs.set_title("All You Got: log-frequency spectrogram with the analyst's section boundaries", loc="left")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "02-spectrogram.png"), dpi=130)
    plt.close(fig)

    # 03 structure: bar self-similarity and novelty
    SSM = np.array(res["ssm"])
    n = len(bars)
    fig = plt.figure(figsize=(12, 13.5))
    gs = fig.add_gridspec(2, 1, height_ratios=[4, 1])
    axm = fig.add_subplot(gs[0])
    cm2 = LinearSegmentedColormap.from_list("div", ["#1f5fae", "#f4f3ef", "#c4461c"])
    axm.imshow(SSM, cmap=cm2, vmin=-1, vmax=1, origin="upper", extent=(0.5, n + 0.5, n + 0.5, 0.5))
    for sec in secs:
        axm.axhline(sec["bar0"] - 0.5, color=INK, lw=0.6)
        axm.axvline(sec["bar0"] - 0.5, color=INK, lw=0.6)
        axm.text(sec["bar0"] + 0.3, 0.2, sec["id"], fontsize=8, va="bottom", ha="left", color=INK,
                 fontweight="bold", rotation=0)
    axm.set_xlabel("bar")
    axm.set_ylabel("bar")
    axm.grid(False)
    axm.set_title("Bar self-similarity (arrangement features + chroma); red = alike, blue = unlike", loc="left",
                  pad=16)
    axn = fig.add_subplot(gs[1])
    xb = np.arange(1, n + 1)
    axn.plot(xb, res["novelty4"], color=SERIES[0], label="novelty, 4-bar kernel")
    axn.plot(xb, res["novelty8"], color=SERIES[1], label="novelty, 8-bar kernel")
    for c in res["boundary_candidates_bar"]:
        axn.axvline(c, color=INK2, lw=0.5, ls=":")
    for sec in secs:
        axn.axvline(sec["bar0"], color=INK, lw=0.8)
    axn.set_xlim(0.5, n + 0.5)
    axn.set_xlabel("bar (dotted: novelty peaks; solid: the analyst's boundaries)")
    axn.legend(frameon=False, fontsize=8, loc="upper right")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "03-structure.png"), dpi=120)
    plt.close(fig)

    # 04 arrangement heat map: z-scored bar features
    feats = [("lufs_m", "loudness"), ("sub", "sub"), ("bass", "bass"), ("lowmid", "low-mid"), ("mid", "mid"),
             ("presence", "presence"), ("brill", "brilliance"), ("air", "air"), ("centroid", "brightness"),
             ("flatness", "noisiness"), ("width_db", "width"), ("four_floor", "kick on beat"),
             ("backbeat", "clap 2 & 4"), ("hat_off", "offbeat hat"), ("hat16", "16th hats"),
             ("harm_ratio", "harmonic share"), ("voc_ratio", "centre vocal band")]
    Z = np.array([[b[f] for b in bars] for f, _ in feats], float)
    Z = (Z - np.nanmean(Z, 1, keepdims=True)) / (np.nanstd(Z, 1, keepdims=True) + 1e-9)
    fig, axh = plt.subplots(figsize=(16, 6))
    axh.imshow(np.clip(Z, -2.5, 2.5), aspect="auto", cmap=cm2, vmin=-2.5, vmax=2.5,
               extent=(0.5, n + 0.5, len(feats) - 0.5, -0.5), interpolation="nearest")
    axh.set_yticks(range(len(feats)))
    axh.set_yticklabels([lab for _, lab in feats])
    axh.set_xlabel("bar")
    axh.grid(False)
    for sec in secs:
        axh.axvline(sec["bar0"] - 0.5, color=INK, lw=0.9)
        axh.text(sec["bar0"] - 0.3, -0.7, sec["id"], fontsize=8, fontweight="bold", va="bottom", ha="left")
    axh.set_title("Arrangement by bar (z-scores; orange = more than usual, blue = less)", loc="left", pad=16)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "04-arrangement.png"), dpi=130)
    plt.close(fig)

    # 05 tempo: onset residuals against the first tempo extended, and the fitted tempo per segment
    if "drift_vs_first_tempo" in res:
        d = res["drift_vs_first_tempo"]
        fig, axt = plt.subplots(2, 1, figsize=(14, 7), sharex=True)
        axt[0].scatter(d["t"], np.array(d["resid"]) * 1000, s=5, color=SERIES[0])
        axt[0].set_ylabel("kick onset minus\n109 BPM grid (ms)")
        for t0, bpm in res["tempo_map"]:
            axt[0].axvline(t0, color=SERIES[1], lw=1)
            axt[0].text(t0 + 1, 240, f"marker: {bpm:g} BPM", color=INK, fontsize=8, va="top")
        axt[0].set_ylim(-280, 280)
        axt[0].set_title("Tempo map check: kick onsets against the 109 BPM grid carried through the whole song "
                         "(the saw-tooth after 165 s is the faster tempo)", loc="left")
        lt = np.array(res["local_tempo"])
        axt[1].plot(lt[:, 0], lt[:, 1], color=SERIES[2], marker="o", ms=2.5, lw=1)
        axt[1].set_ylim(100, 120)
        axt[1].set_ylabel("local tempo, 12 s\nautocorrelation (BPM)")
        axt[1].set_xlabel("time (s)")
        for ax_ in axt:
            ax_.set_xlim(0, dur)
            for sec in secs:
                ax_.axvline(sec["t0"], color=INK2, lw=0.5, alpha=0.5)
        fig.tight_layout()
        fig.savefig(os.path.join(out, "05-tempo.png"), dpi=130)
        plt.close(fig)


# ----------------------------------------------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav")
    ap.add_argument("--out", required=True)
    ap.add_argument("--sections", help="the analyst's section map (JSON): bars and ids")
    ap.add_argument("--lyrics", help="lyric_times.py output, to mark the recognised sung phrases")
    args = ap.parse_args()
    wav = os.path.expanduser(args.wav)
    out = os.path.expanduser(args.out)
    os.makedirs(out, exist_ok=True)

    res = analyse(wav)
    bars = res["bars"]
    print(f"{res['file']}: {res['duration']:.3f} s, {res['sr']} Hz, {len(bars)} bars")
    print("markers:", res["markers"])
    print("tempo map:", res["tempo_map"])
    print(f"integrated {res['integrated_lufs']:.1f} LUFS, LRA {res['lra']:.1f} LU, tuning {res['tuning_cents']:+.1f}"
          f" cents, detector lag {res['detector_lag_ms']:.0f} ms")
    for tf in res["tempo_fit"]:
        print("tempo fit:", {k: (round(float(v), 3) if isinstance(v, (float, np.floating)) else v)
                             for k, v in tf.items()})
    print("key candidates:", [(round(float(c), 3), k) for c, k in res["key"]])
    print("novelty boundary candidates (bar a section starts on):", res["boundary_candidates_bar"])
    hdr = ("bar", "t0", "lufs_m", "sub", "bass", "mid", "presence", "brill", "centroid", "width_db", "four_floor",
           "backbeat", "hat_off", "hat16", "harm_ratio", "voc_ratio", "bass_note", "chord")
    print(" ".join(f"{h:>8}" for h in hdr))
    for b in bars:
        vals = []
        for h in hdr:
            v = b[h]
            vals.append(f"{v:8.2f}" if isinstance(v, float) else f"{v:>8}")
        print(" ".join(vals))

    series = res.pop("_series")
    with open(os.path.join(out, "bars.json"), "w") as f:
        json.dump({k: v for k, v in res.items() if k not in ("ssm",)}, f, indent=1,
                  default=lambda o: o.item() if hasattr(o, "item") else o.tolist())
    res["_series"] = series

    if args.sections:
        with open(args.sections) as f:
            sections = json.load(f)
        bt = {b["bar"]: b for b in bars}
        for sec in sections["sections"]:
            sec["t0"] = bt[sec["bar0"]]["t0"]
            sec["t1"] = bt[sec["bar1"]]["t1"]
        lyr = None
        if args.lyrics:
            with open(os.path.expanduser(args.lyrics)) as f:
                lyr = [s_ for s_ in json.load(f)["segments"] if s_["no_speech"] < 0.6]
        plots(res, sections, lyr, out)
        print("plots ->", out)


if __name__ == "__main__":
    main()

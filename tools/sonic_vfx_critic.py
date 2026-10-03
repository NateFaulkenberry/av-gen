#!/usr/bin/env python3
"""The Sonic VFX evaluator (brief §16; research/05-evaluation.md): extends the Creative Critic and liminal_critic.

    measure  --video clip.mp4 --trace trace.csv [--project p.json] [--video-start s] --out r.json [--md r.md]
             numeric findings in the Critic's shape (key, rule, dimension, severity, evidence) over the dimensions
             the Critic does not cover for Sonic scenes:
               composition   saliency peaks after NMS, dominance ratio, balance, symmetry, negative space, thirds
               colour        OKLab palette, hue spread, colourfulness, clipping, muddiness, value structure, and the
                             palette against the scene's declared `sonicScene.palette`
               motion        a speed per cell of an 8x6 grid, its tiers (p90/p10, Gini): "is everything moving alike"
               audio-visual  for every response.*/notes.*/visual.* signal: lagged correlation per cell (a synchrony
                             map), event-locked latency / gain / decay / reliability, the modulation-spectrum ratio
                             (slow sound -> slow picture?), specificity (do kick and hat move different places?), and
                             every signal the scene's `sonicScene.vocabulary` names that nothing on screen answers
               defects       empty frame, blown highlights, flash risk (WCAG 2.3.1-style), flicker share
    inputs   --video clip.mp4 --trace trace.csv [--project p.json] --out inputs.json
             a Creative Critic inputs file: the trace's response signals as the Critic's five audio features, kick
             and note-on times as events, the scene's thesis and palette as intent, routes as a modulation LIST
    compare  a.json b.json        what moved between two measure reports, by stable key
    trace    --project p.json --out trace.csv     the engine's per-frame signals (avgen --sonic-trace, no GPU)
    selftest                      synthetic clips with known answers

The trace is the ground truth: correspondence is measured against the exact signals the engine computed, not against
a re-analysis of the audio, because the question is whether the MAPPING works. Event columns in a trace are 0 (rows
are written after the frame's events clear), so hits are read from their `...Env` envelopes.

System python 3.9: numpy, PIL, ffmpeg. No new dependency.
"""
import argparse
import csv
import json
import math
import os
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AVGEN = os.path.join(ROOT, "build", "release", "src", "avgen")
W, H = 384, 216          # analysis resolution (small responders: a glint, a meteor streak)
GW, GH = 8, 6            # region grid
LAGS_MS = list(range(-100, 401, 33))
SEVERITY = {"high": 3, "medium": 2, "low": 1, "info": 0}
SLOW_SIGNALS = ("response.sustain", "response.intensity", "notes.held", "sonic.energy.slow")

# ---------------------------------------------------------------------------------------------------- io


def decode(video, fps=None, start=0.0):
    """RGB frames at W x H as float32 0..1, and the stream's fps."""
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                            "stream=r_frame_rate", "-of", "csv=p=0", video], capture_output=True, text=True).stdout
    num, den = (probe.strip().split("/") + ["1"])[:2]
    src = float(num) / float(den or 1) if probe.strip() else 30.0
    fps = fps or src
    cmd = ["ffmpeg", "-v", "error", "-i", video, "-vf", "fps=%g,scale=%d:%d:flags=area" % (fps, W, H),
           "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    frames = np.frombuffer(raw, np.uint8).reshape(-1, H, W, 3).astype(np.float32) / 255.0
    return frames, fps


def read_trace(path):
    with open(path) as f:
        r = csv.reader(f)
        header = next(r)
        rows = [[float(x) for x in row] for row in r if row]
    a = np.array(rows, np.float64)
    return {name: a[:, i] for i, name in enumerate(header)}


def load_project(path):
    if not path:
        return {}, {}
    doc = json.load(open(path))
    scene = {}
    ref = (doc.get("assets") or {}).get("scene")
    if isinstance(ref, dict) and ref.get("path"):
        p = os.path.join(os.path.dirname(path), ref["path"])
        if os.path.exists(p):
            scene = json.load(open(p))
    elif isinstance(ref, str):
        p = os.path.join(os.path.dirname(path), ref)
        if os.path.exists(p):
            scene = json.load(open(p))
    return doc, scene

# ---------------------------------------------------------------------------------------------------- colour


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def oklab(rgb):
    c = srgb_to_linear(np.clip(rgb, 0, 1))
    l = 0.4122214708 * c[..., 0] + 0.5363325363 * c[..., 1] + 0.0514459929 * c[..., 2]
    m = 0.2119034982 * c[..., 0] + 0.6806995451 * c[..., 1] + 0.1073969566 * c[..., 2]
    s = 0.0883024619 * c[..., 0] + 0.2817188376 * c[..., 1] + 0.6299787005 * c[..., 2]
    l, m, s = np.cbrt(l), np.cbrt(m), np.cbrt(s)
    return np.stack([0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                     1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                     0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s], -1)


def hex_to_rgb(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)])


def kmeans(x, k, iters=12):
    """Deterministic k-means: initial centres at evenly spaced quantiles of lightness."""
    order = np.argsort(x[:, 0])
    centres = x[order[np.linspace(0, len(x) - 1, k).astype(int)]].copy()
    for _ in range(iters):
        d = ((x[:, None, :] - centres[None]) ** 2).sum(-1)
        lab = d.argmin(1)
        for j in range(k):
            if (lab == j).any():
                centres[j] = x[lab == j].mean(0)
    share = np.bincount(lab, minlength=k) / len(x)
    return centres, share


def luma(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]

# ---------------------------------------------------------------------------------------------------- composition


def box(a, r):
    if r <= 0:
        return a
    k = 2 * r + 1
    p = np.pad(a, r, mode="edge")
    c = p.cumsum(0).cumsum(1)
    c = np.pad(c, ((1, 0), (1, 0)))
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


def saliency(gray):
    """Spectral residual (Hou & Zhang 2007) at 64 px wide, upsampled, normalised to sum 1."""
    h, w = gray.shape
    small = gray[:: max(1, h // 36), :: max(1, w // 64)]
    f = np.fft.fft2(small)
    logamp = np.log(np.abs(f) + 1e-8)
    resid = logamp - box(logamp, 1)
    s = np.abs(np.fft.ifft2(np.exp(resid + 1j * np.angle(f)))) ** 2
    s = box(box(s, 2), 2)
    s = np.kron(s, np.ones((math.ceil(h / s.shape[0]), math.ceil(w / s.shape[1]))))[:h, :w]
    return s / max(s.sum(), 1e-12)


def peaks(s, frac=0.5, radius=None):
    radius = radius or max(2, s.shape[1] // 10)
    out = []
    t = s.copy()
    top = t.max()
    while True:
        i = int(t.argmax())
        y, x = divmod(i, t.shape[1])
        if t[y, x] < frac * top or len(out) >= 8:
            break
        out.append((x / t.shape[1], y / t.shape[0], float(t[y, x] / top)))
        t[max(0, y - radius):y + radius + 1, max(0, x - radius):x + radius + 1] = 0
    return out


def components(mask):
    """Connected components (4-neighbour) of a small boolean grid: sizes."""
    h, w = mask.shape
    seen = np.zeros_like(mask, bool)
    sizes = []
    for y0 in range(h):
        for x0 in range(w):
            if mask[y0, x0] and not seen[y0, x0]:
                stack = [(y0, x0)]
                seen[y0, x0] = True
                n = 0
                while stack:
                    y, x = stack.pop()
                    n += 1
                    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        yy, xx = y + dy, x + dx
                        if 0 <= yy < h and 0 <= xx < w and mask[yy, xx] and not seen[yy, xx]:
                            seen[yy, xx] = True
                            stack.append((yy, xx))
                sizes.append(n)
    return sizes


def otsu(v):
    hist, edges = np.histogram(v, 64, (0, 1))
    p = hist / max(hist.sum(), 1)
    mids = (edges[:-1] + edges[1:]) / 2
    best, thr = -1, 0.5
    for i in range(1, 63):
        w0, w1 = p[:i].sum(), p[i:].sum()
        if w0 == 0 or w1 == 0:
            continue
        m0, m1 = (p[:i] * mids[:i]).sum() / w0, (p[i:] * mids[i:]).sum() / w1
        b = w0 * w1 * (m0 - m1) ** 2
        if b > best:
            best, thr = b, mids[i]
    return thr


def gini(v):
    v = np.sort(np.abs(np.asarray(v, float)))
    n = len(v)
    if n == 0 or v.sum() == 0:
        return 0.0
    return float((2 * np.arange(1, n + 1) - n - 1).dot(v) / (n * v.sum()))

# ---------------------------------------------------------------------------------------------------- findings


class Findings:
    def __init__(self):
        self.items = []

    def add(self, rule, dimension, severity, title, message, evidence=None, kind="issue", region=None):
        key = rule if region is None else "%s:%s" % (rule, region)
        self.items.append({"key": key, "rule": rule, "dimension": dimension, "severity": severity, "kind": kind,
                           "title": title, "message": message, "evidence": evidence or {}})

# ---------------------------------------------------------------------------------------------------- measures


def measure_frames(frames, F, meta):
    """Composition, colour and defects over a few representative frames."""
    n = len(frames)
    picks = sorted(set(np.linspace(0, n - 1, min(8, n)).astype(int)))
    comp = []
    for i in picks:
        rgb = frames[i]
        g = luma(rgb)
        s = saliency(g)
        pk = peaks(s)
        yy, xx = np.mgrid[0:H, 0:W]
        cx, cy = float((s * xx).sum() / W), float((s * yy).sum() / H)
        flat = np.sort(s.ravel())[::-1]
        top_mass = float(flat[: int(0.05 * flat.size)].sum())
        small = box(g, 3)[::4, ::4]
        sym = float(np.corrcoef(small.ravel(), small[:, ::-1].ravel())[0, 1]) if small.std() > 1e-6 else 1.0
        gy, gx = np.gradient(g)
        grad = np.hypot(gx, gy)
        # Quiet: flat (local contrast under 1.5%) and dark or even -- the frame's breathing room.
        local = np.sqrt(np.maximum(box(g * g, 4) - box(g, 4) ** 2, 0.0))
        neg = float(((local < 0.015) & (grad < max(np.percentile(grad, 50), 1e-4))).mean())
        thirds = [(1 / 3, 1 / 3), (2 / 3, 1 / 3), (1 / 3, 2 / 3), (2 / 3, 2 / 3)]
        d = min(math.hypot(pk[0][0] - a, pk[0][1] - b) for a, b in thirds) if pk else 1.0
        comp.append({"t": i / F, "peaks": len(pk), "dominance": (pk[0][2] / pk[1][2]) if len(pk) > 1 else 9.0,
                     "focal": pk[0][:2] if pk else None, "balance": math.hypot(cx - 0.5, cy - 0.5) * 2,
                     "symmetry": sym, "negative": neg, "thirds": d, "top5": top_mass, "std": float(g.std()),
                     "mean": float(g.mean())})
    allpix = np.concatenate([frames[i].reshape(-1, 3) for i in picks])[::7]
    lab = oklab(allpix)
    centres, share = kmeans(lab, 6)
    chroma = np.hypot(lab[:, 1], lab[:, 2])
    hue = np.arctan2(lab[:, 2], lab[:, 1])
    wsum = chroma.sum()
    R = abs((chroma * np.exp(1j * hue)).sum()) / max(wsum, 1e-9)
    hue_spread = float(math.sqrt(max(0.0, -2 * math.log(max(R, 1e-9))))) if wsum > 1e-6 else 0.0
    rgb255 = allpix * 255
    rg = rgb255[:, 0] - rgb255[:, 1]
    yb = 0.5 * (rgb255[:, 0] + rgb255[:, 1]) - rgb255[:, 2]
    colourfulness = float(math.hypot(rg.std(), yb.std()) + 0.3 * math.hypot(rg.mean(), yb.mean()))
    clip = float((allpix.max(1) >= 254 / 255).mean())
    mud = float(((chroma < 0.04) & (lab[:, 0] > 0.3) & (lab[:, 0] < 0.7)).mean())
    lum = luma(allpix)
    value_range = float(np.percentile(lum, 95) - np.percentile(lum, 5))
    # value structure (notan) on the middle frame
    mid = luma(frames[picks[len(picks) // 2]])
    thr = otsu(mid)
    m = box(mid, 1)[::3, ::3] > thr
    big = [c for c in components(m) + components(~m) if c >= 0.005 * m.size]
    palette = [{"oklab": [round(float(v), 4) for v in c], "share": round(float(sh), 3)}
               for c, sh in sorted(zip(centres, share), key=lambda t: -t[1])]
    declared = None
    pal = (meta.get("sonicScene") or {}).get("palette") or {}
    if pal:
        # Dominant and secondary are large areas: they must be clusters of the frame. An accent stays under a tenth
        # of the frame and a highlight may appear only on hits, so those are looked for among the 5% most chromatic
        # and the 5% brightest pixels of every analysed frame instead.
        allp = np.concatenate([frames[i].reshape(-1, 3) for i in range(0, len(frames), max(1, len(frames) // 24))])
        lab_all = oklab(allp[::3])
        chroma_all = np.hypot(lab_all[:, 1], lab_all[:, 2])
        vivid = lab_all[chroma_all >= np.percentile(chroma_all, 95)]
        bright = lab_all[lab_all[:, 0] >= np.percentile(lab_all[:, 0], 95)]
        rare = np.concatenate([vivid, bright])
        declared = {}
        for k, hx in pal.items():
            try:
                target = oklab(hex_to_rgb(hx)[None])[0]
            except (ValueError, IndexError, AttributeError):
                continue
            if k in ("dominant", "secondary"):
                dist = float(np.sqrt(((centres - target) ** 2).sum(1)).min())
                where = "clusters"
            else:
                dist = float(np.sqrt(((rare - target) ** 2).sum(1)).min()) if len(rare) else 9.0
                where = "the most chromatic and brightest 5%"
            declared[k] = {"hex": hx, "nearest": round(dist, 4), "searched": where}
    return {"composition": comp, "palette": palette, "hueSpread": hue_spread, "colourfulness": colourfulness,
            "clip": clip, "muddiness": mud, "valueRange": value_range, "notanShapes": len(big),
            "declaredPalette": declared}


def cell_series(frames):
    """Per cell of the grid: mean luma, the 95th-percentile luma (so a small bright responder -- a glint, a streak --
    counts), and motion energy (frame difference)."""
    n = len(frames)
    L = luma(frames)
    blocks = L.reshape(n, GH, H // GH, GW, W // GW).transpose(0, 1, 3, 2, 4).reshape(n, GH * GW, -1)
    cells = blocks.mean(2)
    peaks = np.percentile(blocks, 95, axis=2)
    diff = np.abs(np.diff(L, axis=0, prepend=L[:1]))
    dcells = diff.reshape(n, GH, H // GH, GW, W // GW).mean(axis=(2, 4)).reshape(n, -1)
    return L, cells, dcells, peaks


def hp(x, win):
    if len(x) < win * 2:
        return x - x.mean()
    k = np.ones(win) / win
    return x - np.convolve(np.pad(x, (win // 2, win - 1 - win // 2), mode="edge"), k, mode="valid")


def corr(a, b):
    a = a - a.mean()
    b = b - b.mean()
    d = math.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d > 1e-12 else 0.0


def lagged(sig, vis, F, lags=None):
    best = (0.0, 0)
    for ms in (lags or LAGS_MS):
        k = int(round(ms / 1000.0 * F))
        if k >= 0:
            a, b = sig[: len(sig) - k], vis[k:]
        else:
            a, b = sig[-k:], vis[: len(vis) + k]
        if len(a) < 10:
            continue
        r = corr(a, b)
        if r > best[0]:
            best = (r, ms)
    return best


def surrogate(sig, rng):
    """A phase-randomised copy: the same power spectrum (so the same rhythm, the same autocorrelation), every
    alignment with the picture broken. A circular shift of a four-on-the-floor envelope lands on the other kicks and
    keeps the alignment; this does not."""
    X = np.fft.rfft(sig - sig.mean())
    ph = rng.uniform(0, 2 * np.pi, len(X))
    ph[0] = 0.0
    if len(sig) % 2 == 0:
        ph[-1] = 0.0
    return np.fft.irfft(np.abs(X) * np.exp(1j * ph), len(sig))


def lagged_all(sig, V, F, lags):
    """The best correlation of `sig` with every column of V over the lags: (r per column, lag ms per column)."""
    n, m = V.shape
    best_r = np.full(m, -1.0)
    best_lag = np.zeros(m, int)
    for ms in lags:
        k = int(round(ms / 1000.0 * F))
        if k >= 0:
            a, B = sig[: n - k], V[k:]
        else:
            a, B = sig[-k:], V[: n + k]
        if len(a) < 10:
            continue
        a = a - a.mean()
        B = B - B.mean(0)
        den = np.sqrt((a * a).sum() * (B * B).sum(0))
        r = np.where(den > 1e-12, (a[:, None] * B).sum(0) / np.maximum(den, 1e-12), 0.0)
        better = r > best_r
        best_r = np.where(better, r, best_r)
        best_lag = np.where(better, ms, best_lag)
    return best_r, best_lag


def null_z(sig, vis, F, r, shifts=24, lags=None, events=None, lag_ms=None):
    """z of the observed best correlation against the same search -- every cell, every feature, every lag -- over
    phase-randomised surrogates of the signal. The search has to be repeated: the best of 144 series and 16 lags is
    high by chance alone, and a null that searched only the chosen cell called an unrelated envelope significant."""
    rng = np.random.default_rng(1)
    vals = []
    n = len(sig)
    ioi = float(np.median(np.diff(events))) if events is not None and len(events) >= 4 else 0.0
    lags = lags or LAGS_MS
    span = (max(lags) - min(lags)) / 1000.0 * F
    regular = ioi >= 2 and float(np.std(np.diff(events)) / ioi) < 0.15
    if regular and ioi <= 1.1 * span and lag_ms is not None:
        # The lag search spans a whole period: it would realign any moved copy of a regular pulse train, so for this
        # material the null is judged at the lag the picture was found at (the event-locked latency says which).
        lags = [lag_ms]
    for _ in range(shifts):
        if ioi >= 2:
            # A hit envelope keeps its pulses and is moved between multiples of its own inter-onset interval: off the
            # beat for four-on-the-floor (a plain circular shift lands on the other kicks), at random for irregular hits.
            m = int(rng.integers(1, max(2, int((n - ioi) // ioi))))
            sur = np.roll(sig, int(round((m + rng.uniform(0.25, 0.75)) * ioi)))
        else:
            sur = surrogate(sig, rng)
        vals.append(max(float(lagged_all(sur, V, F, lags)[0].max()) for V in vis.values()))
    # On Fisher's z (atanh r), where a correlation's sampling spread no longer shrinks toward 1: smooth series give
    # surrogates near the top of the range, and a raw-r z would compress a real 0.99 against a chance 0.7.
    v = np.arctanh(np.clip(np.array(vals), -0.999, 0.999))
    return float((np.arctanh(min(r, 0.999)) - v.mean()) / max(v.std(), 1e-6))


def events_of(env, F, rise=0.15):
    """Onsets of an envelope signal: frames where it jumps up by `rise` (hits are envelopes in a trace)."""
    d = np.diff(env, prepend=env[:1])
    idx = [i for i in range(1, len(env)) if d[i] >= rise and d[i - 1] < rise]
    out, last = [], -10 ** 9
    for i in idx:
        if i - last >= int(0.06 * F):
            out.append(i)
            last = i
    return out


def erp(vis, events, F, pre=0.2, post=1.0):
    a, b = int(pre * F), int(post * F)
    wins = [vis[i - a:i + b] for i in events if i - a >= 0 and i + b <= len(vis)]
    if len(wins) < 3:
        return None
    w = np.array(wins)
    base = w[:, :a]
    mu, sd = base.mean(), max(base.std(), 1e-6)
    z = (w - mu) / sd
    m = z.mean(0)
    pk = int(m[a:].argmax()) + a
    latency = (pk - a) / F
    gain = float(m[pk])
    after = m[pk:]
    below = np.where(after < gain / math.e)[0]
    decay = float(below[0] / F) if len(below) else float(len(after) / F)
    reliability = float((z[:, a:].max(1) > 2.0).mean())
    return {"latency": round(latency, 3), "gain": round(gain, 2), "decay": round(decay, 3),
            "reliability": round(reliability, 2), "events": len(wins)}


def mod_centroid(x, F):
    x = x - x.mean()
    if len(x) < 16 or x.std() < 1e-9:
        return None
    p = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / F)
    m = (f >= 0.1) & (f <= min(15.0, F / 2))
    if p[m].sum() <= 0:
        return None
    return float((p[m] * f[m]).sum() / p[m].sum())


SLOW_LAGS_MS = list(range(0, 3001, 100))


def is_slow(name):
    return name in SLOW_SIGNALS or name in ("response.level",) or name.endswith(".slow")


def correspondence(trace, frames, F, video_start, skip=1.5):
    n = len(frames)
    t = video_start + np.arange(n) / F
    L, cells, motion, peaks = cell_series(frames)
    win = max(3, int(F))
    slow_win = max(3, int(8 * F))
    # Fast signals: prewhitened (high-passed over 1 s, then differenced): two slowly drifting series correlate
    # whatever drives them. Slow signals (a sustain that brightens the sky over two seconds) are judged on series
    # high-passed over 8 s and not differenced, at lags up to 3 s: whitening would remove the very response they make.
    white = lambda x: np.diff(hp(x, win), prepend=0.0)
    # ...whitened at their own scale: the change over a third of a second, which keeps the 1-3 s lags and removes the
    # drift that makes any two slow series correlate.
    step = max(1, int(F / 3))
    calm = lambda x: (lambda y: np.concatenate([np.zeros(step), y[step:] - y[:-step]]))(hp(x, slow_win))
    series = {"luma": cells, "motion": motion, "peak": peaks}
    fast = {k: np.stack([white(v[:, j]) for j in range(v.shape[1])], 1) for k, v in series.items()}
    slow = {k: np.stack([calm(v[:, j]) for j in range(v.shape[1])], 1) for k, v in series.items() if k != "motion"}
    out = {}
    tt = trace.get("time")
    if tt is None:
        return out, motion
    # The lead-in is left out: a world growing in from silence while every signal rises from zero is one shared
    # event, and a single coincidence dominates a correlation.
    k0 = min(int(skip * F), max(n - 32, 0))
    fast = {k: v[k0:] for k, v in fast.items()}
    slow = {k: v[k0:] for k, v in slow.items()}
    series = {k: v[k0:] for k, v in series.items()}
    t = t[k0:]
    names = [k for k in trace if k != "time" and k.startswith(("response.", "notes.", "visual.", "sonic.")) and
             not k.startswith("notes.voice.") and not k.startswith("notes.class")]
    for name in names:
        raw = np.interp(t, tt, trace[name])
        if raw.std() < 1e-6:
            continue
        slowly = is_slow(name)
        sig = calm(raw) if slowly else white(raw)
        vis = slow if slowly else fast
        lags = SLOW_LAGS_MS if slowly else LAGS_MS
        best = {"r": 0.0}
        for kind, V in vis.items():
            rs, ls = lagged_all(sig, V, F, lags)
            j = int(np.argmax(rs))
            if rs[j] > best["r"]:
                best = {"r": float(rs[j]), "lag_ms": int(ls[j]), "cell": j, "feature": kind,
                        "map": [round(float(x), 3) for x in rs]}
        if best["r"] <= 0:
            out[name] = {"r": 0.0, "slow": slowly}
            continue
        best["slow"] = slowly
        events = events_of(raw, F) if name.endswith("Env") else None
        best["z"] = round(null_z(sig, vis, F, best["r"], lags=lags, events=events, lag_ms=best["lag_ms"]), 2)
        best["r"] = round(best["r"], 3)
        # The share of the frame answering it (cells within 70% of the best): whole-frame or local?
        m = np.array(best["map"])
        best["spread"] = round(float((m >= 0.7 * best["r"]).mean()), 3)
        cell_series_ = series[best["feature"]][:, best["cell"]]
        if name.endswith("Env"):
            best["erp"] = erp(cell_series_, events_of(raw, F), F)
        cs = mod_centroid(raw, F)
        cv = mod_centroid(cell_series_, F)
        if cs and cv:
            best["modulationRatio"] = round(cv / cs, 2)
        out[name] = best
    return out, motion


def measure(args):
    frames, F = decode(args.video, args.fps, 0.0)
    trace = read_trace(args.trace) if args.trace else {}
    doc, scene = load_project(args.project)
    meta = {"sonicScene": doc.get("sonicScene") or scene.get("sonicScene") or {}}
    fx = Findings()
    st = measure_frames(frames, F, meta)
    # ---- composition ----
    comp = st["composition"]
    med = lambda k: float(np.median([c[k] for c in comp]))
    if med("peaks") >= 4:
        fx.add("competing_focal_points", "composition", "medium", "Competing focal points",
               "%.0f saliency peaks above half the strongest in a typical frame: attention is split." % med("peaks"),
               {"peaks": med("peaks")})
    if med("dominance") < 1.5:
        fx.add("weak_hierarchy", "visual_hierarchy", "medium", "No dominant subject",
               "The strongest salient region is only %.2fx the second." % med("dominance"),
               {"dominance": med("dominance")})
    if med("symmetry") > 0.85:
        fx.add("mirror_symmetry", "composition", "low", "Strong mirror symmetry",
               "Left-right correlation %.2f: deliberate, or a procedural tell." % med("symmetry"),
               {"symmetry": med("symmetry")})
    if med("negative") < 0.10:
        fx.add("crowded", "composition", "medium", "Little negative space",
               "%.0f%% of the frame is quiet." % (100 * med("negative")), {"negative": med("negative")})
    if med("balance") > 0.7:
        fx.add("lopsided", "composition", "low", "Visual mass far off centre",
               "The saliency centre of mass is %.2f of the half-frame from the centre." % med("balance"),
               {"balance": med("balance")})
    # ---- colour ----
    if st["clip"] > 0.02:
        fx.add("blown_highlights", "color", "medium", "Blown highlights",
               "%.1f%% of pixels clip." % (100 * st["clip"]), {"clip": st["clip"]})
    if st["muddiness"] > 0.4:
        fx.add("muddy", "color", "medium", "Muddy midtones", "%.0f%% grey midtones." % (100 * st["muddiness"]),
               {"muddiness": st["muddiness"]})
    if st["valueRange"] < 0.15:
        fx.add("compressed_values", "color", "low", "Compressed value range",
               "p95 - p5 luma is %.2f." % st["valueRange"], {"valueRange": st["valueRange"]})
    if st["notanShapes"] > 12:
        fx.add("value_noise", "color", "low", "Value noise",
               "%d light/dark shapes in the value structure (strong design has a few large ones)." % st["notanShapes"],
               {"shapes": st["notanShapes"]})
    if st["declaredPalette"]:
        off = {k: v for k, v in st["declaredPalette"].items() if v["nearest"] > 0.12}
        if off:
            fx.add("palette_off_intent", "color", "medium", "Declared palette not on screen",
                   "No measured colour is near the scene's declared %s." % ", ".join(sorted(off)), off)
    # ---- defects ----
    if med("std") < 0.01:
        fx.add("empty_frame", "technical_quality", "high", "Empty frame", "Luma standard deviation %.4f." % med("std"))
    L = luma(frames)
    cells = L.reshape(len(frames), GH, H // GH, GW, W // GW).mean(axis=(2, 4)).reshape(len(frames), -1)
    dl = np.diff(cells, axis=0)
    big = (np.abs(dl) >= 0.1).mean(1) >= 0.25
    signs = np.sign(dl.mean(1))
    flashes = [i for i in range(1, len(big)) if big[i] and big[i - 1] is not None and signs[i] != signs[i - 1]]
    worst = 0
    for i in range(len(big)):
        worst = max(worst, sum(1 for f in flashes if i <= f < i + int(F)) // 2)
    if worst > 3:
        fx.add("flash_risk", "technical_quality", "high", "Flash risk",
               "%d general flashes in one second over a quarter of the frame (WCAG 2.3.1 allows 3)." % worst,
               {"perSecond": worst})
    gm = cells.mean(1)
    if len(gm) > 32:
        p = np.abs(np.fft.rfft(gm - gm.mean())) ** 2
        f = np.fft.rfftfreq(len(gm), 1 / F)
        share = float(p[(f >= 3) & (f <= 30)].sum() / max(p[f > 0].sum(), 1e-12))
        if share > 0.35:
            fx.add("flicker", "temporal_coherence", "medium", "Whole-frame flicker",
                   "%.0f%% of the mean-luma modulation is 3-30 Hz." % (100 * share), {"share": share})
    # ---- motion hierarchy ----
    _, _, motion, _ = cell_series(frames)
    speed = motion.mean(0)
    moving = speed[speed > 1e-4]
    tiers = {}
    if len(moving) >= 4:
        ratio = float(np.percentile(moving, 90) / max(np.percentile(moving, 10), 1e-6))
        tiers = {"p90/p10": round(ratio, 2), "gini": round(gini(moving), 3)}
        if ratio < 2.0:
            fx.add("uniform_motion", "motion", "medium", "Everything moves alike",
                   "Cell motion p90/p10 is %.2f: no slow background against a fast focus." % ratio, tiers)
    # ---- audio-visual ----
    av = {}
    if trace:
        av, _ = correspondence(trace, frames, F, args.video_start, getattr(args, "skip", 1.5))
        for name, r in sorted(av.items()):
            if r.get("r", 0) <= 0:
                continue
            if r.get("z", 0) >= 3 and r.get("spread", 0) > 0.7 and name.startswith("response."):
                fx.add("whole_frame_response", "musical_synchronization", "medium", "Whole frame answers " + name,
                       "%.0f%% of the frame responds to %s alike; a response reads as authored when it is placed."
                       % (100 * r["spread"], name), r, region=name)
            e = r.get("erp")
            if e and name.endswith("Env") and name.startswith("response.") and e["latency"] > 0.1:
                fx.add("late_response", "musical_synchronization", "medium", "Late response to " + name,
                       "The picture peaks %.0f ms after the hit." % (1000 * e["latency"]), e, region=name)
            # Slow against fast, judged only where the class is known: sustained energy should move the picture
            # slowly, a hit's envelope quickly. (Any other signal shares its cell with everything else on screen.)
            mr = r.get("modulationRatio")
            if mr and r.get("z", 0) >= 3:
                if name in SLOW_SIGNALS and mr > 4.0:
                    # An observation, not an issue: the cell's spectrum is shared by everything else in it.
                    fx.add("sustain_moves_fast", "musical_synchronization", "info", "Sustained sound, fast picture",
                           "Where the picture follows %s it modulates %.1fx faster than the signal." % (name, mr),
                           r, region=name, kind="observation")
                elif name.startswith("response.") and name.endswith("Env") and mr < 0.25:
                    fx.add("hit_moves_slow", "musical_synchronization", "low", "A hit with a slow answer",
                           "Where the picture follows %s it modulates %.1fx slower than the hits." % (name, 1 / mr),
                           r, region=name)
        # specificity: do the kick and the hat move different places?
        k, h = av.get("response.kickEnv"), av.get("response.hatEnv")
        if k and h and k.get("map") and h.get("map") and k.get("z", 0) >= 3 and h.get("z", 0) >= 3:
            sim = corr(np.array(k["map"]), np.array(h["map"]))
            if sim > 0.8:
                fx.add("kick_hat_same_place", "musical_synchronization", "medium", "Kick and hat move the same place",
                       "Their synchrony maps correlate %.2f: drums read as one gesture." % sim, {"similarity": sim})
        # the scene's own vocabulary: every signal it says the world answers, answered?
        for row in (meta["sonicScene"].get("vocabulary") or []):
            sigs = [s for s in row if isinstance(s, str) and "." in s and s.split(".")[0] in
                    ("response", "notes", "visual", "sonic", "audio", "timbre")]
            for s in sigs[:1]:
                key = s if s in av else (s + "Env" if (s + "Env") in av else None)
                r = av.get(key) if key else None
                if key and r is not None and r.get("z", 0) < 2.0:
                    # A slow signal's answer is judged on its own timescale (correspondence), but a two-second swell
                    # is harder to prove than a hit: an observation, not an issue.
                    slowly = r.get("slow", is_slow(key))
                    fx.add("vocabulary_silent", "musical_synchronization", "low" if slowly else "medium",
                           "No visible answer to " + s,
                           "The scene says '%s' answers %s; nothing on screen follows it (z %.1f)."
                           % (row[-1] if row else "", s, r.get("z", 0)), r, region=s)
    score = {}
    for d in ("composition", "visual_hierarchy", "color", "motion", "musical_synchronization", "technical_quality",
              "temporal_coherence"):
        pen = sum(SEVERITY[f["severity"]] for f in fx.items if f["dimension"] == d)
        score[d] = round(math.exp(-0.35 * pen), 3)
    report = {"schema": "sonic.vfx.critic/1", "video": args.video, "trace": args.trace, "project": args.project,
              "fps": F, "frames": len(frames), "scores": score, "findings": fx.items,
              "measures": {"frames": st, "motionTiers": tiers,
                           "audioVisual": {k: {kk: vv for kk, vv in v.items() if kk != "map"} for k, v in av.items()},
                           "synchronyMaps": {k: v.get("map") for k, v in av.items() if v.get("map")}},
              "thesis": meta["sonicScene"].get("thesis")}
    json.dump(report, open(args.out, "w"), indent=1)
    if args.md:
        write_md(report, args.md)
    print("%d findings; scores %s" % (len(fx.items), score))
    return report


def write_md(r, path):
    lines = ["# Sonic VFX critic: %s" % os.path.basename(r["video"]), ""]
    if r.get("thesis"):
        lines += ["Thesis: " + r["thesis"], ""]
    lines += ["| dimension | score |", "|---|---|"] + ["| %s | %.2f |" % kv for kv in r["scores"].items()] + [""]
    lines += ["## Findings", ""]
    for f in sorted(r["findings"], key=lambda f: -SEVERITY[f["severity"]]):
        lines.append("- **%s** (%s, %s): %s" % (f["title"], f["severity"], f["dimension"], f["message"]))
    lines += ["", "## Audio-visual correspondence (strongest responders)", "",
              "| signal | r | z | lag ms | where | spread | latency / decay | mod. ratio |", "|---|---|---|---|---|---|---|---|"]
    av = r["measures"]["audioVisual"]
    for name, v in sorted(av.items(), key=lambda kv: -kv[1].get("z", 0))[:20]:
        e = v.get("erp") or {}
        lines.append("| %s | %s | %s | %s | %s %s | %s | %s | %s |" % (
            name, v.get("r"), v.get("z"), v.get("lag_ms"), v.get("feature", ""),
            "cell %d,%d" % (v["cell"] % GW, v["cell"] // GW) if "cell" in v else "", v.get("spread"),
            ("%s / %s" % (e.get("latency"), e.get("decay"))) if e else "", v.get("modulationRatio")))
    open(path, "w").write("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------------------------------- inputs / compare


def inputs(args):
    trace = read_trace(args.trace)
    doc, scene = load_project(args.project)
    ss = doc.get("sonicScene") or scene.get("sonicScene") or {}
    tt = trace["time"]
    hz = 1.0 / float(np.median(np.diff(tt)))
    pick = lambda *names: next((trace[n] for n in names if n in trace), np.zeros_like(tt))
    feats = {"hz": hz, "t0": float(tt[0]),
             "low": [round(float(v), 4) for v in pick("response.bass", "sonic.energy")],
             "lowmid": [round(float(v), 4) for v in pick("response.sustain", "sonic.energy.slow")],
             "mid": [round(float(v), 4) for v in pick("response.snareEnv")],
             "high": [round(float(v), 4) for v in pick("response.hatEnv")],
             "onset": [round(float(v), 4) for v in pick("response.onsetEnv", "response.transient")]}
    F = hz
    kicks = [round(float(tt[i]), 3) for i in events_of(pick("response.kickEnv"), F)]
    notes = [round(float(tt[i]), 3) for i in events_of(pick("response.noteEnv"), F)]
    routes = []
    for r in (doc.get("routes") or []):
        if isinstance(r, dict) and r.get("source") and r.get("target"):
            ev = kicks if "kick" in r["source"] else notes if "note" in r["source"] else None
            item = {"source": r["source"], "target": r["target"]}
            if ev:
                item["events"] = ev
            routes.append(item)
    out = {"video": {"path": os.path.abspath(args.video), "start": args.video_start},
           "audio": {"analysis": {"features": feats, "low_onsets": kicks, "onsets": notes}, "start": 0.0},
           "intent": {"mood": ss.get("thesis", ""), "visual_language": json.dumps(ss.get("palette", {})),
                      "segments": []},
           "scene": {"modulation": routes}}
    json.dump(out, open(args.out, "w"), indent=1)
    print("wrote", args.out, "(%d routes, %d kicks, %d note-ons)" % (len(routes), len(kicks), len(notes)))


def compare(args):
    a, b = json.load(open(args.a)), json.load(open(args.b))
    ka = {f["key"]: f for f in a["findings"]}
    kb = {f["key"]: f for f in b["findings"]}
    print("scores:")
    for d in a["scores"]:
        print("  %-24s %.2f -> %.2f" % (d, a["scores"][d], b["scores"].get(d, float("nan"))))
    for k in sorted(set(ka) - set(kb)):
        print("  resolved:", k)
    for k in sorted(set(kb) - set(ka)):
        print("  new:     ", k, "-", kb[k]["message"])


def trace_cmd(args):
    subprocess.run([AVGEN, "--project", args.project, "--sonic-trace", args.out], check=True)

# ---------------------------------------------------------------------------------------------------- selftest


def _clip(tmp, name, frames, columns, F=30.0):
    """Writes a synthetic clip and its trace; returns (video, trace)."""
    vid = os.path.join(tmp, name + ".mp4")
    n, h, w = frames.shape[:3]
    p = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (w, h),
                          "-r", "%g" % F, "-i", "-", "-pix_fmt", "yuv420p", "-crf", "12", vid], stdin=subprocess.PIPE)
    p.communicate(frames.tobytes())
    tr = os.path.join(tmp, name + ".csv")
    with open(tr, "w") as f:
        f.write("time," + ",".join(columns) + "\n")
        for i in range(n):
            f.write("%.4f," % (i / F) + ",".join("%.4f" % columns[c][i] for c in columns) + "\n")
    return vid, tr


def _measure_clip(tmp, name, frames, columns):
    vid, tr = _clip(tmp, name, frames, columns)
    a = argparse.Namespace(video=vid, trace=tr, project=None, video_start=0.0, fps=30.0,
                           out=os.path.join(tmp, name + ".json"), md=None, skip=0.0)
    return measure(a)["measures"]["audioVisual"]


def selftest(_args):
    """Synthetic clips with known answers:
       local    one region flashes on irregular 'kicks', the rest drifts with a 'pad': found there, near zero lag,
                local, early in its event-locked response
       periodic the same on a strict four-on-the-floor grid: a circularly shifted null lands on the other kicks; the
                phase-randomised one must still call it significant
       small    a 6x6 px glint flashing on the kicks, nothing else moving: found in its cell, by its peak luma
       slow     a region that brightens with a sustain signal 1.5 s late and smoothly: significant on the slow tier
       control  an unrelated envelope against the glint clip: not significant"""
    tmp = tempfile.mkdtemp(prefix="svc-")
    F, n = 30.0, 300
    t = np.arange(n) / F
    rng = np.random.default_rng(7)
    ok = True

    def envelope(kicks):
        env = np.zeros(n)
        for k in kicks:
            env[k:] = np.maximum(env[k:], np.exp(-(np.arange(n - k)) / (0.15 * F)))
        return env

    def check(label, passed):
        nonlocal ok
        print("  %-52s %s" % (label, "ok" if passed else "FAIL"))
        ok = ok and passed

    # local, with irregular kicks
    kicks = sorted(set(int(F * x) for x in np.cumsum(rng.uniform(0.3, 0.8, 30)) if x * F < n - 1))
    env = envelope(kicks)
    pad = 0.5 + 0.5 * np.sin(2 * math.pi * 0.1 * t)
    frames = np.zeros((n, 216, 384, 3), np.uint8)
    for i in range(n):
        frames[i, :, :, 2] = int(40 + 60 * pad[i])
        frames[i, 30:90, 40:120, :] = int(30 + 220 * env[i])
    k = _measure_clip(tmp, "local", frames, {"response.kickEnv": env, "response.sustain": pad})["response.kickEnv"]
    cell = (k["cell"] % GW, k["cell"] // GW)
    check("local: kick z >= 3", k["z"] >= 3)
    check("local: kick lag within 70 ms", abs(k["lag_ms"]) <= 70)
    check("local: kick found top-left (cell x<=2, y<=2)", cell[0] <= 2 and cell[1] <= 2)
    check("local: kick is local (spread < 0.4)", k["spread"] < 0.4)
    check("local: event-locked latency < 0.1 s", (k.get("erp") or {}).get("latency", 9) < 0.1)

    # periodic: every 15 frames exactly
    env = envelope(list(range(10, n, 15)))
    frames = np.full((n, 216, 384, 3), 40, np.uint8)
    for i in range(n):
        frames[i, 120:200, 250:360, :] = int(30 + 220 * env[i])
    k = _measure_clip(tmp, "periodic", frames, {"response.kickEnv": env})["response.kickEnv"]
    check("periodic: four-on-the-floor kick z >= 3", k["z"] >= 3)

    # small: a 6x6 glint
    env = envelope(kicks)
    frames = np.full((n, 216, 384, 3), 25, np.uint8)
    for i in range(n):
        frames[i, 170:176, 330:336, :] = int(25 + 230 * env[i])
    k = _measure_clip(tmp, "small", frames, {"response.kickEnv": env})["response.kickEnv"]
    cell = (k["cell"] % GW, k["cell"] // GW)
    check("small: glint z >= 3", k["z"] >= 3)
    check("small: glint found in its cell (x 6, y 4)", cell == (6, 4))
    # the control: an envelope of other, unrelated kicks against the same clip
    other = envelope(sorted(set(int(F * x) for x in np.cumsum(rng.uniform(0.3, 0.8, 30)) if x * F < n - 1)))
    k = _measure_clip(tmp, "control", frames, {"response.kickEnv": other})["response.kickEnv"]
    check("control: an unrelated envelope is not significant (z %.1f)" % k.get("z", 0), k.get("z", 0) < 3)

    # slow: 20 s of a smoothed random sustain, the picture following 1.5 s late and smoothed over a second
    n2 = 600
    t2 = np.arange(n2) / F
    walk = np.cumsum(rng.normal(0, 1, n2))
    sus = np.convolve(np.pad(walk, (30, 29), mode="edge"), np.ones(60) / 60, mode="valid")
    sus = (sus - sus.min()) / max(sus.max() - sus.min(), 1e-9)
    late = np.interp(t2 - 1.5, t2, sus)
    late = np.convolve(np.pad(late, (15, 14), mode="edge"), np.ones(30) / 30, mode="valid")
    frames = np.full((n2, 216, 384, 3), 30, np.uint8)
    for i in range(n2):
        frames[i, 0:80, :, :] = int(40 + 180 * late[i])
    k = _measure_clip(tmp, "slow", frames, {"response.sustain": sus})["response.sustain"]
    check("slow: sustain judged on the slow tier", bool(k.get("slow")))
    check("slow: sustain z >= 3 (z %.1f)" % k.get("z", 0), k.get("z", 0) >= 3)
    check("slow: lag found near 1.5 s (0.9-2.2 s; %d ms)" % k.get("lag_ms", 0), 900 <= k.get("lag_ms", 0) <= 2200)

    print("selftest", "passed" if ok else "FAILED")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("measure")
    m.add_argument("--video", required=True)
    m.add_argument("--trace")
    m.add_argument("--project")
    m.add_argument("--video-start", type=float, default=0.0)
    m.add_argument("--fps", type=float, default=30.0)
    m.add_argument("--out", required=True)
    m.add_argument("--md")
    m.add_argument("--skip", type=float, default=1.5, help="seconds of lead-in left out of the correspondence")
    i = sub.add_parser("inputs")
    i.add_argument("--video", required=True)
    i.add_argument("--trace", required=True)
    i.add_argument("--project")
    i.add_argument("--video-start", type=float, default=0.0)
    i.add_argument("--out", required=True)
    c = sub.add_parser("compare")
    c.add_argument("a")
    c.add_argument("b")
    t = sub.add_parser("trace")
    t.add_argument("--project", required=True)
    t.add_argument("--out", required=True)
    sub.add_parser("selftest")
    args = ap.parse_args()
    r = {"measure": measure, "inputs": inputs, "compare": compare, "trace": trace_cmd, "selftest": selftest}[args.cmd](args)
    return r if isinstance(r, int) else 0


if __name__ == "__main__":
    sys.exit(main())

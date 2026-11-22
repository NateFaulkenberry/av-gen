#!/usr/bin/env python3
"""Neon Dreams: the Sabertooth stems' events, for build.py's palette arc and the reactivity sheet.

    /usr/bin/python3 examples/neon-dreams/audio/analyse_stems.py [--audio ~/Desktop/sabertooth]

Writes sabertooth_events.json beside this file: per stem the onsets (time, strength) picked from a 10 ms RMS envelope
(rise over an adaptive floor), and per stem a 30 fps level envelope (dB-scaled 0..1) for the plots. Needs numpy and
ffmpeg. The stems were checked against the mix (iteration 5): every one lines up at 0 ms (sample xcorr, and band
envelopes within one 10 ms hop), including the four the engine's probe warns about (HH, Crash, tone block, Cloud
Diving: sparse or processed in the mastered mix, so their waveform correlation is low, not misaligned).
"""
import argparse, json, os, subprocess
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
# it13 (owner msg 178: "the pulse waves from the speakers don't seem lined up with the beat"): at 22050 Hz a 10 ms hop
# is 220.5 samples; the envelope used 220 (0.009977 s) and stamped each one 0.01 s, so every onset drifted late by
# 0.23 % of its time (102 ms at 45 s, on top of the envelope peak's ~50 ms). 24 kHz makes the hop exactly 240 samples,
# and each onset is moved back to its attack's leading edge on a 1 ms envelope (`leading_edge`).
SR = 24000
HOP = 0.01
HOP_N = 240
STEMS = {"kick": "Kick", "snare": "Snare", "hh": "HH", "toms": "Toms", "crash": "Crash", "bass": "BASS",
         "moog": "Moog", "cloud": "Cloud Diving", "block": "tone block"}
# onset picking: (relative threshold over the stem's 99th percentile, minimum gap in s)
PICK = {"kick": (0.35, 0.12), "snare": (0.35, 0.12), "hh": (0.3, 0.06), "toms": (0.3, 0.08), "crash": (0.45, 1.2),
        "block": (0.35, 0.08), "cloud": (0.4, 2.0), "moog": (0.4, 0.3), "bass": (0.4, 0.2)}


def load(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32)


def envelope(x):
    h = HOP_N
    n = len(x) // h
    return np.sqrt((x[: n * h].reshape(n, h) ** 2).mean(1) + 1e-12)


def leading_edge(x, t, back=0.07, ahead=0.02, frac=0.1):
    """The attack's leading edge near a picked onset `t` (an envelope peak on the 10 ms grid): the strongest 1 ms RMS
    in [t - back, t + ahead], then back from it to where the envelope first stood under `frac` of that peak."""
    a, b = max(0, int((t - back) * SR)), min(len(x), int((t + ahead) * SR))
    if b - a < 48:
        return t
    w = 24
    e = np.sqrt(np.convolve(x[a:b] ** 2, np.ones(w) / w, "same"))
    k = int(np.argmax(e))
    below = np.where(e[:k] < frac * e[k])[0]
    return (a + (below[-1] + 1 if len(below) else 0)) / SR


def onsets(env, rel, gap):
    db = 20 * np.log10(env + 1e-9)
    top = np.percentile(env, 99.5)
    floor = np.convolve(env, np.ones(30) / 30, "same")
    rise = np.maximum(env[1:] - np.maximum(env[:-1], floor[:-1] * 0.8), 0)
    out = []
    for i in range(1, len(rise) - 1):
        if rise[i] > rel * top * 0.25 and rise[i] >= rise[i - 1] and rise[i] >= rise[i + 1] and env[i + 1] > rel * top:
            t = (i + 1) * HOP
            if out and t - out[-1][0] < gap:
                if env[i + 1] / top > out[-1][1]:
                    out[-1] = (t, float(env[i + 1] / top))
                continue
            out.append((round(t, 3), round(float(min(env[i + 1] / top, 1.5)), 3)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--audio", default=os.path.expanduser("~/Desktop/sabertooth"))
    a = ap.parse_args()
    res = {"source": "Sabertooth stems", "hop": HOP, "fps": 30, "stems": {},
           "note": "it13 (msg 178): exact 10 ms hop at 24 kHz; onsets at each attack's leading edge"}
    for key, label in STEMS.items():
        x = load(os.path.join(a.audio, label + ".wav"))
        env = envelope(x)
        top = np.percentile(env, 99.5)
        lvl = np.clip((20 * np.log10(env / top + 1e-9) + 48) / 48, 0, 1)   # 0 at -48 dB under the stem's peak
        k = int(round(1 / 30 / HOP * 1000))
        fr = [round(float(lvl[int(i * (1 / 30) / HOP)]), 3) for i in range(int(len(lvl) * HOP * 30))]
        on = []
        for t, v in onsets(env, *PICK[key]):
            te = round(float(leading_edge(x, float(t))), 3)
            if on and te - on[-1][0] < 0.5 * PICK[key][1]:   # two picks on one attack: keep the stronger
                if v > on[-1][1]:
                    on[-1] = (on[-1][0], float(v))
                continue
            on.append((te, float(v)))
        res["stems"][key] = {"label": label, "onsets": on, "level30": fr}
    # silent bars of the tone block (rms under -40 dB for >= 0.5 s inside the song)
    env = np.array(res["stems"]["block"]["level30"])
    quiet, start = [], None
    for i, v in enumerate(env):
        if v < 0.17 and start is None:
            start = i
        if (v >= 0.17 or i == len(env) - 1) and start is not None:
            if (i - start) / 30 >= 0.5:
                quiet.append([round(start / 30, 2), round(i / 30, 2)])
            start = None
    res["block_silences"] = quiet
    with open(os.path.join(HERE, "sabertooth_events.json"), "w") as f:
        json.dump(res, f, separators=(",", ":"))
    for key, s in res["stems"].items():
        on = s["onsets"]
        print("%-6s %4d onsets  first %s" % (key, len(on), [o[0] for o in on[:8]]))
    print("crash:", [o[0] for o in res["stems"]["crash"]["onsets"]])
    print("toms:", [o[0] for o in res["stems"]["toms"]["onsets"]][:60])
    print("block silences:", quiet)


if __name__ == "__main__":
    main()

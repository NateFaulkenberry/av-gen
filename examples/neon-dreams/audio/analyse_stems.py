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
SR = 22050
HOP = 0.01
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
    h = int(SR * HOP)
    n = len(x) // h
    return np.sqrt((x[: n * h].reshape(n, h) ** 2).mean(1) + 1e-12)


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
    res = {"source": "Sabertooth stems", "hop": HOP, "fps": 30, "stems": {}}
    for key, label in STEMS.items():
        env = envelope(load(os.path.join(a.audio, label + ".wav")))
        top = np.percentile(env, 99.5)
        lvl = np.clip((20 * np.log10(env / top + 1e-9) + 48) / 48, 0, 1)   # 0 at -48 dB under the stem's peak
        k = int(round(1 / 30 / HOP * 1000))
        fr = [round(float(lvl[int(i * (1 / 30) / HOP)]), 3) for i in range(int(len(lvl) * HOP * 30))]
        res["stems"][key] = {"label": label, "onsets": onsets(env, *PICK[key]), "level30": fr}
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

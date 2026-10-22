#!/usr/bin/env python3
"""Living Prism probe B1b: what the signals do over a LONG noise floor after the music stops.

usage: noise_tail.py <B1 fixture dir> <out dir>

Writes long.wav (the B1 mix's first 18.8 s, then 80 s of white noise at -70 dBFS, about an open preamp or a quiet
room) and long.json (the orb scene, a `sonic` block, routes that copy audio.* into macro knobs because --sonic-trace
has no audio.* columns). The audio.<band> auto-gain divides by a running peak that falls with a 4 s time constant to a
1e-4 floor (analyzer.cpp), so the question is whether the noise is normalised up toward 1 once the music's peak has
decayed (ln(peak/1e-4) x 4 s, ~30-40 s).
"""
import json
import sys
import wave
from pathlib import Path

import numpy as np

fix = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
with wave.open(str(fix / "mix.wav")) as w:
    sr = w.getframerate()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32767
music = x[: int(18.8 * sr)]
rng = np.random.default_rng(3)
noise = rng.standard_normal(int(80 * sr)) * 10 ** (-70 / 20)
y = np.concatenate([music, noise])
with wave.open(str(out / "long.wav"), "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(sr)
    w.writeframes((np.clip(y, -1, 1) * 32767).astype("<i2").tobytes())
knobs = ["aBass", "aMid", "aTreble", "aFlux", "aOnsetStr", "aRms"]
src = ["audio.bass", "audio.mid", "audio.treble", "audio.spectralFlux", "audio.onsetStrength", "audio.rms"]
project = {
    "format": "avgen-project", "version": 4,
    "assets": {"scene": {"kind": "orb"}, "audio": str(out / "long.wav")},
    "sonic": {},
    "sources": [{"kind": "macro", "name": "macros", "settings": {"knobs": [{"name": k, "default": 0.0} for k in knobs]}},
                {"kind": "envelope", "name": "aOnset", "settings": {"trigger": "audio.onset"}},
                {"kind": "envelope", "name": "rOnset", "settings": {"trigger": "response.onset"}}],
    "parameters": {}, "routes": [{"source": s, "target": f"macros/{k}", "amount": 1.0 if k != "aOnsetStr" else 0.25,
                                  "op": "replace"} for s, k in zip(src, knobs)],
}
(out / "long.json").write_text(json.dumps(project, indent=1))
print("wrote", out)

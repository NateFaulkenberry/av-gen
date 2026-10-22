#!/usr/bin/env python3
"""Living Prism probe B1: a synthetic mix and two stems with KNOWN hit times.

Writes into OUT_DIR (argument 1; never into the repository):
  mix.wav, drums.wav, bass.wav  (48 kHz mono 16-bit)
  truth.json                    (the exact kick, snare and bass-note onset times, seconds)
  project.json                  (an offline AV Gen project: mix + two stems, a `sonic` block, envelope sources
                                 on every transient detector, and macro knobs fed by routes: two probes and four audio.* copies)

Kicks are on a 120 BPM grid with a sub-frame jitter that walks through 0..1/60 s, so the frame quantisation of each
detector shows up as a spread rather than a constant. Snares sit half a beat after every other kick (1.25 s + k). The bass plays 1 s notes with a
10 ms attack, plus one 6 s held note (12-18 s) to test sustained-tone false onsets; 18.8-21 s is digital silence
and 21-24 s a -70 dBFS noise floor in the mix (silence behaviour).
"""
import json
import sys
import wave
from pathlib import Path

import numpy as np

SR = 48000
DUR = 24.0
OUT = Path(sys.argv[1]).resolve()
# B4 (the kick/snare/hat phase-out study): "--hats" adds closed hats; without it the fixture is B1's, unchanged.
HATS = "--hats" in sys.argv[2:]
OUT.mkdir(parents=True, exist_ok=True)
rng = np.random.default_rng(7)
n = int(SR * DUR)
t = np.arange(n) / SR
drums = np.zeros(n)
bass = np.zeros(n)

kicks, snares, notes, hats = [], [], [], []
beat = 0.5
for i in range(36):  # 0.5 .. 18.0 s, then silence
    jitter = (i % 12) / 12.0 / 60.0  # walks through one 60 fps frame
    k = 0.5 + i * beat + jitter
    kicks.append(k)
    s0 = int(k * SR)
    L = int(0.25 * SR)
    tt = np.arange(L) / SR
    f = 45 + 40 * np.exp(-tt / 0.03)
    ph = 2 * np.pi * np.cumsum(f) / SR
    body = np.sin(ph) * np.exp(-tt / 0.12)
    click = rng.standard_normal(L) * np.exp(-tt / 0.002) * 0.5
    drums[s0:s0 + L] += 0.8 * (body + click)[: n - s0]
for j in range(18):
    s = 1.25 + j * 1.0 + 0.003 * (j % 5)  # off the kick grid by half a beat, so the classes separate
    if s > 18.0:
        break
    snares.append(s)
    s0 = int(s * SR)
    L = int(0.18 * SR)
    tt = np.arange(L) / SR
    noise = rng.standard_normal(L)
    # crude high-pass: first difference
    noise = np.diff(np.concatenate([[0.0], noise]))
    tone = np.sin(2 * np.pi * 190 * tt) * np.exp(-tt / 0.05)
    drums[s0:s0 + L] += (0.35 * noise * np.exp(-tt / 0.06) + 0.25 * tone)[: n - s0]

def note(start, length, hz, amp=0.35):
    s0 = int(start * SR)
    L = int(length * SR)
    tt = np.arange(L) / SR
    env = np.minimum(1.0, tt / 0.010) * np.minimum(1.0, (length - tt) / 0.05)
    wave_ = sum(np.sin(2 * np.pi * hz * h * tt) / h for h in range(1, 6))
    bass[s0:s0 + L] += (amp * env * wave_)[: n - s0]

for j, start in enumerate(np.arange(0.75, 12.0, 1.0)):
    hz = [55.0, 65.41, 49.0, 73.42][j % 4]
    notes.append(float(start))
    note(start, 0.8, hz)
notes.append(12.0)
note(12.0, 6.0, 55.0)  # one held note: a sustained tone must not keep firing onsets

if HATS:
    for i in range(35):  # closed hats at x.125 / x.625 s, off every other class's grid
        h = 0.625 + i * 0.5 + 0.002 * (i % 7)
        hats.append(h)
        s0 = int(h * SR)
        L = int(0.04 * SR)
        tt = np.arange(L) / SR
        nz = rng.standard_normal(L)
        nz = np.diff(np.diff(np.concatenate([[0.0, 0.0], nz])))  # a crude second-order high-pass
        drums[s0:s0 + L] += (0.18 * nz * np.exp(-tt / 0.008))[: n - s0]
mix = 0.7 * drums + 0.7 * bass
# 18.8-21 s: digital silence. 21-24 s: a -70 dBFS noise floor (a quiet room, an open preamp), in the mix only.
tail = t >= 21.0
mix[tail] += rng.standard_normal(int(tail.sum())) * (10 ** (-70 / 20)) * 0.9
peak = max(np.abs(mix).max(), np.abs(drums).max(), np.abs(bass).max())

def write(name, x):
    x = np.clip(x / peak * 0.9, -1, 1)
    with wave.open(str(OUT / name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((x * 32767).astype("<i2").tobytes())

write("mix.wav", mix)
write("drums.wav", drums)
write("bass.wav", bass)
(OUT / "truth.json").write_text(json.dumps({"kicks": kicks, "snares": snares, "hats": hats, "bassNotes": notes,
                                            "silenceFrom": 18.8}, indent=1))

# Envelope sources: one per transient detector. attackMs 0, sustain 1, hold 0, release 60 ms: the frame on which the
# envelope first reads > 0.5 is the frame it was triggered on (+ the envelope source's own publish rule).
detectors = {
    "aOnset": "audio.onset", "aBeat": "audio.beat", "aLow": "audio.onsetLow", "aMid": "audio.onsetMid",
    "rKick": "response.kick", "rSnare": "response.snare", "rLow": "response.low", "rOnset": "response.onset",
    "dOnset": "stem.drums.onset", "dKick": "stem.drums.kick", "dSnare": "stem.drums.snare",
    "bOnset": "stem.bass.onset", "beatPulse": "beat.pulse",
}
if HATS:
    detectors.update({"aHigh": "audio.onsetHigh", "rHat": "response.hat", "dHat": "stem.drums.hat"})
sources = [{"kind": "envelope", "name": k, "settings": {"trigger": v}} for k, v in detectors.items()]
sources.append({"kind": "macro", "name": "macros", "settings": {"knobs": [{"name": "rms", "default": 0.0},
                                                                          {"name": "kick", "default": 0.0},
                                                                          {"name": "aBass", "default": 0.0},
                                                                          {"name": "aTreble", "default": 0.0},
                                                                          {"name": "aOnsetStr", "default": 0.0},
                                                                          {"name": "tempoConf", "default": 0.0}]}})
params = {}
for k in detectors:
    p = f"sources/{k}/"
    params.update({p + "attackMs": 0.0, p + "decayMs": 0.0, p + "sustain": 1.0, p + "holdMs": 0.0,
                   p + "releaseMs": 60.0})
routes = [
    # a stateless route into a knob: macro.rms(t) should equal stem.drums.rms one frame earlier (ADR-1573)
    {"source": "stem.drums.rms", "target": "macros/rms", "amount": 1.0, "op": "replace"},
    # an event route with an in-chain envelope into a knob (Rainbow World's pattern for macros/swell)
    {"source": "stem.drums.kick", "target": "macros/kick", "amount": 1.0, "op": "replace",
     "chain": {"envelope": "linearfall", "envelopeFallPerSecond": 8.0}},
    # audio.* is not a --sonic-trace column; expose a few through knobs (one frame late, by ADR-1573)
    {"source": "audio.bass", "target": "macros/aBass", "amount": 1.0, "op": "replace"},
    {"source": "audio.treble", "target": "macros/aTreble", "amount": 1.0, "op": "replace"},
    {"source": "audio.onsetStrength", "target": "macros/aOnsetStr", "amount": 0.25, "op": "replace"},
    {"source": "audio.tempoConfidence", "target": "macros/tempoConf", "amount": 1.0, "op": "replace"},
]
project = {
    "format": "avgen-project", "version": 4,
    "assets": {"scene": {"kind": "orb"}, "audio": str(OUT / "mix.wav"),
               "stems": [{"key": "drums", "label": "Drums", "file": str(OUT / "drums.wav"), "hits": True},
                         {"key": "bass", "label": "Bass", "file": str(OUT / "bass.wav")}]},
    "sonic": {}, "sources": sources, "parameters": params, "routes": routes,
}
(OUT / "project.json").write_text(json.dumps(project, indent=1))
print("wrote", OUT)

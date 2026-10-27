#!/usr/bin/env python3
"""The Audio Response Lab's deterministic fixture generator (docs/prototypes/audio-response-lab, ADR-1720).

    python3 tools/audio_lab/make_fixtures.py --out build/release/test-fixtures/audio-lab [--only sparse,isolation]

Writes one directory per fixture:

    <out>/<name>/mix.wav            the stereo mix AV Gen plays (float32, 48 kHz)
    <out>/<name>/<stem>.wav         the stems it was summed from (the isolation fixture only), kept beside the mix
    <out>/<name>/manifest.json      the ground truth: every event's onset SAMPLE index (canonical), its time, source,
                                    kind, register and flags; beats, tempo segments, sections, silent intervals
                                    measured from the produced audio; the seed and the file hashes
<out>/index.json lists them. Nothing is committed but this script: the build runs it into the build tree
(tests/CMakeLists.txt), so no test depends on audio in the repository.

Deterministic: every random draw comes from numpy's PCG64 seeded per fixture, and the WAVs are written by this
script's own writer. Two runs on one machine give byte-identical files (the manifest records each sha256). Across
machines numpy's transcendental functions can differ in the last ulp, so tests assert events and levels, never
hashes from another machine.

Ground truth is what the generator PRODUCED: an event's sample is the first non-zero sample it wrote, and the silent
intervals are measured from the summed mix after it is written. Nothing here is inferred by a detector.

The seven categories (the owner's brief §3): sparse, dense, noisy, syncopated, dynamic, isolation, adversarial.
"""

import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

try:
    import numpy as np
except ImportError:  # the build must not fail for want of numpy: the lab's tests SKIP and say why
    np = None

RATE = 48000
GENERATOR_VERSION = 2

# Registers, as the production code names them (ADR-1604: low 30-250 Hz, mid 250 Hz-4 kHz, high 4-16 kHz).
LOW, MID, HIGH, BROAD = "low", "mid", "high", "broad"
SWELL_S = 0.100  # an event whose attack is this slow or slower is a swell: no onset is owed for it


# ---------------------------------------------------------------------------------------------------------------------
# Instruments. Each returns a mono float64 segment that starts at its attack (sample 0 is the onset) and ends faded.

def _fade_tail(seg, seconds=0.005):
    k = min(len(seg), int(seconds * RATE))
    if k > 0:
        seg[-k:] *= np.linspace(1.0, 0.0, k)
    return seg


def _t(seconds):
    return np.arange(int(round(seconds * RATE))) / RATE


def kick(amp=0.8):
    tt = _t(0.30)
    freq = 50.0 + 100.0 * np.exp(-tt / 0.03)  # 150 -> 50 Hz
    return _fade_tail(amp * np.sin(2 * np.pi * np.cumsum(freq) / RATE) * np.exp(-tt / 0.09))


def snare(rng, amp=0.5):
    tt = _t(0.20)
    noise = rng.uniform(-1.0, 1.0, len(tt))
    body = np.sin(2 * np.pi * 190.0 * tt) * np.exp(-tt / 0.04)
    return _fade_tail(amp * (0.55 * noise * np.exp(-tt / 0.05) + 0.45 * body))


def hat(rng, amp=0.3):
    tt = _t(0.06)
    noise = np.diff(rng.uniform(-1.0, 1.0, len(tt) + 1))  # a first difference: most energy above ~6 kHz
    return _fade_tail(amp * noise * np.exp(-tt / 0.012))


def bass(hz, seconds, amp=0.6, attack=0.010):
    tt = _t(seconds)
    env = np.minimum(1.0, tt / attack)
    seg = (np.sin(2 * np.pi * hz * tt) + 0.4 * np.sin(2 * np.pi * 2 * hz * tt)) * env * amp / 1.4
    return _fade_tail(seg, 0.03)


def pluck(hz, amp=0.4):
    tt = _t(0.8)
    return _fade_tail(amp * np.sin(2 * np.pi * hz * tt) * np.minimum(1.0, tt / 0.002) * np.exp(-tt / 0.18))


def tone(hz, seconds, amp=0.3, attack=0.3, partials=(1.0, 0.3, 0.15), vibrato=0.0):
    tt = _t(seconds)
    env = np.minimum(1.0, tt / attack)
    phase = 2 * np.pi * hz * tt
    if vibrato > 0.0:  # a slow pitch wobble (cents), no amplitude change
        phase = 2 * np.pi * np.cumsum(hz * 2 ** (vibrato / 1200.0 * np.sin(2 * np.pi * 5.0 * tt))) / RATE
    seg = sum(w * np.sin((k + 1) * phase) for k, w in enumerate(partials)) / sum(partials)
    return _fade_tail(amp * env * seg, 0.08)


def band_noise(rng, seconds, lo, hi, amp=0.3, attack=0.002, decay=None):
    """Noise band-limited to [lo, hi] Hz in the frequency domain (no scipy), with an attack and an optional decay."""
    n = int(round(seconds * RATE))
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / RATE)
    spec[(f < lo) | (f > hi)] = 0.0
    x = np.fft.irfft(spec, n)
    x /= max(1e-12, float(np.max(np.abs(x))))
    tt = np.arange(n) / RATE
    env = np.minimum(1.0, tt / max(attack, 1.0 / RATE))
    if decay is not None:
        env *= np.exp(-tt / decay)
    return _fade_tail(amp * x * env, 0.01)


def click(amp=0.6, seconds=0.002):
    tt = _t(seconds)
    return amp * np.exp(-tt / (seconds / 4)) * np.where(np.arange(len(tt)) % 2 == 0, 1.0, -1.0)


def thump(hz=25.0, amp=0.8):
    """A sub-bass 'percussion' below the low register's 30 Hz floor (adversarial: outside the expected band)."""
    tt = _t(0.25)
    return _fade_tail(amp * np.sin(2 * np.pi * hz * tt) * np.minimum(1.0, tt / 0.003) * np.exp(-tt / 0.08))


def tick(hz=14000.0, amp=0.25):
    """A pure high tick above the high register's 16 kHz-ish focus (adversarial)."""
    tt = _t(0.015)
    return _fade_tail(amp * np.sin(2 * np.pi * hz * tt) * np.minimum(1.0, tt / 0.0005) * np.exp(-tt / 0.004), 0.002)


def register_shares(seg, seconds=0.050):
    """The low (30-250 Hz), mid (250 Hz-4 kHz) and high (4-16 kHz) shares of an event's first 50 ms of power: the
    production's own register split (ADR-1604), measured on what was written, not on what the kind is called."""
    x = seg[: int(seconds * RATE)]
    if len(x) < 16:
        return {"low": 0.0, "mid": 0.0, "high": 0.0}
    n = 1 << int(math.ceil(math.log2(max(len(x), RATE // 10))))  # >= 100 ms of bins: 10 Hz resolution
    p = np.abs(np.fft.rfft(x * np.hanning(len(x)), n)) ** 2
    f = np.fft.rfftfreq(n, 1.0 / RATE)
    bands = {"low": (30.0, 250.0), "mid": (250.0, 4000.0), "high": (4000.0, 16000.0)}
    power = {b: float(p[(f >= lo) & (f < hi)].sum()) for b, (lo, hi) in bands.items()}
    total = sum(power.values())
    return {b: round(v / total, 3) if total > 0 else 0.0 for b, v in power.items()}


# ---------------------------------------------------------------------------------------------------------------------

class Fixture:
    """Collects sources (mono tracks) and the events written into them."""

    def __init__(self, name, category, seconds, seed, purpose):
        self.name, self.category, self.seconds, self.seed, self.purpose = name, category, seconds, seed, purpose
        self.rng = np.random.Generator(np.random.PCG64(seed))
        self.n = int(round(seconds * RATE))
        self.tracks = {}       # source -> mono float64
        self.pans = {}         # source -> pan (-1..1)
        self.gains = {}        # source -> mix gain
        self.roles = {}        # source -> production role (ADR-1630)
        self.events = []
        self.beats = None      # {"times": [...], "downbeats": [...], "tempo": [{"start", "bpm"}], ...}
        self.sections = []     # [{"start", "end", "label", ...}]
        self.notes = []        # free text: what is deliberately ambiguous or adversarial
        self.stems = False     # write the sources as stems beside the mix

    def source(self, key, gain=1.0, pan=0.0, role="other"):
        if key not in self.tracks:
            self.tracks[key] = np.zeros(self.n)
            self.gains[key], self.pans[key], self.roles[key] = gain, pan, role
        return self.tracks[key]

    def add(self, key, t, seg, kind, register, onset=True, ambiguous=False, note=None, **extra):
        """Writes `seg` into source `key` at second `t` (rounded to a sample) and records the event."""
        track = self.source(key)
        i = int(round(t * RATE))
        if i >= self.n:
            return
        k = min(len(seg), self.n - i)
        track[i:i + k] += seg[:k]
        nz = np.flatnonzero(np.abs(seg[:k]) > 0.0)
        first = i + int(nz[0]) if len(nz) else i  # the first sample the event actually wrote
        shares = register_shares(seg[:k])
        attack = extra.pop("attackSeconds", None)
        if attack is not None and attack >= SWELL_S:
            ambiguous = True
            note = note or f"a {attack * 1000:.0f} ms swell: no sharp attack"
        e = {"source": key, "kind": kind, "register": register, "sample": first, "time": first / RATE,
             "registerShares": shares, "registers": [b for b, v in shares.items() if v >= 0.10],
             "durationSamples": int(k), "peak": float(np.max(np.abs(seg[:k]))) if k else 0.0,
             "onset": bool(onset), "ambiguous": bool(ambiguous)}
        if note:
            e["note"] = note
        if attack is not None:
            e["attackSeconds"] = attack
        e.update(extra)
        self.events.append(e)

    def grid(self, segments, start_beat_time=0.0, beats_per_bar=4):
        """segments: [(start_second, end_second, bpm)] contiguous; returns the beat times and records them."""
        times, downbeats, tempo = [], [], []
        index = 0
        t = start_beat_time
        for s0, s1, bpm in segments:
            tempo.append({"start": s0, "end": s1, "bpm": bpm})
            period = 60.0 / bpm
            t = max(t, s0)
            while t < s1 - 1e-9:
                ts = round(t * RATE) / RATE
                times.append(ts)
                if index % beats_per_bar == 0:
                    downbeats.append(ts)
                index += 1
                t += period
        self.beats = {"times": times, "downbeats": downbeats, "tempo": tempo, "beatsPerBar": beats_per_bar}
        return times


# ---------------------------------------------------------------------------------------------------------------------
# The seven categories.

def make_sparse():
    fx = Fixture("sparse", "sparse", 30.0, 1001,
                 "onset precision, envelope release, baseline noise and false triggers")
    fx.source("mix")
    fx.sections.append({"start": 0.0, "end": 2.0, "label": "leading silence"})
    for t in (2.0, 3.5, 5.0, 6.5):
        fx.add("mix", t, kick(0.8), "kick", LOW)
    fx.sections.append({"start": 2.0, "end": 7.5, "label": "isolated kicks"})
    for t, d, hz in ((8.0, 0.4, 55.0), (9.5, 1.0, 73.42), (11.5, 0.25, 49.0), (13.0, 0.8, 61.74)):
        fx.add("mix", t, bass(hz, d, 0.7), "bass", LOW, durationSeconds=d, hz=hz)
    fx.sections.append({"start": 8.0, "end": 14.5, "label": "isolated bass notes"})
    fx.add("mix", 15.0, snare(fx.rng, 0.6), "snare", BROAD)
    fx.add("mix", 16.5, hat(fx.rng, 0.45), "hat", HIGH)
    fx.add("mix", 18.0, pluck(1318.5, 0.5), "pluck", MID, hz=1318.5)
    fx.add("mix", 19.5, snare(fx.rng, 0.6), "snare", BROAD)
    fx.sections.append({"start": 15.0, "end": 20.5, "label": "separated percussive events"})
    fx.add("mix", 21.0, tone(440.0, 4.0, 0.35, attack=0.05), "sustain", MID, durationSeconds=4.0, hz=440.0,
           attackSeconds=0.05,
           note="one onset at the start; the held body must not retrigger")
    fx.sections.append({"start": 21.0, "end": 25.0, "label": "sustained note", "sustained": True})
    fx.sections.append({"start": 25.0, "end": 30.0, "label": "trailing silence"})
    return fx


def make_dense():
    fx = Fixture("dense", "dense", 24.0, 1002,
                 "event separation, masking, signal stability and frequency selectivity")
    fx.source("mix", gain=0.6)
    rng = fx.rng
    bpm = 128.0
    beat = 60.0 / bpm
    fx.grid([(0.0, 24.0, bpm)])
    # Selectivity: one register at a time, then everything.
    fx.sections += [{"start": 0.0, "end": 4.0, "label": "low only", "register": LOW},
                    {"start": 4.0, "end": 8.0, "label": "mid only", "register": MID},
                    {"start": 8.0, "end": 12.0, "label": "high only", "register": HIGH},
                    {"start": 12.0, "end": 24.0, "label": "everything", "register": "all"}]
    fx.add("mix", 0.0, bass(55.0, 3.9, 0.55), "bass", LOW, durationSeconds=3.9, hz=55.0)
    t = 0.0
    while t < 4.0 - 1e-9:
        if t > 0.0:
            fx.add("mix", t, kick(0.7), "kick", LOW)
        t += beat
    fx.add("mix", 4.0, tone(523.25, 3.9, 0.25, attack=0.3), "pad", MID, durationSeconds=3.9, attackSeconds=0.3)
    # The beat clock pass (ADR-1782): every section's pattern starts on the fixture's own grid. They started at whole
    # seconds, which are not on a 128 BPM grid, so from 12 s the kicks sat 0.6 of a beat off the manifest's beats and a
    # tracker that followed the kicks scored F 0.
    on_grid = lambda s: math.ceil(s / beat - 1e-9) * beat
    t = on_grid(4.0) + 3 * beat / 4
    while t < 8.0 - 1e-9:
        fx.add("mix", t, pluck(1046.5 if int(t / beat) % 2 else 1568.0, 0.4), "pluck", MID)
        t += 3 * beat / 4
    t = on_grid(8.0)
    while t < 12.0 - 1e-9:
        fx.add("mix", t, hat(rng, 0.35), "hat", HIGH)
        t += beat / 4
    # Everything: 4/4 kick, 8th-note bass overlapping the kicks, backbeat snare, 16th hats, a pad, 3/16 plucks.
    fx.add("mix", 12.0, tone(261.63, 11.9, 0.15, attack=0.5, partials=(1.0, 0.5, 0.3)), "pad", MID,
           durationSeconds=11.9, attackSeconds=0.5)
    k = 0
    t = on_grid(12.0)
    while t < 24.0 - 1e-9:
        if k % 4 == 0:
            fx.add("mix", t, kick(0.65), "kick", LOW)
        if k % 4 in (0, 2):
            fx.add("mix", t, bass(55.0 if (k // 16) % 2 == 0 else 65.41, beat / 2 * 0.9, 0.4), "bass", LOW)
        if k % 8 == 4:
            fx.add("mix", t, snare(rng, 0.45), "snare", BROAD)
        fx.add("mix", t + beat / 4 * 0, hat(rng, 0.2), "hat", HIGH, ambiguous=(k % 2 == 0),
               note="masked by a simultaneous kick/bass/snare attack" if k % 2 == 0 else None)
        fx.add("mix", t + beat / 4, hat(rng, 0.15), "hat", HIGH)
        k += 2
        t += beat / 2
    t = on_grid(12.0) + 3 * beat / 4
    while t < 24.0 - 1e-9:
        fx.add("mix", t, pluck(1318.5, 0.3), "pluck", MID)
        t += 3 * beat / 4
    # Closely spaced pairs (60 ms and 40 ms apart) in the low-only and high-only sections.
    for t0, gap in ((2.20, 0.060), (3.20, 0.040)):
        fx.add("mix", t0, snare(rng, 0.5), "snare", BROAD, pair=gap)
        fx.add("mix", t0 + gap, snare(rng, 0.5), "snare", BROAD, pair=gap, ambiguous=gap < 0.05,
               note=f"second of a pair {gap * 1000:.0f} ms apart")
    fx.notes.append("hats that land on a kick, bass or snare attack are flagged ambiguous (masked)")
    return fx


def make_noisy():
    fx = Fixture("noisy", "noisy", 30.0, 1003, "false triggers and unstable responses at several SNRs")
    fx.source("mix", gain=0.45)  # SNR 0 dB noise peaks near full scale: the whole fixture is scaled, the SNRs kept
    rng = fx.rng
    signal_peak = 0.6
    # A continuous broadband floor whose level steps per section (SNR against the hits' peak).
    # One continuous noise (no step between sections: a step in level is an attack), its gain gliding over 1 s from
    # one section's level to the next, faded in over 2 s from silence and out over 2 s at 25 s.
    plan = [(0.0, 5.0, None, -40.0), (5.0, 10.0, 30.0, None), (10.0, 15.0, 20.0, None), (15.0, 20.0, 10.0, None),
            (20.0, 25.0, 0.0, None)]
    noise = band_noise(rng, 25.0, 30.0, 16000.0, 1.0, attack=0.001)
    noise /= max(float(np.sqrt(np.mean(noise ** 2))), 1e-12)
    gain_db = np.full(fx.n, -120.0)
    levels = []
    for s0, s1, snr, abs_db in plan:
        level_db = abs_db if abs_db is not None else 20 * np.log10(signal_peak) - snr
        levels.append(level_db)
        gain_db[int(s0 * RATE):int(s1 * RATE)] = level_db
        fx.sections.append({"start": s0, "end": s1, "label": "noise only" if snr is None else f"hits at SNR {snr:g} dB",
                            "snrDb": snr, "noiseRmsDbfs": round(float(level_db), 2)})
    for k in range(1, len(plan)):  # 1 s glides centred on each boundary
        b = plan[k][0]
        i0, i1 = int((b - 0.5) * RATE), int((b + 0.5) * RATE)
        gain_db[i0:i1] = np.linspace(levels[k - 1], levels[k], i1 - i0)
    gain = 10 ** (gain_db / 20.0)
    gain[: 2 * RATE] *= np.linspace(0.0, 1.0, 2 * RATE)
    gain[int(23.0 * RATE):int(25.0 * RATE)] *= np.linspace(1.0, 0.0, int(25.0 * RATE) - int(23.0 * RATE))
    fx.tracks["mix"][: len(noise)] += (noise * gain[: len(noise)])
    for s0 in (5.0, 10.0, 15.0, 20.0):
        for j, t in enumerate(np.arange(s0 + 0.5, s0 + 4.6, 0.75)):
            seg = kick(signal_peak) if j % 2 == 0 else snare(rng, signal_peak)
            fx.add("mix", float(t), seg, "kick" if j % 2 == 0 else "snare", LOW if j % 2 == 0 else BROAD,
                   ambiguous=(s0 == 20.0), note="SNR 0 dB" if s0 == 20.0 else None)
    # A sustained noisy texture (a slow swell: no onset expected) with noisy percussion (band-noise bursts) on top.
    swell = band_noise(rng, 5.0, 200.0, 6000.0, 0.25, attack=2.0)
    fx.add("mix", 25.0, swell, "noise-swell", BROAD, onset=False, ambiguous=True, attackSeconds=2.0,
           note="a 2 s noise swell: a texture, not an event")
    for t in (25.6, 26.4, 27.5, 28.3, 29.1):
        fx.add("mix", t, band_noise(rng, 0.12, 2000.0, 9000.0, 0.45, attack=0.001, decay=0.03), "noise-hit", HIGH)
    fx.sections.append({"start": 25.0, "end": 30.0, "label": "noisy texture + noise percussion"})
    fx.notes.append("the noise floor is continuous: false triggers are counted in 0-5 s (noise only)")
    return fx


def make_syncopated():
    fx = Fixture("syncopated", "syncopated", 32.0, 1004,
                 "onset timing and beat/phase tracking without a four-on-the-floor pulse")
    fx.source("mix")
    rng = fx.rng
    bpm = 110.0
    beat = 60.0 / bpm
    bar = 4 * beat
    t0 = 0.5
    fx.grid([(t0, 32.0, bpm)], start_beat_time=t0)
    # Positions within a bar, in 16ths (0..15).
    son_clave = [0, 3, 6, 10, 12]               # 3-2 son clave
    displaced_snare = [5, 13]                   # backbeat pushed a 16th late
    offbeat_hats = [2, 6, 10, 14]               # the "and"s only
    bars = int((32.0 - t0) // bar)
    for b in range(bars):
        start = t0 + b * bar
        section = "clave" if b < 4 else ("displaced" if b < 8 else ("irregular" if b < 12 else "clave"))
        if b in (6, 11):  # unexpected rests: whole bars of silence
            fx.sections.append({"start": start, "end": start + bar, "label": "rest bar"})
            continue
        if section == "clave":
            for p in son_clave:
                fx.add("mix", start + p * beat / 4, kick(0.7) if p in (0, 6) else snare(rng, 0.45),
                       "kick" if p in (0, 6) else "snare", LOW if p in (0, 6) else BROAD, sixteenth=p)
            for p in offbeat_hats:
                fx.add("mix", start + p * beat / 4, hat(rng, 0.25), "hat", HIGH, sixteenth=p,
                       ambiguous=p in son_clave, note="coincides with a clave hit" if p in son_clave else None)
        elif section == "displaced":
            for p in (0, 7, 11):  # kick on 1, then off the grid's strong positions
                fx.add("mix", start + p * beat / 4, kick(0.7), "kick", LOW, sixteenth=p)
            for p in displaced_snare:
                fx.add("mix", start + p * beat / 4, snare(rng, 0.5), "snare", BROAD, sixteenth=p)
            for p in offbeat_hats:
                fx.add("mix", start + p * beat / 4, hat(rng, 0.25), "hat", HIGH, sixteenth=p)
        else:  # irregular: quintuplets over beats 1-2, triplets over 3-4, a pluck accent on the last 16th
            for q in range(5):
                fx.add("mix", start + q * (2 * beat) / 5, pluck(1046.5 if q else 1568.0, 0.45), "pluck", MID,
                       tuplet="5:2")
            for q in range(3):
                fx.add("mix", start + 2 * beat + q * (2 * beat) / 3, kick(0.6) if q == 0 else snare(rng, 0.4),
                       "kick" if q == 0 else "snare", LOW if q == 0 else BROAD, tuplet="3:2")
            fx.add("mix", start + 15 * beat / 4, hat(rng, 0.35), "hat", HIGH, sixteenth=15)
        if b % 2 == 0:  # a bass note pushed off the downbeat
            fx.add("mix", start + 3 * beat / 4, bass(55.0, 0.6, 0.45), "bass", LOW, sixteenth=3)
    fx.sections += [{"start": t0, "end": t0 + 4 * bar, "label": "son clave"},
                    {"start": t0 + 4 * bar, "end": t0 + 8 * bar, "label": "displaced accents"},
                    {"start": t0 + 8 * bar, "end": t0 + 12 * bar, "label": "irregular subdivisions"},
                    {"start": t0 + 12 * bar, "end": 32.0, "label": "son clave (return)"}]
    fx.notes.append("the intended beat grid is 110 BPM from 0.5 s; most beats carry no event (by design)")
    return fx


def make_dynamic():
    fx = Fixture("dynamic", "dynamic", 60.0, 1005,
                 "adaptation, tracker lock, envelope behaviour and stale state across level and tempo changes")
    fx.source("mix")
    rng = fx.rng
    # Tempo: 120 BPM, then a step to 96 BPM (not an octave of 120) at 44 s.
    times = fx.grid([(0.0, 44.0, 120.0), (44.0, 60.0, 96.0)])
    plan = [(0.0, 8.0, "quiet intro", lambda t: -30.0), (8.0, 20.0, "build", lambda t: -30.0 + 30.0 * (t - 8.0) / 12.0),
            (20.0, 28.0, "sudden loud", lambda t: 0.0), (28.0, 34.0, "drop to silence", None),
            (34.0, 44.0, "recovery", lambda t: -6.0), (44.0, 60.0, "tempo change 120 -> 96", lambda t: -6.0)]
    for s0, s1, label, gain in plan:
        fx.sections.append({"start": s0, "end": s1, "label": label})
    k = 0
    for t in times:
        g = None
        for s0, s1, _, gain in plan:
            if s0 <= t < s1:
                g = gain
        if g is None:
            continue
        a = 10 ** (g(t) / 20.0)
        pos = k % 4
        fx.add("mix", t, kick(0.7 * a), "kick", LOW, gainDb=round(g(t), 2))
        if pos in (1, 3):
            fx.add("mix", t + 0.004, snare(rng, 0.4 * a), "snare", BROAD, gainDb=round(g(t), 2), ambiguous=True,
                   note="4 ms after its kick: one attack")
        k += 1
    fx.sections.append({"start": 28.0, "end": 34.0, "label": "silence", "silent": True})
    fx.notes.append("snares sit 4 ms after a kick on beats 2 and 4: one attack, flagged ambiguous")
    return fx


def make_isolation():
    fx = Fixture("isolation", "isolation", 24.0, 1006,
                 "source attribution, leakage between sources, false activity in a silent stem, mix-only behaviour")
    fx.stems = True
    rng = fx.rng
    fx.source("drums", gain=0.5, pan=0.0, role="drums")
    fx.source("bass", gain=0.5, pan=0.0, role="bass")
    fx.source("synth", gain=0.45, pan=0.2, role="synth")
    fx.source("pad", gain=0.4, pan=-0.2, role="other")  # silent throughout: a configured source that never plays
    bpm = 120.0
    beat = 0.5
    fx.grid([(0.5, 24.0, bpm)], start_beat_time=0.5)
    # Who plays when: drums alone, bass alone, synth alone, then everything overlapping.
    fx.sections += [{"start": 0.5, "end": 4.0, "label": "drums alone (snare stacked on kicks)", "active": ["drums"]},
                    {"start": 4.0, "end": 8.0, "label": "bass alone", "active": ["bass"]},
                    {"start": 8.0, "end": 12.0, "label": "synth alone", "active": ["synth"]},
                    {"start": 12.0, "end": 22.0, "label": "all overlapping", "active": ["drums", "bass", "synth"]},
                    {"start": 22.0, "end": 24.0, "label": "silence", "active": []}]
    for b in range(1, 48):  # from 0.5 s: sample 0 is not a musical case (no detector has a hop of history there)
        t = b * beat
        if t < 4.0:  # drums alone: kick every beat, a snare STACKED on the odd ones
            fx.add("drums", t, kick(0.8), "kick", LOW)
            if b % 2 == 1:
                fx.add("drums", t, snare(rng, 0.55), "snare", BROAD, ambiguous=True,
                       note="snare on top of the kick (one attack)")
            fx.add("drums", t + beat / 2, hat(rng, 0.35), "hat", HIGH)
        elif 12.0 <= t < 22.0:  # with everything: kick on even beats, the snare ALONE on odd ones
            if b % 2 == 0:
                fx.add("drums", t, kick(0.8), "kick", LOW)
            else:
                fx.add("drums", t, snare(rng, 0.55), "snare", BROAD)
            fx.add("drums", t + beat / 2, hat(rng, 0.35), "hat", HIGH)
    for t, d, hz in ((4.0, 0.9, 55.0), (5.0, 0.4, 73.42), (5.75, 1.0, 49.0), (7.0, 0.5, 61.74),
                     (12.0, 0.45, 55.0), (13.0, 0.45, 55.0), (14.25, 0.7, 65.41), (16.0, 0.45, 49.0),
                     (17.0, 0.95, 61.74), (18.5, 0.45, 55.0), (20.0, 0.9, 73.42)):
        fx.add("bass", t, bass(hz, d, 0.75), "bass", LOW, durationSeconds=d, hz=hz,
               overlaps="kick" if 12.0 <= t < 22.0 and abs(t / beat - round(t / beat)) < 1e-6 else None)
    for t, hz in ((8.0, 1046.5), (8.75, 1318.5), (9.5, 1568.0), (10.25, 1174.7), (11.0, 1396.9),
                  (12.25, 1046.5), (13.25, 1568.0), (14.75, 1318.5), (16.25, 1760.0), (17.75, 1046.5),
                  (19.25, 1318.5), (20.75, 1568.0)):
        fx.add("synth", t, pluck(hz, 0.5), "pluck", MID, hz=hz,
               overlaps="hat" if 12.0 <= t < 22.0 and abs((t - 0.25) / beat - round((t - 0.25) / beat)) < 1e-6 else None)
    fx.add("synth", 10.0, tone(659.25, 1.8, 0.25, attack=0.3), "sustain", MID, durationSeconds=1.8,
           attackSeconds=0.3)
    fx.notes.append("pad is configured and silent throughout: any activity on it is false")
    return fx


def make_adversarial():
    fx = Fixture("adversarial", "adversarial", 30.0, 1007,
                 "cases that defeat simplistic detectors or cause unstable responses")
    fx.source("mix")
    rng = fx.rng
    fx.add("mix", 0.5, tone(440.0, 4.5, 0.4, attack=0.02, partials=(1.0,)), "sustain", MID, durationSeconds=4.5,
           attackSeconds=0.02,
           note="a pure sustained tone: one onset, then nothing")
    fx.sections.append({"start": 0.5, "end": 5.0, "label": "sustained pure tone", "sustained": True})
    fx.add("mix", 5.5, bass(41.2, 4.5, 0.7, attack=0.04), "bass-drone", LOW, durationSeconds=4.5, attackSeconds=0.04,
           note="bass without percussion: one onset, then a held note")
    fx.sections.append({"start": 5.5, "end": 10.0, "label": "bass drone, no percussion", "sustained": True})
    for t in (10.5, 11.5, 12.5):
        fx.add("mix", t, thump(25.0, 0.8), "sub-thump", LOW, ambiguous=True, note="25 Hz: below the low register")
    for t in (11.0, 12.0, 13.0, 13.5):
        fx.add("mix", t, tick(14000.0, 0.3), "high-tick", HIGH, ambiguous=True, note="a 14 kHz tone tick")
    fx.sections.append({"start": 10.0, "end": 14.0, "label": "percussion outside the expected bands"})
    for t, gap in ((15.0, 0.015), (16.0, 0.030), (17.0, 0.050), (18.0, 0.080)):
        fx.add("mix", t, click(0.6), "click", BROAD, pair=gap)
        fx.add("mix", t + gap, click(0.6), "click", BROAD, pair=gap, ambiguous=gap < 0.05,
               note=f"second click {gap * 1000:.0f} ms after the first")
    fx.sections.append({"start": 15.0, "end": 19.0, "label": "very close events"})
    for j in range(30):  # a 30 Hz buzz roll for one second
        fx.add("mix", 20.0 + j / 30.0, snare(rng, 0.35), "roll", BROAD, ambiguous=True,
               note="repeated transients 33 ms apart")
    fx.sections.append({"start": 20.0, "end": 21.0, "label": "repeated transients (roll)"})
    low = band_noise(rng, 7.0, 30.0, 16000.0, 1.0, attack=0.5)
    low *= 10 ** (-60.0 / 20.0) / max(float(np.sqrt(np.mean(low ** 2))), 1e-12)
    fx.add("mix", 23.0, low, "low-noise", BROAD, onset=False, note="-60 dBFS RMS broadband noise: nothing should fire")
    fx.sections.append({"start": 23.0, "end": 30.0, "label": "low-level noise (-60 dBFS)", "noiseRmsDbfs": -60.0})
    return fx


CATEGORIES = {
    "sparse": make_sparse, "dense": make_dense, "noisy": make_noisy, "syncopated": make_syncopated,
    "dynamic": make_dynamic, "isolation": make_isolation, "adversarial": make_adversarial,
}


# ---------------------------------------------------------------------------------------------------------------------

def wav_bytes(samples):
    """samples: (frames, channels) float in [-1, 1] -> a float32 WAV."""
    data = np.asarray(samples, dtype=np.float64)
    if data.ndim == 1:
        data = data[:, None]
    payload = data.astype("<f4").tobytes()
    channels = data.shape[1]
    block = channels * 4
    fmt_chunk = struct.pack("<HHIIHH", 3, channels, RATE, RATE * block, block, 32)
    chunks = b"fmt " + struct.pack("<I", len(fmt_chunk)) + fmt_chunk
    chunks += b"fact" + struct.pack("<II", 4, data.shape[0])
    chunks += b"data" + struct.pack("<I", len(payload)) + payload
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


def silent_intervals(mix, min_seconds=0.25, floor_db=-80.0, window=0.010):
    """Where the produced mix is below `floor_db` RMS for at least `min_seconds`, measured in 10 ms windows."""
    mono = np.max(np.abs(mix), axis=1)
    w = int(window * RATE)
    blocks = len(mono) // w
    rms = np.sqrt(np.mean(mono[: blocks * w].reshape(blocks, w) ** 2, axis=1))
    quiet = 20 * np.log10(np.maximum(rms, 1e-12)) < floor_db
    out, start = [], None
    for b, q in enumerate(list(quiet) + [False]):
        if q and start is None:
            start = b
        elif not q and start is not None:
            if (b - start) * window >= min_seconds:
                out.append([round(start * window, 4), round(b * window, 4)])
            start = None
    return out


def pan_gains(pan):
    angle = (pan + 1.0) * np.pi / 4.0
    return float(np.cos(angle)), float(np.sin(angle))


def write_fixture(fx, out):
    d = out / fx.name
    d.mkdir(parents=True, exist_ok=True)
    mix = np.zeros((fx.n, 2))
    for key, mono in fx.tracks.items():
        gl, gr = pan_gains(fx.pans[key])
        mix[:, 0] += fx.gains[key] * gl * mono
        mix[:, 1] += fx.gains[key] * gr * mono
    peak = float(np.max(np.abs(mix)))
    if peak > 0.98:
        sys.exit(f"make_fixtures: {fx.name}: the mix clips (peak {peak:.3f})")
    files = {}

    def write(name, samples):
        blob = wav_bytes(samples)
        (d / name).write_bytes(blob)
        files[name] = {"rate": RATE, "channels": 1 if np.ndim(samples) == 1 else samples.shape[1],
                       "frames": int(samples.shape[0]), "format": "f32", "sha256": hashlib.sha256(blob).hexdigest()}

    write("mix.wav", mix)
    sources = {}
    for key, mono in fx.tracks.items():
        entry = {"role": fx.roles[key], "gain": fx.gains[key], "pan": fx.pans[key],
                 "peak": round(float(np.max(np.abs(mono))), 6),
                 "silent": silent_intervals(np.stack([mono, mono], axis=1))}
        if fx.stems:
            write(f"{key}.wav", mono)
            entry["file"] = f"{key}.wav"
        sources[key] = entry
    events = sorted(fx.events, key=lambda e: (e["sample"], e["source"]))
    for i, e in enumerate(events):
        e["id"] = i
    manifest = {
        "format": "avgen-audio-lab-fixture", "version": 1, "generator": "tools/audio_lab/make_fixtures.py",
        "generatorVersion": GENERATOR_VERSION, "name": fx.name, "category": fx.category, "purpose": fx.purpose,
        "seed": fx.seed, "rate": RATE, "channels": 2, "channelLayout": "stereo (L, R)",
        "durationSeconds": fx.seconds, "frames": fx.n, "mixPeak": round(peak, 6),
        "panLaw": "constant power, angle = (pan + 1) * pi / 4",
        "timing": "event 'sample' is canonical: the first non-zero sample the event wrote into the mix's timeline; "
                  "'time' = sample / rate",
        "sources": sources, "stems": fx.stems, "events": events, "beats": fx.beats, "sections": fx.sections,
        "silent": silent_intervals(mix), "notes": fx.notes, "files": files,
    }
    (d / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    return manifest


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--only", default="", help="comma-separated categories (default: all seven)")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if np is None:
        (args.out / "index.json").write_text(json.dumps({"unavailable": f"numpy is not installed for {sys.executable}"}) + "\n")
        print(f"make_fixtures: numpy missing for {sys.executable}; wrote an 'unavailable' marker")
        return
    names = [n for n in args.only.split(",") if n] or list(CATEGORIES)
    for n in names:
        if n not in CATEGORIES:
            sys.exit(f"make_fixtures: unknown category '{n}' ({', '.join(CATEGORIES)})")
    index = {"format": "avgen-audio-lab-index", "generatorVersion": GENERATOR_VERSION, "fixtures": []}
    for n in names:
        m = write_fixture(CATEGORIES[n](), args.out)
        index["fixtures"].append({"name": m["name"], "category": m["category"], "dir": m["name"],
                                  "events": len(m["events"]), "durationSeconds": m["durationSeconds"],
                                  "sha256": m["files"]["mix.wav"]["sha256"]})
        print(f"make_fixtures: {m['name']}: {len(m['events'])} events, {m['durationSeconds']:g} s, "
              f"peak {m['mixPeak']:.3f}")
    (args.out / "index.json").write_text(json.dumps(index, indent=1) + "\n")


if __name__ == "__main__":
    main()

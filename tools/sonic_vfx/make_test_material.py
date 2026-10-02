#!/usr/bin/env python3
"""Sonic VFX test material: one short performance per input class of the brief's §20 test matrix.

    python3 tools/sonic_vfx/make_test_material.py            # all classes
    python3 tools/sonic_vfx/make_test_material.py edrums      # one

Every sample is computed here; there are no recordings and no third-party material, and the fixed seeds make it
deterministic. The classes:

  pads       slow sustained chords on a warm pad                         (sustained, harmonic)
  chords     rhythmic chord stabs on bright keys                         (harmony, polyphony, rhythm)
  bass       an 8th-note bass line, driven                               (bass energy, low attacks)
  lead       a monophonic saw lead: legato phrases and a fast run        (melody, pitch, intervals)
  arp        16th-note arpeggios over two octaves on a pluck             (rapid melodic pattern)
  edrums     electronic drums one at a time: kick, then snare, then hats, then all three
  drumloop   a full electronic drum loop with claps, open hats and a tom fill
  dense      dense MIDI: 32nd-note bursts and wide chords                (note density, polyphony)
  sparse     a few bell notes with long silences between                 (sparse, sustained tails)
  velocity   one motif six times, velocity 20 -> 127                     (velocity alone)
  rapid      rapid repeated notes and trills                             (rapid sequences)
  sustained  very long held notes and a held chord                       (slow sustained)
  full       pad, bass, drums and lead together                          (a full mix)

Writes assets/audio/sonic-vfx-<class>.wav (gitignored; regenerate) and examples/sonic-vfx/test/<class>.mid (the
notes each class played, tracked). Drum notes are written to the MIDI file on channel 10 only in `edrums` and
`drumloop` (`<class>-drums.mid`); a scene's drum response comes from the audio, as it would from a kit's output.
"""
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools"))
import make_sonic_material as msm  # noqa: E402  (adsr, additive, voices, normalise, write_wav, vlq)

RATE = msm.RATE
BPM = 120.0
BEAT = 60.0 / BPM
AUDIO_DIR = os.path.join(REPO, "assets", "audio")
MIDI_DIR = os.path.join(REPO, "examples", "sonic-vfx", "test")


# ---- voices --------------------------------------------------------------------------------------------------------
def voice_keys(f0, count, hold, vel, seed):
    """Bright electric keys: a few harmonics with a percussive decay and a velocity-dependent brightness."""
    ks = np.arange(1, 24)
    amps = (1.0 / ks ** 1.2) * np.exp(-ks * f0 / (2200.0 + 2600.0 * vel))
    sig = msm.additive(f0, count, amps, 0.0, seed) + 0.5 * msm.additive(f0 * 2.0, count, amps[:8] * 0.4, 3.0, seed + 1)
    return sig * msm.adsr(count, 0.003, 0.5, 0.35, 0.25, hold) * (0.25 + 0.75 * vel)


def voice_pluck(f0, count, hold, vel, seed):
    t = np.arange(count) / RATE
    ks = np.arange(1, 30)
    amps = 1.0 / ks
    sig = np.zeros(count)
    rng = np.random.RandomState(seed)
    for k, a in zip(ks, amps):
        f = f0 * k
        if f > RATE * 0.45:
            break
        sig += a * np.exp(-t * (2.5 + 0.9 * k)) * np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28))
    return sig * msm.adsr(count, 0.001, 2.0, 1.0, 0.08, hold) * (0.3 + 0.7 * vel)


def voice_lead(f0, count, hold, vel, seed):
    """A detuned saw lead with a little vibrato after the attack."""
    t = np.arange(count) / RATE
    vib = 1.0 + 0.004 * np.sin(2 * np.pi * 5.5 * t) * np.clip((t - 0.25) / 0.3, 0.0, 1.0)
    ks = np.arange(1, 36)
    amps = (1.0 / ks) * np.exp(-ks * f0 / 5200.0)
    sig = np.zeros(count)
    rng = np.random.RandomState(seed)
    phase = np.cumsum(vib) / RATE
    for det in (-5.0, 5.0):
        r = 2.0 ** (det / 1200.0)
        for k, a in zip(ks, amps):
            if f0 * k > RATE * 0.45:
                break
            sig += a * np.sin(2 * np.pi * f0 * k * r * phase + rng.uniform(0, 6.28))
    return sig * msm.adsr(count, 0.012, 0.3, 0.8, 0.18, hold) * (0.35 + 0.65 * vel) * 0.6


def voice_bass(f0, count, hold, vel, seed):
    t = np.arange(count) / RATE
    ks = np.arange(1, 40)
    saw = msm.additive(f0, count, (1.0 / ks) * np.exp(-ks * f0 / 1400.0), 0.0, seed)
    sub = np.sin(2 * np.pi * f0 * 0.5 * t)
    env = msm.adsr(count, 0.004, 0.18, 0.75, 0.08, hold)
    return np.tanh(2.4 * (saw + 0.9 * sub) * env) * (0.4 + 0.6 * vel)


VOICES = {"pad": msm.voice_pad, "bell": msm.voice_bell, "keys": voice_keys, "pluck": voice_pluck,
          "lead": voice_lead, "bass": voice_bass}
TAILS = {"pad": 3.5, "bell": 3.0, "keys": 1.0, "pluck": 1.2, "lead": 0.6, "bass": 0.4}


# ---- drums ----------------------------------------------------------------------------------------------------------
def shaped_noise(count, lo, hi, seed):
    rng = np.random.RandomState(seed)
    noise = rng.uniform(-1.0, 1.0, count)
    spec = np.fft.rfft(noise)
    f = np.fft.rfftfreq(count, 1.0 / RATE)
    band = np.clip((f - lo) / (lo * 0.4), 0.0, 1.0) * np.clip((hi - f) / (hi * 0.25) + 1.0, 0.0, 1.0)
    out = np.fft.irfft(spec * band, count)
    return out / max(np.max(np.abs(out)), 1e-9)


def drum(kind, vel, seed):
    if kind == "kick":
        n = int(0.5 * RATE)
        t = np.arange(n) / RATE
        f = 46.0 + 110.0 * np.exp(-t / 0.035)
        body = np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t / 0.32)
        click = shaped_noise(n, 2000.0, 9000.0, seed) * np.exp(-t / 0.004) * 0.35
        return np.tanh(1.6 * (body + click)) * (0.4 + 0.6 * vel)
    if kind == "snare":
        n = int(0.35 * RATE)
        t = np.arange(n) / RATE
        tone = (np.sin(2 * np.pi * 185.0 * t) + 0.6 * np.sin(2 * np.pi * 330.0 * t)) * np.exp(-t / 0.07)
        noise = shaped_noise(n, 1500.0, 9000.0, seed) * np.exp(-t / 0.13)
        return (0.55 * tone + 0.9 * noise) * np.minimum(1.0, t / 0.0005) * (0.35 + 0.65 * vel)
    if kind == "clap":
        n = int(0.4 * RATE)
        t = np.arange(n) / RATE
        noise = shaped_noise(n, 900.0, 4500.0, seed)
        env = sum(np.where(t >= o, np.exp(-(t - o) / 0.008), 0.0) for o in (0.0, 0.011, 0.022))
        env = env + np.where(t >= 0.03, 0.6 * np.exp(-(t - 0.03) / 0.12), 0.0)
        return noise * env * 0.7 * (0.35 + 0.65 * vel)
    if kind in ("hat", "openhat"):
        decay = 0.045 if kind == "hat" else 0.32
        n = int((decay * 5.0 + 0.02) * RATE)
        t = np.arange(n) / RATE
        noise = shaped_noise(n, 7000.0, 18000.0, seed)
        return noise * np.exp(-t / decay) * np.minimum(1.0, t / 0.0003) * 0.55 * (0.3 + 0.7 * vel)
    if kind in ("tomhi", "tomlo"):
        n = int(0.6 * RATE)
        t = np.arange(n) / RATE
        f0 = 150.0 if kind == "tomhi" else 98.0
        f = f0 * (1.0 + 0.6 * np.exp(-t / 0.05))
        return np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t / 0.25) * (0.4 + 0.6 * vel)
    raise ValueError(kind)


GM = {"kick": 36, "snare": 38, "clap": 39, "hat": 42, "openhat": 46, "tomlo": 45, "tomhi": 48}


# ---- the performances -----------------------------------------------------------------------------------------------
# A note is (start_beats, length_beats, midi, velocity, voice). A hit is (start_beats, kind, velocity).

def chord(start, length, pitches, vel, voice):
    return [(start, length, p, vel, voice) for p in pitches]


def perf_pads():
    n = []
    for i, ch in enumerate(((45, 57, 60, 64, 71), (41, 53, 57, 60, 64), (40, 52, 55, 60, 67), (43, 55, 59, 62, 64))):
        n += chord(i * 8.0, 7.6, ch, 66 + 6 * (i % 2), "pad")
    return n, [], 32.0


def perf_chords():
    n = []
    prog = ((57, 60, 64, 67), (53, 57, 60, 64), (55, 59, 62, 67), (52, 55, 59, 64))
    rhythm = (0.0, 1.0, 1.5, 2.5, 3.0)
    for bar in range(6):
        ch = prog[bar % 4]
        for r in rhythm:
            n += chord(bar * 4.0 + r, 0.4, ch, 78 + 20 * (r == 0.0), "keys")
    return n, [], 24.0


def perf_bass():
    n = []
    line = (28, 28, 40, 28, 31, 28, 43, 31, 26, 26, 38, 26, 33, 31, 28, 26)
    for bar in range(6):
        for i in range(8):
            p = line[(bar % 2) * 8 + i]
            n.append((bar * 4.0 + i * 0.5, 0.42, p, 92 if i % 2 == 0 else 72, "bass"))
    return n, [], 24.0


def perf_lead():
    n = []
    phrase = [(0.0, 1.5, 69), (1.5, 0.5, 71), (2.0, 1.0, 72), (3.0, 1.0, 76), (4.0, 2.0, 74), (6.0, 1.0, 72),
              (7.0, 1.0, 71), (8.0, 3.0, 69), (12.0, 0.5, 76), (12.5, 0.5, 77), (13.0, 0.5, 79), (13.5, 0.5, 81),
              (14.0, 0.25, 83), (14.25, 0.25, 84), (14.5, 0.25, 86), (14.75, 0.25, 88), (15.0, 3.0, 84),
              (18.0, 1.0, 81), (19.0, 1.0, 79), (20.0, 4.0, 76)]
    for b, length, p in phrase:
        n.append((b, length * 1.02, p, 96, "lead"))
    return n, [], 25.0


def perf_arp():
    n = []
    up = [57, 60, 64, 69, 72, 76, 81, 84]
    pattern = up + up[::-1][1:-1]
    for i in range(int(22 * 4)):
        p = pattern[i % len(pattern)] + (0 if (i // 28) % 2 == 0 else -4)
        n.append((i * 0.25, 0.2, p, 70 + int(30 * ((i % 4) == 0)), "pluck"))
    return n, [], 22.0


def perf_edrums():
    hits = []
    for b in range(8):                       # bars 1-2: kick alone, quarters
        hits.append((b * 1.0, "kick", 110))
    for b in range(4):                       # bars 3-4: snare alone, backbeat
        hits.append((8.0 + b * 2.0 + 1.0, "snare", 105))
    for e in range(16):                      # bars 5-6: hats alone, 8ths
        hits.append((16.0 + e * 0.5, "hat", 95 if e % 2 == 0 else 70))
    for e in range(16):                      # bars 7-8: all three
        t = 24.0 + e * 0.5
        hits.append((t, "hat", 90 if e % 2 == 0 else 64))
        if e % 2 == 0:
            hits.append((t, "kick", 112))
        if e % 4 == 2:
            hits.append((t, "snare", 108))
    return [], hits, 33.0


def perf_drumloop():
    hits = []
    for bar in range(8):
        base = bar * 4.0
        for q in range(4):
            hits.append((base + q, "kick", 115))
        for q in (1, 3):
            hits.append((base + q, "snare" if bar % 2 == 0 else "clap", 105))
        for s16 in range(16):
            if bar == 7 and s16 >= 8:
                continue
            kind = "openhat" if s16 % 8 == 6 else "hat"
            hits.append((base + s16 * 0.25, kind, 100 if s16 % 4 == 0 else 60 + 10 * (s16 % 2)))
        if bar == 7:  # a tom fill
            for k, (o, kind) in enumerate(((2.0, "tomhi"), (2.5, "tomhi"), (3.0, "tomlo"), (3.5, "tomlo"))):
                hits.append((base + o, kind, 100))
    return [], hits, 33.0


def perf_dense():
    n = []
    for burst in range(6):
        base = burst * 3.0
        for i in range(24):
            n.append((base + i * 0.0625, 0.06, 60 + (i * 5) % 24, 70 + (i % 3) * 15, "pluck"))
        n += chord(base + 1.75, 1.0, (48, 55, 60, 64, 67, 71, 74, 79), 90, "keys")
    return n, [], 19.0


def perf_sparse():
    notes = [(0.0, 3.0, 64), (7.0, 4.0, 57), (13.0, 2.0, 71), (19.0, 5.0, 52)]
    return [(b, length, p, 84, "bell") for b, length, p in notes], [], 28.0


def perf_velocity():
    n = []
    motif = (60, 64, 67, 72)
    for k, v in enumerate((20, 40, 60, 80, 100, 127)):
        for i, p in enumerate(motif):
            n.append((k * 4.0 + i * 0.75, 0.6, p, v, "keys"))
    return n, [], 25.0


def perf_rapid():
    n = []
    for i in range(32):                      # repeated 16ths on one note
        n.append((i * 0.25, 0.18, 72, 90, "lead"))
    for i in range(48):                      # a trill
        n.append((8.0 + i * 0.125, 0.11, 76 if i % 2 == 0 else 77, 88, "lead"))
    return n, [], 15.0


def perf_sustained():
    n = [(0.0, 12.0, 45, 70, "pad"), (12.0, 12.0, 52, 70, "pad")]
    n += chord(24.0, 12.0, (45, 52, 57, 60, 64), 72, "pad")
    return n, [], 38.0


def perf_full():
    pads, _, _ = perf_pads()
    bass, _, _ = perf_bass()
    lead, _, _ = perf_lead()
    _, hits, _ = perf_drumloop()
    lead = [(b + 4.0, length, p, v, voice) for b, length, p, v, voice in lead if b + 4.0 < 30.0]
    return [x for x in pads if x[0] < 30.0] + bass + lead, hits, 32.0


PERFORMANCES = {"pads": perf_pads, "chords": perf_chords, "bass": perf_bass, "lead": perf_lead, "arp": perf_arp,
                "edrums": perf_edrums, "drumloop": perf_drumloop, "dense": perf_dense, "sparse": perf_sparse,
                "velocity": perf_velocity, "rapid": perf_rapid, "sustained": perf_sustained, "full": perf_full}


# ---- rendering ------------------------------------------------------------------------------------------------------
def render(notes, hits, beats):
    count = int((beats * BEAT + 2.0) * RATE)
    left = np.zeros(count)
    right = np.zeros(count)
    for i, (sb, lb, p, v, voice) in enumerate(notes):
        start = int(round(sb * BEAT * RATE))
        hold = int(round(lb * BEAT * RATE))
        n = min(hold + int(TAILS[voice] * RATE), count - start)
        if n <= 0:
            continue
        sig = VOICES[voice](msm.midi_hz(p), n, hold, v / 127.0, 1000 + i)
        pan = 0.5 + 0.2 * math.sin(p * 1.3)
        gain = 0.55 if voice == "bass" else 0.5
        left[start:start + n] += gain * sig * math.cos(pan * math.pi / 2) * math.sqrt(2)
        right[start:start + n] += gain * sig * math.sin(pan * math.pi / 2) * math.sqrt(2)
    for i, (sb, kind, v) in enumerate(hits):
        start = int(round(sb * BEAT * RATE))
        sig = drum(kind, v / 127.0, 5000 + i)
        n = min(sig.size, count - start)
        pan = {"hat": 0.6, "openhat": 0.62, "tomhi": 0.42, "tomlo": 0.58}.get(kind, 0.5)
        left[start:start + n] += 0.8 * sig[:n] * math.cos(pan * math.pi / 2) * math.sqrt(2)
        right[start:start + n] += 0.8 * sig[:n] * math.sin(pan * math.pi / 2) * math.sqrt(2)
    return msm.normalise(left, right, target_rms_db=-17.0, peak_db=-1.0)


def write_midi(path, events, ppq=480):
    """A type-0 file at BPM. `events` are (start_beats, length_beats, midi, velocity, channel 0-15)."""
    import struct
    evs = []
    for sb, lb, p, v, ch in events:
        evs.append((int(round(sb * ppq)), 1, p, v, ch))
        evs.append((int(round((sb + lb) * ppq)), 0, p, 0, ch))
    evs.sort(key=lambda e: (e[0], e[1], e[2]))
    track = bytearray()
    track += msm.vlq(0) + b"\xff\x51\x03" + int(round(60e6 / BPM)).to_bytes(3, "big")
    track += msm.vlq(0) + b"\xff\x58\x04\x04\x02\x18\x08"
    last = 0
    for tick, on, p, v, ch in evs:
        track += msm.vlq(tick - last)
        last = tick
        track += bytes([(0x90 if on else 0x80) | ch, p, v if on else 64])
    track += msm.vlq(0) + b"\xff\x2f\x00"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, ppq))
        f.write(b"MTrk" + struct.pack(">I", len(track)) + bytes(track))


def main():
    names = sys.argv[1:] or list(PERFORMANCES)
    for name in names:
        notes, hits, beats = PERFORMANCES[name]()
        left, right = render(notes, hits, beats)
        digest = msm.write_wav(os.path.join(AUDIO_DIR, "sonic-vfx-%s.wav" % name), left, right)
        write_midi(os.path.join(MIDI_DIR, "%s.mid" % name), [(sb, lb, p, v, 0) for sb, lb, p, v, _ in notes])
        if hits:
            write_midi(os.path.join(MIDI_DIR, "%s-drums.mid" % name),
                       [(sb, 0.1, GM[kind], v, 9) for sb, kind, v in hits])
        print("%-10s %5.1f s  %3d notes %3d hits  sha %s" % (name, left.size / RATE, len(notes), len(hits),
                                                              digest[:12]))


if __name__ == "__main__":
    main()

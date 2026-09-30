#!/usr/bin/env python3
"""Sonic Garden test material (docs/prototypes/sonic-garden, brief §17 and §34-36).

One MIDI sequence rendered through four contrasting synthesized sounds, plus the §35 context sequences and the
§36 timbre morph. Every sample is computed here from the notes below: there are no samples, no third-party
material and no randomness beyond fixed seeds, so the output is deterministic.

    python3 tools/make_sonic_material.py            # writes everything to the default places

Writes:
  assets/audio/sonic-{pad,bell,bass,perc}.wav    the §34 phrase through each sound (gitignored; regenerate)
  assets/audio/sonic-context-{pad,bell}.wav       §35: sustained, then arpeggio, then dense chords
  assets/audio/sonic-morph.wav                    §36: one phrase looped; clean -> bright -> resonant -> distorted -> noisy
  examples/sonic-garden/notes/{phrase,context,morph}.mid   the note events each file played (tracked)

Stereo 16-bit 48 kHz. Needs numpy.
"""
import argparse
import hashlib
import json
import math
import os
import struct
import wave

import numpy as np

RATE = 48000
BPM = 96.0
BEAT = 60.0 / BPM
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# ---- the note sequences ------------------------------------------------------------------------------------------
# A note is (start_beats, length_beats, midi_note, velocity 1..127).

def phrase_notes():
    """The §34 phrase: 8 bars at 96 BPM (20 s). It covers every musical behaviour the context layer reads:
    a sustained chord, a rising and falling arpeggio, a melody with repeated notes over held chords, and a burst
    of dense stabs, with velocity varying throughout."""
    n = []
    # bars 1-2: a sustained A minor 9 chord, soft, then a held G/B
    for p in (45, 57, 60, 64, 71):
        n.append((0.0, 4.0, p, 70))
    for p in (47, 55, 59, 62, 69):
        n.append((4.0, 4.0, p, 64))
    # bars 3-4: sixteenth-note arpeggio, up then down, velocity swelling
    up = [57, 60, 64, 67, 69, 72, 76, 79]
    arp = up + up[::-1]
    for i in range(32):
        p = arp[i % 16]
        vel = 60 + int(40 * (i / 31.0))
        n.append((8.0 + i * 0.25, 0.22, p, vel))
    # bars 5-6: melody (repeated notes and steps) over a held F major 7 chord
    for p in (41, 53, 57, 60, 64):
        n.append((16.0, 8.0, p, 58))
    melody = [(0.0, 1.0, 72), (1.0, 0.5, 72), (1.5, 0.5, 74), (2.0, 1.5, 76), (3.5, 0.5, 74),
              (4.0, 1.0, 72), (5.0, 0.5, 69), (5.5, 0.5, 69), (6.0, 2.0, 67)]
    for b, length, p in melody:
        n.append((16.0 + b, length * 0.95, p, 90))
    # bars 7-8: dense stabs, eighth-note chords, loud, then a final held chord
    stabs = [(57, 60, 64, 69), (55, 59, 62, 67), (53, 57, 60, 65), (52, 56, 59, 64)]
    for i in range(12):
        chord = stabs[(i // 3) % 4]
        for p in chord:
            n.append((24.0 + i * 0.5, 0.3, p, 100 + (i % 3) * 9))
    for p in (45, 52, 57, 60, 64):
        n.append((30.0, 2.0, p, 96))
    return n, 32.0


def context_notes():
    """§35: one sound, three musical contexts of 4 bars each (10 s each at 96 BPM, 30 s in all)."""
    n = []
    # A: slow sustained notes, one at a time, overlapping
    for i, p in enumerate((57, 64, 60, 67, 62, 69, 64, 60)):
        n.append((i * 2.0, 3.0, p, 72))
    # B: a rapid arpeggio in 16ths (same pitch material)
    pattern = [57, 60, 64, 69, 72, 69, 64, 60]
    for i in range(64):
        n.append((16.0 + i * 0.25, 0.2, pattern[i % 8], 80))
    # C: a dense chord sequence, a new 5-note chord every beat
    chords = [(45, 57, 60, 64, 67), (43, 55, 59, 62, 65), (41, 53, 57, 60, 64), (40, 52, 56, 59, 62)]
    for i in range(16):
        for p in chords[i % 4]:
            n.append((32.0 + i, 0.95, p, 84))
    return n, 48.0


def morph_notes():
    """§36: a two-bar phrase looped 6 times (30 s); the sound changes, the notes do not."""
    n = []
    cell = [(0.0, 1.5, 57), (1.5, 0.5, 60), (2.0, 1.0, 64), (3.0, 1.0, 62),
            (4.0, 1.5, 57), (5.5, 0.5, 55), (6.0, 2.0, 57)]
    for rep in range(6):
        for b, length, p in cell:
            n.append((rep * 8.0 + b, length * 0.95, p, 88))
    return n, 48.0


# ---- synthesis ----------------------------------------------------------------------------------------------------

def midi_hz(p):
    return 440.0 * 2.0 ** ((p - 69) / 12.0)


def adsr(count, a, d, s, r, hold_samples):
    """Envelope for a note held `hold_samples`, followed by its release; `count` is the buffer length."""
    t = np.arange(count) / RATE
    env = np.zeros(count)
    held = t < hold_samples / RATE
    ta = t[held]
    env_h = np.where(ta < a, ta / max(a, 1e-6), s + (1.0 - s) * np.exp(-(ta - a) / max(d, 1e-6)))
    env[held] = env_h
    level_at_release = env_h[-1] if env_h.size else 0.0
    tr = t[~held] - hold_samples / RATE
    env[~held] = level_at_release * np.exp(-tr / max(r, 1e-6))
    return env


def additive(f0, count, amps, detune_cents=0.0, phase_seed=0):
    t = np.arange(count) / RATE
    out = np.zeros(count)
    rng = np.random.RandomState(phase_seed)
    nyq = RATE * 0.45
    for k, a in enumerate(amps, start=1):
        f = f0 * k * 2.0 ** (detune_cents / 1200.0)
        if f >= nyq or a == 0.0:
            continue
        out += a * np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi))
    return out


def voice_pad(f0, count, hold, vel, seed):
    # Warm analog pad: three detuned saws (1/k), gently low-passed, slow attack, long release.
    ks = np.arange(1, 41)
    amps = (1.0 / ks) * np.exp(-ks * f0 / 900.0)
    sig = sum(additive(f0, count, amps, c, seed + i) for i, c in enumerate((-7.0, 0.0, 6.0)))
    return sig * adsr(count, 0.45, 0.8, 0.8, 1.2, hold) * (0.35 + 0.65 * vel)


def voice_bell(f0, count, hold, vel, seed):
    # FM bell: carrier ratio 1, modulator ratio 1.4 (inharmonic), index decaying from 6 to 0.5.
    t = np.arange(count) / RATE
    idx = 0.5 + 5.5 * np.exp(-t / 0.35) * (0.5 + 0.5 * vel)
    mod = np.sin(2 * np.pi * f0 * 1.4 * t)
    car = np.sin(2 * np.pi * f0 * t + idx * mod)
    # a second, quieter, higher bell partial (ratio 3.5 modulator) adds shimmer
    car2 = 0.3 * np.sin(2 * np.pi * f0 * 2.0 * t + 2.0 * np.exp(-t / 0.2) * np.sin(2 * np.pi * f0 * 7.0 * t))
    env = np.exp(-t / 1.1) * np.minimum(1.0, t / 0.002)
    rel = adsr(count, 0.001, 10.0, 1.0, 0.25, hold)  # note-off damps the ring
    return (car + car2) * env * rel * (0.3 + 0.7 * vel)


def voice_bass(f0, count, hold, vel, seed):
    # Distorted wavetable-style bass: a saw/square morph plus a sub an octave down, hard tanh drive.
    t = np.arange(count) / RATE
    ks = np.arange(1, 60)
    morph = 0.5 + 0.5 * np.sin(2 * np.pi * 0.7 * t[0])  # fixed per note (the table position)
    saw = additive(f0, count, 1.0 / ks, 0.0, seed)
    sq = additive(f0, count, np.where(ks % 2 == 1, 1.0 / ks, 0.0), 0.0, seed + 1)
    sub = np.sin(2 * np.pi * f0 * 0.5 * t)
    raw = (1 - morph) * saw + morph * sq + 0.8 * sub
    env = adsr(count, 0.004, 0.25, 0.85, 0.12, hold)
    drive = 7.0 * (0.5 + 0.5 * vel)
    return np.tanh(drive * raw * env) * 0.8 * (0.4 + 0.6 * vel) * np.minimum(1.0, env * 8.0)


def voice_perc(f0, count, hold, vel, seed):
    # Noisy percussion: a band-passed noise burst centred near the note's pitch, plus a pitched click.
    rng = np.random.RandomState(seed)
    noise = rng.uniform(-1.0, 1.0, count)
    spec = np.fft.rfft(noise)
    freqs = np.fft.rfftfreq(count, 1.0 / RATE)
    centre = f0 * 4.0
    shape = np.exp(-0.5 * (np.log2(np.maximum(freqs, 1.0) / centre) / 1.2) ** 2)
    noise = np.fft.irfft(spec * shape, count)
    noise /= max(np.max(np.abs(noise)), 1e-9)
    t = np.arange(count) / RATE
    env = np.exp(-t / 0.07) * np.minimum(1.0, t / 0.0008)
    click = np.sin(2 * np.pi * f0 * 2.0 * t) * np.exp(-t / 0.015)
    return (0.9 * noise + 0.5 * click) * env * (0.25 + 0.75 * vel)


VOICES = {"pad": voice_pad, "bell": voice_bell, "bass": voice_bass, "perc": voice_perc}
TAILS = {"pad": 4.0, "bell": 3.0, "bass": 0.8, "perc": 0.5}


def render(notes, total_beats, voice, tail, seed_base, morph=None):
    seconds = total_beats * BEAT + 1.5
    count = int(seconds * RATE)
    left = np.zeros(count)
    right = np.zeros(count)
    for i, (sb, lb, p, v) in enumerate(notes):
        start = int(round(sb * BEAT * RATE))
        hold = int(round(lb * BEAT * RATE))
        n = min(hold + int(tail * RATE), count - start)
        if n <= 0:
            continue
        vel = v / 127.0
        if morph is not None:
            sig = morph(midi_hz(p), n, hold, vel, seed_base + i, start)
        else:
            sig = voice(midi_hz(p), n, hold, vel, seed_base + i)
        pan = 0.5 + 0.25 * math.sin(p * 1.7)  # a gentle, fixed spread by pitch
        left[start:start + n] += sig * math.cos(pan * math.pi / 2) * math.sqrt(2)
        right[start:start + n] += sig * math.sin(pan * math.pi / 2) * math.sqrt(2)
    return left, right


def morph_voice_factory(total_seconds):
    """§36: one saw voice whose synthesis parameters follow the song position:
    0-20% clean (few harmonics), 20-40% brighter (cutoff opens), 40-60% resonant (a peak at the cutoff),
    60-80% distorted (tanh drive), 80-100% noisy (noise replaces the tone). Each stage ramps continuously."""
    def stage(x, a, b):
        return float(np.clip((x - a) / (b - a), 0.0, 1.0))

    def voice(f0, count, hold, vel, seed, start_sample):
        t = np.arange(count) / RATE
        x0 = start_sample / RATE / total_seconds
        # parameters sampled per note-sample on the global timeline
        x = x0 + t / total_seconds
        bright = np.clip((x - 0.1) / 0.3, 0.0, 1.0)
        reson = np.clip((x - 0.4) / 0.2, 0.0, 1.0)
        drive = np.clip((x - 0.6) / 0.2, 0.0, 1.0)
        noisy = np.clip((x - 0.8) / 0.2, 0.0, 1.0)
        cutoff = 400.0 + bright * 5600.0  # Hz
        out = np.zeros(count)
        rng = np.random.RandomState(seed)
        for k in range(1, 50):
            f = f0 * k
            if f > RATE * 0.45:
                break
            lp = 1.0 / (1.0 + (f / cutoff) ** 4)
            peak = 1.0 + reson * 6.0 * np.exp(-0.5 * (np.log2(f / cutoff) / 0.15) ** 2)
            out += (1.0 / k) * lp * peak * np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi))
        env = adsr(count, 0.01, 0.4, 0.75, 0.25, hold)
        tone = out * env
        tone = np.tanh((1.0 + 9.0 * drive) * tone) / np.tanh(1.0 + 9.0 * drive) * (1.0 + 0.0 * drive)
        noise = rng.uniform(-1.0, 1.0, count) * env * 0.8
        return ((1.0 - 0.85 * noisy) * tone + noisy * noise) * (0.4 + 0.6 * vel)

    return voice


def normalise(left, right, target_rms_db=-18.0, peak_db=-1.0):
    both = np.concatenate([left, right])
    active = both[np.abs(both) > 1e-4]
    rms = math.sqrt(float(np.mean(active ** 2))) if active.size else 1e-9
    gain = 10 ** (target_rms_db / 20.0) / max(rms, 1e-9)
    peak = float(np.max(np.abs(both))) * gain
    limit = 10 ** (peak_db / 20.0)
    if peak > limit:
        gain *= limit / peak
    return left * gain, right * gain


def write_wav(path, left, right):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    inter = np.empty(left.size * 2)
    inter[0::2] = left
    inter[1::2] = right
    pcm = np.clip(np.round(inter * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


# ---- MIDI file ----------------------------------------------------------------------------------------------------

def vlq(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def write_midi(path, notes, ppq=480):
    """A type-0 Standard MIDI File, channel 1, at BPM."""
    events = []
    for sb, lb, p, v in notes:
        on = int(round(sb * ppq))
        off = int(round((sb + lb) * ppq))
        events.append((on, 1, p, v))
        events.append((off, 0, p, 0))
    events.sort(key=lambda e: (e[0], e[1], e[2]))  # offs before ons at the same tick
    track = bytearray()
    track += vlq(0) + b"\xff\x51\x03" + int(round(60e6 / BPM)).to_bytes(3, "big")
    track += vlq(0) + b"\xff\x58\x04\x04\x02\x18\x08"
    last = 0
    for tick, is_on, p, v in events:
        track += vlq(tick - last)
        last = tick
        track += bytes([0x90 if is_on else 0x80, p, v if is_on else 64])
    track += vlq(0) + b"\xff\x2f\x00"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, ppq))
        f.write(b"MTrk" + struct.pack(">I", len(track)) + bytes(track))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--audio-dir", default=os.path.join(REPO, "assets", "audio"))
    parser.add_argument("--notes-dir", default=os.path.join(REPO, "examples", "sonic-garden", "notes"))
    args = parser.parse_args()

    report = {}
    phrase, pbeats = phrase_notes()
    context, cbeats = context_notes()
    morph, mbeats = morph_notes()
    write_midi(os.path.join(args.notes_dir, "phrase.mid"), phrase)
    write_midi(os.path.join(args.notes_dir, "context.mid"), context)
    write_midi(os.path.join(args.notes_dir, "morph.mid"), morph)

    for seed, name in enumerate(("pad", "bell", "bass", "perc")):
        l, r = render(phrase, pbeats, VOICES[name], TAILS[name], 1000 * (seed + 1))
        if name == "bass":
            # The bass's saturation is on its bus too, as a distorted patch's usually is: the voices of a chord
            # intermodulate there, which is most of what makes distortion sound rough rather than merely bright.
            l, r = np.tanh(1.6 * l), np.tanh(1.6 * r)
        l, r = normalise(l, r)
        path = os.path.join(args.audio_dir, "sonic-%s.wav" % name)
        report[os.path.basename(path)] = write_wav(path, l, r)
    for name in ("pad", "bell"):
        l, r = render(context, cbeats, VOICES[name], TAILS[name], 7000)
        l, r = normalise(l, r)
        path = os.path.join(args.audio_dir, "sonic-context-%s.wav" % name)
        report[os.path.basename(path)] = write_wav(path, l, r)
    total = mbeats * BEAT + 1.5
    l, r = render(morph, mbeats, None, 0.6, 9000, morph=morph_voice_factory(mbeats * BEAT))
    l, r = normalise(l, r)
    path = os.path.join(args.audio_dir, "sonic-morph.wav")
    report[os.path.basename(path)] = write_wav(path, l, r)
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()

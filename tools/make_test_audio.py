#!/usr/bin/env python3
"""Generates a deterministic synthetic test track (120 BPM: kick, snare, hats, bass, pad).

Usage: make_test_audio.py OUT.wav [--seconds 24] [--rate 48000]
Pure standard library so it runs anywhere; ~10 s for 24 s of audio.
"""
import argparse
import array
import math
import random
import wave


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    parser.add_argument("--seconds", type=float, default=24.0)
    parser.add_argument("--rate", type=int, default=48000)
    parser.add_argument("--bpm", type=float, default=120.0)
    args = parser.parse_args()

    rate = args.rate
    frames = int(args.seconds * rate)
    beat = 60.0 / args.bpm
    bar = beat * 4
    rng = random.Random(20260908)
    left = array.array("h", [0]) * frames
    right = array.array("h", [0]) * frames

    bass_notes = [55.0, 55.0, 65.41, 49.0]  # A1 A1 C2 G1 per bar
    pad_chords = [(220.0, 261.63, 329.63), (196.0, 246.94, 293.66)]  # Am, G

    two_pi = 2.0 * math.pi
    for i in range(frames):
        t = i / rate
        bar_index = int(t / bar)
        beat_in_bar = (t % bar) / beat
        in_break = 8 <= bar_index < 10  # bars 8-9: no kick/snare (dynamics test)
        s = 0.0

        # Kick: pitch sweep 160 -> 45 Hz, 250 ms decay, every beat.
        tb = t % beat
        if not in_break and t >= 0.5:
            env = math.exp(-tb * 12.0)
            freq = 45.0 + 115.0 * math.exp(-tb * 40.0)
            s += 0.9 * env * math.sin(two_pi * freq * tb)

        # Snare on beats 2 and 4: noise + 180 Hz body, 150 ms.
        if not in_break and int(beat_in_bar) in (1, 3):
            ts = tb
            env = math.exp(-ts * 18.0)
            s += 0.45 * env * (rng.uniform(-1, 1) * 0.7 + 0.3 * math.sin(two_pi * 180.0 * ts))

        # Hi-hats on off-beats (8ths); 16ths in every fourth bar.
        eighth = (t % (beat / 2))
        sixteenth = (t % (beat / 4))
        hat_t = sixteenth if bar_index % 4 == 3 else eighth
        if hat_t < 0.03 and t >= 0.5:
            s += 0.25 * math.exp(-hat_t * 90.0) * rng.uniform(-1, 1)

        # Bass: 8th-note gated, fundamental + 3rd harmonic.
        if t >= 0.5:
            note = bass_notes[bar_index % 4]
            gate = 1.0 if eighth < beat * 0.35 else 0.0
            s += 0.35 * gate * (math.sin(two_pi * note * t) + 0.3 * math.sin(two_pi * note * 3 * t))

        # Pad: sawtooth-ish chords, low level, swells over each 2-bar phrase.
        chord = pad_chords[(bar_index // 2) % 2]
        phrase = (t % (bar * 2)) / (bar * 2)
        swell = 0.5 + 0.5 * math.sin(two_pi * phrase - math.pi / 2)
        pad = 0.0
        for f in chord:
            for h in range(1, 6):
                pad += math.sin(two_pi * f * h * t + 0.3 * h) / h
        s += 0.05 * swell * pad

        # Gentle stereo: pad slightly wider, everything else centred.
        pan = 0.06 * math.sin(two_pi * 0.1 * t)
        l = max(-1.0, min(1.0, s * (1.0 - pan)))
        r = max(-1.0, min(1.0, s * (1.0 + pan)))
        left[i] = int(l * 32000)
        right[i] = int(r * 32000)

    inter = array.array("h", [0]) * (frames * 2)
    inter[0::2] = left
    inter[1::2] = right
    with wave.open(args.out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(inter.tobytes())
    print(f"wrote {args.out}: {args.seconds}s stereo {rate} Hz, {args.bpm} BPM")


if __name__ == "__main__":
    main()

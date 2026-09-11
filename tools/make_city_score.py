#!/usr/bin/env python3
"""Renders "Night Shift": 104 seconds of E-minor city-pop, generated here.

The proof-of-concept music video needs a *song*, and the project may not ship anyone else's. Every
sample below is computed from the notes in this file, so the result carries no third-party rights at
all -- there is nothing to clear, nothing to attribute and nothing to take down. It is also
deterministic: the same arguments produce the same bytes, so a rendered movie can be diffed against
another run of the same movie.

It is written to be *sequenced*, which is a harder requirement than being heard. The engine's
musical-event detector reads energy, onsets and a beat clock and folds them into sections; a piece
with no rhythmic floor gives the beat tracker nothing to lock to, and a piece that only ever rises
can never produce a drop. So the arrangement is deliberately blocked out:

    bars  0- 7   INTRO     pad and hat. No kick: the first kick has to be an event.
    bars  8-15   VERSE     kick, bass, a quiet backbeat.
    bars 16-23   CHORUS    snare, arpeggio, the lot.
    bars 24-29   VERSE     the arpeggio drops out; the bass keeps walking.
    bars 30-31   BREAK     everything but the pad ducks. Two bars, quietest in the piece.
    bars 32-39   CHORUS    the return, a little louder than the first one.
    bars 40-41   OUTRO     pad alone, fading.

96 BPM: a beat is 0.625 s and a bar is 2.5 s, so the section boundaries land on 0:00, 0:20, 0:40,
0:60, 0:75, 0:80 and 0:100 -- round numbers an author can cut to without a calculator.

Usage: make_city_score.py OUT.wav [--bpm 96] [--rate 48000] [--seed 20260911]
Pure standard library. About a minute.
"""
import argparse
import array
import math
import random
import wave

TWO_PI = 2.0 * math.pi

# E minor / G major, the mode most pop in a city is written in. Cmaj7 - Bm7 - Em7 - Am7 loops
# without ever landing on a tonic cadence, so the two minutes have no natural stopping point.
CHORDS = [
    (130.81, 164.81, 196.00, 246.94),  # Cmaj7   C  E  G  B
    (123.47, 146.83, 185.00, 220.00),  # Bm7     B  D  F# A
    (164.81, 196.00, 246.94, 293.66),  # Em7     E  G  B  D
    (110.00, 130.81, 164.81, 196.00),  # Am7     A  C  E  G
]
BASS = [65.41, 61.74, 82.41, 55.00]  # C2, B1, E2, A1

# E minor pentatonic across two octaves. Nothing here can be dissonant against any of the four
# chords, which is what lets the arpeggio be seeded rather than written.
ARP = [329.63, 392.00, 440.00, 493.88, 587.33, 659.25, 783.99, 880.00, 987.77, 1174.66]

BAR_INTRO, BAR_VERSE, BAR_CHORUS, BAR_VERSE2, BAR_BREAK, BAR_FINAL, BAR_OUTRO = 0, 8, 16, 24, 30, 32, 40
TOTAL_BARS = 42


def smoothstep(x: float) -> float:
    x = min(1.0, max(0.0, x))
    return x * x * (3.0 - 2.0 * x)


def section_of(bar: float) -> str:
    if bar < BAR_VERSE:
        return "intro"
    if bar < BAR_CHORUS:
        return "verse"
    if bar < BAR_VERSE2:
        return "chorus"
    if bar < BAR_BREAK:
        return "verse2"
    if bar < BAR_FINAL:
        return "break"
    if bar < BAR_OUTRO:
        return "final"
    return "outro"


def weights(bar: float) -> dict:
    """How loud each voice is at `bar`. One table, so the arrangement is legible as a table."""
    s = section_of(bar)
    w = {
        "intro":  dict(pad=0.55, hat=0.45, kick=0.0, bass=0.0, snare=0.0, arp=0.0, lead=0.0),
        "verse":  dict(pad=0.85, hat=0.80, kick=1.0, bass=1.0, snare=0.45, arp=0.0, lead=0.0),
        "chorus": dict(pad=1.00, hat=1.00, kick=1.0, bass=1.0, snare=1.00, arp=0.9, lead=0.8),
        "verse2": dict(pad=0.85, hat=0.70, kick=1.0, bass=1.0, snare=0.50, arp=0.0, lead=0.35),
        # A break has to be the quietest thing in the piece by a margin nothing else reaches, or the
        # detector correctly declines to call it one -- the Glowmere score learned that twice.
        "break":  dict(pad=0.30, hat=0.05, kick=0.0, bass=0.0, snare=0.0, arp=0.0, lead=0.0),
        "final":  dict(pad=1.00, hat=1.00, kick=1.0, bass=1.0, snare=1.00, arp=1.0, lead=1.0),
        "outro":  dict(pad=0.70, hat=0.20, kick=0.0, bass=0.4, snare=0.0, arp=0.0, lead=0.0),
    }[s]
    out = dict(w)
    # Ease the first bar of each section so nothing switches on with a click, except the first kick
    # of the verse, which is meant to arrive.
    for boundary in (BAR_VERSE, BAR_CHORUS, BAR_VERSE2, BAR_BREAK, BAR_FINAL, BAR_OUTRO):
        if 0.0 <= bar - boundary < 1.0:
            fade = smoothstep(bar - boundary)
            for key in ("pad", "hat", "arp", "lead"):
                previous = weights(boundary - 0.01)[key] if boundary > 0 else 0.0
                out[key] = previous + (out[key] - previous) * fade
    # The final chorus is the arrival: a little hotter for its first two bars.
    if BAR_FINAL <= bar < BAR_FINAL + 2:
        boost = 1.0 + 0.25 * (1.0 - smoothstep((bar - BAR_FINAL) / 2.0))
        for key in out:
            out[key] *= boost
    if bar >= BAR_OUTRO:
        fade = 1.0 - smoothstep((bar - BAR_OUTRO) / max(TOTAL_BARS - BAR_OUTRO, 1e-6))
        for key in out:
            out[key] *= fade
    return out


def add(buf_l, buf_r, i0: int, samples, pan: float, n: int) -> None:
    left = 0.5 - pan * 0.5
    right = 0.5 + pan * 0.5
    for k, v in enumerate(samples):
        i = i0 + k
        if 0 <= i < n:
            buf_l[i] += v * left * 2.0 * 0.5
            buf_r[i] += v * right * 2.0 * 0.5


def kick(rate: int, gain: float) -> list:
    """A sine with a fast pitch drop. This is what the beat tracker locks to and what audio.bass
    reads; without it the routes have a signal with no rhythm in it."""
    n = int(0.34 * rate)
    out = [0.0] * n
    for k in range(n):
        t = k / rate
        freq = 48.0 + 90.0 * math.exp(-t * 30.0)
        out[k] = gain * math.exp(-t * 9.0) * math.sin(TWO_PI * freq * t)
    # A short click at the front, so the onset detector has an edge to find.
    for k in range(min(int(0.004 * rate), n)):
        out[k] += gain * 0.5 * (1.0 - k / max(int(0.004 * rate), 1))
    return out


def snare(rate: int, gain: float, rng: random.Random) -> list:
    n = int(0.22 * rate)
    out = [0.0] * n
    noise = 0.0
    for k in range(n):
        t = k / rate
        noise += (rng.random() * 2.0 - 1.0 - noise) * 0.55
        body = 0.35 * math.sin(TWO_PI * 185.0 * t) * math.exp(-t * 26.0)
        out[k] = gain * math.exp(-t * 16.0) * (noise * 0.9 + body)
    return out


def hat(rate: int, gain: float, rng: random.Random, open_hat: bool) -> list:
    n = int((0.14 if open_hat else 0.055) * rate)
    out = [0.0] * n
    previous = 0.0
    for k in range(n):
        t = k / rate
        white = rng.random() * 2.0 - 1.0
        # One-pole high pass: the hat has to live above the pad or it muddies the spectral centroid.
        high = white - previous
        previous = white
        out[k] = gain * math.exp(-t * (24.0 if open_hat else 60.0)) * high * 0.5
    return out


def pluck(rate: int, freq: float, gain: float, seconds: float, bright: float) -> list:
    """Two partials and an exponential tail. Used for the bass, the arpeggio and the lead, because
    one plucked voice at three registers reads as one band rather than three."""
    n = int(seconds * rate)
    out = [0.0] * n
    for k in range(n):
        t = k / rate
        attack = min(1.0, t / 0.006)
        decay = math.exp(-t * (2.2 + 1.8 * bright))
        s = math.sin(TWO_PI * freq * t)
        s += bright * 0.42 * math.exp(-t * 6.0) * math.sin(TWO_PI * freq * 2.0 * t)
        s += bright * 0.16 * math.exp(-t * 11.0) * math.sin(TWO_PI * freq * 3.01 * t)
        out[k] = gain * attack * decay * s
    return out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    parser.add_argument("--bpm", type=float, default=96.0)
    parser.add_argument("--rate", type=int, default=48000)
    parser.add_argument("--seed", type=int, default=20260911)
    args = parser.parse_args()

    rate = args.rate
    beat = 60.0 / args.bpm
    bar = beat * 4.0
    seconds = TOTAL_BARS * bar
    n = int(seconds * rate)
    rng = random.Random(args.seed)

    left = [0.0] * n
    right = [0.0] * n

    # ---- the pad -------------------------------------------------------------------------------
    # Each chord held for one bar and crossfaded into the next, so the harmony moves without any
    # voice restarting. Panned by pitch: low voices centred, high voices spread.
    for b in range(TOTAL_BARS + 1):
        start = b * bar
        gain = weights(b + 0.5)["pad"]
        if gain <= 0.0:
            continue
        chord = CHORDS[b % len(CHORDS)]
        for v, freq in enumerate(chord):
            pan = (v / max(len(chord) - 1, 1) - 0.5) * 0.7
            span = bar * 1.35
            m = int(span * rate)
            voice = [0.0] * m
            for k in range(m):
                t = k / rate
                # Cosine shoulders: no clicks, and no derivative discontinuity for the spectral flux
                # to read as an onset where there is not one.
                if t < bar * 0.35:
                    env = 0.5 - 0.5 * math.cos(math.pi * t / (bar * 0.35))
                elif t > span - bar * 0.5:
                    env = 0.5 - 0.5 * math.cos(math.pi * (span - t) / (bar * 0.5))
                else:
                    env = 1.0
                vib = 1.0 + 0.0014 * math.sin(TWO_PI * (0.13 + 0.041 * v) * t)
                ph = TWO_PI * freq * vib * t
                voice[k] = (0.052 * gain * env *
                            (math.sin(ph) + 0.30 * math.sin(2.0 * ph) + 0.10 * math.sin(3.0 * ph)))
            add(left, right, int(start * rate), voice, pan, n)

    # ---- the drums -----------------------------------------------------------------------------
    kickBody = kick(rate, 1.0)
    for b in range(TOTAL_BARS):
        for step in range(8):  # eighth notes
            at = b * bar + step * beat * 0.5
            w = weights(b + step / 8.0)
            # Kick on 1 and 3, plus a pickup on the "and" of 4 every fourth bar.
            if step in (0, 4) or (step == 7 and b % 4 == 3):
                if w["kick"] > 0.0:
                    gain = 0.62 * w["kick"] * (0.75 if step == 7 else 1.0)
                    add(left, right, int(at * rate), [v * gain for v in kickBody], 0.0, n)
            if step in (2, 6) and w["snare"] > 0.0:
                add(left, right, int(at * rate), snare(rate, 0.30 * w["snare"], rng), 0.05, n)
            if w["hat"] > 0.0:
                openHat = step == 7
                gain = 0.16 * w["hat"] * (1.0 if step % 2 == 0 else 0.62)
                add(left, right, int(at * rate), hat(rate, gain, rng, openHat), -0.18, n)

    # ---- the bass ------------------------------------------------------------------------------
    # Root on 1, fifth on the "and" of 3: enough movement to read as a line, few enough notes that
    # it never argues with the kick.
    for b in range(TOTAL_BARS):
        root = BASS[b % len(BASS)]
        for step, freq, length in ((0, root, beat * 1.6), (5, root * 1.5, beat * 0.8),
                                   (6, root, beat * 1.4)):
            at = b * bar + step * beat * 0.5
            w = weights(b + step / 8.0)["bass"]
            if w <= 0.0:
                continue
            add(left, right, int(at * rate), pluck(rate, freq, 0.30 * w, length, 0.35), 0.0, n)

    # ---- the arpeggio --------------------------------------------------------------------------
    # Sixteenths through the chord's own pentatonic neighbours, seeded. The only stochastic element.
    for b in range(TOTAL_BARS):
        for step in range(16):
            at = b * bar + step * beat * 0.25
            w = weights(b + step / 16.0)["arp"]
            if w <= 0.0 or rng.random() > 0.55:
                continue
            freq = ARP[rng.randrange(len(ARP))]
            pan = (freq - ARP[0]) / (ARP[-1] - ARP[0]) - 0.5
            add(left, right, int(at * rate),
                pluck(rate, freq, 0.085 * w * (400.0 / freq) ** 0.3, 0.9, 1.0), pan * 0.8, n)

    # ---- the lead ------------------------------------------------------------------------------
    # A four-note phrase per two bars, the same shape every time, so the chorus has a hook rather
    # than an ornament. Transposed by the chord underneath it.
    phrase = [(0.0, 4), (beat * 1.5, 6), (beat * 2.5, 5), (beat * 3.0, 7)]
    for b in range(0, TOTAL_BARS, 2):
        w = weights(b + 1.0)["lead"]
        if w <= 0.0:
            continue
        shift = (b // 2) % 2
        for offset, index in phrase:
            freq = ARP[min(index + shift, len(ARP) - 1)]
            at = b * bar + offset
            add(left, right, int(at * rate), pluck(rate, freq, 0.13 * w, 1.4, 0.85), 0.12, n)

    # ---- master --------------------------------------------------------------------------------
    # A soft knee rather than a hard clip: the analysis reads peak as well as RMS, and a clipped peak
    # would tell the routes about the limiter instead of about the music.
    out = array.array("h", [0]) * (n * 2)
    for i in range(n):
        for ch, buf in ((0, left), (1, right)):
            v = buf[i] * 0.95
            v = math.tanh(v * 1.1) * 0.88
            out[i * 2 + ch] = max(-32768, min(32767, int(v * 32767.0)))

    with wave.open(args.out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(out.tobytes())
    print(f"{args.out}: {seconds:.1f} s, {rate} Hz stereo, {args.bpm:g} BPM, seed {args.seed}")
    for name, b in (("INTRO", BAR_INTRO), ("VERSE", BAR_VERSE), ("CHORUS", BAR_CHORUS),
                    ("VERSE 2", BAR_VERSE2), ("BREAK", BAR_BREAK), ("CHORUS 2", BAR_FINAL),
                    ("OUTRO", BAR_OUTRO)):
        print(f"  {b * bar:7.2f}s  bar {b:3d}  {name}")


if __name__ == "__main__":
    main()

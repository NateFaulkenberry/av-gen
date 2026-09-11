#!/usr/bin/env python3
"""Renders the Glowmere Valley score: 90 seconds of D-Dorian ambient, generated here.

The piece exists because the shot needs music and the project may not ship anyone else's. Every
sample below is computed from the notes in this file, so the result carries no third-party rights
at all -- there is nothing to clear, nothing to attribute and nothing to take down. It is also
deterministic: the same arguments produce the same bytes, so a rendered movie can be compared
against another run of the same movie.

It is written to be *analysed*, not only heard. The engine's routes read bass, treble, spectral
flux and a beat clock, and a track with no transients gives a beat tracker nothing to lock to and
a bloom threshold nothing to cross. So the arrangement has a deliberate low pulse on beats 1 and 3,
bell strikes with fast attacks in the top two octaves, and an air bed that moves the spectral
centroid without ever producing an onset.

Usage: make_glowmere_score.py OUT.wav [--seconds 90] [--rate 48000] [--bpm 72]
Pure standard library. About a minute for the full 90 seconds.
"""
import argparse
import array
import math
import random
import wave

TWO_PI = 2.0 * math.pi

# D Dorian. The mode's raised sixth (B against a D root) is what keeps a minor-ish drone from
# reading as mourning, which is the wrong feeling for a valley full of light.
D1, D2 = 36.708, 73.416

# Chord per two bars, as frequencies. Dm9, Gm7, Bbmaj7, Am7 -- a loop that never resolves to a
# tonic cadence, so the ninety seconds have no natural stopping point until the fade.
CHORDS = [
    (146.83, 174.61, 220.00, 329.63),  # Dm9   D  F  A  E
    (196.00, 233.08, 293.66, 349.23),  # Gm7   G  Bb D  F
    (233.08, 293.66, 349.23, 440.00),  # Bbma7 Bb D  F  A
    (220.00, 261.63, 329.63, 392.00),  # Am7   A  C  E  G
]

# The bell voice: D minor pentatonic across three octaves, which cannot form a dissonance against
# any of the four chords above. Sparse, seeded, and the only part of the piece that is random.
BELLS = [293.66, 349.23, 392.00, 440.00, 523.25,
         587.33, 698.46, 783.99, 880.00, 1046.50,
         1174.66, 1396.91, 1567.98, 1760.00]


def envelope(t: float, span: float, attack: float, release: float) -> float:
    """Trapezoid with cosine shoulders: no clicks, and no derivative discontinuity for the
    spectral flux to read as an onset where there is not one."""
    if t < 0.0 or t > span:
        return 0.0
    if t < attack:
        return 0.5 - 0.5 * math.cos(math.pi * t / attack)
    if t > span - release:
        return 0.5 - 0.5 * math.cos(math.pi * (span - t) / release)
    return 1.0


# Where the break sits, as a fraction of the piece. Just past the middle: late enough that the
# arrangement has built something to take away, early enough to leave room for the arrival to land
# and then settle.
kBreakStart = 0.54
kBreakEnd = 0.615
kDropSettle = 0.70
# The gain is back within about a second and a half of the break ending, well inside the two and a
# half seconds the classifier allows a break to resolve in.
kDropReturn = 0.632


def break_gain(t: float, seconds: float) -> float:
    """How loud *everything* is, including the drone, through the break.

    The drone is the only voice `section_weight` does not gate -- it follows the swell alone -- so
    it set the floor of the break and no amount of thinning the other voices could get under it.
    The first two attempts at a break measured 0.042 and 0.090 mean RMS against natural troughs of
    0.040 elsewhere in the piece; the detector correctly declined to call either a break, because
    neither was one. A break has to be the quietest thing in the track by a margin nothing else
    reaches, and that means the drone has to duck too.

    It never reaches zero. A break of literal silence reads as a fault in the file, and the
    classifier wants energy to collapse rather than vanish.
    """
    u = t / seconds
    if u < kBreakStart or u >= kDropReturn:
        return 1.0
    if u < kBreakEnd:
        hush = 1.0 - smoothstep((u - kBreakStart) / max(kBreakEnd - kBreakStart, 1e-6))
        return 0.10 + 0.90 * hush
    # The return is *fast*. A drop, to the classifier, is a break resolving within about two and a
    # half seconds; recovering over the seven seconds it takes the arrangement to settle means the
    # energy is back but never arrived, and no drop is recognised. So the gain snaps back over
    # roughly a bar and a half and the long taper is left to `boost`, which shapes the arrival
    # rather than gating it.
    rise = smoothstep((u - kBreakEnd) / max(kDropReturn - kBreakEnd, 1e-6))
    return 0.10 + 0.90 * rise


def smoothstep(x: float) -> float:
    x = min(1.0, max(0.0, x))
    return x * x * (3.0 - 2.0 * x)


def section_weight(t: float, seconds: float) -> tuple:
    """How loud each voice is at time `t`, following the camera: the shot opens beside the hero,
    travels the valley from about a quarter in, climbs to a vista at two thirds and arrives near
    the end. Returned as (pad, pulse, bell, air)."""
    u = t / seconds
    pad = min(1.0, max(0.0, (u - 0.10) / 0.25))
    pulse = min(1.0, max(0.0, (u - 0.22) / 0.20))
    bell = 0.35 + 0.65 * min(1.0, max(0.0, (u - 0.05) / 0.55))
    air = 0.25 + 0.75 * min(1.0, max(0.0, (u - 0.30) / 0.45))
    # The break and the drop.
    #
    # Added because the piece had neither, and the visual system reads musical *structure*, not only
    # level. A drop, to the detector, is a break resolving into a loud downbeat -- so a score that
    # only ever rises can never produce one, and the whole family of behaviours that hang off a drop
    # (the camera landing a reveal on it, the world answering it) has nothing to fire on. The
    # arrangement was monotonic: pad in at 10%, pulse at 22%, bell and air ramping, fade at 93%.
    #
    # So: everything but the air drops out for about four seconds, then returns at full. The air bed
    # stays because a break with literal silence in it reads as a fault in the file rather than as a
    # held breath, and because the detector wants energy to *collapse*, not to vanish.
    if kBreakStart <= u < kBreakEnd:
        # Not a step. A hard gate would put a click in the audio and give the onset detector a
        # transient exactly where the music is meant to be emptying out.
        hush = 1.0 - smoothstep((u - kBreakStart) / max(kBreakEnd - kBreakStart, 1e-6))
        # Deeper than the first attempt, which measured 0.042 mean RMS against a *natural* trough of
        # 0.040 elsewhere in the piece -- so it was not a break, it was another dip, and the detector
        # correctly declined to call it one. A break has to be the quietest thing in the track by a
        # margin nothing else reaches.
        pad *= 0.05 + 0.95 * hush
        pulse *= 0.0 + 1.0 * hush
        bell *= 0.03 + 0.97 * hush
        air *= 0.20 + 0.80 * hush
    elif u < kDropSettle and u >= kBreakEnd:
        # The return, over about a bar and a half: loud, and slightly louder than before it, because
        # a drop that comes back to exactly where it left is a gap rather than an arrival.
        rise = smoothstep((u - kBreakEnd) / max(kDropSettle - kBreakEnd, 1e-6))
        boost = 1.0 + 0.45 * (1.0 - rise)
        pad *= boost
        pulse *= boost
        bell *= boost
        air *= boost

    # The arrival, and then the settle: everything but the drone recedes over the last six seconds
    # so the picture is left alone at the end of its own journey.
    if u > 0.93:
        fade = (1.0 - u) / 0.07
        pad *= fade
        pulse *= fade
        bell *= fade
        air *= fade
    return pad, pulse, bell, air


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    parser.add_argument("--seconds", type=float, default=90.0)
    parser.add_argument("--rate", type=int, default=48000)
    parser.add_argument("--bpm", type=float, default=72.0)
    parser.add_argument("--seed", type=int, default=20260910)
    args = parser.parse_args()

    rate = args.rate
    n = int(args.seconds * rate)
    beat = 60.0 / args.bpm
    bar = beat * 4.0
    rng = random.Random(args.seed)

    left = [0.0] * n
    right = [0.0] * n

    # ---- the drone, and the air bed ------------------------------------------------------------
    # Two octaves of D, detuned by a fifth of a hertz so they beat against each other once every
    # five seconds; that slow beating is most of why a held sine stops sounding like a test tone.
    noise = 0.0
    for i in range(n):
        t = i / rate
        _, _, _, air = section_weight(t, args.seconds)
        swell = (0.82 + 0.18 * math.sin(TWO_PI * t / 31.0)) * break_gain(t, args.seconds)
        s = 0.20 * swell * (math.sin(TWO_PI * D1 * t) + 0.7 * math.sin(TWO_PI * (D1 + 0.2) * t))
        s += 0.12 * swell * math.sin(TWO_PI * D2 * t)
        # One-pole low-passed noise. Its cutoff opens as the piece grows, which walks the spectral
        # centroid upward without ever producing a transient.
        noise += (rng.random() * 2.0 - 1.0 - noise) * (0.010 + 0.030 * air)
        a = 0.16 * air * noise
        left[i] = s + a
        right[i] = s + a * 0.82

    # ---- the pad -------------------------------------------------------------------------------
    # Each chord is held for two bars and crossfaded into the next, so the harmony moves without
    # any voice ever restarting. Panned by pitch: low voices centred, high voices spread.
    span = bar * 2.0
    steps = int(args.seconds / span) + 2
    for c in range(steps):
        start = c * span
        chord = CHORDS[c % len(CHORDS)]
        for v, freq in enumerate(chord):
            pan = (v / max(len(chord) - 1, 1) - 0.5) * 0.8
            i0 = max(int(start * rate), 0)
            i1 = min(int((start + span * 1.15) * rate), n)
            for i in range(i0, i1):
                t = i / rate
                pad, _, _, _ = section_weight(t, args.seconds)
                if pad <= 0.0:
                    continue
                e = envelope(t - start, span * 1.15, span * 0.45, span * 0.5)
                # A slow vibrato, a different rate per voice, so the chord shimmers rather than
                # sitting still.
                vib = 1.0 + 0.0016 * math.sin(TWO_PI * (0.11 + 0.037 * v) * t)
                ph = TWO_PI * freq * vib * t
                s = 0.055 * pad * e * (math.sin(ph) + 0.32 * math.sin(2.0 * ph) + 0.12 * math.sin(3.0 * ph))
                left[i] += s * (0.5 - pan * 0.5) * 2.0 * 0.5
                right[i] += s * (0.5 + pan * 0.5) * 2.0 * 0.5

    # ---- the pulse -----------------------------------------------------------------------------
    # Beats 1 and 3, a soft sine thump with a pitch drop. This is what the beat tracker locks to,
    # and what `audio.bass` reads; without it the routes have a signal with no rhythm in it.
    hit = int(0.42 * rate)
    b = 0
    while b * beat < args.seconds:
        if b % 2 == 0:
            start = b * beat
            _, pulse, _, _ = section_weight(start, args.seconds)
            if pulse > 0.0:
                i0 = int(start * rate)
                for k in range(min(hit, n - i0)):
                    tt = k / rate
                    decay = math.exp(-tt * 7.0)
                    freq = 52.0 + 46.0 * math.exp(-tt * 26.0)
                    s = 0.34 * pulse * decay * math.sin(TWO_PI * freq * tt)
                    left[i0 + k] += s
                    right[i0 + k] += s
        b += 1

    # ---- the bells -----------------------------------------------------------------------------
    # Struck on a seeded subset of eighth notes, which is the only stochastic element in the piece.
    # Two partials and a long exponential tail: fast attacks for the treble routes, and a decay
    # long enough that several are always ringing at once.
    ring = int(3.2 * rate)
    e8 = beat * 0.5
    k = 0
    while k * e8 < args.seconds:
        start = k * e8
        _, _, bell, _ = section_weight(start, args.seconds)
        if bell > 0.0 and rng.random() < 0.11 * bell:
            freq = BELLS[rng.randrange(len(BELLS))]
            pan = (freq - BELLS[0]) / (BELLS[-1] - BELLS[0]) - 0.5
            gain = 0.16 * bell * (0.55 + 0.45 * rng.random()) * (300.0 / freq) ** 0.35
            i0 = int(start * rate)
            for j in range(min(ring, n - i0)):
                tt = j / rate
                decay = math.exp(-tt * 1.15)
                s = gain * decay * (math.sin(TWO_PI * freq * tt) +
                                    0.38 * math.exp(-tt * 2.6) * math.sin(TWO_PI * freq * 2.005 * tt))
                left[i0 + j] += s * (0.5 - pan * 0.55)
                right[i0 + j] += s * (0.5 + pan * 0.55)
        k += 1

    # ---- master --------------------------------------------------------------------------------
    # A soft knee rather than a hard clip: the analysis reads peak as well as RMS, and a clipped
    # peak would tell the routes about the limiter instead of about the music.
    out = array.array("h", [0]) * (n * 2)
    for i in range(n):
        for ch, buf in ((0, left), (1, right)):
            v = buf[i] * 0.92
            v = math.tanh(v * 1.15) * 0.87
            out[i * 2 + ch] = max(-32768, min(32767, int(v * 32767.0)))

    with wave.open(args.out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(out.tobytes())
    print(f"{args.out}: {args.seconds:.1f} s, {rate} Hz stereo, {args.bpm:g} BPM, seed {args.seed}")


if __name__ == "__main__":
    main()

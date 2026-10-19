#!/usr/bin/env python3
"""The stem-analysis test fixture (stems-multichannel M1, brief section 6).

    python3 tools/make_stem_fixture.py --out build/release/test-fixtures/stems

Writes a stereo reference mix, four analysis stems (drums, bass, synth, pad) and the variants the offline tests need,
plus `fixture.json`: every event time, gain, pan, offset, format and the generation parameters. Nothing here is
committed but this script; the build runs it into the build tree (tests/CMakeLists.txt) so no test depends on audio
in the repository.

Deterministic: the only randomness (the uniform noise in the snare and hats) comes from numpy's PCG64 seeded with SEED, and
the files are written by this script's own WAV writer, byte for byte. Two runs on one machine produce identical
files (the test checks the sha256 the metadata records). Across machines numpy's transcendental functions may
differ in the last ulp, so the tests assert events and levels, never file hashes from another machine.

The mix is built here, offline, from the stems with documented gains and pans. That is solely how the fixture is
made: AV Gen itself must play mix.wav and analyse the stems separately, never sum them.

Musical layout (120 bpm, beat = 0.5 s, 20 s):
  drums  kick on every beat, snare on beats 2 and 4 (0.5 s + k), hats on every 8th note that is NOT a beat; the
         drums stop for a deliberate gap 8.0-10.0 s.
  bass   notes with known on/off times (10 ms attack, sine + 2nd harmonic, 40-110 Hz), silent 12.0-14.0 s.
  synth  stereo: plucks (2 ms attack, 180 ms decay, 1-2 kHz) at known times, two sustained notes with a 300 ms
         attack (4.0-7.0 s, 15.0-18.0 s). Written as 24-bit PCM.
  pad    the fourth stem: one sustained chord 10.0-16.0 s (slow 500 ms attack), silent elsewhere. Written as 16-bit PCM.
Overlaps on purpose: kicks with bass notes, plucks with hats, the pad with everything in 10-16 s.
"""

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

try:
    import numpy as np
except ImportError:  # the build must not fail for want of numpy: the stem tests SKIP and say why
    np = None

SEED = 20261019
RATE = 48000
DURATION = 20.0
BPM = 120.0
BEAT = 60.0 / BPM

# Gains and pans the mix is built with (pan: -1 left, +1 right, constant-power law).
MIX = {
    "drums": {"gain": 0.45, "pan": 0.0},
    "bass": {"gain": 0.45, "pan": 0.0},
    "synth": {"gain": 0.35, "pan": None},  # stereo stem: its own L/R, no pan
    "pad": {"gain": 0.3, "pan": -0.3},
}


def drum_events():
    kicks, snares, hats = [], [], []
    t = 0.0
    while t < DURATION - 1e-9:
        gap = 8.0 <= t < 10.0
        if not gap:
            kicks.append(round(t, 6))
            beat_index = int(round(t / BEAT))
            if beat_index % 2 == 1:
                snares.append(round(t, 6))
            off = t + BEAT / 2
            if off < DURATION and not (8.0 <= off < 10.0):
                hats.append(round(off, 6))
        t += BEAT
    return kicks, snares, hats


BASS_NOTES = [  # (start s, duration s, Hz)
    (0.0, 1.0, 55.0), (1.5, 0.5, 73.42), (2.5, 1.25, 41.2), (4.0, 2.0, 55.0), (6.5, 0.75, 82.41),
    (8.0, 1.5, 61.74), (10.0, 1.0, 55.0), (11.25, 0.5, 98.0), (14.0, 2.0, 49.0), (16.5, 1.0, 110.0),
    (18.0, 1.5, 55.0),
]
SYNTH_PLUCKS = [  # (start s, Hz) -- short attacks
    (0.75, 1046.5), (1.25, 1318.5), (2.75, 1568.0), (3.25, 1174.7), (7.25, 1396.9), (7.75, 1046.5),
    (9.25, 1760.0), (11.75, 1318.5), (13.25, 1568.0), (18.75, 1975.5), (19.25, 1046.5),
]
SYNTH_SUSTAINS = [(4.0, 3.0, 659.25), (15.0, 3.0, 523.25)]  # (start, duration, Hz), 300 ms attack
PAD_CHORD = (10.0, 6.0, [220.0, 277.18, 329.63])  # 500 ms attack


def tail_fade(seg, rate, seconds=0.005):
    """Fades the last `seconds` of a segment to zero: a segment cut off mid-decay is a step, and a step is an attack."""
    k = min(len(seg), int(seconds * rate))
    if k > 0:
        seg[-k:] *= np.linspace(1.0, 0.0, k)
    return seg


def render_drums(rate, rng):
    n = int(DURATION * rate)
    out = np.zeros(n)
    kicks, snares, hats = drum_events()
    for t in kicks:
        i = int(round(t * rate))
        m = int(0.30 * rate)
        tt = np.arange(m) / rate
        freq = 50.0 + 100.0 * np.exp(-tt / 0.03)  # 150 -> 50 Hz sweep
        phase = 2 * np.pi * np.cumsum(freq) / rate
        seg = tail_fade(0.5 * np.sin(phase) * np.exp(-tt / 0.09), rate)
        out[i:i + m] += seg[: max(0, min(m, n - i))]
    for t in snares:
        i = int(round(t * rate))
        m = int(0.20 * rate)
        tt = np.arange(m) / rate
        noise = rng.uniform(-1.0, 1.0, m)
        seg = tail_fade(0.2 * noise * np.exp(-tt / 0.05) + 0.18 * np.sin(2 * np.pi * 190.0 * tt) * np.exp(-tt / 0.04), rate)
        out[i:i + m] += seg[: max(0, min(m, n - i))]
    for t in hats:
        i = int(round(t * rate))
        m = int(0.06 * rate)
        tt = np.arange(m) / rate
        noise = rng.uniform(-1.0, 1.0, m + 1)
        hp = np.diff(noise)  # first difference: a crude high-pass, most energy above ~6 kHz
        seg = tail_fade(0.1 * hp * np.exp(-tt / 0.012), rate)
        out[i:i + m] += seg[: max(0, min(m, n - i))]
    return out


def envelope(m, rate, attack, release=0.01):
    tt = np.arange(m) / rate
    env = np.minimum(1.0, tt / max(attack, 1.0 / rate))
    rel = int(release * rate)
    if rel > 0 and m > rel:
        env[-rel:] *= np.linspace(1.0, 0.0, rel)
    return env


def render_bass(rate):
    n = int(DURATION * rate)
    out = np.zeros(n)
    for start, dur, hz in BASS_NOTES:
        i = int(round(start * rate))
        m = int(round(dur * rate))
        tt = np.arange(m) / rate
        seg = (0.6 * np.sin(2 * np.pi * hz * tt) + 0.25 * np.sin(2 * np.pi * 2 * hz * tt)) * envelope(m, rate, 0.010, 0.02)
        out[i:i + m] += seg[: max(0, min(m, n - i))]
    return out


def render_synth(rate):
    n = int(DURATION * rate)
    left = np.zeros(n)
    right = np.zeros(n)
    for start, hz in SYNTH_PLUCKS:
        i = int(round(start * rate))
        m = int(0.8 * rate)
        tt = np.arange(m) / rate
        seg = tail_fade(0.5 * np.sin(2 * np.pi * hz * tt) * np.minimum(1.0, tt / 0.002) * np.exp(-tt / 0.18), rate)
        k = max(0, min(m, n - i))
        left[i:i + m] += 0.8 * seg[:k]
        right[i:i + m] += 1.0 * seg[:k]
    for start, dur, hz in SYNTH_SUSTAINS:
        i = int(round(start * rate))
        m = int(round(dur * rate))
        tt = np.arange(m) / rate
        seg = 0.35 * (np.sin(2 * np.pi * hz * tt) + 0.3 * np.sin(2 * np.pi * 3 * hz * tt)) * envelope(m, rate, 0.300, 0.05)
        k = max(0, min(m, n - i))
        left[i:i + m] += 1.0 * seg[:k]
        right[i:i + m] += 0.7 * seg[:k]
    return np.stack([left, right], axis=1)


def render_pad(rate):
    n = int(DURATION * rate)
    out = np.zeros(n)
    start, dur, freqs = PAD_CHORD
    i = int(round(start * rate))
    m = int(round(dur * rate))
    tt = np.arange(m) / rate
    seg = sum(0.2 * np.sin(2 * np.pi * f * tt) for f in freqs) * envelope(m, rate, 0.500, 0.3)
    out[i:i + m] += seg[: max(0, min(m, n - i))]
    return out


def wav_bytes(samples, rate, fmt):
    """samples: (frames,) or (frames, channels) float in [-1, 1]. fmt: 'f32' | 's24' | 's16'."""
    data = np.asarray(samples, dtype=np.float64)
    if data.ndim == 1:
        data = data[:, None]
    channels = data.shape[1]
    if fmt == "f32":
        payload = data.astype("<f4").tobytes()
        tag, bits = 3, 32
    elif fmt == "s16":
        payload = np.round(np.clip(data, -1, 1) * 32767).astype("<i2").tobytes()
        tag, bits = 1, 16
    elif fmt == "s24":
        ints = np.round(np.clip(data, -1, 1) * 8388607).astype("<i4")
        raw = ints.reshape(-1).view(np.uint8).reshape(-1, 4)[:, :3]
        payload = raw.tobytes()
        tag, bits = 1, 24
    else:
        raise ValueError(fmt)
    block = channels * bits // 8
    fmt_chunk = struct.pack("<HHIIHH", tag, channels, rate, rate * block, block, bits)
    chunks = b"fmt " + struct.pack("<I", len(fmt_chunk)) + fmt_chunk
    if tag == 3:
        chunks += b"fact" + struct.pack("<II", 4, data.shape[0])
    chunks += b"data" + struct.pack("<I", len(payload)) + payload
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


def pan_gains(pan):
    angle = (pan + 1.0) * np.pi / 4.0
    return np.cos(angle), np.sin(angle)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    if np is None:
        (out / "fixture.json").write_text(json.dumps({"unavailable": f"numpy is not installed for {sys.executable} "
                                                                     "(tools/ci/requirements.txt)"}) + "\n")
        print(f"make_stem_fixture: numpy missing for {sys.executable}; wrote an 'unavailable' marker")
        return
    rng = np.random.Generator(np.random.PCG64(SEED))

    drums = render_drums(RATE, rng)
    bass = render_bass(RATE)
    synth = render_synth(RATE)
    pad = render_pad(RATE)

    mix = np.zeros((len(drums), 2))
    for name, mono in (("drums", drums), ("bass", bass), ("pad", pad)):
        gl, gr = pan_gains(MIX[name]["pan"])
        mix[:, 0] += MIX[name]["gain"] * gl * mono
        mix[:, 1] += MIX[name]["gain"] * gr * mono
    mix += MIX["synth"]["gain"] * synth
    peak = float(np.max(np.abs(mix)))
    if peak > 0.98:
        sys.exit(f"make_stem_fixture: the mix clips (peak {peak:.3f}); lower the gains")

    files = {}

    def write(name, samples, rate, fmt):
        if float(np.max(np.abs(samples))) > 0.999:
            sys.exit(f"make_stem_fixture: {name} clips (peak {float(np.max(np.abs(samples))):.3f})")
        blob = wav_bytes(samples, rate, fmt)
        (out / name).write_bytes(blob)
        frames = samples.shape[0]
        channels = 1 if np.ndim(samples) == 1 else samples.shape[1]
        files[name] = {"rate": rate, "channels": channels, "frames": frames, "format": fmt,
                       "sha256": hashlib.sha256(blob).hexdigest()}

    write("mix.wav", mix, RATE, "f32")
    write("drums.wav", drums, RATE, "f32")
    write("bass.wav", bass, RATE, "s16")
    write("synth.wav", synth, RATE, "s24")
    write("pad.wav", pad, RATE, "s16")
    # Variants.
    late = 0.25
    write("bass_late.wav", np.concatenate([np.zeros(int(late * RATE)), bass])[: len(bass)], RATE, "s16")
    rate44 = 44100
    write("synth_44k1.wav", render_synth(rate44), rate44, "f32")
    write("drums_short.wav", drums[: RATE * 1], RATE, "f32")
    write("silent.wav", np.zeros(len(drums)), RATE, "f32")
    # A WAV whose data chunk claims bytes it does not have, and whose header is cut mid-fmt: undecodable.
    (out / "corrupt.wav").write_bytes(wav_bytes(drums[:4800], RATE, "f32")[:30])
    files["corrupt.wav"] = {"undecodable": True}

    kicks, snares, hats = drum_events()
    meta = {
        "generator": "tools/make_stem_fixture.py",
        "seed": SEED,
        "rate": RATE,
        "durationSeconds": DURATION,
        "bpm": BPM,
        "mix": {"file": "mix.wav", "gains": MIX, "panLaw": "constant power, angle = (pan + 1) * pi / 4"},
        "stems": {
            "drums": {"file": "drums.wav", "kicks": kicks, "snares": snares, "hats": hats, "silent": [[8.0, 10.0]]},
            "bass": {"file": "bass.wav", "notes": [{"start": s, "duration": d, "hz": h} for s, d, h in BASS_NOTES],
                     "attackSeconds": 0.010, "silent": [[12.0, 14.0]]},
            "synth": {"file": "synth.wav", "plucks": [{"start": s, "hz": h} for s, h in SYNTH_PLUCKS],
                      "sustains": [{"start": s, "duration": d, "hz": h} for s, d, h in SYNTH_SUSTAINS],
                      "pluckAttackSeconds": 0.002, "sustainAttackSeconds": 0.300},
            "pad": {"file": "pad.wav", "sustains": [{"start": PAD_CHORD[0], "duration": PAD_CHORD[1], "hz": PAD_CHORD[2]}],
                    "attackSeconds": 0.500},
        },
        "variants": {
            "bass_late.wav": {"of": "bass", "lateBySeconds": late, "alignOffsetSeconds": -late},
            "synth_44k1.wav": {"of": "synth", "rate": rate44},
            "drums_short.wav": {"of": "drums", "durationSeconds": 1.0},
            "silent.wav": {"silent": True},
            "corrupt.wav": {"undecodable": True},
        },
        "files": files,
    }
    (out / "fixture.json").write_text(json.dumps(meta, indent=1) + "\n")
    print(f"make_stem_fixture: {len(files)} files in {out} (mix peak {peak:.3f})")


if __name__ == "__main__":
    main()

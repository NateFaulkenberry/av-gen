#!/usr/bin/env python3
"""A drum fixture from REAL one-shots (ADR-1726), for the drums-role classifier (kick/snare/hat).

    python3 tools/audio_lab/make_drum_fixture.py --out <fixtures-dir> [--samples assets/audio/drum-samples]

The one-shots are commercial (the owner's ~/Samples, copied into assets/audio/drum-samples/, which ignores itself):
they are never committed, cached or uploaded. Without them the script writes an `unavailable` marker and the lab and
its test SKIP, saying why. The manifest records each one-shot's file name, its path under ~/Samples and its sha256,
so the fixture can be rebuilt on a machine that has the packs.

Layout (120 BPM grid from 0.5 s), every hit at a known sample, one drums-role stem (the mix is that stem):
  1. each one-shot ALONE, four times, 0.5 s apart: the clean class test (kick, snare, hat; acoustic and electronic);
  2. a groove with no stacking: kick on 1 and 3, snare on 2 and 4, closed hat on the off-8ths;
  3. the same groove with the snare STACKED on a kick on 2 and 4 (one attack, two classes).
"""

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

try:
    import numpy as np
except ImportError:
    np = None

sys.path.insert(0, str(Path(__file__).resolve().parent))

RATE = 48000
REPO = Path(__file__).resolve().parents[2]
# (file, class, kit, path under ~/Samples)
ONE_SHOTS = [
    ("20 Inch Kick4.wav", "kick", "acoustic", "Drum Drops/Modern Funk Kit One Shots/24 Bit WAV Files/20 Inch Kick4.wav"),
    ("Birch Snare 12 Inch4.wav", "snare", "acoustic",
     "Drum Drops/Modern Funk Kit One Shots/24 Bit WAV Files/Birch Snare 12 Inch4.wav"),
    ("Supraphonic 400 Snare4.wav", "snare", "acoustic",
     "Drum Drops/Modern Funk Kit One Shots/24 Bit WAV Files/Supraphonic 400 Snare4.wav"),
    ("Prototype Dry Hihat Closed3.wav", "hat", "acoustic",
     "Drum Drops/Modern Funk Kit One Shots/24 Bit WAV Files/Prototype Dry Hihat Closed3.wav"),
    ("Flat Hats 14 Inch Closed3.wav", "hat", "acoustic",
     "Drum Drops/Modern Funk Kit One Shots/24 Bit WAV Files/Flat Hats 14 Inch Closed3.wav"),
    ("SD_Kick_Activity.wav", "kick", "electronic",
     "Slate Digital Samples/SD_HipHop_SamplePack/Samples/Drums/One Shots/SD Trap Drum One Shots/Kicks/SD_Kick_Activity.wav"),
    ("SD_Snare_Aloof.wav", "snare", "electronic",
     "Slate Digital Samples/SD_HipHop_SamplePack/Samples/Drums/One Shots/SD Trap Drum One Shots/Snares/SD_Snare_Aloof.wav"),
    ("SD_Snare_Evoke.wav", "snare", "electronic",
     "Slate Digital Samples/SD_HipHop_SamplePack/Samples/Drums/One Shots/SD Trap Drum One Shots/Snares/SD_Snare_Evoke.wav"),
    ("SD_ClHat_Bombshell.wav", "hat", "electronic",
     "Slate Digital Samples/SD_HipHop_SamplePack/Samples/Drums/One Shots/SD Trap Drum One Shots/Hats/Closed/SD_ClHat_Bombshell.wav"),
]


def read_wav(path):
    """PCM 16/24/32-bit or float32 WAV -> mono float64 at its own rate (no resampling here)."""
    raw = path.read_bytes()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV")
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(raw):
        cid, size = raw[pos:pos + 4], struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, ch, rate, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            if tag == 0xFFFE and len(body) >= 26:
                tag = struct.unpack("<H", body[24:26])[0]
            fmt = (tag, ch, rate, bits)
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    tag, ch, rate, bits = fmt
    if tag == 3 and bits == 32:
        x = np.frombuffer(data, dtype="<f4").astype(np.float64)
    elif bits == 16:
        x = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    elif bits == 24:
        b = np.frombuffer(data[: len(data) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v.astype(np.float64) / float(1 << 23)
    elif bits == 32:
        x = np.frombuffer(data, dtype="<i4").astype(np.float64) / float(1 << 31)
    else:
        raise ValueError(f"{path}: {bits}-bit format {tag} not handled")
    x = x[: len(x) // ch * ch].reshape(-1, ch).mean(axis=1)
    return x, rate


def resample(x, src, dst):
    if src == dst:
        return x
    n = int(round(len(x) * dst / src))
    return np.interp(np.arange(n) * src / dst, np.arange(len(x)), x)


def main():
    import make_fixtures as mf  # the WAV writer, the register measure, the silence measure

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--samples", type=Path, default=REPO / "assets" / "audio" / "drum-samples")
    args = ap.parse_args()
    d = args.out / "drums-real"
    d.mkdir(parents=True, exist_ok=True)
    missing = [f for f, *_ in ONE_SHOTS if not (args.samples / f).exists()]
    if np is None or missing:
        why = "numpy missing" if np is None else f"one-shots missing in {args.samples}: {missing}"
        (d / "manifest.json").write_text(json.dumps({"unavailable": why}) + "\n")
        print(f"make_drum_fixture: {why}; wrote an 'unavailable' marker")
        return
    shots = {}
    sources = []
    for f, cls, kit, under in ONE_SHOTS:
        p = args.samples / f
        x, rate = read_wav(p)
        x = resample(x, rate, RATE)
        x = x / max(1e-9, float(np.max(np.abs(x)))) * 0.7  # each shot peaks at -3 dBFS
        # The attack is where the shot first passes 1% of its peak; everything before it is trimmed.
        start = int(np.argmax(np.abs(x) > 0.007))
        x = x[start:start + int(0.45 * RATE)]
        if len(x) > 0.01 * RATE:
            x[-int(0.01 * RATE):] *= np.linspace(1.0, 0.0, int(0.01 * RATE))
        shots[f] = (x, cls, kit)
        sources.append({"file": f, "class": cls, "kit": kit, "samplesPath": "~/Samples/" + under,
                        "sha256": hashlib.sha256(p.read_bytes()).hexdigest(), "sourceRate": rate})
    duration = 0.5 + 4 * 0.5 * len(ONE_SHOTS) + 16 * 0.5 * 2 + 1.5
    n = int(round(duration * RATE))
    track = np.zeros(n)
    events = []

    def put(t, f, section, stacked=False):
        x, cls, kit = shots[f]
        i = int(round(t * RATE))
        k = min(len(x), n - i)
        track[i:i + k] += x[:k]
        shares = mf.register_shares(x[:k])
        events.append({"source": "drums", "kind": cls, "cls": cls, "kit": kit, "oneShot": f, "section": section,
                       "register": max(shares, key=shares.get), "registerShares": shares,
                       "registers": [b for b, v in shares.items() if v >= 0.10], "sample": i, "time": i / RATE,
                       "durationSamples": int(k), "onset": True, "ambiguous": stacked,
                       **({"note": "stacked on a kick: one attack, two classes"} if stacked else {})})

    t = 0.5
    for f, *_ in ONE_SHOTS:
        for _ in range(4):
            put(t, f, "alone")
            t += 0.5
    sections = [{"start": 0.5, "end": t, "label": "each one-shot alone"}]
    beat = 0.5
    for stacked, label in ((False, "groove, unstacked"), (True, "groove, snare stacked on kick")):
        s0 = t
        for b in range(16):
            tb = t + b * beat
            kit = "acoustic" if b < 8 else "electronic"
            kick = "20 Inch Kick4.wav" if kit == "acoustic" else "SD_Kick_Activity.wav"
            sn = "Birch Snare 12 Inch4.wav" if kit == "acoustic" else "SD_Snare_Aloof.wav"
            hat = "Prototype Dry Hihat Closed3.wav" if kit == "acoustic" else "SD_ClHat_Bombshell.wav"
            if b % 2 == 0:
                put(tb, kick, label)
            else:
                if stacked:
                    put(tb, kick, label, stacked=True)
                put(tb, sn, label, stacked=stacked)
            put(tb + beat / 2, hat, label)
        t += 16 * beat
        sections.append({"start": s0, "end": t, "label": label})
    peak = float(np.max(np.abs(track)))
    track *= 0.9 / max(peak, 0.9)  # stacked hits sum: keep the stem below full scale
    for e in events:
        e["peak"] = None
    events.sort(key=lambda e: (e["sample"], e["kind"]))
    for i, e in enumerate(events):
        e["id"] = i
    mix = np.stack([track, track], axis=1) * np.cos(np.pi / 4)
    files = {}
    for name, samples in (("mix.wav", mix), ("drums.wav", track)):
        blob = mf.wav_bytes(samples)
        (d / name).write_bytes(blob)
        files[name] = {"rate": RATE, "frames": int(samples.shape[0]), "format": "f32",
                       "sha256": hashlib.sha256(blob).hexdigest()}
    manifest = {
        "format": "avgen-audio-lab-fixture", "version": 1, "generator": "tools/audio_lab/make_drum_fixture.py",
        "name": "drums-real", "category": "drums-real",
        "purpose": "the drums-role classifier on real one-shots (acoustic and electronic): alone, grooved, stacked",
        "seed": 0, "rate": RATE, "channels": 2, "channelLayout": "stereo (L, R)", "durationSeconds": duration,
        "frames": n, "mixPeak": round(float(np.max(np.abs(mix))), 6),
        "timing": "event 'sample' is where the one-shot was placed after trimming it to its first sample over 1% "
                  "of its peak", "oneShots": sources,
        "licence": "commercial packs from the owner's ~/Samples: never commit, cache or upload the audio",
        "sources": {"drums": {"role": "drums", "gain": 1.0, "pan": 0.0, "peak": round(float(np.max(np.abs(track))), 6),
                              "silent": mf.silent_intervals(np.stack([track, track], axis=1)), "file": "drums.wav"}},
        "stems": True, "events": events, "beats": None, "sections": sections, "silent": mf.silent_intervals(mix),
        "notes": ["the mix is the drum stem itself (both channels)"], "files": files,
    }
    (d / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    index = args.out / "index.json"
    idx = json.loads(index.read_text()) if index.exists() else {"format": "avgen-audio-lab-index", "fixtures": []}
    idx["fixtures"] = [f for f in idx.get("fixtures", []) if f.get("name") != "drums-real"]
    idx["fixtures"].append({"name": "drums-real", "category": "drums-real", "dir": "drums-real",
                            "events": len(events), "durationSeconds": duration, "sha256": files["mix.wav"]["sha256"]})
    index.write_text(json.dumps(idx, indent=1) + "\n")
    print(f"make_drum_fixture: {len(events)} hits, {duration:g} s -> {d}")


if __name__ == "__main__":
    main()

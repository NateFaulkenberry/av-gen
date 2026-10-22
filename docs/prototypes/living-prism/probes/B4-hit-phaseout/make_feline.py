#!/usr/bin/env python3
"""Living Prism probe B4: the mix kick/snare/hat detectors against a drum stem's, on real music.

usage: make_feline.py <repo root> <out dir>

Writes feline.json into OUT (never the repository): Feline Footwear as the mix, its drum stem (local, gitignored, never
committed) with the hits tier on, a `sonic` block, and an envelope source on every hit detector the study compares.
There is no ground truth for a commercial track, so the drum stem's own classifier (stem.drums.kick/snare/hat, which on
the synthetic kit is 36/36, 17/17 with no confusions) is the REFERENCE the mix detectors are scored against, and
stem.drums.onset says when a drum hit happened at all.
"""
import json
import sys
from pathlib import Path

repo = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
detectors = {
    "rKick": "response.kick", "rSnare": "response.snare", "rHat": "response.hat", "rLow": "response.low",
    "rOnset": "response.onset", "aOnset": "audio.onset", "aLow": "audio.onsetLow", "aMid": "audio.onsetMid",
    "aHigh": "audio.onsetHigh", "dKick": "stem.drums.kick", "dSnare": "stem.drums.snare", "dHat": "stem.drums.hat",
    "dOnset": "stem.drums.onset", "beatPulse": "beat.pulse",
}
params = {}
for k in detectors:
    p = f"sources/{k}/"
    params.update({p + "attackMs": 0.0, p + "decayMs": 0.0, p + "sustain": 1.0, p + "holdMs": 0.0,
                   p + "releaseMs": 60.0})
project = {
    "format": "avgen-project", "version": 4,
    "assets": {"scene": {"kind": "orb"}, "audio": str(repo / "assets/audio/feline-footwear.wav"),
               "stems": [{"key": "drums", "label": "Drums", "hits": True,
                          "file": str(repo / "assets/audio/stems/feline-footwear (Drums).wav")}]},
    "sonic": {}, "parameters": params, "routes": [],
    "sources": [{"kind": "envelope", "name": k, "settings": {"trigger": v}} for k, v in detectors.items()],
}
(out / "feline.json").write_text(json.dumps(project, indent=1))
print("wrote", out / "feline.json")

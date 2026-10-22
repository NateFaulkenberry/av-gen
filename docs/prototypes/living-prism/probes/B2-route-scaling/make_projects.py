#!/usr/bin/env python3
"""Living Prism probe B2: what N simultaneous modulation routes cost per frame on the CPU.

Writes projects routes-<N>.json into OUT_DIR (argument 1; never the repository), each the B1 fixture's mix and
stems (argument 2: the B1 fixture directory) on the orb scene with N routes. The routes rotate through four chain
shapes Rainbow World actually uses (a smoothed continuous level, a latched onset envelope, a trend detector with a
peak hold and a spring, a plain stateless remap) and spread over the orb's six parameters, so the cost measured is
the chain evaluation, not the target.

Run each through `avgen --project routes-<N>.json --sonic-trace out.csv --fps 60`; the summary line's "update"
figure is Engine::update per frame. The slope over N is the per-route cost.
"""
import json
import sys
from pathlib import Path

OUT = Path(sys.argv[1]).resolve()
FIX = Path(sys.argv[2]).resolve()
OUT.mkdir(parents=True, exist_ok=True)

chains = [
    ("stem.bass.level", {"gain": 2.4, "offset": -1.45, "clampEnabled": True, "attackMs": 10.0, "decayMs": 180.0}),
    ("stem.bass.onset", {"envelope": "linearfall", "envelopeFallPerSecond": 2.4, "envelopeRefractoryMs": 130.0,
                         "envelopeRearm": 0.05}),
    ("stem.drums.rms", {"trendSeconds": 8.0, "trendFastMs": 450.0, "trendFloor": 0.06, "threshold": "binary",
                        "thresholdLevel": 1.25, "decayMs": 1500.0, "envelopeRefractoryMs": 12000.0,
                        "envelopeRearm": 0.3, "envelope": "peakhold", "envelopeHoldMs": 6500.0,
                        "envelopeFallPerSecond": 0.33, "springHz": 0.5, "springDamping": 1.0}),
    ("response.level", {"remapEnabled": True, "remapOutMin": 0.2, "remapOutMax": 0.9}),
]
targets = ["orb/scale", "orb/emissive", "orb/rotationSpeed", "orb/impulse", "orb/baseColor", "orb/emissiveColor"]

for n in (0, 250, 1000, 4000):
    routes = []
    for i in range(n):
        src, chain = chains[i % len(chains)]
        routes.append({"source": src, "target": targets[i % len(targets)], "amount": 0.001, "op": "add",
                       "chain": chain})
    project = {
        "format": "avgen-project", "version": 4,
        "assets": {"scene": {"kind": "orb"}, "audio": str(FIX / "mix.wav"),
                   "stems": [{"key": "drums", "label": "Drums", "file": str(FIX / "drums.wav"), "hits": True},
                             {"key": "bass", "label": "Bass", "file": str(FIX / "bass.wav")}]},
        "sonic": {}, "sources": [], "parameters": {}, "routes": routes,
    }
    (OUT / f"routes-{n}.json").write_text(json.dumps(project))
print("wrote", OUT)

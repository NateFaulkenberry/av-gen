#!/usr/bin/env python3
"""Living Prism probe B3: end-to-end traces on Rainbow World's LIVE project, played from a file.

usage: make_variants.py <repo root> <out dir>

Writes two copies of examples/rainbow-world/rainbow-world.json into OUT (never the repository):
  rw-mix.json    + assets.audio = Feline Footwear: no stems, so every route takes its response.* fallback, i.e. what the
                 live scene does on a plain stereo input;
  rw-stems.json  + the three Feline stems (local, gitignored assets; never committed), so the stem routes run.
Both add probe knobs (macro.p_*) fed by copies of chosen production routes, because --sonic-trace has no parameter
finals: a knob publishes its route's final one frame later, exactly (probe B1), so macro.p_x(k) = that route's final
at frame k-1.
"""
import copy
import json
import sys
from pathlib import Path

repo = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
base = json.loads((repo / "examples/rainbow-world/rainbow-world.json").read_text())
scene = repo / "examples/rainbow-world" / base["assets"]["scene"]["path"]
audio = repo / "assets/audio/feline-footwear.wav"
stems = {k: repo / f"assets/audio/stems/feline-footwear ({k.capitalize()}).wav" for k in ("drums", "bass", "synth")}

def probe_routes(routes):
    """Copies of production routes into probe knobs (amount normalised to 1 so the knob reads the chain)."""
    want = {
        # trace 1: the clap/snare bump on the arches (live fallback: response.snareEnv; stems: stem.drums.highMid)
        ("response.snareEnv", "material/rw_rainbow/op/9/constant/constant"): "p_snareArch",
        ("stem.drums.highMid", "material/rw_rainbow/op/9/constant/constant"): "p_clapArchStem",
    }
    extra, knobs = [], []
    for r in routes:
        k = want.get((r["source"], r["target"]))
        if k and k not in knobs:
            p = copy.deepcopy(r)
            p["target"] = f"macros/{k}"
            p["amount"] = 1.0
            extra.append(p)
            knobs.append(k)
    return extra, knobs

for name, with_stems in (("rw-mix", False), ("rw-stems", True)):
    d = copy.deepcopy(base)
    d["assets"]["scene"]["path"] = str(scene)
    d["assets"]["audio"] = {"path": str(audio)}
    if with_stems:
        d["assets"]["stems"] = [{"key": k, "label": k.capitalize(), "file": str(p)} for k, p in stems.items()]
    extra, knobs = probe_routes(d["routes"])
    d["routes"] += extra
    for s in d["sources"]:
        if s["kind"] == "macro":
            s["settings"].setdefault("knobs", [])
            s["settings"]["knobs"] += [{"name": k, "default": 0.0} for k in knobs]
    d["sources"].append({"kind": "envelope", "name": "snareEv", "settings": {"trigger": "response.snare"}})
    (out / f"{name}.json").write_text(json.dumps(d))
    print("wrote", out / f"{name}.json", "probe knobs", knobs)

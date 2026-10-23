#!/usr/bin/env python3
"""Living Prism probe B5: source roles (ADR-1630), the history from a source (ADR-1631) and the stem-set check
(ADR-1632), on Feline Footwear and its stems.

usage: make_projects.py <repo root> <out dir> <before|after> <drum_signals.py>

Writes probe projects into OUT (never the repository). Each plays the mix, `assets/audio/feline-footwear.wav`; the stems
(local, gitignored, never committed) are analysis only. `drum_signals.py` is the generator's own module: main's
(`git show main:tools/drum_signals.py`, reading `stem.drums.kick`) for `before`, the branch's (`role.drums.kick`) for
`after`, so each side runs the drum signals its own scenes would.

  before (main 38486d55: `hits` per stem, no roles)
    all-hits     drums, bass and synth stems, each with "hits": true: what the class names do on a bass or synth stem
    drums-plain  the three stems with no hits flag: the silent-kick trap (signal.kick picks an active stem.drums)
    mix          no stems
  after (eng/stem-roles)
    roles        the three stems with roles drums / bass / synth and nothing else
    history      `roles` plus "feeds": {"history": "role.drums"}: the history's onset rows from the drum stem
    silent       `roles` with the drum stem replaced by a silent file of the same length (`silent-drums.wav`, made here)
    mix          no stems
    offset       `roles` with the bass stem's offset set 25 ms late: the stem-set check must warn and name 0.0000 s

Every project carries a `sonic` block, the drum and level signals it reads, and an envelope source on each event the
analysis compares (probe B1: an env source publishes one frame after its trigger; analyse.py subtracts it).
"""
import importlib.util
import json
import sys
import wave
from pathlib import Path

repo = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
side = sys.argv[3]
spec = importlib.util.spec_from_file_location("drum_signals", sys.argv[4])
ds = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ds)
out.mkdir(parents=True, exist_ok=True)

mix = repo / "assets/audio/feline-footwear.wav"
stem = {k: repo / f"assets/audio/stems/feline-footwear ({k.title()}).wav" for k in ("drums", "bass", "synth")}


def silent_like(src, dst):
    with wave.open(str(src)) as w:
        params = w.getparams()
    with wave.open(str(dst), "wb") as o:
        o.setparams(params)
        frame = b"\0" * (params.sampwidth * params.nchannels)
        chunk = frame * 48000
        left = params.nframes
        while left > 0:
            n = min(left, 48000)
            o.writeframes(chunk[: n * len(frame)])
            left -= n


def project(stems, feeds=None, events=None):
    events = dict(events or {})
    params = {}
    for k in events:
        p = f"sources/{k}/"
        params.update({p + "attackMs": 0.0, p + "decayMs": 0.0, p + "sustain": 1.0, p + "holdMs": 0.0,
                       p + "releaseMs": 60.0})
    names = ({"kick", "bassLevel", "synthLevel"} & set(ds.NAMES)) if hasattr(ds, "NAMES") else {"kick"}
    doc = {"format": "avgen-project", "version": 4,
           "assets": {"scene": {"kind": "orb"}, "audio": str(mix)},
           "sonic": {}, "parameters": params, "routes": [],
           "sources": ds.sources(names) +
                      [{"kind": "envelope", "name": k, "settings": {"trigger": v}} for k, v in events.items()]}
    if stems:
        doc["assets"]["stems"] = stems
    if feeds:
        doc["feeds"] = feeds
    return doc


common = {"sigKick": "signal.kick", "rSnare": "response.snare", "rHat": "response.hat", "rOnset": "response.onset"}
per_stem = {}
for k, p in (("drums", "d"), ("bass", "b"), ("synth", "y")):
    for c in ("kick", "snare", "hat", "onset"):
        per_stem[f"{p}{c.title()}"] = f"stem.{k}.{c}"

if side == "before":
    def st(k, hits):
        d = {"key": k, "label": k.title(), "file": str(stem[k])}
        if hits:
            d["hits"] = True
        return d
    docs = {
        "all-hits": project([st(k, True) for k in stem], events={**common, **per_stem}),
        "drums-plain": project([st(k, False) for k in stem], events={**common, **per_stem}),
        "mix": project([], events=common),
    }
else:
    def st(k, file=None, offset=0.0):
        d = {"key": k, "label": k.title(), "file": str(file or stem[k]), "role": k}
        if offset:
            d["offset"] = offset
        return d
    silent = out / "silent-drums.wav"
    if not silent.exists():
        silent_like(stem["drums"], silent)
    roles = {**per_stem, "adKick": "role.drums.kick", "adSnare": "role.drums.snare", "adHat": "role.drums.hat",
             "adOnset": "role.drums.onset", "abOnset": "role.bass.onset", "ayOnset": "role.synth.onset"}
    three = [st(k) for k in stem]
    docs = {
        "roles": project(three, events={**common, **roles}),
        "history": project(three, feeds={"history": "role.drums"}, events={**common, **roles}),
        "silent": project([st("drums", file=silent), st("bass"), st("synth")], events={**common, **roles}),
        "mix": project([], events=common),
        "offset": project([st("drums"), st("bass", offset=0.025), st("synth")], events=common),
    }
for name, doc in docs.items():
    (out / f"{name}.json").write_text(json.dumps(doc, indent=1))
    print("wrote", out / f"{name}.json")

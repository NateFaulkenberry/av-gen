"""Aurora Borealis: the instrument -- the songs, their stems, the conditioned signals and the routes.

Pass 3 (docs/prototypes/aurora-borealis/20-brief-sabertooth.md, 21-research-pass3-audio.md, 23-plan-pass3.md): each
instrument has its own place in the frame.

  sky     Cloud Diving   signal.cloudSwell (300/1200 ms) and signal.cloudHue (1500/4000 ms) -> the aurora
          Crash          signal.crashStarEv -> a fast shooting star (starA / starB, a pool of two) + signal.crashGlow
  left    Moog           signal.moogNote (rms rise x1.6, 150 ms latch) + signal.moogGlow -> the left bank's glints
  river   Kick           signal.kickBeamEv -> the amber river beam (riverKick)
          Snare          signal.snareBeamEv -> the cyan twin beam (riverSnare)
  right   Toms           signal.tomHit (sized) + signal.tomBody -> plane flashes on the right ranges
          Tone block     signal.blockTick (sized) -> small sharp glints stepping through a one-bar sequence

Every transient is `stem.<key>.onset` (or the Moog's rms rise) into a latch with attack 0 and its own refractory: no
classifier, no `macro.*`, no route chain. An effect trigger reads an EVENT signal, and a signal's event is its input's
raw event (conditioned_signal.cpp), so each refractory is applied by gating the raw onset with its latch's first frames
(`<name>On`): `<name>Ev` fires once per accepted hit. Transients are stem-only: a mix alone fires no stars, beams or
glints; the aurora keeps a mix fallback.

Songs: Sabertooth (the default, `aurora-borealis.json`) reads its nine stems by key; Feline Footwear
(`aurora-borealis-feline.json`) drives the same signals from its three role stems where it has the instrument
(synth -> the aurora, the drum classes -> the beams, bass notes -> the left glints) and leaves the rest at rest.

Isolation: one control channel per instrument rests at 1 (ADR-1612) and scales every input of its part, CC 21-27.
`--sonic-trace --trace-finals` verifies it; nothing is drawn.
"""
from __future__ import annotations

import look

PARTS = ["cloud", "crash", "moog", "kick", "snare", "toms", "toneblock"]
CC = {p: 21 + i for i, p in enumerate(PARTS)}
UNIT = {"clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0}

STEMS = "../../assets/audio/stems/"
SONGS = {
    "sabertooth": {
        "mix": STEMS + "sabertooth/Sabertooth.wav",
        # (key, label, role, file): roles per 23-plan-pass3.md -- Kick keeps `drums` (the drum signals still resolve),
        # Cloud Diving `synth`, BASS `bass`, the rest `other`; every signal reads its stem by key
        "stems": [(k, label, role, "sabertooth/" + f) for k, label, role, f in [
            ("kick", "Kick", "drums", "Kick.wav"), ("snare", "Snare", "other", "Snare.wav"),
            ("toms", "Toms", "other", "Toms.wav"), ("crash", "Crash", "other", "Crash.wav"),
            ("hh", "HH", "other", "HH.wav"), ("bass", "Bass", "bass", "BASS.wav"),
            ("cloud", "Cloud Diving", "synth", "Cloud Diving.wav"), ("moog", "Moog", "other", "Moog.wav"),
            ("toneblock", "Tone Block", "other", "tone block.wav")]],
        "feeds": {"beat": "stem.kick"},
        # instrument -> the bus prefix of its stem; None: the song has no such part
        "src": {"cloud": "stem.cloud", "crash": "stem.crash", "moog": "stem.moog", "kick": "stem.kick",
                "snare": "stem.snare", "toms": "stem.toms", "toneblock": "stem.toneblock"},
        "onset": {},
        "cloudGain": 4.0,      # A s3.6: rms p95 0.19, max 0.24
        "moogRise": True,
    },
    "feline": {
        "mix": "../../assets/audio/feline-footwear.wav",
        "stems": [(k, label, k, "feline-footwear (%s).wav" % label)
                  for k, label in [("drums", "Drums"), ("bass", "Bass"), ("synth", "Synth")]],
        "feeds": {"beat": "role.drums", "hits": "role.drums"},
        "src": {"cloud": "stem.synth", "crash": None, "moog": "stem.bass", "kick": "stem.drums", "snare": "stem.drums",
                "toms": None, "toneblock": None},
        # one drum stem: its classes stand in for the separate kick and snare stems
        "onset": {"kick": "role.drums.kick", "snare": "role.drums.snare"},
        "cloudGain": 8.0,      # pass 1's synth gain (the Feline synth is quieter)
        "moogRise": False,     # a bass line: its onsets are its notes
    },
}


def inp(source, chain=None, amount=1.0, part=None, **extra):
    d = {"source": source}
    if chain:
        d["chain"] = dict(chain)
    if amount != 1.0:
        d["amount"] = float(amount)
    if part and "depthSource" not in extra:
        d.update(depthSource="control." + part, depthMin=0.0, depthMax=1.0)   # the part's knob, resting at 1
    d.update(extra)
    return d


def sig(name, inputs, combine="firstActive", chain=None, event=False):
    s = {"combine": combine, "inputs": inputs or [{"constant": 0.0}]}   # a part the song lacks rests at 0
    if chain:
        s["chain"] = chain
    if event:
        s["event"] = True
    return {"kind": "signal", "name": name, "settings": s}


def latch(refr_ms, life_s, rearm=0.05):
    """An event latch: 1 on the event's own frame (attack 0), a linear fall over `life_s`, deaf for `refr_ms`. The binary
    threshold (before the envelope) lifts every event to 1: an onset's value is its strength, 0.05-1."""
    return {"threshold": "binary", "thresholdLevel": 0.01, "attackMs": 0.0, "envelope": "linearfall", "envelopeFallPerSecond": 1.0 / life_s,
            "envelopeRefractoryMs": float(refr_ms), "envelopeRearm": rearm}


def binary(level):
    return {"threshold": "binary", "thresholdLevel": level}


def trend(slow_s, fast_ms, floor, level):
    return {"trendSeconds": slow_s, "trendFastMs": fast_ms, "trendFloor": floor, "threshold": "binary",
            "thresholdLevel": level}


def remap(lo, hi, out_lo=0.0, out_hi=1.0):
    return {"remapEnabled": True, "remapInMin": lo, "remapInMax": hi, "remapOutMin": out_lo, "remapOutMax": out_hi,
            "clampEnabled": True, "clampMin": out_lo, "clampMax": out_hi}


def size_hold():
    """A hit's size, from the stem's peak, held through the hit (a later, softer frame cannot lower it)."""
    return {"envelope": "peakhold", "envelopeHoldMs": 300.0, "envelopeFallPerSecond": 4.0}


def onset_event(name, onset, refr_ms, life_s, part, size=None):
    """One transient instrument as three signals: `name`, the accent's envelope (a latch: attack 0, refractory, a linear
    fall over life_s, scaled by the part's knob and by `size`); `nameOn`, the latch's own first frames (it is at 1 on
    its event frame and above 0.9 for a tenth of its life, well inside the refractory); `nameEv`, the raw onset gated by
    them: an event that respects the refractory, for effect triggers and random picks."""
    if not onset:
        return [sig(name + "Latch", []), sig(name, []), sig(name + "On", []), sig(name + "Ev", [], event=True)]
    return [
        sig(name + "Latch", [inp(onset, latch(refr_ms, life_s))]),
        # the size signal already carries the part's knob
        sig(name, [inp("signal.%sLatch" % name, depthSource=size, depthMin=0.0, depthMax=1.0) if size else
                   inp("signal.%sLatch" % name, part=part)]),
        sig(name + "On", [inp("signal.%sLatch" % name, binary(0.9), part=part)]),
        sig(name + "Ev", [inp(onset, gate="signal.%sOn" % name)], event=True),
    ]


def sources(song):
    S = SONGS[song]
    src, ons = S["src"], S["onset"]

    def s(inst, field):
        return "%s.%s" % (src[inst], field) if src[inst] else None

    def onset(inst):
        return ons.get(inst) or s(inst, "onset")

    def cloud(name, a, d):
        ins = [inp(s("cloud", "rms"), dict(gain=S["cloudGain"], **UNIT, attackMs=a, decayMs=d), part="cloud")]
        # firstActive: the stem while it has data, else the mix's sustain, so the aurora still breathes on a mix alone
        ins.append(inp("response.sustain", dict(gain=2.0, offset=-0.75, **UNIT, attackMs=a, decayMs=d), part="cloud"))
        return sig(name, ins)

    out = [
        # ---- sky: Cloud Diving. Intensity follows the swell (300/1200 ms, A s3.6); the hue blends slowly (1500/4000)
        cloud("cloudSwell", 300.0, 1200.0),
        cloud("cloudHue", 1500.0, 4000.0),
    ]
    # ---- sky: Crash. 12 hits, re-hits 0.70 s apart (A s3.1): a 300 ms refractory; the glow falls over 0.4 s
    out += onset_event("crashStar", onset("crash"), 300.0, 0.4, "crash")
    out += [
        sig("crashGlow", [inp("signal.crashStar")]),
        # phrase starts peak 0.41-0.42, re-hits 0.17-0.28 (A s3.1)
        sig("crashSize", [inp(s("crash", "peak"), dict(**remap(0.15, 0.42), **size_hold()))] if src["crash"] else []),
        # the pool of two stars (B s4): A takes a crash when it is free (a star lives 1.0 s; A is held 0.9 s), B one
        # that lands while A is busy -- so the newest crash always gets a star and at most two are ever alive
        sig("starA", [inp("signal.crashStarEv", latch(900.0, 0.9))]),
        sig("starAFresh", [inp("signal.starA", binary(0.95))]),
        sig("starABusy", [inp("signal.starA", binary(0.01)), inp("signal.starAFresh", amount=-1.0)], combine="sum",
            chain=binary(0.5)),
        sig("starAEv", [inp("signal.crashStarEv", gate="signal.starAFresh")], event=True),
        sig("starBEv", [inp("signal.crashStarEv", gate="signal.starABusy")], event=True),
    ]
    # ---- river: Kick and Snare (A s3.2): the onset with a 100 ms refractory (the snare double-fires on 40 of 48)
    out += onset_event("kickBeam", onset("kick"), 100.0, 0.25, "kick")
    out += onset_event("snareBeam", onset("snare"), 100.0, 0.25, "snare")
    # ---- left: Moog. The re-bounced part (19:42) is 23 long notes, each a hard attack and a slow decay, at least 1.04 s
    # apart: the rms rise x1.6 (A s3.5) catches all 23 at -3.3 ms; a 300 ms refractory (A's was 150) drops the three
    # re-steps 0.18-0.24 s after a note (PROGRESS, pass 3)
    if S["moogRise"]:
        out += [sig("moogRise", [inp(s("moog", "rms"), trend(0.25, 15.0, 0.003, 1.6))]),
                sig("moogNote", [inp("signal.moogRise", latch(300.0, 0.45), part="moog")])]
    else:
        out += [sig("moogNote", [inp(s("moog", "onset"), latch(150.0, 0.45), part="moog")])]
    out += [sig("moogGlow", [inp(s("moog", "level"), {"attackMs": 20.0, "decayMs": 600.0}, part="moog")])]
    # ---- right: Toms (28 notes in six fills, peak 0.15-0.41 -> size 0.4-1.3) and tone block (176 accents, peak
    # 0.34-0.55 -> size 0.5-1.0); each size scales its hit, and the part's knob scales the size
    out += [sig("tomSize", [inp(s("toms", "peak"), dict(**remap(0.15, 0.41, 0.4, 1.3), **size_hold()), part="toms")]
                if src["toms"] else [])]
    out += onset_event("tomHit", onset("toms"), 100.0, 0.5, "toms", size="signal.tomSize")
    out += [sig("tomBody", [inp(s("toms", "level"), {"attackMs": 30.0, "decayMs": 500.0}, part="toms")]
                if src["toms"] else []),
            sig("blockSize", [inp(s("toneblock", "peak"), dict(**remap(0.34, 0.55, 0.5, 1.0), **size_hold()),
                                  part="toneblock")] if src["toneblock"] else [])]
    out += onset_event("blockTick", onset("toneblock"), 80.0, 0.16, "toneblock", size="signal.blockSize")
    # ---- the light the land and river take from the aurora: the swell alone (B s3), no drum or bass share
    out += [sig("auroraLight", [inp("signal.cloudSwell", amount=0.75)], chain=UNIT)]
    # ---- subsets
    out += [
        # a new tom subset per accepted tom note (the k-th note's value is fixed by the seed: seek equals play)
        {"kind": "random", "name": "tomPick", "settings": {"trigger": "signal.tomHitEv", "seed": 17}},
        # the Moog's subset glides slowly: an rms rise has no event to pick on, and notes about 1.4 s apart sit a
        # fifth of a cycle apart, so each lights a different scattering, held (nearly) still through its glint
        {"kind": "lfo", "name": "moogDrift", "settings": {"shape": "saw"}},
        # the tone block steps through one bar: a repeated pattern lights the same sequence of facets
        {"kind": "lfo", "name": "barSaw", "settings": {"shape": "saw"}},
    ]
    return out


def lfo_parameters():
    return {"sources/moogDrift/rate": 0.137, "sources/barSaw/beatSync": True, "sources/barSaw/beatsPerCycle": 4.0}


def R(src, target, amount):
    return {"source": src, "target": target, "amount": float(amount), "op": "add", "polarity": "unipolar"}


def routes():
    A = "fx/aurora/"
    W = "nodes/%s/water/" % look.WATER_NODE
    out = [
        # ---- Cloud Diving -> the aurora (B s3): intensity, height, width, fold amplitude, inner detail, airglow
        R("signal.cloudSwell", A + "intensity", 1.6),
        R("signal.cloudSwell", A + "curtainHeight", 400.0),
        R("signal.cloudSwell", A + "ribbonWidth", 0.02),
        R("signal.cloudSwell", A + "ribbonSwirl", 0.6),
        R("signal.cloudSwell", A + "filaments", 1.5),
        R("signal.cloudSwell", A + "edgeBrightness", 1.5),
        R("signal.cloudSwell", A + "horizonGlow", 0.3),
        # ... and, slowly, the persistence of the bright regions and the colour
        R("signal.cloudHue", A + "ribbonCoverage", 0.25),
        R("signal.cloudHue", "palette/position", float(len(look.LOOKS) - 1) - look.REST_POSITION),
        R("signal.cloudSwell", "fx/stars/brightness", -0.4),
        # the light the river takes from the aurora
        R("signal.auroraLight", W + "glow", 0.5),
        # ---- Crash: the environment's response to a shooting star (B s4)
        R("signal.crashGlow", A + "intensity", 0.3),
        R("signal.crashGlow", W + "glow", 0.3),
        # a phrase-start crash is a brighter star than a re-hit on its decay
        R("signal.crashSize", "fx/starA/coreIntensity", 40.0),
        R("signal.crashSize", "fx/starB/coreIntensity", 40.0),
    ]
    out += look.music_routes(R)
    return out


def control():
    return {"channels": [{"name": p, "rest": 1.0, "pickup": True} for p in PARTS],
            "midi": {"enabled": True, "filter": "*", "bindings": [
                {"source": "*", "channel": -1, "kind": "cc", "number": CC[p], "signal": p} for p in PARTS]}}


def apply(doc, song="sabertooth"):
    """Put the instrument into a project document: the song's mix, its stems, the beat feed, the signals, the routes."""
    S = SONGS[song]
    doc["assets"]["audio"] = S["mix"]
    doc["assets"]["stems"] = [{"key": k, "label": label, "role": role, "file": STEMS + f}
                              for k, label, role, f in S["stems"]]
    doc["feeds"] = dict(S["feeds"])
    doc["sources"] = sources(song)
    doc["routes"] = routes()
    doc["control"] = control()
    doc["parameters"].update(lfo_parameters())
    doc["palette"]["position"] = look.REST_POSITION   # silence: between dusk indigo and the violet veil

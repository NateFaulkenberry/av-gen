"""Aurora Borealis: the instrument -- the songs, their stems, the conditioned signals and the routes.

Pass 3 (docs/prototypes/aurora-borealis/20-brief-sabertooth.md, 21-research-pass3-audio.md, 23-plan-pass3.md): each
instrument has its own place in the frame.

  sky     Cloud Diving   signal.cloudSwell (300/1200 ms) and signal.cloudHue (1500/4000 ms) -> the aurora
          Crash          signal.crashStarEv -> a fireball thrown over the valley (ballA / ballB, a pool of two)
  left    Bass           signal.bassStrike + signal.bassHold -> plane flashes with lines, ridgelines glowing; the
                         bands running down the range (lfo.rangeFlow) -- pass 6: the sides swapped
  river   Kick           signal.kickBeamEv -> the magenta river beam (riverKick)
          Snare          signal.snareBeamEv -> the lilac twin beam (riverSnare)
          Toms           signal.tomHitEv -> the violet beam (riverToms), each hit in its own lane
  right   Moog           signal.moogStrike (every note, sized by level) + signal.moogHold (the sustain) -> picked
                         creases as wide lines and filled planes, every ridgeline glowing; the bands running down the range
          (pass 4: the toms keep only their river lane; the tone block is unassigned -- its signals stay, unrouted)

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

PARTS = ["cloud", "crash", "moog", "kick", "snare", "toms", "toneblock", "bass"]
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
                "snare": "stem.snare", "toms": "stem.toms", "toneblock": "stem.toneblock", "bass": "stem.bass"},
        "onset": {},
        "cloudGain": 4.0,      # A s3.6: rms p95 0.19, max 0.24
        "moogRise": True,
    },
    "feline": {
        "mix": "../../assets/audio/feline-footwear.wav",
        "stems": [(k, label, k, "feline-footwear (%s).wav" % label)
                  for k, label in [("drums", "Drums"), ("bass", "Bass"), ("synth", "Synth")]],
        "feeds": {"beat": "role.drums", "hits": "role.drums"},
        "src": {"cloud": "stem.synth", "crash": None, "moog": "stem.synth", "kick": "stem.drums", "snare": "stem.drums",
                "toms": None, "toneblock": None, "bass": "stem.bass"},
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


import os as _os
SOFT = {"moog": float(_os.environ.get("AB_SOFT_MOOG", "0.9")), "bass": float(_os.environ.get("AB_SOFT_BASS", "0.9"))}


def note_signals(name, src, onset, refr_ms, life_s, size, hold_range, hold_decay_ms, soft):
    """Pass 5 (the owner: the Moog and bass "need to be more sensitive hit more notes and sustain if the moog sustains").
    Three signals per instrument, besides its `<name>Note` (the stem's onsets, picking the subset):
      `<name>Soft`    the softer notes the onset detector does not call: the stem's onset strength (its spectral flux
                      over the detector's threshold) gated at `soft` (< 1: below the detector's own line), latched on
                      each rise past it with the same refractory, sized by the stem's level
      `<name>Strike`  every note: the larger of the two
      `<name>Hold`    the sustain: the stem's level, following each note up in 5 ms and down as it decays, so while a
                      note rings the light holds and as it fades the light fades with it; 0 in silence"""
    if not onset:
        return [sig(name + "Soft", []), sig(name + "Strike", []), sig(name + "Hold", [])]
    strike_latch = {"threshold": "gate", "thresholdLevel": soft, "attackMs": 0.0, "envelope": "linearfall",
                    "envelopeFallPerSecond": soft / life_s, "envelopeRefractoryMs": float(refr_ms),
                    "envelopeRearm": 0.5 * soft, **remap(0.0, soft)}
    lo, hi = hold_range
    return [
        sig(name + "Soft", [inp(src + ".onsetStrength", strike_latch, depthSource=size, depthMin=0.0, depthMax=1.0)]),
        sig(name + "Strike", [inp("signal.%sNote" % name), inp("signal.%sSoft" % name)], combine="max"),
        sig(name + "Hold", [inp(src + ".rms", dict(remap(lo, hi), attackMs=5.0, decayMs=hold_decay_ms), part=name)]),
    ]


def sources(song):
    S = SONGS[song]
    src, ons = S["src"], S["onset"]

    def s(inst, field):
        return "%s.%s" % (src[inst], field) if src[inst] else None

    def onset(inst):
        return ons.get(inst) or s(inst, "onset")

    def cloud(name, a, d, a2=0.0, d2=0.0):
        ins = [inp(s("cloud", "rms"), dict(gain=S["cloudGain"], **UNIT, attackMs=a, decayMs=d), part="cloud")]
        # firstActive: the stem while it has data, else the mix's sustain, so the aurora still breathes on a mix alone
        ins.append(inp("response.sustain", dict(gain=2.0, offset=-0.75, **UNIT, attackMs=a, decayMs=d), part="cloud"))
        # pass 4: a second smoothing stage on the output (a2/d2): one stage only thins the stem's frame-to-frame
        # wobble, two all but remove it (the owner: "too twitchy")
        return sig(name, ins, chain={"attackMs": a2, "decayMs": d2} if a2 or d2 else None)

    out = [
        # ---- sky: Cloud Diving. Intensity follows the swell (300/1200 ms, A s3.6); the hue blends slowly (1500/4000)
        cloud("cloudSwell", 500.0, 1500.0, 250.0, 600.0),
        cloud("cloudHue", 2500.0, 5000.0, 800.0, 1600.0),
        # pass 4: the owner wanted the aurora wilder, then "ribbon animation needs smoothing ... twist and curl more,
        # feels too twitchy". The swell's energy goes into the sheets' shape -- churn (ADR-1664, a phase, so no jump),
        # swirl, fold depth, width, height -- on a slow envelope, so they curl and twist into large shapes evolving
        # over 1-2 s instead of following every wobble of the stem; the colour moves faster than the hue did, smoothly
        cloud("cloudShape", 1200.0, 3000.0, 600.0, 1200.0),
        cloud("cloudColor", 1000.0, 2500.0, 600.0, 1200.0),
    ]
    # (pass 4: the swell-start pulse up the ribbons is gone -- a jump at every swell's start, measured as the one
    # twitch left once the shape moved smoothly)
    # ---- sky: Crash. 12 hits, re-hits 0.70 s apart (A s3.1): a 300 ms refractory; the glow falls over 0.4 s
    out += onset_event("crashStar", onset("crash"), 300.0, 0.4, "crash")
    out += [
        # the environment's response rises over 0.2 s (instant, a crash on the wild aurora swung the frame by 0.1 in
        # 267 ms: the flash gate)
        sig("crashGlow", [inp("signal.crashStar")], chain={"attackMs": 200.0, "decayMs": 500.0}),
        # phrase starts peak 0.41-0.42, re-hits 0.17-0.28 (A s3.1)
        sig("crashSize", [inp(s("crash", "peak"), dict(**remap(0.15, 0.42), **size_hold()))] if src["crash"] else []),
        # the pool of two light balls (pass 4; the stars' pool before): A takes a crash when it is free (a ball lives
        # 2.0 s; A is held 1.3 s: the groups' re-hits come 1.40 and 0.70 s apart), B one that lands while A is busy
        sig("ballA", [inp("signal.crashStarEv", latch(1300.0, 1.3))]),
        sig("ballAFresh", [inp("signal.ballA", binary(0.97))]),
        sig("ballABusy", [inp("signal.ballA", binary(0.01)), inp("signal.ballAFresh", amount=-1.0)], combine="sum",
            chain=binary(0.5)),
        sig("ballAEv", [inp("signal.crashStarEv", gate="signal.ballAFresh")], event=True),
        sig("ballBEv", [inp("signal.crashStarEv", gate="signal.ballABusy")], event=True),
    ]
    # ---- river: Kick and Snare (A s3.2): the onset with a 100 ms refractory (the snare double-fires on 40 of 48)
    out += onset_event("kickBeam", onset("kick"), 100.0, 0.25, "kick")
    out += onset_event("snareBeam", onset("snare"), 100.0, 0.25, "snare")
    # ---- left: Moog. Every onset of the Moog stem (35 ms apart at most: trill notes come about 0.1 s apart) picks a
    # fresh subset (random.moogPick), sized by the stem's level at the hit; pass 5 adds the softer notes, the strike
    # (both) and the sustain (note_signals)
    out += [sig("moogSize", [inp(s("moog", "rms"), dict(**remap(0.04, 0.22, 0.35, 1.0), **size_hold()), part="moog")]
                if src["moog"] else [])]
    out += onset_event("moogNote", onset("moog"), 35.0, 0.2, "moog", size="signal.moogSize")
    out += note_signals("moog", src["moog"], onset("moog"), 35.0, 0.2, "signal.moogSize", (0.02, 0.2), 150.0,
                        SOFT["moog"])
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
    # ---- right: BASS (pass 4, the owner's final revision: "change the right side mountains response to BASS.wav";
    # pass 5: more sensitive, sustaining). Each note (the stem's onset, now 60 ms apart at most) lights a fresh set of
    # planes, sized by the stem's level; the softer notes and the sustain as the Moog's (note_signals)
    out += [sig("bassSize", [inp(s("bass", "rms"), dict(**remap(0.06, 0.3, 0.35, 1.0), **size_hold()), part="bass")]
                if src["bass"] else [])]
    out += onset_event("bassNote", onset("bass"), 60.0, 0.3, "bass", size="signal.bassSize")
    out += note_signals("bass", src["bass"], onset("bass"), 60.0, 0.3, "signal.bassSize", (0.04, 0.3), 220.0,
                        SOFT["bass"])
    # ---- the light the land and river take from the aurora: the swell alone (B s3), no drum or bass share
    out += [sig("auroraLight", [inp("signal.cloudSwell", amount=0.75)], chain=UNIT)]
    # ---- subsets
    out += [
        # a new Moog subset on every trill note (the k-th onset's value is fixed by the seed: seek equals play)
        {"kind": "random", "name": "moogPick", "settings": {"trigger": "signal.moogNoteEv", "seed": 23}},
        # a new set of right-bank planes per bass note (the k-th note's value is fixed by the seed: seek equals play)
        {"kind": "random", "name": "bassPick", "settings": {"trigger": "signal.bassNoteEv", "seed": 17}},
        # pass 6: the saw that runs the mountains' bands down the range (look.py FLOW_*), a function of time: seek
        # equals play
        {"kind": "lfo", "name": "rangeFlow", "settings": {"shape": "saw"}},
    ]
    return out


def R(src, target, amount):
    return {"source": src, "target": target, "amount": float(amount), "op": "add", "polarity": "unipolar"}


def routes():
    A = "fx/aurora/"
    W = "nodes/%s/water/" % look.WATER_NODE
    out = [
        # ---- Cloud Diving -> the aurora. Pass 4 (owner: "act much more wildly ... twist and turn more chaotically, change
        # colors faster"): the swell now churns the folds (ADR-1664, a phase: no jump), swirls them hard, lifts and
        # ripples the rims, and moves the palette on the fast envelope; brightness, height and width as before
        R("signal.cloudSwell", A + "intensity", 1.6),
        R("signal.cloudShape", A + "curtainHeight", 250.0),
        R("signal.cloudShape", A + "ribbonWidth", 0.012),
        R("signal.cloudShape", A + "ribbonChurn", 14.0),
        R("signal.cloudShape", A + "ribbonSwirl", 3.2),
        R("signal.cloudShape", A + "turbulence", 0.7),
        R("signal.cloudShape", A + "filaments", 1.5),
        R("signal.cloudSwell", A + "edgeBrightness", 1.5),
        R("signal.cloudSwell", A + "horizonGlow", 0.3),
        R("signal.cloudHue", A + "ribbonCoverage", 0.25),
        R("signal.cloudColor", "palette/position", float(len(look.LOOKS) - 1) - look.REST_POSITION),
        R("signal.cloudSwell", "fx/stars/brightness", -0.4),
        # the light the river takes from the aurora
        R("signal.auroraLight", W + "glow", 0.5),
        # ---- Crash: the environment's response to a shooting star (B s4)
        R("signal.crashGlow", A + "intensity", 0.1),   # 0.15 with the pass-5 fireballs: 0.050 at 47.7 s
        R("signal.crashGlow", W + "glow", 0.1),   # 0.3 on a kick + bed failed the flash gate at 23-25 s
        # a phrase-start crash throws a brighter ball than a re-hit on its decay
        R("signal.crashSize", "fx/ballA/intensity", 0.6),
        R("signal.crashSize", "fx/ballB/intensity", 0.6),

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
    doc["parameters"]["sources/rangeFlow/rate"] = look.FLOW_HZ
    doc["palette"]["position"] = look.REST_POSITION   # silence: between dusk indigo and the violet veil

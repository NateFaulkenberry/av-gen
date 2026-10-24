"""Aurora Borealis: the instrument -- the stems, the conditioned signals and the routes (02-research-signals.md s3).

Principle (the brief, s4): context sets the colour, events set the accents.

  context  signal.arrangement   how much of the band is playing (the stems' absolute levels), slow
           signal.synthBright   the synth's brightness (its centroid; a silent synth reads 0), slow
           signal.synthBed      the synth's sustained bed
           signal.hatDrive      the hat rate (eighths against sixteenths), never per hit
           signal.paletteAxis   arrangement and brightness on one axis through the four looks (look.LOOKS)
  events   signal.kickWave      the kick on the drum stem's grid, backed by the class (0 ms attack, 333 ms ramp)
           signal.snareFold     the snare class: a 40 ms bloom, a 290 ms fall
           signal.bassHit       a bass note, sized from its onset frame (onset-held size, 0 ms attack)
           signal.bassBody      the bass's continuous body, slow
           signal.swell         a synth swell (about half the owner's 18 are detectable: a secondary accent)
           signal.auroraLight   the one value that lights the aurora AND the facets facing it, on the same frame

Every musical input is a role (`role.drums|bass|synth.*`, ADR-1630), so live channels given those roles later drive the
same signals unchanged; every signal has a mix fallback, so the scene still plays on a mix alone. Smoothing lives only
in the signals; routes carry amounts. No `macro.*` (a frame late), no `.level` with a depth on an accent.

Isolation: the control channels `drums`, `bass` and `synth` rest at 1 (ADR-1612) and scale every input of their part
(a depth source), so one knob (CC 21/22/23) mutes a part. `--sonic-trace --trace-finals` verifies it; nothing is drawn.
"""
from __future__ import annotations

import look

STEMS = [("drums", "Drums"), ("bass", "Bass"), ("synth", "Synth")]
MIX = "../../assets/audio/feline-footwear.wav"
UNIT = {"clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0}
SLOW = {"attackMs": 1500.0, "decayMs": 3000.0}
CC = {"drums": 21, "bass": 22, "synth": 23}


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
    s = {"combine": combine, "inputs": inputs}
    if chain:
        s["chain"] = chain
    if event:
        s["event"] = True
    return {"kind": "signal", "name": name, "settings": s}


def remap(lo, hi):
    return {"remapEnabled": True, "remapInMin": lo, "remapInMax": hi, "remapOutMin": 0.0, "remapOutMax": 1.0}


def sources():
    """Dependency order: a signal that reads another is listed after it (ADR-1602)."""
    hold = {"envelope": "peakhold", "envelopeHoldMs": 750.0, "envelopeFallPerSecond": 1000.0}
    note = {"envelope": "linearfall", "envelopeFallPerSecond": 2.4, "attackMs": 0.0, "envelopeRefractoryMs": 130.0,
            "envelopeRearm": 0.05}
    return [
        # the mix's "drums are playing" gate (tools/drum_signals.py), for the kick's stand-in on a mix alone
        sig("drumming", [inp("response.hat", hold), inp("response.snare", hold)], combine="max",
            chain={"threshold": "binary", "thresholdLevel": 0.01}),
        # ---- context
        sig("arrangementStem", [
            inp("role.drums.rms", dict(gain=100.0, **UNIT, **SLOW), 0.3, part="drums"),
            inp("role.bass.rms", dict(gain=50.0, **UNIT, **SLOW), 0.3, part="bass"),
            inp("role.synth.rms", dict(gain=8.0, offset=-0.1, **UNIT, **SLOW), 0.4, part="synth")], combine="sum"),
        sig("arrangement", [inp("signal.arrangementStem", gate="role.synth.active"),
                            inp("sonic.density.slow", dict(**remap(0.60, 0.88), **UNIT, **SLOW))], chain=UNIT),
        sig("synthBright", [
            inp("role.synth.centroid", dict(gain=5.0, offset=-2.6, **UNIT, **SLOW),
                depthSource="role.synth.rms", depthMin=-0.2, depthMax=12.0),
            inp("sonic.brightness.slow", dict(**remap(0.45, 0.62), **UNIT, **SLOW))], chain=UNIT),
        sig("synthBedStem", [
            inp("role.synth.level", dict(gain=2.2, offset=-0.95, **UNIT, attackMs=250.0, decayMs=900.0), 0.3,
                part="synth"),
            inp("role.synth.rms", dict(gain=7.0, offset=-0.8, **UNIT, attackMs=250.0, decayMs=900.0), 0.8,
                part="synth"),
            inp("role.synth.onset", {"envelope": "linearfall", "envelopeFallPerSecond": 1.6, "attackMs": 20.0}, 0.35,
                part="synth")], combine="sum"),
        sig("synthBed", [inp("signal.synthBedStem", gate="role.synth.active"),
                         inp("response.sustain", dict(gain=2.0, offset=-0.75, **UNIT, attackMs=250.0, decayMs=1200.0),
                             0.9, part="synth")], chain=UNIT),
        sig("hatDrive", [inp("response.hatRate", {"attackMs": 500.0, "decayMs": 2000.0}, part="drums")]),
        sig("paletteAxis", [inp("signal.arrangement", amount=0.55),
                            inp("signal.synthBright", amount=0.45, part="synth")], combine="sum", chain=UNIT),
        # ---- events
        sig("kickRecent", [inp("role.drums.kick", {"envelope": "peakhold", "envelopeHoldMs": 600.0,
                                                   "envelopeFallPerSecond": 1000.0}),
                           inp("signal.drumming")], chain={"threshold": "binary", "thresholdLevel": 0.5}),
        sig("kickWave", [inp("beat.pulse", gate="signal.kickRecent", part="drums"),
                         inp("role.drums.kick", part="drums")], combine="max",
            chain={"envelope": "linearfall", "envelopeFallPerSecond": 3.0, "attackMs": 0.0,
                   "envelopeRefractoryMs": 200.0, "envelopeRearm": 0.05}),
        sig("snareFold", [
            inp("role.drums.snare", {"attackMs": 40.0, "envelope": "linearfall", "envelopeFallPerSecond": 3.5},
                part="drums"),
            inp("response.snare", {"attackMs": 40.0, "envelope": "linearfall", "envelopeFallPerSecond": 3.5},
                part="drums")]),
        sig("bassSize", [inp("role.bass.peak", dict(gain=1.5, **UNIT, attackMs=0.0, decayMs=500.0))]),
        sig("bassHitRaw", [inp("role.bass.onset", note, depthSource="signal.bassSize", depthMin=0.4, depthMax=1.3)]),
        sig("bassHit", [inp("signal.bassHitRaw", part="bass")]),
        sig("bassBody", [
            inp("role.bass.level", dict(gain=2.0, offset=-0.8, **UNIT, attackMs=150.0, decayMs=800.0), part="bass"),
            inp("response.bass", dict(gain=2.0, offset=-0.8, **UNIT, attackMs=150.0, decayMs=800.0), part="bass")],
            chain=UNIT),
        sig("swell", [inp("role.synth.treble", {"trendSeconds": 4.0, "trendFastMs": 250.0, "trendFloor": 0.1,
                                                "threshold": "binary", "thresholdLevel": 1.35, "attackMs": 0.0,
                                                "decayMs": 400.0, "envelope": "peakhold", "envelopeHoldMs": 1500.0,
                                                "envelopeFallPerSecond": 0.6, "envelopeRefractoryMs": 6000.0,
                                                "envelopeRearm": 0.3},
                          gate="role.synth.active", part="synth")], chain=UNIT),
        # ---- the light
        sig("auroraLight", [inp("signal.arrangement", amount=0.55), inp("signal.kickWave", amount=0.28),
                            inp("signal.bassHit", amount=0.16), inp("signal.swell", amount=0.25)], combine="sum"),
    ]


def R(src, target, amount):
    return {"source": src, "target": target, "amount": float(amount), "op": "add", "polarity": "unipolar"}


def routes():
    A = "fx/aurora/"
    W = "nodes/%s/water/" % look.WATER_NODE
    return [
        # context: the palette, how tall the curtains stand, the horizon's air, the stars, the fine rays
        R("signal.paletteAxis", "palette/position", float(len(look.LOOKS) - 1)),
        R("signal.arrangement", A + "curtainHeight", 380.0),
        R("signal.synthBed", A + "horizonGlow", 0.35),
        R("signal.synthBed", A + "ribbonWidth", 0.02),
        R("signal.arrangement", "fx/stars/brightness", -0.45),
        R("signal.hatDrive", A + "filaments", 1.6),
        R("signal.hatDrive", A + "sparkle", 0.35),
        # the light: the aurora and the facets facing it (the same signal, proportional amounts), the river's glow
        R("signal.auroraLight", A + "intensity", 3.4),
        R("signal.auroraLight", look.ROCK.path("auroraGlow"), 0.75),
        R("signal.auroraLight", W + "glow", 0.5),
        # bass: the lower rims' undulation, the folds, the river's reflection
        R("signal.bassHit", A + "ribbonEdge", 0.22),
        R("signal.bassBody", A + "ribbonSwirl", 0.45),
        R("signal.bassBody", W + "reflection", 0.25),
        R("signal.bassHit", W + "reflection", 0.3),
        # drums: the snare's bright fold; the kick's small bloom (its wave through the ribbons is E3, when merged)
        R("signal.snareFold", A + "edgeBrightness", 2.4),
        R("signal.kickWave", "post/bloom/intensity", 0.08),
        # swell: the ribbons spread
        R("signal.swell", A + "ribbonWidth", 0.015),
    ]


def control():
    return {"channels": [{"name": p, "rest": 1.0, "pickup": True} for p, _ in STEMS],
            "midi": {"enabled": True, "filter": "*", "bindings": [
                {"source": "*", "channel": -1, "kind": "cc", "number": CC[p], "signal": p} for p, _ in STEMS]}}


def apply(doc):
    """Put the instrument into a project document: the Feline mix, the three stems by role, the drum feeds."""
    doc["assets"]["audio"] = MIX
    doc["assets"]["stems"] = [{"key": k, "label": label, "role": k,
                               "file": "../../assets/audio/stems/feline-footwear (%s).wav" % label}
                              for k, label in STEMS]
    doc["feeds"] = {"beat": "role.drums", "hits": "role.drums"}
    doc["sources"] = sources()
    doc["routes"] = routes()
    doc["control"] = control()
    doc["palette"]["position"] = 0.0   # silence sits on the first look; the music moves along the axis

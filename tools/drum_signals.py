"""The kick a live scene reads after ADR-1620 (option B): one conditioned signal (ADR-1602), the drum source's kick when
there is one, else the beat clock's pulse while the drums are audibly playing.

ADR-1620 removed the mix's kick (`response.kick` and its envelope fire only from a source a project's feeds name;
`audio.onsetLow` left the bus): measured on Feline Footwear against its drum stem, the mix's kick detector was 0.54
precise -- about half its kicks were bass notes and hat clicks. The mix's snare and hat (0.93 and 0.85) stay as they
were. A scene that wants "the kick" reads `signal.kick`:

  signal.drumming         1 while the mix's own hat or snare detector has fired within the last DRUMMING_HOLD_MS: the
                          drums are audibly playing. The beat clock runs on through a breakdown, a drumless intro or
                          outro; this gate is what stops the kick stand-in there.
  signal.kick      event  stem.drums.kick while a drum source keyed `drums` is active (an offline stem, or a live
                          input with the hits analysis), else `beat.pulse` x 0.7 while signal.drumming.
  signal.kickEnv          the same as an envelope (the response model's kick release, 160 ms), for routes that read a
                          level

0.7 is the mix kick's median strength on Feline (0.69), so a route tuned on the old hit lands at the size it was tuned
at. The mix input reads response.* (hat, snare), so a project using it needs a `sonic` block.

`sources(names)` returns the source blocks for the names a project uses, dependencies first, to put BEFORE any source
that reads them. `used(obj)` finds the names a project document (or any JSON-like object) reads.
"""
from __future__ import annotations

import re

KICK_RELEASE_MS = 160.0     # ResponseSettings::hitRelease[Kick]
KICK_SCALE = 0.7            # the mix kick's median strength on Feline Footwear
DRUMMING_HOLD_MS = 750.0    # how long after a hat or snare the drums still count as playing


def _drumming():
    hold = {"envelope": "peakhold", "envelopeHoldMs": DRUMMING_HOLD_MS, "envelopeFallPerSecond": 1000.0}
    return {"kind": "signal", "name": "drumming", "settings": {
        "combine": "max",
        "inputs": [{"source": "response.hat", "chain": dict(hold)}, {"source": "response.snare", "chain": dict(hold)}],
        "chain": {"threshold": "binary", "thresholdLevel": 0.01}}}


def _kick(env):
    chain = {"attackMs": 0.0, "decayMs": KICK_RELEASE_MS} if env else None

    def inp(source, **extra):
        i = {"source": source}
        if chain:
            i["chain"] = dict(chain)
        i.update(extra)
        return i

    settings = {"combine": "firstActive", "inputs": [
        inp("stem.drums.kick"),  # active while stem.drums.active (found by name); absent on a plain mix
        inp("beat.pulse", amount=KICK_SCALE, gate="signal.drumming")]}
    if not env:
        settings["event"] = True
    return {"kind": "signal", "name": "kickEnv" if env else "kick", "settings": settings}


NAMES = ("kick", "kickEnv")
SIGNAL = {n: "signal." + n for n in NAMES}


def sources(names):
    """The signal sources for `names` (a subset of NAMES), dependencies first."""
    names = set(names)
    unknown = names - set(NAMES)
    if unknown:
        raise KeyError(f"not a drum signal: {sorted(unknown)}")
    if not names:
        return []
    return [_drumming()] + [_kick(n == "kickEnv") for n in NAMES if n in names]


_PATTERN = re.compile(r"\bsignal\.(kick|kickEnv)\b")


def used(obj):
    """The drum signal names a JSON-like object reads (any string value naming `signal.kick` or `signal.kickEnv`)."""
    found = set()

    def walk(o):
        if isinstance(o, dict):
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)
        elif isinstance(o, str):
            for m in _PATTERN.finditer(o):
                found.add(m.group(1))

    walk(obj)
    return found

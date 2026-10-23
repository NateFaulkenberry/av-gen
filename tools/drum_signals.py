"""The source-aware signals a scene reads (ADR-1620, ADR-1630): one conditioned signal per musical idea (ADR-1602) --
the source of that ROLE when one is active, else the mix's stand-in.

Sources are found by role, never by key (ADR-1630). A stem or live input source says what it is (`"role": "drums"`,
`bass`, `synth`, `vocal`, `other`), and `role.<role>.*` publishes the first active source of that role. So these
signals follow a drum bus keyed `drums`, `kit` or `in3` alike.

**The kick (ADR-1620, option B).** A mix has no kick: measured on Feline Footwear against its drum stem, the mix's kick
detector was 0.54 precise, and about half its kicks were bass notes and hat clicks. The mix's snare and hat (0.93 and
0.85) stay as they were. A scene that wants "the kick" reads `signal.kick`:

  signal.drumming         1 while the mix's own hat or snare detector has fired within the last DRUMMING_HOLD_MS: the
                          drums are audibly playing. The beat clock runs on through a breakdown, a drumless intro or
                          outro; this gate is what stops the kick stand-in there.
  signal.kick      event  role.drums.kick while a drums-role source is active (an offline stem, or a live input; a
                          drums source always runs the kick/snare/hat classifier), else `beat.pulse` x 0.7 while
                          signal.drumming.
  signal.kickEnv          the same as an envelope (the response model's kick release, 160 ms), for routes that read a
                          level

0.7 is the mix kick's median strength on Feline (0.69), so a route tuned on the old hit lands at the size it was tuned
at.

**Per-role levels (ADR-1630).** One level per part, so a scene names the idea once instead of routing `feeds.levels`
(which moves every `response.*` level to one source):

  signal.bassLevel        role.bass.level while a bass source is active, else response.bass (the mix's 30-150 Hz body)
  signal.synthLevel       role.synth.level while a synth source is active, else response.sustain (the mix's sustained,
                          non-percussive energy: the nearest the mix has to a pad or lead)

A level is a level, never an identity: these read activity, not a class.

Every mix input reads response.*, so a project using any of these needs a `sonic` block.

`sources(names)` returns the source blocks for the names a project uses, dependencies first, to put BEFORE any source
that reads them. `used(obj)` finds the names a project document (or any JSON-like object) reads.
"""
from __future__ import annotations

import re

KICK_RELEASE_MS = 160.0     # ResponseSettings::hitRelease[Kick]
KICK_SCALE = 0.7            # the mix kick's median strength on Feline Footwear
DRUMMING_HOLD_MS = 750.0    # how long after a hat or snare the drums still count as playing

ROLES = ("drums", "bass", "synth", "vocal", "other")  # audio::SourceRole, in its order


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
        inp("role.drums.kick"),  # active while role.drums.active (found by name); absent with no drums source
        inp("beat.pulse", amount=KICK_SCALE, gate="signal.drumming")]}
    if not env:
        settings["event"] = True
    return {"kind": "signal", "name": "kickEnv" if env else "kick", "settings": settings}


# role -> the mix's stand-in for its level
_LEVEL_FALLBACK = {"bass": "response.bass", "synth": "response.sustain"}


def _level(role):
    return {"kind": "signal", "name": role + "Level", "settings": {
        "combine": "firstActive",
        "inputs": [{"source": f"role.{role}.level"},  # active while role.<role>.active
                   {"source": _LEVEL_FALLBACK[role]}]}}


KICK_NAMES = ("kick", "kickEnv")
LEVEL_NAMES = tuple(r + "Level" for r in _LEVEL_FALLBACK)
NAMES = KICK_NAMES + LEVEL_NAMES
SIGNAL = {n: "signal." + n for n in NAMES}


def sources(names):
    """The signal sources for `names` (a subset of NAMES), dependencies first."""
    names = set(names)
    unknown = names - set(NAMES)
    if unknown:
        raise KeyError(f"not a source-aware signal: {sorted(unknown)}")
    out = []
    if names & set(KICK_NAMES):
        out.append(_drumming())
        out += [_kick(n == "kickEnv") for n in KICK_NAMES if n in names]
    out += [_level(n[:-len("Level")]) for n in LEVEL_NAMES if n in names]
    return out


_PATTERN = re.compile(r"\bsignal\.(" + "|".join(NAMES) + r")\b")


def used(obj):
    """The signal names (of NAMES) a JSON-like object reads (any string value naming `signal.<name>`)."""
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

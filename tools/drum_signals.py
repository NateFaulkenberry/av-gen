"""The drum vocabulary a live scene speaks after ADR-1620: one conditioned signal (ADR-1602) per idea, the true drum
class from a drum source when there is one, else the mix's honest stand-in.

ADR-1620 removed the mix's kick, snare and hat (`response.kick/snare/hat` and their envelopes fire only from a source a
project's feeds name; `audio.onsetLow/Mid/High` left the bus): measured on Feline Footwear against its drum stem, about
half the mix's kicks were bass notes and hat clicks. A scene that wants "the kick" now reads `signal.kick`:

  signal.kick      event  stem.drums.kick when a drum source keyed `drums` is active (an offline stem, or a live input
                          with the hits tier), else the beat clock's pulse while the music is playing (`beat.pulse` gated
                          by signal.busy: on Feline 0.67/0.62 against the drum stem's kicks, where the mix's kick
                          detector was 0.54/0.62, and it never fires off the grid)
  signal.snare     event  stem.drums.snare, else the pulse on the backbeat (beats 2 and 4) while the music is playing:
                          0.70/0.56 against the stem's snares, where the mix's broadband onset is 0.09 precise against them
  signal.hat       event  stem.drums.hat, else the mix's broadband onset (`response.onset`: 0.98 precise against any drum
                          hit, about 0 ms)
  signal.kickEnv, signal.snareEnv, signal.hatEnv   the same as envelopes (the response model's releases: 160, 120 and
                          60 ms), for routes that read a level
  signal.busy             1 while the mix has attacks (response.onsetRate at or above BUSY_RATE): the beat clock runs on
                          through a drumless passage, and these stand-ins should not
  signal.backbeat         1 on beats 2 and 4 of the bar while busy (a gate for signal.snare)

The mix inputs read response.* (onset, onsetRate), so a project using them needs a `sonic` block.

`sources(names)` returns the source blocks for the names a project uses, in the order the rack needs (a signal reading
another is listed after it), to put BEFORE any interpret source that reads them. `used(obj)` finds the names a project
document (or any JSON-like object) reads.
"""
from __future__ import annotations

import re

# The response model's hit releases (ResponseSettings::hitRelease), as an envelope's decay.
_RELEASE_MS = {"kick": 160.0, "snare": 120.0, "hat": 60.0}


# The attack rate (response.onsetRate: onsets a second / 12, over 1.5 s) at or above which the music counts as playing.
# 0.05 is 0.6 onsets a second. On Feline Footwear the beat clock runs on through the drumless outro (182-210 s), where
# 14% of the kick stand-in's and 16% of the snare stand-in's pulses landed on no drum hit at all; gated here, 3% and 4%,
# with the same recall.
BUSY_RATE = 0.05


def _busy():
    return {"kind": "signal", "name": "busy", "settings": {"combine": "firstActive", "inputs": [
        {"source": "response.onsetRate", "chain": {"threshold": "binary", "thresholdLevel": BUSY_RATE}}]}}


def _backbeat():
    # beat.bar is the position in the bar (0 = the downbeat); a binary threshold is 1 at or above its level. Summed:
    # +1 past 1/8, -1 past 3/8, +1 past 5/8, -1 past 7/8 -- 1 round beats 2 and 4, 0 round beats 1 and 3 -- plus
    # signal.busy, and 1 only where both are (the output chain's threshold at 1.5).
    edges = [(0.125, 1.0), (0.375, -1.0), (0.625, 1.0), (0.875, -1.0)]
    inputs = []
    for level, amount in edges:
        i = {"source": "beat.bar", "chain": {"threshold": "binary", "thresholdLevel": level}}
        if amount != 1.0:
            i["amount"] = amount
        inputs.append(i)
    inputs.append({"source": "signal.busy"})
    return {"kind": "signal", "name": "backbeat", "settings": {
        "combine": "sum", "inputs": inputs, "chain": {"threshold": "binary", "thresholdLevel": 1.5}}}


def _hit(cls, env):
    chain = {"attackMs": 0.0, "decayMs": _RELEASE_MS[cls]} if env else None

    def inp(source, **extra):
        i = {"source": source}
        if chain:
            i["chain"] = dict(chain)
        i.update(extra)
        return i

    drum = inp("stem.drums." + cls)  # active while stem.drums.active (found by name), absent on a plain mix
    # The pulse is always full strength; scaled to the median strength the mix's class gave on Feline Footwear (kick
    # 0.69, snare 0.90), so a route tuned on the old hit lands at the size it was tuned at.
    if cls == "kick":
        mix = inp("beat.pulse", amount=0.7, gate="signal.busy")
    elif cls == "snare":
        mix = inp("beat.pulse", amount=0.9, gate="signal.backbeat")
    else:
        mix = inp("response.onset")
    settings = {"combine": "firstActive", "inputs": [drum, mix]}
    if not env:
        settings["event"] = True
    return {"kind": "signal", "name": cls + ("Env" if env else ""), "settings": settings}


NAMES = ("kick", "kickEnv", "snare", "snareEnv", "hat", "hatEnv")
SIGNAL = {n: "signal." + n for n in NAMES}


def sources(names):
    """The signal sources for `names` (a subset of NAMES, in any order), dependencies first."""
    names = set(names)
    unknown = names - set(NAMES)
    if unknown:
        raise KeyError(f"not a drum signal: {sorted(unknown)}")
    out = []
    if names & {"kick", "kickEnv", "snare", "snareEnv"}:
        out.append(_busy())
    if names & {"snare", "snareEnv"}:
        out.append(_backbeat())
    for n in NAMES:
        if n in names:
            out.append(_hit(n.replace("Env", ""), n.endswith("Env")))
    return out


_PATTERN = re.compile(r"\bsignal\.(kick|kickEnv|snare|snareEnv|hat|hatEnv)\b")


def used(obj):
    """The drum signal names a JSON-like object reads (any string value naming `signal.<name>`)."""
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


def install(doc):
    """Puts the drum signals a project document reads at the front of its sources (replacing any it already has), so
    every interpret, envelope and signal source after them reads them on their frame. Returns the names installed."""
    names = used({k: v for k, v in doc.items() if k != "sources"} | {
        "sources": [s for s in doc.get("sources", []) if not _is_drum_source(s)]})
    kept = [s for s in doc.get("sources", []) if not _is_drum_source(s)]
    doc["sources"] = sources(names) + kept
    return names


def _is_drum_source(s):
    return s.get("kind") == "signal" and s.get("name") in set(NAMES) | {"backbeat", "busy"}

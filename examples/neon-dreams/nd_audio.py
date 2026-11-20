"""Neon Dreams iteration 5: audio reactivity and the palette arc (imported by build.py).

One instrument, one system, never the whole town at once (art-direction section 12, brief sections 8, 9, 11):

| Stem         | Drives                                                                                     |
|--------------|--------------------------------------------------------------------------------------------|
| BASS level   | the Great Lantern's bell: rises 1.5 m and brightens 25 %; its cloud glow; the cloud swell; the  |
|              | Condensery's every light and its plume/throat glows, as a slow ember (the Condensery's only cue) |
| Kick onset   | a ring of light leaving the Great Lantern at 120 m/s (an onset field on the kick stem's onsets): |
|              | eave lines, lantern strings and pier lights, lit panes brighten as it passes each building     |
| Snare onset  | sign businesses A and B in turn (a cycle source on the snare), gangway strips                  |
| HH onset     | sparkle on the garden umbels' points, lantern bulbs, keel lanterns; star twinkle                 |
| Toms onset   | a ripple running along the Quay's lantern strings; flashes of the Veil's inner pockets          |
| Crash onset  | section events: the palette arc's steps, the sky's horizon lift, the lighthouse beams flaring   |
| Moog level   | the window program: lit fraction +10 points, the tint mix, pane brightness (the town's voice)  |
| Cloud Diving | the clouds' inner glow, the nebula (visible sky), the cosmic ocean, the Lone Lamp's window     |
| tone block   | beacons and roundel signs blink in a pattern stepped by each hit; dark while the stem rests     |
|              | (the bar at 46.2 s)                                                                        |

The palette arc (Ultraviolet Dream -> Neon Rose -> Electric Blue -> Amber Eclipse) steps on the crash stem's section
crashes as timeline tracks, each system at its own rate (signs 1.5 s, eaves 2.5 s, windows 6 s with every building
starting within 4 s, garden and lanterns 4 s, the bell 5 s, cloud underlight 10 s, the air 16 s, the far field 30 s).
Colours interpolate in linear RGB between neighbouring states (violet, rose, blue and amber never pass through green).
The Condensery keeps its amber; the Lone Lamp its single amber window; beacons stay red.
"""
from __future__ import annotations

import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(HERE, "audio", "sabertooth_events.json")) as _f:
    EVENTS = json.load(_f)

# the section crashes, at their leading edge (the picker reports the envelope's peak, ~0.12 s after the hit)
CRASH = [round(t - 0.12, 2) for t, _ in EVENTS["stems"]["crash"]["onsets"]]
C_FIRST, C_PRE, C_ARRIVAL, C_CLOSE, C_VEIL, C_FAR, C_REVEAL = CRASH[:7]

# when each palette state begins, per system: (start second, state, seconds to cross)
ARC = {
    "sign":     [(C_PRE, "rose", 1.5), (C_VEIL, "blue", 1.5), (C_REVEAL, "amber", 1.5)],
    "eave":     [(C_ARRIVAL, "rose", 2.5), (C_VEIL, "blue", 2.5), (C_REVEAL, "amber", 2.5)],
    "window":   [(C_ARRIVAL, "rose", 10.0), (C_VEIL, "blue", 10.0), (C_REVEAL, "amber", 10.0)],  # 6 s + 4 s stagger
    "practical": [(C_ARRIVAL, "rose", 6.0), (C_VEIL, "blue", 6.0), (C_REVEAL, "amber", 6.0)],
    "garden":   [(C_ARRIVAL, "rose", 4.0), (C_VEIL, "blue", 4.0), (C_REVEAL, "amber", 4.0)],
    "bell":     [(C_ARRIVAL, "rose", 5.0), (C_VEIL, "blue", 5.0), (C_REVEAL, "amber", 5.0)],
    "underlight": [(C_ARRIVAL, "rose", 10.0), (C_VEIL, "blue", 10.0), (C_REVEAL, "amber", 10.0)],
    "air":      [(C_ARRIVAL, "rose", 16.0), (C_VEIL, "blue", 16.0), (C_FAR, "amber", 13.0)],
    "far":      [(C_ARRIVAL, "rose", 30.0), (C_VEIL, "blue", 20.0), (C_FAR, "amber", 13.0)],
}


def smooth(u):
    u = min(max(u, 0.0), 1.0)
    return u * u * (3 - 2 * u)


def arc_keys(fn, system, song):
    """Linear keys sampling the eased schedule: the start, 5 points across each transition, the end."""
    times = {0.0, song}
    for (t0, _, dur) in ARC[system]:
        for k in range(7):
            tt = t0 + dur * k / 6.0
            if tt < song:
                times.add(round(tt, 3))
    out = []
    for t in sorted(times):
        out.append({"time": round(t, 3), "value": [round(v, 5) for v in arc_at(fn, system, t)], "interp": "linear"})
    return out


def arc_at(fn, system, t):
    """fn(state) -> value (list): the value at second t; a transition runs until the next one starts, which then
    begins from where it had got to."""
    val = fn("uv")
    sched = ARC[system]
    for i, (t0, st, dur) in enumerate(sched):
        if t < t0:
            break
        nxt = sched[i + 1][0] if i + 1 < len(sched) else float("inf")
        tend = min(t, nxt)
        u = smooth((tend - t0) / dur)
        target = fn(st)
        val = [a + (b - a) * u for a, b in zip(val, target)]
    return val


def track(target, keys, component=-1):
    return {"target": target, "component": component, "enabled": True, "timeBase": "seconds", "mode": "replace",
            "loopLength": 0.0, "keys": keys}


def const_track(target, value):
    return track(target, [{"time": 0.0, "value": value, "interp": "step"}])


# ---------------------------------------------------------------------------------------------------------------
# material programs
class Prog:
    """A material program under construction that remembers where its addressable ops are (1-based paths)."""

    def __init__(self, name):
        self.name = name
        self.ops = []
        self.at = {}

    def op(self, label=None, **kw):
        self.ops.append(kw)
        if label:
            self.at[label] = (len(self.ops), kw["kind"])
        return len(self.ops) - 1

    def path(self, label, field="constant"):
        i, kind = self.at[label]
        return "material/%s/op/%d/%s/%s" % (self.name, i, kind, field)

    def doc(self, emission, intensity, base=-1):
        assert len(self.ops) <= 96, (self.name, len(self.ops))
        return {"name": self.name, "ops": self.ops, "baseColor": base, "metallic": -1, "roughness": -1,
                "emission": emission, "emissionIntensity": intensity, "opacity": -1}


def gain_term(p, src_reg, dst, amount):
    """reg[dst] = 1 + amount * reg[src] (remap without clamp)."""
    p.op(kind="remap", dst=dst, srcA=src_reg, constant=[0.0, 1.0, 1.0, 1.0 + amount], value=0.0)


def emit_program(name, colour, intensity, ring=0.0, hh=0.0, toms=0.0, pattern=False):
    """Emission = colour x the product of its audio terms (the GLB's emissive texture still multiplies after).
    ring: the kick ring field; hh: a sparkle on a moving noise subset scaled by a routed constant;
    toms: a ripple running along +x scaled by a routed constant; pattern: the tone-block beacon chase."""
    p = Prog(name)
    p.op("colour", kind="constant", dst=0, constant=list(colour) + [1.0])
    p.op(kind="constant", dst=1, constant=[1.0, 1.0, 1.0, 1.0])
    if ring:
        p.op(kind="field", dst=2, field="kickRing")
        gain_term(p, 2, 2, ring)
        p.op(kind="multiply", dst=1, srcA=1, srcB=2)
    if hh or toms:
        p.op(kind="input", input="worldPosition", dst=3)
        p.op(kind="input", input="time", dst=4)
    if hh:
        # a subset of points that changes 9 times a second
        p.op(kind="constant", dst=5, constant=[0.0, 9.0, 0.0, 0.0])
        p.op(kind="multiply", dst=5, srcA=4, srcB=5)
        p.op(kind="add", dst=5, srcA=3, srcB=5)
        p.op(kind="noise", dst=5, srcA=5, value=1.7, seed=23)
        p.op(kind="smoothstep", dst=5, srcA=5, constant=[0.62, 0.78, 0.0, 0.0])
        p.op("hh", kind="constant", dst=6, constant=[0.0, 0.0, 0.0, 0.0])
        p.op(kind="multiply", dst=5, srcA=5, srcB=6)
        gain_term(p, 5, 5, hh)
        p.op(kind="multiply", dst=1, srcA=1, srcB=5)
    if toms:
        # cos(2 pi (x / 24 m - 1.6 t)): bands 24 m apart running east along the Quay at 38 m/s
        p.op(kind="constant", dst=5, constant=[1.0 / 24.0, 0.0, 0.0, 0.0])
        p.op(kind="multiply", dst=5, srcA=3, srcB=5)
        p.op(kind="constant", dst=6, constant=[-1.6, 0.0, 0.0, 0.0])
        p.op(kind="multiply", dst=6, srcA=4, srcB=6)
        p.op(kind="add", dst=5, srcA=5, srcB=6)
        p.op(kind="palette", dst=5, srcA=5, constant=[0.5, 0.5, 0.5, 0.0], constant2=[0.5, 0.5, 0.5, 0.0],
             constant3=[1.0, 1.0, 1.0, 0.0], constant4=[0.0, 0.0, 0.0, 0.0], value=0.0)
        p.op(kind="power", dst=5, srcA=5, value=4.0)
        p.op("toms", kind="constant", dst=6, constant=[0.0, 0.0, 0.0, 0.0])
        p.op(kind="multiply", dst=5, srcA=5, srcB=6)
        gain_term(p, 5, 5, toms)
        p.op(kind="multiply", dst=1, srcA=1, srcB=5)
    if pattern:
        # a quarter of the lamps lit at a time; each tone-block hit moves the lit quarter on (the palette op's value
        # is replaced by a 4-step cycle source)
        p.op(kind="input", input="instanceRandom", dst=3)
        p.op("chase", kind="palette", dst=3, srcA=3, constant=[0.5, 0.5, 0.5, 0.0], constant2=[0.5, 0.5, 0.5, 0.0],
             constant3=[1.0, 1.0, 1.0, 0.0], constant4=[0.0, 0.0, 0.0, 0.0], value=0.0)
        p.op(kind="smoothstep", dst=3, srcA=3, constant=[0.55, 0.85, 0.0, 0.0])
        p.op(kind="remap", dst=3, srcA=3, constant=[0.0, 1.0, 0.1, 1.0], value=0.0)
        p.op(kind="multiply", dst=1, srcA=1, srcB=3)
    p.op(kind="multiply", dst=7, srcA=0, srcB=1)
    return p, p.doc(7, intensity)


def window_program(name, tints_from, tints_to, intensity, ring=0.0):
    """Lit panes on the kit's modelled windows (iteration 3's rule: the pane's own UV hash, its floor, its building),
    now with the palette arc's crossfade (every building starts within 4 s of the next), the Moog's lit fraction and
    tint mix, and the kick ring. Pane UV: u = brightness level (16 steps), v = a per-window hash."""
    p = Prog(name)
    o = p.op
    o(kind="input", input="uv", dst=0)
    o(kind="input", input="instanceRandom", dst=1)
    o(kind="input", input="localPosition", dst=2)
    o(kind="constant", dst=3, constant=[0.0, 1000.0, 0.0, 0.0])
    o(kind="multiply", dst=4, srcA=0, srcB=3)
    o(kind="constant", dst=3, constant=[1000.0, 0.0, 0.0, 0.0])
    o(kind="multiply", dst=5, srcA=1, srcB=3)
    o(kind="add", dst=4, srcA=4, srcB=5)
    o(kind="constant", dst=3, constant=[0.125, 0.3125, 0.0, 0.0])
    o(kind="multiply", dst=6, srcA=2, srcB=3)
    o(kind="swizzle", dst=6, srcA=6, constant=[2.0, 0.0, 1.0, 2.0])
    o(kind="swizzle", dst=7, srcA=6, constant=[0.0, 3.0, 2.0, 3.0])
    o(kind="add", dst=7, srcA=4, srcB=7)
    o(kind="quantize", dst=7, srcA=7, value=1.0, seed=17)
    o(kind="add", dst=6, srcA=5, srcB=6)
    o(kind="quantize", dst=6, srcA=6, value=1.0, seed=29)
    o(kind="swizzle", dst=6, srcA=6, constant=[3.0, 3.0, 3.0, 3.0])
    o(kind="smoothstep", dst=6, srcA=6, constant=[0.28, 0.30, 0.0, 0.0])
    o(kind="gradient", dst=6, srcA=6, constant=[1.25, 0.0, 0.0, 0.12], value=1.0)
    o("litfrac", kind="gradient", dst=5, srcA=1, constant=[0.0, 0.40, 0.0, 0.10], value=1.0)   # Moog adds to w
    o(kind="multiply", dst=5, srcA=5, srcB=6)
    o(kind="constant", dst=3, constant=[1.0, 0.0, 0.0, 0.0])
    o(kind="multiply", dst=5, srcA=5, srcB=3)
    o(kind="swizzle", dst=6, srcA=7, constant=[1.0, 3.0, 1.0, 1.0])
    o(kind="constant", dst=3, constant=[0.0, 1.0, 0.0, 0.0])
    o(kind="multiply", dst=6, srcA=6, srcB=3)
    o(kind="add", dst=5, srcA=5, srcB=6)
    o(kind="gradient", dst=5, srcA=5, constant=[1.0, -1.0, 0.0, 0.0], value=80.0)
    o(kind="power", dst=6, srcA=0, value=2.0)
    o(kind="gradient", dst=6, srcA=6, constant=[0.8, 0.0, 0.0, 0.2], value=1.0)
    o(kind="multiply", dst=5, srcA=5, srcB=6)
    if ring:
        o(kind="field", dst=3, field="kickRing")
        gain_term(p, 3, 3, ring)
        o(kind="multiply", dst=5, srcA=5, srcB=3)
    # the tint: a second hash of the window, biased toward the first stop (the Moog lowers the bias)
    o(kind="quantize", dst=6, srcA=4, value=1.0, seed=43)
    o(kind="swizzle", dst=6, srcA=6, constant=[3.0, 3.0, 3.0, 3.0])
    o("tintpow", kind="power", dst=6, srcA=6, value=1.7)
    o("from", kind="oklabRamp", dst=3, srcA=6, constant=tints_from[0] + [1.0], constant2=tints_from[1] + [1.0],
      constant3=tints_from[2] + [1.0], constant4=tints_from[3] + [1.0], value=1.0)
    o("to", kind="oklabRamp", dst=4, srcA=6, constant=tints_to[0] + [1.0], constant2=tints_to[1] + [1.0],
      constant3=tints_to[2] + [1.0], constant4=tints_to[3] + [1.0], value=1.0)
    # the crossfade: saturate(w - 0.6 x the building's random), w keyed 0 -> 1.6 over a transition
    o("progress", kind="gradient", dst=2, srcA=1, constant=[0.0, -0.6, 0.0, 1.6], value=1.0)
    o(kind="mixBy", dst=6, srcA=3, srcB=4, srcC=2)
    o(kind="multiply", dst=7, srcA=6, srcB=5)
    o(kind="constant", dst=0, constant=[0.012, 0.011, 0.02, 1.0])
    return p, p.doc(7, intensity, base=0)


# ---------------------------------------------------------------------------------------------------------------
# the kick ring
def kick_ring_field(origin):
    return {"name": "kickRing", "kind": "onset", "enabled": True, "space": "world", "strength": 1.0,
            "falloff": {"kind": "none"}, "waveGeometry": "radial", "point": [round(v, 2) for v in origin],
            "axis": [0.0, 1.0, 0.0], "audioSpeed": 120.0, "onsetSource": "low", "onsetDecay": 0.9,
            "onsetWidth": 7.0}


def route(source, target, amount, op="add", component=-1, **chain):
    r = {"source": source, "target": target, "amount": amount, "op": op, "polarity": "unipolar"}
    if component >= 0:
        r["component"] = component
    if chain:
        r["chain"] = chain
    return r

"""Neon Dreams iteration 11 (owner msg 130: "I was thinking more the lights in the city could react more as well").

The city's own light plays the song, one system per instrument, each in its own place and its own section, so the
town never flashes all at once (rule 1 of notes/audio-ideas-it10.md; the trace sheet counts how many peak together):

| System                      | Stem / cue                 | Where                                     | When           |
|-----------------------------|----------------------------|-------------------------------------------|----------------|
| skyline call-and-response   | Moog onsets (80)           | district masks: Quay -> Lampmarket ->     | all song       |
|                             |                            | Lanternhill -> Eastreach, one per note:   |                |
|                             |                            | signs, eave/crown strips, window bands    |                |
| kick ring, town and isles   | kick onsets (fields)       | town ring stronger (strips, panes,        | all song       |
|                             |                            | lanterns); a second ring `isleRing` at    |                |
|                             |                            | 240 m/s, 0 by 450 m, outside the district |                |
|                             |                            | masks (isle strips and garden lamps) and  |                |
|                             |                            | the isles' cloud glows (delayed routes)   |                |
| lantern chase               | HH onsets (step timeline)  | lantern strings: a lamp group a hat       | the town, not  |
|                             |                            | (the strings' sparkle rests there)        | in tom fills   |
| sign chase                  | snare onsets (step)        | signs: a band steps down the street       | the town       |
| window flicks               | the pad's phrases 30.9,    | every building: floors / columns flip in  | 2 bars each    |
|                             | 42.2 s                     | eighth notes (a wipe up, alternate floors)|                |
| landmark crowns             | bass level (its top)       | ndStrip_sign_c = the Great Lantern's frame| all song       |
|                             |                            | and the Mooring Mast; the bell +70 %      |                |
| the ocean breathes          | the pad's phrases > 56 s   | a ring through ndOcean from the break's   | the reveal     |
|                             |                            | centre; the break's rim glow swells       |                |

In the town the tone-block beacons and the gardens' hat sparkle rest to about a third, so the new systems do not stack
on them (the trace sheet: work/engine/it11/trace, <= 4 systems at peak in ~94 % of frames).

Every envelope is a timeline track computed here from the measured onsets (audio/sabertooth_events.json): exactly
seek-safe (ADR-1168 family), and the trace reads the finals.
"""
from __future__ import annotations

import math

import nd_audio as AU

E = AU.EVENTS["stems"]
ONSETS = {k: [t for t, _ in v["onsets"]] for k, v in E.items()}
STRENGTH = {k: [s for _, s in v["onsets"]] for k, v in E.items()}

# (name, centre x z, radii x z): soft ellipses over the districts the approach sees side by side
DISTRICTS = [
    ("quay", (15.0, -5.0), (115.0, 55.0)),
    ("lampmarket", (-237.0, -87.0), (105.0, 60.0)),
    ("lanternhill", (5.0, -160.0), (115.0, 75.0)),
    ("eastreach", (211.0, -140.0), (80.0, 55.0)),
]
MOOG_GAIN = {"sign": 3.0, "strip": 5.0, "panes": 3.5}   # x the district's envelope (peak 1)
MOOG_AD = (0.03, 0.55)                                  # the art's 30 ms attack, 500 ms decay (to 5 %)
TOWN = (23.0, 47.2)                                     # the town section (arrival crash -> the dive)
HH_CHASE = {"period": 4.0, "steps": 4, "gain": 2.4, "ad": (0.012, 0.22)}
SNARE_CHASE = {"period": 132.0, "steps": 6, "gain": 2.2, "ad": (0.01, 0.42), "power": 6.0}
FLICK_PHRASES = [30.9, 42.2]                            # the pad's phrases the camera spends inside the town
TOM_FILLS = [(1.1, 3.6), (24.1, 25.9), (35.4, 37.4), (41.9, 43.0), (46.1, 48.6), (69.1, 70.3)]   # the toms own the strings


def in_fill(t):
    return any(a - 0.1 <= t <= b + 0.3 for a, b in TOM_FILLS)


FLICK = {"beat": 0.66 / 2, "bars": 2, "gain": 1.0}
ISLE_RING = {"speed": 240.0, "outer": 450.0, "inner": 55.0, "gain": 4.0}   # x (1 - the town mask)
TOWN_RING_GAIN = {"strip": 4.5, "panes": 2.0, "lantern": 2.4}   # it10: 1.8 / 0.6 / 1.0
CROWN_BASS = 2.2       # x the crown's own emission at full bass (it10: the bell +25 %)
OCEAN = {"speed": 110.0, "travel": 4.5, "rise": 0.9, "width": 70.0, "gain": 1.6}


# ---------------------------------------------------------------------------------------------------------------
# envelopes as timeline keys
def env_value(t, onsets, attack, decay, amps):
    v = 0.0
    k = 3.0 / max(decay, 1e-3)
    for t0, a in zip(onsets, amps):
        u = t - t0
        if u < 0 or u > attack + decay * 2.0:
            continue
        v += a * (u / attack if u < attack else math.exp(-k * (u - attack)))
    return min(v, 1.0)


def env_keys(onsets, attack, decay, amps=None, gate=None, vec=True, song=70.18):
    """Linear keys of the summed attack/exponential-decay envelope (peak 1, clamped); `gate` (a, b) keeps onsets
    inside it (soft: the last ones' tails are kept)."""
    amps = amps or [1.0] * len(onsets)
    if gate:
        pairs = [(t, a) for t, a in zip(onsets, amps) if gate[0] <= t <= gate[1]]
        onsets, amps = [p[0] for p in pairs], [p[1] for p in pairs]
    times = {0.0, song}
    for t0 in onsets:
        for dt in (-0.001, attack) + tuple(attack + decay * f for f in (0.08, 0.18, 0.32, 0.5, 0.75, 1.0, 1.5)):
            if 0 <= t0 + dt <= song:
                times.add(round(t0 + dt, 4))
    keys = []
    for t in sorted(times):
        v = round(env_value(t, onsets, attack, decay, amps), 4)
        keys.append({"time": t, "value": [v] * 4 if vec else v, "interp": "linear"})
    return keys


def step_keys(onsets, steps, gate=None, song=70.18):
    """A phase stepping 1/steps on each onset (inside the gate), for a palette op's value (a chase)."""
    keys = [{"time": 0.0, "value": 0.0, "interp": "step"}]
    k = 0
    for t in onsets:
        if gate and not (gate[0] <= t <= gate[1]):
            continue
        k += 1
        keys.append({"time": round(max(t - 0.017, 0.0), 4), "value": round(-(k % steps) / steps, 4), "interp": "step"})
    return keys


def moog_turns():
    """Whose turn each Moog note is: the districts in order, one per note (Quay, Lampmarket, Lanternhill, Eastreach)."""
    turns = {d[0]: [] for d in DISTRICTS}
    for k, t in enumerate(ONSETS["moog"]):
        turns[DISTRICTS[k % len(DISTRICTS)][0]].append(t)
    return turns


# ---------------------------------------------------------------------------------------------------------------
# program terms (each multiplies reg `out` by 1 + gain x its term; temps are clobbered)
def _district(p, pos, t1, cx, cz, rx, rz):
    p.op(kind="constant", dst=t1, constant=[-cx, 0.0, -cz, 0.0])
    p.op(kind="add", dst=t1, srcA=pos, srcB=t1)
    p.op(kind="multiply", dst=t1, srcA=t1, srcB=t1)
    p.op(kind="gradient", dst=t1, srcA=t1, constant=[1.0 / (rx * rx), 0.0, 1.0 / (rz * rz), 1.0], value=-1.0)
    p.op(kind="smoothstep", dst=t1, srcA=t1, constant=[0.0, 0.35, 0.0, 0.0])


def moog_term(p, out, pos, acc, t1, t2, gain, msum=None):
    """out x= 1 + gain x sum(district mask x its Moog envelope); msum (optional) = sum of the masks (the town)."""
    p.op(kind="input", input="worldPosition", dst=pos)
    for i, (name, (cx, cz), (rx, rz)) in enumerate(DISTRICTS):
        _district(p, pos, t1, cx, cz, rx, rz)
        if msum is not None and i == 0:
            p.op(kind="swizzle", dst=msum, srcA=t1, constant=[0.0, 1.0, 2.0, 3.0])   # a copy
        elif msum is not None:
            p.op(kind="add", dst=msum, srcA=msum, srcB=t1)
        p.op("moog_" + name, kind="constant", dst=t2, constant=[0.0, 0.0, 0.0, 0.0])
        if i == 0:
            p.op(kind="multiply", dst=acc, srcA=t1, srcB=t2)
        else:
            p.op(kind="multiply", dst=t1, srcA=t1, srcB=t2)
            p.op(kind="add", dst=acc, srcA=acc, srcB=t1)
    AU.gain_term(p, acc, acc, gain)
    p.op(kind="multiply", dst=out, srcA=out, srcB=acc)


def town_mask(p, pos, msum, t1):
    """msum = the sum of the district masks (1 in the town's districts, 0 out over the isles)."""
    p.op(kind="input", input="worldPosition", dst=pos)
    for i, (name, (cx, cz), (rx, rz)) in enumerate(DISTRICTS):
        _district(p, pos, t1, cx, cz, rx, rz)
        if i == 0:
            p.op(kind="swizzle", dst=msum, srcA=t1, constant=[0.0, 1.0, 2.0, 3.0])
        else:
            p.op(kind="add", dst=msum, srcA=msum, srcB=t1)


def isle_term(p, out, msum, t1, t2):
    """out x= 1 + gain x isleRing x (1 - town): the second kick ring, out over the isles only."""
    p.op(kind="field", dst=t1, field="isleRing")
    p.op(kind="remap", dst=t2, srcA=msum, constant=[0.0, 1.0, 1.0, 0.0], value=1.0)   # clamp(1 - msum)
    p.op(kind="multiply", dst=t1, srcA=t1, srcB=t2)
    AU.gain_term(p, t1, t1, ISLE_RING["gain"])
    p.op(kind="multiply", dst=out, srcA=out, srcB=t1)


def chase_term(p, out, a, b, label, space, period, gain, power=6.0):
    """A band every `period` m along the position's x (local: along a lantern string; world: down the Quay), its
    phase a step timeline (`<label>_phase` value) and its light an envelope (`<label>_amt`)."""
    p.op(kind="input", input=space, dst=a)
    p.op(kind="constant", dst=b, constant=[1.0 / period, 0.0, 0.0, 0.0])
    p.op(kind="multiply", dst=a, srcA=a, srcB=b)
    p.op(label + "_phase", kind="palette", dst=a, srcA=a, constant=[0.5, 0.5, 0.5, 0.0],
         constant2=[0.5, 0.5, 0.5, 0.0], constant3=[1.0, 1.0, 1.0, 0.0], constant4=[0.0, 0.0, 0.0, 0.0], value=0.0)
    p.op(kind="swizzle", dst=a, srcA=a, constant=[0.0, 0.0, 0.0, 0.0])
    p.op(kind="power", dst=a, srcA=a, value=power)
    p.op(label + "_amt", kind="constant", dst=b, constant=[0.0, 0.0, 0.0, 0.0])
    p.op(kind="multiply", dst=a, srcA=a, srcB=b)
    AU.gain_term(p, a, a, gain)
    p.op(kind="multiply", dst=out, srcA=out, srcB=a)


def flick_term(p, lit, a, b):
    """Window flicks: windows on the floors the pattern names flip (lit -> dark, dark -> lit). The pattern is a comb
    over the floor index (local y / 3.2 m) or the column (local x / 2.4 m): 0.5 + 0.5 cos(2 pi (u * scale + phase)),
    sharpened; scale, phase and amount are timeline tracks (`flick_scale`, `flick` value, `flick_amt`), and `flick_axis`
    picks floors or columns."""
    p.op(kind="input", input="localPosition", dst=a)
    p.op("flick_axis", kind="constant", dst=b, constant=[0.0, 0.3125, 0.0, 0.0])
    p.op(kind="multiply", dst=a, srcA=a, srcB=b)
    p.op(kind="gradient", dst=a, srcA=a, constant=[1.0, 1.0, 1.0, 0.0], value=1.0 / 64.0)   # (x + y) / 64, in [0, 1]
    p.op("flick_scale", kind="constant", dst=b, constant=[64.0, 64.0, 64.0, 64.0])
    p.op(kind="multiply", dst=a, srcA=a, srcB=b)
    p.op("flick", kind="palette", dst=a, srcA=a, constant=[0.5, 0.5, 0.5, 0.0], constant2=[0.5, 0.5, 0.5, 0.0],
         constant3=[1.0, 1.0, 1.0, 0.0], constant4=[0.0, 0.0, 0.0, 0.0], value=0.0)
    p.op(kind="swizzle", dst=a, srcA=a, constant=[0.0, 0.0, 0.0, 0.0])
    p.op(kind="power", dst=a, srcA=a, value=8.0)
    p.op("flick_amt", kind="constant", dst=b, constant=[0.0, 0.0, 0.0, 0.0])
    p.op(kind="multiply", dst=a, srcA=a, srcB=b)
    p.op(kind="remap", dst=b, srcA=lit, constant=[0.0, 1.0, 1.0, -1.0], value=0.0)   # 1 - 2 lit
    p.op(kind="multiply", dst=b, srcA=b, srcB=a)
    p.op(kind="add", dst=lit, srcA=lit, srcB=b)


# ---------------------------------------------------------------------------------------------------------------
# the window flicks' patterns, one per pad phrase, in eighth notes over two bars
FLICK_PATTERNS = [
    # (axis, scale (cycles per unit of the comb input: floors or columns), phase per eighth note)
    ("floors", 1.0 / 12.0, -1.0 / 12.0),    # a dark/lit floor climbing the building, a floor an eighth
    ("floors", 0.5, 0.5),                   # alternate floors swapping on every eighth
    ("floors", 1.0 / 12.0, 1.0 / 12.0),     # a floor coming down
    ("columns", 1.0 / 8.0, -1.0 / 8.0),     # a column of windows running across the facade
]


def flick_tracks(prog_path, phrases, song=70.18):
    beat8 = FLICK["beat"]
    n = int(FLICK["bars"] * 1.4 / beat8)
    amt = [{"time": 0.0, "value": [0.0] * 4, "interp": "linear"}]
    ph = [{"time": 0.0, "value": 0.0, "interp": "step"}]
    sc = [{"time": 0.0, "value": [64.0 / 12.0] * 4, "interp": "step"}]
    ax = [{"time": 0.0, "value": [0.0, 0.3125, 0.0, 0.0], "interp": "step"}]
    for i, t0 in enumerate(phrases):
        axis, scale, dphi = FLICK_PATTERNS[i % len(FLICK_PATTERNS)]
        ax.append({"time": round(t0 - 0.05, 3), "value": [0.0, 0.3125, 0.0, 0.0] if axis == "floors" else
                   [1.0 / 2.4, 0.0, 0.0, 0.0], "interp": "step"})
        sc.append({"time": round(t0 - 0.05, 3), "value": [64.0 * scale] * 4, "interp": "step"})
        for k in range(n):
            ph.append({"time": round(t0 + k * beat8, 3), "value": round((k * dphi) % 1.0, 4), "interp": "step"})
        end = t0 + n * beat8
        amt += [{"time": round(t0 - 0.001, 3), "value": [0.0] * 4, "interp": "linear"},
                {"time": round(t0 + 0.04, 3), "value": [1.0] * 4, "interp": "linear"},
                {"time": round(end - 0.15, 3), "value": [1.0] * 4, "interp": "linear"},
                {"time": round(end + 0.25, 3), "value": [0.0] * 4, "interp": "linear"}]
    return [AU.track(prog_path("flick_amt"), amt), AU.track(prog_path("flick", "value"), ph),
            AU.track(prog_path("flick_scale"), sc), AU.track(prog_path("flick_axis"), ax)]


# ---------------------------------------------------------------------------------------------------------------
# the second onset field: the kick ring out over the isles
def isle_ring_field(origin):
    f = AU.kick_ring_field(origin)
    f.update({"name": "isleRing", "audioSpeed": ISLE_RING["speed"], "onsetWidth": 14.0, "onsetDecay": 0.6,
              "falloff": {"kind": "linear", "inner": ISLE_RING["inner"], "outer": ISLE_RING["outer"]}})
    return f


# ---------------------------------------------------------------------------------------------------------------
# the cosmic ocean breathes: a ring from the break's centre on each pad phrase
def ocean_ops(base_ops, centre, radius):
    """ndOcean's ops plus the ring: band = clamp(1 - ((|p - c| / R - r(t)) / w)^2) x amount(t), emission x (1 + g band).
    Registers 0-6 are the ocean's; the ring uses 0 and 1 after the colour (5) is made, and 7."""
    ops = list(base_ops)
    cx, cz = centre
    ops += [
        {"kind": "input", "input": "worldPosition", "dst": 0},
        {"kind": "constant", "dst": 1, "constant": [-cx, 0.0, -cz, 0.0]},
        {"kind": "add", "dst": 0, "srcA": 0, "srcB": 1},
        {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 0},
        {"kind": "gradient", "dst": 0, "srcA": 0, "constant": [1.0 / radius ** 2, 0.0, 1.0 / radius ** 2, 0.0],
         "value": 1.0},
        {"kind": "power", "dst": 0, "srcA": 0, "value": 0.5},                      # |p - c| / R
        {"kind": "constant", "dst": 1, "constant": [0.0, 0.0, 0.0, 0.0]},          # -r(t) / R  (track)
        {"kind": "add", "dst": 0, "srcA": 0, "srcB": 1},
        {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 0},
        {"kind": "remap", "dst": 0, "srcA": 0,
         "constant": [0.0, (OCEAN["width"] / radius) ** 2, 1.0, 0.0], "value": 1.0},  # clamp(1 - x / w^2)
        {"kind": "constant", "dst": 7, "constant": [0.0, 0.0, 0.0, 0.0]},          # amount (track)
        {"kind": "multiply", "dst": 0, "srcA": 0, "srcB": 7},
        {"kind": "remap", "dst": 0, "srcA": 0, "constant": [0.0, 1.0, 1.0, 1.0 + OCEAN["gain"]], "value": 0.0},
        {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 0},
    ]
    radius_op = len(base_ops) + 7     # 1-based index of the -r(t)/R constant
    amount_op = len(base_ops) + 11
    return ops, radius_op, amount_op


def ocean_tracks(name, radius_op, amount_op, radius, phrases, song=70.18):
    rk = [{"time": 0.0, "value": [0.0] * 4, "interp": "linear"}]
    ak = [{"time": 0.0, "value": [0.0] * 4, "interp": "linear"}]
    sp, tr, rise = OCEAN["speed"], OCEAN["travel"], OCEAN["rise"]
    for t0 in phrases:
        rk += [{"time": round(t0 - 0.001, 3), "value": [0.0] * 4, "interp": "step"},
               {"time": round(t0, 3), "value": [0.0] * 4, "interp": "linear"},
               {"time": round(t0 + tr, 3), "value": [round(-sp * tr / radius, 4)] * 4, "interp": "linear"}]
        ak += [{"time": round(t0, 3), "value": [0.0] * 4, "interp": "linear"},
               {"time": round(t0 + rise, 3), "value": [1.0] * 4, "interp": "linear"},
               {"time": round(t0 + tr * 0.6, 3), "value": [0.7] * 4, "interp": "linear"},
               {"time": round(t0 + tr, 3), "value": [0.0] * 4, "interp": "linear"}]
    return [AU.track("material/%s/op/%d/constant/constant" % (name, radius_op), rk),
            AU.track("material/%s/op/%d/constant/constant" % (name, amount_op), ak)]

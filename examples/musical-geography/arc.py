"""Musical Geography CP-D: the time-of-day arc over the whole of Feline Footwear, as timeline tracks.

The arc is the Gate 1 recommendation (04- §3): day -> sunset -> night, with night on the song's climax and held to the end
(draft12: the dawn return over the last ~15% was cut). One continuous flight; the states change in flight.

How it is driven (no engine change):
  * `env/dayNight/dayPhase` is keyed on the timeline. It moves the sun and every DayNight curve (sky, sun, fog, haze,
    fill). Phase runs forward only: 0.61 (day) -> 0.72 (sunset) -> 1.0 = 0.0 (night, a one-frame step across the ring
    seam, where both sides are night) -> 0.27 (dawn).
  * Everything DayNight does not own is keyed alongside it at the same blend: the tile programs' constants (one program
    per ring for the whole piece, every optional op kept -- look.tile_program(always=True)), the post grade, the ambient,
    the violet fill light, the far ring's colour, the aliens' glow and their night halos.
  * Colours blend in OKLab (sunset to night through linear RGB passes through a grey-brown, 04- §3). The weights
    follow the DayNight curves' own smoothstep, so the land and the sky turn together.
  * The lights switch on rather than cross-fade (04- §3 Transitions): toward night the path lights first, then the
    contours, then the pads and the aliens' halos; toward dawn they go off in the reverse order.

Sections (measured from the audio's energy bands; 04- §13 CP-D):
  0-8 intro, 8-28 build, 28-112 main, 112-130 the lift, 130-184 the climax, 184-202 the outro, 202-210.5 the fade.
"""
from __future__ import annotations

import math

import look

SONG = 210.5

# (time s, state) holds; the phase moves linearly between consecutive keys (a transition), or holds (same state).
ARC = [
    (0.0, "day"),
    (66.0, "day"),        # the day holds through the sea, the shore, the path and the crowns
    (100.0, "sunset"),    # the sweep and the river at sunset
    (108.0, "sunset"),
    (128.0, "night"),     # dusk through the lift; night lands as the climax begins (~130)
    (SONG, "night"),      # draft12 (owner: "lets also end on the night cycle ... keep it dark to end with"): no dawn
                          # return -- night holds from ~130 s through the last frame
]

# toward night the lights come on in order: path (sunset), contours (dusk), pads + halos (night)
STAGGER = {"path": (0.0, 0.5), "edge": (0.3, 0.75), "pad": (0.55, 1.0)}


def _smooth(e0, e1, x):
    t = min(max((x - e0) / max(e1 - e0, 1e-6), 0.0), 1.0)
    return t * t * (3 - 2 * t)


# ---- OKLab --------------------------------------------------------------------------------------------------------
def _hex_srgb(h):
    h = h.lstrip("#")
    return [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]


def _lin(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _enc(c):
    c = min(max(c, 0.0), 1.0)
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def _to_oklab(rgb):
    r, g, b = rgb
    l_ = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m_ = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s_ = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
    return [0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
            1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
            0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_]


def _from_oklab(lab):
    L, a, b = lab
    l_ = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m_ = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s_ = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    return [4.0767416621 * l_ - 3.3077115913 * m_ + 0.2309699292 * s_,
            -1.2684380046 * l_ + 2.6097574011 * m_ - 0.3413193965 * s_,
            -0.0041960863 * l_ - 0.7034186147 * m_ + 1.7076147010 * s_]


def mix_hex(a, b, w):
    """Two sRGB hex colours mixed in OKLab, back to sRGB hex (look.hexlin linearises it)."""
    if w <= 0:
        return a
    if w >= 1:
        return b
    la = _to_oklab([_lin(c) for c in _hex_srgb(a)])
    lb = _to_oklab([_lin(c) for c in _hex_srgb(b)])
    rgb = _from_oklab([x + (y - x) * w for x, y in zip(la, lb)])
    return "#" + "".join("%02X" % int(round(_enc(c) * 255)) for c in rgb)


# ---- the states, completed and blended ----------------------------------------------------------------------------
def full_state(name):
    """A state with every key the program and the arc read (the optional ones at their off values)."""
    s = dict(look.STATES[name])
    s.setdefault("stops", list(look.LAND_STOPS))
    s.setdefault("border_fine", look.BORDER_DARK_FINE)
    s.setdefault("water_emit", 0.0)
    s.setdefault("fill_col", "#000000")
    s.setdefault("fill_int", 0.0)
    s.setdefault("sun_az_offset", 0.0)
    s["glow"] = 1.0 if name == "night" else 0.0   # the aliens' night glow and halos
    s.update(look.CLOUD_STATE[name])               # the clouds (CP-D r2 v2)
    s.update(look.WATER_BODIES[name])              # the rivers' and ponds' colours and glows (CP-D r2 land)
    s["landing_gain"], s["landing_color"] = look.LANDING_GLOW[name][0], look.LANDING_GLOW[name][1]
    return s


GAIN_CHANNEL = {"path_emit": "path", "path": "path", "edge_gain": "edge", "edge": "edge", "pad_emit": "pad",
                "pad": "pad", "glow": "pad", "water_emit": "pad"}


def blend(a, b, u):
    """State a -> b at linear fraction u (0..1) of the transition. Colours in OKLab at the DayNight smoothstep;
    the light gains on their staggered windows (toward night: on in order; away from night: off in reverse)."""
    A, B = full_state(a), full_state(b)
    w = _smooth(0.0, 1.0, u)
    to_night, from_night = b == "night", a == "night"

    def weight(key):
        ch = GAIN_CHANNEL.get(key)
        if ch is None or not (to_night or from_night):
            return w
        e0, e1 = STAGGER[ch]
        if to_night:
            return _smooth(e0, e1, u)
        return _smooth(1.0 - e1, 1.0 - e0, u)   # mirrored: pads off first, path last

    out = {}
    for k in set(A) | set(B):
        x, y = A.get(k), B.get(k)
        if isinstance(x, str) and x.startswith("#"):
            out[k] = mix_hex(x, y, weight(k))
        elif isinstance(x, list) and x and isinstance(x[0], str):
            out[k] = [mix_hex(p, q, weight(k)) for p, q in zip(x, y)]
        elif isinstance(x, list):
            out[k] = [p + (q - p) * weight(k) for p, q in zip(x, y)]
        elif isinstance(x, (int, float)) and not isinstance(x, bool):
            out[k] = x + (y - x) * weight(k)
        else:
            out[k] = x if w < 0.5 else y
    return out


def phase_of(a, b, u):
    pa, pb = look.STATES[a]["phase"], look.STATES[b]["phase"]
    if pb < pa:                       # sunset -> night crosses the ring seam: run on to 1.0
        pb += 1.0
    return pa + (pb - pa) * u


def at(t):
    """(phase, blended state) at song time t."""
    for (t0, a), (t1, b) in zip(ARC[:-1], ARC[1:]):
        if t0 <= t <= t1:
            u = 0.0 if t1 <= t0 else (t - t0) / (t1 - t0)
            if a == b:
                return look.STATES[a]["phase"], full_state(a)
            return phase_of(a, b, u), blend(a, b, u)
    return look.STATES[ARC[-1][1]]["phase"], full_state(ARC[-1][1])


def sample_times(step=1.0):
    """Every hold's ends, and every transition at `step`."""
    ts = set()
    for (t0, a), (t1, b) in zip(ARC[:-1], ARC[1:]):
        ts.add(t0)
        ts.add(t1)
        if a != b:
            n = max(1, int(math.ceil((t1 - t0) / step)))
            ts.update(t0 + (t1 - t0) * k / n for k in range(n + 1))
    return sorted(ts)


def phase_track():
    """dayPhase keys; the ring seam is crossed with a step (1.0 and 0.0 are both midnight)."""
    keys = []
    for t in sample_times(2.0):
        p, _ = at(t)
        keys.append([t, p])
    out = []
    for i, (t, p) in enumerate(keys):
        if p >= 1.0:
            # hold just under 1 until the seam, then the night key at 0 one frame later
            out.append({"time": round(t, 4), "value": 0.9995, "interp": "step"})
            out.append({"time": round(t + 1.0 / 60.0, 4), "value": 0.0, "interp": "linear"})
            continue
        out.append({"time": round(t, 4), "value": round(p, 6), "interp": "linear"})
    # after the seam, night holds at 0.0 until the dawn transition starts (drop the extra 0 keys' duplicates)
    dedup = []
    for k in out:
        if dedup and abs(dedup[-1]["time"] - k["time"]) < 1e-6:
            dedup[-1] = k
        else:
            dedup.append(k)
    return dedup


def track(target, keys):
    return {"target": target, "component": -1, "enabled": True, "timeBase": "seconds", "mode": "replace",
            "loopLength": 0.0, "keys": keys}


def keyed(target, samples):
    """samples: [(t, value)] -> a track, dropping keys that a linear interpolation reproduces exactly."""
    keys = []
    for i, (t, v) in enumerate(samples):
        if 0 < i < len(samples) - 1 and samples[i - 1][1] == v == samples[i + 1][1]:
            continue
        keys.append({"time": round(t, 4), "value": v, "interp": "linear"})
    return track(target, keys)


def is_static(samples):
    return all(s[1] == samples[0][1] for s in samples)

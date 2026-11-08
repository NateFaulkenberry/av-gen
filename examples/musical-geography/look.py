"""Musical Geography: the art direction as data -- the four palette states, the sky and fog curves, the tile's
material program and the post. Everything that decides how the scene looks, in one place; build.py assembles it.

Source of truth for every value: docs/prototypes/musical-geography/04-art-direction.md (§3 palettes, §4 tile
language, §6 light carriers, §11 look requirements). Change a value here and in 04- together.

What is live today and what waits for engineering (03-technical-plan.md §5, 02-requests.md):
  * LIVE now:   the DayNight curves for sky, sun, fog and haze (ADR-343/350 keys), the sun light, the fog medium
                (ADR-705), post (bloom, tonemap look, grade, grain), the tile material program's top band, side
                shade and hypsometric tint (it reads worldPosition.y, which is the tile's height on a flat-topped slab).
  * WAITS:      TERRAIN_KEYS (request 1972: per-state land/water ramps, under colour, path/pad colours, edge and crest
                gains as DayNight curves). Until 1972 lands, build.py bakes ONE state's terrain colours into the
                program's constants (--state), so a still in any state is still correct; a phase sweep is not.
                The contour side mask (1970), crest weight (1971), region channels (1973) and lattice randoms (1978)
                are read by the program the moment they exist; until then the program draws no contour band.
"""
from __future__ import annotations

PI = 3.141592653589793


def hexlin(h: str) -> list[float]:
    """sRGB hex -> linear RGB (the engine takes linear)."""
    h = h.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(round(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4, 5))
    return out


# ---- the four states (04- §3). `phase` is DayNight's: 0 midnight, 0.25 sunrise, 0.5 noon, 0.75 sunset. ----------
# The song arc (04- §3, owner-approved recommendation): day -> sunset -> night -> dawn, i.e. phase 0.5 -> 0.72 ->
# 0.0 (=1.0) -> 0.27, always forward in time.
STATES = {
    "day": dict(
        fill=0.45, contrast=1.25,
        sky_int=1.0, haze=0.35,
        moon_int=0.0, phase=0.64,   # r5: a low afternoon sun (~25 deg) from the south-west -- gentle slab-limited slopes need grazing light to shade
        sky_zen="#2F6BFF", sky_hor="#A8C4FF", sky_ground="#A8C4FF", fog="#A8C4FF", fog_sun="#FFF2D6",
        sun="#FFF4E0", sun_int=4.0, amb="#A6B6FF", amb_int=0.3,
        land=["#8A6CF2", "#C26AE4", "#FF7A8C", "#FF9E4A", "#FFC444"],
        water=["#0B36D0", "#0F44E0", "#3A86FF", "#8CC6FF"], shallow="#2EC2D2", under="#4A3C9A",
        path="#FFF5E2", path_emit=0.35, pad="#FF8FB4", pad_emit=0.0,
        drift="#8E9CF2", drift_amt=0.14, edge="#FFF4C8", edge_gain=0.0, crest_gain=0.0,
        fog_mult=4.5, exposure=-0.5, saturation=1.6, look="punchy", bloom=0.25),
    "sunset": dict(
        fill=0.55, contrast=1.22,
        sky_int=0.9, haze=0.6,
        moon_int=0.0, phase=0.72,
        sky_zen="#4A22B0", sky_hor="#FF8A50", sky_ground="#FF8A50", fog="#FF8A50", fog_sun="#FFB060",
        sun="#FFB848", sun_int=3.4, amb="#5E46B8", amb_int=1.0,
        land=["#4E2A7E", "#8A2C78", "#D0405A", "#FF7448", "#FFA032"],
        water=["#24164F", "#4A2380", "#8A2F8C", "#E2507A"], shallow="#9A3A9A", under="#3A1A4E",
        path="#FFD7A0", path_emit=0.6, pad="#FFB070", pad_emit=0.0,
        drift="#6A3AA0", drift_amt=0.28, edge="#FFC27A", edge_gain=0.6, crest_gain=0.6,
        fog_mult=5.0, exposure=-0.1, saturation=1.2, look="medium-high", bloom=0.25),
    "night": dict(
        fill=0.9, contrast=1.1,
        sky_int=0.9, haze=0.5,
        moon_int=2.2, phase=0.0,
        sky_zen="#080A2C", sky_hor="#33237A", sky_ground="#33237A", fog="#33237A", fog_sun="#41308E",
        sun="#C8B4FF", sun_int=0.0, amb="#5A38B0", amb_int=0.6,
        land=["#221A6C", "#36228E", "#52289E", "#7430B0", "#9C44C8"],
        water=["#0E1452", "#18227A", "#2443B0", "#3F78FF"], shallow="#2A3AA0", under="#120E3A",
        path="#FF5FB0", path_emit=5.0, pad="#FF4FA8", pad_emit=6.0,
        drift="#5A2CB4", drift_amt=0.28, edge="#5A86FF", edge_gain=7.0, crest_gain=5.0,
        fog_mult=0.85, exposure=0.6, saturation=1.0, look="medium-high", bloom=0.6),
    "dawn": dict(
        fill=0.7, contrast=1.12,
        sky_int=1.0, haze=0.55,
        moon_int=0.0, phase=0.27,
        sky_zen="#A6BCDC", sky_hor="#FFD9A4", sky_ground="#FFD9A4", fog="#FFD9A4", fog_sun="#FFE2A0",
        sun="#FFC874", sun_int=4.4, amb="#B8C8D8", amb_int=0.55,
        land=["#6FB57E", "#9CCC84", "#DCE08E", "#F4D68C", "#FBE9C4"],
        water=["#3E5E98", "#5A7EB8", "#86A6D0", "#F2DCA8"], shallow="#8CBFA8", under="#4A6A55",
        path="#F2A93B", path_emit=0.25, pad="#FFC060", pad_emit=0.0,
        drift="#F4C49A", drift_amt=0.28, edge="#FFD27A", edge_gain=0.5, crest_gain=0.4,
        fog_mult=1.35, exposure=-0.2, saturation=1.0, look="medium-high", bloom=0.25),
}
# Contour gains as approved by the owner (04- §11): on at dusk and night, faint or off by day. The sunset key is 0.6
# (faint); the dusk key between sunset and night brings the lines in at full night strength.
DUSK = dict(phase=0.80, edge_gain=4.0)

# Land ramp height stops, in tiles (absolute height, never renormalised; 04- §3).
LAND_STOPS = [0.0, 10.0 / 3, 20.0 / 3, 10.0, 20.0]   # stops 0-3 evenly spaced (the oklabRamp op); lowered in CP-A r4 so the warm
                                                       # bands cover about half the land, as in the r2 mock-up
SEA_LEVEL = -0.4

# ---- the tile (04- §4) ---------------------------------------------------------------------------------------------
TILE = dict(spacing=1.0, tileSize=0.915, thickness=0.30, bevel=0.0)
TOP_BAND = 0.085          # edge band width, fraction of the top (contour edges, 1970)
SIDE_FOOT = 0.70          # side face fades to 70% of the top colour at the slab's underside (0.18 in the mock-up: in the
                          # engine the slope-facing sides became a dark stipple across every hillside)
ROUGH_LAND, ROUGH_WATER = 0.8, 0.42   # land 0.62 -> 0.8 in r5: a low sun put a white glare over the land   # water 0.16 in the mock-up; 0.28 in the engine (0.16 mirrored a pale sky over the whole sea)


def ordered_states():
    return sorted(STATES.items(), key=lambda kv: kv[1]["phase"])


def _curve(key, f=lambda v: v):
    return [{"phase": s["phase"], "value": f(s[key])} for _, s in ordered_states()]


def day_night(cycle_seconds=600.0, paused=True, phase=0.5):
    """The environment's DayNight block: ADR-343 settings + ADR-350 curves, keyed at the four states."""
    col = lambda k: _curve(k, hexlin)   # noqa: E731
    return {
        "enabled": True,
        "cycleSeconds": cycle_seconds,
        "paused": paused,
        "dayPhase": phase,
        "sunLight": "sun",
        "moonLight": "moon",
        "moonIntensity": [{"phase": st["phase"], "value": st["moon_int"]} for _, st in ordered_states()],
        # 42 deg at noon (04- §3 daylight); sunset and dawn elevations come from their phases (9 and 13 deg)
        "sunPeakElevation": 0.733,
        "zenithColor": col("sky_zen"),
        "horizonColor": col("sky_hor"),
        "groundColor": col("fog"),        # below the horizon the sky is the fog: no band where the window ends
        "sunColor": col("sun"),
        "fogColor": col("fog"),
        "sunIntensity": _curve("sun_int"),
        # our own sky brightness and haze per state (the engine's defaults dim the night sky to 0.16, near black)
        "skyIntensity": _curve("sky_int"),
        "haze": _curve("haze"),
        # the sky's light on the land at full strength in every state: the fill is how the shadow side gets its
        # colour (violet at sunset, indigo at night); the engine's default dims it to 0.16-0.46 off-noon
        "hdriIntensity": _curve("fill"),   # r5: separate from the visible sky -- a lower fill gives dark shaded sides
    }


def fog_medium(state):
    """The single fog medium (ADR-705). Density grows with distance (horizonDensity) so near and mid ground stay
    clean and the far ring fades fully (request 1977, 04- §4). Thinner by day, denser at night: fog_mult."""
    m = STATES[state]["fog_mult"]
    return {
        "fogColor": hexlin(STATES[state]["fog"]),
        "volumeDensity": round(0.0009 / m, 6),
        "volumeMaxDistance": 0.0,      # surface-only fog: the same air in closed form, no march (about 8 ms saved)
        "horizonDensity": 9.0,         # near and mid ground clean, the far ring fully veiled (no window edge)
        "fogHeight": 2.0,
        "fogHeightFalloff": 0.035,
        "volumeAnisotropy": 0.6,
        "volumeScattering": 0.6,
        "volumeNoise": 0.0,
    }


def sky(state):
    s = STATES[state]
    return {
        "enabled": True, "background": True, "useKeyLight": True,
        "zenithColor": hexlin(s["sky_zen"]), "horizonColor": hexlin(s["sky_hor"]),
        "groundColor": hexlin(s["fog"]), "haze": s["haze"], "sunColor": hexlin(s["fog_sun"]),
        "sunIntensity": 0.0, "sunSize": 0.02, "sunGlow": 0.08, "intensity": 1.0,
    }


def moon_light(state):
    """The night key (DayNight drives its intensity and direction; the sun is below the horizon at night)."""
    return {"id": "moon", "name": "moon", "type": "directional", "direction": [-0.4, -0.7, 0.55],
            "color": hexlin(STATES["night"]["sun"]), "intensity": STATES[state]["moon_int"],
            # only the night casts moon shadows: a second shadow map in daylight costs GPU time for nothing
            "castsShadow": STATES[state]["moon_int"] > 0.0,
            "softness": 0.35}


def sun_light(state):
    s = STATES[state]
    return {"id": "sun", "name": "sun", "type": "directional", "direction": [0.55, -0.6, 0.58],
            "color": hexlin(s["sun"]), "intensity": s["sun_int"], "castsShadow": True, "softness": 0.25}


# ---- the tile material program ------------------------------------------------------------------------------------
def _c4(rgb, a=1.0):
    return list(rgb) + [a]


SHALLOW_AMT = 0.33       # shallows mixed in at most 33% (04- §3)
BORDER_DARK_FINE = 1.0    # the 1x ring: its geometric gap already draws the edge; a dark painted line on top of it
                          # beat into moire rings at grazing angles
UNDER_SHADE = 0.72        # the underlay takes the land's own colour, darkened: the gaps read as shadow, not as a
                          # second, blue-black surface speckling through every hillside
BORDER_DARK = 0.62       # the 1-px border's darkening (ADR-1976 B: the tile reads at any distance)
BORDER_BAND = 0.03       # tileBorder's band (engineering's fixture value)
RING_THICKNESS = {1.0: 0.30, 2.0: 0.36, 4.0: 0.72}
JITTER = 0.04            # per-tile value jitter (04- §4 had 0.0225; raised for the far rings, §11; the op budget
                         # of 64 has no room for a distance-graded amount)
PAD_HEIGHT = (17.0, 20.0)              # crowns light only up here (04- §6)
PAD_CLUSTER = 0.2, 0.95                # 5-tile blocks; about 1 in 20 blocks lit


def tile_program(state, cell=1.0, name=None, far=None, shade=1.0, border=None):
    """The slab's whole look, for one palette state and one ring.

    Reads: localPosition through engineering's tileBorder (ADR-1976: the border's coverage, prefiltered per axis below
    4 px a tile, so no moire; the top-face mask), worldPosition.y (the tile's top on a flat face), instanceRandom =
    (water, shore, flat, path) and instanceColor.a = the lattice hash (ADR-1950/1978). Writes base colour, roughness
    and top-only emission. The 2x and 4x rings close their geometric gaps (box = cell) and paint the fine lattice
    with tileBorder's subdivisions; `far` (default: cell > 1) drops the pads and the jitter there, for cost.
    Terrain colours are baked from `state` until request 1972 makes them DayNight curves; contour edges (1970) and
    crest light (1971) are added when their channels exist (Phase 3).

    Registers: r0 localPosition, r1 tileBorder (coverage, px, top, tile px), r2 colour, r3/r7 scratch,
    r4 worldPosition, r5 random, r6 roughness, then r4 emission."""
    s = STATES[state]
    far = cell > 1.0 if far is None else far
    name = name or "mgTile%dx_%s" % (int(cell), state)
    land = [hexlin(c) for c in s["land"]]
    gap = TILE["tileSize"] if cell == 1.0 else 1.0
    half = cell * gap / 2.0
    thick = RING_THICKNESS.get(cell, 0.3)
    sub = int(cell)
    B = lambda k: [float(k)] * 4   # noqa: E731
    o = []
    add = o.append
    add({"kind": "input", "input": "localPosition", "dst": 0})
    add({"kind": "tileBorder", "dst": 1, "srcA": 0, "constant": [half, half, BORDER_BAND, thick / 2],
         "constant2": [sub, 0, 0, 0], "value": 1.0})
    add({"kind": "input", "input": "worldPosition", "dst": 4})
    add({"kind": "swizzle", "dst": 3, "srcA": 4, "constant": B(1)})                       # y
    # the height ramp, blended in OKLab: stops 0-3 over 0..LAND_STOPS[3] (evenly spaced, ADR-1470), then the top stop
    add({"kind": "remap", "dst": 7, "srcA": 3, "constant": [0, LAND_STOPS[3], 0, 1], "value": 1.0})
    add({"kind": "oklabRamp", "dst": 2, "srcA": 7, "value": 1.0, "constant": _c4(land[0]),
         "constant2": _c4(land[1]), "constant3": _c4(land[2]), "constant4": _c4(land[3])})
    add({"kind": "smoothstep", "dst": 7, "srcA": 3, "constant": [LAND_STOPS[3], LAND_STOPS[4], 0, 0]})
    add({"kind": "constant", "dst": 6, "constant": _c4(land[4])})
    add({"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 6, "srcC": 7})
    if not far:   # pads: crown x high x a 5-tile block picked by its hash (before r3/r4 are reused)
        add({"kind": "smoothstep", "dst": 7, "srcA": 3, "constant": [PAD_HEIGHT[0], PAD_HEIGHT[1], 0, 0]})
        add({"kind": "quantize", "dst": 6, "srcA": 4, "value": PAD_CLUSTER[0]})
        add({"kind": "swizzle", "dst": 6, "srcA": 6, "constant": B(3)})
        add({"kind": "threshold", "dst": 6, "srcA": 6, "value": PAD_CLUSTER[1]})
        add({"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 6})                         # r7 = pad site
    add({"kind": "input", "input": "instanceRandom", "dst": 5})                            # water shore flat path
    if not far:
        add({"kind": "swizzle", "dst": 6, "srcA": 5, "constant": B(2)})
        add({"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 6})                         # x crown
        add({"kind": "constant", "dst": 4, "constant": _c4([v * s["pad_emit"] for v in hexlin(s["pad"])])})
        add({"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 7})                         # r4 = emission
    else:
        add({"kind": "constant", "dst": 4, "constant": [0, 0, 0, 1]})
    add({"kind": "swizzle", "dst": 3, "srcA": 5, "constant": B(0)})                        # water
    add({"kind": "remap", "dst": 6, "srcA": 3, "constant": [0, 1, ROUGH_LAND, ROUGH_WATER], "value": 1.0})
    add({"kind": "constant", "dst": 7, "constant": _c4(hexlin(s["water"][0]))})
    add({"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 7, "srcC": 3})
    add({"kind": "swizzle", "dst": 3, "srcA": 5, "constant": B(1)})                        # shore -> shallows
    add({"kind": "constant", "dst": 7, "constant": _c4(hexlin(s["shallow"]))})
    add({"kind": "mix", "dst": 7, "srcA": 2, "srcB": 7, "value": SHALLOW_AMT})
    add({"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 7, "srcC": 3})
    add({"kind": "swizzle", "dst": 3, "srcA": 5, "constant": B(3)})                        # path
    add({"kind": "constant", "dst": 7, "constant": _c4(hexlin(s["path"]))})
    add({"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 7, "srcC": 3})
    add({"kind": "constant", "dst": 7, "constant": _c4([v * s["path_emit"] for v in hexlin(s["path"])])})
    add({"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 7})
    add({"kind": "add", "dst": 4, "srcA": 4, "srcB": 3})
    if not far:   # per-tile value jitter on the lattice hash (request 1978)
        add({"kind": "input", "input": "instanceColor", "dst": 3})
        add({"kind": "swizzle", "dst": 3, "srcA": 3, "constant": B(3)})
        add({"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, 1 - JITTER, 1 + JITTER], "value": 1.0})
        add({"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3})
    # the side shade over the slab's local height (top face: 1)
    add({"kind": "swizzle", "dst": 5, "srcA": 1, "constant": B(2)})                        # top mask
    add({"kind": "swizzle", "dst": 3, "srcA": 0, "constant": B(1)})
    add({"kind": "remap", "dst": 3, "srcA": 3, "constant": [-thick / 2, thick / 2, SIDE_FOOT, 1.0], "value": 1.0})
    add({"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 5})
    add({"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3})
    # the tile border (tileBorder's coverage; it draws the fine lattice on the coarse rings)
    add({"kind": "swizzle", "dst": 3, "srcA": 1, "constant": B(0)})
    bd = border if border is not None else (BORDER_DARK_FINE if cell == 1.0 else BORDER_DARK)
    add({"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, shade, shade * bd], "value": 1.0})
    add({"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3})
    add({"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5})                              # emission: top only
    return {"name": name, "ops": o, "baseColor": 2, "metallic": -1, "roughness": 6, "emission": 4,
            "emissionIntensity": 1.0, "opacity": -1}


# ---- post (04- §3 grade rows, §11 banding) ------------------------------------------------------------------------
# AgX (operator 1, the default) in every state; the day's "Punchy" is a grade (contrast, saturation) on top of it.


def post(state):
    s = STATES[state]
    return {
        "post/bloom/enabled": True,
        "post/bloom/intensity": s["bloom"],
        "post/bloom/threshold": 1.0,
        "camera/exposure/compensation": s["exposure"],
        "post/grade/saturation": s["saturation"],
        "post/grade/contrast": s["contrast"],
        # §11 banding: film grain is the only dither the output has today (no output dither exists; asked for in
        # 02-requests.md). Kept just above the 8-bit step so slow sky gradients break up without visible noise.
        "post/output/grain": 0.035,
    }

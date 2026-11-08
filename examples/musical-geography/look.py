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
        phase=0.50,
        sky_zen="#3C78FF", sky_hor="#D6E2FF", sky_ground="#C8D6FA", fog="#C8D6FA", fog_sun="#FFF2D6",
        sun="#FFF4E0", sun_int=4.6, amb="#A6B6FF", amb_int=0.6,
        land=["#8A6CF2", "#C26AE4", "#FF7A8C", "#FFA64E", "#FFD84A"],
        water=["#0B36D0", "#1C58F2", "#3A86FF", "#8CC6FF"], shallow="#2EC2D2", under="#1A1E66",
        path="#FFF5E2", path_emit=0.0, pad="#FF8FB4", pad_emit=0.0,
        drift="#8E9CF2", drift_amt=0.14, edge="#FFF4C8", edge_gain=0.0, crest_gain=0.0,
        fog_mult=1.7, exposure=-0.1, saturation=1.30, look="punchy", bloom=0.25),
    "sunset": dict(
        phase=0.72,
        sky_zen="#33206E", sky_hor="#FF8A50", sky_ground="#E07462", fog="#E07462", fog_sun="#FFB060",
        sun="#FF9A4A", sun_int=5.0, amb="#5E46B8", amb_int=0.9,
        land=["#4E2A7E", "#8A2C78", "#D0405A", "#F2785A", "#FFB45A"],
        water=["#24164F", "#4A2380", "#8A2F8C", "#E2507A"], shallow="#9A3A9A", under="#1E0E2E",
        path="#FFD7A0", path_emit=0.6, pad="#FFB070", pad_emit=0.0,
        drift="#6A3AA0", drift_amt=0.28, edge="#FFC27A", edge_gain=0.6, crest_gain=0.6,
        fog_mult=1.35, exposure=-0.2, saturation=1.0, look="medium-high", bloom=0.25),
    "night": dict(
        phase=0.0,
        sky_zen="#080A2C", sky_hor="#33237A", sky_ground="#2C1F66", fog="#2C1F66", fog_sun="#41308E",
        sun="#C8B4FF", sun_int=1.2, amb="#5A38B0", amb_int=0.6,
        land=["#221A6C", "#36228E", "#52289E", "#7430B0", "#9C44C8"],
        water=["#0E1452", "#18227A", "#2443B0", "#3F78FF"], shallow="#2A3AA0", under="#06061E",
        path="#FF5FB0", path_emit=5.0, pad="#FF4FA8", pad_emit=6.0,
        drift="#5A2CB4", drift_amt=0.28, edge="#5A86FF", edge_gain=7.0, crest_gain=5.0,
        fog_mult=0.85, exposure=0.6, saturation=1.0, look="medium-high", bloom=0.6),
    "dawn": dict(
        phase=0.27,
        sky_zen="#A6BCDC", sky_hor="#FFD9A4", sky_ground="#F2DDB9", fog="#F2DDB9", fog_sun="#FFE2A0",
        sun="#FFC874", sun_int=4.4, amb="#B8C8D8", amb_int=0.55,
        land=["#6FB57E", "#9CCC84", "#DCE08E", "#F4D68C", "#FBE9C4"],
        water=["#3E5E98", "#5A7EB8", "#86A6D0", "#F2DCA8"], shallow="#8CBFA8", under="#2A3A3A",
        path="#F2A93B", path_emit=0.25, pad="#FFC060", pad_emit=0.0,
        drift="#F4C49A", drift_amt=0.28, edge="#FFD27A", edge_gain=0.5, crest_gain=0.4,
        fog_mult=1.35, exposure=-0.2, saturation=1.0, look="medium-high", bloom=0.25),
}
# Contour gains as approved by the owner (04- §11): on at dusk and night, faint or off by day. The sunset key is 0.6
# (faint); the dusk key between sunset and night brings the lines in at full night strength.
DUSK = dict(phase=0.80, edge_gain=4.0)

# Land ramp height stops, in tiles (absolute height, never renormalised; 04- §3).
LAND_STOPS = [0.0, 5.0, 12.0, 20.0, 30.0]
SEA_LEVEL = -0.4

# ---- the tile (04- §4) ---------------------------------------------------------------------------------------------
TILE = dict(spacing=1.0, tileSize=0.915, thickness=0.30, bevel=0.0)
TOP_BAND = 0.085          # edge band width, fraction of the top (contour edges, 1970)
SIDE_FOOT = 0.18          # side face fades to 18% of the top colour at the slab's underside
ROUGH_LAND, ROUGH_WATER = 0.62, 0.16


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
        # 42 deg at noon (04- §3 daylight); sunset and dawn elevations come from their phases (9 and 13 deg)
        "sunPeakElevation": 0.733,
        "zenithColor": col("sky_zen"),
        "horizonColor": col("sky_hor"),
        "groundColor": col("sky_ground"),
        "sunColor": col("sun"),
        "fogColor": col("fog"),
        "sunIntensity": _curve("sun_int"),
    }


def fog_medium(state):
    """The single fog medium (ADR-705). Density grows with distance (horizonDensity) so near and mid ground stay
    clean and the far ring fades fully (request 1977, 04- §4). Thinner by day, denser at night: fog_mult."""
    m = STATES[state]["fog_mult"]
    return {
        "fogColor": hexlin(STATES[state]["fog"]),
        "volumeDensity": round(0.0011 / m, 6),
        "horizonDensity": 5.0,
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
        "groundColor": hexlin(s["sky_ground"]), "haze": 0.8, "sunColor": hexlin(s["fog_sun"]),
        "sunIntensity": 0.0, "sunSize": 0.02, "sunGlow": 0.08, "intensity": 1.0,
    }


def sun_light(state):
    s = STATES[state]
    return {"id": "sun", "name": "sun", "type": "directional", "direction": [0.55, -0.6, 0.58],
            "color": hexlin(s["sun"]), "intensity": s["sun_int"], "castsShadow": True, "softness": 0.25}


# ---- the tile material program ------------------------------------------------------------------------------------
def _c4(rgb, a=1.0):
    return list(rgb) + [a]


def tile_program(state, name="mgTile"):
    """Hypsometric tint from absolute height (worldPosition.y on a flat top), the side shade over the slab's local
    height, top-only emission, and the top-edge band. Terrain colours are baked from `state` until request 1972
    makes them DayNight curves.

    Registers: r0 worldPosition, r1 height (y), r2 colour, r3 scratch, r4 localPosition, r5 top mask, r6 emission."""
    s = STATES[state]
    land = [hexlin(c) for c in s["land"]]
    ops = [
        {"kind": "input", "input": "worldPosition", "dst": 0},
        {"kind": "swizzle", "dst": 1, "srcA": 0, "constant": [1.0, 1.0, 1.0, 1.0]},        # y in every lane
        {"kind": "constant", "dst": 2, "constant": _c4(land[0])},
    ]
    # five-stop ramp as four smoothstepped mixes on absolute height
    for i in range(1, 5):
        ops += [
            {"kind": "smoothstep", "dst": 3, "srcA": 1, "constant": [LAND_STOPS[i - 1], LAND_STOPS[i], 0.0, 0.0]},
            {"kind": "constant", "dst": 7, "constant": _c4(land[i])},
            {"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 7, "srcC": 3},
        ]
    # water below sea level + a little: the water ramp's mid colour (the wave-height ramp needs the crest channel)
    ops += [
        {"kind": "smoothstep", "dst": 3, "srcA": 1, "constant": [SEA_LEVEL + 0.35, SEA_LEVEL + 0.05, 0.0, 0.0]},
        {"kind": "constant", "dst": 7, "constant": _c4(hexlin(s["water"][1]))},
        {"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 7, "srcC": 3},
    ]
    # top mask from the normal (1 on the top face, 0 on the sides), and the side shade over local height:
    # the slab's local y runs -thickness/2..+thickness/2; the side fades to SIDE_FOOT at the underside
    t = TILE["thickness"]
    ops += [
        {"kind": "input", "input": "normal", "dst": 3},
        {"kind": "gradient", "dst": 5, "srcA": 3, "constant": [0.0, 1.0, 0.0, -0.5], "value": 2.0},
        {"kind": "input", "input": "localPosition", "dst": 4},
        {"kind": "swizzle", "dst": 3, "srcA": 4, "constant": [1.0, 1.0, 1.0, 1.0]},
        {"kind": "remap", "dst": 3, "srcA": 3, "constant": [-t / 2, t / 2, SIDE_FOOT, 1.0], "value": 1.0},
        {"kind": "mixBy", "dst": 3, "srcA": 3, "srcB": 5, "srcC": 5},   # top face: 1 (the mask), side: the fade
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3},
    ]
    return {"name": name, "ops": ops, "baseColor": 2, "metallic": -1, "roughness": -1, "emission": -1,
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
        "post/grade/contrast": 1.12 if s["look"] == "punchy" else 1.06,
        # §11 banding: film grain is the only dither the output has today (no output dither exists; asked for in
        # 02-requests.md). Kept just above the 8-bit step so slow sky gradients break up without visible noise.
        "post/output/grain": 0.035,
    }

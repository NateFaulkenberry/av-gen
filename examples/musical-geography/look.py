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
# land2: the height ramp's stops scale with the relief (world.TILES heightScale 1.25 -> 2.2), so the colour bands keep
# their share of the land
RELIEF = 2.2 / 1.25

STATES = {
    "day": dict(
        side_px=0,   # slabSidePixels (ADR-1959): risers smaller than this shade as their tops (r8)
        stops=[v * RELIEF for v in (0.0, 2.0, 4.0, 6.0, 12.0)],   # r7: day only -- gold over whole hill masses, magenta-orchid lowlands
        fill_col="#C060FF", fill_int=1.4,
        fill=0.25, contrast=1.3,
        sky_int=1.0, haze=0.35,
        moon_int=0.0, phase=0.61,   # r5: a low afternoon sun (~25 deg) from the south-west -- gentle slab-limited slopes need grazing light to shade
        sky_zen="#2F6BFF", sky_hor="#C4C4F4", sky_ground="#A890D8", fog="#C4C4F4", fog_sun="#FFF2D6",
        sun="#FFF4E0", sun_int=4.0, amb="#A6B6FF", amb_int=0.3,
        land=["#A848C8", "#DC56AE", "#FF7486", "#FF9442", "#FFC03A"],   # r7: magenta-orchid lowlands, gold hill masses
        water=["#0B36D0", "#0F44E0", "#3A86FF", "#8CC6FF"], shallow="#2EC2D2", under="#4A3C9A",
        path="#FFF5E2", path_emit=0.875, pad="#FF8FB4", pad_emit=0.0,
        drift="#8E9CF2", drift_amt=0.14, edge="#FFF4C8", edge_gain=0.6, crest_gain=0.0,
        fog_mult=4.5, exposure=-0.5, saturation=1.6, look="punchy", bloom=0.25),
    "sunset": dict(
        side_px=26,   # slabSidePixels (ADR-1959): risers smaller than this shade as their tops (r8)
        fill_col="#6A40D0", fill_int=0.6,
        fill=0.55, contrast=1.22,
        sky_int=0.9, haze=0.2,   # meant to steepen the sky gradient; the sky renders flat regardless (02-requests, art p3)
        moon_int=0.0, phase=0.72,
        sky_zen="#5A2496", sky_hor="#FF8A50", sky_ground="#B04868", fog="#FF8A50", fog_sun="#FFB060",
        sun="#FFB848", sun_int=3.4, amb="#5E46B8", amb_int=1.0,
        land=["#4E2A7E", "#8A2C78", "#D0405A", "#FF7448", "#FFA032"],
        water=["#24164F", "#4A2380", "#8A2F8C", "#E2507A"], shallow="#9A3A9A", under="#3A1A4E",
        path="#FFD7A0", path_emit=1.5, pad="#FFB070", pad_emit=0.0,
        drift="#6A3AA0", drift_amt=0.28, edge="#FFC27A", edge_gain=0.45, crest_gain=0.0,   # crest cut at sunset: its peaks read as polka dots
        fog_mult=5.0, exposure=-0.1, saturation=1.2, look="medium-high", bloom=0.25),
    "night": dict(
        water_emit=0.35,   # CP-B r2: the river (and the sea) glow faintly, river colour
        side_px=12,   # slabSidePixels (ADR-1959): risers smaller than this shade as their tops (r8)
        border_fine=0.5,   # r7: under the moon the 1x gaps darken less, so its painted grid must darken more
        fill=0.9, contrast=1.1,
        sky_int=0.9, haze=0.5,
        moon_int=2.2, phase=0.0,
        sky_zen="#080A2C", sky_hor="#33237A", sky_ground="#241A60", fog="#33237A", fog_sun="#41308E",
        sun="#C8B4FF", sun_int=0.0, amb="#5A38B0", amb_int=0.6,
        land=["#221A6C", "#36228E", "#52289E", "#7430B0", "#9C44C8"],
        water=["#0E1452", "#18227A", "#2443B0", "#3F78FF"], shallow="#2A3AA0", under="#120E3A",
        path="#FF5FB0", path_emit=2.0, pad="#FF4FA8", pad_emit=6.0,
        drift="#5A2CB4", drift_amt=0.28, edge="#5A86FF", edge_gain=5.0, crest_gain=0.0,   # CP-B: on 4 m tiles the crest lit whole tiles as white blocks on the sea
        fog_mult=0.85, exposure=0.6, saturation=1.0, look="medium-high", bloom=0.6),
    "dawn": dict(
        sun_az_offset=-1.01,   # radians (ADR-1961)
        stops=[v * RELIEF for v in (0.0, 2.0, 4.0, 6.0, 12.0)],   # r8: the day treatment -- warm bands over whole hill masses
        side_px=0,   # slabSidePixels (ADR-1959): risers smaller than this shade as their tops (r8)
        fill=0.25, contrast=1.3,
        sky_int=0.95, haze=0.35,
        moon_int=0.0, phase=0.27,
        # CP-D draft2 (coordinator: dawn read olive-green and muddy in sequence -- "pale gold and rose, clean, never
        # green"): the green ramp, teal shallows, olive sky floor and green-grey underside are gone; a rose-to-gold
        # land under a peach-rose horizon, lavender water with a rose crest, a cleaner grade (04- §13 CP-D draft2)
        sky_zen="#8FA8E8", sky_hor="#FFD6BC", sky_ground="#D8A8B0", fog="#FFD4C0", fog_sun="#FFE6B8",
        sun="#FFD08A", sun_int=4.4, amb="#E0C8D8", amb_int=0.55,
        land=["#C07A9A", "#E694A6", "#F6B49A", "#FAD08A", "#FFE8B8"],
        water=["#4E5AA0", "#6C78BC", "#A09CD6", "#FFD8C8"], shallow="#F0B8C0", under="#6A4A70",
        path="#F8C060", path_emit=0.625, pad="#FFC8A0", pad_emit=0.0,
        drift="#F8C8B0", drift_amt=0.28, edge="#FFD8A0", edge_gain=0.5, crest_gain=0.0,   # crest cut: its peaks read as polka dots (as sunset)
        fog_mult=8.0, exposure=-0.2, saturation=1.3, look="medium-high", bloom=0.25,
        fog_density=0.00019),   # the DayNight curve's own key (draft2): the default sunrise fog (0.00035) was the haze
}
# Contour gains as approved by the owner (04- §11): on at dusk and night, faint or off by day. The sunset key is 0.6
# (faint); the dusk key between sunset and night brings the lines in at full night strength.
DUSK = dict(phase=0.80, edge_gain=4.0)

# Land ramp height stops, in tiles (absolute height, never renormalised; 04- §3).
LAND_STOPS = [v * RELIEF for v in (0.0, 10.0 / 3, 20.0 / 3, 10.0, 20.0)]   # stops 0-3 evenly spaced (the oklabRamp op); lowered in CP-A r4 so the warm
                                                       # bands cover about half the land, as in the r2 mock-up
SEA_LEVEL = -0.4

# ---- the tile (04- §4) ---------------------------------------------------------------------------------------------
TILE = dict(spacing=1.0, tileSize=0.915, thickness=0.30, bevel=0.0)
TOP_BAND = 0.085          # edge band width, fraction of the top (contour edges, 1970)
SIDE_FOOT = 0.70          # side face fades to 70% of the top colour at the slab's underside (0.18 in the mock-up: in the
                          # engine the slope-facing sides became a dark stipple across every hillside)
ROUGH_LAND, ROUGH_WATER = 0.8, 0.55   # land 0.62 -> 0.8 in r5: a low sun put a white glare over the land   # water 0.16 in the mock-up; 0.28 in the engine (0.16 mirrored a pale sky over the whole sea)


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
        # r6: the engine's default arc put the afternoon sun in the north-west, behind every north-west view (they
        # were back-lit and flat). Turning the arc puts the day sun in the south-west (side light from the camera's
        # left) and the sunset sun in the south.
        "sunAzimuthAtDawn": 3.24,
        # ADR-1961: dawn alone turned -58 deg so its low sun comes from the north-east, side light for the north-west
        # views (on the shared arc it sat straight ahead of them, back-lighting dawn into haze)
        "sunAzimuthOffsetCurve": [{"phase": st["phase"], "value": st.get("sun_az_offset", 0.0)}
                                  for _, st in ordered_states()],
        "zenithColor": col("sky_zen"),
        "horizonColor": col("sky_hor"),
        "groundColor": col("sky_ground"),  # the sky below the horizon (seen past the tiles' end, ~10 deg down at 120 m):
                                           # a darker land-coloured band, not horizon orange (engineering, Phase 5)
        "sunColor": col("sun"),
        "fogColor": col("fog"),
        "sunIntensity": _curve("sun_int"),
        # our own sky brightness and haze per state (the engine's defaults dim the night sky to 0.16, near black)
        "skyIntensity": _curve("sky_int"),
        "haze": _curve("haze"),
        # the sky's light on the land at full strength in every state: the fill is how the shadow side gets its
        # colour (violet at sunset, indigo at night); the engine's default dims it to 0.16-0.46 off-noon
        "hdriIntensity": _curve("fill"),   # r5: separate from the visible sky -- a lower fill gives dark shaded sides
        # CP-D draft2: the fog's ONE density (ADR-705) as our own curve. Until now we set none, so applyDefaults filled
        # it with the Tree of Life defaults, and they overwrote fog_medium's density every frame (fog_mult never
        # applied). Day, sunset and night keep exactly the default they were approved under (fog_density_default);
        # dawn alone gets its own, thinner key.
        "volumeDensity": [{"phase": st["phase"], "value": st.get("fog_density", fog_density_default(st["phase"]))}
                          for _, st in ordered_states()],
    }


# The Tree of Life default fog density curve (day_night.cpp applyDefaults, ADR-705), smoothstep between its keys
_TOL_DENSITY = [(0.0, 0.000225), (0.25, 0.000358), (0.50, 0.000158), (0.75, 0.000341), (0.82, 0.000275),
                (0.90, 0.000233)]


def fog_density_default(phase):
    keys = _TOL_DENSITY + [(1.0, _TOL_DENSITY[0][1])]
    for (p0, v0), (p1, v1) in zip(keys[:-1], keys[1:]):
        if p0 <= phase <= p1:
            t = (phase - p0) / (p1 - p0)
            t = t * t * (3 - 2 * t)
            return round(v0 + (v1 - v0) * t, 7)
    return keys[0][1]


# ---- the moving ground (ADR-1953), authored for restraint: the sea moves, the land breathes, the music adds -------------
CONTOUR_INTERVAL = 9.0   # clouds2: 6 -> 9 m (the steep night hills bunched into white blocks at 148 s); land2: 4 -> 6 m with the taller relief; metres between contour lines (04- §6); 3 -> 4 with the 4 m tiles


# B (ADR-1990): the swell reaches the land. Authored here (the flight's clearance reads the authored waves), keyable per
# state as env/tiles/waveN/onLand. Draft3: 0.08 (the land barely felt the sea's trains).
LAND_SWELL = 0.5


def tile_dynamics(state="day"):  # noqa: E302
    """Two crossing wave trains on the water (the land barely feels them), a slow regional breath, a drift, and rings
    that onsets drop ahead of the camera. Quiet audio leaves a calm sea, not a constant swell (04- §6, brief §10)."""
    return {
        "amplitude": 0.35,                     # response.low adds up to +0.6 (routes)
        "glow": dict(GLOW_REST, landing=landing_glow(state)[0], landingColor=landing_glow(state)[1]),   # Phase 7
        # ADR-1965: rivers ripple downstream (a slow drift, no audio term) and carry a quarter of the sea's swell; the
        # ponds lie nearly still (a tenth), so they read as calm water set into the land, apart from the open sea
        "water": {"riverWaves": 0.4, "pondWaves": 0.3, "driftAmplitude": 0.6, "driftWavelength": 24.0,
                  "driftSpeed": 3.0},   # cpd-r3: more motion so inland water reads as water (was 0.25 / 0.1 / 0.35)
        "waves": [
            {"kind": "planar", "amplitude": 1.0, "wavelength": 64, "speed": 7, "direction": 205, "sharpness": 0.55,
             "onLand": LAND_SWELL, "onWater": 1, "crest": 0.6},
            {"kind": "planar", "amplitude": 0.6, "wavelength": 44, "speed": 5.5, "direction": 265, "sharpness": 0.35,
             "onLand": LAND_SWELL, "onWater": 1, "crest": 0.6},
        ],
        "breathing": {"amplitude": 0.12, "period": 18, "wavelength": 800},
        "rolling": [0.4, 0.0],
        "rings": {"amplitude": 1.6, "speed": 45, "wavelength": 24, "width": 20, "decay": 2.4, "crest": 1},
        "contour": {"interval": CONTOUR_INTERVAL, "offset": 0.0, "fade": CONTOUR_EASE},
    }


# ---- the music (Phase 7, ADR-1963/1964; CP-D draft3) -------------------------------------------------------------
# Engineering's starting mapping (02-requests, Phase 7), tuned by eye and ear on Feline across the whole arc. Every
# route ADDS to a rest value and is unipolar, so silence shows the rest: the land still waves and breathes, the lights
# sit at their rest glow. The glow lanes multiply each state's own emission, so the touch scales with the arc: the day
# (path 0.35-0.875, no contours, no pads) moves lightly, the night strongly.
GLOW_REST = {"path": 0.3, "contour": 0.35, "crest": 1.0, "pads": 0.3}

# The landing glow per state (ADR-1960, keyed through the arc): bright and warm by day so it reads on a lit ground
# (eng: gain 12 reads in daylight), far lower at night where the ground is dark and the contours already glow.
LANDING_GLOW = {"day": (12.0, "#FFD99A"), "sunset": (8.0, "#FFC070"), "night": (2.5, "#FF6FC0"),
                "dawn": (10.0, "#FFD8B0")}


def landing_glow(S):
    """(gain, linear colour) for a state name or an arc blend (which carries `landing_gain`/`landing_color`)."""
    if isinstance(S, str):
        g, c = LANDING_GLOW[S]
        return g, hexlin(c)
    return S["landing_gain"], hexlin(S["landing_color"])


# CP-D r2 smooth (owner: "quite twitchy ... smooth these animations ... not so sure about beat pulsing any of the edge
# glow"; then: "make glowing edges drift in according to the music intensity"). Measured first, per route, as the 99th
# percentile frame-to-frame step over the route's own range, night 140-160 s at 30 fps (1.0 = a full-range jump in one
# frame): the swell 0.99, the contour flash 1.00, the contour climb 0.99 (its per-bar wrap), the rings 108 triggers in
# 20 s, the pads 0.70, the path 0.26, the breathing 0.05. So:
#   * no beat or onset term on any edge glow; the path, contours and pads drift with the SONG'S ENERGY (audio.energy,
#     which carries the sections: 0.31 in the intro, 0.67 in the climax, where response.level is normalised flat),
#     remapped 0.30-0.68 -> 0-1, slewed 1.5 s up / 4.5 s down and eased by a 0.25 Hz critically damped spring;
#   * the swell integrates the lows (0.6 s attack, 2.5 s release, 0.4 Hz spring) instead of jumping on each kick;
#   * a ring at most every 2.5 s (a latching envelope on the onsets) instead of 5 a second;
#   * the contours drift one interval per PHRASE, not per bar.
EDGE_ENERGY = {"remapEnabled": True, "remapInMin": 0.30, "remapInMax": 0.68, "remapOutMin": 0.0, "remapOutMax": 1.0,
               "clampEnabled": True, "clampMin": 0.0, "clampMax": 1.0, "attackMs": 1500.0, "decayMs": 4500.0,
               "springHz": 0.25, "springDamping": 1.0}


def audio_routes():
    def r(source, target, amount, **chain):
        # the processor chain goes under "chain" (params/serialization.cpp): flat keys are ignored without a warning --
        # which is how draft3's attack/decay and the contour flash's peak hold never applied
        out = {"source": source, "target": target, "amount": amount, "op": "add", "polarity": "unipolar"}
        if chain:
            out["chain"] = chain
        return out
    return [
        r("response.low", "env/tiles/amplitude", 1.0, attackMs=600.0, decayMs=2500.0, springHz=0.4, springDamping=1.0),
        r("response.onset", "env/tiles/rings/trigger", 1.0, envelope="peakhold", envelopeHoldMs=0.0,
          envelopeFallPerSecond=20.0, envelopeRefractoryMs=2500.0),
        r("audio.energy", "env/tiles/breathing/amplitude", 0.25, attackMs=400.0, decayMs=2000.0),   # one 0.97 jump
        r("beat.phrase", "env/tiles/contour/offset", CONTOUR_INTERVAL),
        r("audio.energy", "env/tiles/glow/contour", 0.9, **EDGE_ENERGY),
        r("audio.energy", "env/tiles/glow/path", 0.9, **EDGE_ENERGY),
        r("audio.energy", "env/tiles/glow/pads", 0.9, **EDGE_ENERGY),
    ]


# the aliens (entity reactions: a seek replays them, so a scrub hops the way a play does)
ALIEN_REACTIONS = [
    {"signal": "beat.pulse", "target": "hop/trigger", "depth": 1.0},
    # CP-D r2 final: 300/1200 ms (was 80/400: 0.055 of range per frame at p99, over the 0.05 spec)
    {"signal": "response.level", "target": "hop/apex", "depth": 1.6, "chain": {"attackMs": 300.0, "decayMs": 1200.0}},
    {"signal": "response.level", "target": "hop/strength", "depth": 0.8, "chain": {"attackMs": 300.0, "decayMs": 1200.0}},
]

# Feline's stems: the beat clock and the hits from the drums, the levels from the bass (docs/help/audio-input.md)
FELINE_STEMS = [{"key": "drums", "label": "Drums", "role": "drums", "file": "stems/feline-footwear (Drums).wav"},
                {"key": "bass", "label": "Bass", "role": "bass", "file": "stems/feline-footwear (Bass).wav"},
                {"key": "synth", "label": "Synth", "role": "synth", "file": "stems/feline-footwear (Synth).wav"}]
# CP-D r2: the levels come from the MIX, not the bass stem. Canaried with --sonic-trace over the whole song: with
# "levels": "stem.bass", response.high is 0 everywhere (the bass has no highs) and the crown pads never move; from the
# mix it averages 0.43 (p90 0.93), and response.mid 0.57 (was 0.27).
FELINE_FEEDS = {"beat": "stem.drums", "hits": "stem.drums"}


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


def fill_light(state):  # noqa: E302
    """A shadowless violet fill from opposite the sun (r6): the shaded side of orange land under a blue sky turned
    brown -- the brief's "muddy" trap. A painter's coloured fill puts violet in the shadows instead (04- §3)."""
    s = STATES[state] if isinstance(state, str) else state
    return {"id": "fill", "name": "fill", "type": "directional", "direction": [-0.62, -0.3, 0.62],   # travels south-west: from the north-east, opposite the turned day sun
            "color": hexlin(s.get("fill_col", "#000000")), "intensity": s.get("fill_int", 0.0),
            "castsShadow": False, "softness": 1.0}


def sun_light(state):
    s = STATES[state]
    return {"id": "sun", "name": "sun", "type": "directional", "direction": [0.55, -0.6, 0.58],
            "color": hexlin(s["sun"]), "intensity": s["sun_int"], "castsShadow": True, "softness": 0.25}


# ---- the tile material program ------------------------------------------------------------------------------------
def _c4(rgb, a=1.0):
    return list(rgb) + [a]


SHALLOW_AMT = 0.33       # shallows mixed in at most 33% (04- §3)
BORDER_DARK_FINE = 0.72  # r7: the 1x ring paints the same prefiltered lattice as the 2x/4x rings (tileBorder), a
                          # little lighter because its real gap already darkens it -- the two rings' mean brightness
                          # now match (the "lighter rectangle" of r3-r6 was the 1x program with no lattice at all)
# Engineering (CP-D gap column): the one wide, purple gap is the gap line under the camera, the only one you look
# straight down; what you see at its bottom is the underlay's top, 2.8 m below the tiles (world.UNDERLAY), so it took
# the height ramp's lower, bluer colour. Read at the tiles' height it is the walls' hue. (UNDER_RAMP, in metres per S.)
UNDER_RAMP = 0.7
UNDER_SHADE = 0.72        # the underlay takes the land's own colour, darkened: the gaps read as shadow, not as a
                          # second, blue-black surface speckling through every hillside
BORDER_DARK = 0.62       # the 1-px border's darkening (ADR-1976 B: the tile reads at any distance)
CREST_EDGE = (0.72, 0.98)  # crest light only near the peaks: a broad crest lit the crossing trains as a diamond grid
HANDOVER = (200.0, 270.0)   # metres: the 1x ring's look eases into the 2x ring's before the swap (ADR-1995)
GRAZE = (0.03, 0.15)   # the view's y (toward the camera) over which a top's contour band fades in (clouds2)
COARSE_CONTOUR = 0.25
CONTOUR_EASE = 0.15   # ADR-1995: contour edges ease in and out over this share of a step (0.35 dashes the lines)
CONTOUR_FADE_COMP = 1.15   # ADR-1995's contour ease (fade 0.15) takes ~15% of the lines' mean strength: given back
CONTOUR_FADE = (150.0, 500.0, 0.1)   # metres, and the gain left beyond (CP-D draft2: the night glare strip)
LANDING_BAND = 0.075   # the landing outline's band, local metres per S (eng's clip: 0.3 at 4 m tiles)
CONTOUR_BAND = 0.06       # the contour line's width on the top's border, fraction of the half-size
BORDER_BAND = 0.03       # tileBorder's band (engineering's fixture value)
RING_THICKNESS = {1.0: 0.30, 2.0: 0.36, 4.0: 0.72}
JITTER = 0.04            # per-tile value jitter (04- §4 had 0.0225; raised for the far rings, §11; the op budget
                         # of 64 has no room for a distance-graded amount)
PAD_HEIGHT = (17.0, 20.0)              # crowns light only up here (04- §6)
PAD_CLUSTER = 0.2, 0.95                # 5-tile blocks; about 1 in 20 blocks lit


# The water bodies per state (CP-D r2 land): the sea keeps its ramp's first stop; the rivers and ponds their own colour,
# and a night glow (river blue, ponds violet) so each body reads apart from the sea.
WATER_BODIES = {
    "day": dict(river_col="#1FC6F2", pond_col="#3EE4D2", river_emit=0.0, pond_emit=0.0),
    "sunset": dict(river_col="#F08AC4", pond_col="#C8407E", river_emit=0.08, pond_emit=0.15),
    "night": dict(river_col="#3A6CFF", pond_col="#B060FF", river_emit=0.6, pond_emit=1.1),
    "dawn": dict(river_col="#9CB8FF", pond_col="#FFC4D4", river_emit=0.0, pond_emit=0.0),
}


def water_bodies(s):
    """Linear colours and glows for (sea, river, pond) from a state dict (an arc blend carries the WATER_BODIES keys)."""
    w = s
    sea_emit = w.get("water_emit") or 0.0
    gains = [sea_emit, w["river_emit"], w["pond_emit"]]
    cols = [hexlin(w["water"][0]), hexlin(w["river_col"]), hexlin(w["pond_col"])]
    glow_src = [hexlin(w["water"][3]), hexlin(w["river_col"]), hexlin(w["pond_col"])]
    return {"col": cols, "emit": [[v * g for v in c] for c, g in zip(glow_src, gains)], "emit_gain": gains}


def tile_program(state, cell=1.0, name=None, far=None, shade=1.0, border=None, scale=1.0, contour=True, always=False,
                 ramp_offset=0.0):
    """The slab's whole look, for one palette state and one ring.

    Reads: localPosition through engineering's tileBorder (ADR-1976: the border's coverage, prefiltered per axis below
    4 px a tile, so no moire; the top-face mask), worldPosition.y (the tile's top on a flat face), instanceRandom =
    (water, shore, flat, path) and instanceColor.a = the lattice hash (ADR-1950/1978). Writes base colour, roughness
    and top-only emission. The 2x and 4x rings close their geometric gaps (box = cell) and paint the fine lattice
    with tileBorder's subdivisions; `far` (default: cell > 1) drops the pads and the jitter there, for cost.
    Terrain colours are baked from `state` until request 1972 makes them DayNight curves; contour edges (1970) and
    crest light (1971) are added when their channels exist (Phase 3).

    Registers: r0 localPosition, r1 tileBorder (coverage, px, top, tile px), r2 colour, r3/r7 scratch,
    r4 worldPosition, r5 random, r6 roughness, then r4 emission.

    `state` is a state's name or a state dict (arc.py's blends). `always` (CP-D, the full piece) keeps every optional
    op (the night water glow, the contour band) even at gain 0, so one program serves every state and the timeline
    keys its constants through the arc."""
    s = dict(STATES[state], **WATER_BODIES[state]) if isinstance(state, str) else state
    far = cell > 1.0 if far is None else far
    name = name or "mgTile%dx_%s" % (int(cell), state)
    land = [hexlin(c) for c in s["land"]]
    LS = s.get("stops", LAND_STOPS)
    gap = TILE["tileSize"] if cell == 1.0 else 1.0
    half = cell * scale * gap / 2.0
    thick = RING_THICKNESS.get(cell, 0.3) * scale
    sub = int(cell)
    B = lambda k: [float(k)] * 4   # noqa: E731
    o = []
    add = o.append

    def near_far(reg, near, far_v):   # ADR-1995 handover: near -> far_v over HANDOVER (the 1x ring meets the 2x)
        add({"kind": "input", "input": "cameraDistance", "dst": reg})
        add({"kind": "remap", "dst": reg, "srcA": reg, "constant": [HANDOVER[0], HANDOVER[1], near, far_v],
             "value": 1.0})

    def glow(reg, lane, tmp):   # Phase 7 (ADR-1963): an emission lane times its live gain (tileGlow x path, y contour,
        add({"kind": "input", "input": "tileGlow", "dst": tmp})          # z crest, w pads; 1 = as authored)
        add({"kind": "swizzle", "dst": tmp, "srcA": tmp, "constant": B(lane)})
        add({"kind": "multiply", "dst": reg, "srcA": reg, "srcB": tmp})

    add({"kind": "input", "input": "localPosition", "dst": 0})
    add({"kind": "tileBorder", "dst": 1, "srcA": 0, "constant": [half, half, BORDER_BAND, thick / 2],
         "constant2": [sub, 0, 0, 0], "value": 1.0})
    add({"kind": "input", "input": "worldPosition", "dst": 4})
    add({"kind": "swizzle", "dst": 3, "srcA": 4, "constant": B(1)})                       # y
    if ramp_offset:   # the underlay: read the ramp at the tiles' tops, not at its own (lower) top -- see UNDER_RAMP
        add({"kind": "constant", "dst": 7, "constant": B(ramp_offset)})
        add({"kind": "add", "dst": 3, "srcA": 3, "srcB": 7})
    # the height ramp, blended in OKLab: stops 0-3 over 0..LS[3] (evenly spaced, ADR-1470), then the top stop
    add({"kind": "remap", "dst": 7, "srcA": 3, "constant": [0, LS[3], 0, 1], "value": 1.0})
    add({"kind": "oklabRamp", "dst": 2, "srcA": 7, "value": 1.0, "constant": _c4(land[0]),
         "constant2": _c4(land[1]), "constant3": _c4(land[2]), "constant4": _c4(land[3])})
    add({"kind": "smoothstep", "dst": 7, "srcA": 3, "constant": [LS[3], LS[4], 0, 0]})
    add({"kind": "constant", "dst": 6, "constant": _c4(land[4])})
    add({"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 6, "srcC": 7})
    if not far:   # pads: crown x high x a 5-tile block picked by its hash (before r3/r4 are reused)
        add({"kind": "smoothstep", "dst": 7, "srcA": 3, "constant": [PAD_HEIGHT[0], PAD_HEIGHT[1], 0, 0]})
        add({"kind": "quantize", "dst": 6, "srcA": 4, "value": PAD_CLUSTER[0] / scale})
        add({"kind": "swizzle", "dst": 6, "srcA": 6, "constant": B(3)})
        add({"kind": "threshold", "dst": 6, "srcA": 6, "value": PAD_CLUSTER[1]})
        add({"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 6})                         # r7 = pad site
    add({"kind": "input", "input": "instanceRandom", "dst": 5})                            # water shore flat path
    if not far:
        add({"kind": "swizzle", "dst": 6, "srcA": 5, "constant": B(2)})
        add({"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 6})                         # x crown
        add({"kind": "constant", "dst": 4, "constant": _c4([v * s["pad_emit"] for v in hexlin(s["pad"])])})
        add({"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 7})                         # r4 = emission
        if cell == 1.0:   # ADR-1995 handover: the coarse rings have no pads, so the 1x ring's fade out before the swap
            near_far(6, 1.0, 0.0)
            add({"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 6})
        glow(4, 3, 6)                                                                     # x the pads' glow
    else:
        add({"kind": "constant", "dst": 4, "constant": [0, 0, 0, 1]})
    add({"kind": "swizzle", "dst": 3, "srcA": 5, "constant": B(0)})                        # water
    # CP-D r2 land (ADR-1965): each body of water its own colour and night glow -- sea, river, pond -- read from the
    # waterBody class (w = 0, 1, 2) through a ramp whose stops are the three colours (the class lands exactly on one)
    wb = water_bodies(s)
    add({"kind": "input", "input": "waterBody", "dst": 6})
    add({"kind": "swizzle", "dst": 6, "srcA": 6, "constant": B(3)})
    add({"kind": "remap", "dst": 6, "srcA": 6, "constant": [0, 3, 0, 1], "value": 1.0})    # class / 3
    add({"kind": "oklabRamp", "dst": 7, "srcA": 6, "value": 1.0, "constant": _c4(wb["col"][0]),
         "constant2": _c4(wb["col"][1]), "constant3": _c4(wb["col"][2]), "constant4": _c4(wb["col"][2])})
    add({"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 7, "srcC": 3})
    if always or max(wb["emit_gain"]) > 0:   # the night glow per body (CP-B r2's sea glow, now the river's and ponds' own)
        add({"kind": "oklabRamp", "dst": 7, "srcA": 6, "value": 1.0, "constant": _c4(wb["emit"][0]),
             "constant2": _c4(wb["emit"][1]), "constant3": _c4(wb["emit"][2]), "constant4": _c4(wb["emit"][2])})
        add({"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 3})
        add({"kind": "add", "dst": 4, "srcA": 4, "srcB": 7})
    add({"kind": "remap", "dst": 6, "srcA": 3, "constant": [0, 1, ROUGH_LAND, ROUGH_WATER], "value": 1.0})
    add({"kind": "swizzle", "dst": 3, "srcA": 5, "constant": B(1)})                        # shore -> shallows
    add({"kind": "constant", "dst": 7, "constant": _c4(hexlin(s["shallow"]))})
    add({"kind": "mix", "dst": 7, "srcA": 2, "srcB": 7, "value": SHALLOW_AMT})
    add({"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 7, "srcC": 3})
    add({"kind": "swizzle", "dst": 3, "srcA": 5, "constant": B(3)})                        # path
    add({"kind": "constant", "dst": 7, "constant": _c4(hexlin(s["path"]))})
    add({"kind": "mixBy", "dst": 2, "srcA": 2, "srcB": 7, "srcC": 3})
    add({"kind": "constant", "dst": 7, "constant": _c4([v * s["path_emit"] for v in hexlin(s["path"])])})
    add({"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 7})
    glow(3, 0, 7)                                                                         # x the path's glow
    add({"kind": "add", "dst": 4, "srcA": 4, "srcB": 3})
    if not far:   # per-tile value jitter on the lattice hash (request 1978)
        add({"kind": "input", "input": "instanceColor", "dst": 3})
        add({"kind": "swizzle", "dst": 3, "srcA": 3, "constant": B(3)})
        if cell == 1.0:   # the jitter eases out over the handover (the 2x ring draws none)
            add({"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, -JITTER, JITTER], "value": 1.0})
            near_far(7, 1.0, 0.0)
            add({"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 7})
            add({"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, 1, 2], "value": 0.0})   # + 1, unclamped
        else:
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
    bd = border if border is not None else (s.get("border_fine", BORDER_DARK_FINE) if cell == 1.0 else BORDER_DARK)
    if cell == 1.0 and not far:   # the border's darkness meets the coarse rings' BORDER_DARK over the handover
        near_far(7, 1.0 - bd, 1.0 - BORDER_DARK)
        add({"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 7})
        add({"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, shade, 0.0], "value": 0.0})   # shade (1 - d)
    else:
        add({"kind": "remap", "dst": 3, "srcA": 3, "constant": [0, 1, shade, shade * bd], "value": 1.0})
    add({"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 3})
    # Phase 3: contour lines on the flagged sides (ADR-1970) and the crest light (ADR-1971), from tileContour
    eg, cg = s.get("edge_gain", 0.0), s.get("crest_gain", 0.0)
    if contour and (always or eg > 0 or cg > 0):
        add({"kind": "input", "input": "instanceEmissive", "dst": 1})
        add({"kind": "tileContour", "dst": 3, "srcA": 0, "srcB": 1, "constant": [half, half, CONTOUR_BAND,
                                                                                  LANDING_BAND * scale],
             "value": 1.0})
        add({"kind": "swizzle", "dst": 1, "srcA": 3, "constant": B(0)})                     # contour coverage
        add({"kind": "input", "input": "instanceRandom", "dst": 7})                          # land only: the sea's
        add({"kind": "remap", "dst": 7, "srcA": 7, "constant": [0, 1, 1, 0], "value": 1.0})   # waves crossing 0 m
        add({"kind": "swizzle", "dst": 7, "srcA": 7, "constant": B(0)})                     # drew closed blobs
        add({"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 7})
        # clouds2: the coarse rings (8 m and 16 m cells, past ~270 m) paint their sub-tiles' contours; on the land2
        # walls every sub-tile crosses an interval, so a far steep flank lit solid (148 s; measured: gone at edge gain 0)
        egr = eg * CONTOUR_FADE_COMP * (1.0 if cell == 1.0 else COARSE_CONTOUR)
        add({"kind": "constant", "dst": 7, "constant": _c4([v * egr for v in hexlin(s["edge"])])})
        add({"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 7})
        # CP-D draft2: fade the lines with distance -- on a far hill (the 4x ring, 4 m tiles a few pixels wide) the
        # bands covered whole tiles and bunched into one glare strip (night, 148-164 s)
        add({"kind": "input", "input": "cameraDistance", "dst": 7})
        add({"kind": "remap", "dst": 7, "srcA": 7, "constant": [CONTOUR_FADE[0], CONTOUR_FADE[1], 1.0,
                                                                 CONTOUR_FADE[2]], "value": 1.0})
        add({"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 7})
        if cell == 1.0:   # the coarse rings carry COARSE_CONTOUR of the gain: meet it over the handover
            near_far(7, 1.0, COARSE_CONTOUR)
            add({"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 7})
        # clouds2: fade the band where a top is seen edge-on. On land2's steep walls the camera sees the tops at a
        # grazing angle from just below their level, where the border band covers the whole visible sliver of each top
        # and a wall lit solid (148-150 s; measured: gone at edge gain 0, unchanged by the coarse-ring and distance cuts)
        add({"kind": "input", "input": "viewDirection", "dst": 7})
        add({"kind": "swizzle", "dst": 7, "srcA": 7, "constant": B(1)})
        add({"kind": "smoothstep", "dst": 7, "srcA": 7, "constant": [GRAZE[0], GRAZE[1], 0, 0]})
        add({"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 7})
        glow(1, 1, 7)                                                                     # x the contours' glow
        add({"kind": "add", "dst": 4, "srcA": 4, "srcB": 1})
        # CP-D r2 (owner: "an edge glow on the tiles affected when a character lands"; ADR-1960): the landed tiles'
        # outline (tileContour z) x landingGlow (the arc-keyed colour x gain: env/tiles/glow/landing, landingColor)
        add({"kind": "swizzle", "dst": 1, "srcA": 3, "constant": B(2)})
        add({"kind": "input", "input": "landingGlow", "dst": 7})
        add({"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 7})
        add({"kind": "add", "dst": 4, "srcA": 4, "srcB": 1})
        if cg > 0:
            add({"kind": "swizzle", "dst": 1, "srcA": 3, "constant": B(1)})                 # crest
            add({"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [CREST_EDGE[0], CREST_EDGE[1], 0, 0]})
            add({"kind": "constant", "dst": 7, "constant": _c4([v * cg for v in hexlin(s["water"][3])])})
            add({"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 7})
            glow(1, 2, 7)                                                                 # x the crest's glow
            add({"kind": "add", "dst": 4, "srcA": 4, "srcB": 1})
    add({"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5})                              # emission: top only
    return {"name": name, "ops": o, "baseColor": 2, "metallic": -1, "roughness": 6, "emission": 4,
            "emissionIntensity": 1.0, "opacity": -1}


# ---- post (04- §3 grade rows, §11 banding) ------------------------------------------------------------------------
# AgX (operator 1, the default) in every state; the day's "Punchy" is a grade (contrast, saturation) on top of it.


def post(state):
    s = STATES[state] if isinstance(state, str) else state
    return {
        "post/bloom/enabled": True,
        "post/bloom/intensity": s["bloom"],
        "post/bloom/threshold": 1.0,
        "camera/exposure/compensation": s["exposure"],
        "camera/exposure/mode": 0,   # r6: manual -- automatic metering pulled every frame back to a flat middle grey
        "post/grade/saturation": s["saturation"],
        "post/grade/contrast": s["contrast"],
        # §11 banding: film grain is the only dither the output has today (no output dither exists; asked for in
        # 02-requests.md). Kept just above the 8-bit step so slow sky gradients break up without visible noise.
        "post/output/grain": 0.035,
    }


# ---- the sky's clouds (CP-D r2, owner: "the sky reads too plain, add cinematic clouds") ------------------------------
# Clouds in the tile world's own language: each layer is a second "tiles" ground (ADR-1950) lifted to altitude, on a
# lattice of square slabs. Its own continent field is the cover (the "sea" cells are cut away: the program writes
# opacity = 1 - water, alphaMode mask) and its own hills are the billows, so a cloud's top and underside step like the
# land below. Lit by the scene's sun, moon, fill and sky (they carry the arc for free) and veiled by the same fog, so the
# layers recede. Live: GPU-generated per frame and culled like the ground; no volume march (ADR-1289's lobes cost
# 18-26 ms, ADR-1864).
# (name, cell m, slab thickness m, altitude m, viewDistance m, the cover's TileTerrain overrides)
CLOUD_LAYERS = [
    ("mgCloudLow", 16.0, 3.0, 260.0, 1800.0,      # low scud: the scale cue well above the 40 m ceiling (sparse)
     dict(seed=21, continentWavelength=260.0, landThreshold=0.70, inlandBand=0.1, hillHeight=10.0,
          hillWavelength=90.0)),
    ("mgCloudMid", 48.0, 24.0, 650.0, 5000.0,     # the cumulus masses: stepped towers, clear sky between
     dict(seed=33, continentWavelength=1100.0, landThreshold=0.63, inlandBand=0.12, hillHeight=110.0,
          hillWavelength=300.0)),
]
CLOUD_BASE = dict(shoreBand=0.02, seaLevel=0.0, seaDepth=0.0, shoreRise=1.0, hillOctaves=2, hillWarp=40.0,
                  warpWavelength=300.0, hillLow=0.2, hillHigh=0.85, crownThreshold=1.0, crownFlatten=0.0,
                  regionWavelength=3000.0, regionVariation=0.2, path=[], rivers=[], heightScale=1.0)
CLOUD_COLOR = "#FFFFFF"
CLOUD_SHADE = 0.45   # an underside, of a top (a cheap self-shadow: the sun never reaches a slab's underside)


# The clouds per state (CP-D r2 v2, owner: at night "they look too much like a mirror reflection of the ground ... too
# dark on the bottom"). Keyed through the arc like the terrain (arc.full_state). By night:
#   under  0.45 -> 0.85  the undersides lifted (moon and sky fill), so the deck is not a dark flipped ground;
#   side   1.0 -> 0.55   the side faces held down: the moon caught them at grazing angles as stray bright streaks;
#   core   0 -> 0.55     only cells whose billow rises above (1.5 core - 0.5) of of the layer's relief stay: a sparser,
#                        broken night sky of separate masses with clear sky between, a rhythm the ground does not have;
#   bounce               a faint cool self-light (the ambient bounce), lifting every face a little.
CLOUD_STATE = {
    "day": dict(cloud_color="#FFFFFF", cloud_under=0.75, cloud_side=1.0, cloud_core=0.0, cloud_bounce="#000000",
                cloud_bounce_gain=0.0, cloud_lining=0.08, cloud_lining_tint="#FFF2D8", cloud_sky_mix=0.75, cloud_opacity=0.55),
    "sunset": dict(cloud_color="#FFFFFF", cloud_under=0.7, cloud_side=1.0, cloud_core=0.0, cloud_bounce="#000000",
                   cloud_bounce_gain=0.0, cloud_lining=0.14, cloud_lining_tint="#FFD0A0", cloud_sky_mix=0.55, cloud_opacity=0.65),
    "night": dict(cloud_color="#E4E8FF", cloud_under=0.85, cloud_side=0.55, cloud_core=0.55, cloud_bounce="#6A74D8",
                  cloud_bounce_gain=0.10, cloud_lining=0.5, cloud_lining_tint="#D8E0FF", cloud_sky_mix=0.0, cloud_opacity=1.0),
    "dawn": dict(cloud_color="#FFFFFF", cloud_under=0.7, cloud_side=1.0, cloud_core=0.0, cloud_bounce="#000000",
                 cloud_bounce_gain=0.0, cloud_lining=0.1, cloud_lining_tint="#FFE0C0", cloud_sky_mix=0.65, cloud_opacity=0.6),
}


def cloud_omit_below(S, thick, relief):
    """ADR-1966's per-cell "cores only" cut (keyed through the arc): eng's conversion of the v2 opacity cut,
    (1.5 core - 0.5) relief + 3 + thick/2, less 2 m so the day (core 0) keeps every cell. Night: 7.25 m / 48.75 m."""
    return (1.5 * S["cloud_core"] - 0.5) * relief + 3.0 + thick / 2.0 - 2.0


def cloud_program(name, thick, alt, relief, state="day"):
    """The cloud slab (opaque since ADR-1966: the empty cells are never drawn). Tops lit, undersides and sides held
    (the approved v2 night: undersides 0.85, sides 0.55), a cool bounce, and (ADR-1967) a silver lining: the faces and
    edges toward the key light (the sun, or the moon at night) catch its colour, n.L built from multiply/swizzle/add."""
    S = dict(CLOUD_STATE[state]) if isinstance(state, str) else state
    B = lambda k: [float(k)] * 4   # noqa: E731
    return {"name": name, "ops": [
        {"kind": "constant", "dst": 2, "constant": _c4(hexlin(S["cloud_color"]))},
        {"kind": "input", "input": "normal", "dst": 0},
        {"kind": "swizzle", "dst": 1, "srcA": 0, "constant": B(1)},
        {"kind": "remap", "dst": 7, "srcA": 1, "constant": [-1.0, 1.0, S["cloud_under"], 1.0], "value": 1.0},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 7},                                   # tops lit, undersides
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 1},                                   # n.y^2: 0 on a side
        {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.0, 1.0, S["cloud_side"], 1.0], "value": 1.0},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 1},                                   # the sides held down
        {"kind": "constant", "dst": 4, "constant": _c4([v * S["cloud_bounce_gain"] for v in hexlin(S["cloud_bounce"])])},
        # clouds3 (owner: near overhead clouds' dark flat undersides "read a bit more like a platform"): the more a
        # cloud is overhead, the more its surface gives way to the sky's own colour (skyAmbient, as emission) and its
        # albedo thins -- a seen-through underside, without a blend pass; the horizon's clouds are untouched
        {"kind": "input", "input": "viewDirection", "dst": 1},
        {"kind": "swizzle", "dst": 1, "srcA": 1, "constant": B(1)},
        {"kind": "remap", "dst": 1, "srcA": 1, "constant": [-OVERHEAD[0], -OVERHEAD[1], 0.0, S["cloud_sky_mix"]],
         "value": 1.0},
        {"kind": "input", "input": "skyAmbient", "dst": 5},
        {"kind": "multiply", "dst": 5, "srcA": 5, "srcB": 1},
        {"kind": "add", "dst": 4, "srcA": 4, "srcB": 5},
        {"kind": "remap", "dst": 1, "srcA": 1, "constant": [0.0, 1.0, 1.0, 0.0], "value": 1.0},
        {"kind": "multiply", "dst": 2, "srcA": 2, "srcB": 1},
        # the silver lining: n.L (no dot op), eased, x the key light's colour x a keyed gain, into the emission
        {"kind": "input", "input": "keyLightDirection", "dst": 3},
        {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 0},
        # horizontal n.L only (x and z): from under the deck the tops are never seen, so the lining is the EDGES and
        # sides that face the key light, the rim a viewer below actually sees
        {"kind": "swizzle", "dst": 1, "srcA": 3, "constant": B(0)},
        {"kind": "swizzle", "dst": 7, "srcA": 3, "constant": B(2)},
        {"kind": "add", "dst": 1, "srcA": 1, "srcB": 7},
        {"kind": "smoothstep", "dst": 1, "srcA": 1, "constant": [LINING_EDGE[0], LINING_EDGE[1], 0, 0]},
        {"kind": "input", "input": "keyLightColor", "dst": 7},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 7},
        {"kind": "constant", "dst": 7, "constant": _c4([S["cloud_lining"] * c for c in hexlin(S["cloud_lining_tint"])])},
        {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 7},
        {"kind": "add", "dst": 4, "srcA": 4, "srcB": 1},
        {"kind": "constant", "dst": 6, "constant": B(1.0)},
        # ADR-1996: per-pixel opacity -- the overhead clouds thin to cloud_opacity, the horizon's stay solid
        {"kind": "input", "input": "viewDirection", "dst": 5},
        {"kind": "swizzle", "dst": 5, "srcA": 5, "constant": B(1)},
        {"kind": "remap", "dst": 5, "srcA": 5, "constant": [-0.6, -0.2, S["cloud_opacity"], 1.0], "value": 1.0},
    ], "baseColor": 2, "metallic": -1, "roughness": 6, "emission": 4, "emissionIntensity": 1.0, "opacity": 5}


OVERHEAD = (0.25, 0.85)   # -viewDirection.y over which a cloud counts as overhead (clouds3)
LINING_EDGE = (0.05, 0.6)   # horizontal n.L where the lining starts and is full


def cloud_gate(S):
    """ADR-1996: the material's opacity is the gate (1 = the opaque path); the program sets the per-pixel share.
    Solid at night (the approved v2), else 0.99 so the program's overhead thinning applies."""
    return 1.0 if S["cloud_opacity"] >= 0.999 else 0.99


def cloud_nodes(state="day"):
    nodes, programs = [], []
    S_ = dict(CLOUD_STATE[state]) if isinstance(state, str) else state
    for name, cell, thick, alt, view, cover in CLOUD_LAYERS:
        tiles = dict(CLOUD_BASE, **cover)
        programs.append(cloud_program(name + "Prog", thick, alt, cover["hillHeight"], state))
        nodes.append({"name": name, "kind": "procedural", "position": [0, 0, 0], "procedural": {
            "source": {"kind": "box", "size": [cell, thick, cell]},
            "distribution": {"kind": "generator", "generator": {
                "name": "tiles", "version": 1, "cellSize": cell, "viewDistance": view, "align": 1, "tiles": tiles,
                # ADR-1966: the empty ("water") cells are never emitted, the night's cores-only cut is per cell
                # (keyed through the arc), and the deck stands still (no waves, rolling or contours from the ground)
                "omit": "water", "omitBelow": round(cloud_omit_below(S_, thick, cover["hillHeight"]), 3),
                "dynamics": False, "contours": False}},
            "distributionTransform": {"position": [0, alt, 0]},
            "castsShadow": False, "lod": {"cull": True},
            # clouds3 (owner: "maybe we could try making them translucent"): the slabs draw in the refraction phase
            # (ADR-1603) with no offset, so a share of what lies behind shows through -- keyed through the arc
            # (cloud_opacity: day 0.55 .. night 1.0, the approved v2 night stays solid)
            "material": {"baseColor": [1, 1, 1], "roughness": 1.0, "metallic": 0.0, "program": name + "Prog",
                         "refraction": {"strength": 0.0005, "opacity": cloud_gate(S_),
                                        "thickness": 0.0}}}})
    return nodes, programs

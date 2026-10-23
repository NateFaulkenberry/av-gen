"""10. DIGITAL OCEAN: THE TIDEWRITER (docs/prototypes/digital-ocean/00-brief.md; plan 01-plan.md, log PROGRESS.md,
report 02-report.md).

A stylized ocean at electric twilight, seen from a composed, still camera 12 m over the water: Gerstner swells (sine
deformers in pairs) roll in from under a low sun across a camera-centred sea mesh, painted by unlit material programs
(the reflected sky, the sun's path and glints, light through backlit crests, foam caps). The music is WRITTEN INTO THE
SEA: a spectrum field lays the bass's last fifteen seconds out as swell rolling in (ADR-1370 smooths it), kicks send
lifted fronts in from 70 m out. Dolphins leap across the sun's path (path deformers on splines: ambient pods on LFO
saws, a pad-fired hero on an envelope). MIDI: eight CCs and five pads (MIDI_CC, MIDI_PADS).
"""
import math
import os

from .. import kit
from .. import meshes as mx
from ..kit import R, M, hexrgb

ID = "digital-ocean"
TITLE = "Digital Ocean"

MESH_DIR = os.path.join(kit.ABSTRACT_DIR, "meshes")

DESIGN = {
    "category": "digital seascape",
    "thesis": "The Tidewriter, illustrated (Pass 4): an anime ocean designed from the ground up -- a tiny island "
              "with one windswept palm and a few rocks, graphic water in flat colour regions with crest strokes and "
              "sparse foam, the sun's reflection a path of broken marks from the horizon to your feet, illustrated "
              "cloud formations, a sport motor yacht cruising past, dolphins leaping across the path. Colour is the "
              "instrument: the music moves the whole world between violet sunset, golden afternoon and electric "
              "blue, while the bass writes its history into the swell.",
    "composition": {
        "background": "a violet-to-peach sky, broad tapered sun rays, illustrated cloud formations (flat undersides, "
                      "a few lobes, lit coral toward the sun, mauve in shadow) in two drifting layers",
        "midground": "a tiny island on the left third: a low dune, one windswept palm reaching toward the sun, a "
                     "boulder group at its left end, a thin surf line; the yacht crossing in front",
        "foreground": "graphic water: indigo troughs, a cobalt body and lavender wave backs cut from the swell's "
                      "height and faces, pale turquoise crest strokes and pink-white foam as tapered dashes, the "
                      "sun's path a column of broken warm marks that widens toward the viewer",
        "focal": "the low sun on the right third and its reflection path down to the foreground",
        "secondary": ["the motor yacht and its wake", "dolphin leaps across the path", "the island's silhouette"],
        "atmosphere": "distance haze on land and clouds; the far sea converges on its middle tone",
        "post": "a little bloom on the sun and the path",
        "camera": "still: 10 m over the water, 36 mm, the horizon at 47 % from the top (46.5-47.5 % through the breath)",
    },
    "palette": {"dominant": "#16206a", "secondary": "#7b45a0", "accent": "#f062c8", "highlight": "#fff0d8",
                "background_value": "mid (a coral horizon under a violet sky)",
                "saturation": "three coordinated palettes on one axis (violet sunset, golden afternoon, electric "
                              "blue) plus a bioluminescent night end"},
    "motion": {
        "very_slow": ["the cloud banks turning round the viewer (two layers, parallax)", "the palette following the piece"],
        "medium": ["three Gerstner trains (their pace and dominant direction on controls)", "the bass's history "
                   "rolling in", "the yacht's crossing", "ray drift"],
        "fast": ["kick fronts rolling in", "dolphin leaps", "ray flashes on kicks"],
        "extremely_fast": ["the path's marks flaring on hats", "spray"],
    },
    "vocabulary": [
        ["intensity (12 s)", "response.intensity -> visual.mood", "PALETTE: a build moves the world toward golden "
         "afternoon and electric blue; a breakdown sinks it back to the violet sunset; the water's bands tighten"],
        ["bass", "tide (spectrum field) + response.bass", "TIDEWRITING: the bass's last ~15 s roll in as swell; the "
         "water's body deepens and the crest light lifts"],
        ["kick", "kickFront (onset field) + signal.kickEnv", "a lifted front rolls in; crest light, foam and the "
         "sun rays flash"],
        ["level", "response.level", "the swell grows and quickens (an integrated phase: no crest jumps), the rays "
         "strengthen (slowly)"],
        ["onsets", "response.onsetEnv", "the rays reach further for a moment"],
        ["mids", "audio.mid", "the wind sea roughens"],
        ["snare", "signal.snare", "spray off the near swell"],
        ["hats / highs", "signal.hatEnv, audio.treble", "the sun path's marks and the crest strokes flare; mint enters the lit faces"],
        ["sustain", "response.sustain", "the sky's middle turns pink, the clouds' lit undersides warm, the halo "
         "swells"],
        ["brightness", "sonic.brightness.slow", "a slight warm or cool drift of the whole grade"],
        ["MIDI notes", "notes.noteOn, lastPitch, lastVelocity", "sparks on the sun's path, by pitch and velocity"],
        ["CC 1", "control.palette", "PALETTE: violet sunset -> golden afternoon -> electric blue"],
        ["CC 2", "control.night", "NIGHT: a bioluminescent night"],
        ["CC 3", "control.waves", "WAVES: the swell's height"],
        ["CC 4", "control.energy", "WAVE ENERGY: chop, foam and the tide's depth"],
        ["CC 5", "control.rays", "SUN RAYS: strength, reach, halo and the sun's path on the water"],
        ["CC 6", "control.clouds", "CLOUDS: the glow on the clouds"],
        ["CC 7", "control.water", "WATER COLOUR: the richness of the water"],
        ["CC 8", "control.spray", "SPRAY: drums' spray and notes' sparks"],
        ["CC 9", "control.speed", "WAVE SPEED: the authored pace to 2.5x, without a crest jumping"],
        ["CC 10", "control.direction", "WAVE DIRECTION: the cross swell gives way to the wind sea"],
        ["CC 11", "control.bands", "BAND CONTRAST: soft painted bands to crisp cel shading"],
        ["CC 12", "control.highlights", "HIGHLIGHTS: the crest strokes brighten and widen"],
        ["CC 13", "control.foam", "FOAM: more and brighter foam caps"],
        ["CC 14", "control.detail", "SURFACE DETAIL: streaks across the bands"],
        ["pad 36", "control.padLeap", "LEAP: a dolphin arcs across the sun's path"],
        ["pad 37", "control.padWave", "HERO WAVE: a big swell rises far out and rolls in, foaming"],
        ["pad 38", "control.padGlow", "BLOOM: bioluminescence along every crest"],
        ["pad 39", "control.padFlare", "FLARE: the sun and its rays blaze and settle"],
        ["pad 40", "control.padSpray", "SPRAY: a gust of spray"],
        ["pad 41", "control.padDay", "DAYBREAK: the world sweeps to electric day and back to the sunset"],
        ["silence", "(no input)", "the swell rolls, clouds and rays drift, the yacht crosses every ~75 s, pods "
         "leap every 53 and 79 s"],
    ],
    "tier": "light: a graphic sea (three meshes, eight deformers each, a 48-op program near and 38 far), seven "
            "unlit programs, a tiny island with one palm and eight Kenney rocks, a yacht and its masked wake, 16 "
            "cloud formations (one lofted pillow each), fixed particle pools; no lights, no shadows, no fog march",
}

INDEX_ENTRY = {
    "category": "Sonic Abstract",
    "description": "Play live audio (a mix with a clear low end) plus a MIDI controller: CC 1 palette (violet sunset "
                   "-> golden afternoon -> electric blue), CC 2 night, CC 3 waves, CC 4 wave energy, CC 5 sun rays, "
                   "CC 6 clouds, CC 7 water colour, CC 8 spray, CC 9 wave speed, CC 10 wave direction, CC 11 band "
                   "contrast, CC 12 highlights, CC 13 foam, CC 14 surface detail; pads 36 dolphin leap, 37 hero wave, 38 "
                   "bioluminescent bloom, 39 sun flare, 40 spray, 41 daybreak. An illustrated ocean world whose colour "
                   "and swell follow the music. See docs/prototypes/digital-ocean/11-report-art-direction-reset.md.",
    "live": True,
    "name": "Sonic Abstract - Digital Ocean",
    "project": "sonic-abstract/digital-ocean.json",
}


# ------------------------------------------------------------------------------------------------ look-development knobs
# Module-level so a look-development script can set them before build() (tools stay pure functions of these).
COMPOSITION = "F1"


def unit(v):
    n = math.sqrt(sum(x * x for x in v))
    return tuple(x / n for x in v)


def sun_dir(elev_deg, az_deg):
    """Towards the sun: elevation above the horizon, azimuth clockwise from -Z (the view) seen from above."""
    e, a = math.radians(elev_deg), math.radians(az_deg)
    return unit((math.sin(a) * math.cos(e), math.sin(e), -math.cos(a) * math.cos(e)))


SUN_ELEV, SUN_AZ = 5.5, 13.0
S = sun_dir(SUN_ELEV, SUN_AZ)

# The compositions of Stage 1: (eye height, focal length, pitch in degrees, yaw in degrees)
COMPOSITIONS = {
    "A": (3.2, 30.0, -1.5, 4.0),     # low and cinematic: in the swell
    "B": (32.0, 26.0, -13.0, 0.0),   # elevated: the pattern of the waves
    "C": (9.0, 55.0, -0.9, 6.0),     # graphic: a long lens, the horizon high, the sun on a third
    "C2": (12.0, 45.0, -3.6, 4.0),   # graphic, higher: horizon on the upper third, the swell rows as bands
    "C3": (7.0, 40.0, -2.6, 2.0),
    "B2": (20.0, 30.0, -7.5, 2.0),
    # Pass 2: an elevated, illustrative view that shows the swell's pattern, the island, the boat and the sky
    "E1": (18.0, 38.0, -4.5, 4.0),
    "E2": (26.0, 34.0, -7.0, 2.0),
    "E3": (14.0, 32.0, -2.5, 0.0),
    # Pass 4 (the reset brief): three framings round a tiny island, each with its own island placement (COMP_ISLAND)
    # (the owner's framing direction: water and sky each about half, the horizon a touch off the midline, 45-48 %
    # or 52-55 % from the top, never a dead-centre split; held through the camera's breath. A 36 mm lens over the
    # 24 mm sensor gives 36.9 deg vertically, so pitch -1.15 puts the horizon at 47.0 %; the 0.35 m breath against
    # the fixed aim moves it by 0.5 %, so it stays 46.5-47.5 %.)
    "F1": (10.0, 36.0, -1.15, 0.0),  # low and close: the island large on the left, the horizon at 47 % (CHOSEN)
    "F2": (14.0, 32.0, -2.8, 0.0),   # the classic poster: the horizon on the upper two-fifths
    "F3": (21.0, 28.0, -4.6, 0.0),   # elevated and wide: more sea, the island smaller and further right
}
# per composition: the island's centre (x, z) and its scale
COMP_ISLAND = {"F1": ((-68.0, -270.0), 1.2), "F2": ((-128.0, -330.0), 1.0), "F3": ((-70.0, -340.0), 1.1)}


# ================================================================================================ the ocean mesh
def ocean_rings(r0, r1, near_step, screen_k):
    """Ring radii from r0 to r1: `near_step` metres apart near the camera (fine enough for the shortest wave), then
    apart by screen_k * r^2 -- a constant height on screen for a camera over a plane (a row about a pixel and a half at
    1080p with this lens), so the far water never turns into sub-pixel slivers."""
    rs = [r0]
    r = r0
    while r < r1:
        r += max(near_step, screen_k * r * r)
        rs.append(min(r, r1))
    return rs


def ocean_wedge(radii, half_angle_deg, segments, yaw_deg=0.0):
    """A fan of the sea in front of the camera (the camera at the origin looking along -Z turned by `yaw`): rings at
    `radii`, `segments` across +-half_angle. Shared vertices, flat (+Y) normals: the deformers make the waves and the
    vertex stage rebuilds the normals. Returns (verts, normals, indices)."""
    verts = []
    ha = math.radians(half_angle_deg)
    y0 = math.radians(yaw_deg)
    cols = segments + 1
    for r in radii:
        for j in range(cols):
            a = y0 - ha + 2.0 * ha * j / segments
            verts.append((round(r * math.sin(a), 4), 0.0, round(-r * math.cos(a), 4)))
    idx = []
    for i in range(len(radii) - 1):
        for j in range(segments):
            a = i * cols + j
            b = a + 1
            c = a + cols + 1
            d = a + cols
            idx += [a, b, c, a, c, d]       # front faces up (+Y) for this fan's ordering
    norms = [(0.0, 1.0, 0.0)] * len(verts)
    return verts, norms, idx


# The sea in three pieces: the vertex stage's finite-difference step is 1e-3 of a source's bounds, so each piece keeps a
# step fine enough for the waves it can show. (name, r0, r1, segments)
SEA_PIECES = [("near", 0.6, 200.0, 200), ("mid", 200.0, 1200.0, 200), ("far", 1200.0, 6000.0, 160)]
SEA_HALF_ANGLE = 62.0


def dome_mesh(radius=8000.0, segments=64, rings=24, skirt_deg=2.0):
    """The sky: a hemisphere and a 2 deg skirt below the horizon (to close the gap beyond the sea's far edge, which is
    0.1 deg down) -- NOT a full sphere. A sphere centred on the camera is drawn first and was shaded under the whole sea
    as well (the sky program ran on every pixel of the frame, not on the 40 % that is sky)."""
    verts = []
    elevs = [-skirt_deg] + [90.0 * (i / rings) ** 1.4 for i in range(rings + 1)]
    for e in elevs:
        er = math.radians(e)
        for j in range(segments + 1):
            a = 2.0 * math.pi * j / segments
            verts.append((round(radius * math.cos(er) * math.sin(a), 3), round(radius * math.sin(er), 3),
                          round(-radius * math.cos(er) * math.cos(a), 3)))
    idx = []
    cols = segments + 1
    for i in range(len(elevs) - 1):
        for j in range(segments):
            a = i * cols + j
            idx += [a, a + 1, a + cols + 1, a, a + cols + 1, a + cols]
    norms = [tuple(-c / radius for c in v) for v in verts]
    mx.write_glb(os.path.join(MESH_DIR, "do-dome.glb"), verts, norms, idx, "dome")
    return len(verts)


def generate_meshes():
    yaw = COMPOSITIONS[COMPOSITION][3]
    out = {"dome": dome_mesh()}
    for name, r0, r1, seg in SEA_PIECES:
        radii = ocean_rings(r0, r1, 0.5, 6.0e-5)
        v, n, i = ocean_wedge(radii, SEA_HALF_ANGLE, seg, yaw)
        mx.write_glb(os.path.join(MESH_DIR, "do-sea-%s.glb" % name), v, n, i, "sea_" + name)
        out[name] = (len(radii), len(v), len(i) // 3)
    return out


# ================================================================================================ waves
def gerstner(direction_deg, wavelength, amplitude, steep, speed_scale=1.0, phase=0.0):
    """One Gerstner train as two sine deformers: vertical a sin(k x + w t), and horizontal (along the travel) Q a cos,
    a quarter-cycle apart, which gathers the surface toward each crest (sharp crests, broad troughs). `direction_deg`
    is the travel direction: 0 = toward the camera (+Z), positive = arriving from the right of the frame. Deep-water
    dispersion w = sqrt(g k), times `speed_scale`."""
    a = math.radians(direction_deg)
    travel = (-math.sin(a), 0.0, math.cos(a))           # unit, toward the viewer at 0
    axis = [-travel[0], 0.0, -travel[2]]                 # crests move toward -axis as t grows
    k = 2.0 * math.pi / wavelength
    w = math.sqrt(9.81 * k) * speed_scale
    vert = {"kind": "sine", "amount": float(amplitude), "frequency": round(k, 6), "speed": round(w, 6),
            "phase": float(phase), "axis": [round(c, 6) for c in axis], "displacementAxis": [0.0, 1.0, 0.0],
            "space": "world"}
    # the horizontal amplitude is steep / k: `steep` is the classic Gerstner Q*A*k (1 = the crest folds into a cusp).
    # Negative along the travel, so points gather toward each crest (positive gathers them into the troughs)
    horiz = {"kind": "sine", "amount": round(-steep / k, 6), "frequency": round(k, 6), "speed": round(w, 6),
             "phase": round(phase + 0.5 * math.pi, 6), "axis": [round(c, 6) for c in axis],
             "displacementAxis": [round(c, 6) for c in travel], "space": "world"}
    return [vert, horiz]


STEEP = (0.42, 0.26, 0.17)   # Q*A*k per train: their SUM must stay under 1 or the surface folds over itself
SWELL_SPEED = 0.62      # stylised: slower than deep water, a dream's pace
# the swell set: (direction, wavelength m, amplitude m, steepness Q (0..1), phase): the long swell from under the sun,
# a cross swell from the right, a wind sea from the left
SWELLS = [
    (-6.0, 72.0, 1.05, STEEP[0], 0.0),
    (31.0, 37.0, 0.5, STEEP[1], 1.3),
    (-38.0, 15.5, 0.19, STEEP[2], 2.1),
]


def sea_deformers():
    d = []
    for direction, wl, amp, steep, ph in SWELLS:
        d += gerstner(direction, wl, amp, steep, SWELL_SPEED, ph)
    return d


# ================================================================================================ palettes
# Each palette names every colour the programs paint with (sRGB hex; converted to linear by hexrgb).
#   zenith, mid, warm, cool   the sky: top, middle, horizon toward / away from the sun
#   sun, glow                 the disc and its glow
#   deep                      the water's own body colour (what the eye sees looking into a face)
#   rzen, rhor                what a face reflects: the sky high above / at the horizon (darker, bluer than the sky)
#   sss                       light through a backlit crest
#   glint                     the sun on the water
#   foam                      crest foam
#   cshade, clit, crim        clouds: the shadowed body, the underside lit by the low sun, the silver rim
#   isle                      the distant island's silhouette
#   dback, dbelly, drim       a dolphin: its back, its belly, the sun's rim on it
PALETTES = {
    # ---- Pass 2: three coordinated families on one axis (dusk -> day -> electric), each designed as a whole
    # violet sunset: cobalt water with magenta-lavender crest light, peach rays, pink-orange clouds, dark violet land
    "violet": dict(zenith="#2a1a66", mid="#8a4aa8", warm="#ffb08a", cool="#c87ab8", sun="#fff2dc", glow="#ff8a6a",
                   deep="#2d2a96", rzen="#2c2f86", rhor="#d898cc", sss="#7a5ed0", glint="#ffd6a8", foam="#ffe4f4",
                   cshade="#5a3690", clit="#ffa48e", crim="#ffe2c4", isle="#6a4a96",
                   dback="#1d1448", dbelly="#8a68b8", drim="#ffc8a8",
                   land="#5a4aa8", land2="#2a1a5e", rock="#8a5c92", sand="#f4c0c0", leaf="#3a2a78", trunk="#3a2870",
                   hull="#fff2ea", hull2="#e2506a", sail="#ffb35a", sail2="#ffe7c0", ray="#ffd0a8", land3="#b07ab0", pearl="#fff0e6", glaze="#1c1a4a", deck="#dccbd8", hshade="#a99ac8", wmain="#3f3ab0", wcrest="#d47ad6", hl="#a8ecea", frond="#30246f", wnear="#1c1a6c"),
    # golden afternoon: turquoise and teal water, cream foam, coral sun, soft blue sky, warm green island
    "afternoon": dict(zenith="#2b68cc", mid="#7fb4ec", warm="#ffe0ac", cool="#bfe2f4", sun="#fffbe8", glow="#ffc884",
                      deep="#0a3c84", rzen="#2c78bc", rhor="#a8dcea", sss="#1fb4c8", glint="#fffbe8", foam="#fff4dc",
                      cshade="#90a8d8", clit="#fff5e6", crim="#fffbf0", isle="#7e9cc4",
                      dback="#284a7c", dbelly="#d0e2f0", drim="#fff4dc",
                      land="#45a85c", land2="#1f6e62", rock="#c88a5c", sand="#f6e2b4", leaf="#2f9a52", trunk="#8a5a3c",
                      hull="#fffbf0", hull2="#e84a3a", sail="#ff6a48", sail2="#fff6e6", ray="#d8c090", land3="#a8d86a", pearl="#fbf8ef", glaze="#14304e", deck="#e4dccc", hshade="#a8bccc", wmain="#1270b4", wcrest="#86ecdc", hl="#f6fff2", frond="#2f8a4c", wnear="#0a4a90"),
    # electric blue: ultramarine and cyan water, mint surf, saturated blue sky, yellow-coral accents, navy land
    "electric": dict(zenith="#0a2a9c", mid="#2c6ae4", warm="#8ff0ff", cool="#4a8cf0", sun="#fff7a8", glow="#ffd84a",
                     deep="#05126a", rzen="#0e40b6", rhor="#62d4ff", sss="#1c84f0", glint="#ffffe0", foam="#c8fff0",
                     cshade="#2448b0", clit="#c4ecff", crim="#fff6c0", isle="#2244a0",
                     dback="#0a1a56", dbelly="#4a80d8", drim="#fff7b0",
                     land="#14266a", land2="#0a1446", rock="#2a4aa0", sand="#4a76d0", leaf="#0e1c5a", trunk="#0c1650",
                     hull="#ffd84a", hull2="#ff5a4a", sail="#ff6a52", sail2="#fff7c8", ray="#8cc8f0", land3="#4a6ad8", pearl="#eef4ff", glaze="#081a52", deck="#cfd9ee", hshade="#7f9ad8", wmain="#0c38c8", wcrest="#62eaff", hl="#eafff8", frond="#10205e", wnear="#071a7a"),
    # the night end (CC 1): bioluminescent
    "midnight": dict(zenith="#03050f", mid="#17123c", warm="#3b2b6a", cool="#1d2150", sun="#e9f0ff", glow="#5d72c8",
                     deep="#02060f", rzen="#050a1c", rhor="#2a285c", sss="#10f0a8", glint="#cfe0ff", foam="#7affe0",
                     cshade="#0a0a22", clit="#33346e", crim="#a8b6ff", isle="#0a0a26",
                     dback="#020410", dbelly="#141a3a", drim="#9fb4ff",
                     land="#0a0c26", land2="#05061a", rock="#14183a", sand="#1a2650", leaf="#060a20", trunk="#05081a",
                     hull="#c8d4ff", hull2="#3a4a9a", sail="#6a7ad8", sail2="#a8b6ff", ray="#a8b8ff", land3="#1a2050", pearl="#7c88b8", glaze="#02040e", deck="#4a5488", hshade="#2a3260", wmain="#06123a", wcrest="#14c09a", hl="#8affe0", frond="#06081e", wnear="#020510"),
    # Pass 1's electric twilight, kept for before/after comparisons
    "twilight": dict(zenith="#100d36", mid="#563a8a", warm="#ff9b7d", cool="#8a6cc2", sun="#ffe2cc", glow="#ff7f68",
                     deep="#0a0b35", rzen="#191c58", rhor="#8a6cbd", sss="#18cfe0", glint="#ffd6c2", foam="#bdfde6",
                     cshade="#33245f", clit="#ff8a78", crim="#ffd3b4", isle="#3f2a68",
                     dback="#140f33", dbelly="#4a3a78", drim="#ffc3a8",
                     land="#3f2a68", land2="#24184a", rock="#5a3a7a", sand="#c08aa0", leaf="#2a1f52", trunk="#2a1f4a",
                     hull="#f5e6e8", hull2="#e2506a", sail="#ffb35a", sail2="#ffe7c0", ray="#ffc9a0", land3="#8a5a9a", pearl="#efe4ec", glaze="#141238", deck="#d6c8d2", hshade="#8a7aa8", wmain="#22266e", wcrest="#18cfe0", hl="#ffe0f0", frond="#241a52", wnear="#0b0c3a"),
}
PALETTE = "violet"
DEBUG_SKY = None
BANDS = 3
DEBUG_CLOUD = None
DEBUG_CLOUD_FLAT = False   # look development: the clouds as a flat double-sided colour, no program
CLOUD_DOUBLE = False       # the cloud material double-sided (look development)
CLOUD_DEFORMERS = 2        # how many of the cloud deformers to keep (look development)
DEBUG_WATER = None         # look development: (op label, register): the water emits that register after that op
CAMERA_BREATH = True
REVIEW_MIDI = None          # look development: {"padLeap": [t, ...], "night": [(t, v), ...]} scores the MIDI offline


def pal(name, key, scale=1.0):
    return hexrgb(PALETTES[name][key], scale)


# ================================================================================================ programs
class Prog:
    """A material program assembled by label: `op(label, kind, dst, ...)` appends one op and remembers its 1-based
    number (what a route's `material/<name>/op/<n>/...` path addresses), so a route names `P["glint"]`, not a count."""

    def __init__(self, name):
        self.name = name
        self.ops = []
        self.at = {}

    def op(self, label, kind, dst, a=None, b=None, c=None, k=None, f=None, inp=None, seed=None):
        o = {"kind": kind, "dst": dst}
        if kind == "remap" and f is None:
            f = 0.0         # an op's value defaults to 1, which makes a remap CLAMP: say so explicitly
        if a is not None:
            o["srcA"] = a
        if b is not None:
            o["srcB"] = b
        if c is not None:
            o["srcC"] = c
        if k is not None:
            o["constant"] = [round(float(x), 6) for x in (list(k) + [0.0] * 4)[:4]]
        if f is not None:
            o["value"] = float(f)
        if inp is not None:
            o["input"] = inp
        if seed is not None:
            o["seed"] = int(seed)
        self.ops.append(o)
        if label:
            assert label not in self.at, label
            self.at[label] = len(self.ops)
        assert len(self.ops) <= 48, "%s: more than 48 ops" % self.name
        return len(self.ops)

    def __getitem__(self, label):
        return self.at[label]

    def colour(self, label, dst, rgb, scale=1.0):
        return self.op(label, "constant", dst, k=[x * scale for x in rgb] + [1.0])

    def pcolour(self, label, dst, key, scale=1.0):
        """A palette colour (by key): remembered, so a palette change can be routed onto it (`palette_routes`)."""
        self.pal = getattr(self, "pal", [])
        self.pal.append((label, key, scale))
        return self.colour(label, dst, pal(PALETTE, key), scale)

    def doc(self, emission, opacity=-1, base=-1):
        return {"name": self.name, "ops": self.ops, "baseColor": base, "metallic": -1, "roughness": -1,
                "emission": emission, "emissionIntensity": 1.0, "opacity": opacity}


SH = unit((S[0], 0.0, S[2]))     # towards the sun, level


def sea_program(far=False):
    """The sea, unlit. What a face reflects (the sky's gradient at the reflected ray's elevation), the water's own body
    colour where the eye looks into a face (a Fresnel mix), the sun on the water (a broad warm path and the crisp glints
    in it, where the reflected ray meets the sun), and light through the backlit crests.

    The reflected ray r = 2 (n.v) n - v is never built: its elevation and its angle to the sun are taken as halves,
    r.y / 2 + 1/2 = (n.v) n.y + (1 - v.y) / 2 and r.l / 2 = (n.v)(n.l) + (-v.l) / 2, which saves the doublings.
    Registers: 0 N (tilted by the micro-ripples), 1 V, 2 n.v."""
    P = Prog("doSeaFar" if far else "doSea")
    sx, sy, sz = S
    pn = PALETTE
    P.op("N", "input", 0, inp="normal")
    P.op("V", "input", 1, inp="viewDirection")
    if far:
        # the far water (beyond 200 m): its rows sample the shorter trains too coarsely, so a crest's light and the
        # normal turn into dashes that change every frame. A painter's answer: the distant sea flattens into smooth
        # colour and the glitter path. The normal leans to the vertical with distance; the crest light fades.
        P.op(None, "input", 2, inp="cameraDistance")
        P.op("flatten", "remap", 2, a=2, k=[FAR_FLAT0, FAR_FLAT1, 0.0, FAR_FLAT_MAX], f=1.0)
        P.op(None, "constant", 3, k=[0.0, 1.0, 0.0, 0.0])
        P.op(None, "mixBy", 0, a=0, b=3, c=2)
    else:
        # micro-ripples: the normal tilted by a fine noise carried by the surface (its rest position, so the swell's
        # orbits carry it); `shimmer`'s offset drifts (a slow saw), `shimmerAmp` is how rough
        # (microDetail: a noise that fades to its mean as a pixel's footprint grows over its period -- the engine's
        # own anti-aliasing, which replaced a hand-made distance fade and freed two ops)
        P.op(None, "input", 2, inp="localPosition")
        P.op("shimmer", "microDetail", 2, a=2, f=SHIMMER_FREQ, k=[0.0, 0.0, 0.0], seed=11)
        P.op("shimmerAmp", "remap", 2, a=2, k=[0.3, 0.7, -SHIMMER_AMP, SHIMMER_AMP])
        P.op(None, "add", 0, a=0, b=2)                                   # a tilt along (1, 1, 1): enough
    P.op(None, "fresnel", 2, f=1.0)                                      # 1 - n.v (the fragment's own normal)
    P.op("nv", "remap", 2, a=2, k=[0.0, 1.0, 1.0, 0.0])
    # the reflected sky: by r.y
    P.op(None, "swizzle", 3, a=0, k=[1, 1, 1, 1])
    P.op(None, "multiply", 3, a=3, b=2)
    P.op(None, "gradient", 4, a=1, k=[0.0, -0.5, 0.0, 0.5], f=1.0)      # (1 - v.y) / 2
    P.op(None, "add", 3, a=3, b=4)                                       # r.y / 2 + 1/2
    P.op("rygrad", "remap", 3, a=3, k=[0.5, 0.5 + REFL_SPAN, 0.0, 1.0], f=1.0)
    if BANDS:
        P.op(None, "quantize", 3, a=3, f=BANDS)
    else:
        P.op(None, "power", 3, a=3, f=0.6)
    P.pcolour("rhor", 4, "rhor")
    P.pcolour("rzen", 5, "rzen")
    P.op("refl", "mixBy", 4, a=4, b=5, c=3)
    # the sun on the water: by r.l
    P.op(None, "gradient", 5, a=0, k=[sx, sy, sz, 0.0], f=1.0)           # n.l
    P.op(None, "multiply", 5, a=5, b=2)
    P.op("back", "gradient", 6, a=1, k=[-0.5 * sx, -0.5 * sy, -0.5 * sz, 0.0], f=1.0)   # -v.l / 2: toward the sun
    P.op("rl", "add", 5, a=5, b=6)                                       # r.l / 2
    P.op("path", "smoothstep", 7, a=5, k=[PATH_COS / 2.0, 0.5])
    P.op(None, "power", 7, a=7, f=PATH_SHAPE)
    P.pcolour("pathCol", 3, "glow", PATH_GAIN)
    P.op(None, "multiply", 7, a=7, b=3)
    P.op(None, "add", 4, a=4, b=7)
    P.op("glints", "smoothstep", 5, a=5, k=[GLINT_COS0 / 2.0, GLINT_COS1 / 2.0])
    P.pcolour("glint", 3, "glint", GLINT_GAIN)
    P.op(None, "multiply", 5, a=5, b=3)
    P.op("sunlit", "add", 4, a=4, b=5)
    # the water's body where the eye looks into a face, the reflection at grazing angles
    P.pcolour("deep", 5, "deep")
    P.op("fres", "fresnel", 7, f=FRESNEL_POWER)
    P.op("fresMin", "remap", 7, a=7, k=[0.0, 1.0, FRESNEL_MIN, 1.0], f=1.0)
    P.op("water", "mixBy", 5, a=5, b=4, c=7)
    # light through the backlit crests: crest height x toward the sun
    P.op(None, "power", 6, a=6, f=SSS_FOCUS)                             # (-v.l / 2)^focus: toward the sun
    P.op("wp", "input", 7, inp="worldPosition")
    P.op("crest", "gradient", 7, a=7, k=[0.0, 1.0 / (CREST_HI - CREST_LO), 0.0, -CREST_LO / (CREST_HI - CREST_LO)],
         f=1.0)
    if not far:
        # (critique rounds 1-2) crest light and foam are middle-distance things: right under the eye the face is seen
        # front-on, and the nearest swell became a flat glowing slab of foam. The crest ramp itself fades toward the
        # camera, so both the light through it and the foam on it need a taller crest there.
        P.op(None, "input", 3, inp="cameraDistance")
        P.op("crestNear", "remap", 3, a=3, k=[CREST_NEAR0, CREST_NEAR1, CREST_NEAR_MIN, 1.0], f=1.0)
        P.op(None, "multiply", 7, a=7, b=3)
    P.op(None, "multiply", 6, a=6, b=7)
    P.pcolour("sss", 3, "sss", 2.0 ** SSS_FOCUS * SSS_GAIN)
    P.op(None, "multiply", 6, a=6, b=3)
    if far:
        P.op(None, "input", 7, inp="cameraDistance")
        P.op("crestFade", "remap", 7, a=7, k=[FAR_FLAT0, FAR_FLAT1, 1.0, 1.0 - FAR_FLAT_MAX], f=1.0)
        P.op(None, "multiply", 6, a=6, b=7)
    P.op("lit", "add", 5, a=5, b=6)
    if not far:
        # foam (ADDED, not mixed: a tall crest under a mixed foam was a flat slab): thin caps on the highest crests only (where the trains reinforce), seen edge-on from this height as
        # ribbons; screen-space curvature was tried and gave a broad wash. `foam`'s colour is what the drums scale
        P.op("foamMask", "smoothstep", 6, a=7, k=[FOAM_H0, FOAM_H1])
        P.pcolour("foam", 7, "foam", FOAM_GAIN)
        P.op(None, "multiply", 6, a=6, b=7)
        P.op("foamed", "add", 5, a=5, b=6)                               # added: the crest's form stays under it
    return P


REFL_SPAN = 0.3             # r.y / 2 over which the reflection runs from the horizon's colour to the zenith's
PATH_COS = math.cos(math.radians(14.0))
PATH_SHAPE = 3.0
PATH_GAIN = 0.55
GLINT_COS0, GLINT_COS1 = math.cos(math.radians(2.6)), math.cos(math.radians(0.9))
GLINT_GAIN = 2.4
SHIMMER_FREQ = 0.55
SHIMMER_AMP = 0.055
SHIMMER_NEAR, SHIMMER_FAR = 50.0, 200.0
FAR_FLAT0, FAR_FLAT1, FAR_FLAT_MAX = 200.0, 700.0, 0.85
FRESNEL_POWER = 3.0
FRESNEL_MIN = 0.2
CREST_LO, CREST_HI = 0.3, 1.9
SSS_GAIN = 0.9
SSS_FOCUS = 3.0
CREST_NEAR0, CREST_NEAR1, CREST_NEAR_MIN = 12.0, 60.0, 0.55
FOAM_H0, FOAM_H1 = 0.64, 0.8          # of the crest ramp (0.3..1.9 m): the tops of the highest crests
FOAM_GAIN = 0.6


def water_program(far=False):
    """THE ANIME WATER, redesigned from its fundamentals (Pass 4; the owner's brief 10, built in stages A-D).

    One illustrated surface in six coordinated layers, each a cut of ONE scalar from the wave geometry (nothing
    posterizes a colour, nothing reflects physically):
      t       the illustration's light, centred on 0: the displaced HEIGHT (crests +, troughs -, over W_HEIGHT) mixed
              with the face's orientation (the backs of the waves, toward the camera, are the lit faces), plus a
              small share of a wind-streaked detail noise of the REST position (the swell's orbits carry it)
      AA      the engine's per-pixel `footprint` becomes a contrast gain g (1 near, W_AA_MIN far); t is multiplied by
              it before any edge, so no edge is sharper than its pixels and the far sea converges on the middle tone
      A. base regions   the foreground's deep indigo giving way to the cobalt body (`zone`, by distance), and the
              wave-face bands: troughs (`deep`), the body (`wmain`), the lit backs (`sss`): two soft edges on t g
      B. crest marks    `m` = the streaked noise mixed with t g: a cut of it near the crest draws tapered strokes that
              ride the crests (the noise breaks them along the crest into dashes, t ends them across it)
      C. the sun's path a column under the sun (the view ray across the sun's azimuth, narrowing with distance), lit
              as broken horizontal marks by the same `m`: the rays' continuation across the sea
      D. foam           a higher cut of `m g`, on the reinforced crests only, in the palette's pink-white
      horizon a Fresnel blend into the reflected horizon colour, shared by both programs, so they meet without a seam
    The far program (beyond 200 m) is the same less the zone, the strokes and the foam; its path marks are merged
    toward one smooth path by g. Registers: 0 N then zone, 1 V, 2 g, 3 t g, 4 m, 5/6 scratch, 7 colour."""
    P = Prog("doSeaFar" if far else "doSea")
    stage = WATER_STAGE
    P.op("N", "input", 0, inp="normal")
    P.op("V", "input", 1, inp="viewDirection")
    P.op("foot", "input", 2, inp="footprint")
    P.op("aaGain", "remap", 2, a=2, k=[0.0, W_AA_FP, 1.0, W_AA_MIN], f=1.0)             # g, clamped
    P.op("wp", "input", 3, inp="worldPosition")
    P.op("crest", "gradient", 3, a=3, k=[0.0, 1.0 / W_HEIGHT, 0.0, 0.5], f=1.0)        # height / span + 0.5
    P.op("face", "gradient", 4, a=0, k=[-W_FACE * SH[0], W_FLAT, -W_FACE * SH[2], 0.5 - W_FLAT], f=1.0)
    P.op("faceMix", "mix", 3, a=3, b=4, f=W_FACE_MIX)
    P.op(None, "input", 6, inp="localPosition")
    P.op("stretch", "constant", 4, k=[W_STREAK, 1.0, 1.0, 1.0])                          # wind streaks along x
    P.op(None, "multiply", 6, a=6, b=4)
    P.op("detailNoise", "noise", 6, a=6, f=W_DETAIL_FREQ, k=[0.0, 0.0, 0.0], seed=19)
    P.op("detail", "mix", 3, a=3, b=6, f=W_DETAIL)                                       # t (1 - D) + noise D
    P.op("centre", "remap", 3, a=3, k=[0.0, 1.0, -0.5, 0.5])                             # t centred on 0
    P.op("contrast", "multiply", 3, a=3, b=2)                                            # t g
    # A. the base regions
    P.pcolour("deep", 7, "deep")
    if not far:
        P.op(None, "input", 0, inp="cameraDistance")
        P.op("zone", "remap", 0, a=0, k=[W_ZONE0, W_ZONE1, 1.0, 0.0], f=1.0)             # 1 in the foreground
        P.pcolour("wnear", 5, "wnear")
        P.op(None, "mixBy", 7, a=7, b=5, c=0)
    for lab, e, soft, key in (("band1", W_E1, W_SOFT1, "wmain"), ("band2", W_E2, W_SOFT, "sss")):
        P.op(lab, "smoothstep", 4, a=3, k=[e - soft, e + soft])
        P.pcolour(key, 5, key)
        P.op(None, "mixBy", 7, a=7, b=5, c=4)
    # the marks' scalar: the streaked noise and the crest together (far: the noise centred and faded by g, so the
    # marks merge into one smooth path toward the horizon instead of flickering)
    if far:
        P.op("noiseCentre", "remap", 6, a=6, k=[0.0, 1.0, -0.5, 0.5])
        P.op("noiseAA", "multiply", 6, a=6, b=2)
        P.op("noiseBack", "remap", 6, a=6, k=[-0.5, 0.5, 0.0, 1.0])
    P.op("marks", "mix", 4, a=6, b=3, f=W_MARK_CREST)                                   # m
    # B. the crest strokes (near)
    if not far and stage in ("B", "C", "D"):
        P.op("strokeLo", "smoothstep", 5, a=4, k=[W_S0, W_S0 + W_SW])
        P.pcolour("hl", 6, "hl", W_HL)
        P.op("stroked", "mixBy", 7, a=7, b=6, c=5)
    # D. foam (near): the cut rises with the sea (routes), so caps stay caps
    if not far and stage == "D":
        P.op("foamAA", "multiply", 5, a=4, b=2)
        P.op("foamMask", "smoothstep", 5, a=5, k=[W_FOAM_CUT, W_FOAM_CUT + W_FOAM_SOFT])
        P.pcolour("foam", 6, "foam", W_FOAM_GAIN)
        P.op("foamed", "mixBy", 7, a=7, b=6, c=5)
    # C. the sun's path
    if stage in ("C", "D"):
        px, pz = SH[2], -SH[0]                                                           # across the sun's azimuth
        P.op("across", "gradient", 5, a=1, k=[px, 0.0, pz, 0.5], f=1.0)
        P.op(None, "remap", 5, a=5, k=[0.0, 1.0, -0.5, 0.5])
        if not far:
            P.op("pathWidth", "remap", 6, a=0, k=[0.0, 1.0, W_PATH_NARROW, 1.0], f=1.0)   # narrows with distance
            w = W_PATH_W
        else:
            # beyond 200 m the column keeps narrowing toward the horizon (the coordinator's round 2)
            P.op(None, "input", 6, inp="cameraDistance")
            P.op("pathWidth", "remap", 6, a=6, k=[200.0, 3000.0, 1.0, W_PATH_FAR_NARROW], f=1.0)
            w = W_PATH_W / W_PATH_NARROW
        P.op(None, "multiply", 5, a=5, b=6)
        P.op(None, "multiply", 5, a=5, b=5)
        P.op("column", "smoothstep", 5, a=5, k=[w * w, 0.0])
        if not far:
            # the marks scalar TIMES the column, then one cut: at the column's centre every lit back is a mark,
            # toward its edges only the crests are, so the edges are ragged and the path scatters
            P.op(None, "multiply", 5, a=5, b=4)
            P.op("pathMarks", "smoothstep", 5, a=5, k=[W_P0, W_P0 + W_PW])
        else:
            # far: the cut first (the crest rows break the marks), THEN the column's soft profile: soft sides
            P.op("pathMarks", "smoothstep", 6, a=4, k=[W_P0_FAR, W_P0_FAR + W_PW_FAR])
            P.op(None, "multiply", 5, a=5, b=6)
        P.pcolour("glint", 6, "glint", W_PATH_GAIN)
        P.op("sunlit", "mixBy", 7, a=7, b=6, c=5)
    # the horizon: confined to the last degrees (a 10 m eye sees most of the sea at a grazing angle, and a gentle
    # Fresnel washed every band into one mauve-grey, Pass 4.2); the far program blends further for depth
    P.op("fres", "fresnel", 5, f=W_FRES_POWER_FAR if far else W_FRES_POWER)
    P.op("fresGain", "remap", 5, a=5, k=[0.0, 1.0, 0.0, W_HORIZON_FAR if far else W_HORIZON])
    P.pcolour("rhor", 6, "rhor")
    P.op("horizon", "mixBy", 7, a=7, b=6, c=5)
    P.emit = 7
    return P


# the anime water's look (each a parameter of the op it sits in, so routes and MIDI reach them)
WATER_STAGE = "D"       # the brief's stages: A base regions, B + crest strokes, C + the sun's path, D + foam
W_HEIGHT = 3.6          # metres of height over which t runs from trough to crest (the `crest` op)
W_FACE, W_FLAT = 0.9, 0.25   # weight of the face's orientation / of flatness in the `face` term
W_FACE_MIX = 0.35       # how much of t is orientation (the rest is height)
W_DETAIL_FREQ = 0.9     # the surface detail's scale (1/m) across the view (z); along x it is W_STREAK of it
W_STREAK = 0.16         # the detail is stretched along x (across the view): dashes along the crests, not blotches
W_DETAIL = 0.07         # its share of t
W_AA_FP, W_AA_MIN = 0.6, 0.35   # g falls linearly from 1 (sharp pixels) to W_AA_MIN at a footprint of W_AA_FP m
W_ZONE0, W_ZONE1 = 12.0, 120.0  # the foreground zone (wnear) fades into the body over these distances (m)
W_SOFT1 = 0.06          # the trough edge's half-width: a soft shadow under each wave, not a hole
W_SOFT = 0.018          # band edge half-width in centred t (the band contrast: smaller is crisper)
W_E1, W_E2 = -0.07, 0.1    # the band thresholds in centred t: deep | main | lit
W_MARK_CREST = 0.7      # m = mix(noise, t g, this): how much the crest decides the marks
W_S0, W_SW = 0.42, 0.02    # the stroke cut in m and its edge width
W_HL = 1.1
W_FOAM_CUT, W_FOAM_SOFT = 0.445, 0.02
W_FOAM_GAIN = 0.95
W_PATH_W = 0.15         # the sun path's half-width near (the sine of its angle across the sun's azimuth)
W_PATH_NARROW = 3.2     # how much narrower the path is beyond the foreground zone
W_P0, W_PW = 0.2, 0.1      # the path marks' cut in m x column and its edge width (near)
W_P0_FAR, W_PW_FAR = 0.17, 0.08   # far: the cut sits in the crest rows, so the column breaks by the waves
W_PATH_FAR_NARROW = 2.6     # the far column narrows this much more from 200 m to 3 km
W_PATH_GAIN = 1.8
W_FRES_POWER, W_FRES_POWER_FAR = 24.0, 10.0   # the Fresnel power near / far
W_HORIZON, W_HORIZON_FAR = 0.45, 0.8           # how far the horizon colour reaches into the water near / far
WATER = "anime"         # "anime" (Pass 4) or "pass2"


def sky_program():
    """The sky dome: the horizon (warm toward the sun, cool away), the middle, the zenith; the sun's glow and disc;
    below the horizon, the far water (the reflected horizon). Registers: 0 V, 1 the view ray's elevation e."""
    P = Prog("doSky")
    sx, sy, sz = S
    pn = PALETTE
    P.op("V", "input", 0, inp="viewDirection")
    P.op(None, "swizzle", 1, a=0, k=[1, 1, 1, 1])
    P.op("e", "remap", 1, a=1, k=[0.0, 1.0, 0.0, -1.0])                  # e = -v.y: the ray's elevation
    P.op(None, "gradient", 2, a=0, k=[-0.5 * SH[0], 0.0, -0.5 * SH[2], 0.5], f=1.0)   # toward the sun, 0..1
    P.op("warmth", "power", 2, a=2, f=2.4)
    P.pcolour("cool", 3, "cool")
    P.pcolour("warm", 4, "warm")
    P.op("horizon", "mixBy", 3, a=3, b=4, c=2)
    P.op(None, "remap", 4, a=1, k=[0.004, 0.075, 0.0, 1.0], f=1.0)
    P.op(None, "power", 4, a=4, f=0.8)
    P.pcolour("mid", 5, "mid")
    P.op(None, "mixBy", 5, a=3, b=5, c=4)
    P.op(None, "remap", 4, a=1, k=[0.05, 0.26, 0.0, 1.0], f=1.0)
    P.pcolour("zenith", 6, "zenith")
    P.op("sky", "mixBy", 5, a=5, b=6, c=4)
    # the sun
    P.op("cosSun", "gradient", 4, a=0, k=[-sx, -sy, -sz, 0.0], f=1.0)
    if RAYS:
        # SUN RAYS (Pass 2), painted into the sky so clouds and the island cover them. The pattern is a noise of the
        # view ray's direction AROUND the sun: w = v + s c (the part of the ray across the sun's axis), divided by its
        # length sqrt(1 - c^2) -- `power` with a negative exponent is the reciprocal no op offers. The noise's offset
        # drifts (a slow saw), so the rays grow, part and merge instead of spinning.
        P.op(None, "multiply", 2, a=4, b=4)
        P.op(None, "remap", 2, a=2, k=[0.0, 1.0, 1.0001, 0.0001])                     # 1 - c^2 (never 0)
        P.op(None, "power", 2, a=2, f=-0.5)                                           # 1 / |w|
        P.op(None, "constant", 3, k=[sx, sy, sz, 0.0])
        P.op(None, "multiply", 3, a=3, b=4)
        P.op(None, "add", 3, a=3, b=0)                                                 # w = v + s c
        P.op(None, "multiply", 3, a=3, b=2)                                            # its direction
        P.op("rayNoise", "noise", 3, a=3, f=RAY_FREQ, k=[0.0, 0.0, 0.0], seed=29)
        # Pass 4: broad tapered rays of unequal strength -- a soft cut of the noise (its slopes are the tapers),
        # times the noise again, so a ray's brightness follows how far above the cut it rises
        P.op("rayBands", "smoothstep", 2, a=3, k=[RAY_LO, RAY_HI])
        P.op(None, "multiply", 3, a=3, b=2)
        P.op("rayLen", "remap", 2, a=4, k=[math.cos(math.radians(RAY_REACH)), 1.0, 0.0, 1.0], f=1.0)
        P.op(None, "power", 2, a=2, f=RAY_FALL)
        P.op(None, "multiply", 3, a=3, b=2)
        P.pcolour("ray", 2, "ray", RAY_GAIN)
        P.op(None, "multiply", 3, a=3, b=2)
        P.op("rays", "add", 5, a=5, b=3)
    P.op("glowPow", "power", 6, a=4, f=28.0)                            # the broad glow
    P.pcolour("glow", 7, "glow", 0.55)
    P.op(None, "multiply", 6, a=6, b=7)
    P.op(None, "add", 5, a=5, b=6)
    P.op("haloPow", "power", 6, a=4, f=900.0)                           # the halo round the disc
    P.pcolour("halo", 7, "glow", 1.6)
    P.op(None, "multiply", 6, a=6, b=7)
    P.op(None, "add", 5, a=5, b=6)
    P.op("disc", "smoothstep", 6, a=4, k=[SUN_COS - 0.00012, SUN_COS + 0.00004, 0.0, 0.0])
    P.pcolour("sun", 7, "sun", 7.0)
    P.op(None, "multiply", 6, a=6, b=7)
    P.op(None, "add", 5, a=5, b=6)
    # below the horizon: the far water
    P.op(None, "threshold", 6, a=1, f=0.0)
    P.pcolour("far", 7, "rhor")
    P.op(None, "mixBy", 5, a=7, b=5, c=6)
    return P


def cloud_program():
    """The clouds, unlit: a dusky body, the underside and the sun-facing side lit coral by the low sun, a bright rim where
    a cloud's edge is backlit near the sun, and the haze of distance (the horizon's own colour, warm toward the sun).
    Registers: 0 N, 1 V."""
    P = Prog("doCloud")
    sx, sy, sz = S
    pn = PALETTE
    P.op("N", "input", 0, inp="normal")
    P.op("V", "input", 1, inp="viewDirection")
    # Pass C: painted in two bands, like an anime sky -- the undersides that face the low sun glow, the tops and the
    # sides away from it fall into a flat shadow tone, split by one soft edge (not a gradient)
    P.op(None, "gradient", 2, a=0, k=[0.55 * SH[0], -0.75, 0.55 * SH[2], 0.15], f=1.0)
    P.op("toward", "gradient", 4, a=1, k=[-sx, -sy, -sz, 0.0], f=1.0)     # cos of the angle to the sun
    P.op(None, "smoothstep", 5, a=4, k=[math.cos(math.radians(CLOUD_POOL[1])), math.cos(math.radians(CLOUD_POOL[0]))])
    P.op(None, "remap", 5, a=5, k=[0.0, 1.0, CLOUD_AWAY, 1.0])
    P.op(None, "add", 2, a=2, b=5)
    P.op("cband", "smoothstep", 2, a=2, k=[CLOUD_BAND0, CLOUD_BAND1])
    P.pcolour("cshade", 3, "cshade")
    P.pcolour("clit", 5, "clit", 1.1)
    P.op("body", "mixBy", 3, a=3, b=5, c=2)
    # the rim: grazing edges, toward the sun
    P.op(None, "fresnel", 2, f=2.0)
    P.op(None, "power", 4, a=4, f=40.0)
    P.op(None, "multiply", 2, a=2, b=4)
    P.pcolour("crim", 4, "crim", 1.1)                                  # (round 1: 3.0 drew cut-paper outlines)
    P.op(None, "multiply", 2, a=2, b=4)
    P.op("rim", "add", 3, a=3, b=2)
    # distance haze: decametres (op constants are clamped to +-1000)
    P.op(None, "input", 2, inp="cameraDistance")
    P.op(None, "remap", 2, a=2, k=[1500.0 / 10.0, 7000.0 / 10.0, 0.0, 1.0], f=0.0)
    P.op(None, "constant", 4, k=[0.1, 0.1, 0.1, 0.1])
    P.op(None, "multiply", 2, a=2, b=4)
    P.op(None, "remap", 2, a=2, k=[0.0, 1.0, CLOUD_HAZE0, CLOUD_HAZE1], f=1.0)
    P.op(None, "gradient", 4, a=1, k=[-0.5 * SH[0], 0.0, -0.5 * SH[2], 0.5], f=1.0)
    P.op(None, "power", 4, a=4, f=2.4)
    P.pcolour("hcool", 5, "mid")
    P.pcolour("hwarm", 6, "warm")
    P.op(None, "mixBy", 5, a=5, b=6, c=4)
    P.op("hazed", "mixBy", 3, a=3, b=5, c=2)
    return P


CLOUD_HAZE0, CLOUD_HAZE1 = 0.1, 0.5
CLOUD_POOL = (2.0, 19.0)     # degrees from the sun: fully lit inside, CLOUD_AWAY outside
CLOUD_AWAY = 0.1
CLOUD_BAND0, CLOUD_BAND1 = 0.62, 0.74


def cloud_outline(lobes, belly, n=72, c0=(0.0, 0.0)):
    """A cloud's silhouette in its own plane (x along, y up): the union of a few circles `lobes` [(cx, cy, r)] over
    a flat underside at y = -belly, sampled as a star-shaped polygon from c0 (which must lie inside a lobe). One clean
    contour: the lobes are what the top reads as; the base is one line."""
    pts, rads = [], []
    for i in range(n):
        th = 2.0 * math.pi * i / n
        dx, dy = math.cos(th), math.sin(th)
        far = 0.0
        for cx, cy, r in lobes:
            # the far intersection of the ray c0 + t d with the circle
            ox, oy = c0[0] - cx, c0[1] - cy
            b = ox * dx + oy * dy
            c = ox * ox + oy * oy - r * r
            disc = b * b - c
            if disc > 0.0:
                far = max(far, -b + math.sqrt(disc))
        rads.append(far)
    # the joints between lobes are cusps: smooth the radius over five samples, so the contour is one soft curve
    w = max(2, n // 144)                                       # half the window: wider for the densely sampled ribbons
    sm = [sum(rads[(i + k) % n] for k in range(-w, w + 1)) / (2 * w + 1) for i in range(n)]
    for i in range(n):
        th = 2.0 * math.pi * i / n
        x, y = c0[0] + math.cos(th) * sm[i], c0[1] + math.sin(th) * sm[i]
        if y < 0.0:
            y = -belly * math.tanh(-y / belly)                   # the underside: flat, but curving into the lobes' ends
        pts.append((x, y))
    return pts


def cloud_pillow(lobes, belly, thick, n=72, rings=(1.0, 0.86, 0.68, 0.48, 0.28, 0.11)):
    """The silhouette inflated into a pillow: rings of the outline scaled toward its centre, lifted out of the plane
    by sqrt(1 - s^2) x thick on both sides, capped at the centre. Returns (verts (x, y, z), faces) with smooth-shaded
    normals from smooth_shaded: the two-band cloud program then splits it into one lit and one shadowed region with
    a single soft edge -- not one rim per lobe."""
    c0 = (sum(l[0] * l[2] for l in lobes) / sum(l[2] for l in lobes), 0.35 * max(l[1] + l[2] for l in lobes))
    span = max(l[0] + l[2] for l in lobes) - min(l[0] - l[2] for l in lobes)
    height = max(l[1] + l[2] for l in lobes)
    n = int(n * max(1.0, min(8.0, span / (3.0 * height))))          # a ribbon needs many more samples round
    out = cloud_outline(lobes, belly, n, c0)
    verts, faces = [], []
    for side in (1.0, -1.0):
        base = len(verts)
        for k, sc in enumerate(rings):
            z = side * thick * math.sqrt(max(0.0, 1.0 - sc * sc))
            for (x, y) in out:
                verts.append((c0[0] + (x - c0[0]) * sc, c0[1] + (y - c0[1]) * sc, z))
        centre = len(verts)
        verts.append((c0[0], c0[1], side * thick))
        K = len(rings)
        for k in range(K - 1):
            a0, a1 = base + k * n, base + (k + 1) * n
            for i in range(n):
                m = (i + 1) % n
                if side > 0:                                       # wound outward on both sheets
                    faces += [(a0 + i, a0 + m, a1 + i), (a0 + m, a1 + m, a1 + i)]
                else:
                    faces += [(a0 + i, a1 + i, a0 + m), (a0 + m, a1 + i, a1 + m)]
        last = base + (K - 1) * n
        for i in range(n):
            m = (i + 1) % n
            faces.append((last + i, last + m, centre) if side > 0 else (last + i, centre, last + m))
    return verts, faces


def cloud_lobes(width, height, n, seed, tier=0.0):
    """A formation's lobes: `n` circles along the width, the biggest toward the middle, their undersides near y = 0
    (an anime cumulus: flat base, rounded top); with `tier`, a second, smaller row stacked on the first (a crown)."""
    g = lambda j: mx._hash(seed * 131, j, 7)
    lobes = []
    n = max(n, int(round(width / (1.5 * height))))          # a ribbon needs lobes close enough to overlap
    for i in range(n):
        u = (i + 0.5) / n - 0.5 + (g(i) - 0.5) * 0.3 / n
        r = height * (0.5 + 0.5 * (1.0 - (2.0 * u) ** 2) ** 0.7) * (0.6 + 0.4 * g(i + 20))
        r = max(min(r, height * 0.95), 0.72 * height)
        if width > 6.0 * height:                                 # a ribbon: near-equal lobes, a shallow scallop on top
            r = height * (0.88 + 0.07 * g(i + 20))
        lobes.append((u * width * (1.0 - r / width), r * 0.82, r))
    if tier > 0.0:
        m = max(2, n - 2)
        for i in range(m):
            u = (i + 0.5) / m - 0.5 + (g(i + 40) - 0.5) * 0.2
            r = height * tier * (0.6 + 0.4 * (1.0 - (2.0 * u) ** 2)) * (0.7 + 0.3 * g(i + 60))
            lobes.append((u * width * 0.55, height * 0.95 + r * 0.45, r))
    return lobes


# The formations: (azimuth deg from the view, distance m, base m, width m, height m, lobes, tier). The sun is at
# azimuth 13, elevation 5.5: its negative space (az 0..26, below elev 9) stays clear. Big banks on the left, a low
# ribbon on the right, one formation above-right of the sun; the rest round the sky, so as the field turns (65 min a
# turn) every one comes past the sun. Two layers, two drift rates: parallax.
CLOUDS_NEAR = {
    "layered": [(-14.0, 3600.0, 520.0, 1100.0, 340.0, 4, 0.0), (50.0, 4000.0, 210.0, 1500.0, 120.0, 3, 0.0),
                (23.0, 4600.0, 1120.0, 850.0, 240.0, 3, 0.0), (-2.0, 4800.0, 1000.0, 600.0, 170.0, 3, 0.0),
                (-66.0, 3500.0, 280.0, 1300.0, 420.0, 4, 0.0), (80.0, 3800.0, 320.0, 1200.0, 380.0, 4, 0.0),
                (140.0, 3600.0, 300.0, 1500.0, 440.0, 4, 0.0), (-130.0, 3900.0, 290.0, 1300.0, 400.0, 4, 0.0),
                (-180.0, 3700.0, 330.0, 1500.0, 460.0, 4, 0.0)],
    "rich": [(-14.0, 3600.0, 520.0, 1100.0, 420.0, 5, 0.55), (50.0, 4000.0, 210.0, 1500.0, 130.0, 4, 0.0),
             (23.0, 4600.0, 1120.0, 900.0, 280.0, 4, 0.45), (-2.0, 4800.0, 1000.0, 620.0, 190.0, 3, 0.4),
             (-66.0, 3500.0, 280.0, 1300.0, 480.0, 5, 0.5), (-52.0, 4300.0, 640.0, 600.0, 170.0, 3, 0.0),
             (80.0, 3800.0, 320.0, 1200.0, 420.0, 5, 0.5), (140.0, 3600.0, 300.0, 1500.0, 500.0, 5, 0.55),
             (-130.0, 3900.0, 290.0, 1300.0, 440.0, 5, 0.5), (-180.0, 3700.0, 330.0, 1500.0, 520.0, 5, 0.55)],
}
CLOUDS_FAR = {
    "layered": [(-18.0, 6500.0, 70.0, 3200.0, 110.0, 3, 0.0), (54.0, 6800.0, 190.0, 2600.0, 130.0, 3, 0.0),
                (-78.0, 6200.0, 520.0, 1900.0, 300.0, 4, 0.0), (18.0, 7000.0, 820.0, 2000.0, 230.0, 3, 0.0),
                (112.0, 6600.0, 180.0, 2800.0, 120.0, 3, 0.0), (-150.0, 6800.0, 560.0, 1900.0, 320.0, 4, 0.0),
                (170.0, 6400.0, 200.0, 3000.0, 130.0, 3, 0.0)],
    "rich": [(-18.0, 6500.0, 70.0, 3200.0, 130.0, 4, 0.0), (54.0, 6800.0, 190.0, 2600.0, 150.0, 4, 0.0),
             (-78.0, 6200.0, 520.0, 1900.0, 340.0, 5, 0.45), (18.0, 7000.0, 820.0, 2000.0, 260.0, 4, 0.4),
             (-40.0, 7200.0, 1150.0, 1400.0, 180.0, 3, 0.0), (112.0, 6600.0, 180.0, 2800.0, 140.0, 4, 0.0),
             (-150.0, 6800.0, 560.0, 1900.0, 360.0, 5, 0.45), (170.0, 6400.0, 200.0, 3000.0, 150.0, 4, 0.0)],
}
CLOUD_STYLE = "layered"     # "layered" (simplified layered silhouettes) or "rich" (a richer formation)
CLOUD_THICK = 0.55          # the pillow's depth as a share of its height


def cloud_meshes():
    """One GLB per formation, in its own frame (x across the view, y up, z away from the camera), standing across
    the line to the origin so it faces the camera. Returns [(node name, layer, position, yaw deg)]. One mesh per
    node: a whole layer in one GLB instanced at the origin (bounds 14 km round the camera) never drew single-sided
    (Pass 4.2); formations as their own nodes draw, and a world-space twist about the origin turns each of them
    round the viewer the same way."""
    out = []
    for name, layer in (("near", CLOUDS_NEAR[CLOUD_STYLE]), ("far", CLOUDS_FAR[CLOUD_STYLE])):
        for k, (az, dist, base, w, h, n, tier) in enumerate(layer):
            a = math.radians(az)
            cx, cz = dist * math.sin(a), -dist * math.cos(a)
            lobes = cloud_lobes(w, h, n, seed=k + (50 if name == "far" else 0), tier=tier)
            v, f = cloud_pillow(lobes, 0.06 * h, CLOUD_THICK * h)
            verts = [(round(x, 2), round(y, 2), round(z, 2)) for x, y, z in v]      # +z away from the camera
            vv, nrm, idx = mx.smooth_shaded(verts, f)
            node = "cloud_%s%d" % (name, k)
            mx.write_glb(os.path.join(MESH_DIR, "do-%s.glb" % node.replace("_", "-")), vv,
                         [tuple(round(c, 4) for c in q) for q in nrm], idx, node)
            # the frame's yaw: local +z (away) must point from the origin to the formation
            out.append((node, name, [round(cx, 2), float(base), round(cz, 2)], round(math.degrees(math.atan2(cx, cz)), 3)))
    return out


CLOUD_BILLOW = 14.0         # metres of slow billow (a noise of world position, drifting): formations breathe, no puff wobbles
CLOUD_DRIFT = 0.0022        # radians a second the cloud field turns round the viewer (about 65 minutes a turn)




# ================================================================================================ Pass 4: the world
# The tiny island (the owner's reset brief, 10, which supersedes 08's long strip): ONE small, low piece of land with
# ONE distinctive tree and a few rocks, close enough to be recognised at once. Everything on it is painted by the
# illustrated programs (doLand for the land, doIllus for the tree and the rocks: a flat colour, a sun-side light band,
# a shadow band, a warm rim, the haze of distance), so the palette moves it with the rest of the world.
#
# Assets: the rocks are Kenney's CC0 low-poly rocks (assets/kenney/rock_*.glb, tracked), scaled 6-10x and painted by
# doIllus (their own dirt/grass materials are overridden). The Tree of Life (assets/treeisle/, gitignored, 3.16 M
# triangles, no licence recorded) was inspected for its silhouette -- a broad spreading crown on a gnarled trunk, 77 m
# tall -- and NOT used: a derived mesh would inherit its licence and its gitignore, and this scene must render on CI
# without private assets. The tree is designed here instead: a windswept coconut palm, the tiny-island archetype.
ISLAND_C = (-128.0, -330.0)      # centre (x, z): the left third
ISLAND_W, ISLAND_D, ISLAND_H = 96.0, 52.0, 10.5  # across the view, depth, the top (m): a low plateau over a rock base
ROCK_TOP, SAND_TOP = 5.0, 5.7         # the rock band runs from the water to ROCK_TOP; a sand line to SAND_TOP; the top above
ISLAND_SCALE = 1.0               # a composition knob: scales the whole island (land, tree, rocks, surf) about ISLAND_C
ISLAND_YAW = -6.0                # degrees
PALM_H = 18.0                    # the palm's height (m)
LAND_DEBUG = False               # look development: paint the island with doIllus instead of doLand


def island_radius(theta):
    """The footprint's radius at polar angle theta (from +x toward +z): an ellipse (ISLAND_W x ISLAND_D) with three
    gentle lobes, so the outline is irregular but still one simple, readable shape."""
    a, b = ISLAND_W * 0.5, ISLAND_D * 0.5
    c, s = math.cos(theta), math.sin(theta)
    r = a * b / math.sqrt((b * c) ** 2 + (a * s) ** 2)
    return r * (1.0 + 0.1 * math.sin(2.0 * theta + 1.1) + 0.07 * math.sin(3.0 * theta + 2.3)
                + 0.04 * math.sin(5.0 * theta + 0.4))


def island_height(x, z, jitter=True):
    """The land (the owner's island direction, the coordinator's round 2): an organic, asymmetric top -- a gentle
    rise toward the palm (x > 0), a softer fall to the right, a few soft swells, no straight edge -- over an ERODED
    ROCK BASE: from the sand line down to the water the rim is a steep irregular face whose radius is jogged per
    angular sector and per height (a hash), so it reads as clustered boulders and irregular masses, not terraces.
    Below the waterline it falls to -3 m."""
    th = math.atan2(z, x)
    R = island_radius(th)
    sector = int((th + math.pi) / (2.0 * math.pi) * 28.0) % 28
    jag = 1.0 + 0.1 * (mx._hash(sector, 3, 17) - 0.5) + 0.06 * (mx._hash(sector, 5, 23) - 0.5)
    r = math.hypot(x, z) / R
    rc = r / jag                                                  # the rock face's own radius
    if rc >= 1.0:
        return -3.0 * min(1.0, (rc - 1.0) * 6.0) - 0.5
    # the top: a rise under the palm, a long soft fall to the right, soft swells
    rise = math.exp(-((x - 18.0) / 26.0) ** 2) * math.exp(-(z / 30.0) ** 2)
    swell = 0.06 * math.sin(x * 0.11 + 1.0) * math.cos(z * 0.14 + 0.4) + 0.04 * math.sin(x * 0.23 - z * 0.17)
    top = ISLAND_H * (0.62 + 0.38 * rise + swell) * (1.0 - 0.12 * max(0.0, x / 48.0))
    rt = rock_top(x, z)
    if rc < 0.5:
        return top
    if rc < 0.74:
        return top + (rt + 0.3 - top) * mx.smooth(0.5, 0.74, rc)            # the sand slope to the rock
    u = (rc - 0.74) / 0.26
    h = rt + 0.3 - (rt + 0.8) * u ** 1.3                                      # the rock face
    if jitter:                                                                # boulder masses: bumps by sector and height
        band = int(h / 1.6)
        h += 0.9 * (mx._hash(sector * 7 + band, 11, 29) - 0.5) * (1.0 - u) + 0.5 * (mx._hash(sector * 3 + band, 13, 31) - 0.5)
    return h


def rock_top(x, z):
    """Where the rock gives way to the sand: higher under the rise, lower at the ends, wobbling along the shore, so
    the rock band is a varying mass rather than one horizontal stripe."""
    rise = math.exp(-((x - 18.0) / 30.0) ** 2)
    return ROCK_TOP * (0.72 + 0.4 * rise + 0.12 * math.sin(x * 0.17 + z * 0.1 + 0.6) + 0.08 * math.sin(x * 0.31 - 1.1))


def island_meshes():
    nr, nt = 34, 56                                 # 56 facets round: chunky rock at 270 m
    verts, faces = [(0.0, island_height(0.0, 0.0), 0.0)], []
    for i in range(1, nr + 1):
        u = (i / nr) ** 0.7 * 1.2                   # to 1.2 of the radius: the skirt under the water
        for j in range(nt):
            th = 2.0 * math.pi * j / nt
            # the rock band's vertices are jittered radially and round, so its facets are irregular masses
            jr = 1.0 + (0.06 * (mx._hash(i, j, 41) - 0.5) if 0.74 <= u <= 1.0 else 0.0)
            ta = th + (0.02 * (mx._hash(i, j, 43) - 0.5) if 0.74 <= u <= 1.0 else 0.0)
            x, z = u * jr * island_radius(ta) * math.cos(ta), u * jr * island_radius(ta) * math.sin(ta)
            verts.append((round(x, 3), round(island_height(x, z), 3), round(z, 3)))
    for j in range(nt):
        faces.append((0, 1 + (j + 1) % nt, 1 + j))                 # wound so the faces look up (+y)
    for i in range(1, nr):
        a0, a1 = 1 + (i - 1) * nt, 1 + i * nt
        for j in range(nt):
            k = (j + 1) % nt
            faces += [(a0 + j, a0 + k, a1 + j), (a0 + k, a1 + k, a1 + j)]
    tris = [(verts[a], verts[b], verts[c]) for a, b, c in faces]
    mx.write_faceted(os.path.join(MESH_DIR, "do-island.glb"), tris, "island")       # flat facets: cut rock
    return len(verts)


def ellipsoid(c, r, seg=16, rings=10):
    verts, faces = [], []
    for i in range(rings + 1):
        th = math.pi * i / rings
        for j in range(seg):
            ph = 2 * math.pi * j / seg
            verts.append((c[0] + r[0] * math.sin(th) * math.cos(ph), c[1] + r[1] * math.cos(th),
                          c[2] + r[2] * math.sin(th) * math.sin(ph)))
    for i in range(rings):
        for j in range(seg):
            a, b = i * seg + j, i * seg + (j + 1) % seg
            c2, d = (i + 1) * seg + (j + 1) % seg, (i + 1) * seg + j
            faces += [(a, b, c2), (a, c2, d)]
    return verts, faces


def palm_mesh():
    """The distinctive tree: a windswept coconut palm designed as a silhouette. A slender trunk that leans and curves
    toward the sun (+x) with a slight S; a crown of nine fronds -- long tapered ribbons that rise, arc and droop,
    swept to +x by the wind, each twisted a little so some read broad and some thin; three dead fronds hanging under
    the crown; a cluster of coconuts. Trunk and crown are two meshes, so the crown alone sways. Height PALM_H."""
    H = PALM_H
    lean, S = 0.36 * H, 0.05 * H
    trunk = []
    segs, sides = 14, 7

    def axis(u):
        return (lean * u ** 1.7, H * u, S * math.sin(math.pi * u))

    rings = []
    for k in range(segs + 1):
        u = k / segs
        cx, cy, cz = axis(u)
        rr = 0.06 * H * (1.0 - 0.45 * u) * (1.0 + 0.06 * math.sin(26.0 * u))     # the trunk's faint rings
        if u < 0.08:
            rr *= 1.0 + 3.0 * (0.08 - u)                                         # the root flare
        rings.append([(cx + rr * math.cos(2 * math.pi * j / sides), cy, cz + rr * math.sin(2 * math.pi * j / sides))
                      for j in range(sides)])
    for k in range(segs):
        for j in range(sides):
            a, b = rings[k][j], rings[k][(j + 1) % sides]
            c, d = rings[k + 1][(j + 1) % sides], rings[k + 1][j]
            trunk += [(a, c, b), (a, d, c)]
    mx.write_faceted(os.path.join(MESH_DIR, "do-palm-trunk.glb"), trunk, "palmTrunk")
    top = axis(1.0)
    crown = []
    # (azimuth rad from +x, length / H, rise, droop, twist): nine fronds spread round, the longest on the sun side
    fronds = [(0.15, 0.72, 0.5, 1.5, 0.2), (0.75, 0.64, 0.65, 1.3, 0.9), (1.45, 0.56, 0.8, 1.1, 1.3),
              (2.2, 0.58, 0.7, 1.2, 0.5), (2.95, 0.66, 0.45, 1.45, 1.1), (3.7, 0.6, 0.6, 1.35, 0.3),
              (4.4, 0.54, 0.85, 1.0, 1.4), (5.1, 0.62, 0.65, 1.25, 0.7), (5.75, 0.7, 0.4, 1.55, 0.1)]
    for az, Lf, rise, droop, twist in fronds:
        L = Lf * H
        dx, dz = math.cos(az), math.sin(az)
        dx += 0.35                                       # the wind sweeps every frond toward +x
        nrm = math.hypot(dx, dz)
        dx, dz = dx / nrm, dz / nrm
        n = 7
        pts = []
        for k in range(n + 1):
            u = k / n
            y = rise * L * 0.55 * math.sin(math.pi * min(1.0, u * 0.8)) - droop * L * 0.55 * u * u
            w = 0.12 * H * math.sin(math.pi * min(1.0, u * 1.2 + 0.02)) * (1.0 - 0.25 * u) + 0.03
            tw = twist * (0.3 + 0.7 * u)                 # the ribbon turns along its length
            px, py, pz = -dz * math.cos(tw), math.sin(tw), dx * math.cos(tw)
            c = (top[0] + dx * L * u, top[1] + y, top[2] + dz * L * u)
            pts.append((c, (px * w, py * w, pz * w)))
        for k in range(n):
            (c0, w0), (c1, w1) = pts[k], pts[k + 1]
            a = tuple(c0[i] + w0[i] for i in range(3))
            b = tuple(c0[i] - w0[i] for i in range(3))
            c = tuple(c1[i] - w1[i] for i in range(3))
            d = tuple(c1[i] + w1[i] for i in range(3))
            crown += [(a, b, c), (a, c, d), (a, c, b), (a, d, c)]
    # dead fronds: three narrow ribbons hanging straight down from under the crown
    for az, Lf in ((0.9, 0.28), (2.6, 0.24), (4.6, 0.3)):
        L = Lf * H
        dx, dz = math.cos(az) * 0.3, math.sin(az) * 0.3
        a = (top[0] + dx * 0.5, top[1] - 0.02 * H, top[2] + dz * 0.5)
        b = (top[0] + dx * L, top[1] - L, top[2] + dz * L)
        w = 0.035 * H
        px, pz = -math.sin(az), math.cos(az)
        q = [(a[0] + px * w, a[1], a[2] + pz * w), (a[0] - px * w, a[1], a[2] - pz * w), (b[0], b[1], b[2])]
        crown += [tuple(q), (q[0], q[2], q[1])]
    v, f = ellipsoid((top[0] + 0.02 * H, top[1] - 0.03 * H, top[2]), (0.045 * H, 0.03 * H, 0.045 * H), 10, 5)
    crown += [(v[a], v[b], v[c]) for a, b, c in f]
    mx.write_faceted(os.path.join(MESH_DIR, "do-palm-crown.glb"), crown, "palmCrown")
    return len(trunk) + len(crown)


# the rocks (island-local x across the view, z toward the camera; Kenney rock, scale, yaw deg, sink m): a tall stack
# at the left end balancing the palm on the right, low rocks along the near shore, one standing in the water
ROCKS = [(-34.0, 12.0, "rock_largeA", 9.0, 25.0, 0.6), (-30.0, 19.0, "rock_largeE", 8.0, -40.0, 0.6),
         (-37.0, -4.0, "rock_largeE", 8.0, 70.0, 0.6), (-30.0, -14.0, "rock_largeA", 8.0, 140.0, 0.5),
         (-22.0, 23.0, "rock_smallA", 11.0, 30.0, 0.5), (8.0, 27.0, "rock_largeE", 8.0, 15.0, 0.6),
         (18.0, 26.0, "rock_smallA", 12.0, 40.0, 0.5), (32.0, 19.0, "rock_smallA", 10.0, 0.0, 0.5),
         (-12.0, 27.0, "rock_largeA", 7.0, 40.0, 0.6), (30.0, -19.0, "rock_largeE", 8.0, 200.0, 0.5)]
PALM_AT = (24.0, -3.0)
PALM_YAW = 0.0


def island_point(x, z, y=None):
    """A local island point (x across, z toward the camera) in the world: the island's yaw and scale about ISLAND_C."""
    a = math.radians(ISLAND_YAW)
    sc = ISLAND_SCALE
    wx = ISLAND_C[0] + sc * (x * math.cos(a) + z * math.sin(a))
    wz = ISLAND_C[1] + sc * (-x * math.sin(a) + z * math.cos(a))
    wy = sc * (island_height(x, z) if y is None else y)
    return [round(wx, 3), round(wy, 3), round(wz, 3)]


def island_transform():
    sc = ISLAND_SCALE
    return {"position": [ISLAND_C[0], 0.0, ISLAND_C[1]], "rotation": [0.0, ISLAND_YAW, 0.0], "scale": [sc, sc, sc]}


# ---- the boats (the owner's brief 09): a real boat designed first, simplified second, stylized third. Three
# candidates share one builder: stations along the hull give the deck half-breadth, the sheer, the chine and the keel;
# the superstructure is lofted the same way. Local +X is forward, +Y up, the waterline at y = 0.
def loft(stations, closed_bottom=True):
    """Rings of points [(x, [(y, z_half), ...]), ...] -> a closed hull surface, mirrored to port (z < 0)."""
    tris = []
    rings = []
    for x, prof in stations:
        half = [(x, y, z) for y, z in prof]                       # starboard, from the deck edge down to the keel
        ring = half + [(x, y, -z) for y, z in reversed(prof[:-1])]  # back up the port side (the keel shared)
        rings.append(ring)
    for i in range(len(rings) - 1):
        r0, r1 = rings[i], rings[i + 1]
        for j in range(len(r0) - 1):
            a, b, c, d = r0[j], r0[j + 1], r1[j + 1], r1[j]
            tris += [(a, b, c), (a, c, d)]
    return tris, rings


def cap(ring, flip=False):
    """A flat cap over a closed ring (a fan from its centroid): the deck, the transom."""
    cx = sum(p[0] for p in ring) / len(ring)
    cy = sum(p[1] for p in ring) / len(ring)
    cz = sum(p[2] for p in ring) / len(ring)
    out = []
    for j in range(len(ring)):
        a, b = ring[j], ring[(j + 1) % len(ring)]
        out.append(((cx, cy, cz), b, a) if flip else ((cx, cy, cz), a, b))
    return out


def deck_surface(stations, y_of):
    """The deck between the two sheer lines (x, half-breadth) at height y_of(x)."""
    out = []
    for (x0, h0), (x1, h1) in zip(stations, stations[1:]):
        a, b = (x0, y_of(x0), h0), (x0, y_of(x0), -h0)
        c, d = (x1, y_of(x1), -h1), (x1, y_of(x1), h1)
        out += [(a, b, c), (a, c, d)]
    return out


def box_loft(sections):
    """A superstructure: [(x, y_bottom, y_top, half_bottom, half_top), ...] -> a closed lofted solid (rounded by the
    taper only). Returns (side tris, glazing-band tris) -- the band is the upper 55 % of the sides."""
    sides, glass = [], []
    rings = []
    for x, y0, y1, h0, h1 in sections:
        ym = y0 + 0.45 * (y1 - y0)
        hm = h0 + 0.45 * (h1 - h0)
        rings.append([(x, y0, h0), (x, ym, hm), (x, y1, h1), (x, y1, -h1), (x, ym, -hm), (x, y0, -h0)])
    for i in range(len(rings) - 1):
        r0, r1 = rings[i], rings[i + 1]
        for j in range(len(r0)):
            a, b = r0[j], r0[(j + 1) % 6]
            c, d = r1[(j + 1) % 6], r1[j]
            quad = [(a, d, c), (a, c, b)]
            (glass if j in (1, 4) else sides).extend(quad)
    sides += cap(rings[0], flip=True) + cap(rings[-1])
    return sides, glass


def motor_yacht():
    """A: a 16 m sport motor yacht. Seen abeam, its identity is the profile: a stem raked well forward (the bow
    rises and points), a sheer that sweeps down from the bow to a low stern, a hull that narrows from amidships in
    plan, a dark boot band at the waterline, and a low cabin whose raked windscreen and hardtop carry one long dark
    glazing band. The parts: hull (pearl), boot (dark), deck and cabin (pale), glazing (navy)."""
    L = 16.0
    # (x from the transom, deck half-breadth, sheer height, chine half-breadth, chine height, keel depth)
    st = [(0.0, 2.1, 1.05, 1.95, 0.0, -0.55), (2.5, 2.25, 1.12, 2.0, -0.05, -0.75), (5.5, 2.3, 1.25, 1.95, -0.08, -0.9),
          (8.5, 2.2, 1.45, 1.75, -0.05, -0.95), (11.0, 1.85, 1.7, 1.35, 0.0, -0.9), (13.0, 1.35, 1.98, 0.85, 0.15, -0.7),
          (14.6, 0.75, 2.3, 0.35, 0.35, -0.4), (15.6, 0.22, 2.55, 0.08, 0.6, -0.1), (16.0, 0.02, 2.7, 0.01, 0.8, 0.2)]
    BOOT = 0.42
    stations = [(x - L * 0.5, [(sh, hb), (sh - 0.22, hb * 0.99), (BOOT, ch + (hb - ch) * 0.75), (cy, ch), (k, 0.0)])
                for x, hb, sh, ch, cy, k in st]
    tris, rings = loft(stations)
    n = len(rings[0])                      # 9 points a ring: 0 sheer .. 4 keel .. 8 sheer (port)
    hull, boot = [], []
    per = (n - 1) * 2
    for i in range(len(rings) - 1):
        for j in range(n - 1):
            q = tris[(i * (n - 1) + j) * 2:(i * (n - 1) + j) * 2 + 2]
            (boot if 2 <= j <= 5 else hull).extend(q)       # the bands below the boot line are dark
    hull += cap(rings[0], flip=True)                         # the transom
    deck = deck_surface([(x - L * 0.5, hb) for x, hb, *_ in st], lambda x: 1.0 + max(0.0, (x + 3.0)) * 0.13)
    deck += [((-L * 0.5 - 1.1, 0.3, 1.9), (-L * 0.5, 0.3, 1.9), (-L * 0.5, 0.3, -1.9)),
             ((-L * 0.5 - 1.1, 0.3, 1.9), (-L * 0.5, 0.3, -1.9), (-L * 0.5 - 1.1, 0.3, -1.9))]
    sup = [(-3.6, 1.2, 2.6, 1.95, 1.75), (-1.2, 1.25, 2.72, 1.98, 1.8), (1.4, 1.35, 2.8, 1.85, 1.65),
           (3.2, 1.5, 2.35, 1.6, 1.25), (4.8, 1.7, 1.85, 1.3, 0.9)]
    sides, glass = box_loft(sup)

    def rake(t):
        """The stem rakes forward with height: forward of 11 m, x moves ahead by up to 0.75 m per metre of height."""
        out = []
        for tri in t:
            out.append(tuple((x + 0.75 * max(0.0, y - 0.2) * mx.smooth(3.0, 7.0, x), y, z) for x, y, z in tri))
        return out
    return {"hull": rake(hull), "boot": rake(boot), "deck": rake(deck) + sides, "glaze": glass}


def speedboat():
    """B: a 10 m deep-V performance runabout -- long narrow hull, a sharp bow, a long foredeck, a low wraparound
    windshield over a cockpit aft."""
    L = 10.0
    st = [(0.0, 1.25, 0.95, 1.15, 0.0, -0.45), (2.5, 1.35, 0.95, 1.15, -0.05, -0.6), (5.5, 1.3, 1.0, 1.0, 0.0, -0.65),
          (7.8, 1.0, 1.1, 0.6, 0.15, -0.45), (9.3, 0.45, 1.2, 0.15, 0.5, 0.0), (10.0, 0.03, 1.3, 0.02, 1.0, 0.8)]
    stations = [(x - L * 0.5, [(sh, hb), (sh - 0.2, hb), (cy, ch), (k, 0.0)]) for x, hb, sh, ch, cy, k in st]
    hull, rings = loft(stations)
    hull += cap(rings[0], flip=True)
    deck = deck_surface([(x - L * 0.5, hb) for x, hb, *_ in st], lambda x: 0.95 + max(0.0, x) * 0.05)
    sides, glass = box_loft([(-0.6, 1.0, 1.55, 1.2, 1.1), (0.4, 1.0, 1.6, 1.22, 1.05), (1.2, 1.02, 1.3, 1.1, 0.7)])
    return {"hull": hull, "deck": deck + sides, "glaze": glass}


def sloop():
    """C: a 13 m modern sloop -- a long narrow hull with a plumb bow, a low coachroof, a slender 18 m mast, a high
    aspect mainsail and a fractional jib (designed fabric: each a gentle camber), trimmed for the wind from the left."""
    L = 13.0
    st = [(0.0, 1.85, 1.05, 1.6, 0.05, -0.4), (3.0, 2.0, 1.1, 1.65, 0.0, -0.55), (6.5, 1.95, 1.2, 1.4, 0.0, -0.6),
          (10.0, 1.3, 1.35, 0.7, 0.2, -0.45), (12.5, 0.35, 1.45, 0.1, 0.7, -0.1), (13.0, 0.03, 1.5, 0.02, 1.2, 0.6)]
    stations = [(x - L * 0.5, [(sh, hb), (sh - 0.2, hb), (cy, ch), (k, 0.0)]) for x, hb, sh, ch, cy, k in st]
    hull, rings = loft(stations)
    hull += cap(rings[0], flip=True)
    deck = deck_surface([(x - L * 0.5, hb) for x, hb, *_ in st], lambda x: 1.08 + max(0.0, x) * 0.03)
    sides, glass = box_loft([(-2.0, 1.1, 1.55, 1.3, 1.15), (1.2, 1.15, 1.6, 1.25, 1.05), (2.4, 1.2, 1.4, 1.0, 0.7)])
    mast = []
    for k in range(6):
        a0, a1 = 2 * math.pi * k / 6, 2 * math.pi * (k + 1) / 6
        p = lambda a, y, r: (1.0 + r * math.cos(a), y, r * math.sin(a))
        mast += [(p(a0, 1.2, 0.09), p(a1, 1.2, 0.09), p(a1, 19.0, 0.06)), (p(a0, 1.2, 0.09), p(a1, 19.0, 0.06), p(a0, 19.0, 0.06))]
    def sail(luff0, luff1, clew, belly, n=8):
        out = []
        for i in range(n):
            for j in range(n - i):
                pass
        grid = []
        for i in range(n + 1):
            u = i / n
            row = []
            for j in range(n + 1):
                v = j / n                                                   # 0 the luff .. 1 the leech
                lx = luff0[0] + (luff1[0] - luff0[0]) * u
                ly = luff0[1] + (luff1[1] - luff0[1]) * u
                w = 1.0 - u                                                 # the sail narrows to its head
                x = lx + (clew[0] - luff0[0]) * v * w
                y = ly + (clew[1] - luff0[1]) * v * w
                z = belly * math.sin(math.pi * v) * w + clew[2] * v * w
                row.append((x, y, z))
            grid.append(row)
        for i in range(n):
            for j in range(n):
                a, b, c, d = grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]
                out += [(a, b, c), (a, c, d), (a, c, b), (a, d, c)]
        return out
    sails = sail((1.0, 1.9, 0.0), (1.0, 18.6, 0.0), (-4.3, 1.9, 1.1), 0.55)
    sails += sail((6.2, 1.5, 0.0), (1.2, 15.0, 0.0), (0.4, 1.7, 1.6), 0.45)
    return {"hull": hull, "deck": deck + sides + mast, "glaze": glass, "sail": sails}


BOAT_DESIGNS = {"A": motor_yacht, "B": speedboat, "C": sloop}
BOAT_KIND = "A"
# the boat crosses the view left to right on a straight course 130 m out, at a cruising pace; the course runs well past
# both edges of the frame, so the LFO saw's wrap (back to the start) happens out of sight
BOAT_COURSE = ((-120.0, -84.0), (120.0, -64.0))
BOAT_SPEED = {"A": 3.2, "B": 9.0, "C": 2.6}
BOAT_PARTS = [("boatHull", "hull", "pearl"), ("boatBoot", "boot", "glaze"), ("boatDeck", "deck", "deck"),
              ("boatGlaze", "glaze", "glaze"), ("boatSail", "sail", "pearl")]


def boat_meshes():
    parts = BOAT_DESIGNS[BOAT_KIND]()
    for _, key, _c in BOAT_PARTS:
        if key in parts:
            mx.write_faceted(os.path.join(MESH_DIR, "do-%s-%s.glb" % (BOAT_KIND.lower(), key)), parts[key], key)
    return {k: len(v) for k, v in parts.items()}


def wake_mesh(hl):
    """A Kelvin wake in the boat's frame (+X forward, the stern at x = -hl/2): two arms diverging from the stern at
    about 19.5 deg, 46 m long, widening as they age, and two short bow-wave arms from the stem. Flat strips at the
    waterline with uv = (age along the arm 0..1, across 0..1); the foam program cuts them into broken graphic foam."""
    verts, uvs, idx = [], [], []

    def arm(x0, z0, ang, length, w0, w1, n=28):
        base = len(verts)
        dx, dz = -math.cos(ang), math.sin(ang)          # back and outward
        px, pz = dz, -dx                                 # across the arm
        for i in range(n + 1):
            u = i / n
            cx, cz = x0 + dx * length * u, z0 + dz * length * u
            w = w0 + (w1 - w0) * u
            for side in (-1.0, 1.0):
                verts.append((round(cx + px * w * side, 3), 0.06, round(cz + pz * w * side, 3)))
                uvs.append((u, 0.5 + 0.5 * side))
        for i in range(n):
            a = base + 2 * i
            idx.extend([a, a + 2, a + 3, a, a + 3, a + 1, a, a + 3, a + 2, a, a + 1, a + 3])   # both faces
    st = -hl * 0.5 + 0.3
    k = math.radians(19.5)
    arm(st, 1.6, k, 40.0, 0.5, 1.8)
    arm(st, -1.6, -k, 40.0, 0.5, 1.8)
    # the stern's own churned water: a short strip straight back
    arm(st, 0.0, 0.0, 16.0, 1.5, 2.6, 10)
    # the bow wave: short arms from the stem
    bx = hl * 0.5 - 1.2
    arm(bx, 0.6, math.radians(28.0), 7.0, 0.35, 0.9, 8)
    arm(bx, -0.6, -math.radians(28.0), 7.0, 0.35, 0.9, 8)
    return verts, uvs, idx


def write_wake(hl):
    v, uv, idx = wake_mesh(hl)
    return write_uv_glb("do-wake.glb", "wake", v, uv, idx)


def write_uv_glb(fname, label, v, uv, idx, normals=None):
    """A GLB with UVs (meshes.write_glb carries none): a minimal writer with TEXCOORD_0. Normals default to up."""
    import json as _json, struct as _struct
    n = len(v)
    pb = b"".join(_struct.pack("<3f", *p) for p in v)
    nb = b"".join(_struct.pack("<3f", *(normals[i] if normals else (0.0, 1.0, 0.0))) for i in range(n))
    tb = b"".join(_struct.pack("<2f", *t) for t in uv)
    ib = _struct.pack("<%dI" % len(idx), *idx)
    blob = pb + nb + tb + ib
    blob += b"\0" * ((4 - len(blob) % 4) % 4)
    lo = [min(p[k] for p in v) for k in range(3)]
    hi = [max(p[k] for p in v) for k in range(3)]
    o = [0, len(pb), len(pb) + len(nb), len(pb) + len(nb) + len(tb)]
    g = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": label}],
         "materials": [{"name": label, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]}}],
         "meshes": [{"name": label, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                     "indices": 3, "material": 0, "mode": 4}]}],
         "buffers": [{"byteLength": len(blob)}],
         "bufferViews": [{"buffer": 0, "byteOffset": o[0], "byteLength": len(pb), "target": 34962},
                         {"buffer": 0, "byteOffset": o[1], "byteLength": len(nb), "target": 34962},
                         {"buffer": 0, "byteOffset": o[2], "byteLength": len(tb), "target": 34962},
                         {"buffer": 0, "byteOffset": o[3], "byteLength": len(ib), "target": 34963}],
         "accessors": [{"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3", "min": lo, "max": hi},
                       {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC3"},
                       {"bufferView": 2, "componentType": 5126, "count": n, "type": "VEC2"},
                       {"bufferView": 3, "componentType": 5125, "count": len(idx), "type": "SCALAR"}]}
    js = _json.dumps(g, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    path = os.path.join(MESH_DIR, fname)
    with open(path, "wb") as f:
        f.write(_struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob)))
        f.write(_struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(_struct.pack("<II", len(blob), 0x004E4942) + blob)
    return n


def wake_program():
    """Graphic wake foam: a world-anchored noise (the foam stays where the boat left it while the arms move on), cut by
    a threshold that rises with the foam's age (the arm thins and breaks up toward its end) and with the distance from
    the arm's centre line. The result is the OPACITY of a masked material: hard, clean foam shapes, no smoke."""
    P = Prog("doWake")
    P.op(None, "input", 0, inp="worldPosition")
    P.op("wakeNoise", "noise", 0, a=0, f=WAKE_FREQ, k=[0.0, 0.0, 0.0], seed=41)
    P.op(None, "input", 1, inp="uv")
    P.op(None, "swizzle", 2, a=1, k=[0, 0, 0, 0])                                   # age along the arm (u)
    P.op(None, "swizzle", 3, a=1, k=[1, 1, 1, 1])                                   # across (v)
    P.op(None, "remap", 3, a=3, k=[0.0, 1.0, -1.0, 1.0])
    P.op(None, "multiply", 3, a=3, b=3)                                             # (2v - 1)^2: 0 centre, 1 edge
    P.op(None, "power", 2, a=2, f=0.8)
    P.op(None, "add", 2, a=2, b=3)                                                  # how hard it is to be foam
    P.op("wakeCut", "remap", 2, a=2, k=[0.0, 1.6, WAKE_CUT0, WAKE_CUT1])
    P.op(None, "constant", 5, k=[-1.0, -1.0, -1.0, -1.0])
    P.op(None, "multiply", 2, a=2, b=5)
    P.op(None, "add", 4, a=0, b=2)                                                  # noise - threshold
    P.op("wakeEdge", "smoothstep", 4, a=4, k=[0.0, 0.04])                           # opacity (cut at 0.5)
    P.pcolour("wakeFoam", 6, "foam", 1.0)
    return P


WAKE_FREQ = 0.55
WAKE_CUT0, WAKE_CUT1 = 0.42, 0.78     # Pass 4: a quieter wake under the illustrated foam


RIDERS = {}     # node -> the identities of its deformer slots (filled as nodes are made; read by wave_route)
SEA_IDS = ["s0", "s1", "s2", "s3", "s4", "s5", "tide", "lift"]       # sea_deformers() + audio_deformers()
RIDE_IDS = ["s0", "s1", "s2", "s4", "tide", "lift"]                  # ride_deformers()


def wave_route(s, src, ident, amount, field="amount", **kw):
    """One route per node that carries the deformer `ident` (a swell sine "s0".."s5", or an audio field): the sea's
    pieces, the boat's parts and its wake, so whatever moves the water moves what floats on it."""
    for node, ids in RIDERS.items():
        if ident in ids:
            s.route(R(src, "procedural/%s/deform/%d/%s" % (node, ids.index(ident) + 1, field), amount, **kw))


def ride_deformers():
    """What a floating object rides (a node takes at most 8 deformers): the long swell's full Gerstner pair, the
    vertical sines of the two shorter trains (their small horizontal drift does not matter for a hull), and the two
    audio fields -- 6 deformers, leaving room for a path and a roll."""
    d = sea_deformers()
    return [d[0], d[1], d[2], d[4]] + audio_deformers()


def boat(s):
    """The chosen boat on its course: a straight open spline across the view, `pathOffset` from an LFO saw (pure in
    time), then the sea's own world-space Gerstner stack, so it rises, falls and travels with the surface under it; a
    gentle local roll. The wake and the bow wave are emitters that follow the same saw: the wake trails behind the
    course, widening, and nothing forms a ring."""
    (x0, z0), (x1, z1) = BOAT_COURSE
    length = math.hypot(x1 - x0, z1 - z0)
    d = ((x1 - x0) / length, 0.0, (z1 - z0) / length)
    s.nodes.append(spline_node("boatCourse", [(x0, 0.0, z0), (x1, 0.0, z1)], False))
    hl = {"A": 16.0, "B": 10.0, "C": 13.0}[BOAT_KIND]
    speed = BOAT_SPEED[BOAT_KIND]
    s.sources.append({"kind": "lfo", "name": "boat", "settings": {"shape": "saw"}})
    s.param("sources/boat/rate", round(speed / (length - hl), 6))
    roll = [{"kind": "sine", "amount": BOAT_ROLL, "frequency": 0.12, "speed": 0.9, "phase": 0.0,
             "axis": [0.0, 1.0, 0.0], "displacementAxis": [0.0, 0.0, 1.0], "space": "local"},
            {"kind": "path", "spline": "boatCourse", "amount": 1.0, "axis": [1.0, 0.0, 0.0],
             "center": [0.0, 0.0, 0.0], "pathOffset": hl * 0.5 + BOAT_REVIEW_OFFSET, "pathScale": 1.0,
             "pathRoll": 0.0, "space": "local"}]
    ride = roll + ride_deformers()            # the hull rides the swell and the music's swell as the sea does
    keys = BOAT_DESIGNS[BOAT_KIND]()
    for name, key, colour in BOAT_PARTS:
        if key not in keys:
            continue
        s.proc(name, {"kind": "mesh", "asset": "meshes/do-%s-%s.glb" % (BOAT_KIND.lower(), key)},
               material=flat(colour, double=(key == "sail")), deformers=ride)
        RIDERS[name] = ["roll", "path"] + RIDE_IDS
        s.route(R("lfo.boat", "procedural/%s/deform/2/pathOffset" % name, length - hl))
    # the wake: geometry riding the same course and the same sea, its foam cut by doWake (masked, crisp)
    write_wake(hl)
    wp = wake_program()
    s.program(wp.doc(emission=6, opacity=4))
    s.proc("boatWake", {"kind": "mesh", "asset": "meshes/do-wake.glb"},
           material={"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [0.0, 0.0, 0.0], "emissiveIntensity": 0.0,
                     "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": "doWake", "alphaMode": "mask",
                     "alphaCutoff": 0.5, "doubleSided": True},
           deformers=[roll[1]] + ride_deformers())
    RIDERS["boatWake"] = ["path"] + RIDE_IDS
    s.route(R("lfo.boat", "procedural/boatWake/deform/1/pathOffset", length - hl))
    return wp


BOAT_ROLL = 0.06
BOAT_REVIEW_OFFSET = 0.0     # look development only: parks the yacht this far along its course (the silent variant has no saw)


def illus_program():
    """Land, trees and the boat, painted: the node's own colour (its emission: palettes route onto it), a broad light
    band on the faces toward the sun and a shadow band away from it (two smoothstepped regions, not a gradient), a
    warm rim where a silhouette catches the low sun, and distance haze toward the horizon. Registers: 0 N, 1 V."""
    P = Prog("doIllus")
    sx, sy, sz = S
    pn = PALETTE
    P.op("N", "input", 0, inp="normal")
    P.op("V", "input", 1, inp="viewDirection")
    P.op("base", "input", 2, inp="materialEmission")
    P.op(None, "gradient", 3, a=0, k=[0.75 * SH[0], 0.55, 0.75 * SH[2], 0.0], f=1.0)   # facing the low sun and the sky
    P.op("band", "smoothstep", 3, a=3, k=[ILLUS_BAND0, ILLUS_BAND1])
    P.op(None, "remap", 3, a=3, k=[0.0, 1.0, ILLUS_SHADOW, ILLUS_LIT])
    P.op(None, "multiply", 2, a=2, b=3)
    P.op(None, "fresnel", 4, f=3.0)                                       # the rim toward the sun
    P.op(None, "gradient", 5, a=1, k=[-sx, -sy, -sz, 0.0], f=1.0)
    P.op(None, "power", 5, a=5, f=6.0)
    P.op(None, "multiply", 4, a=4, b=5)
    P.pcolour("illusRim", 5, "glow", ILLUS_RIM)
    P.op(None, "multiply", 4, a=4, b=5)
    P.op(None, "add", 2, a=2, b=4)
    P.op(None, "input", 4, inp="cameraDistance")                         # haze: decametres
    P.op(None, "constant", 5, k=[0.1, 0.1, 0.1, 0.1])
    P.op(None, "multiply", 4, a=4, b=5)
    P.op("haze", "remap", 4, a=4, k=[ILLUS_HAZE0, ILLUS_HAZE1, 0.0, ILLUS_HAZE_MAX], f=1.0)
    P.op(None, "gradient", 5, a=1, k=[-0.5 * SH[0], 0.0, -0.5 * SH[2], 0.5], f=1.0)
    P.op(None, "power", 5, a=5, f=2.4)
    P.pcolour("hzCool", 6, "cool")
    P.pcolour("hzWarm", 7, "warm")
    P.op(None, "mixBy", 6, a=6, b=7, c=5)
    P.op("hazed", "mixBy", 2, a=2, b=6, c=4)
    return P


def land_program():
    """The island's colour bands by height -- the rock base from the water to ROCK_TOP, a sand line to SAND_TOP, the
    land on top -- written as the material's BASE COLOUR (not its emission): the island is a lit cel material
    (ADR-1071), so the scene's key light at the sun shades it (the owner's direction: sun-facing faces lit and warm,
    faces away in shadow, a rim toward the sun), in agreement with the rays and the path. The rock takes a little
    facet variation from the normal. Then the haze that ties it to the horizon. Registers: 0 N, 2 wp, 3 colour."""
    P = Prog("doLand")
    P.op("N", "input", 0, inp="normal")
    P.op("wp", "input", 2, inp="worldPosition")
    P.pcolour("rock", 3, "rock")
    P.pcolour("land2", 4, "land2")
    P.op(None, "noise", 5, a=2, f=0.14, k=[0.0, 0.0, 0.0], seed=61)                  # mottled rock: boulder masses
    P.op("mottle", "smoothstep", 5, a=5, k=[0.42, 0.5])
    P.op(None, "mixBy", 3, a=4, b=3, c=5)
    P.op(None, "noise", 6, a=2, f=0.045, k=[0.0, 0.0, 0.0], seed=67)                 # the rock line wobbles
    P.op(None, "remap", 6, a=6, k=[0.0, 1.0, 1.6, -1.6])
    P.op(None, "swizzle", 5, a=2, k=[1, 1, 1, 1])                                   # the height
    P.op(None, "add", 5, a=5, b=6)
    P.op("sandLine", "smoothstep", 6, a=5, k=[ROCK_TOP * 0.85 - 0.1, ROCK_TOP * 0.85 + 0.1])
    P.pcolour("sand", 4, "sand")
    P.op(None, "mixBy", 3, a=3, b=4, c=6)
    P.op("landTop", "smoothstep", 6, a=5, k=[SAND_TOP - 0.1, SAND_TOP + 0.1])
    P.pcolour("land", 4, "land")
    P.op(None, "mixBy", 3, a=3, b=4, c=6)
    P.op(None, "input", 4, inp="cameraDistance")
    P.op(None, "constant", 5, k=[0.1, 0.1, 0.1, 0.1])
    P.op(None, "multiply", 4, a=4, b=5)
    P.op("haze", "remap", 4, a=4, k=[ILLUS_HAZE0, ILLUS_HAZE1, 0.0, ILLUS_HAZE_MAX], f=1.0)
    P.pcolour("landHaze", 5, "cool")
    P.op("hazed", "mixBy", 3, a=3, b=5, c=4)
    return P


LAND_UNDER0, LAND_UNDER_SOFT = 0.6, 0.02
LAND_LIT0, LAND_LIT_SOFT = 0.46, 0.02
LAND_SAND0, LAND_SAND1 = 0.25, 0.75


ILLUS_BAND0, ILLUS_BAND1 = 0.32, 0.42
ILLUS_SHADOW, ILLUS_LIT = 0.62, 1.12
ILLUS_RIM = 0.9
ILLUS_HAZE0, ILLUS_HAZE1, ILLUS_HAZE_MAX = 60.0, 500.0, 0.55


def cel(key=None, double=False, rim=0.3):
    """A lit cel material (ADR-1071) under the scene's key light at the sun: two lit bands over the palette's shadow
    tone, a warm rim toward the sun. `key` None: the base colour comes from a program (the island)."""
    m = {"baseColor": pal(PALETTE, key) if key else [1.0, 1.0, 1.0], "emissiveColor": [0.0, 0.0, 0.0],
         "emissiveIntensity": 0.0, "roughness": 0.8, "metallic": 0.0,
         "toon": {"bands": 2, "softness": 0.02, "terminator": 0.05, "shadowColor": pal(PALETTE, "land2"),
                  "ambient": CEL_AMBIENT, "rimWidth": rim, "rimColor": pal(PALETTE, "warm"),
                  "rimIntensity": 1.3 if rim else 0.0, "specular": 0.0, "specularSize": 0.12}}
    if double:
        m["doubleSided"] = True
    return m


CEL_AMBIENT = 0.42


def flat(key, scale=1.0, double=False):
    m = {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": pal(PALETTE, key, scale), "emissiveIntensity": 1.0,
         "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": "doIllus"}
    if double:
        m["doubleSided"] = True
    return m


# the nodes painted by doIllus and the palette key each one's colour comes from (palette_routes routes them)
ILLUS_NODES = [("palmTrunk", "trunk"), ("palmCrown", "frond"), ("rocks", "rock"), ("boatHull", "pearl"),
               ("boatBoot", "glaze"), ("boatDeck", "deck"), ("boatGlaze", "glaze"), ("boatSail", "pearl")]
CEL_NODES = ["palmTrunk", "palmCrown", "rocks"]      # lit cel materials: the palette routes their baseColor


def world(s):
    island_meshes()
    palm_mesh()
    boat_meshes()
    ip = illus_program()
    s.program(ip.doc(emission=2))
    lp = land_program()
    s.program(lp.doc(emission=-1, base=3))
    # the island is a stable anchor: no deformer, nothing routed to its shape; lit by the key light at the sun
    s.proc("island", {"kind": "mesh", "asset": "meshes/do-island.glb"},
           material=flat("land") if LAND_DEBUG else dict(cel(None, rim=0.35), program="doLand"),
           transform=island_transform())
    # the palm: the trunk stands still; the crown sways a little (0.25 m at the fronds' tips, ~9 s), never bends
    q = math.radians(PALM_YAW) / 2.0
    at = island_point(*PALM_AT)
    at[1] -= 0.3 * ISLAND_SCALE
    sc = ISLAND_SCALE
    point = at + [0.0, round(math.sin(q), 5), 0.0, round(math.cos(q), 5), sc, sc, sc]
    sway = [{"kind": "sine", "amount": 0.25, "frequency": 0.12, "speed": 0.7, "phase": 0.0, "axis": [0.0, 1.0, 0.0],
             "displacementAxis": [0.8, 0.0, 0.6], "center": [0.0, 0.0, 0.0], "falloff": 16.0, "space": "local"}]
    s.proc("palmTrunk", {"kind": "mesh", "asset": "meshes/do-palm-trunk.glb"},
           distribution={"kind": "points", "points": [point]}, material=cel("trunk"))
    s.proc("palmCrown", {"kind": "mesh", "asset": "meshes/do-palm-crown.glb"},
           distribution={"kind": "points", "points": [point]}, material=cel("frond", double=True), deformers=sway)
    # the rocks: Kenney's low-poly rocks, scaled up and painted flat (one node per rock asset)
    by_asset = {}
    for x, z, asset, scale, yaw, sink in ROCKS:
        p = island_point(x, z)
        p[1] = max(-0.3 * sc, min(p[1], (ROCK_TOP - 0.5) * sc)) - sink * sc  # on the rock face or in the surf
        q = math.radians(yaw) / 2.0
        by_asset.setdefault(asset, []).append(p + [0.0, round(math.sin(q), 5), 0.0, round(math.cos(q), 5),
                                                 scale * sc, scale * sc, scale * sc])
    for k, (asset, pts) in enumerate(sorted(by_asset.items())):
        s.proc("rocks" if k == 0 else "rocks%d" % k, {"kind": "mesh", "asset": kit.asset("kenney/%s.glb" % asset)},
               distribution={"kind": "points", "points": pts}, material=cel("rock"))
        if k:
            ILLUS_NODES.append(("rocks%d" % k, "rock"))
            CEL_NODES.append("rocks%d" % k)
    shore(s)
    wp = boat(s)
    return ip, lp, wp


def shore_mesh(n=240):
    """Pass 3, Pass E: the island's surf line. A low vertical ribbon (SHORE_LO..SHORE_HI m) round the footprint's
    waterline (island_radius, pushed out by SHORE_OUT), facing out to sea. uv = (round the shore 0..1, up the ribbon
    0..1). At 330 m a pixel is ~0.2 m, so the ribbon is a few px: the white surf line of an anime background, broken
    by shore_program."""
    verts, uvs, nrm, idx = [], [], [], []
    for i in range(n + 1):
        q = 2.0 * math.pi * i / n
        r = island_radius(q) * SHORE_OUT
        x, z = r * math.cos(q), r * math.sin(q)
        # the outward normal: across the outline's tangent
        q1 = q + 1e-3
        r1 = island_radius(q1) * SHORE_OUT
        tx, tz = r1 * math.cos(q1) - x, r1 * math.sin(q1) - z
        L = math.hypot(tx, tz) or 1.0
        for k, y in ((0, SHORE_LO), (1, SHORE_HI)):
            verts.append((round(x, 3), y, round(z, 3)))
            uvs.append((i / n, float(k)))
            nrm.append((-tz / L, 0.0, tx / L))
    for i in range(n):
        p0 = 2 * i
        idx += [p0, p0 + 2, p0 + 1, p0 + 1, p0 + 2, p0 + 3]
    return verts, uvs, idx, nrm


def shore_program():
    """The surf line: brightest at the ribbon's middle height, broken by a large world-anchored noise (~25 m) that
    the swell carries up and down the ribbon, so the breaks drift as waves pass -- lapping, not flicker. Masked."""
    P = Prog("doShore")
    P.op(None, "input", 0, inp="uv")
    P.op(None, "swizzle", 1, a=0, k=[1, 1, 1, 1])                                   # up the ribbon
    P.op(None, "remap", 1, a=1, k=[0.0, 1.0, -1.0, 1.0])
    P.op(None, "multiply", 1, a=1, b=1)
    P.op(None, "remap", 1, a=1, k=[0.0, 1.0, 1.0, 0.0])                            # 1 at the middle, 0 at the edges
    P.op(None, "input", 2, inp="worldPosition")
    P.op("shoreNoise", "noise", 2, a=2, f=SHORE_FREQ, k=[0.0, 0.0, 0.0], seed=53)
    P.op(None, "mix", 1, a=1, b=2, f=0.5)
    P.op("shoreCut", "smoothstep", 3, a=1, k=[SHORE_CUT, SHORE_CUT + 0.05])            # opacity
    P.pcolour("shoreFoam", 4, "foam", SHORE_GAIN)
    return P


SHORE_OUT = 1.01
SHORE_LO, SHORE_HI = -0.3, 0.5
SHORE_FREQ = 0.1
SHORE_CUT = 0.56
SHORE_GAIN = 1.0


def shore(s):
    v, uv, idx, nrm = shore_mesh()
    write_uv_glb("do-shore.glb", "shore", v, uv, idx, nrm)
    sp = shore_program()
    s.program(sp.doc(emission=4, opacity=3))
    s.proc("shore", {"kind": "mesh", "asset": "meshes/do-shore.glb"},
           material={"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [0.0, 0.0, 0.0], "emissiveIntensity": 0.0,
                     "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": "doShore", "alphaMode": "mask",
                     "alphaCutoff": 0.5, "doubleSided": True},
           transform=island_transform(), deformers=ride_deformers())
    RIDERS["shore"] = list(RIDE_IDS)
    SHORE_PROGS.append(sp)


SHORE_PROGS = []




# ================================================================================================ dolphins
DOLPHIN_LEN = 8.5          # Pass 2: twice Pass 1 (3.1 m read as specks)


def dolphin_mesh():
    """A stylised bottlenose dolphin, faceted (the Alpine family's grammar), nose at +X, dorsal +Y, length
    DOLPHIN_LEN, tail at x = 0. A lofted body (elliptical rings along a profile with a melon and a beak), a swept
    dorsal fin, two pectorals and the tail flukes."""
    L = DOLPHIN_LEN
    # stations: (x / L, half height, half width, centre-line y), all in units of L
    prof = [(0.0, 0.010, 0.012, 0.0), (0.06, 0.022, 0.020, 0.004), (0.18, 0.045, 0.040, 0.010),
            (0.34, 0.075, 0.068, 0.018), (0.52, 0.095, 0.085, 0.020), (0.68, 0.092, 0.083, 0.016),
            (0.80, 0.075, 0.068, 0.010), (0.88, 0.058, 0.052, 0.004), (0.92, 0.036, 0.034, -0.012),
            (0.97, 0.020, 0.020, -0.016), (1.0, 0.004, 0.004, -0.017)]
    ring = 10
    rings = []
    for fx, hh, hw, cy in prof:
        r = []
        for k in range(ring):
            a = 2.0 * math.pi * k / ring
            r.append((fx * L, (cy + hh * math.cos(a)) * L, hw * math.sin(a) * L))
        rings.append(r)
    tris = []
    for i in range(len(rings) - 1):
        for k in range(ring):
            a, b = rings[i][k], rings[i][(k + 1) % ring]
            c, d = rings[i + 1][(k + 1) % ring], rings[i + 1][k]
            tris += [(a, c, d), (a, b, c)]
    tip = (L * 1.004, -0.017 * L, 0.0)
    for k in range(ring):
        tris.append((rings[-1][k], rings[-1][(k + 1) % ring], tip))
    tail = (-0.004 * L, 0.0, 0.0)
    for k in range(ring):
        tris.append((rings[0][(k + 1) % ring], rings[0][k], tail))

    def fin(pts, thick):
        """A flat fin as a thin wedge: the outline `pts` (a fan from pts[0]) offset +-thick along z."""
        out = []
        for i in range(1, len(pts) - 1):
            p0, p1, p2 = pts[0], pts[i], pts[i + 1]
            up = [(q[0], q[1], q[2] + thick) for q in (p0, p1, p2)]
            dn = [(q[0], q[1], q[2] - thick) for q in (p0, p1, p2)]
            out += [tuple(up), (dn[0], dn[2], dn[1])]
        return out

    # dorsal fin: swept back, rooted on the back at x ~ 0.5 L
    top = (0.115 * L)
    tris += fin([(0.58 * L, top - 0.01 * L, 0.0), (0.40 * L, top - 0.01 * L, 0.0), (0.36 * L, top + 0.11 * L, 0.0),
                 (0.46 * L, top + 0.07 * L, 0.0)], 0.012 * L)

    def flat(pts, thick):
        """A horizontal fin (y up): the outline in x/z, offset +-thick in y."""
        out = []
        for i in range(1, len(pts) - 1):
            p0, p1, p2 = pts[0], pts[i], pts[i + 1]
            up = [(q[0], q[1] + thick, q[2]) for q in (p0, p1, p2)]
            dn = [(q[0], q[1] - thick, q[2]) for q in (p0, p1, p2)]
            out += [(up[0], up[2], up[1]), tuple(dn)]
        return out

    for side in (1.0, -1.0):
        # flukes
        tris += flat([(0.03 * L, 0.0, 0.0), (0.0, 0.0, side * 0.03 * L), (-0.07 * L, 0.0, side * 0.17 * L),
                      (-0.02 * L, 0.0, side * 0.15 * L), (0.06 * L, 0.0, side * 0.02 * L)], 0.006 * L)
        # pectorals: below the sides, angled down and back
        tris += [((0.70 * L, -0.03 * L, side * 0.07 * L), (0.60 * L, -0.11 * L, side * 0.17 * L),
                  (0.64 * L, -0.04 * L, side * 0.08 * L))]
        tris += [((0.70 * L, -0.03 * L, side * 0.07 * L), (0.64 * L, -0.04 * L, side * 0.08 * L),
                  (0.60 * L, -0.11 * L, side * 0.17 * L))]
    body = 2 * (len(prof) - 1) * ring + 2 * ring
    # every body facet must face outward (its normal away from the centre line): checked, not assumed
    for t in tris[:body]:
        n = mx.cross(mx.sub(t[1], t[0]), mx.sub(t[2], t[0]))
        cx = sum(v[0] for v in t) / 3.0
        cy = sum(v[1] for v in t) / 3.0
        cz = sum(v[2] for v in t) / 3.0
        assert n[1] * cy + n[2] * cz >= -1e-9 or abs(cx - L) < 0.05 * L or cx < 0.02 * L, "inward facet"
    mx.write_faceted(os.path.join(MESH_DIR, "do-dolphin2.glb"), tris, "dolphin")
    return len(tris)


def dolphin_program():
    """A dolphin against the low sun: a dark backlit body, a paler belly, a rim of sunlight on the edges that face
    it and a wet sheen. Registers: 0 N, 1 V."""
    P = Prog("doDolphin")
    sx, sy, sz = S
    pn = PALETTE
    P.op("N", "input", 0, inp="normal")
    P.op("V", "input", 1, inp="viewDirection")
    P.op(None, "gradient", 2, a=0, k=[0.0, -1.0, 0.0, 0.2], f=0.8)                 # the belly
    P.pcolour("dback", 3, "dback")
    P.pcolour("dbelly", 4, "dbelly")
    P.op("body", "mixBy", 3, a=3, b=4, c=2)
    P.op(None, "fresnel", 2, f=2.2)                                               # the rim, toward the sun
    P.op(None, "gradient", 4, a=1, k=[-0.5 * sx, -0.5 * sy, -0.5 * sz, 0.5], f=1.0)
    P.op(None, "power", 4, a=4, f=5.0)
    P.op(None, "multiply", 2, a=2, b=4)
    P.pcolour("drim", 4, "drim", 2.2)
    P.op(None, "multiply", 2, a=2, b=4)
    P.op(None, "add", 3, a=3, b=2)
    P.op(None, "gradient", 2, a=0, k=[sx, sy, sz, 0.0], f=1.0)                     # wet sheen
    P.op(None, "power", 2, a=2, f=24.0)
    P.pcolour("dsheen", 4, "glint", 1.5)
    P.op(None, "multiply", 2, a=2, b=4)
    P.op("lit", "add", 3, a=3, b=2)
    return P


def catmull(points, closed, samples=64):
    """The engine's Catmull-Rom (spatial/spline.cpp: a Hermite segment with tangents (next - prev) * tension, tension
    0.5), sampled densely: [(x, y, z, distance)]."""
    n = len(points)
    segs = n if closed else n - 1

    def P(i):
        return points[i % n] if closed else points[max(0, min(n - 1, i))]

    out = []
    dist = 0.0
    last = None
    for sg in range(segs):
        a, b, pv, nx = P(sg), P(sg + 1), P(sg - 1), P(sg + 2)
        m0 = [(b[k] - pv[k]) * 0.5 for k in range(3)]
        m1 = [(nx[k] - a[k]) * 0.5 for k in range(3)]
        for j in range(samples):
            u = j / samples
            u2, u3 = u * u, u * u * u
            h00, h10, h01, h11 = 2 * u3 - 3 * u2 + 1, u3 - 2 * u2 + u, -2 * u3 + 3 * u2, u3 - u2
            q = tuple(a[k] * h00 + m0[k] * h10 + b[k] * h01 + m1[k] * h11 for k in range(3))
            if last is not None:
                dist += math.sqrt(sum((q[k] - last[k]) ** 2 for k in range(3)))
            out.append((q[0], q[1], q[2], dist))
            last = q
    end = P(segs) if not closed else P(0)
    dist += math.sqrt(sum((end[k] - last[k]) ** 2 for k in range(3)))
    out.append((end[0], end[1], end[2], dist))
    return out


def crossings(samples):
    """Where the curve passes through the mean sea level going up (an exit) and down (an entry): [(kind, distance,
    point)]."""
    out = []
    for p, q in zip(samples, samples[1:]):
        if (p[1] < 0.0) != (q[1] < 0.0):
            t = p[1] / (p[1] - q[1])
            pt = tuple(p[k] + (q[k] - p[k]) * t for k in range(3))
            out.append(("exit" if q[1] > p[1] else "entry", p[3] + (q[3] - p[3]) * t, pt))
    return out


def leap_points(centre, heading_deg, height, half_span, loop=True, depth=36.0, reach=60.0):
    """A leap in the vertical plane through `centre` along `heading` (degrees from +X toward -Z): out of the deep, an
    arc `height` m over the mean sea level and `2 half_span` m wide, back down; with `loop`, a long return far below
    (invisible under the opaque sea) closes it. In the plane: (along, up)."""
    h = math.radians(heading_deg)
    T = (math.cos(h), 0.0, -math.sin(h))
    w = half_span
    # an approach just under the surface (the dorsal fin cuts it), a steep rise, the arc, a dive (Pass 2's gesture)
    prof = [(-4.6 * w, -7.0), (-3.4 * w, -1.5), (-2.2 * w, -1.35), (-1.25 * w, -2.6), (-0.55 * w, 0.75 * height),
            (0.0, height), (0.55 * w, 0.75 * height), (1.25 * w, -2.6), (2.4 * w, -7.0)]
    if loop:
        prof += [(2.4 * w + 0.4 * reach, -0.45 * depth), (1.2 * w + 0.5 * reach, -depth),
                 (-1.2 * w - 0.5 * reach, -depth), (-2.4 * w - 0.4 * reach, -0.45 * depth)]
    return [(round(centre[0] + T[0] * a, 3), round(b, 3), round(centre[2] + T[2] * a, 3)) for a, b in prof]


# The pods: (name, centre (x, z), heading deg, height, half span, period s, lag s per member, member offsets (along
# the view's depth, height scale)). Pod A crosses the sun's path at 70 m left to right; pod B, smaller and far, swims
# right to left near the island side.
PODS = [
    ("podA", (40.0, -100.0), -4.0, 5.5, 11.0, 53.0, [(0.0, 0.0, 1.0), (0.55, -3.5, 0.85), (1.25, 2.8, 0.92)]),
    ("podB", (-20.0, -240.0), 176.0, 3.4, 9.0, 79.0, [(0.0, 0.0, 1.0), (0.7, 3.0, 0.8)]),
]
LEAP_SPEED = 12.0            # metres a second along the path (a dolphin's leap)
HERO = ("hero", (22.0, -55.0), -2.0, 8.0, 14.0)
HERO_ATTACK_MS = None        # computed from the hero path's length


def spline_node(name, points, closed):
    return {"name": name, "kind": "spline", "spline": {
        "name": name, "kind": "catmullRom", "closed": bool(closed), "tension": 0.5, "generator": "points",
        "points": [{"position": list(p), "roll": 0.0, "scale": 1.0} for p in points], "samplesPerSegment": 48,
        "up": [0.0, 1.0, 0.0]}}


def dolphin_node(s, name, spline):
    """One dolphin: the tail's kick (a local sine growing from the nose to the flukes), then bent along `spline` by the
    path deformer, whose `pathOffset` (deform/2) is where along the path its tail is."""
    s.proc(name, {"kind": "mesh", "asset": "meshes/do-dolphin2.glb"}, material=unlit("doDolphin"), deformers=[
        {"kind": "sine", "amount": 0.07, "frequency": round(2.0 * math.pi / (1.7 * DOLPHIN_LEN), 5), "speed": 7.5,
         "phase": 0.0, "axis": [1.0, 0.0, 0.0], "displacementAxis": [0.0, 1.0, 0.0], "center": [DOLPHIN_LEN, 0.0, 0.0],
         "falloff": DOLPHIN_LEN, "space": "local"},
        {"kind": "path", "spline": spline, "amount": 1.0, "axis": [1.0, 0.0, 0.0], "center": [0.0, 0.0, 0.0],
         "pathOffset": 0.0, "pathScale": 1.0, "pathRoll": 0.0, "space": "local"}])


def splash(s, name, point, capacity=320):
    """Water thrown up where a dolphin breaks the surface, and the rings its drops leave (`spawnRate` is routed open for
    the moment the body passes through)."""
    s.particles(name, capacity=capacity, seed=zlib_seed(name), shape="sphere",
                position=[round(point[0], 3), 0.25, round(point[2], 3)], extent=[0.45, 0.45, 0.45],
                direction=[0.0, 1.0, 0.0], spawnRate=0.0, lifetimeMin=0.5, lifetimeMax=1.1, spread=0.34,
                speedMin=3.0, speedMax=7.5, gravity=[0.0, -9.8, 0.0], drag=0.3, sizeStart=0.15, sizeEnd=0.04,
                colorStart=pal(PALETTE, "foam") + [0.95], colorEnd=pal(PALETTE, "hl") + [0.0], emissive=2.0,
                blend="additive", turbulence=0.0, collision="splash", collisionHeight=0.0, splashLifetime=1.6,
                splashSize=14.0, ringThickness=0.09)


def zlib_seed(name):
    import zlib
    return zlib.crc32(name.encode()) % 100000


def window_routes(source, target, lo, hi, amount, period_wrap=True):
    """`amount` on `target` while `source` (0..1) is in [lo, hi): two binary steps, +amount at lo and -amount at hi.
    Wrapped into 0..1 (a window across the wrap is split)."""
    lo, hi = lo % 1.0, hi % 1.0
    if hi < lo:          # across the wrap: [lo, 1) and [0, hi)
        return (window_routes(source, target, lo, 0.99999, amount) + window_routes(source, target, 0.0, hi, amount))
    out = []
    if lo > 0.0:
        out.append(R(source, target, amount, threshold="binary", thresholdLevel=round(lo, 5)))
    else:
        out.append(R(source, target, amount, gain=0.0, offset=1.0))
    out.append(R(source, target, -amount, threshold="binary", thresholdLevel=round(hi, 5)))
    return out


def solve_loop(centre, heading, height, span, length):
    """The return depth/reach that makes a closed leap loop `length` metres long (bisection on the reach)."""
    lo, hi = 10.0, 900.0
    for _ in range(40):
        mid = 0.5 * (lo + hi)
        pts = leap_points(centre, heading, height, span, True, depth=0.35 * mid + 20.0, reach=mid)
        L = catmull(pts, True)[-1][3]
        if L < length:
            lo = mid
        else:
            hi = mid
    return pts


def dolphins(s):
    """The pods (ambient: an LFO saw per pod, pure in time, seamless at its wrap because the loop is closed), and the
    hero leap (an envelope fired by a pad: its linear attack carries the hero along an open path, its instant release
    sends it back to the start, deep under the opaque sea, unseen)."""
    dolphin_mesh()
    dp = dolphin_program()
    s.program(dp.doc(emission=3))
    for pod, (cx, cz), heading, height, span, period, members in PODS:
        s.sources.append({"kind": "lfo", "name": pod, "settings": {"shape": "saw"}})
        s.param("sources/%s/rate" % pod, round(1.0 / period, 6))
        for m, (lag, depth_off, hscale) in enumerate(members):
            h = math.radians(heading)
            nrm = (math.sin(h), 0.0, math.cos(h))                  # across the leap plane, toward the camera
            c = (cx + nrm[0] * depth_off, 0.0, cz + nrm[2] * depth_off)
            pts = solve_loop(c, heading, height * hscale, span * (0.85 + 0.15 * hscale), period * LEAP_SPEED)
            curve = catmull(pts, True)
            L = curve[-1][3]
            name = "%s_%d" % (pod, m)
            s.nodes.append(spline_node(name + "_path", pts, True))
            dolphin_node(s, name, name + "_path")
            off = -lag / period
            s.route(R("lfo." + pod, "procedural/%s/deform/2/pathOffset" % name, L, offset=round(off, 5)))
            for kind, dc, pt in crossings(curve):
                sp = "%s_%s" % (name, kind)
                splash(s, sp, pt)
                # the nose reaches the surface at tail offset dc - L_d, the tail leaves it at dc
                lo = (dc - DOLPHIN_LEN) / L - off
                hi = dc / L - off
                s.route(*window_routes("lfo." + pod, "particles/%s/spawnRate" % sp, lo, hi + 0.15 / period, 520.0))
    # the hero
    name, (cx, cz), heading, height, span = HERO
    pts = leap_points((cx, 0.0, cz), heading, height, span, loop=False)
    curve = catmull(pts, False)
    L = curve[-1][3]
    s.nodes.append(spline_node("hero_path", pts, False))
    dolphin_node(s, "hero", "hero_path")
    attack = (L - DOLPHIN_LEN) / LEAP_SPEED * 1000.0
    s.sources.append({"kind": "envelope", "name": "leap", "settings": {"trigger": LEAP_TRIGGER}})
    s.params_({"sources/leap/attackMs": round(attack, 1), "sources/leap/decayMs": 0.0, "sources/leap/sustain": 1.0,
               "sources/leap/holdMs": 250.0, "sources/leap/releaseMs": 1.0})
    span_d = L - DOLPHIN_LEN
    s.route(R("env.leap", "procedural/hero/deform/2/pathOffset", span_d))
    for kind, dc, pt in crossings(curve):
        sp = "hero_%s" % kind
        splash(s, sp, pt, capacity=600)
        lo, hi = (dc - DOLPHIN_LEN) / span_d, dc / span_d + 0.2 * LEAP_SPEED / span_d
        s.route(*window_routes("env.leap", "particles/%s/spawnRate" % sp, max(lo, 0.001), min(hi, 0.999), 800.0))
    return dp


LEAP_TRIGGER = "control.padLeap"
HERO_WAVE_TRIGGER = "control.padWave"


# ================================================================================================ the instrument
def travel_axis(direction_deg):
    a = math.radians(direction_deg)
    return [round(-math.sin(a), 6), 0.0, round(math.cos(a), 6)]


TIDE_ORIGIN = 400.0         # the music enters the sea this far out (m), under the sun
TIDE_SPEED = 26.0           # and rolls in at this speed (m/s): 15 s from there to the viewer (the history holds 16.4)
TIDE_SMOOTH = 0.13          # seconds of history averaged (ADR-1370): 13 rows, ~3.5 m of sea, a true low-pass
TIDE_AMOUNT = 1.5           # metres of swell at a full band
KICK_SPEED = 22.0
KICK_ORIGIN = 70.0
KICK_SOURCE = "low"
KICK_LIFT = 0.55


def audio_fields(s):
    """TIDEWRITING: the bass's own history, written into the sea. A `spectrum` field (the low band) heard with a delay
    that grows with the distance from a line 400 m out, times a planar sine carrier (a 46 m swell of its own): what the
    bass did fifteen seconds ago is the swell at your feet now. KICK FRONTS: an `onset` field (low-band onsets) rolls a
    lifted band in from the same line on every kick. Both are applied by field deformers (deform/7 and deform/8) on every
    piece of the sea, so the seams stay closed."""
    ax = travel_axis(-6.0)
    # A planar wave's distance is dot(q, axis) in the FIELD'S OWN FRAME -- `point` is ignored for planar geometry
    # (spatial/field.cpp waveDistance) -- so the line the music enters at is the field node's position.
    origin = [round(-ax[0] * TIDE_ORIGIN, 3), 0.0, round(-ax[2] * TIDE_ORIGIN, 3)]
    kick_origin = [round(-ax[0] * KICK_ORIGIN, 3), 0.0, round(-ax[2] * KICK_ORIGIN, 3)]
    common = {"waveGeometry": "planar", "axis": ax, "falloff": {"kind": "none"}}
    s.nodes.append({"name": "tideBass", "kind": "field", "position": origin, "field": dict(
        common, kind="spectrum", audioBand="range", bandLow=0.0, bandHigh=0.08, audioSpeed=TIDE_SPEED,
        audioSmooth=TIDE_SMOOTH, strength=1.0)})
    s.nodes.append({"name": "tideCarrier", "kind": "field", "position": origin, "field": dict(
        common, kind="wave", waveShape="sine", amplitude=1.0, wavelength=46.0, waveSpeed=7.5, waveWidth=0.0,
        strength=1.0)})
    s.nodes.append({"name": "tide", "kind": "field", "position": [0, 0, 0], "field": {
        "kind": "compound", "combine": "multiply", "children": ["tideBass", "tideCarrier"], "strength": 1.0}})
    # (the onset history keeps the newest 8 onsets: with a busy kick they are all a few seconds old, so the fronts
    # start 70 m out, close enough to be seen arriving)
    s.nodes.append({"name": "kickFront", "kind": "field", "position": kick_origin, "field": dict(
        common, kind="onset", onsetSource=KICK_SOURCE, audioSpeed=KICK_SPEED, onsetWidth=11.0, onsetDecay=0.35,
        strength=1.0)})
    # THE HERO WAVE (a pad): one pulse front that leaves a line 240 m out at the pad's event and rolls in at 13 m/s,
    # on its own clock (ADR-906: a triggered field is silent before its first event and keeps no other state, so a
    # second hit restarts it far out). Added to the kick fronts in one compound: one deformer slot for both.
    wave_origin = [round(-ax[0] * HERO_WAVE_FROM, 3), 0.0, round(-ax[2] * HERO_WAVE_FROM, 3)]
    s.nodes.append({"name": "heroWave", "kind": "field", "position": wave_origin, "field": dict(
        common, kind="wave", waveShape="pulse", amplitude=1.0, wavelength=30.0, waveSpeed=HERO_WAVE_SPEED,
        waveWidth=HERO_WAVE_WIDTH, waveOrigin=0.0, strength=round(HERO_WAVE_HEIGHT / KICK_LIFT, 4),
        trigger={"source": "signal", "name": HERO_WAVE_TRIGGER, "threshold": 0.3})})
    s.nodes.append({"name": "lift", "kind": "field", "position": [0, 0, 0], "field": {
        "kind": "compound", "combine": "add", "children": ["kickFront", "heroWave"], "strength": 1.0}})


def audio_deformers():
    return [{"kind": "field", "field": "tide", "amount": TIDE_AMOUNT, "axis": [0.0, 1.0, 0.0], "space": "world",
             "alongNormal": False},
            {"kind": "field", "field": "lift", "amount": KICK_LIFT, "axis": [0.0, 1.0, 0.0], "space": "world",
             "alongNormal": False}]


def spray(s):
    """Spray thrown off the near swell by the snare (a burst per hit, a fixed pool)."""
    s.particles("snareSpray", capacity=600, seed=17, shape="box", position=[8.5, 1.3, -38.0], extent=[7.0, 0.3, 2.5],
                direction=[0.1, 1.0, 0.45], spawnRate=0.0, lifetimeMin=0.5, lifetimeMax=1.0, spread=0.22,
                speedMin=2.5, speedMax=5.0, gravity=[0.0, -9.8, 0.0], drag=0.4, sizeStart=0.11, sizeEnd=0.03,
                colorStart=pal(PALETTE, "foam") + [0.9], colorEnd=pal(PALETTE, "sss") + [0.0], emissive=1.4,
                blend="additive", turbulence=0.4, turbulenceScale=0.3)
    # sparks on the sun's path, one burst per MIDI note: low notes near, high notes far
    s.particles("noteSparks", capacity=900, seed=23, shape="sphere", position=[8.0, 0.8, -40.0],
                extent=[1.4, 0.3, 1.4], direction=[0.0, 1.0, 0.0], spawnRate=0.0, lifetimeMin=0.6, lifetimeMax=1.4,
                spread=0.6, speedMin=0.6, speedMax=2.2, gravity=[0.0, -1.5, 0.0], drag=0.8, sizeStart=0.22,
                sizeEnd=0.0, colorStart=pal(PALETTE, "glint") + [1.0], colorEnd=pal(PALETTE, "glow") + [0.0],
                emissive=4.0, blend="additive", turbulence=0.0)


def atmosphere(s, sky):
    """Pass D: the rays drift (two LFO sines on two axes of the ray noise's offset: pure in time, never a spin, never in
    step), and the boat throws a bow wave and leaves a wake on the side the swell comes from."""
    M = "material/%s/op/%d/noise/constant" % (sky.name, sky["rayNoise"])
    for k, (hz, amp) in enumerate(RAY_DRIFT):
        name = "rays%d" % k
        s.sources.append({"kind": "lfo", "name": name, "settings": {"shape": "sine"}})
        s.param("sources/%s/rate" % name, hz)
        s.route(R("lfo.%s.bipolar" % name, M, amp, comp=(0 if k == 0 else 2), polarity="bipolar"))




SEA_NODES = ["sea_near", "sea_mid", "sea_far"]


def musical_colour(s, progs, sea, seafar, sky, cp):
    """Pass E: colour as the primary instrument. ONE axis moves the whole world through three designed palettes --
    violet sunset (0), golden afternoon (0.5), electric blue (1) -- piecewise, each segment a full coordinated palette
    (`palette_routes`), smoothed over 1.5 s: a lighting change, not a colour cycle. Its position, `visual.mood`, is the
    performer's palette control (CC 1) plus a slow share of the piece's 12 s intensity, so a build brightens the world
    toward day and electric blue and a breakdown sinks it back to the sunset. On top, each band has its own colour role
    (smoothed signals, different time constants, never all at once)."""
    s.sources.append({"kind": "envelope", "name": "day", "settings": {"trigger": "control.padDay"}})
    s.params_({"sources/day/attackMs": 3000.0, "sources/day/decayMs": 0.0, "sources/day/sustain": 1.0,
               "sources/day/holdMs": 5000.0, "sources/day/releaseMs": 4000.0})
    s.map(M("mood", [("control.palette", 1.0), ("intensity", MOOD_INTENSITY), ("env.day", 0.75)], "sum"))
    seg = dict(attackMs=1500, decayMs=1500, clampEnabled=True, clampMin=0.0, clampMax=1.0)
    palette_routes(s, progs, "visual.mood", "afternoon", dict(seg, gain=2.0), frm="violet")
    palette_routes(s, progs, "visual.mood", "electric", dict(seg, gain=2.0, offset=-1.0), frm="afternoon")
    M_ = "material/%s/op/%d/constant/constant"
    # BASS deepens the water's body and lifts the light through the crests (weight)
    for prog in (sea, seafar):
        s.route(R("bass", M_ % (prog.name, prog["deep"]), 1.0, op="multiply", gain=-0.35, offset=1.0, attackMs=200,
                  decayMs=1200),
                R("bass", M_ % (prog.name, prog["sss"]), 1.0, op="multiply", gain=0.15, offset=1.0, attackMs=200,
                  decayMs=1200))
    # HIGHS add mint to the crest light (delicate, fast)
    mint = hexrgb("#a8ffe0")
    for prog in (sea, seafar):
        for c in range(3):
            s.route(R("audio.treble", M_ % (prog.name, prog["sss"]), round(mint[c] * 0.15, 4), comp=c, attackMs=60,
                      decayMs=600))
    # SUSTAIN shifts the sky's middle toward pink and warms the clouds' lit undersides (slow)
    pink = hexrgb("#ff8ac8")
    for c in range(3):
        s.route(R("sustain", M_ % (sky.name, sky["mid"]), round(pink[c] * 0.15, 4), comp=c, attackMs=2500,
                  decayMs=5000))
    s.route(R("sustain", M_ % (cp.name, cp["clit"]), 1.0, op="multiply", gain=0.3, offset=1.0, attackMs=2500,
              decayMs=5000))
    # LEVEL: the rays strengthen with loudness; KICK: they flash warm and brief (each its own decay)
    s.route(R("level", M_ % (sky.name, sky["ray"]), 1.0, op="multiply", gain=0.35, offset=1.0, attackMs=800,
              decayMs=2500),
            R("kickEnv", M_ % (sky.name, sky["ray"]), 1.0, op="multiply", gain=0.6, offset=1.0, attackMs=0,
              decayMs=350))
    # the rays reach further on strong onsets (their fade angle opens), briefly
    s.route(R("onsetEnv", "material/doSky/op/%d/remap/constant" % sky["rayLen"], -0.04, comp=0, attackMs=0,
              decayMs=900))


MOOD_INTENSITY = 0.12       # Pass 4: the sunset holds under most music (0.35 carried every still halfway to the afternoon; the palette routes run at gain 2, so this is 24 % of the way at full intensity)


def instrument(s, sea, seafar, sky):
    """The modulation map. Every audio response has its own scale and decay; nothing pulses the whole frame."""
    M = "material/%s/op/%d/constant/constant"
    deco = sea_deformers()
    # (every swell route goes to the sea AND to what rides it: wave_route)
    # LEVEL: the long swell grows with the music's loudness (scene energy), slowly
    wave_route(s, "level", "s0", 0.5, attackMs=1800, decayMs=4500)
    # MIDS: the wind sea roughens
    wave_route(s, "audio.mid", "s4", 0.07, attackMs=300, decayMs=1500)
    # INTENSITY: STRUCTURE -- the crests sharpen as the piece builds (the horizontal Gerstner amounts grow)
    # (+0.1 on the long swell only: the steepnesses' sum stays under 1 at full intensity, 0.85 -> 0.95)
    wave_route(s, "intensity", "s1", deco[1]["amount"] * (0.10 / STEEP[0]), attackMs=4000, decayMs=8000)
    # KICK: the crest light and the foam flash for a moment (the kick's lifted front rolls in on its own)
    for prog in (sea, seafar):
        s.route(R("kickEnv", M % (prog.name, prog["sss"]), 1.0, op="multiply", gain=0.2, offset=1.0, attackMs=0,
                  decayMs=420))
    if "foam" in sea.at:
        s.route(R("kickEnv", M % (sea.name, sea["foam"]), 1.0, op="multiply", gain=0.4, offset=1.0, attackMs=0,
                  decayMs=600))
    # the crest span follows the sea's height (a taller sea needs a taller crest to foam): foam stays a cap. The
    # gradient's y component only: its w is the centring offset (Pass 4)
    s.route(R("level", "material/doSea/op/%d/gradient/constant" % sea["crest"], -0.3 / W_HEIGHT, comp=1,
              attackMs=1800, decayMs=4500))
    if "foamMask" in sea.at:
        s.route(R("level", "material/doSea/op/%d/smoothstep/constant" % sea["foamMask"], 0.05, comp=0, attackMs=1800,
                  decayMs=4500),
                R("level", "material/doSea/op/%d/smoothstep/constant" % sea["foamMask"], 0.05, comp=1, attackMs=1800,
                  decayMs=4500))
    # SNARE: spray off the near swell
    s.route(R("snare", "particles/snareSpray/burst", 70.0, threshold="binary", thresholdLevel=0.25))
    # HIGHS: the sun's glints flare and the micro-ripples deepen (more, smaller glints); hats sparkle the path
    for prog in (sea, seafar):
        if "glint" in prog.at:
            s.route(R("hatEnv", M % (prog.name, prog["glint"]), 1.0, op="multiply", gain=0.9, offset=1.0, attackMs=0,
                      decayMs=220),
                    R("audio.treble", M % (prog.name, prog["glint"]), 1.0, op="multiply", gain=0.8, offset=1.0,
                      attackMs=60, decayMs=500))
    # (the highs once deepened the micro-ripples too: at 60-200 m that noise aliased into crumpled foil -- removed)
    # SUSTAIN: held sound swells the sun's halo and warms the sun's path
    s.route(R("sustain", M % (sky.name, sky["halo"]), 1.0, op="multiply", gain=0.4, offset=1.0, attackMs=1500,
              decayMs=3500),
            R("sustain", M % (sky.name, sky["glow"]), 1.0, op="multiply", gain=0.35, offset=1.0, attackMs=1500,
              decayMs=3500))
    for prog in (sea, seafar):
        if "pathCol" in prog.at:
            s.route(R("sustain", M % (prog.name, prog["pathCol"]), 1.0, op="multiply", gain=0.8, offset=1.0,
                      attackMs=1500, decayMs=3500))
    # BRIGHTNESS: the palette drifts a little warmer or cooler with the music's centroid, very slowly
    s.route(R("brightnessSlow", "post/grade/hueShift", 0.03, offset=-0.5, attackMs=4000, decayMs=8000))
    # NOTES: a spark burst on the sun's path, its distance by pitch (low near, high far), its size by velocity
    s.route(R("noteOn", "particles/noteSparks/burst", 90.0, depth="lastVelocity"),
            R("lastPitch", "particles/noteSparks/position", -150.0, comp=2, offset=-0.15),
            R("lastPitch", "particles/noteSparks/position", 150.0 * 0.21, comp=0, offset=-0.15))


# ================================================================================================ MIDI: the performer's map
# Any device, any channel. CCs are bus signals (control.<name>, 0..1) shaped by the routes; pads are events.
MIDI_CC = [
    (1, "palette", "PALETTE: the world's colour, violet sunset -> golden afternoon -> electric blue (smoothly)"),
    (2, "night", "NIGHT: dusk falls into a bioluminescent night (the crests and foam glow)"),
    (3, "waves", "WAVES: the swell's height (the long swell and the cross swell rise)"),
    (4, "energy", "WAVE ENERGY: chop, foam and how deep the music writes into the sea, together"),
    (5, "rays", "SUN RAYS: the rays' strength and reach, the halo and the sun's path on the water"),
    (6, "clouds", "CLOUDS: the glow on the clouds' lit undersides and rims"),
    (7, "water", "WATER COLOUR: the richness of the water's body and crest light"),
    (8, "spray", "SPRAY: how much spray and sparkle the drums and notes throw"),
    (9, "speed", "WAVE SPEED: the waves' pace, from the authored to 2.5x, without a crest ever jumping"),
    (10, "direction", "WAVE DIRECTION: the sea swings from the cross swell (from the right) to the wind sea (left)"),
    (11, "bands", "BAND CONTRAST: the water's colour regions tighten from soft painted bands to crisp cel shading"),
    (12, "highlights", "HIGHLIGHTS: the crest strokes brighten and widen"),
    (13, "foam", "FOAM: more and brighter foam caps on the crests"),
    (14, "detail", "SURFACE DETAIL: streaks across the bands, from a clean surface to a busy one"),
]
MIDI_PADS = [
    (36, "padLeap", "LEAP: a dolphin breaks the surface and arcs across the sun's path"),
    (37, "padWave", "HERO WAVE: one big swell rises far out and rolls in to your feet, foaming"),
    (38, "padGlow", "BLOOM: a burst of bioluminescence along every crest, fading over seconds"),
    (39, "padFlare", "FLARE: the sun flares; its halo and path blaze and settle"),
    (40, "padSpray", "SPRAY: a gust of spray off the near swell"),
    (41, "padDay", "DAYBREAK: the world sweeps toward electric day over 3 s, holds, and sinks back to the sunset"),
]


def control_map():
    b = []
    for num, name, _ in MIDI_CC:
        b.append({"source": "*", "channel": -1, "kind": "cc", "number": num, "signal": name})
    for num, name, _ in MIDI_PADS:
        b.append({"source": "*", "channel": -1, "kind": "noteEvent", "number": num, "signal": name})
    return {"midi": {"enabled": True, "filter": "*", "bindings": b}}


def palette_routes(s, progs, signal, to, chain, frm=None):
    """A palette change on one control: for every palette colour every program paints with, add (to - frm) per
    channel at full control (frm defaults to the scene's palette). Every illustrated node's flat colour too."""
    frm = frm or PALETTE
    for prog in progs:
        for label, key, scale in getattr(prog, "pal", []):
            a, b = pal(frm, key, scale), pal(to, key, scale)
            for c in range(3):
                d = round(b[c] - a[c], 5)
                if abs(d) > 1e-4:
                    s.route(R(signal, "material/%s/op/%d/constant/constant" % (prog.name, prog[label]), d, comp=c,
                              **chain))
    present = {n["name"] for n in s.nodes}
    for node, key in ILLUS_NODES:
        if node not in present:
            continue
        a, b = pal(frm, key), pal(to, key)
        field = "baseColor" if node in CEL_NODES else "emissiveColor"
        for c in range(3):
            if abs(b[c] - a[c]) > 1e-4:
                s.route(R(signal, "procedural/%s/material/%s" % (node, field), round(b[c] - a[c], 5), comp=c,
                          **chain))
        if node in CEL_NODES:
            a2, b2 = pal(frm, "land2"), pal(to, "land2")
            for c in range(3):
                if abs(b2[c] - a2[c]) > 1e-4:
                    s.route(R(signal, "procedural/%s/material/toon/shadowColor" % node, round(b2[c] - a2[c], 5),
                              comp=c, **chain))


def midi(s, progs, sea, seafar, sky, cp=None):
    s.control = control_map()
    M = "material/%s/op/%d/constant/constant"
    CC = {"attackMs": 120, "decayMs": 120}
    # NIGHT: the palette's dark end (a time of day). PALETTE (CC 1) is part of visual.mood (musical_colour).
    palette_routes(s, progs, "control.night", "midnight", dict(attackMs=250, decayMs=250))
    wave_route(s, "control.waves", "s0", 1.1, **CC)
    wave_route(s, "control.waves", "s2", 0.5, **CC)
    wave_route(s, "control.energy", "s4", 0.16, **CC)
    wave_route(s, "control.energy", "tide", 1.0, op="multiply", gain=1.4, offset=1.0, **CC)
    if WATER == "anime":
        water_controls(s, sea, seafar)
    if "glint" in sea.at:
        s.route(R("control.energy", M % (sea.name, sea["glint"]), 1.0, op="multiply", gain=0.8, offset=1.0, **CC))
    # a taller sea needs a taller crest to foam: the crest span follows the waves control (the y component only)
    s.route(R("control.waves", "material/doSea/op/%d/gradient/constant" % sea["crest"], -0.42 / W_HEIGHT, comp=1,
              **CC))
    # foam is a crest cap whatever the sea's height: a taller sea (waves) raises the cut as much as energy lowers it,
    # so energy makes the caps livelier and brighter, never a slab (Pass 3, Pass C)
    if "foamMask" in sea.at:
        FM = "material/doSea/op/%d/smoothstep/constant" % sea["foamMask"]
        s.route(R("control.energy", FM, -0.02, comp=0, **CC), R("control.energy", FM, -0.02, comp=1, **CC),
                R("control.waves", FM, 0.07, comp=0, **CC), R("control.waves", FM, 0.07, comp=1, **CC),
                R("control.energy", M % (sea.name, sea["foam"]), 1.0, op="multiply", gain=0.6, offset=1.0, **CC))
    for prog, key, g in ((sky, "ray", 1.0), (sky, "halo", 0.9), (sky, "glow", 0.5), (sea, "pathCol", 0.9),
                         (seafar, "pathCol", 0.9), (sea, "glint", 0.6), (seafar, "glint", 0.6)):
        if key in prog.at:
            s.route(R("control.rays", M % (prog.name, prog[key]), 1.0, op="multiply", gain=g, offset=1.0, **CC))
    s.route(R("control.rays", "material/doSky/op/%d/remap/constant" % sky["rayLen"], -0.08, comp=0, **CC))
    if cp is not None:
        for key, g in (("clit", 0.6), ("crim", 1.2)):
            s.route(R("control.clouds", M % (cp.name, cp[key]), 1.0, op="multiply", gain=g, offset=1.0, **CC))
    for prog in (sea, seafar):
        s.route(R("control.water", M % (prog.name, prog["sss"]), 1.0, op="multiply", gain=0.8, offset=1.0, **CC),
                R("control.water", M % (prog.name, prog["deep"]), 1.0, op="multiply", gain=0.6, offset=1.0, **CC))
    s.route(R("control.spray", "particles/snareSpray/burst", 1.0, op="multiply", gain=2.5, offset=1.0, **CC),
            R("control.spray", "particles/noteSparks/burst", 1.0, op="multiply", gain=2.5, offset=1.0, **CC))
    # PADS: complete gestures with their own envelopes
    # (padLeap fires the hero's envelope source; padWave starts the hero wave's own clock -- both in the scene)
    glow = {"envelope": "peakhold", "envelopeHoldMs": 700, "envelopeFallPerSecond": 0.3}
    em = pal("midnight", "sss")
    for prog in (sea, seafar):
        for c in range(3):
            s.route(R("control.padGlow", M % (prog.name, prog["sss"]), round(em[c] * 6.0, 4), comp=c, **glow))
    for c in range(3):
        if "foam" in sea.at:
            s.route(R("control.padGlow", M % (sea.name, sea["foam"]), round(pal("midnight", "foam")[c] * 1.5, 4),
                      comp=c, **glow))
    flare = {"envelope": "peakhold", "envelopeHoldMs": 250, "envelopeFallPerSecond": 0.45}
    for prog, key, g in ((sky, "halo", 1.6), (sky, "glow", 0.7), (sea, "pathCol", 1.2), (seafar, "pathCol", 1.2),
                         (sea, "glint", 0.6)):
        if key in prog.at:
            s.route(R("control.padFlare", M % (prog.name, prog[key]), 1.0, op="multiply", gain=g, offset=1.0, **flare))
    s.route(R("control.padSpray", "particles/snareSpray/burst", 420.0))


def water_controls(s, sea, seafar):
    """Pass 3, Pass D: the water's own performer controls, each also played by the music (no new analysis: the
    response signals the scene already reads).

      SPEED (CC 9)      every swell train's phase gets an INTEGRATED route (ADR-1041 + ADR-1372): the rate is the
                        control (0 = the authored pace, 1 = 2.5x) plus a little of the music's level, times the
                        train's own angular speed, so the waves quicken and slow without a crest ever jumping
      DIRECTION (CC 10) the sea's dominant direction swings from the cross swell (from the right) to the wind sea
                        (from the left): their amplitudes cross-fade, so nothing sweeps far crests sideways
      BANDS (CC 11)     band contrast: the edges between the colour regions tighten toward cel shading
      HIGHLIGHTS (CC 12) the crest strokes brighten and widen
      FOAM (CC 13)      more and brighter foam caps
      DETAIL (CC 14)    the surface detail's share of the light (streaks across the bands)"""
    CC = {"attackMs": 120, "decayMs": 120}
    slow = {"attackMs": 800, "decayMs": 800}
    deco = sea_deformers()
    # SPEED: d(phase)/dt = w (rate - 1) on top of the authored speed w (pure in time, so seeks stay exact at rate 1)
    for k, d in enumerate(deco):
        w = d["speed"]
        wave_route(s, "control.speed", "s%d" % k, round(w * (SPEED_MAX - 1.0), 6), field="phase", integrate=True,
                   **slow)
        wave_route(s, "level", "s%d" % k, round(w * SPEED_LEVEL, 6), field="phase", integrate=True, attackMs=2500,
                   decayMs=5000)
    # DIRECTION: the cross swell (s2, s3) gives way to the wind sea (s4, s5), vertical and horizontal together
    for ident, g in (("s2", -0.75), ("s3", -0.75), ("s4", 1.6), ("s5", 1.3)):
        wave_route(s, "control.direction", ident, 1.0, op="multiply", gain=g, offset=1.0, **slow)
    M = "material/%s/op/%d/%s/constant"
    for prog in (sea, seafar):
        # BANDS: each edge's half-width shrinks (to 15 % at full); the music's intensity tightens them a little
        for band in ("band1", "band2"):
            for src, amt in (("control.bands", 0.85 * W_SOFT), ("intensity", 0.3 * W_SOFT)):
                s.route(R(src, M % (prog.name, prog[band], "smoothstep"), amt, comp=0, **CC),
                        R(src, M % (prog.name, prog[band], "smoothstep"), -amt, comp=1, **CC))
        # DETAIL: the noise's share of t (0.07 -> 0.16); t stays centred because the noise's mean is the middle
        s.route(R("control.detail", "material/%s/op/%d/mix/value" % (prog.name, prog["detail"]), 0.09, **CC))
    # HIGHLIGHTS: brighter (x1.8) and wider (the stroke band opens downward); hats flash them
    C = "material/%s/op/%d/constant/constant"
    hero = {"envelope": "peakhold", "envelopeHoldMs": 12000, "envelopeFallPerSecond": 0.15}
    if "hl" in sea.at:
        s.route(R("control.highlights", C % (sea.name, sea["hl"]), 1.0, op="multiply", gain=0.5, offset=1.0, **CC),
                R("control.highlights", M % (sea.name, sea["strokeLo"], "smoothstep"), -0.03, comp=0, **CC),
                R("control.highlights", M % (sea.name, sea["strokeLo"], "smoothstep"), -0.03, comp=1, **CC),
                R("hatEnv", C % (sea.name, sea["hl"]), 1.0, op="multiply", gain=0.5, offset=1.0, attackMs=0,
                  decayMs=250),
                # THE HERO WAVE (pad 37, ~18 s): its strokes brighten
                R("control.padWave", C % (sea.name, sea["hl"]), 1.0, op="multiply", gain=0.4, offset=1.0, **hero))
    # FOAM: the cut drops (more caps) and the foam brightens; the hero wave's crest -- the tallest thing on the
    # water -- carries a white line while it rolls in
    if "foamMask" in sea.at:
        FM = M % (sea.name, sea["foamMask"], "smoothstep")
        s.route(R("control.foam", FM, -0.03, comp=0, **CC), R("control.foam", FM, -0.03, comp=1, **CC),
                R("control.foam", C % (sea.name, sea["foam"]), 1.0, op="multiply", gain=0.2, offset=1.0, **CC),
                R("control.padWave", FM, -0.025, comp=0, **hero), R("control.padWave", FM, -0.025, comp=1, **hero))
    # loud music raises the swell; the lit band narrows to keep the near wave from paling over (Pass F)
    for prog in (sea, seafar):
        for c in (0, 1):
            s.route(R("level", M % (prog.name, prog["band2"], "smoothstep"), 0.06, comp=c, attackMs=1800,
                      decayMs=4500))


SPEED_MAX = 2.5         # the speed control's full rate (x the authored pace)
SPEED_LEVEL = 0.35      # how much the music's level quickens the waves (at full level: +35 %)


def score_midi(s, score):
    """Review only: every control signal named in `score` is replaced, wherever it is read (routes, the envelope's
    trigger, the hero wave's trigger), by a timeline source -- event mode for a pad ([t, ...]), a curve for a CC
    ([(t, value), ...]) -- so an offline render plays the MIDI exactly as scored."""
    pads = {name for _, name, _ in MIDI_PADS}
    for name, keys in score.items():
        if name in pads:
            settings = {"mode": "event", "keys": [{"time": float(t), "value": 1.0} for t in keys]}
        else:
            settings = {"keys": [{"time": float(t), "value": float(v), "interp": "smooth"} for t, v in keys]}
        s.sources.append({"kind": "timeline", "name": name, "settings": settings})
        old, new = "control." + name, "timeline." + name
        for r in s.routes:
            if r["source"] == old:
                r["source"] = new
        for mp in s.mappings + s.mappings2 + s.mappings3:
            for inp in mp["inputs"]:
                if inp["signal"] == old:
                    inp["signal"] = new
        for src in s.sources:
            if src.get("settings", {}).get("trigger") == old:
                src["settings"]["trigger"] = new
        for n in s.nodes:
            trig = n.get("field", {}).get("trigger")
            if trig and trig.get("name") == old:
                trig["name"] = new


HERO_WAVE_FROM = 240.0      # metres out the hero wave rises
HERO_WAVE_SPEED = 13.0      # m/s: about 18 s to your feet
HERO_WAVE_HEIGHT = 2.3      # metres (2.6 saturated Pass 2's crest light into a slab as it arrived)
HERO_WAVE_WIDTH = 6.0       # metres: narrower than Pass 2's 10, so its faces turn and the anime water draws a face


SUN_RADIUS_DEG = 1.7
RAYS = True
RAY_FREQ = 1.9         # how many rays round the sun (the noise's frequency on the unit circle): fewer, broader
RAY_LO, RAY_HI = 0.42, 0.8
RAY_REACH = 58.0       # degrees from the sun at which a ray has faded
RAY_FALL = 2.8
RAY_GAIN = 0.5
RAY_DRIFT = ((0.0137, 1.6), (0.0089, 2.3))   # (Hz, amplitude) of the two slow sines that move the rays
SUN_COS = math.cos(math.radians(SUN_RADIUS_DEG))


# ================================================================================================ build
def unlit(program):
    return {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [0.0, 0.0, 0.0], "emissiveIntensity": 0.0,
            "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": program}


def apply_composition():
    """The composition's island placement (look development sets COMPOSITION, then builds)."""
    global ISLAND_C, ISLAND_SCALE
    if COMPOSITION in COMP_ISLAND:
        ISLAND_C, ISLAND_SCALE = COMP_ISLAND[COMPOSITION]


def camera(s):
    """The composed view (C2), and a breath so the frame is never frozen: the eye rises and settles 0.35 m over 26 s,
    the aim drifts 0.7 deg across over 71 s. Two components on two periods (never in step); every track a seamless
    loop (loopLength = span, first key = last key) because a live transport is endless (ADR-1342)."""
    h, focal, pitch, yaw = COMPOSITIONS[COMPOSITION]
    p, y = math.radians(pitch), math.radians(yaw)
    pos = [0.0, h, 0.0]
    fwd = (math.sin(y) * math.cos(p), math.sin(p), -math.cos(y) * math.cos(p))
    tgt = [round(pos[k] + 100.0 * fwd[k], 4) for k in range(3)]
    s.camera = {"mode": 1, "position": pos, "target": tgt, "fov": 40.0, "orbitSpeed": 0.0}
    s.params_({"camera/lens/focalLength": focal})
    if CAMERA_BREATH:
        rise, Tr = 0.35, 26.0
        s.track("camera/position", [{"time": 0.0, "value": h, "interp": "smooth"},
                                    {"time": Tr * 0.5, "value": round(h + rise, 4), "interp": "smooth"},
                                    {"time": Tr, "value": h, "interp": "smooth"}], loop=Tr, comp=1)
        pan, Tp = 1.2, 71.0
        k = [{"time": 0.0, "value": tgt[0], "interp": "smooth"},
             {"time": Tp * 0.25, "value": round(tgt[0] + pan, 4), "interp": "smooth"},
             {"time": Tp * 0.75, "value": round(tgt[0] - pan, 4), "interp": "smooth"},
             {"time": Tp, "value": tgt[0], "interp": "smooth"}]
        s.track("camera/target", k, loop=Tp, comp=0)


def build():
    SHORE_PROGS.clear()
    RIDERS.clear()
    apply_composition()
    generate_meshes()
    s = kit.Scene(ID, TITLE, DESIGN)
    # the scene's key light is the sun (the island's lit cel materials take their light from it; every other material
    # is unlit and ignores it). No shadow: the only lit things are one island, one palm and ten rocks
    s.light("sun", "directional", direction=[round(-c, 6) for c in S], color=pal(PALETTE, "glow"), intensity=5.0,
            castsShadow=False, contactShadow=False)
    s.environment = {
        "intensity": 0.0, "background": pal(PALETTE, "warm"), "fogColor": pal(PALETTE, "warm"), "volumeDensity": 0.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": pal(PALETTE, "zenith"), "horizonColor": pal(PALETTE, "warm"),
                "groundColor": pal(PALETTE, "rhor"), "haze": 0.3, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    if WATER == "anime":
        sea, seafar, sky = water_program(), water_program(far=True), sky_program()
    else:
        sea, seafar, sky = sea_program(), sea_program(far=True), sky_program()
    audio_fields(s)
    if DEBUG_WATER:
        for prog in (sea, seafar):
            if DEBUG_WATER[0] in prog.at:
                prog.ops = prog.ops[:prog[DEBUG_WATER[0]]]
                prog.emit = DEBUG_WATER[1]
    s.program(sea.doc(emission=getattr(sea, "emit", 5)))
    s.program(seafar.doc(emission=getattr(seafar, "emit", 5)))
    if DEBUG_SKY:
        sky.ops = sky.ops[:DEBUG_SKY[0]]
        s.program(sky.doc(emission=DEBUG_SKY[1]))
    else:
        s.program(sky.doc(emission=5))
    s.proc("dome", {"kind": "mesh", "asset": "meshes/do-dome.glb"}, material=dict(unlit("doSky"), doubleSided=True))
    for name, *_ in SEA_PIECES:
        s.proc("sea_" + name, {"kind": "mesh", "asset": "meshes/do-sea-%s.glb" % name},
               material=unlit("doSea" if name == "near" else "doSeaFar"), deformers=sea_deformers() + audio_deformers())
        RIDERS["sea_" + name] = list(SEA_IDS)
    # the clouds: one node of puffs, turned slowly round the viewer by a world-space twist (seamless, and running with
    # no transport)
    cp = cloud_program()
    if DEBUG_CLOUD:
        cp.ops = cp.ops[:DEBUG_CLOUD[0]]
    s.program(cp.doc(emission=DEBUG_CLOUD[1] if DEBUG_CLOUD else 3))
    for node, layer, pos, yaw in cloud_meshes():
        drift = CLOUD_DRIFT if layer == "near" else CLOUD_DRIFT * 0.55
        deformers = [{"kind": "twist", "amount": 0.0, "speed": drift, "phase": 0.0, "axis": [0.0, 1.0, 0.0],
                      "center": [0.0, 0.0, 0.0], "space": "world"},
                     # slow billowing: the formations swell and settle (a noise of world position, drifting)
                     {"kind": "noise", "amount": CLOUD_BILLOW, "scale": 0.0025, "speed": 0.03, "seed": 9,
                      "space": "world"}]
        s.proc(node, {"kind": "mesh", "asset": "meshes/do-%s.glb" % node.replace("_", "-")},
               material=(flat("cshade", double=True) if DEBUG_CLOUD_FLAT else
                         dict(unlit("doCloud"), doubleSided=CLOUD_DOUBLE)),
               deformers=deformers[:CLOUD_DEFORMERS] or None,
               transform={"position": pos, "rotation": [0.0, yaw, 0.0], "scale": [1.0, 1.0, 1.0]})
    ip, lp, wp = world(s)
    dp = dolphins(s)
    spray(s)
    atmosphere(s, sky)
    instrument(s, sea, seafar, sky)
    musical_colour(s, [sea, seafar, sky, cp, dp, ip, lp, wp] + SHORE_PROGS, sea, seafar, sky, cp)
    midi(s, [sea, seafar, sky, cp, dp, ip, lp, wp] + SHORE_PROGS, sea, seafar, sky, cp)
    if REVIEW_MIDI:
        score_midi(s, REVIEW_MIDI)
    camera(s)
    s.params_({"post/bloom/intensity": 0.22, "post/bloom/threshold": 1.4, "post/bloom/emissionWeight": 1.0,
               "post/tonemap/operator": 4, "post/output/vignette": 0.25, "post/output/grain": 0.008})
    return s

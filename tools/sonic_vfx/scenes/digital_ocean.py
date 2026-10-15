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
    "thesis": "The Tidewriter, illustrated: an anime-background ocean world at sunset -- a low island of cut-paper "
              "palms, a sport motor yacht cruising past with its wake, dolphins leaping across the sun's path, "
              "painted cumulus banks and drifting sun rays. Colour is the instrument: the music moves the whole world "
              "between violet sunset, golden afternoon and electric blue, while the bass writes its history into "
              "the swell.",
    "composition": {
        "background": "a violet-to-coral sky, painted cumulus banks in two drifting layers, sun rays, far island strips",
        "midground": "a low island strip with five cut-paper trees and a broken surf line on the left third; the yacht crossing in front",
        "foreground": "anime water: soft colour bands drawn by the swell's height and faces, thin crest strokes, foam "
                      "caps on the reinforced crests, the sun's path broken into horizontal sparks",
        "focal": "the low sun with its rays on the right third and its column of glints",
        "secondary": ["the motor yacht and its V-wake", "dolphin leaps", "the island's silhouette"],
        "atmosphere": "distance haze on land and clouds; the far sea flattens into colour and glitter",
        "post": "a little bloom on the sun and the glints",
        "camera": "still: 14 m over the water, 32 mm, the horizon at ~44 % from the top",
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
        "extremely_fast": ["glints on the sun's path", "spray"],
    },
    "vocabulary": [
        ["intensity (12 s)", "response.intensity -> visual.mood", "PALETTE: a build moves the world toward golden "
         "afternoon and electric blue; a breakdown sinks it back to the violet sunset; the water's bands tighten"],
        ["bass", "tide (spectrum field) + response.bass", "TIDEWRITING: the bass's last ~15 s roll in as swell; the "
         "water's body deepens and the crest light lifts"],
        ["kick", "kickFront (onset field) + response.kickEnv", "a lifted front rolls in; crest light, foam and the "
         "sun rays flash"],
        ["level", "response.level", "the swell grows and quickens (an integrated phase: no crest jumps), the rays "
         "strengthen (slowly)"],
        ["onsets", "response.onsetEnv", "the rays reach further for a moment"],
        ["mids", "audio.mid", "the wind sea roughens"],
        ["snare", "response.snare", "spray off the near swell"],
        ["hats / highs", "response.hatEnv, audio.treble", "glints and crest strokes flare; mint enters the lit faces"],
        ["sustain", "response.sustain", "the sky's middle turns pink, the clouds' lit undersides warm, the halo "
         "swells"],
        ["brightness", "sonic.brightness.slow", "a slight warm or cool drift of the whole grade"],
        ["MIDI notes", "notes.noteOn, lastPitch, lastVelocity", "sparks on the sun's path, by pitch and velocity"],
        ["CC 1", "control.palette", "PALETTE: violet sunset -> golden afternoon -> electric blue"],
        ["CC 2", "control.night", "NIGHT: a bioluminescent night"],
        ["CC 3", "control.waves", "WAVES: the swell's height"],
        ["CC 4", "control.energy", "WAVE ENERGY: chop, foam and the tide's depth"],
        ["CC 5", "control.rays", "SUN RAYS: strength, reach, halo and the sun's path"],
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
    "tier": "light: an anime-water sea (three meshes, eight deformers each, a 47-op program near and 39 far), seven "
            "unlit programs, an island strip, five trees, a "
            "yacht and its masked wake, ~320 cloud puffs, fixed particle pools; no lights, no shadows, no fog march",
}

INDEX_ENTRY = {
    "category": "Sonic Abstract",
    "description": "Play live audio (a mix with a clear low end) plus a MIDI controller: CC 1 palette (violet sunset "
                   "-> golden afternoon -> electric blue), CC 2 night, CC 3 waves, CC 4 wave energy, CC 5 sun rays, "
                   "CC 6 clouds, CC 7 water colour, CC 8 spray, CC 9 wave speed, CC 10 wave direction, CC 11 band "
                   "contrast, CC 12 highlights, CC 13 foam, CC 14 surface detail; pads 36 dolphin leap, 37 hero wave, 38 "
                   "bioluminescent bloom, 39 sun flare, 40 spray, 41 daybreak. An illustrated ocean world whose colour "
                   "and swell follow the music. See docs/prototypes/digital-ocean/07-report-anime-water.md.",
    "live": True,
    "name": "Sonic Abstract - Digital Ocean",
    "project": "sonic-abstract/digital-ocean.json",
}


# ------------------------------------------------------------------------------------------------ look-development knobs
# Module-level so a look-development script can set them before build() (tools stay pure functions of these).
COMPOSITION = "E3"


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
}


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
    (-6.0, 72.0, 1.15, STEEP[0], 0.0),
    (31.0, 37.0, 0.42, STEEP[1], 1.3),
    (-38.0, 15.5, 0.16, STEEP[2], 2.1),
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
    "violet": dict(zenith="#24185e", mid="#7b45a0", warm="#ffa27c", cool="#c070b0", sun="#fff0d8", glow="#ff7f66",
                   deep="#1c1656", rzen="#2c2f86", rhor="#d58cc0", sss="#7d4fc0", glint="#ffe2cc", foam="#ffd8f0",
                   cshade="#5a3486", clit="#ff9c86", crim="#ffe0c0", isle="#6a4a96",
                   dback="#1d1448", dbelly="#7a58a8", drim="#ffc8a8",
                   land="#4e3488", land2="#2c1d5c", rock="#8a4e8e", sand="#f0a6a0", leaf="#3a2a78", trunk="#3a2266",
                   hull="#fff2ea", hull2="#e2506a", sail="#ffb35a", sail2="#ffe7c0", ray="#ffd2b0", land3="#c27aa8", pearl="#f4ece8", glaze="#1c1a4a", deck="#d9cdd8", hshade="#a99ac8", wmain="#33287e", wcrest="#d47ad6", hl="#ffd6ee"),
    # golden afternoon: turquoise and teal water, cream foam, coral sun, soft blue sky, warm green island
    "afternoon": dict(zenith="#2b68cc", mid="#7fb4ec", warm="#ffe0ac", cool="#bfe2f4", sun="#fffbe8", glow="#ffc884",
                      deep="#0a3c84", rzen="#2c78bc", rhor="#a8dcea", sss="#1fb4c8", glint="#fffbe8", foam="#fff4dc",
                      cshade="#90a8d8", clit="#fff5e6", crim="#fffbf0", isle="#7e9cc4",
                      dback="#284a7c", dbelly="#d0e2f0", drim="#fff4dc",
                      land="#45a85c", land2="#1f6e62", rock="#c88a5c", sand="#f6e2b4", leaf="#2f9a52", trunk="#8a5a3c",
                      hull="#fffbf0", hull2="#e84a3a", sail="#ff6a48", sail2="#fff6e6", ray="#d8c090", land3="#a8d86a", pearl="#fbf8ef", glaze="#14304e", deck="#e4dccc", hshade="#a8bccc", wmain="#1270b4", wcrest="#86ecdc", hl="#f6fff2"),
    # electric blue: ultramarine and cyan water, mint surf, saturated blue sky, yellow-coral accents, navy land
    "electric": dict(zenith="#0a2a9c", mid="#2c6ae4", warm="#8ff0ff", cool="#4a8cf0", sun="#fff7a8", glow="#ffd84a",
                     deep="#05126a", rzen="#0e40b6", rhor="#62d4ff", sss="#1c84f0", glint="#ffffe0", foam="#c8fff0",
                     cshade="#2448b0", clit="#c4ecff", crim="#fff6c0", isle="#2244a0",
                     dback="#0a1a56", dbelly="#4a80d8", drim="#fff7b0",
                     land="#14266a", land2="#0a1446", rock="#2a4aa0", sand="#4a76d0", leaf="#0e1c5a", trunk="#0c1650",
                     hull="#ffd84a", hull2="#ff5a4a", sail="#ff6a52", sail2="#fff7c8", ray="#8cc8f0", land3="#4a6ad8", pearl="#eef4ff", glaze="#081a52", deck="#cfd9ee", hshade="#7f9ad8", wmain="#0c38c8", wcrest="#62eaff", hl="#eafff8"),
    # the night end (CC 1): bioluminescent
    "midnight": dict(zenith="#03050f", mid="#17123c", warm="#3b2b6a", cool="#1d2150", sun="#e9f0ff", glow="#5d72c8",
                     deep="#02060f", rzen="#050a1c", rhor="#2a285c", sss="#10f0a8", glint="#cfe0ff", foam="#7affe0",
                     cshade="#0a0a22", clit="#33346e", crim="#a8b6ff", isle="#0a0a26",
                     dback="#020410", dbelly="#141a3a", drim="#9fb4ff",
                     land="#0a0c26", land2="#05061a", rock="#14183a", sand="#1a2650", leaf="#060a20", trunk="#05081a",
                     hull="#c8d4ff", hull2="#3a4a9a", sail="#6a7ad8", sail2="#a8b6ff", ray="#a8b8ff", land3="#1a2050", pearl="#7c88b8", glaze="#02040e", deck="#4a5488", hshade="#2a3260", wmain="#06123a", wcrest="#14c09a", hl="#8affe0"),
    # Pass 1's electric twilight, kept for before/after comparisons
    "twilight": dict(zenith="#100d36", mid="#563a8a", warm="#ff9b7d", cool="#8a6cc2", sun="#ffe2cc", glow="#ff7f68",
                     deep="#0a0b35", rzen="#191c58", rhor="#8a6cbd", sss="#18cfe0", glint="#ffd6c2", foam="#bdfde6",
                     cshade="#33245f", clit="#ff8a78", crim="#ffd3b4", isle="#3f2a68",
                     dback="#140f33", dbelly="#4a3a78", drim="#ffc3a8",
                     land="#3f2a68", land2="#24184a", rock="#5a3a7a", sand="#c08aa0", leaf="#2a1f52", trunk="#2a1f4a",
                     hull="#f5e6e8", hull2="#e2506a", sail="#ffb35a", sail2="#ffe7c0", ray="#ffc9a0", land3="#8a5a9a", pearl="#efe4ec", glaze="#141238", deck="#d6c8d2", hshade="#8a7aa8", wmain="#22266e", wcrest="#18cfe0", hl="#ffe0f0"),
}
PALETTE = "violet"
DEBUG_SKY = None
BANDS = 3
DEBUG_CLOUD = None
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

    def doc(self, emission, opacity=-1):
        return {"name": self.name, "ops": self.ops, "baseColor": -1, "metallic": -1, "roughness": -1,
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
    """THE ANIME WATER (Pass 3; the owner's brief 06). A material program on the displaced sea mesh, unlit.

    Every look is driven by the wave geometry, never by posterizing a colour:
      t       the illustration's light, centred on 0: the displaced HEIGHT (crests +, troughs -, over `crest`'s
              span) mixed with the face's orientation (`face`), and a little surface detail (a noise of the REST
              position, which the swell's orbits carry, so it rides the water instead of sliding over it)
      AA      the derivative-based part: the engine's per-pixel `footprint` (world metres per pixel, from screen
              derivatives) becomes a contrast gain g, 1 near and falling to G_MIN as a pixel covers more water; t is
              multiplied by it before any edge, so no edge is sharper than the pixels can draw, and toward the horizon
              every band converges on the middle tone: the detail simplifies by itself
      bands   four colour regions on t g -- deep (troughs), main, lit faces (`sss`), crest accent -- each a
              smoothstep edge whose half-width `bandSoft` is the band contrast
      strokes thin highlight lines in a narrow band of t just under the crest: they ride the crests because t is
              the crest
      foam    the detail noise mixed with t g and cut (`foamMask`): broken caps on the reinforced crests only
      glints  the reflected ray against the sun (r.l / 2 = (n.v)(n.l) - v.l / 2), jittered by the detail noise and
              cut into sparks: the broken sun path
      horizon a Fresnel blend into the reflected horizon colour, the same in both programs, so the near and far
              pieces meet without a seam
    The far program (beyond 200 m) is the same less the strokes and the foam.
    Registers: 0 N, 1 V, 2 g, 3 t, 4/5 scratch, 6 detail noise, 7 colour."""
    P = Prog("doSeaFar" if far else "doSea")
    sx, sy, sz = S
    P.op("N", "input", 0, inp="normal")
    P.op("V", "input", 1, inp="viewDirection")
    P.op("foot", "input", 2, inp="footprint")
    P.op("aaGain", "remap", 2, a=2, k=[0.0, W_AA_FP, 1.0, W_AA_MIN], f=1.0)             # g, clamped
    P.op("wp", "input", 3, inp="worldPosition")
    P.op("crest", "gradient", 3, a=3, k=[0.0, 1.0 / W_HEIGHT, 0.0, 0.0], f=1.0)        # height / span, centred
    P.op("face", "gradient", 4, a=0, k=[-W_FACE * SH[0], W_FLAT, -W_FACE * SH[2], -W_FLAT], f=1.0)
    P.op("faceMix", "mix", 3, a=3, b=4, f=W_FACE_MIX)
    P.op(None, "input", 6, inp="localPosition")
    P.op("stretch", "constant", 4, k=[W_STREAK, 1.0, 1.0, 1.0])                          # wind streaks along x
    P.op(None, "multiply", 6, a=6, b=4)
    P.op("detailNoise", "noise", 6, a=6, f=W_DETAIL_FREQ, k=[0.0, 0.0, 0.0], seed=19)
    P.op("detail", "mix", 3, a=3, b=6, f=W_DETAIL)                                       # t (1 - D) + noise D
    P.op("contrast", "multiply", 3, a=3, b=2)                                            # t g
    dz = 0.5 * W_DETAIL                                                                  # the noise's mean
    P.pcolour("deep", 7, "deep")
    for lab, e, key in (("band1", W_E1, "wmain"), ("band2", W_E2, "sss"), ("band3", W_E3, "wcrest")):
        P.op(lab, "smoothstep", 4, a=3, k=[e + dz - W_SOFT, e + dz + W_SOFT])
        P.pcolour(key, 5, key)
        P.op(None, "mixBy", 7, a=7, b=5, c=4)
    if not far:
        P.op("strokeLo", "smoothstep", 4, a=3, k=[W_S0 + dz, W_S0 + dz + W_SW])
        P.op("strokeHi", "smoothstep", 5, a=3, k=[W_S1 + dz + W_SW, W_S1 + dz])           # descending
        P.op(None, "multiply", 4, a=4, b=5)
        P.pcolour("hl", 5, "hl", W_HL)
        P.op("stroked", "mixBy", 7, a=7, b=5, c=4)
    # foam (near): mix(noise, t g) times g -- the foam fades before it can break into flickering fragments.
    # The glints' jitter: near, the raw noise; far, the noise centred and times g, so the sparks merge into one
    # smooth path toward the horizon instead of flickering.
    if not far:
        P.op("foamMix", "mix", 4, a=6, b=3, f=W_FOAM_CREST)
        P.op("foamAA", "multiply", 4, a=4, b=2)
        P.op("foamMask", "smoothstep", 4, a=4, k=[W_FOAM_CUT, W_FOAM_CUT + W_FOAM_SOFT])
        P.pcolour("foam", 5, "foam", W_FOAM_GAIN)
        P.op("foamed", "mixBy", 7, a=7, b=5, c=4)
        # the raw noise (a crest bias here grew the path into a slab under a tall sea); even a perfect reflection
        # sparks only where the noise is above W_GLINT_SPARSE, so a big face turned to the sun stays broken
        j, mean = W_GLINT_JIT, W_GLINT_SPARSE
    else:
        P.op("noiseCentre", "remap", 6, a=6, k=[0.5, 1.5, 0.0, 1.0])
        P.op("noiseAA", "multiply", 6, a=6, b=2)
        j, mean = W_GLINT_JIT, 0.0
    # glints: the reflected ray against the sun, jittered and cut into sparks
    P.op(None, "fresnel", 4, f=1.0)
    P.op("nv", "remap", 4, a=4, k=[0.0, 1.0, 1.0, 0.0])
    P.op(None, "gradient", 5, a=0, k=[sx, sy, sz, 0.0], f=1.0)                           # n.l
    P.op(None, "multiply", 5, a=5, b=4)
    P.op(None, "gradient", 4, a=1, k=[-0.5 * sx, -0.5 * sy, -0.5 * sz, 0.0], f=1.0)      # -v.l / 2
    P.op("rl", "add", 5, a=5, b=4)
    P.op("jitter", "mix", 5, a=5, b=6, f=j)
    c0 = GLINT_COS0 / 2.0 * (1.0 - j) + j * mean
    P.op("glints", "smoothstep", 5, a=5, k=[c0, c0 + W_GLINT_EDGE])                      # edge: ~2 px near
    P.pcolour("glint", 4, "glint", GLINT_GAIN)
    P.op("sunlit", "mixBy", 7, a=7, b=4, c=5)
    P.op("fres", "fresnel", 4, f=W_FRES_POWER)
    P.pcolour("rhor", 5, "rhor", W_HORIZON)
    P.op("horizon", "mixBy", 7, a=7, b=5, c=4)
    P.emit = 7
    return P


# the anime water's look (each a parameter of the op it sits in, so routes and MIDI reach them)
W_HEIGHT = 2.4          # metres of height over which t runs from trough to crest (the `crest` op)
W_FACE, W_FLAT = 0.9, 0.25   # weight of the face's orientation / of flatness in the `face` term
W_FACE_MIX = 0.35       # how much of t is orientation (the rest is height)
W_DETAIL_FREQ = 0.5     # the surface detail's scale (1/m) across the view (z); along x it is W_STREAK of it
W_STREAK = 0.3          # the detail is stretched along x (across the view): wind streaks, not blotches
W_DETAIL = 0.07         # its share of t
W_AA_FP, W_AA_MIN = 0.6, 0.35   # g falls linearly from 1 (sharp pixels) to W_AA_MIN at a footprint of W_AA_FP m
W_SOFT = 0.07           # band edge half-width in centred t (the band contrast: smaller is crisper)
W_E1, W_E2, W_E3 = -0.16, 0.05, 0.22   # the band thresholds in centred t
W_S0, W_S1, W_SW = 0.14, 0.2, 0.02     # the stroke band (centred t) and its edge width
W_HL = 1.1
W_FOAM_CREST = 0.5      # foam = mix(noise, t g, this): how much the crest decides
W_FOAM_CUT, W_FOAM_SOFT = 0.37, 0.03
W_FOAM_GAIN = 0.95
W_GLINT_JIT = 0.06      # the detail's share in the glint test: breaks the path into sparks
W_GLINT_SPARSE = 0.62   # the noise level a perfect reflection needs to spark (0.5: half the path is lit)
W_GLINT_EDGE = 0.003    # the sparks' edge width (in the jittered r.l / 2): soft over a pixel or two near
W_FRES_POWER = 7.0
W_HORIZON = 1.0
WATER = "anime"         # "anime" (Pass 3) or "pass2"


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
        P.op("rayBands", "smoothstep", 3, a=3, k=[RAY_LO, RAY_HI])
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


def cumulus(cx, cz, base, width, height, depth, seed, flat=False):
    """One illustrated formation: a flat base row of wide squashed puffs, then rows stacked above it, each narrower,
    their puffs rounder and bigger toward the crown (an anime cumulus bank); `flat` gives a long wind-stretched
    ribbon instead. Faces the camera (it stands across the line to the origin)."""
    out = []
    th = math.atan2(cx, -cz)
    tx, tz = math.cos(th), math.sin(th)                       # across the view
    nx, nz = math.sin(th), -math.cos(th)                       # away from the camera
    q = (0.0, math.sin(-th / 2.0), 0.0, math.cos(-th / 2.0))
    rows = 1 if flat else 4
    k = 0
    for r in range(rows):
        frac = r / max(rows - 1, 1)
        row_w = width * (1.0 - 0.55 * frac ** 1.3)
        n = (14 if flat else max(3, int(9 - 5 * frac)))
        for i in range(n):
            g = lambda j: mx._hash(seed * 97 + k, j, 77)
            k += 1
            u = (i + 0.5) / n - 0.5 + (g(1) - 0.5) * 0.6 / n
            along = u * row_w
            if flat:
                sx_, sy_ = width / n * (1.2 + 0.8 * g(2)), height * (0.25 + 0.2 * g(3))
                y = base + sy_ * 0.4
            else:
                rad = height * (0.22 + 0.14 * frac + 0.12 * g(2)) * (1.0 - 0.6 * abs(2 * u) ** 2)
                sx_, sy_ = rad * (1.5 - 0.4 * frac), rad * (0.7 if r == 0 else 1.0)
                y = base + (0.0 if r == 0 else height * (0.12 + 0.62 * frac) + (g(3) - 0.5) * rad * 0.3)
            sz_ = depth * (0.4 + 0.3 * g(4))
            d = (g(5) - 0.5) * depth
            x = cx + along * tx + d * nx
            z = cz + along * tz + d * nz
            out.append([round(x, 2), round(y, 2), round(z, 2), 0.0, round(q[1], 5), 0.0, round(q[3], 5),
                        round(sx_, 2), round(sy_, 2), round(sz_, 2)])
    return out


# The formations: (azimuth deg from the view, distance m, base m, width m, height m, depth m, ribbon?). Big banks on
# either side of the sun, one passing near it, ribbons low on the horizon, and smaller banks round the rest of the sky
# (the field turns slowly, so every one of them comes past the sun in time). Two layers, two drift rates: parallax.
CLOUDS_NEAR = [(-30.0, 3600.0, 330.0, 1400.0, 520.0, 300.0, False), (34.0, 4000.0, 380.0, 1700.0, 150.0, 220.0, True),
               (6.0, 4800.0, 230.0, 2600.0, 140.0, 200.0, True), (-58.0, 3400.0, 260.0, 900.0, 380.0, 260.0, False),
               (75.0, 3800.0, 300.0, 1100.0, 460.0, 280.0, False), (140.0, 3600.0, 320.0, 1300.0, 500.0, 300.0, False),
               (-130.0, 3900.0, 280.0, 1200.0, 420.0, 300.0, False), (-180.0, 3700.0, 350.0, 1400.0, 520.0, 300.0, False)]
CLOUDS_FAR = [(-12.0, 6500.0, 170.0, 3000.0, 110.0, 200.0, True), (52.0, 6800.0, 200.0, 2400.0, 120.0, 200.0, True),
              (-75.0, 6200.0, 520.0, 1800.0, 380.0, 300.0, False), (20.0, 7000.0, 700.0, 2000.0, 300.0, 300.0, False),
              (110.0, 6600.0, 180.0, 2600.0, 120.0, 200.0, True), (-150.0, 6800.0, 560.0, 1800.0, 360.0, 300.0, False),
              (170.0, 6400.0, 200.0, 2800.0, 120.0, 200.0, True)]


def cloud_points(layer):
    pts = []
    for k, (az, dist, base, w, h, d, flat) in enumerate(layer):
        a = math.radians(az + SUN_AZ * 0.0)
        pts += cumulus(dist * math.sin(a), -dist * math.cos(a), base, w, h, d, seed=k + (50 if layer is CLOUDS_FAR else 0),
                       flat=flat)
    return pts


CLOUD_BILLOW = 40.0
CLOUD_DRIFT = 0.0022        # radians a second the cloud field turns round the viewer (about 65 minutes a turn)




# ================================================================================================ Pass 2: the world
# The hero island on the left third, a chain of far islands, palms on its beach, the hero sailboat in the middle
# distance. Every one of them is painted by one illustrated program (doIllus): a flat colour (the node's emission, so
# palettes route onto it), a broad sun-side light band and a shadow band, and the haze of distance.
# ---- the island (the owner's simplification brief, 08): a long, LOW strip near the horizon -- an extruded silhouette,
# not terrain. A gently irregular top, a shallow slope to the water, a narrow sand line; 3-8 % of the image height.
ISLAND_C = (-300.0, -900.0)      # centre (x, z): the left third, near the horizon
ISLAND_W, ISLAND_D, ISLAND_H = 640.0, 150.0, 26.0   # length across the view, depth, the highest point (m)
TREE_SCALE = 2.2
ISLAND_STYLE = "C"               # A minimal silhouette, B tropical anime, C cut-paper travel illustration


def island_top(u):
    """The top line along the island, u in [-1, 1]: almost flat, a few soft bumps and one dip, asymmetric (higher
    toward the right end, a long low tail to the left)."""
    bumps = [(-0.62, 0.22, 0.35), (-0.15, 0.3, 0.75), (0.28, 0.18, 0.55), (0.55, 0.25, 1.0), (0.82, 0.12, 0.6)]
    h = 0.45 + sum(a * math.exp(-((u - c) / w) ** 2) for c, w, a in bumps) * 0.6
    return min(h, 1.0)


def island_height(x, z):
    u, v = x / (ISLAND_W * 0.5), z / (ISLAND_D * 0.5)
    r = math.hypot(u ** 6, v) if False else (abs(u) ** 6 + v * v) ** 0.5     # a long rounded rectangle footprint
    if r >= 1.0:
        return -3.0
    edge = mx.smooth(1.0, 0.72, r)                  # a shallow rise from the water, then the flat top
    return ISLAND_H * island_top(max(-1.0, min(1.0, u))) * edge ** 0.6 - 0.5


def island_meshes():
    nx, nz = 160, 26
    verts, faces = [], []
    for i in range(nz + 1):
        for j in range(nx + 1):
            x = (j / nx - 0.5) * ISLAND_W * 1.04
            z = (i / nz - 0.5) * ISLAND_D * 1.1
            verts.append((round(x, 3), round(island_height(x, z), 3), round(z, 3)))
    for i in range(nz):
        for j in range(nx):
            a = i * (nx + 1) + j
            faces += [(a, a + nx + 1, a + 1), (a + 1, a + nx + 1, a + nx + 2)]
    v, n, idx = mx.smooth_shaded(verts, faces)
    mx.write_glb(os.path.join(MESH_DIR, "do-island-low.glb"), v, [tuple(round(c, 4) for c in q) for q in n], idx,
                 "island")
    # the far chain: two faint low strips on the horizon
    def far(x, z):
        u, w = x / 300.0, z / 60.0
        r = (abs(u) ** 4 + w * w) ** 0.5
        return 9.0 * mx.smooth(1.0, 0.6, r) * (0.7 + 0.3 * math.exp(-((u - 0.3) / 0.4) ** 2)) - 1.0 if r < 1 else -3.0
    verts = []
    for i in range(11):
        for j in range(41):
            x, z = (j / 40 - 0.5) * 620.0, (i / 10 - 0.5) * 130.0
            verts.append((round(x, 3), round(far(x, z), 3), round(z, 3)))
    faces = []
    for i in range(10):
        for j in range(40):
            a = i * 41 + j
            faces += [(a, a + 41, a + 1), (a + 1, a + 41, a + 42)]
    v, n, idx = mx.smooth_shaded(verts, faces)
    mx.write_glb(os.path.join(MESH_DIR, "do-farstrip.glb"), v, [tuple(round(c, 4) for c in q) for q in n], idx,
                 "farstrip")
    return len(verts)


FAR_ISLES = [((-2400.0, -5200.0), 1.6, 1.0), ((2700.0, -5600.0), 1.2, 0.8)]


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


def palm_meshes():
    """One tree vocabulary, two shapes: a windswept palm (a curved tapering trunk, six broad tapered fronds -- a
    silhouette, not leaves) and a broad low tropical canopy (three overlapping flattened masses on a short trunk)."""
    H, lean = 16.0, 4.0
    tris = []
    segs, sides = 8, 6
    rings = []
    for k in range(segs + 1):
        u = k / segs
        cx, cy = lean * u * u, H * u
        rr = 0.6 * (1.0 - 0.45 * u)
        rings.append([(cx + rr * math.cos(2 * math.pi * j / sides), cy, rr * math.sin(2 * math.pi * j / sides))
                      for j in range(sides)])
    for k in range(segs):
        for j in range(sides):
            a, b = rings[k][j], rings[k][(j + 1) % sides]
            c, d = rings[k + 1][(j + 1) % sides], rings[k + 1][j]
            tris += [(a, c, b), (a, d, c)]
    top = (lean, H, 0.0)
    for b, (az, L, droop) in enumerate([(0.2, 8.0, 0.55), (1.15, 7.0, 0.7), (2.2, 6.5, 0.6), (3.3, 7.5, 0.5),
                                        (4.3, 6.0, 0.8), (5.3, 7.2, 0.65)]):
        dx, dz = math.cos(az), math.sin(az)
        px, pz = -dz, dx
        n = 5
        pts = []
        for k in range(n + 1):
            u = k / n
            y = 0.9 * math.sin(math.pi * u * 0.6) - droop * 6.0 * u * u
            w = 1.3 * math.sin(math.pi * min(1.0, u * 1.15)) * (1.0 - 0.3 * u) + 0.05
            pts.append(((top[0] + dx * L * u, top[1] + y, top[2] + dz * L * u), w))
        for k in range(n):
            (c0, w0), (c1, w1) = pts[k], pts[k + 1]
            a = (c0[0] + px * w0, c0[1], c0[2] + pz * w0)
            bq = (c0[0] - px * w0, c0[1], c0[2] - pz * w0)
            cq = (c1[0] - px * w1, c1[1], c1[2] - pz * w1)
            d = (c1[0] + px * w1, c1[1], c1[2] + pz * w1)
            tris += [(a, bq, cq), (a, cq, d), (a, cq, bq), (a, d, cq)]
    mx.write_faceted(os.path.join(MESH_DIR, "do-palm.glb"), tris, "palm")
    verts, faces = [], []
    for c, r in (((0.0, 9.0, 0.0), (7.5, 3.6, 6.0)), ((5.5, 10.5, 1.0), (5.5, 3.4, 4.5)),
                 ((-4.5, 8.2, -0.5), (5.0, 3.0, 4.2))):
        v, f = ellipsoid(c, r)
        off = len(verts)
        verts += v
        faces += [(a + off, b + off, c2 + off) for a, b, c2 in f]
    v, f = ellipsoid((0.0, 3.5, 0.0), (0.7, 4.0, 0.7), 8, 4)
    off = len(verts)
    verts += v
    faces += [(a + off, b + off, c2 + off) for a, b, c2 in f]
    v, n, idx = mx.smooth_shaded(verts, faces)
    mx.write_glb(os.path.join(MESH_DIR, "do-canopy.glb"), v, n, idx, "canopy")
    return len(tris)


# trees: (along the island u in [-1, 1], across v, kind, scale, yaw deg). Gaps on purpose; one tall palm marks it.
TREES = {
    "A": [(-0.35, 0.1, "palm", 1.25, 20.0), (0.12, -0.05, "palm", 0.95, -15.0), (0.52, 0.0, "canopy", 1.0, 0.0),
          (0.7, 0.15, "palm", 1.05, 35.0)],
    "B": [(-0.6, 0.0, "canopy", 0.85, 30.0), (-0.35, 0.1, "palm", 1.3, 20.0), (-0.2, -0.1, "palm", 0.85, 60.0),
          (0.12, -0.05, "palm", 1.0, -15.0), (0.5, 0.0, "canopy", 1.05, 0.0), (0.68, 0.15, "palm", 1.1, 35.0),
          (0.82, 0.0, "palm", 0.8, -40.0)],
}
TREES["C"] = TREES["A"] + [(-0.62, 0.0, "canopy", 0.8, 30.0)]


def tree_points(kind):
    pts = []
    for u, v, k, sc, yaw in TREES[ISLAND_STYLE]:
        if k != kind:
            continue
        x, z = u * ISLAND_W * 0.5, v * ISLAND_D * 0.5
        y = island_height(x, z)
        q = math.radians(yaw) / 2.0
        pts.append([round(ISLAND_C[0] + x, 2), round(y - 0.6, 2), round(ISLAND_C[1] + z, 2), 0.0, round(math.sin(q), 5),
                    0.0, round(math.cos(q), 5), sc * TREE_SCALE, sc * TREE_SCALE, sc * TREE_SCALE])
    return pts


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
    arm(st, 1.6, k, 46.0, 0.6, 2.6)
    arm(st, -1.6, -k, 46.0, 0.6, 2.6)
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
WAKE_CUT0, WAKE_CUT1 = 0.24, 0.62


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
             "center": [0.0, 0.0, 0.0], "pathOffset": hl * 0.5, "pathScale": 1.0, "pathRoll": 0.0, "space": "local"}]
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
    """The island as an anime background paints it: about three land colours -- the main colour on the faces that
    look up, a darker underside on the steep faces toward the camera, a lighter accent on the edges that catch the low
    sun -- and a narrow sand line at the water. In cut-paper style (C) the bands are hard and there is no accent.
    Then the haze that ties it to the horizon. Registers: 0 N, 1 V, 2 world position."""
    P = Prog("doLand")
    sx, sy, sz = S
    P.op("N", "input", 0, inp="normal")
    P.op("V", "input", 1, inp="viewDirection")
    P.op("wp", "input", 2, inp="worldPosition")
    P.pcolour("land", 3, "land")
    P.pcolour("land2", 4, "land2")
    P.op(None, "swizzle", 5, a=0, k=[1, 1, 1, 1])                                   # n.y
    hard = ISLAND_STYLE == "C"
    P.op("under", "smoothstep", 5, a=5, k=[LAND_UNDER0, LAND_UNDER0 + (0.01 if hard else LAND_UNDER_SOFT)])
    P.op(None, "mixBy", 3, a=4, b=3, c=5)                                           # steep faces: the underside
    if not hard:
        P.op(None, "fresnel", 4, f=2.5)                                              # the sun-facing accent
        P.op(None, "gradient", 5, a=1, k=[-sx, -sy, -sz, 0.0], f=1.0)
        P.op(None, "power", 5, a=5, f=5.0)
        P.op(None, "multiply", 4, a=4, b=5)
        P.op(None, "smoothstep", 4, a=4, k=[0.12, 0.2])
        P.pcolour("land3", 5, "land3")
        P.op(None, "mixBy", 3, a=3, b=5, c=4)
    if ISLAND_STYLE != "A":
        P.op(None, "swizzle", 5, a=2, k=[1, 1, 1, 1])                                # the sand line at the water
        P.op("shore", "smoothstep", 5, a=5, k=[LAND_SAND1, LAND_SAND0])
        P.pcolour("sand", 4, "sand")
        P.op(None, "mixBy", 3, a=3, b=4, c=5)
    P.op(None, "input", 4, inp="cameraDistance")
    P.op(None, "constant", 5, k=[0.1, 0.1, 0.1, 0.1])
    P.op(None, "multiply", 4, a=4, b=5)
    P.op("haze", "remap", 4, a=4, k=[ILLUS_HAZE0, ILLUS_HAZE1, 0.0, ILLUS_HAZE_MAX], f=1.0)
    P.pcolour("landHaze", 5, "cool")
    P.op("hazed", "mixBy", 3, a=3, b=5, c=4)
    return P


LAND_UNDER0, LAND_UNDER_SOFT = 0.55, 0.12
LAND_SAND0, LAND_SAND1 = 0.6, 1.6


ILLUS_BAND0, ILLUS_BAND1 = 0.32, 0.42
ILLUS_SHADOW, ILLUS_LIT = 0.62, 1.12
ILLUS_RIM = 0.9
ILLUS_HAZE0, ILLUS_HAZE1, ILLUS_HAZE_MAX = 60.0, 500.0, 0.55


def flat(key, scale=1.0, double=False):
    m = {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": pal(PALETTE, key, scale), "emissiveIntensity": 1.0,
         "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": "doIllus"}
    if double:
        m["doubleSided"] = True
    return m


# the nodes painted by doIllus and the palette key each one's colour comes from (palette_routes routes them)
ILLUS_NODES = [("palms", "trunk"), ("canopies", "trunk"), ("boatHull", "pearl"), ("boatBoot", "glaze"),
               ("boatDeck", "deck"),
               ("boatGlaze", "glaze"), ("boatSail", "pearl")] + [("farIsle%d" % k, "isle") for k in range(len(FAR_ISLES))]


def world(s):
    island_meshes()
    palm_meshes()
    boat_meshes()
    ip = illus_program()
    s.program(ip.doc(emission=2))
    lp = land_program()
    s.program(lp.doc(emission=3))
    # the island is a stable anchor: no deformer, nothing routed to its shape
    s.proc("island", {"kind": "mesh", "asset": "meshes/do-island-low.glb"}, material=unlit("doLand"),
           transform={"position": [ISLAND_C[0], 0.0, ISLAND_C[1]], "rotation": [0.0, -4.0, 0.0], "scale": [1.0, 1.0, 1.0]})
    for k, ((x, z), sc, sy) in enumerate(FAR_ISLES):
        s.proc("farIsle%d" % k, {"kind": "mesh", "asset": "meshes/do-farstrip.glb"}, material=flat("isle"),
               transform={"position": [x, 0.0, z], "rotation": [0.0, 8.0 * k, 0.0], "scale": [sc, sy, sc]})
    tree = "trunk"                                 # one dark cut-paper tone for every tree (variant C)
    # only the palms sway, gently (0.2 m at the crown, 9 s); the canopies hold still
    sway = [{"kind": "sine", "amount": 0.2, "frequency": 0.12, "speed": 0.7, "phase": 0.0, "axis": [0.0, 1.0, 0.0],
             "displacementAxis": [0.8, 0.0, 0.6], "center": [0.0, 0.0, 0.0], "falloff": 16.0, "space": "local"}]
    s.proc("palms", {"kind": "mesh", "asset": "meshes/do-palm.glb"},
           distribution={"kind": "points", "points": tree_points("palm")}, material=flat(tree, double=True),
           deformers=sway)
    s.proc("canopies", {"kind": "mesh", "asset": "meshes/do-canopy.glb"},
           distribution={"kind": "points", "points": tree_points("canopy")}, material=flat(tree))
    shore(s)
    wp = boat(s)
    return ip, lp, wp


def shore_mesh(n=360):
    """Pass 3, Pass E: the island's surf line. A low vertical ribbon (SHORE_LO..SHORE_HI m) round the footprint's
    waterline (the superellipse |u|^6 + v^2 = 1 of island_height, pushed out by SHORE_OUT), facing out to sea.
    uv = (round the shore 0..1, up the ribbon 0..1). At 900 m a pixel is ~0.5 m, so the ribbon is a 2-3 px line: the
    white surf line of an anime background, broken by shore_program."""
    a, b = ISLAND_W * 0.5 * SHORE_OUT, ISLAND_D * 0.5 * SHORE_OUT
    verts, uvs, nrm, idx = [], [], [], []
    for i in range(n + 1):
        q = 2.0 * math.pi * i / n
        c, sn = math.cos(q), math.sin(q)
        x = a * math.copysign(abs(c) ** (1.0 / 3.0), c)
        z = b * sn
        # the outward normal of |x/a|^6 + (z/b)^2 = 1
        gx, gz = 6.0 * (x / a) ** 5 / a, 2.0 * (z / b) / b
        L = math.hypot(gx, gz) or 1.0
        for k, y in ((0, SHORE_LO), (1, SHORE_HI)):
            verts.append((round(x, 3), y, round(z, 3)))
            uvs.append((i / n, float(k)))
            nrm.append((gx / L, 0.0, gz / L))
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


SHORE_OUT = 1.005
SHORE_LO, SHORE_HI = -0.4, 1.6
SHORE_FREQ = 0.04
SHORE_CUT = 0.28
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
           transform={"position": [ISLAND_C[0], 0.0, ISLAND_C[1]], "rotation": [0.0, -4.0, 0.0], "scale": [1.0, 1.0, 1.0]},
           deformers=ride_deformers())
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
    ("podB", (-80.0, -300.0), 176.0, 3.4, 9.0, 79.0, [(0.0, 0.0, 1.0), (0.7, 3.0, 0.8)]),
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
        waveWidth=10.0, waveOrigin=0.0, strength=round(HERO_WAVE_HEIGHT / KICK_LIFT, 4),
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


MOOD_INTENSITY = 0.35


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
    s.route(R("kickEnv", M % (sea.name, sea["foam"]), 1.0, op="multiply", gain=0.4, offset=1.0, attackMs=0,
              decayMs=600))
    # the crest ramp follows the sea's height (a taller sea needs a taller crest to foam): foam stays a cap
    s.route(R("level", "material/doSea/op/%d/gradient/constant" % sea["crest"], 1.0, op="multiply", gain=-0.3,
              offset=1.0, attackMs=1800, decayMs=4500))
    s.route(R("level", "material/doSea/op/%d/smoothstep/constant" % sea["foamMask"], 0.05, comp=0, attackMs=1800,
              decayMs=4500),
            R("level", "material/doSea/op/%d/smoothstep/constant" % sea["foamMask"], 0.05, comp=1, attackMs=1800,
              decayMs=4500))
    # SNARE: spray off the near swell
    s.route(R("snare", "particles/snareSpray/burst", 70.0, threshold="binary", thresholdLevel=0.25))
    # HIGHS: the sun's glints flare and the micro-ripples deepen (more, smaller glints); hats sparkle the path
    for prog in (sea, seafar):
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
    (5, "rays", "SUN RAYS: the rays' strength and reach, the halo and the sun's path"),
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
        for c in range(3):
            if abs(b[c] - a[c]) > 1e-4:
                s.route(R(signal, "procedural/%s/material/emissiveColor" % node, round(b[c] - a[c], 5), comp=c,
                          **chain))


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
    s.route(R("control.energy", M % (sea.name, sea["glint"]), 1.0, op="multiply", gain=0.8, offset=1.0, **CC))
    # a taller sea needs a taller crest to foam: the crest ramp follows the waves control
    s.route(R("control.waves", "material/doSea/op/%d/gradient/constant" % sea["crest"], 1.0, op="multiply",
              gain=-0.42, offset=1.0, **CC))
    # foam is a crest cap whatever the sea's height: a taller sea (waves) raises the cut as much as energy lowers it,
    # so energy makes the caps livelier and brighter, never a slab (Pass 3, Pass C)
    FM = "material/doSea/op/%d/smoothstep/constant" % sea["foamMask"]
    s.route(R("control.energy", FM, -0.04, comp=0, **CC), R("control.energy", FM, -0.04, comp=1, **CC),
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
        s.route(R("control.padGlow", M % (sea.name, sea["foam"]), round(pal("midnight", "foam")[c] * 1.5, 4), comp=c,
                  **glow))
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
        for band in ("band1", "band2", "band3"):
            for src, amt in (("control.bands", 0.85 * W_SOFT), ("intensity", 0.3 * W_SOFT)):
                s.route(R(src, M % (prog.name, prog[band], "smoothstep"), amt, comp=0, **CC),
                        R(src, M % (prog.name, prog[band], "smoothstep"), -amt, comp=1, **CC))
        # DETAIL: the noise's share of t (0.07 -> 0.2), its mean shift taken back out of the height term
        s.route(R("control.detail", "material/%s/op/%d/mix/value" % (prog.name, prog["detail"]), 0.13, **CC),
                R("control.detail", M % (prog.name, prog["crest"], "gradient"), -0.065, comp=3, **CC))
    # HIGHLIGHTS: brighter (x1.8) and wider (the stroke band opens downward); hats flash them
    C = "material/%s/op/%d/constant/constant"
    s.route(R("control.highlights", C % (sea.name, sea["hl"]), 1.0, op="multiply", gain=0.8, offset=1.0, **CC),
            R("control.highlights", M % (sea.name, sea["strokeLo"], "smoothstep"), -0.05, comp=0, **CC),
            R("control.highlights", M % (sea.name, sea["strokeLo"], "smoothstep"), -0.05, comp=1, **CC),
            R("hatEnv", C % (sea.name, sea["hl"]), 1.0, op="multiply", gain=0.5, offset=1.0, attackMs=0,
              decayMs=250))
    # FOAM: the cut drops (more caps) and the foam brightens
    FM = M % (sea.name, sea["foamMask"], "smoothstep")
    s.route(R("control.foam", FM, -0.06, comp=0, **CC), R("control.foam", FM, -0.06, comp=1, **CC),
            R("control.foam", C % (sea.name, sea["foam"]), 1.0, op="multiply", gain=0.3, offset=1.0, **CC))


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
HERO_WAVE_HEIGHT = 1.9      # metres (2.6 saturated the crest light into a slab as it arrived)


SUN_RADIUS_DEG = 1.7
RAYS = True
RAY_FREQ = 2.6         # how many rays round the sun (the noise's frequency on the unit circle)
RAY_LO, RAY_HI = 0.44, 0.7
RAY_REACH = 62.0       # degrees from the sun at which a ray has faded
RAY_FALL = 3.2
RAY_GAIN = 0.3
RAY_DRIFT = ((0.0137, 1.6), (0.0089, 2.3))   # (Hz, amplitude) of the two slow sines that move the rays
SUN_COS = math.cos(math.radians(SUN_RADIUS_DEG))


# ================================================================================================ build
def unlit(program):
    return {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [0.0, 0.0, 0.0], "emissiveIntensity": 0.0,
            "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": program}


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
    generate_meshes()
    s = kit.Scene(ID, TITLE, DESIGN)
    s.unshadowed_key()          # every material is unlit: a casting default key would be pure cost
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
    for name, layer, drift in (("clouds", CLOUDS_NEAR, CLOUD_DRIFT), ("cloudsFar", CLOUDS_FAR, CLOUD_DRIFT * 0.55)):
        s.proc(name, {"kind": "sphere", "radius": 1.0, "segments": 18, "rings": 12},
               distribution={"kind": "points", "points": cloud_points(layer)}, material=unlit("doCloud"),
               deformers=[{"kind": "twist", "amount": 0.0, "speed": drift, "phase": 0.0, "axis": [0.0, 1.0, 0.0],
                           "center": [0.0, 0.0, 0.0], "space": "world"},
                          # slow billowing: the puffs swell and settle (a noise of world position, drifting)
                          {"kind": "noise", "amount": CLOUD_BILLOW, "scale": 0.004, "speed": 0.03, "seed": 9,
                           "space": "world"}])
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

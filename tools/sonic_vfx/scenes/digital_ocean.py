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
from ..kit import R, hexrgb

ID = "digital-ocean"
TITLE = "Digital Ocean"

MESH_DIR = os.path.join(kit.ABSTRACT_DIR, "meshes")

DESIGN = {
    "category": "digital seascape",
    "thesis": "The Tidewriter: a stylized ocean at electric twilight where the music is written into the sea. The "
              "bass's last fifteen seconds roll in as swell, every kick sends a lifted front in from the horizon, "
              "highs glint on the sun's path, held sound swells the sun's halo; dolphins leap across the sun's "
              "reflection, and a performer can raise a hero wave, fire a leap, bloom the crests or let night fall.",
    "composition": {
        "background": "a violet-to-coral sky, twilight stratus lit coral round the sun, a hazy island on the left third",
        "midground": "rows of Gerstner swell with cyan light through their backlit crests",
        "foreground": "the near swell, its foam caps and the sun's broken path",
        "focal": "the low cream sun on the right third and its column of glints",
        "secondary": ["dolphins leaping across the sun's path", "the hero wave", "foam ribbons on the highest crests"],
        "atmosphere": "distance flattens the far sea into colour and glitter; clouds haze into the sky",
        "post": "a little bloom on the sun and the glints",
        "camera": "still: 12 m over the water, 45 mm, the horizon on the upper 40 %",
    },
    "palette": {"dominant": "#0a0b35", "secondary": "#563a8a", "accent": "#18cfe0", "highlight": "#ffe2cc",
                "background_value": "mid (a coral horizon under a violet sky)",
                "saturation": "indigo and violet, one cyan accent, one warm light; night and golden hour are the "
                              "palette's two related ends (CC 1, CC 7)"},
    "motion": {
        "very_slow": ["the cloud field turning round the viewer", "the swell growing with the piece"],
        "medium": ["three Gerstner trains in three directions", "the bass's history rolling in as swell"],
        "fast": ["kick fronts rolling in", "dolphin leaps", "the sun's halo breathing with held sound"],
        "extremely_fast": ["glints on the sun's path", "spray"],
    },
    "vocabulary": [
        ["bass", "tide (spectrum field, the low band)", "TIDEWRITING: what the bass played rolls in as swell, "
         "taking ~15 s from 400 m out to your feet"],
        ["kick", "kickFront (onset field, low band) + response.kickEnv", "a lifted front rolls in from 70 m out; the "
         "crest light and foam flash"],
        ["level", "response.level", "the long swell grows with loudness (slowly)"],
        ["intensity", "response.intensity", "STRUCTURE: the swell's crests sharpen as the piece builds"],
        ["mids", "audio.mid", "the wind sea roughens"],
        ["snare", "response.snare", "spray off the near swell"],
        ["hats", "response.hatEnv", "the glints on the sun's path flare"],
        ["highs", "audio.treble", "the glints brighten"],
        ["sustain", "response.sustain", "the sun's halo swells and its path on the water warms"],
        ["brightness", "sonic.brightness.slow", "the whole palette drifts a little warmer or cooler"],
        ["MIDI note", "notes.noteOn, notes.lastPitch, notes.lastVelocity", "a burst of sparks on the sun's path: low "
         "notes near, high notes far, velocity is how many"],
        ["CC 1", "control.night", "NIGHT: dusk falls into a bioluminescent night"],
        ["CC 2", "control.swell", "SWELL: the long and cross swells rise"],
        ["CC 3", "control.chop", "CHOP: the wind sea roughens, more glints"],
        ["CC 4", "control.foam", "FOAM: foam caps on more crests"],
        ["CC 5", "control.sun", "SUN: halo, glow and the sun's path"],
        ["CC 6", "control.tide", "TIDE: how deep the music writes into the sea"],
        ["CC 7", "control.golden", "GOLDEN: back toward golden hour"],
        ["CC 8", "control.spray", "SPRAY: how much the drums and notes throw"],
        ["pad 36", "control.padLeap", "LEAP: a dolphin arcs across the sun's path"],
        ["pad 37", "control.padWave", "HERO WAVE: one big swell rises far out and rolls in, foaming"],
        ["pad 38", "control.padGlow", "BLOOM: bioluminescence along every crest, fading over seconds"],
        ["pad 39", "control.padFlare", "FLARE: the sun flares and settles"],
        ["pad 40", "control.padSpray", "SPRAY: a gust of spray off the near swell"],
        ["silence", "(no input)", "the swell rolls on, the clouds drift, pods of dolphins leap every 31 and 47 s"],
    ],
    "tier": "light: 78k sea vertices (three pieces) through 8 deformers, five unlit programs (48 ops on the near sea), "
            "196 cloud puffs, six dolphins, fixed particle pools; no lights, no shadows, no fog march",
}

INDEX_ENTRY = {
    "category": "Sonic Abstract",
    "description": "Play live audio (a mix or a drum-heavy track) plus a MIDI controller: CC 1 night, CC 2 swell, "
                   "CC 3 chop, CC 4 foam, CC 5 sun, CC 6 tide, CC 7 golden hour, CC 8 spray; pads 36 dolphin leap, "
                   "37 hero wave, 38 bioluminescent bloom, 39 sun flare, 40 spray. A stylized twilight ocean that "
                   "writes the music into its swell. See docs/prototypes/digital-ocean/02-report.md.",
    "live": True,
    "name": "Sonic Abstract - Digital Ocean",
    "project": "sonic-abstract/digital-ocean.json",
}


# ------------------------------------------------------------------------------------------------ look-development knobs
# Module-level so a look-development script can set them before build() (tools stay pure functions of these).
COMPOSITION = "C2"


def unit(v):
    n = math.sqrt(sum(x * x for x in v))
    return tuple(x / n for x in v)


def sun_dir(elev_deg, az_deg):
    """Towards the sun: elevation above the horizon, azimuth clockwise from -Z (the view) seen from above."""
    e, a = math.radians(elev_deg), math.radians(az_deg)
    return unit((math.sin(a) * math.cos(e), math.sin(e), -math.cos(a) * math.cos(e)))


SUN_ELEV, SUN_AZ = 4.6, 12.0
S = sun_dir(SUN_ELEV, SUN_AZ)

# The compositions of Stage 1: (eye height, focal length, pitch in degrees, yaw in degrees)
COMPOSITIONS = {
    "A": (3.2, 30.0, -1.5, 4.0),     # low and cinematic: in the swell
    "B": (32.0, 26.0, -13.0, 0.0),   # elevated: the pattern of the waves
    "C": (9.0, 55.0, -0.9, 6.0),     # graphic: a long lens, the horizon high, the sun on a third
    "C2": (12.0, 45.0, -3.6, 4.0),   # graphic, higher: horizon on the upper third, the swell rows as bands
    "C3": (7.0, 40.0, -2.6, 2.0),
    "B2": (20.0, 30.0, -7.5, 2.0),
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
    "golden": dict(zenith="#203b78", mid="#9a86b4", warm="#ffc489", cool="#e8949a", sun="#fff3d6", glow="#ffa15e",
                   deep="#0b2453", rzen="#1c3a74", rhor="#d9988e", sss="#2fb3a3", glint="#fff0c8", foam="#fff1d8",
                cshade="#7d6c9e", clit="#ffbf8a", crim="#fff0d2", isle="#6a5a92",
                dback="#2a2550", dbelly="#7e6f9e", drim="#ffd9a8"),
    "twilight": dict(zenith="#100d36", mid="#563a8a", warm="#ff9b7d", cool="#8a6cc2", sun="#ffe2cc", glow="#ff7f68",
                     deep="#0a0b35", rzen="#191c58", rhor="#8a6cbd", sss="#18cfe0", glint="#ffd6c2", foam="#bdfde6",
                cshade="#33245f", clit="#ff8a78", crim="#ffd3b4", isle="#3f2a68",
                dback="#140f33", dbelly="#4a3a78", drim="#ffc3a8"),
    "midnight": dict(zenith="#03050f", mid="#17123c", warm="#3b2b6a", cool="#1d2150", sun="#e9f0ff", glow="#5d72c8",
                     deep="#02060f", rzen="#050a1c", rhor="#2a285c", sss="#10f0a8", glint="#cfe0ff", foam="#7affe0",
                cshade="#0a0a22", clit="#33346e", crim="#a8b6ff", isle="#0a0a26",
                dback="#020410", dbelly="#141a3a", drim="#9fb4ff"),
    "tropical": dict(zenith="#1546a8", mid="#6aa6de", warm="#ffb04a", cool="#ff8fae", sun="#fff6d2", glow="#ff9a3a",
                     deep="#003d63", rzen="#0b5a8a", rhor="#ffae8a", sss="#22e0c0", glint="#fff4c4", foam="#fffbea",
                cshade="#8b7fbd", clit="#ffab6a", crim="#fff2d2", isle="#5a5096",
                dback="#1a2e5a", dbelly="#6f86b0", drim="#ffe0b0"),
}
PALETTE = "twilight"
DEBUG_SKY = None
BANDS = 0
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

    def doc(self, emission):
        return {"name": self.name, "ops": self.ops, "baseColor": -1, "metallic": -1, "roughness": -1,
                "emission": emission, "emissionIntensity": 1.0, "opacity": -1}


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
    # the underside, lit by the low sun -- strongly toward the sun, faintly away from it
    P.op(None, "gradient", 2, a=0, k=[0.0, -1.0, 0.0, 0.08], f=1.0)
    P.op(None, "power", 2, a=2, f=1.6)
    P.op("toward", "gradient", 4, a=1, k=[-sx, -sy, -sz, 0.0], f=1.0)     # cos of the angle to the sun
    P.op(None, "smoothstep", 5, a=4, k=[math.cos(math.radians(CLOUD_POOL[1])), math.cos(math.radians(CLOUD_POOL[0]))])
    P.op(None, "remap", 5, a=5, k=[0.0, 1.0, CLOUD_AWAY, 1.0])
    P.op(None, "multiply", 2, a=2, b=5)
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


def cloud_points():
    """The cloud field: banks round the whole horizon (the slow drift turns them past the view forever), each a cluster
    of squashed puffs. Twilight stratus -- long, thin, flat-bottomed streaks low on the horizon -- with a few cumulus
    banks behind them. Deterministic. Returns distribution points [px, py, pz, qx, qy, qz, qw, sx, sy, sz]."""
    pts = []
    n_banks = 26
    for b in range(n_banks):
        h = lambda k: mx._hash(b, k, 71)
        theta = 2.0 * math.pi * (b + 0.6 * h(1)) / n_banks
        D = 3200.0 + 2600.0 * h(2)
        cumulus = False
        tier = int(3 * h(3))                           # three decks: low and long, middle, high and thin
        alt = (150.0, 330.0, 520.0)[tier] + 90.0 * (h(4) - 0.5)
        npuff = 9 + int(10 * h(5))
        length = (520.0 + 900.0 * h(6)) * (1.0, 0.8, 1.3)[tier]
        tx, tz = math.cos(theta), math.sin(theta)
        phi = -theta
        q = (0.0, math.sin(phi / 2.0), 0.0, math.cos(phi / 2.0))
        for i in range(npuff):
            u = (i + 0.5) / npuff - 0.5
            g = lambda k: mx._hash(b * 31 + i, k, 73)
            along = u * length + (g(1) - 0.5) * 60.0
            bulge = 1.0 - (2.0 * u) ** 2
            if cumulus:
                sx = 70.0 + 90.0 * g(2) * bulge
                sy = (35.0 + 70.0 * g(3)) * (0.5 + bulge)
                y = alt + sy * 0.45
            else:
                # (round 1) more, smaller, more varied puffs: an irregular top and a flat, slightly stepped base
                sx = (55.0 + 150.0 * g(2) ** 1.5) * (0.45 + 0.55 * bulge)
                sy = (5.0 + 16.0 * g(3) ** 2 * (0.3 + bulge)) * (1.0, 0.8, 0.55)[tier]
                y = alt + (g(4) - 0.5) * 14.0 + sy * 0.55
            sz = sx * (0.5 + 0.4 * g(5))
            r = D + (g(6) - 0.5) * 120.0
            x = r * math.sin(theta) + along * tx
            z = -r * math.cos(theta) + along * tz
            pts.append([round(x, 2), round(y, 2), round(z, 2), 0.0, round(q[1], 5), 0.0, round(q[3], 5),
                        round(sx, 2), round(sy, 2), round(sz, 2)])
    return pts


CLOUD_DRIFT = 0.0016        # radians a second the cloud field turns round the viewer (about 65 minutes a turn)


def island_meshes():
    """The distant island on the left third: a long low mesa and a sea stack, faceted, its foot sunk under the sea."""
    tris = []
    tris += mx.rock(91, 300.0, 70.0, 15, 4, sink=40.0, lean=(0.03, 0.0))
    tris += mx.translate(mx.rock(93, 260.0, 52.0, 13, 3, sink=40.0), (420.0, 0.0, 40.0))
    tris += mx.translate(mx.rock(95, 200.0, 30.0, 11, 3, sink=30.0), (-420.0, 0.0, -30.0))
    tris += mx.translate(mx.rock(99, 160.0, 22.0, 9, 3, sink=30.0), (760.0, 0.0, 80.0))
    tris = mx.clip_above(tris, -2.0)
    mx.write_faceted(os.path.join(MESH_DIR, "do-isle.glb"), tris, "isle")
    return len(tris)


ISLE_AZ, ISLE_DIST = -16.0, 4300.0


# ================================================================================================ dolphins
DOLPHIN_LEN = 3.1


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
    mx.write_faceted(os.path.join(MESH_DIR, "do-dolphin.glb"), tris, "dolphin")
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
    prof = [(-2.4 * w, -6.5), (-1.25 * w, -2.6), (-0.55 * w, 0.75 * height), (0.0, height),
            (0.55 * w, 0.75 * height), (1.25 * w, -2.6), (2.4 * w, -6.5)]
    if loop:
        prof += [(2.4 * w + 0.4 * reach, -0.45 * depth), (1.2 * w + 0.5 * reach, -depth),
                 (-1.2 * w - 0.5 * reach, -depth), (-2.4 * w - 0.4 * reach, -0.45 * depth)]
    return [(round(centre[0] + T[0] * a, 3), round(b, 3), round(centre[2] + T[2] * a, 3)) for a, b in prof]


# The pods: (name, centre (x, z), heading deg, height, half span, period s, lag s per member, member offsets (along
# the view's depth, height scale)). Pod A crosses the sun's path at 70 m left to right; pod B, smaller and far, swims
# right to left near the island side.
PODS = [
    ("podA", (17.0, -88.0), -4.0, 2.3, 5.5, 53.0, [(0.0, 0.0, 1.0), (0.55, -3.5, 0.85), (1.25, 2.8, 0.92)]),
    ("podB", (-42.0, -150.0), 176.0, 2.0, 5.0, 79.0, [(0.0, 0.0, 1.0), (0.7, 3.0, 0.8)]),
]
LEAP_SPEED = 8.5             # metres a second along the path (a dolphin's leap)
HERO = ("hero", (10.0, -56.0), -2.0, 4.2, 7.0)
HERO_ATTACK_MS = None        # computed from the hero path's length


def spline_node(name, points, closed):
    return {"name": name, "kind": "spline", "spline": {
        "name": name, "kind": "catmullRom", "closed": bool(closed), "tension": 0.5, "generator": "points",
        "points": [{"position": list(p), "roll": 0.0, "scale": 1.0} for p in points], "samplesPerSegment": 48,
        "up": [0.0, 1.0, 0.0]}}


def dolphin_node(s, name, spline):
    """One dolphin: the tail's kick (a local sine growing from the nose to the flukes), then bent along `spline` by the
    path deformer, whose `pathOffset` (deform/2) is where along the path its tail is."""
    s.proc(name, {"kind": "mesh", "asset": "meshes/do-dolphin.glb"}, material=unlit("doDolphin"), deformers=[
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
                speedMin=3.0, speedMax=7.5, gravity=[0.0, -9.8, 0.0], drag=0.3, sizeStart=0.085, sizeEnd=0.025,
                colorStart=pal(PALETTE, "foam") + [0.95], colorEnd=pal(PALETTE, "sss") + [0.0], emissive=1.6,
                blend="additive", turbulence=0.0, collision="splash", collisionHeight=0.0, splashLifetime=1.3,
                splashSize=9.0, ringThickness=0.07)


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


SEA_NODES = ["sea_near", "sea_mid", "sea_far"]


def instrument(s, sea, seafar, sky):
    """The modulation map. Every audio response has its own scale and decay; nothing pulses the whole frame."""
    M = "material/%s/op/%d/constant/constant"
    deco = sea_deformers()
    for node in SEA_NODES:
        D = "procedural/%s/deform/%%d/amount" % node
        # LEVEL: the long swell grows with the music's loudness (scene energy), slowly
        s.route(R("level", D % 1, 0.5, attackMs=1800, decayMs=4500))
        # MIDS: the wind sea roughens
        s.route(R("audio.mid", D % 5, 0.07, attackMs=300, decayMs=1500))
        # INTENSITY: STRUCTURE -- the crests sharpen as the piece builds (the horizontal Gerstner amounts grow)
        # (+0.1 on the long swell only: the steepnesses' sum stays under 1 at full intensity, 0.85 -> 0.95)
        s.route(R("intensity", D % 2, deco[1]["amount"] * (0.10 / STEEP[0]), attackMs=4000, decayMs=8000))
    # KICK: the crest light and the foam flash for a moment (the kick's lifted front rolls in on its own)
    for prog in (sea, seafar):
        s.route(R("kickEnv", M % (prog.name, prog["sss"]), 1.0, op="multiply", gain=0.55, offset=1.0, attackMs=0,
                  decayMs=420))
    s.route(R("kickEnv", M % (sea.name, sea["foam"]), 1.0, op="multiply", gain=1.2, offset=1.0, attackMs=0,
              decayMs=600))
    # the crest ramp follows the sea's height (a taller sea needs a taller crest to foam): foam stays a cap
    s.route(R("level", "material/doSea/op/%d/gradient/constant" % sea["crest"], 1.0, op="multiply", gain=-0.3,
              offset=1.0, attackMs=1800, decayMs=4500))
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
    s.route(R("sustain", M % (sky.name, sky["halo"]), 1.0, op="multiply", gain=0.7, offset=1.0, attackMs=1500,
              decayMs=3500),
            R("sustain", M % (sky.name, sky["glow"]), 1.0, op="multiply", gain=0.35, offset=1.0, attackMs=1500,
              decayMs=3500))
    for prog in (sea, seafar):
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
    (1, "night", "NIGHT: dusk falls into a bioluminescent night (the palette's other end: the crests and foam glow)"),
    (2, "swell", "SWELL: the long swell and the cross swell rise"),
    (3, "chop", "CHOP: the wind sea roughens and the glints multiply"),
    (4, "foam", "FOAM: foam caps on more crests, brighter"),
    (5, "sun", "SUN: the sun's halo, glow and path on the water"),
    (6, "tide", "TIDE: how deep the music writes itself into the sea (the bass's history as swell)"),
    (7, "golden", "GOLDEN: back toward golden hour (paler sky, softer sea)"),
    (8, "spray", "SPRAY: how much spray and sparkle the drums and notes throw"),
]
MIDI_PADS = [
    (36, "padLeap", "LEAP: a dolphin breaks the surface and arcs across the sun's path"),
    (37, "padWave", "HERO WAVE: one big swell rises far out and rolls in to your feet, foaming"),
    (38, "padGlow", "BLOOM: a burst of bioluminescence along every crest, fading over seconds"),
    (39, "padFlare", "FLARE: the sun flares; its halo and path blaze and settle"),
    (40, "padSpray", "SPRAY: a gust of spray off the near swell"),
]


def control_map():
    b = []
    for num, name, _ in MIDI_CC:
        b.append({"source": "*", "channel": -1, "kind": "cc", "number": num, "signal": name})
    for num, name, _ in MIDI_PADS:
        b.append({"source": "*", "channel": -1, "kind": "noteEvent", "number": num, "signal": name})
    return {"midi": {"enabled": True, "filter": "*", "bindings": b}}


def palette_routes(s, progs, signal, to, chain):
    """A palette change on one control: for every palette colour every program paints with, add (to - current) per
    channel at full control. (The island's flat colour too.)"""
    for prog in progs:
        for label, key, scale in getattr(prog, "pal", []):
            a, b = pal(PALETTE, key, scale), pal(to, key, scale)
            for c in range(3):
                d = round(b[c] - a[c], 5)
                if abs(d) > 1e-4:
                    s.route(R(signal, "material/%s/op/%d/constant/constant" % (prog.name, prog[label]), d, comp=c,
                              **chain))
    a, b = pal(PALETTE, "isle"), pal(to, "isle")
    for c in range(3):
        if abs(b[c] - a[c]) > 1e-4:
            s.route(R(signal, "procedural/isle/material/emissiveColor", round(b[c] - a[c], 5), comp=c, **chain))


def midi(s, progs, sea, seafar, sky):
    s.control = control_map()
    M = "material/%s/op/%d/constant/constant"
    CC = {"attackMs": 120, "decayMs": 120}
    # NIGHT and GOLDEN: the palette's two related ends (a time of day, not a colour cycle)
    palette_routes(s, progs, "control.night", "midnight", dict(attackMs=250, decayMs=250))
    palette_routes(s, progs, "control.golden", "golden", dict(attackMs=250, decayMs=250))
    for node in SEA_NODES:
        D = "procedural/%s/deform/%%d/amount" % node
        s.route(R("control.swell", D % 1, 1.1, **CC), R("control.swell", D % 3, 0.5, **CC),
                R("control.chop", D % 5, 0.16, **CC),
                R("control.tide", D % 7, 1.0, op="multiply", gain=1.6, offset=0.4, **CC))
    s.route(R("control.chop", M % (sea.name, sea["glint"]), 1.0, op="multiply", gain=0.8, offset=1.0, **CC))
    # a taller sea needs a taller crest to foam (as with the level): the crest ramp follows the swell control
    s.route(R("control.swell", "material/doSea/op/%d/gradient/constant" % sea["crest"], 1.0, op="multiply",
              gain=-0.42, offset=1.0, **CC))
    s.route(R("control.foam", "material/doSea/op/%d/smoothstep/constant" % sea["foamMask"], -0.16, comp=0, **CC),
            R("control.foam", "material/doSea/op/%d/smoothstep/constant" % sea["foamMask"], -0.1, comp=1, **CC),
            R("control.foam", M % (sea.name, sea["foam"]), 1.0, op="multiply", gain=0.6, offset=1.0, **CC))
    for prog, key in ((sky, "halo"), (sky, "glow"), (sea, "pathCol"), (seafar, "pathCol")):
        s.route(R("control.sun", M % (prog.name, prog[key]), 1.0, op="multiply", gain=0.9, offset=1.0, **CC))
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
        s.route(R("control.padFlare", M % (prog.name, prog[key]), 1.0, op="multiply", gain=g, offset=1.0, **flare))
    s.route(R("control.padSpray", "particles/snareSpray/burst", 420.0))


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
    sea, seafar, sky = sea_program(), sea_program(far=True), sky_program()
    audio_fields(s)
    s.program(sea.doc(emission=5))
    s.program(seafar.doc(emission=5))
    if DEBUG_SKY:
        sky.ops = sky.ops[:DEBUG_SKY[0]]
        s.program(sky.doc(emission=DEBUG_SKY[1]))
    else:
        s.program(sky.doc(emission=5))
    s.proc("dome", {"kind": "mesh", "asset": "meshes/do-dome.glb"}, material=dict(unlit("doSky"), doubleSided=True))
    for name, *_ in SEA_PIECES:
        s.proc("sea_" + name, {"kind": "mesh", "asset": "meshes/do-sea-%s.glb" % name},
               material=unlit("doSea" if name == "near" else "doSeaFar"), deformers=sea_deformers() + audio_deformers())
    # the clouds: one node of puffs, turned slowly round the viewer by a world-space twist (seamless, and running with
    # no transport)
    cp = cloud_program()
    if DEBUG_CLOUD:
        cp.ops = cp.ops[:DEBUG_CLOUD[0]]
    s.program(cp.doc(emission=DEBUG_CLOUD[1] if DEBUG_CLOUD else 3))
    s.proc("clouds", {"kind": "sphere", "radius": 1.0, "segments": 20, "rings": 12},
           distribution={"kind": "points", "points": cloud_points()}, material=unlit("doCloud"),
           deformers=[{"kind": "twist", "amount": 0.0, "speed": CLOUD_DRIFT, "phase": 0.0, "axis": [0.0, 1.0, 0.0],
                       "center": [0.0, 0.0, 0.0], "space": "world"}])
    # the island: a flat silhouette in the horizon's haze
    island_meshes()
    a = math.radians(ISLE_AZ)
    s.proc("isle", {"kind": "mesh", "asset": "meshes/do-isle.glb"},
           material={"baseColor": [0.0, 0.0, 0.0], "emissiveColor": pal(PALETTE, "isle"), "emissiveIntensity": 1.0,
                     "roughness": 1.0, "metallic": 0.0, "unlit": True},
           transform={"position": [round(ISLE_DIST * math.sin(a), 2), 0.0, round(-ISLE_DIST * math.cos(a), 2)],
                      "rotation": [0.0, 20.0, 0.0], "scale": [1.7, 0.95, 1.0]})
    dp = dolphins(s)
    spray(s)
    instrument(s, sea, seafar, sky)
    midi(s, [sea, seafar, sky, cp, dp], sea, seafar, sky)
    if REVIEW_MIDI:
        score_midi(s, REVIEW_MIDI)
    camera(s)
    s.params_({"post/bloom/intensity": 0.22, "post/bloom/threshold": 1.4, "post/bloom/emissionWeight": 1.0,
               "post/tonemap/operator": 4, "post/output/vignette": 0.25, "post/output/grain": 0.008})
    return s

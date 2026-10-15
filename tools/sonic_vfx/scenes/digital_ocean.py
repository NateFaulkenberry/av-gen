"""10. DIGITAL OCEAN: THE TIDEWRITER (docs/prototypes/digital-ocean/00-brief.md; plan 01-plan.md, log PROGRESS.md).

A stylized ocean at the low sun, seen from a composed, nearly still camera. Broad swells roll in from under the sun
toward the viewer; the music is written into the sea. (Stage 1: the graybox.)
"""
import math
import os

from .. import kit
from .. import meshes as mx
from ..kit import R, hexrgb

ID = "digital-ocean"
TITLE = "Digital Ocean"

MESH_DIR = os.path.join(kit.ABSTRACT_DIR, "meshes")

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


def generate_meshes():
    yaw = COMPOSITIONS[COMPOSITION][3]
    out = {}
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


STEEP = (0.5, 0.35, 0.25)
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
PALETTES = {
    "golden": dict(zenith="#203b78", mid="#9a86b4", warm="#ffc489", cool="#e8949a", sun="#fff3d6", glow="#ffa15e",
                   deep="#0b2453", rzen="#1c3a74", rhor="#d9988e", sss="#2fb3a3", glint="#fff0c8", foam="#fff1d8"),
    "twilight": dict(zenith="#100d36", mid="#563a8a", warm="#ff9b7d", cool="#8a6cc2", sun="#ffe2cc", glow="#ff7f68",
                     deep="#0a0b35", rzen="#191c58", rhor="#8a6cbd", sss="#18cfe0", glint="#ffd6c2", foam="#bdfde6"),
    "midnight": dict(zenith="#03050f", mid="#17123c", warm="#3b2b6a", cool="#1d2150", sun="#e9f0ff", glow="#5d72c8",
                     deep="#02060f", rzen="#050a1c", rhor="#2a285c", sss="#10f0a8", glint="#cfe0ff", foam="#7affe0"),
    "tropical": dict(zenith="#1546a8", mid="#6aa6de", warm="#ffb04a", cool="#ff8fae", sun="#fff6d2", glow="#ff9a3a",
                     deep="#003d63", rzen="#0b5a8a", rhor="#ffae8a", sss="#22e0c0", glint="#fff4c4", foam="#fffbea"),
}
PALETTE = "twilight"
DEBUG_SKY = None
BANDS = 0


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
        P.op(None, "input", 2, inp="localPosition")
        P.op("shimmer", "noise", 2, a=2, f=SHIMMER_FREQ, k=[0.0, 0.0, 0.0], seed=11)
        P.op("shimmerAmp", "remap", 2, a=2, k=[0.3, 0.7, -SHIMMER_AMP, SHIMMER_AMP])
        P.op(None, "input", 3, inp="cameraDistance")                     # fade them out by the seam at 200 m
        P.op("shimmerFade", "remap", 3, a=3, k=[SHIMMER_NEAR, SHIMMER_FAR, 1.0, 0.0], f=1.0)
        P.op(None, "multiply", 2, a=2, b=3)
        P.op(None, "constant", 3, k=[1.0, 0.0, 0.55, 0.0])
        P.op(None, "multiply", 2, a=2, b=3)
        P.op(None, "add", 0, a=0, b=2)
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
    P.colour("rhor", 4, pal(pn, "rhor"))
    P.colour("rzen", 5, pal(pn, "rzen"))
    P.op("refl", "mixBy", 4, a=4, b=5, c=3)
    # the sun on the water: by r.l
    P.op(None, "gradient", 5, a=0, k=[sx, sy, sz, 0.0], f=1.0)           # n.l
    P.op(None, "multiply", 5, a=5, b=2)
    P.op("back", "gradient", 6, a=1, k=[-0.5 * sx, -0.5 * sy, -0.5 * sz, 0.0], f=1.0)   # -v.l / 2: toward the sun
    P.op("rl", "add", 5, a=5, b=6)                                       # r.l / 2
    P.op("path", "smoothstep", 7, a=5, k=[PATH_COS / 2.0, 0.5])
    P.op(None, "power", 7, a=7, f=PATH_SHAPE)
    P.colour("pathCol", 3, pal(pn, "glow"), PATH_GAIN)
    P.op(None, "multiply", 7, a=7, b=3)
    P.op(None, "add", 4, a=4, b=7)
    P.op("glints", "smoothstep", 5, a=5, k=[GLINT_COS0 / 2.0, GLINT_COS1 / 2.0])
    P.colour("glint", 3, pal(pn, "glint"), GLINT_GAIN)
    P.op(None, "multiply", 5, a=5, b=3)
    P.op("sunlit", "add", 4, a=4, b=5)
    # the water's body where the eye looks into a face, the reflection at grazing angles
    P.colour("deep", 5, pal(pn, "deep"))
    P.op("fres", "fresnel", 7, f=FRESNEL_POWER)
    P.op("fresMin", "remap", 7, a=7, k=[0.0, 1.0, FRESNEL_MIN, 1.0], f=1.0)
    P.op("water", "mixBy", 5, a=5, b=4, c=7)
    # light through the backlit crests: crest height x toward the sun
    P.op(None, "power", 6, a=6, f=SSS_FOCUS)                             # (-v.l / 2)^focus: toward the sun
    P.op("wp", "input", 7, inp="worldPosition")
    P.op("crest", "gradient", 7, a=7, k=[0.0, 1.0 / (CREST_HI - CREST_LO), 0.0, -CREST_LO / (CREST_HI - CREST_LO)],
         f=1.0)
    P.op(None, "multiply", 6, a=6, b=7)
    P.colour("sss", 3, pal(pn, "sss"), 2.0 ** SSS_FOCUS * SSS_GAIN)
    P.op(None, "multiply", 6, a=6, b=3)
    if far:
        P.op(None, "input", 7, inp="cameraDistance")
        P.op("crestFade", "remap", 7, a=7, k=[FAR_FLAT0, FAR_FLAT1, 1.0, 1.0 - FAR_FLAT_MAX], f=1.0)
        P.op(None, "multiply", 6, a=6, b=7)
    P.op("lit", "add", 5, a=5, b=6)
    return P


REFL_SPAN = 0.3             # r.y / 2 over which the reflection runs from the horizon's colour to the zenith's
PATH_COS = math.cos(math.radians(14.0))
PATH_SHAPE = 3.0
PATH_GAIN = 0.55
GLINT_COS0, GLINT_COS1 = math.cos(math.radians(2.6)), math.cos(math.radians(0.9))
GLINT_GAIN = 2.4
SHIMMER_FREQ = 0.55
SHIMMER_AMP = 0.1
SHIMMER_NEAR, SHIMMER_FAR = 50.0, 200.0
FAR_FLAT0, FAR_FLAT1, FAR_FLAT_MAX = 200.0, 700.0, 0.85
FRESNEL_POWER = 3.0
FRESNEL_MIN = 0.2
CREST_LO, CREST_HI = 0.3, 1.9
SSS_GAIN = 0.9
SSS_FOCUS = 3.0


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
    P.colour("cool", 3, pal(pn, "cool"))
    P.colour("warm", 4, pal(pn, "warm"))
    P.op("horizon", "mixBy", 3, a=3, b=4, c=2)
    P.op(None, "remap", 4, a=1, k=[0.004, 0.075, 0.0, 1.0], f=1.0)
    P.op(None, "power", 4, a=4, f=0.8)
    P.colour("mid", 5, pal(pn, "mid"))
    P.op(None, "mixBy", 5, a=3, b=5, c=4)
    P.op(None, "remap", 4, a=1, k=[0.05, 0.26, 0.0, 1.0], f=1.0)
    P.colour("zenith", 6, pal(pn, "zenith"))
    P.op("sky", "mixBy", 5, a=5, b=6, c=4)
    # the sun
    P.op("cosSun", "gradient", 4, a=0, k=[-sx, -sy, -sz, 0.0], f=1.0)
    P.op("glowPow", "power", 6, a=4, f=28.0)                            # the broad glow
    P.colour("glow", 7, pal(pn, "glow"), 0.55)
    P.op(None, "multiply", 6, a=6, b=7)
    P.op(None, "add", 5, a=5, b=6)
    P.op("haloPow", "power", 6, a=4, f=900.0)                           # the halo round the disc
    P.colour("halo", 7, pal(pn, "glow"), 1.6)
    P.op(None, "multiply", 6, a=6, b=7)
    P.op(None, "add", 5, a=5, b=6)
    P.op("disc", "smoothstep", 6, a=4, k=[SUN_COS - 0.00012, SUN_COS + 0.00004, 0.0, 0.0])
    P.colour("sun", 7, pal(pn, "sun"), 7.0)
    P.op(None, "multiply", 6, a=6, b=7)
    P.op(None, "add", 5, a=5, b=6)
    # below the horizon: the far water
    P.op(None, "threshold", 6, a=1, f=0.0)
    P.colour("far", 7, pal(pn, "rhor"))
    P.op(None, "mixBy", 5, a=7, b=5, c=6)
    return P


SUN_RADIUS_DEG = 1.7
SUN_COS = math.cos(math.radians(SUN_RADIUS_DEG))


# ================================================================================================ build
def unlit(program):
    return {"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [0.0, 0.0, 0.0], "emissiveIntensity": 0.0,
            "roughness": 1.0, "metallic": 0.0, "unlit": True, "program": program}


def camera(s):
    h, focal, pitch, yaw = COMPOSITIONS[COMPOSITION]
    p, y = math.radians(pitch), math.radians(yaw)
    pos = [0.0, h, 0.0]
    fwd = (math.sin(y) * math.cos(p), math.sin(p), -math.cos(y) * math.cos(p))
    tgt = [round(pos[k] + 100.0 * fwd[k], 4) for k in range(3)]
    s.camera = {"mode": 1, "position": pos, "target": tgt, "fov": 40.0, "orbitSpeed": 0.0}
    s.params_({"camera/lens/focalLength": focal})


def build():
    generate_meshes()
    s = kit.Scene(ID, TITLE, {"thesis": "Digital Ocean", "vocabulary": []})
    s.unshadowed_key()          # every material is unlit: a casting default key would be pure cost
    s.environment = {
        "intensity": 0.0, "background": pal(PALETTE, "warm"), "fogColor": pal(PALETTE, "warm"), "volumeDensity": 0.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": pal(PALETTE, "zenith"), "horizonColor": pal(PALETTE, "warm"),
                "groundColor": pal(PALETTE, "rhor"), "haze": 0.3, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    sea, seafar, sky = sea_program(), sea_program(far=True), sky_program()
    s.program(sea.doc(emission=5))
    s.program(seafar.doc(emission=5))
    if DEBUG_SKY:
        sky.ops = sky.ops[:DEBUG_SKY[0]]
        s.program(sky.doc(emission=DEBUG_SKY[1]))
    else:
        s.program(sky.doc(emission=5))
    s.proc("dome", {"kind": "sphere", "radius": 1000.0, "segments": 64, "rings": 32},
           material=dict(unlit("doSky"), doubleSided=True),
           transform={"position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [8.0, 8.0, 8.0]})
    for name, *_ in SEA_PIECES:
        s.proc("sea_" + name, {"kind": "mesh", "asset": "meshes/do-sea-%s.glb" % name},
               material=unlit("doSea" if name == "near" else "doSeaFar"), deformers=sea_deformers())
    camera(s)
    s.params_({"post/bloom/intensity": 0.22, "post/bloom/threshold": 1.4, "post/bloom/emissionWeight": 1.0,
               "post/tonemap/operator": 4, "post/output/vignette": 0.25, "post/output/grain": 0.008})
    return s

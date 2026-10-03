"""STELLAR NURSERY (cosmic). Inside a pillar of gas, chords ignite newborn stars: their number is the polyphony, their
colour the pitch, and the bass is the density of the cloud.

Composition (SCENE-CATALOG.md #9), after the Pillars of Creation: three columns of dark dust stand against a glowing
teal-and-rust nebula, lit from above by stars out of frame, so each pillar is a dark mass with a bright magenta rim.
The tallest stands on the left third; its head holds seven embryonic stars, and a chord lights as many of them as it
has voices, coloured by its pitch (red low, blue-white high). Dark dust drifts out of focus close to the lens; the
camera slides sideways so the pillars separate in depth. (The Cosmic Ocean the catalog first named was removed,
ADR-441: the nebula is a matte-painted emissive wall, and the pillars are SDF.)
"""
import math

from .. import kit, signals
from ..kit import (R, M, hexrgb, scale3, sd_sphere, sd_capsule, sd_smooth, sd_union, sd_move, sd_rot, sd_noise,
                   sd_scale, SLOW, MEDIUM, FAST, HIT, SNAP, VERY_SLOW)

ID = "stellar-nursery"
TITLE = "Stellar Nursery"

TEAL = "#1f6f73"
RUST = "#8a4a24"
MAGENTA = "#ff4fa0"
STAR = "#bfe0ff"

N_STARS = 7
HEAD = (-14.0, 26.0, -60.0)     # the tallest pillar's head (the stars' nest)

DESIGN = {
    "category": "cosmic",
    "thesis": "Inside a pillar of gas, chords ignite newborn stars: their number is the polyphony, their colour the "
              "pitch, and the bass is the density of the cloud.",
    "composition": {
        "background": "a glowing teal and rust nebula wall",
        "midground": "three dark dust pillars with magenta rims, the tallest on the left third",
        "foreground": "dark dust drifting out of focus close to the lens",
        "focal": "the tallest pillar's head and the embryonic stars in it",
        "secondary": ["the two farther pillars", "the shock front a kick sends through the gas", "the starfield"],
        "atmosphere": "the nebula's glow; out-of-focus dust",
        "post": "bloom on the stars, a slight halation, a cool grade with warm dust",
        "camera": "a slow sideways slide (parallax between the pillars)",
    },
    "palette": {"dominant": TEAL, "secondary": RUST, "accent": MAGENTA, "highlight": STAR,
                "background_value": "mid-dark (the nebula)",
                "saturation": "the rims and the stars; the nebula is muted"},
    "motion": {
        "very_slow": ["the camera's slide", "the nebula's churn"],
        "medium": ["the stars' growth (held notes)", "the cloud's density (bass)"],
        "fast": ["stars igniting (chords)"],
        "extremely_fast": ["the kick's shock front", "twinkle (hat)"],
    },
    "vocabulary": [
        ["polyphony", "notes.polyphony", "a chord lights as many embryonic stars as it has voices"],
        ["pitch", "notes.lastPitch", "the stars' temperature: red for low notes, blue-white for high"],
        ["note duration", "notes.held", "a held note's stars grow"],
        ["note", "response.note", "the stars flare as they ignite"],
        ["bass", "response.bass", "the cloud's density and glow"],
        ["kick", "response.kick", "a shock front through the gas"],
        ["hat", "response.hat", "the starfield twinkles"],
        ["sustained", "response.sustain", "ionisation: the pillars' rims brighten"],
        ["tension", "notes.tension", "the cloud churns"],
        ["silence", "(no input)", "dark pillars on the glowing gas; the stars unlit"],
    ],
    "tier": "medium: three SDF pillars (one raymarch), an emissive wall, a starfield",
}


def nebula_program():
    """The nebula wall: warped fBm through a teal-rust ramp, cut by dust lanes; OP_CHURN's constant offsets the warp
    (an integrated phase, so the churn never jumps). The material's emissive intensity scales it."""
    return {
        "name": "snNebula",
        "ops": [
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "noise", "dst": 1, "srcA": 0, "value": 0.012, "seed": 21, "constant": [0.0, 0.0, 0.0, 0.0]},
            {"kind": "constant", "dst": 2, "constant": [60.0, 80.0, 40.0, 0.0]},
            {"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 2},
            {"kind": "add", "dst": 1, "srcA": 0, "srcB": 1},                              # a warped point
            {"kind": "noise", "dst": 3, "srcA": 1, "value": 0.008, "seed": 5},           # the gas
            {"kind": "noise", "dst": 4, "srcA": 1, "value": 0.03, "seed": 9},            # the dust lanes
            {"kind": "smoothstep", "dst": 3, "srcA": 3, "constant": [0.35, 0.78, 0.0, 0.0]},
            {"kind": "smoothstep", "dst": 4, "srcA": 4, "constant": [0.4, 0.62, 0.0, 0.0]},
            {"kind": "remap", "dst": 4, "srcA": 4, "value": 1, "constant": [0.0, 1.0, 1.0, 0.5]},
            {"kind": "multiply", "dst": 5, "srcA": 3, "srcB": 4},
            {"kind": "ramp", "dst": 6, "srcA": 5, "constant": hexrgb("#020506") + [1.0],
             "constant2": hexrgb(TEAL, 0.45) + [1.0], "constant3": hexrgb("#b07048", 0.6) + [1.0]},
            {"kind": "input", "dst": 7, "input": "materialEmission"},
            {"kind": "swizzle", "dst": 7, "srcA": 7, "constant": [0.0, 0.0, 0.0, 0.0]},
            {"kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7},
            {"kind": "constant", "dst": 0, "constant": [0.0, 0.0, 0.0, 1.0]},
        ],
        "baseColor": 0, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 1.0, "opacity": -1,
    }


OP_CHURN = 2


def pillar(base, height, r0, lean, seed, head=None):
    """A column of dust: a broad flared foot, a trunk thinning upward through stacked lobes that wander a little off
    the axis, a ragged head; a cloudy skin of two noise octaves (coarse lumps, fine curdling)."""
    parts = [sd_move((base[0], base[1] + height * 0.06, base[2]), sd_sphere(r0 * 1.5))]      # the foot
    n = 6
    for i in range(n):
        u = (i + 1) / n
        r = r0 * (1.05 - 0.6 * u)
        y = height * (0.08 + 0.8 * u)
        x = lean * u * u + r0 * 0.25 * math.sin(seed + i * 1.7)
        z = r0 * 0.2 * math.cos(seed * 0.7 + i * 2.3)
        parts.append(sd_move((base[0] + x, base[1] + y, base[2] + z), sd_capsule(r, height * 0.18)))
    if head:
        parts.append(sd_move(head, sd_sphere(r0 * 0.58)))
    body = sd_smooth(r0 * 0.55, *parts)
    return sd_noise(r0 * 0.38, 0.2 / r0 * 3.0, body, seed=seed)   # one octave: the second doubled the march cost


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.6, "attack": 1.2, "release": 1.6}
    s.environment = {
        "intensity": 0.0, "background": hexrgb("#020406"), "fogColor": [0, 0, 0], "volumeDensity": 0.0,
        "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": hexrgb("#020406"), "horizonColor": hexrgb("#03070a"),
                "groundColor": hexrgb("#020305"), "haze": 0.5, "sunIntensity": 0.0, "intensity": 1.0,
                "background": True, "useKeyLight": False},
    }
    s.composition = {
        "focalPoints": [{"name": "nest", "position": list(HEAD), "radius": 5.0, "weight": 1.0}],
        "layers": [{"name": "dust", "start": 0.0, "end": 20.0, "contrast": 0.8, "saturation": 0.8},
                   {"name": "pillars", "start": 20.0, "end": 160.0, "contrast": 1.0, "saturation": 1.0},
                   {"name": "nebula", "start": 160.0, "end": 2000.0, "contrast": 0.8, "saturation": 0.85}],
    }
    s.program(nebula_program())

    # ---- the nebula wall, far behind
    s.proc("nebula", {"kind": "box", "size": [1000.0, 700.0, 1.0], "subdivisions": 1},
           material={"baseColor": [0, 0, 0], "emissiveColor": [1, 1, 1], "emissiveIntensity": 1.0, "roughness": 1.0,
                     "metallic": 0.0, "program": "snNebula", "unlit": True},
           transform={"position": [0.0, 120.0, -420.0], "rotation": [0, 0, 0], "scale": [1, 1, 1]})
    s.effect("stars", "stars", ("world",), parameters={"brightness": 0.8, "density": 0.25, "magnitudeSlope": 6.5,
                                                        "colorSpread": 0.6, "twinkle": 0.15, "twinkleRate": 2.0,
                                                        "horizonFade": 0.0, "band": 0.0, "daylight": 0.0})

    # ---- the pillars (one SDF): the tallest on the left with the nest; two farther ones on the right
    tall = pillar((-15.0, -20.0, -60.0), 46.0, 6.0, 1.5, 3, head=(HEAD[0], HEAD[1] - 1.0, HEAD[2]))
    mid = pillar((14.0, -24.0, -92.0), 36.0, 5.0, -2.5, 7)
    far = pillar((34.0, -26.0, -130.0), 30.0, 4.2, 1.0, 11)
    # The pillars are static, so they are a surface-nets MESH (raymarched at full resolution every frame they cost
    # 40 ms at 1080p; meshed once at load they are a few hundred thousand triangles). The embryonic stars, which the
    # music moves, are separate spheres at the head's skin.
    root = sd_union(tall, mid, far)
    s.sdf("pillars", root, (-30.0, -30.0, -140.0), (44.0, 40.0, -50.0),
          material={"baseColor": hexrgb("#2a160d"), "emissiveColor": [0, 0, 0], "emissiveIntensity": 0.0,
                    "roughness": 0.95, "metallic": 0.0},
          look={"aoStrength": 0.6, "aoDistance": 3.0}, max_steps=96, epsilon=0.002, step_scale=0.9,
          max_distance=400.0, mesh=128)
    for k in range(N_STARS):
        a = math.radians(-70.0 + 140.0 * k / (N_STARS - 1))
        p = (HEAD[0] + 3.6 * math.sin(a), HEAD[1] + 1.2 + 1.6 * math.cos(a * 1.7), HEAD[2] + 3.6 * math.cos(a) + 0.6)
        s.proc("star%d" % k, {"kind": "sphere", "radius": 0.75, "segments": 24, "rings": 12},
               material={"baseColor": [0, 0, 0], "emissiveColor": [0, 0, 0], "emissiveIntensity": 1.0,
                         "roughness": 1.0, "metallic": 0.0, "unlit": True},
               transform={"position": list(p), "rotation": [0, 0, 0], "scale": [1, 1, 1]})

    # ---- light: hot stars above and behind (the rims), a dim teal fill from the nebula, a light in the nest
    # two hot stars above and behind, left and right: the pillars' tops and edges catch them as magenta rims
    s.light("rim", "directional", direction=[0.12, -0.45, 0.885], color=hexrgb(MAGENTA), intensity=14.0,
            castsShadow=False)
    s.light("rim2", "directional", direction=[-0.55, -0.3, 0.78], color=hexrgb("#ff7ab8"), intensity=6.0,
            castsShadow=False)
    s.light("nebulaFill", "directional", direction=[0.0, 0.1, 1.0], color=hexrgb(TEAL), intensity=0.6,
            castsShadow=False)
    s.light("nest", "point", position=[HEAD[0], HEAD[1] + 1.5, HEAD[2] + 4.5], color=hexrgb(STAR), intensity=0.0,
            range=24.0, radius=2.0, castsShadow=False, volumetric=0.0)

    # ---- camera: a slow sideways slide (parallax)
    s.camera["fov"] = 40.0
    s.track("camera/position", [{"time": 0.0, "value": [-6.0, 6.0, 30.0], "interp": "smooth"},
                                {"time": 60.0, "value": [6.0, 7.0, 30.0], "interp": "smooth"},
                                {"time": 120.0, "value": [-6.0, 6.0, 30.0], "interp": "smooth"}], loop=120.0)
    s.track("camera/target", [{"time": 0.0, "value": [4.0, 14.0, -70.0], "interp": "smooth"},
                              {"time": 60.0, "value": [10.0, 14.5, -70.0], "interp": "smooth"},
                              {"time": 120.0, "value": [4.0, 14.0, -70.0], "interp": "smooth"}], loop=120.0)
    s.camera = {"mode": 1, "position": [-6.0, 6.0, 30.0], "target": [4.0, 14.0, -70.0], "fov": 40.0,
                "orbitSpeed": 0.0}

    # ---- the instrument ---------------------------------------------------------------------------------------------
    # chords: star k is lit when the polyphony reaches it; pitch colours them (red low, blue-white high)
    s.map(M("pc", [("lastPitch", 1.0)], "mean", -0.3 / 0.4, 1.0 / 0.4))
    red, blue = hexrgb("#ff6a3a", 1.0), hexrgb("#a8d4ff", 1.0)
    lit = []
    for k in range(N_STARS):
        thr = (k + 0.5) / (N_STARS + 1.0)
        s.map(M("lit%d" % k, [("polyphony", 1.0)], "mean", -thr * 14.0 + 0.5, 14.0))
        lit.append("visual.lit%d" % k)
    for k in range(N_STARS):
        # the star's colour: red for low pitches, blue-white for high (an emissive colour in 0..1 per channel);
        # its brightness: lit by the chord's voice count, flaring on each note; a held note swells it
        for c in range(3):
            s.route(R("visual.pc", "procedural/star%d/material/emissiveColor" % k, blue[c] - red[c], comp=c,
                      op="add", attackMs=30, decayMs=900))
        s.param("procedural/star%d/material/emissiveColor" % k, red)
        s.route(R(lit[k], "procedural/star%d/material/emissive" % k, 9.0, attackMs=30, decayMs=900),
                R("noteEnv", "procedural/star%d/material/emissive" % k, 6.0, depth=lit[k], attackMs=0, decayMs=350),
                R("held", "procedural/star%d/transform/scale" % k, 0.6, springHz=0.8, springDamping=0.8))
    s.route(R("polyphony", "lights/nest/intensity", 400.0, attackMs=30, decayMs=900))
    # bass: the cloud's density and glow; sustain: ionisation (the rims); tension: the cloud churns (a phase)
    s.route(R("bass", "procedural/nebula/material/emissive", 1.2, attackMs=50, decayMs=1400),
            R("sustain", "lights/rim/intensity", 8.0, **SLOW),
            R("tension", "material/snNebula/op/%d/noise/constant" % OP_CHURN, 0.12, comp=0, integrate=True,
              attackMs=500, decayMs=500))
    # kick: a shock front from the nest; hat: twinkle
    shock_uv = [round(v, 3) for v in s.project(list(HEAD))[:2]]
    s.route(R("kickEnv", "post/shock/amount", 40.0, attackMs=0, decayMs=0),
            R("kickEnv", "post/shock/radius", -1.6, op="replace", offset=-1.0, attackMs=0, decayMs=0),
            R("hat", "fx/stars/twinkle", 1.5, attackMs=0, decayMs=120),
            R("noteEnv", "post/bloom/intensity", 0.12, attackMs=0, decayMs=300))

    s.params_({
        "post/bloom/intensity": 0.42, "post/bloom/threshold": 0.9, "post/halation/enabled": True,
        "post/halation/intensity": 0.06, "post/output/vignette": 0.45, "post/output/grain": 0.018,
        "post/grade/contrast": 1.08, "post/grade/temperature": -0.02, "post/tonemap/operator": 3,
        "post/shock/amount": 0.0, "post/shock/radius": 1.6, "post/shock/width": 0.1, "post/shock/chroma": 0.3,
        "post/shock/centerX": shock_uv[0], "post/shock/centerY": shock_uv[1],
        "post/dof/enabled": True, "post/dof/physical": True, "camera/lens/aperture": 2.0,
        "camera/focus/mode": 0, "camera/lens/focusDistance": 92.0,
        "camera/lens/focalLength": 35.0,
    })

    # ---- the evaluator's screen regions, projected through the camera at t = 0
    s.region("nest", centre=list(HEAD), radius=6.0)
    s.region_points("pillars", [[-21.0, -20.0, -60.0], [40.0, -26.0, -130.0], [-21.0, 28.0, -60.0],
                                [40.0, 6.0, -130.0]])
    s.region("nebula", box=[0.45, 0.0, 1.0, 0.45])
    return s

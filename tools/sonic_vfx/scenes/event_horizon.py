"""EVENT HORIZON (cosmic). A black hole is eating a star: the star is stretched into a stream of plasma that winds
round the hole and pours into a Doppler-bright disk, every note tears another fragment off it, and the bass is the
hole's own mass bending the sky.

Art pass 2026-10 (docs/prototypes/sonic-garden/ART-PASS-2026-10-event-horizon.md). The first pass was a disk round a
lens: Gargantua as a poster, with nothing happening to anything. The direction now is a tidal disruption event
(Rees 1988; the NASA Goddard and ESO visualisations) seen through Interstellar's lens (James, von Tunzelmann, Franklin
and Thorne 2015):
- the SHADOW is large and clean, ringed by a razor photon ring that the lensed far side of the disk hugs;
- the DISK is Doppler-beamed, as the film's physics wanted and the film left out: the side turning toward the camera
  (left, sweeping into the frame) blazes blue-white, the receding side (right) sinks to deep red;
- the VICTIM is a blue-white star on an inclined orbit, stretched along its path (spaghettification), bleeding a
  stream of plasma that spirals in and heats from star-white to the disk's gold; once per orbit it passes behind the
  hole and the lens smears it into an arc;
- the WRECKAGE of its planets tumbles round the hole, scanned rock lit by the disk from inside;
- the CHAOS is relativistic jets, flares from the hot spot, sparks, and a shock ripple on the kick.
"""
import math

from .. import kit
from ..kit import R, M, hexrgb, scale3, FAST, HIT, SNAP, MEDIUM, SLOW

ID = "event-horizon"
TITLE = "Event Horizon"

# palette (sRGB hex; kit.hexrgb converts to linear)
VOID = "#03030a"
EMBER = "#ff7a2a"
DEEP_RED = "#6a1206"
GOLD = "#ffc77a"
WHITE_HOT = "#fff3dc"
STAR_BLUE = "#bcd2ff"
JET_BLUE = "#6aa8ff"
NEB_TEAL = "#0e2c44"

DESIGN = {
    "category": "cosmic",
    "thesis": "A black hole is eating a star: the star is stretched into a stream of plasma that pours into a "
              "Doppler-bright disk, every note tears another fragment off it, and the bass is the hole's own mass "
              "bending the sky.",
    "composition": {
        "background": "the galaxy's band of stars over a deep indigo-to-teal sky, lensed into arcs round the shadow",
        "midground": "the accretion disk on a shallow diagonal, its approaching (left) side blazing blue-white and "
                     "its receding (right) side deep red; its far side lensed up over the shadow and under it",
        "foreground": "tumbling wreckage of shattered planets, scanned rock rimmed by the disk",
        "focal": "the shadow and its razor photon ring, on the right third",
        "secondary": ["the victim star, stretched along its orbit, and the plasma stream it bleeds into the disk",
                      "relativistic jets from the poles", "note fragments spiralling in", "flares off the hot spot"],
        "atmosphere": "none (vacuum): glowing gas and dust only",
        "post": "bloom and warm halation on the inner disk, a whisper of anamorphic streak, edge chromatic "
                "aberration, grain, a heavy vignette",
        "camera": "a 32-degree arc swept there and back over 160 s, a few degrees above the disk plane",
    },
    "palette": {"dominant": VOID, "secondary": EMBER, "accent": STAR_BLUE, "highlight": WHITE_HOT,
                "background_value": "very dark (L about 0.03)",
                "saturation": "highest in the disk; the star and the jets the cold counterweight"},
    "motion": {
        "very_slow": ["the camera's arc", "the lensed sky drifting", "the wreckage's orbits (80 to 160 s)"],
        "medium": ["the star's orbit (40 s), passing behind the hole once a lap", "the disk's Keplerian shear",
                   "the plasma stream spiralling in"],
        "fast": ["note fragments torn off the star, falling in over 3-4 s", "the snare's flare"],
        "extremely_fast": ["the photon ring flash and shock (kick)", "sparks in the disk (hat)", "the lens chroma"],
    },
    "vocabulary": [
        ["sustained", "response.sustain", "the disk heats: every band brighter, the gas denser and whiter"],
        ["melodic", "response.note", "each note tears a clump of fragments off the star (it flares); the pitch sets "
         "how it falls: a low note swings wide, a high note plunges steeply, trailing light"],
        ["velocity", "notes.lastVelocity", "how many fragments and how bright"],
        ["polyphony", "notes.polyphony", "a chord tears off a cluster: the birthplace spreads"],
        ["bass", "response.bass", "the hole's mass: the lens and the shadow swell, the sky bends more, and the star "
         "flares and bleeds faster into its stream"],
        ["kick", "response.kick", "the photon ring flashes, the jets fire and a shock ripple races out from the hole"],
        ["snare", "response.snare", "a flare erupts from the hot spot on the approaching side, and the star sheds a "
         "burst of plasma"],
        ["hat", "response.hat", "sparks glint across the disk"],
        ["density", "notes.density", "busy playing churns the gas (turbulence, finer streaks)"],
        ["tension", "notes.tension", "dissonance splits the lens's colours"],
        ["brightness", "sonic.brightness", "a bright timbre turns the inner edge from gold to blue-white"],
        ["silence", "(no input)", "the hole keeps eating: the star circles and bleeds, the disk turns, the jets "
         "whisper"],
    ],
    "tier": "live: no fog march; one DF lens, two lights, scanned debris, ten particle systems",
}

BH = (0.0, 0.0, 0.0)
# The disk: fourteen thin flat bands from the innermost stable orbit (3 m) to 15 m, touching edge to edge, each turned
# at its own Keplerian rate (a twist with speed only; ADR-1021 made a full turn safe), so the gas shears as it
# turns. A positive twist about +Y carries +x toward -z: the RIGHT side recedes from the camera (on +z) and the LEFT
# approaches. Every particle system's orbit force is negative to turn the same way (the particle shader's tangent is
# cross(up, toward-the-attractor), which at +x points to +z).
R_IN, R_OUT, N_BANDS = 3.0, 15.0, 14
SPIN_IN = 46.0  # degrees per second at the inner edge; r^-1.5 outward
ORBIT_SIGN = -1.0


def bands():
    step = (R_OUT - R_IN) / N_BANDS
    out = []
    for i in range(N_BANDS):
        r = R_IN + step * (i + 0.5)
        out.append((r, step * 0.5, SPIN_IN * (R_IN + step * 0.5) ** 1.5 / r ** 1.5))
    return out


def disk_program(r_max=R_OUT, angular=1.7, radial=11.0):
    """Polar streaks, Doppler-beamed. The angle has no op, so the unit direction (x/r, z/r) stands for it: noise of
    (A x/r, B r/Rmax, A z/r) varies slowly round the circle and fast across the radius. The beaming reads the WORLD
    position (fixed in space while the bands turn): D is 1 on the approaching left, 0 on the
    receding right (steepened: D = saturate(0.5 - 1.1 x/r), so each half reads as a whole), and a three-stop ramp dims and reddens the right and brightens and blues the left."""
    hot, mid, cool = hexrgb(WHITE_HOT, 1.6), hexrgb(GOLD, 0.8), hexrgb(EMBER, 0.32)
    return {
        "name": "ehDisk",
        "ops": [
            {"kind": "input", "dst": 0, "input": "localPosition"},
            {"kind": "multiply", "dst": 1, "srcA": 0, "srcB": 0},
            {"kind": "gradient", "dst": 2, "srcA": 1, "value": 1.0 / (r_max * r_max), "constant": [1.0, 0.0, 1.0, 0.0]},
            {"kind": "power", "dst": 2, "srcA": 2, "value": 0.5},                       # r / Rmax
            {"kind": "power", "dst": 3, "srcA": 2, "value": -1.0},                      # Rmax / r
            {"kind": "multiply", "dst": 3, "srcA": 0, "srcB": 3},                       # Rmax (x, y, z) / r
            {"kind": "constant", "dst": 4, "constant": [angular / r_max, 0.0, angular / r_max, 0.0]},
            {"kind": "multiply", "dst": 3, "srcA": 3, "srcB": 4},                       # A (x/r, 0, z/r)
            {"kind": "constant", "dst": 4, "constant": [0.0, radial, 0.0, 0.0]},        # op 9: the churn
            {"kind": "multiply", "dst": 4, "srcA": 2, "srcB": 4},                       # (0, B r/Rmax, 0)
            {"kind": "add", "dst": 3, "srcA": 3, "srcB": 4},                            # the polar point
            {"kind": "noise", "dst": 5, "srcA": 3, "value": 1.0, "seed": 7},            # op 12: the grit
            {"kind": "noise", "dst": 6, "srcA": 3, "value": 2.9, "seed": 13},
            {"kind": "mix", "dst": 5, "srcA": 5, "srcB": 6, "value": 0.38},
            {"kind": "smoothstep", "dst": 5, "srcA": 5, "constant": [0.3, 0.78, 0.0, 0.0]},
            {"kind": "remap", "dst": 5, "srcA": 5, "value": 1, "constant": [0.0, 1.0, 0.18, 1.3]},  # streaks
            {"kind": "remap", "dst": 6, "srcA": 2, "value": 1, "constant": [R_IN / r_max, 1.0, 0.0, 1.0]},  # t
            {"kind": "ramp", "dst": 7, "srcA": 6, "constant": hot + [1.0], "constant2": mid + [1.0],
             "constant3": cool + [1.0]},                                                # op 18: the hot stop
            {"kind": "remap", "dst": 6, "srcA": 6, "value": 1, "constant": [0.0, 1.0, 1.0, 0.0]},  # 1 - t
            {"kind": "power", "dst": 6, "srcA": 6, "value": 1.3},
            {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 6},
            # the far side's streaks fade to their mean: the lens magnifies that grazing sliver of the disk into the
            # whole arc over the shadow, and magnified streaks alias into rings (the first pass's "coiled spring")
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "gradient", "dst": 1, "srcA": 0, "value": 1.0, "constant": [0.0, 0.0, 0.16, 0.45]},
            {"kind": "constant", "dst": 4, "constant": [0.62, 0.62, 0.62, 0.62]},
            {"kind": "mixBy", "dst": 5, "srcA": 4, "srcB": 5, "srcC": 1},
            {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 5},
            # ---- Doppler beaming (world space)
            {"kind": "input", "dst": 0, "input": "worldPosition"},
            {"kind": "multiply", "dst": 1, "srcA": 0, "srcB": 0},
            {"kind": "gradient", "dst": 2, "srcA": 1, "value": 1.0 / (r_max * r_max * 4.0),
             "constant": [1.0, 0.0, 1.0, 0.0]},                                         # (r / 2 Rmax)^2
            {"kind": "power", "dst": 2, "srcA": 2, "value": -0.5},                      # 2 Rmax / r
            {"kind": "multiply", "dst": 3, "srcA": 0, "srcB": 2},                       # 2 Rmax x / r
            {"kind": "gradient", "dst": 3, "srcA": 3, "value": 1.0,
             "constant": [-0.55 / r_max, 0.0, 0.0, 0.5]},                               # D = 0.5 - 1.1 x/r
            {"kind": "power", "dst": 3, "srcA": 3, "value": 1.0},
            {"kind": "ramp", "dst": 4, "srcA": 3, "constant": [0.16, 0.04, 0.02, 1.0],
             "constant2": [0.8, 0.55, 0.38, 1.0], "constant3": [2.4, 2.5, 3.0, 1.0]},  # op 34: the beaming ramp
            {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 4},
            {"kind": "input", "dst": 0, "input": "materialEmission"},
            {"kind": "swizzle", "dst": 0, "srcA": 0, "constant": [0.0, 0.0, 0.0, 0.0]},  # the routed gain
            {"kind": "multiply", "dst": 7, "srcA": 7, "srcB": 0},
            {"kind": "constant", "dst": 1, "constant": [0.0, 0.0, 0.0, 1.0]},
        ],
        "baseColor": 1, "metallic": -1, "roughness": -1, "emission": 7, "emissionIntensity": 1.0, "opacity": -1,
    }


OP_CHURN, OP_GRIT, OP_HOT, OP_BEAM = 9, 12, 18, 34


def star_program():
    """The victim's photosphere: blue-white boiling granulation, hotter at its torn tips (local x, the direction of
    travel), limb-darkened."""
    return {
        "name": "ehStar",
        "ops": [
            {"kind": "input", "dst": 0, "input": "localPosition"},
            {"kind": "input", "dst": 7, "input": "time"},
            {"kind": "constant", "dst": 6, "constant": [0.0, 0.0, 0.35, 0.0]},
            {"kind": "multiply", "dst": 6, "srcA": 7, "srcB": 6},
            {"kind": "add", "dst": 1, "srcA": 0, "srcB": 6},                            # boiling: drift in z
            {"kind": "noise", "dst": 2, "srcA": 1, "value": 2.4, "seed": 3},
            {"kind": "noise", "dst": 3, "srcA": 1, "value": 7.0, "seed": 5},
            {"kind": "mix", "dst": 2, "srcA": 2, "srcB": 3, "value": 0.35},
            {"kind": "smoothstep", "dst": 2, "srcA": 2, "constant": [0.2, 0.85, 0.0, 0.0]},
            {"kind": "ramp", "dst": 4, "srcA": 2, "constant": hexrgb("#9fb6ff", 0.75) + [1.0],
             "constant2": hexrgb(STAR_BLUE, 1.1) + [1.0], "constant3": hexrgb("#ffffff", 1.5) + [1.0]},
            {"kind": "multiply", "dst": 5, "srcA": 0, "srcB": 0},
            {"kind": "gradient", "dst": 5, "srcA": 5, "value": 1.0, "constant": [1.6, 0.0, 0.0, 0.5]},  # the tips
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 5},
            {"kind": "fresnel", "dst": 6, "value": 2.0},
            {"kind": "remap", "dst": 6, "srcA": 6, "value": 1, "constant": [0.0, 1.0, 1.0, 0.45]},  # limb darkening
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 6},
            {"kind": "input", "dst": 0, "input": "materialEmission"},
            {"kind": "swizzle", "dst": 0, "srcA": 0, "constant": [0.0, 0.0, 0.0, 0.0]},
            {"kind": "multiply", "dst": 4, "srcA": 4, "srcB": 0},
            {"kind": "constant", "dst": 1, "constant": [0.0, 0.0, 0.0, 1.0]},
        ],
        "baseColor": 1, "metallic": -1, "roughness": -1, "emission": 4, "emissionIntensity": 1.0, "opacity": -1,
    }


# ---- the victim's orbit: a circle of radius STAR_R inclined STAR_INC degrees about x, prograde with the disk, one
# lap in STAR_PERIOD seconds. At phase 0 it is at +x; a quarter lap on, behind the hole and below the disk plane (the
# lens smears it); three quarters on, in front of the hole and above it.
STAR_R, STAR_INC, STAR_PERIOD, STAR_PHASE = 14.5, 14.0, 40.0, 200.0


def orbit_at(rad, inc_deg, phi_deg):
    p, i = math.radians(phi_deg), math.radians(inc_deg)
    return (rad * math.cos(p), -rad * math.sin(p) * math.sin(i), -rad * math.sin(p) * math.cos(i))


def loop_keys(fn, period, steps, interp="smooth"):
    """A seamless loop: `steps` + 1 keys over `period`, the last equal to the first."""
    keys = []
    for k in range(steps + 1):
        v = fn(0.0 if k == steps else k / steps)
        keys.append({"time": round(period * k / steps, 4), "value": [round(c, 4) for c in v], "interp": interp})
    return keys


# Wreckage: scanned rock on slow inclined orbits, prograde. (asset key, orbit radius, inclination, period, phase,
# scale, tumble turns per lap about x, y, z -- whole turns, so the rotation loop closes)
WRECKAGE = [
    ("boulder", 22.0, -9.0, 150.0, 140.0, 2.6, (1, 1, 0)),
    ("red_boulder", 18.5, 12.0, 96.0, 320.0, 1.1, (1, 0, 1)),
    ("stone", 24.0, 4.0, 160.0, 230.0, 9.0, (0, 1, 1)),
    ("boulder", 20.0, 22.0, 120.0, 40.0, 1.3, (2, 1, 0)),
    ("red_boulder", 17.0, -18.0, 80.0, 190.0, 0.8, (1, 2, 1)),
]


def build():
    s = kit.Scene(ID, TITLE, DESIGN)
    s.environment = {
        "intensity": 0.0, "background": hexrgb(VOID), "fogColor": [0, 0, 0], "volumeDensity": 0.0,
        "volumeMaxDistance": 0.0, "skyIntensity": 1.0,
        "sky": {"enabled": True, "zenithColor": scale3(hexrgb("#0a0820"), 0.6),
                "horizonColor": scale3(hexrgb(NEB_TEAL), 0.5), "groundColor": scale3(hexrgb("#0a0618"), 0.5),
                "haze": 0.0, "sunIntensity": 0.0, "intensity": 1.0, "background": True, "useKeyLight": False},
    }
    s.composition = {"focalPoints": [{"name": "horizon", "position": list(BH), "radius": 6.0, "weight": 1.0}]}
    s.program(disk_program())
    s.program(star_program())

    # ---- the disk. UNLIT: the first pass's disk was mostly the disk light's specular sheen on black bands (F0 0.04 at
    # a grazing angle under a 1600-unit point light), which is why it was the same orange on both sides; its own
    # emission, and so the beaming, barely showed.
    for i, (r, w, spin) in enumerate(bands()):
        s.proc("band%d" % i,
               {"kind": "torus", "majorRadius": round(r, 4), "minorRadius": round(w * 1.25, 4),
                "majorSegments": 256, "minorSegments": 8},
               material={"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [1.0, 1.0, 1.0], "emissiveIntensity": 5.0,
                         "roughness": 1.0, "metallic": 0.0, "program": "ehDisk", "unlit": True},
               deformers=[{"kind": "twist", "amount": 0.0, "speed": round(spin * 3.14159265 / 180.0, 5),
                           "axis": [0, 1, 0]},
                          {"kind": "noise", "amount": 0.0, "scale": 0.35, "speed": 0.6, "seed": 40 + i,
                           "axisMask": [0, 1, 0], "space": "world"}],
               transform={"position": [0, 0.0015 * (i % 3), 0], "rotation": [0, 0, 0], "scale": [1, 0.035, 1]})

    # ---- the far sky: the galaxy's band of stars over a deep indigo-to-teal sky (the lens bends both). The first
    # pass's matte-painted nebula plane cost 8 ms a frame at 1080p (a full-screen interpreted fBm) for a background
    # that the vignette mostly hid; the sky's own star field is nearly free.
    s.effect("stars", "stars", ("world",), parameters={
        "brightness": 14.0, "density": 0.2, "magnitudeSlope": 5.0, "colorSpread": 0.6, "twinkle": 0.08,
        "twinkleRate": 2.0, "horizonFade": 0.01, "band": 3.0, "bandTilt": 0.35, "daylight": 0.0,
        "wholeSky": True})   # ADR-1350: open space has no horizon


    # ---- the hole: a large clean shadow, a razor photon ring the lensed disk hugs
    s.effect("bh", "gravLens", ("world",), parameters={
        "einsteinRadius": 6.0, "mode": 1, "horizonScale": 0.8, "chroma": 0.003,
        "ringColor": hexrgb("#fff0d8"), "photonRing": 7.0, "ringWidth": 0.014, "falloffRadius": 3.4,
        "feather": 0.8, "offsetX": BH[0], "offsetY": BH[1], "offsetZ": BH[2]})

    # ---- the victim star, stretched along its orbit (local x is the direction of travel)
    star0 = orbit_at(STAR_R, STAR_INC, STAR_PHASE)
    s.proc("star", {"kind": "sphere", "radius": 1.0, "segments": 48, "rings": 24},
           material={"baseColor": [0.0, 0.0, 0.0], "emissiveColor": [1.0, 1.0, 1.0], "emissiveIntensity": 9.0,
                     "roughness": 1.0, "metallic": 0.0, "program": "ehStar"},
           deformers=[{"kind": "noise", "amount": 0.06, "scale": 1.6, "speed": 0.8, "seed": 77,
                       "axisMask": [1, 1, 1]}],
           position=star0, scale=[1.7, 0.85, 0.85])
    star_pos = loop_keys(lambda u: orbit_at(STAR_R, STAR_INC, STAR_PHASE + 360.0 * u), STAR_PERIOD, 32)
    # yaw so local +x follows the velocity (-sin phi, ., -cos phi): yaw = 90 + phi. The angle is unwrapped: the last
    # key is the first plus one whole turn, the same orientation, so the loop closes.
    star_rot = [{"time": round(STAR_PERIOD * k / 32, 4),
                 "value": [0.0, round(90.0 + STAR_PHASE + 360.0 * k / 32, 3), 0.0], "interp": "linear"}
                for k in range(33)]
    s.track("nodes/star/position", star_pos, loop=STAR_PERIOD)
    s.track("nodes/star/rotation", star_rot, loop=STAR_PERIOD)
    # its light: the cold counter-light on the wreckage
    s.light("starLight", "point", position=list(star0), color=hexrgb(STAR_BLUE), intensity=900.0,
            range=40.0, radius=1.0, castsShadow=False, volumetric=0.0)
    s.track("lights/starLight/position", star_pos, loop=STAR_PERIOD)
    # ---- its corona: soft light round the photosphere, so the star glows instead of ending at a hard edge
    s.particles("corona", capacity=1200, seed=33, shape="sphere", position=list(star0), extent=[1.1, 0.7, 0.7],
                direction=[0, 1, 0], spawnRate=500.0, lifetimeMin=0.8, lifetimeMax=1.6, spread=1.0, speedMin=0.2,
                speedMax=0.6, gravity=[0, 0, 0], drag=0.5, turbulence=0.2, sizeStart=1.0, sizeEnd=2.2,
                sizeVariance=0.4, colorStart=hexrgb(STAR_BLUE) + [0.14], colorEnd=hexrgb("#7f9cff") + [0.0],
                emissive=2.0, blend="additive")
    # ---- the tidal stream: plasma stripped from the star, falling in on a spiral and heating as it goes
    s.particles("stream", capacity=24000, seed=31, shape="sphere", position=list(star0),
                extent=[0.5, 0.3, 0.5], direction=[0, 1, 0], spawnRate=2200.0, lifetimeMin=3.0, lifetimeMax=6.5,
                spread=0.05, speedMin=4.0, speedMax=4.8, gravity=[0, 0, 0], drag=0.08, turbulence=0.08,
                turbulenceScale=0.5, turbulenceSpeed=0.3, attractorPosition=list(BH), attractorStrength=3.2,
                attractorRadius=60.0, orbit=ORBIT_SIGN * 4.2, sizeStart=0.16, sizeEnd=0.04, sizeVariance=0.5,
                sizeSkew=2.0, colorStart=hexrgb(STAR_BLUE) + [0.3], colorEnd=hexrgb(GOLD) + [0.0], emissive=2.4,
                blend="additive", velocityStretch=0.35, stretchMax=0.35)
    s.particles("streamers", capacity=600, seed=37, shape="sphere", position=list(star0),
                extent=[0.4, 0.3, 0.4], direction=[0, 1, 0], spawnRate=40.0, lifetimeMin=4.0, lifetimeMax=7.0,
                spread=0.06, speedMin=4.0, speedMax=4.8, gravity=[0, 0, 0], drag=0.08, turbulence=0.05,
                attractorPosition=list(BH), attractorStrength=3.2, attractorRadius=60.0, orbit=ORBIT_SIGN * 4.2,
                sizeStart=0.09, sizeEnd=0.03, colorStart=hexrgb("#e8f0ff") + [1.0], colorEnd=hexrgb(EMBER) + [0.0],
                emissive=6.0, blend="additive", trailEnabled=True, trailLength=32, trailStride=3, trailWidth=0.7,
                trailTaper=0.1, trailFade=0.0, trailTint=hexrgb(GOLD))
    # (born moving along the orbit, a little ahead of the star: the stream leads it in, spiralling)
    star_dir = loop_keys(lambda u: (-math.sin(math.radians(STAR_PHASE + 360.0 * u)),
                                    -math.cos(math.radians(STAR_PHASE + 360.0 * u)) * math.sin(math.radians(STAR_INC)),
                                    -math.cos(math.radians(STAR_PHASE + 360.0 * u)) * math.cos(math.radians(STAR_INC))),
                         STAR_PERIOD, 32)
    s.track("particles/corona/position", star_pos, loop=STAR_PERIOD)
    for sid in ("stream", "streamers", "fragments"):
        s.track("particles/%s/position" % sid, star_pos, loop=STAR_PERIOD)
        s.track("particles/%s/direction" % sid, star_dir, loop=STAR_PERIOD)

    # ---- jets: particle beams leaving the poles, a steady whisper that the kick fires
    for jid, sign in (("jetUp", 1.0), ("jetDown", -1.0)):
        s.particles(jid, capacity=9000, seed=61 if sign > 0 else 67, shape="sphere", position=[0, 1.2 * sign, 0],
                    extent=[0.15, 0.1, 0.15], direction=[0, sign, 0], spawnRate=1400.0, lifetimeMin=1.4,
                    lifetimeMax=2.6, spread=0.018, speedMin=20.0, speedMax=30.0, gravity=[0, 0, 0], drag=0.0,
                    turbulence=0.15, turbulenceScale=0.12, sizeStart=0.025, sizeEnd=0.09, sizeVariance=0.4,
                    colorStart=hexrgb("#c8d8ff") + [0.18], colorEnd=hexrgb("#5a4aff") + [0.0], emissive=1.1,
                    blend="additive", velocityStretch=3.0, stretchMax=3.0)

    # ---- the gas: matter spiralling in, heating as it falls. Additive and streaked along its orbit.
    s.particles("gasInner", capacity=30000, seed=19, shape="disc", position=[0, 0, 0], extent=[7.0, 0.0, 7.0],
                direction=[0, 1, 0], spawnRate=4000.0, lifetimeMin=1.6, lifetimeMax=3.4, spread=1.0,
                speedMin=0.0, speedMax=0.02, gravity=[0, 0, 0], drag=0.5, turbulence=0.02,
                turbulenceScale=0.5, turbulenceSpeed=0.5, attractorPosition=[0, 0, 0], attractorStrength=0.55,
                attractorRadius=8.0, orbit=ORBIT_SIGN * 4.5, sizeStart=0.022, sizeEnd=0.014, sizeVariance=0.5,
                sizeSkew=2.0, colorStart=hexrgb(GOLD) + [0.0], colorEnd=hexrgb(WHITE_HOT) + [0.35], emissive=0.6,
                blend="additive", velocityStretch=0.8, stretchMax=0.5)
    # the approaching side is brighter (Doppler beaming: the disk turns toward the camera on the LEFT)
    s.particles("beaming", capacity=14000, seed=23, shape="disc", position=[-5.2, 0.02, 1.0], extent=[3.2, 0.0, 4.2],
                direction=[0, 1, 0], spawnRate=4500.0, lifetimeMin=0.8, lifetimeMax=1.8, spread=1.0,
                speedMin=0.0, speedMax=0.02, gravity=[0, 0, 0], drag=0.5, turbulence=0.02,
                attractorPosition=[0, 0, 0], attractorStrength=0.5, attractorRadius=12.0, orbit=ORBIT_SIGN * 4.0,
                sizeStart=0.022, sizeEnd=0.014, sizeVariance=0.5, sizeSkew=2.0,
                colorStart=hexrgb("#f4f6ff") + [0.0], colorEnd=hexrgb(WHITE_HOT) + [0.55], emissive=1.8,
                blend="additive", velocityStretch=0.8, stretchMax=0.45)
    # ---- note fragments: each note tears a clump off the star; it leaves along the orbit at a speed the pitch sets
    # (a low note keeps its speed and swings wide; a high one loses it and plunges), trailing light
    s.particles("fragments", capacity=1500, seed=29, shape="sphere", position=list(star0),
                extent=[0.4, 0.3, 0.4], direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=3.0, lifetimeMax=4.5,
                spread=0.12, speedMin=5.0, speedMax=5.8, gravity=[0, 0, 0], drag=0.05, turbulence=0.05,
                attractorPosition=list(BH), attractorStrength=3.2, attractorRadius=60.0, orbit=ORBIT_SIGN * 4.2,
                sizeStart=0.12, sizeEnd=0.03, colorStart=hexrgb("#f2f6ff") + [1.0],
                colorEnd=hexrgb(GOLD) + [0.0], emissive=9.0, blend="additive",
                trailEnabled=True, trailLength=24, trailStride=2, trailWidth=0.35, trailTaper=0.1, trailFade=0.0)
    # ---- the flare (snare): a burst from the hot spot on the approaching side
    s.particles("flare", capacity=4000, seed=41, shape="sphere", position=[-4.6, 0.05, 1.2],
                extent=[0.35, 0.05, 0.35], direction=[0, 0.3, 1], spawnRate=0.0, lifetimeMin=0.35, lifetimeMax=0.9,
                spread=0.25, speedMin=4.0, speedMax=9.0, gravity=[0, 0, 0], drag=0.6, turbulence=0.2,
                turbulenceScale=0.8, attractorPosition=[0, 0, 0], attractorStrength=1.2, attractorRadius=12.0,
                orbit=ORBIT_SIGN * 6.0, sizeStart=0.06, sizeEnd=0.0, colorStart=hexrgb(WHITE_HOT) + [1.0],
                colorEnd=hexrgb(EMBER) + [0.0], emissive=10.0, blend="additive", velocityStretch=1.2, stretchMax=0.8)
    # ---- sparks (hat): glints scattered over the disk, a few frames each
    s.particles("sparks", capacity=2000, seed=53, shape="disc", position=[0, 0.05, 0], extent=[11.0, 0.0, 11.0],
                direction=[0, 1, 0], spawnRate=0.0, lifetimeMin=0.08, lifetimeMax=0.22, spread=1.0,
                speedMin=0.0, speedMax=0.2, gravity=[0, 0, 0], drag=0.0, turbulence=0.0,
                sizeStart=0.05, sizeEnd=0.0, colorStart=hexrgb("#ffffff") + [1.0], colorEnd=hexrgb(GOLD) + [0.0],
                emissive=16.0, blend="additive")

    # ---- the wreckage: scanned rock on slow inclined orbits, tumbling, rimmed by the disk and the star
    for n, (key, rad, inc, period, phase, sc, turns) in enumerate(WRECKAGE):
        name = "wreck%d" % n
        s.gltf(name, key, orbit_at(rad, inc, phase), scale=sc, tint=hexrgb("#8a8580"), roughness=1.0)
        s.track("nodes/%s/position" % name,
                loop_keys(lambda u, rad=rad, inc=inc, phase=phase: orbit_at(rad, inc, phase + 360.0 * u), period, 24),
                loop=period)
        s.track("nodes/%s/rotation" % name,
                [{"time": 0.0, "value": [0.0, 0.0, 0.0], "interp": "linear"},
                 {"time": period, "value": [360.0 * t for t in turns], "interp": "linear"}], loop=period)

    # ---- light: the inner disk is the light source; a faint cold fill keeps the debris' shadow side from black
    s.light("disk", "point", position=[0, 0.6, 0], color=hexrgb(GOLD), intensity=2200.0, range=70.0,
            radius=3.0, castsShadow=False, volumetric=0.0)
    s.light("fill", "directional", direction=[0.3, -0.25, -1.0], color=hexrgb("#7f95c9"), intensity=0.06,
            castsShadow=False)

    # ---- camera: an arc a few degrees above the plane, the hole on the right third
    s.camera["fov"] = 32.0
    s.arc_camera(BH, radius=34.0, height=2.6, period=160.0, centre_deg=-8.0, sweep_deg=32.0, steps=16,
                 side=6.0, lift=1.4, height_bob=0.35)

    # ---- the instrument -------------------------------------------------------------------------------------------
    # pitch -> plunge: lastPitch 0.30 (about D3) is 0, 0.72 (about C6) is 1
    s.map(M("plunge", [("lastPitch", 1.0)], "mean", -0.30 / 0.42, 1.0 / 0.42))
    s.map(M("heat", [("sustain", 1.0), ("energy", 0.6)], "max"))
    s.map(M("churn", [("density", 1.0), ("rhythm", 0.6)], "max", 0.0, 1.4))
    s.map(M("grit", [("roughness", 1.0), ("energy", 0.4), ("inharmonicity", 2.0, True)], "product", -0.3, 6.5))

    # mass: the bass swells the lens (medium attack, slow release: heavy things move slowly), and the star flares and
    # bleeds faster
    s.route(R("bass", "fx/bh/einsteinRadius", 1.8, attackMs=50, decayMs=900),
            R("bass", "fx/bh/horizonScale", 0.03, attackMs=50, decayMs=900),
            R("bass", "particles/stream/spawnRate", 2400.0, attackMs=50, decayMs=900),
            R("bass", "procedural/star/material/emissive", 5.0, attackMs=50, decayMs=900))
    # heat: sustained sound brightens the disk and thickens its gas (the whole disk, slowly)
    for i in range(N_BANDS):
        s.route(R("visual.heat", "procedural/band%d/material/emissive" % i, 4.0, **SLOW))
    s.route(R("visual.heat", "particles/gasInner/spawnRate", 1600.0, **SLOW),
            R("visual.heat", "particles/gasInner/emissive", 3.0, **SLOW),
            R("visual.heat", "particles/beaming/emissive", 3.0, **SLOW))
    # timbre: a bright sound turns the hot inner edge from gold-white to blue-white (the ramp's hot stop)
    hot_stop = "material/ehDisk/op/%d/ramp/constant" % OP_HOT
    s.route(R("brightness", hot_stop, -0.35, comp=0, **MEDIUM),
            R("brightness", hot_stop, 0.15, comp=1, **MEDIUM),
            R("brightness", hot_stop, 0.9, comp=2, **MEDIUM))
    # roughness: the disk's streaks break into clumps and the gas boils
    s.route(R("visual.grit", "material/ehDisk/op/%d/noise/value" % OP_GRIT, 1.6, **FAST),
            R("visual.grit", "particles/gasInner/turbulence", 1.0, **FAST))
    for i in range(N_BANDS):
        s.route(R("visual.grit", "procedural/band%d/deform/2/amount" % i, 0.12, **FAST))
    # the playing: busy notes churn the disk -- finer, more numerous streaks across the radius
    s.route(R("visual.churn", "material/ehDisk/op/%d/constant/constant" % OP_CHURN, 22.0, comp=1, **MEDIUM),
            R("visual.churn", "particles/gasInner/orbit", ORBIT_SIGN * 2.0, **MEDIUM))
    # melodic: the fragment's speed follows the latest note with no lag (it must be set when the burst fires): a high
    # note is born slow and plunges, a low one swings wide; a chord tears a wider clump
    s.route(R("visual.plunge", "particles/fragments/speedMin", -3.4, attackMs=0, decayMs=0),
            R("visual.plunge", "particles/fragments/speedMax", -3.4, attackMs=0, decayMs=0),
            R("polyphony", "particles/fragments/extent", 1.6, comp=0, **FAST),
            R("polyphony", "particles/fragments/extent", 1.6, comp=2, **FAST),
            R("noteOn", "particles/fragments/burst", 10.0, depth="lastVelocity", attackMs=0, decayMs=140),
            R("noteEnv", "procedural/star/material/emissive", 6.0, attackMs=0, decayMs=300))
    # kick: the photon ring flashes, the jets fire
    s.route(R("kick", "fx/bh/photonRing", 26.0, **HIT),
            R("kick", "particles/jetUp/burst", 160.0, attackMs=0, decayMs=90),
            R("kick", "particles/jetDown/burst", 160.0, attackMs=0, decayMs=90),
            R("kick", "particles/jetUp/emissive", 2.5, attackMs=0, decayMs=500),
            R("kick", "particles/jetDown/emissive", 2.5, attackMs=0, decayMs=500),
            R("kick", "post/lens/chromaticAberration", 0.06, attackMs=0, decayMs=160),
            # a gravitational ripple: a shock ring racing out from the hole (ADR-1065), centred where the arc
            # camera keeps the hole on screen
            R("kickEnv", "post/shock/amount", 34.0, attackMs=0, decayMs=0),
            R("kickEnv", "post/shock/radius", -1.4, op="replace", offset=-1.0, attackMs=0, decayMs=0))
    # snare: a flare from the hot spot, and the star sheds a burst into its stream
    s.route(R("snare", "particles/flare/burst", 260.0, attackMs=0, decayMs=40),
            R("snare", "particles/stream/burst", 900.0, attackMs=0, decayMs=40),
            R("snare", "particles/streamers/burst", 12.0, attackMs=0, decayMs=40),
            R("snare", "post/bloom/intensity", 0.22, attackMs=0, decayMs=260))
    # hat: sparks
    s.route(R("hat", "particles/sparks/burst", 45.0, attackMs=0, decayMs=30))
    # tension: dissonance splits the lens's colours
    s.route(R("tension", "fx/bh/chroma", 0.03, **MEDIUM))

    SHOCK_UV = [round(v, 3) for v in s.project(list(BH))[:2]]
    s.params_({
        "post/bloom/intensity": 0.45, "post/bloom/threshold": 0.9, "post/bloom/emissionWeight": 0.8,
        "post/halation/enabled": True, "post/halation/intensity": 0.25, "post/halation/warmth": 0.6,
        "post/anamorphic/enabled": True, "post/anamorphic/intensity": 0.08, "post/anamorphic/stretch": 0.7,
        "post/lens/chromaticAberration": 0.02, "post/output/vignette": 0.55, "post/output/grain": 0.022,
        "post/grade/contrast": 1.12, "post/grade/saturation": 1.05,
        "camera/lens/focalLength": 38.0, "camera/exposure/compensation": -0.4,
        "post/dof/enabled": True, "post/dof/physical": True, "camera/lens/aperture": 2.8,
        "camera/focus/mode": 0, "camera/lens/focusDistance": 34.0,
        "post/shock/amount": 0.0, "post/shock/radius": 1.4, "post/shock/width": 0.07, "post/shock/chroma": 0.5,
        "post/shock/centerX": SHOCK_UV[0], "post/shock/centerY": SHOCK_UV[1],
    })
    # ---- the evaluator's screen regions, projected through the camera at t = 0, and the performer's baseline
    s.region("horizon", centre=list(BH), radius=4.5)
    s.region_ring("disk", list(BH), 15.0)
    s.region_ring("lensed", list(BH), 8.0, axis="z")
    s.region("background", box=[0.0, 0.0, 1.0, 0.2])
    s.response = {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.55, "attack": 1.2, "release": 1.5}
    return s
